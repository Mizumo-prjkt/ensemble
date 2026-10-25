use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use ensemble_core::{
    init_debug_system, AgenticData, Ao3Project, Chapter, LoreConflict, LoreEntity, LoreGraph,
    LoreGraphBox, LoreGraphFollowup, LoreGraphNode, LoreHistoryEvent, LorePlan, LoreReference,
    ProjectFormatType, ProjectSerializer, ValidationStage, ZstdArchive,
};
use tempfile::tempdir;

#[test]
fn test_project_archive_full_suite() {
    init_debug_system();

    let temp_dir = tempdir().expect("Failed to create temp dir");
    let proj_path = temp_dir.path().join("test_work.ao3proj");

    // 1. Create a project
    let mut original_proj = Ao3Project::new();
    original_proj.title = "My Test AO3 Epic".to_string();
    original_proj.author = "TestAuthor".to_string();
    original_proj.work_skin_css =
        ".custom-class { color: #bd93f9; font-weight: bold; }".to_string();
    original_proj.history_log = "Initial commit: created story.".to_string();

    original_proj.chapters.clear();
    let mut ch1 = Chapter::new("Chapter 1: The Gathering", 1);
    ch1.html = "<p>It was a dark and stormy night...</p>".to_string();
    original_proj.chapters.push(ch1.clone());

    let mut ch2 = Chapter::new("Chapter 2: The Journey", 2);
    ch2.html = "<p>They ventured into the unknown realm.</p>".to_string();
    original_proj.chapters.push(ch2.clone());

    // 2. Save project to .ao3proj archive
    ProjectSerializer::save(&proj_path, &original_proj).expect("Save failed");
    assert!(proj_path.exists());
    assert!(fs::metadata(&proj_path).unwrap().len() > 0);

    // 3. Verify format detection
    assert!(ZstdArchive::is_zstd_archive(&proj_path));

    // 4. Load project back using 3-stage validation pipeline
    let mut loaded_proj = Ao3Project::new();
    let report = ProjectSerializer::load_with_validation(&proj_path, &mut loaded_proj, None);
    assert!(report.success, "Load failed: {}", report.summary_error);
    assert!(!report.is_legacy);
    assert_eq!(report.failed_stage, ValidationStage::None);
    assert!(
        report.format_type == ProjectFormatType::ModernArchiveZip
            || report.format_type == ProjectFormatType::ModernArchiveZstd
    );
    assert!(!loaded_proj.is_legacy_format);

    // 5. Verify integrity of loaded contents
    assert_eq!(loaded_proj.title, original_proj.title);
    assert_eq!(loaded_proj.author, original_proj.author);
    assert_eq!(loaded_proj.work_skin_css, original_proj.work_skin_css);
    assert_eq!(loaded_proj.history_log, original_proj.history_log);
    assert_eq!(loaded_proj.chapters.len(), 2);
    assert_eq!(loaded_proj.chapters[0].title, ch1.title);
    assert_eq!(loaded_proj.chapters[0].html, ch1.html);
    assert_eq!(loaded_proj.chapters[1].title, ch2.title);
    assert_eq!(loaded_proj.chapters[1].html, ch2.html);

    // 6. Test loading existing user deflate archive if present
    let real_file_path =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../test/units/test1.ao3proj");
    if real_file_path.exists() {
        let mut real_proj = Ao3Project::new();
        let real_report =
            ProjectSerializer::load_with_validation(&real_file_path, &mut real_proj, None);
        assert!(
            real_report.success,
            "Failed to load real test1.ao3proj: {}",
            real_report.summary_error
        );
        assert!(!real_report.is_legacy);
        assert!(!real_proj.chapters.is_empty());
    }

    // 7. Test Stage 1: Magic Bytes Check on corrupt/invalid file (e.g. PNG / random binary)
    let fake_png_path = temp_dir.path().join("fake_image.ao3proj");
    fs::write(
        &fake_png_path,
        b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDRRandomNotAnArchiveData",
    )
    .unwrap();
    {
        let mut dummy = Ao3Project::new();
        let invalid_report =
            ProjectSerializer::load_with_validation(&fake_png_path, &mut dummy, None);
        assert!(!invalid_report.success);
        assert_eq!(invalid_report.failed_stage, ValidationStage::MagicBytesCheck);
        assert!(invalid_report.summary_error.contains("Magic Bytes"));
        assert!(!invalid_report.detailed_explanation.is_empty());
        assert!(invalid_report
            .detailed_explanation
            .contains("INVALID_MAGIC_BYTES"));
        assert!(invalid_report.header_hex.starts_with("89 50 4E 47"));
    }

    // 8. Test Empty File Check (0 bytes)
    let empty_path = temp_dir.path().join("empty.ao3proj");
    fs::write(&empty_path, b"").unwrap();
    {
        let mut dummy = Ao3Project::new();
        let empty_report = ProjectSerializer::load_with_validation(&empty_path, &mut dummy, None);
        assert!(!empty_report.success);
        assert_eq!(empty_report.failed_stage, ValidationStage::MagicBytesCheck);
        assert!(empty_report.summary_error.contains("0 bytes"));
    }

    // 9. Test Non-existent File (Stage 0: FileAccess)
    {
        let mut dummy = Ao3Project::new();
        let missing_report = ProjectSerializer::load_with_validation(
            &temp_dir.path().join("does_not_exist.ao3proj"),
            &mut dummy,
            None,
        );
        assert!(!missing_report.success);
        assert_eq!(missing_report.failed_stage, ValidationStage::FileAccess);
        assert!(missing_report.summary_error.contains("not found"));
    }

    // 10. Test Stage 2: Structural Integrity Check on archive missing metadata.xml
    let missing_manifest_path = temp_dir.path().join("missing_manifest.ao3proj");
    {
        let mut bad_entries = BTreeMap::new();
        bad_entries.insert(
            "readme.txt".to_string(),
            b"This archive has no metadata.xml descriptor".to_vec(),
        );
        bad_entries.insert("chapter_1.html".to_string(), b"<p>orphan</p>".to_vec());
        ZstdArchive::compress_archive(&missing_manifest_path, &bad_entries).unwrap();
    }
    {
        let mut dummy = Ao3Project::new();
        let struct_report =
            ProjectSerializer::load_with_validation(&missing_manifest_path, &mut dummy, None);
        assert!(!struct_report.success);
        assert_eq!(struct_report.failed_stage, ValidationStage::StructureCheck);
        assert!(struct_report.summary_error.contains("metadata.xml"));
        assert!(struct_report
            .detailed_explanation
            .contains("MISSING_MANIFEST"));
        assert!(struct_report.detailed_explanation.contains("readme.txt"));
    }

    // 11. Test Legacy Format: JSON v1 compatibility and subsequent upgrade to .ao3proj
    let legacy_json_path = temp_dir.path().join("legacy_story.json");
    {
        let legacy_content = r#"{
  "formatVersion": 1,
  "title": "Legacy Classic Fanfic",
  "workSkinCss": ".legacy { font-style: italic; }",
  "chapters": [
    {
      "id": "11111111-2222-3333-4444-555555555555",
      "title": "Legacy Prologue",
      "order": 1,
      "html": "<p>Old style paragraph</p>"
    }
  ]
}
"#;
        fs::write(&legacy_json_path, legacy_content).unwrap();
    }
    {
        let mut legacy_proj = Ao3Project::new();
        let legacy_report =
            ProjectSerializer::load_with_validation(&legacy_json_path, &mut legacy_proj, None);
        assert!(legacy_report.success);
        assert!(legacy_report.is_legacy);
        assert_eq!(legacy_report.format_type, ProjectFormatType::LegacyJsonV1);
        assert!(legacy_proj.is_legacy_format);
        assert_eq!(legacy_proj.title, "Legacy Classic Fanfic");
        assert_eq!(legacy_proj.chapters.len(), 1);
        assert_eq!(legacy_proj.chapters[0].title, "Legacy Prologue");

        // Upgrade test: Save legacy project to .ao3proj
        let upgraded_path = temp_dir.path().join("upgraded_story.ao3proj");
        ProjectSerializer::save(&upgraded_path, &legacy_proj).unwrap();

        // Reload upgraded file
        let mut reloaded_proj = Ao3Project::new();
        let up_report =
            ProjectSerializer::load_with_validation(&upgraded_path, &mut reloaded_proj, None);
        assert!(up_report.success);
        assert!(!up_report.is_legacy);
        assert_eq!(reloaded_proj.title, "Legacy Classic Fanfic");
        assert_eq!(reloaded_proj.chapters.len(), 1);
    }

    // 12. Test Emergency Autosave (.ao3projasve) saving, 3-stage validation, and crash recovery
    {
        let mut autosave_proj = Ao3Project::new();
        autosave_proj.title = "Unsaved In-Progress Masterpiece".to_string();
        autosave_proj.author = "CreativeWriter".to_string();
        autosave_proj.work_skin_css = ".autosave-style { font-family: monospace; }".to_string();
        autosave_proj.chapters.clear();
        let mut auto_ch = Chapter::new("Chapter 1: The Crash Event", 1);
        auto_ch.html =
            "<p>Lines written right before power loss or unexpected shutdown...</p>".to_string();
        autosave_proj.chapters.push(auto_ch);

        let autosave_path = temp_dir.path().join("in_progress_work.ao3projasve");
        ProjectSerializer::save(&autosave_path, &autosave_proj).unwrap();
        assert!(autosave_path.exists());

        let mut recovered_proj = Ao3Project::new();
        let rec_report =
            ProjectSerializer::load_with_validation(&autosave_path, &mut recovered_proj, None);
        assert!(rec_report.success);
        assert!(
            rec_report.format_type == ProjectFormatType::ModernArchiveZip
                || rec_report.format_type == ProjectFormatType::ModernArchiveZstd
        );
        assert_eq!(recovered_proj.title, "Unsaved In-Progress Masterpiece");
        assert_eq!(recovered_proj.chapters.len(), 1);
        assert_eq!(
            recovered_proj.chapters[0].title,
            "Chapter 1: The Crash Event"
        );
        assert!(recovered_proj.chapters[0].html.contains("power loss"));
        assert!(recovered_proj.work_skin_css.contains(".autosave-style"));
    }

    // 13. Test agentic-datas attribute, encrypted agentifiers, 5MB shared & 20MB localized caps
    {
        let mut proj = Ao3Project::new();
        proj.title = "Agentic Collaborative Story".to_string();
        proj.author = "Antigravity & Claude".to_string();

        let agentic = &mut proj.agentic_data;

        // A. Test shared storage & 5MB cap
        assert_eq!(agentic.total_shared_bytes(), 0);
        let small_shared = b"Shared co-author plot outline and world lore.".to_vec();
        agentic
            .set_shared_file("plot_outline.md", small_shared.clone())
            .unwrap();
        assert!(agentic.has_shared_file("plot_outline.md"));
        assert_eq!(
            agentic.shared_file("plot_outline.md").unwrap(),
            small_shared.as_slice()
        );
        assert_eq!(agentic.total_shared_bytes(), small_shared.len());

        // Try to exceed 5MB shared cap
        let oversized_shared = vec![b'X'; 5 * 1024 * 1024 + 1024];
        let quota_err = agentic
            .set_shared_file("huge_data.bin", oversized_shared)
            .unwrap_err();
        assert!(quota_err.to_lowercase().contains("quota exceeded"));
        assert!(quota_err.contains("5 MB"));

        // B. Test localized agent storage & 20MB cap
        assert_eq!(agentic.total_localized_bytes(), 0);
        let claude_memory =
            b"{\"last_review\": \"chapter 1 paced well\", \"notes\": \"expand dialogue\"}".to_vec();
        agentic
            .set_agent_file("claude", "memory.json", claude_memory.clone())
            .unwrap();
        assert!(agentic.has_agent_file("claude", "memory.json"));
        assert_eq!(
            agentic.agent_file("claude", "memory.json").unwrap(),
            claude_memory.as_slice()
        );

        let antigravity_memory =
            b"{\"character_arcs\": {\"hero\": \"transformative\"}}".to_vec();
        agentic
            .set_agent_file("antigravity", "brain.json", antigravity_memory.clone())
            .unwrap();
        assert!(agentic.has_agent_file("antigravity", "brain.json"));
        assert!(agentic.agent_names().contains(&"claude".to_string()));
        assert!(agentic.agent_names().contains(&"antigravity".to_string()));

        // Try to exceed 20MB localized cap
        let oversized_agent = vec![b'Z'; 20 * 1024 * 1024 + 1024];
        let loc_quota_err = agentic
            .set_agent_file("cursor", "big.bin", oversized_agent)
            .unwrap_err();
        assert!(loc_quota_err.to_lowercase().contains("quota exceeded"));
        assert!(loc_quota_err.contains("20 MB"));

        // C. Test agent_model_history.xml
        agentic.append_history_entry(
            "antigravity",
            "gemini-2.5-pro",
            "co-author",
            "init",
            "Drafted chapter 1 premise.",
            -1,
            0,
            "",
        );
        agentic.append_history_entry(
            "claude",
            "claude-3-7-sonnet",
            "reviewer",
            "review",
            "Reviewed chapter 1 pacing and character voice.",
            -1,
            0,
            "",
        );
        let history_xml = agentic.raw_model_history_xml();
        assert!(history_xml.contains("<agent-model-history"));
        assert!(history_xml.contains("gemini-2.5-pro"));
        assert!(history_xml.contains("claude-3-7-sonnet"));
        assert!(agentic.history_entries().len() >= 3);

        // D. Test encrypted agentifiers.json
        agentic.register_agent_identifier(
            "antigravity",
            "gemini",
            "co-author",
            "Primary architect",
        );
        agentic.register_agent_identifier("claude", "anthropic", "reviewer", "Style editor");
        let encrypted_agentifiers = agentic.export_encrypted_agentifiers(b"");
        assert!(!encrypted_agentifiers.is_empty());
        let enc_str = String::from_utf8_lossy(&encrypted_agentifiers);
        assert!(enc_str.contains("aes-256-cbc-hmac-sha256"));
        assert!(!enc_str.contains("Primary architect"));

        // Verify decryption roundtrip
        let mut decrypt_target = AgenticData::new();
        decrypt_target
            .import_encrypted_agentifiers(&encrypted_agentifiers, b"")
            .unwrap();
        let dec_obj = decrypt_target.agentifiers_json();
        assert_eq!(dec_obj["format"], "ensemble-agentifiers-v1");
        assert!(dec_obj["agents"].get("antigravity").is_some());
        assert!(dec_obj["agents"].get("claude").is_some());

        // E. Save full project archive to disk and verify all agentic container entries
        let agentic_proj_path = temp_dir.path().join("agentic_story.ao3proj");
        ProjectSerializer::save(&agentic_proj_path, &proj).unwrap();
        assert!(agentic_proj_path.exists());

        let raw_entries = ZstdArchive::decompress_archive(&agentic_proj_path).unwrap();
        assert!(raw_entries.contains_key("agentic-datas/index.json"));
        assert!(raw_entries.contains_key("agentic-datas/agent_model_history.xml"));
        assert!(raw_entries.contains_key("agentic-datas/agentifiers.json"));
        assert!(raw_entries.contains_key("agentic-datas/shared/plot_outline.md"));
        assert!(raw_entries.contains_key("agentic-datas/claude/memory.json"));
        assert!(raw_entries.contains_key("agentic-datas/antigravity/brain.json"));
        assert_eq!(
            raw_entries["agentic-datas/shared/plot_outline.md"],
            small_shared
        );
        assert_eq!(
            raw_entries["agentic-datas/claude/memory.json"],
            claude_memory
        );
        assert_eq!(
            raw_entries["agentic-datas/antigravity/brain.json"],
            antigravity_memory
        );
        assert!(raw_entries.contains_key("metadata.xml"));
        assert!(String::from_utf8_lossy(&raw_entries["metadata.xml"]).contains("<agentic-datas"));

        // F. Load with validation pipeline (Stages 1-3)
        let mut reloaded_proj = Ao3Project::new();
        let load_report = ProjectSerializer::load_with_validation(
            &agentic_proj_path,
            &mut reloaded_proj,
            None,
        );
        assert!(load_report.success);
        assert_eq!(reloaded_proj.title, "Agentic Collaborative Story");
        assert!(reloaded_proj
            .agentic_data
            .has_shared_file("plot_outline.md"));
        assert_eq!(
            reloaded_proj
                .agentic_data
                .shared_file("plot_outline.md")
                .unwrap(),
            small_shared.as_slice()
        );
        assert!(reloaded_proj
            .agentic_data
            .has_agent_file("claude", "memory.json"));
        assert_eq!(
            reloaded_proj
                .agentic_data
                .agent_file("claude", "memory.json")
                .unwrap(),
            claude_memory.as_slice()
        );
        assert!(reloaded_proj
            .agentic_data
            .has_agent_file("antigravity", "brain.json"));
        assert_eq!(
            reloaded_proj
                .agentic_data
                .agent_file("antigravity", "brain.json")
                .unwrap(),
            antigravity_memory.as_slice()
        );
        assert!(reloaded_proj.agentic_data.history_entries().len() >= 3);
    }

    // 14. Test Lore Graph, unique .grph magic bytes, AO3 tags, and lore/ directory structure
    {
        let mut lore_proj = Ao3Project::new();
        lore_proj.title = "Lore & Worldbuilding Masterpiece".to_string();
        lore_proj.author = "Author & Co-Authors".to_string();
        lore_proj.rating = "Teen And Up Audiences".to_string();
        lore_proj.archive_warnings = vec![
            "Creator Chose Not To Use Archive Warnings".to_string()
        ];
        lore_proj.categories = vec!["Gen".to_string(), "M/M".to_string()];
        lore_proj.add_fandom("Original Fiction");
        lore_proj.add_relationship("Hero/Rival");
        lore_proj.add_character("Hero");
        lore_proj.add_character("Rival");
        lore_proj.add_tag("Worldbuilding");
        lore_proj.add_tag("Plot Twists");

        let lm = &mut lore_proj.lore;
        lm.ensure_defaults();

        // A. Verify .grph Unique Magic Bytes
        let grph_magic = LoreGraph::magic_bytes();
        assert_eq!(grph_magic.len(), 8);
        assert_eq!(grph_magic[0], 0x89);
        assert_eq!(&grph_magic[1..5], b"GRPH");
        assert_eq!(grph_magic[5], 0x01);
        assert_eq!(&grph_magic[6..8], b"\r\n");
        assert!(LoreGraph::has_valid_magic(grph_magic));
        assert!(!LoreGraph::has_valid_magic(b"MERMAID;"));
        assert!(!LoreGraph::has_valid_magic(b"\x89PNG\r\n\x1a\n"));

        // B. Test .grph DSL composition & parsing
        let mut test_graph = LoreGraph::new();
        test_graph.title = "Epic Narrative Arc".to_string();
        test_graph.direction = "LR".to_string();

        test_graph.add_or_update_node(LoreGraphNode {
            id: "hero".to_string(),
            label: "The Chosen Hero".to_string(),
            node_type: "character".to_string(),
            x: 100.0,
            y: 150.0,
            color: "#3b82f6".to_string(),
            shape: "rounded".to_string(),
            lore_id: "char_hero".to_string(),
            ..Default::default()
        });

        test_graph.add_or_update_node(LoreGraphNode {
            id: "rival".to_string(),
            label: "The Dark Rival".to_string(),
            node_type: "character".to_string(),
            x: 500.0,
            y: 150.0,
            color: "#ef4444".to_string(),
            shape: "diamond".to_string(),
            lore_id: String::new(),
            ..Default::default()
        });

        test_graph.connect_nodes(
            "hero",
            "rival",
            "-x",
            "rivalry & conflict",
            "dashed",
            "#ef4444",
        );

        test_graph.add_or_update_box(LoreGraphBox {
            id: "academy_box".to_string(),
            label: "Royal Knight Academy".to_string(),
            x: 80.0,
            y: 100.0,
            width: 600.0,
            height: 300.0,
            color: "#8b5cf6".to_string(),
            member_node_ids: vec!["hero".to_string(), "rival".to_string()],
        });

        test_graph.add_or_update_followup(LoreGraphFollowup {
            node_id: "rival".to_string(),
            target_chapter: 3,
            status: "pending".to_string(),
            note: "Reveal hidden backstory in tournament arc.".to_string(),
        });

        let dsl = test_graph.to_dsl();
        assert!(dsl.contains("graph \"Epic Narrative Arc\""));
        assert!(dsl.contains("node hero"));
        assert!(dsl.contains("pos(100, 150)"));
        assert!(dsl.contains("hero -x rival"));
        assert!(dsl.contains("box academy_box"));
        assert!(dsl.contains("followup rival"));

        let mut parsed_graph = LoreGraph::new();
        parsed_graph.from_dsl(&dsl).unwrap();
        assert_eq!(parsed_graph.title, "Epic Narrative Arc");
        assert_eq!(parsed_graph.nodes.len(), 2);
        assert_eq!(parsed_graph.connectors.len(), 1);
        assert_eq!(parsed_graph.connectors[0].arrow_type, "-x");
        assert_eq!(parsed_graph.boxes.len(), 1);
        assert_eq!(parsed_graph.followups.len(), 1);
        assert_eq!(parsed_graph.find_node("hero").unwrap().x, 100.0);
        assert_eq!(parsed_graph.find_node("hero").unwrap().y, 150.0);

        // C. Test binary .grph file bytes (magic header + DSL)
        let grph_bytes = test_graph.to_grph_bytes();
        assert!(grph_bytes.starts_with(grph_magic));
        let mut from_bytes_graph = LoreGraph::new();
        from_bytes_graph.from_grph_bytes(&grph_bytes).unwrap();
        assert_eq!(from_bytes_graph.nodes.len(), 2);

        // D. Test moving blocks updates pos(x, y)
        parsed_graph.update_node_position("hero", 250.0, 350.0);
        assert_eq!(parsed_graph.find_node("hero").unwrap().x, 250.0);
        assert_eq!(parsed_graph.find_node("hero").unwrap().y, 350.0);
        assert!(parsed_graph.to_dsl().contains("pos(250, 350)"));

        // E. Populate Lore Manager
        lm.add_or_update_entity(LoreEntity {
            id: "char_hero".to_string(),
            name: "The Chosen Hero".to_string(),
            category: "Character".to_string(),
            description: "Protagonist seeking redemption.".to_string(),
            tags: vec!["canon".to_string(), "magic".to_string()],
            ..Default::default()
        });

        lm.add_or_update_history_event(LoreHistoryEvent {
            id: "hist_cataclysm".to_string(),
            title: "The Great Cataclysm".to_string(),
            era_or_date: "100 years prior".to_string(),
            summary: "Shattered the ancient empire into seven kingdoms.".to_string(),
            ..Default::default()
        });

        lm.add_or_update_conflict(LoreConflict {
            id: "conf_magic_system".to_string(),
            title: "Mana Consumption Inconsistency".to_string(),
            severity: "medium".to_string(),
            chapter_a: 1,
            chapter_b: 4,
            description: "Chapter 1 says spells require gems, Chapter 4 uses spoken runes."
                .to_string(),
            ..Default::default()
        });

        lm.resolve_conflict(
            "conf_magic_system",
            "Gems amplify spoken runes; both are canonically valid.",
            "Dual-channel spellcasting system",
            "user",
        );

        lm.add_or_update_plan(LorePlan {
            id: "plan_betrayal".to_string(),
            title: "Mentor's Betrayal".to_string(),
            target_chapter: 7,
            status: "planned".to_string(),
            description: "Foreshadow betrayal in Chapter 5, execute in Chapter 7.".to_string(),
            ..Default::default()
        });

        lm.add_or_update_reference(LoreReference {
            id: "ref_magic_rules".to_string(),
            title: "World Codex Vol 1".to_string(),
            source: "Author Notes".to_string(),
            citation: "Page 42".to_string(),
            ..Default::default()
        });

        lm.default_graph = test_graph;

        // F. Save project to .ao3proj archive
        let lore_proj_path = temp_dir.path().join("lore_story.ao3proj");
        ProjectSerializer::save(&lore_proj_path, &lore_proj).unwrap();
        assert!(lore_proj_path.exists());

        // G. Verify all 7 required files in lore/ directory plus lore/graphs/data.grph
        let raw_entries = ZstdArchive::decompress_archive(&lore_proj_path).unwrap();
        assert!(raw_entries.contains_key("lore/lore_data.json"));
        assert!(raw_entries.contains_key("lore/history.json"));
        assert!(raw_entries.contains_key("lore/conflicts.json"));
        assert!(raw_entries.contains_key("lore/resolved.json"));
        assert!(raw_entries.contains_key("lore/plans.json"));
        assert!(raw_entries.contains_key("lore/references.json"));
        assert!(raw_entries.contains_key("lore/metadata.xml"));
        assert!(raw_entries.contains_key("lore/graphs/data.grph"));

        let archived_grph_bytes = &raw_entries["lore/graphs/data.grph"];
        assert!(LoreGraph::has_valid_magic(archived_grph_bytes));

        let meta_str = String::from_utf8_lossy(&raw_entries["metadata.xml"]);
        assert!(meta_str.contains("<rating>Teen And Up Audiences</rating>"));
        assert!(meta_str.contains("<warning>Creator Chose Not To Use Archive Warnings</warning>"));
        assert!(meta_str.contains("<category>Gen</category>"));
        assert!(meta_str.contains("<category>M/M</category>"));
        assert!(meta_str.contains("<fandom>Original Fiction</fandom>"));
        assert!(meta_str.contains("<relationship>Hero/Rival</relationship>"));
        assert!(meta_str.contains("<character>Hero</character>"));
        assert!(meta_str.contains("<character>Rival</character>"));
        assert!(meta_str.contains("<tag>Worldbuilding</tag>"));
        assert!(meta_str.contains("<lore enabled=\"true\""));

        // H. Reload project via 3-stage validation pipeline
        let mut reloaded = Ao3Project::new();
        let rep = ProjectSerializer::load_with_validation(&lore_proj_path, &mut reloaded, None);
        assert!(rep.success);
        assert_eq!(reloaded.title, "Lore & Worldbuilding Masterpiece");
        assert_eq!(reloaded.rating, "Teen And Up Audiences");
        assert!(reloaded
            .archive_warnings
            .contains(&"Creator Chose Not To Use Archive Warnings".to_string()));
        assert!(reloaded.categories.contains(&"Gen".to_string()));
        assert!(reloaded.fandoms.contains(&"Original Fiction".to_string()));
        assert!(reloaded.relationships.contains(&"Hero/Rival".to_string()));
        assert!(reloaded.characters.contains(&"Hero".to_string()));
        assert!(reloaded.tags.contains(&"Worldbuilding".to_string()));

        let reloaded_lore = &reloaded.lore;
        assert!(!reloaded_lore.entities.is_empty());
        assert!(reloaded_lore.find_entity("char_hero").is_some());
        assert!(!reloaded_lore.history_events.is_empty());
        assert!(!reloaded_lore.resolved_conflicts.is_empty());
        assert!(!reloaded_lore.plans.is_empty());
        assert!(!reloaded_lore.references.is_empty());
        assert_eq!(reloaded_lore.default_graph.nodes.len(), 2);
        assert_eq!(reloaded_lore.default_graph.connectors.len(), 1);
        assert_eq!(reloaded_lore.default_graph.boxes.len(), 1);
        assert_eq!(reloaded_lore.default_graph.followups.len(), 1);
    }

    // 15. Test loading legacy/older .ao3proj archive created before modern features
    {
        let legacy_archive_proj_path = temp_dir.path().join("legacy_bare_archive.ao3proj");
        let mut bare_entries = BTreeMap::new();
        bare_entries.insert(
            "metadata.xml".to_string(),
            br#"<?xml version="1.0" encoding="UTF-8"?>
<project>
  <title>Old Archive Story</title>
  <author>Retro Author</author>
  <chapter id="1" order="0" file="chapters/ch1.html">Beginning</chapter>
</project>
"#
            .to_vec(),
        );
        bare_entries.insert(
            "chapters/ch1.html".to_string(),
            b"<p>Chapter content from old archive.</p>".to_vec(),
        );
        ZstdArchive::compress_archive(&legacy_archive_proj_path, &bare_entries).unwrap();
        assert!(legacy_archive_proj_path.exists());

        let mut legacy_proj = Ao3Project::new();
        let rep = ProjectSerializer::load_with_validation(
            &legacy_archive_proj_path,
            &mut legacy_proj,
            None,
        );
        assert!(rep.success);
        assert_eq!(legacy_proj.title, "Old Archive Story");
        assert_eq!(legacy_proj.author, "Retro Author");
        assert_eq!(legacy_proj.chapters.len(), 1);
        assert_eq!(legacy_proj.chapters[0].title, "Beginning");
        assert_eq!(
            legacy_proj.chapters[0].html,
            "<p>Chapter content from old archive.</p>"
        );
        assert!(!legacy_proj.history_log.trim().is_empty());
        assert!(legacy_proj
            .history_log
            .contains("Initial project import / baseline commit"));
        assert!(!legacy_proj.lore.default_graph.title.is_empty());
        assert!(!legacy_proj.agentic_data.history_entries().is_empty());
    }

    // 17. Test Snapshots: history/ directory in .ao3proj, diff generation, and author tagging
    {
        let mut proj = Ao3Project::new();
        proj.reset_to_new();
        proj.title = "Snapshot Test Story".to_string();
        proj.author = "TestAuthor".to_string();
        assert!(!proj.snapshot_diffs.is_empty());

        proj.chapters[0].html =
            "<p>Modified chapter line 1</p>\n<p>Added line 2</p>".to_string();
        proj.add_chapter("Chapter 2");
        proj.chapters[1].html = "<p>Brand new chapter 2 content</p>".to_string();

        let new_hash = proj.append_history_commit("", "Add chapter 2 and modify chapter 1");
        assert!(!new_hash.is_empty());
        assert!(proj.snapshot_diffs.contains_key(&new_hash));
        let diff = proj.snapshot_diff(&new_hash);
        assert!(diff.contains("--- a/chapters/"));
        assert!(diff.contains("+++ b/chapters/"));
        assert!(diff.contains("+<p>Added line 2</p>"));
        assert!(diff.contains("# Snapshot:"));
        assert!(diff.contains("@"));

        let snap_proj_path = temp_dir.path().join("snap_test.ao3proj");
        ProjectSerializer::save(&snap_proj_path, &proj).unwrap();
        assert!(snap_proj_path.exists());

        let entries = ZstdArchive::decompress_archive(&snap_proj_path).unwrap();
        assert!(entries.contains_key("history.cmt"));
        assert!(entries.contains_key(&format!("history/{}.diff", new_hash)));

        let mut reloaded_proj = Ao3Project::new();
        let rep =
            ProjectSerializer::load_with_validation(&snap_proj_path, &mut reloaded_proj, None);
        assert!(rep.success);
        assert!(reloaded_proj.snapshot_diffs.contains_key(&new_hash));
        assert_eq!(reloaded_proj.snapshot_diff(&new_hash), diff);
        assert!(reloaded_proj.snapshot_diff("nonexistent12").is_empty());
    }

    // 18. Test loadWithValidation ProgressCallback
    {
        let mut proj = Ao3Project::new();
        proj.reset_to_new();
        proj.title = "Progress Test Project".to_string();
        proj.author = "Progress Author".to_string();
        proj.chapters[0].html = "<p>Progress test chapter 1.</p>".to_string();
        proj.add_chapter("Progress Chapter 2");
        proj.chapters[1].html = "<p>Progress test chapter 2.</p>".to_string();

        let progress_proj_path = temp_dir.path().join("progress_test.ao3proj");
        ProjectSerializer::save(&progress_proj_path, &proj).unwrap();
        assert!(progress_proj_path.exists());

        let mut progress_steps: Vec<(i32, String)> = Vec::new();
        let mut loaded_proj = Ao3Project::new();
        let mut cb = |percent: i32, status: &str| {
            progress_steps.push((percent, status.to_string()));
        };
        let rep = ProjectSerializer::load_with_validation(
            &progress_proj_path,
            &mut loaded_proj,
            Some(&mut cb),
        );

        assert!(rep.success);
        assert!(!progress_steps.is_empty());
        let mut last_percent = -1;
        for (pct, status) in &progress_steps {
            assert!(*pct >= last_percent);
            assert!(*pct >= 0 && *pct <= 100);
            assert!(!status.is_empty());
            last_percent = *pct;
        }
        assert!(progress_steps.first().unwrap().0 >= 10);
        assert!(progress_steps.last().unwrap().0 >= 80);
    }

    // 16. Verify deleting all chapters down to 0 and reloading
    {
        let mut proj = Ao3Project::new();
        proj.reset_to_new();
        assert_eq!(proj.chapters.len(), 1);
        let removed = proj.remove_chapter(0);
        assert!(removed.is_some());
        assert_eq!(proj.chapters.len(), 0);
        assert_eq!(proj.active_chapter_index, 0);

        let zero_ch_path = temp_dir.path().join("zero_chapters.ao3proj");
        ProjectSerializer::save(&zero_ch_path, &proj).unwrap();
        assert!(zero_ch_path.exists());

        let mut loaded = Ao3Project::new();
        let rep = ProjectSerializer::load_with_validation(&zero_ch_path, &mut loaded, None);
        assert!(rep.success);
        assert_eq!(loaded.chapters.len(), 0);

        // Add a chapter back
        loaded.add_chapter("New First Chapter");
        assert_eq!(loaded.chapters.len(), 1);
        assert_eq!(loaded.chapters[0].title, "New First Chapter");
    }
}

