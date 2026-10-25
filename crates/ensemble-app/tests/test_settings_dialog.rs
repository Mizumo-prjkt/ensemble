use ensemble_app::{
    AboutDialogController, HelpGuideController, MainMenuController, ProjectLoadingController,
    SettingsDialogController, SingleInstanceAction, SingleInstanceManager, ToolbarIcons,
    WebViewPreviewController,
};
use ensemble_core::diff::RgbColor;
use ensemble_mcp::{McpAgentInstaller, McpServer};
use ensemble_net::UpdateChecker;
use ensemble_theme::{AppTheme, SkinFlavor, ThemeManager};
use serde_json::json;
use std::fs;
use std::path::PathBuf;

#[test]
fn test_settings_dialog_and_app_controllers_lifecycle() {
    // 1. Test OLED Darker mode
    ThemeManager::instance().set_theme(AppTheme::Darker);
    assert_eq!(ThemeManager::instance().current_theme(), AppTheme::Darker);
    assert!(ThemeManager::instance().is_dark_mode());
    assert!(ThemeManager::instance().is_oled_mode());
    assert_eq!(
        ThemeManager::instance().window_bg_color(),
        RgbColor::new(0x00, 0x00, 0x00)
    );

    // 2. Test SkinFlavor and Material You wallpaper scanning
    ThemeManager::instance().set_skin_flavor(SkinFlavor::MaterialYou);
    assert_eq!(
        ThemeManager::instance().current_skin_flavor(),
        SkinFlavor::MaterialYou
    );
    ThemeManager::instance().rescan_wallpaper_color();
    assert!(!ThemeManager::instance()
        .material_you_accent_color()
        .to_hex()
        .is_empty());
    assert!(!ThemeManager::instance().wallpaper_status().is_empty());

    // 3. Test Light mode high-contrast text tokens
    ThemeManager::instance().set_theme(AppTheme::Light);
    assert_eq!(
        ThemeManager::instance().primary_text_color(),
        RgbColor::from_css_str("#0f172a").unwrap()
    );
    assert_eq!(
        ThemeManager::instance().muted_text_color(),
        RgbColor::from_css_str("#334155").unwrap()
    );

    // 4. Test AO3 Reversi theme and Material You conflict resolution
    ThemeManager::instance().set_theme(AppTheme::Ao3Reversi);
    assert_eq!(
        ThemeManager::instance().current_theme(),
        AppTheme::Ao3Reversi
    );
    assert!(ThemeManager::instance().is_dark_mode());
    assert!(ThemeManager::instance().is_ao3_reversi());
    assert_eq!(
        ThemeManager::instance().accent_color(),
        RgbColor::from_css_str("#990000").unwrap()
    );
    assert_eq!(
        ThemeManager::instance().window_bg_color(),
        RgbColor::from_css_str("#111111").unwrap()
    );
    assert_eq!(
        ThemeManager::instance().current_skin_flavor(),
        SkinFlavor::StandardEnsemble
    );

    ThemeManager::instance().set_skin_flavor(SkinFlavor::MaterialYou);
    assert_eq!(
        ThemeManager::instance().current_skin_flavor(),
        SkinFlavor::MaterialYou
    );
    assert_eq!(ThemeManager::instance().current_theme(), AppTheme::Dark);

    // toggle_theme with Material You active skips Reversi
    ThemeManager::instance().set_theme(AppTheme::Light);
    ThemeManager::instance().toggle_theme();
    assert_eq!(ThemeManager::instance().current_theme(), AppTheme::Dark);
    ThemeManager::instance().toggle_theme();
    assert_eq!(ThemeManager::instance().current_theme(), AppTheme::Light);

    // StandardEnsemble toggle cycle: Light -> Dark -> Ao3Reversi -> Light
    ThemeManager::instance().set_skin_flavor(SkinFlavor::StandardEnsemble);
    ThemeManager::instance().set_theme(AppTheme::Light);
    ThemeManager::instance().toggle_theme();
    assert_eq!(ThemeManager::instance().current_theme(), AppTheme::Dark);
    ThemeManager::instance().toggle_theme();
    assert_eq!(
        ThemeManager::instance().current_theme(),
        AppTheme::Ao3Reversi
    );
    ThemeManager::instance().toggle_theme();
    assert_eq!(ThemeManager::instance().current_theme(), AppTheme::Light);

    // 5. Test ToolbarIcons vector icon generation
    let normal_color = RgbColor::from_css_str("#334155").unwrap();
    let active_color = RgbColor::from_css_str("#ffffff").unwrap();
    assert!(!ToolbarIcons::bold(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::italic(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::underline(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::strikethrough(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::heading1(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::heading2(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::heading3(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::paragraph(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::link(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::horizontal_rule(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::blockquote(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::bullet_list(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::numbered_list(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::image(normal_color, active_color).is_empty());
    assert!(!ToolbarIcons::code(normal_color, active_color).is_empty());

    ThemeManager::instance().set_theme(AppTheme::Dark);

    // 6. Test SettingsDialogController tabs, checkboxes, and mutual exclusivity
    let mut dialog = SettingsDialogController::new();
    assert_eq!(dialog.tabs().len(), 5);
    assert!(dialog.tabs()[0].contains("General"));
    assert!(dialog.tabs()[1].contains("Editor"));
    assert!(dialog.tabs()[2].contains("WebEngine"));
    assert!(dialog.tabs()[3].contains("AI"));
    assert!(dialog.tabs()[4].contains("Plugins"));

    // GPU acceleration setting persistence
    ThemeManager::instance().update_settings(|s| s.gpu_acceleration = true);
    assert!(ThemeManager::instance().settings().gpu_acceleration);
    ThemeManager::instance().update_settings(|s| s.gpu_acceleration = false);
    assert!(!ThemeManager::instance().settings().gpu_acceleration);
    ThemeManager::instance().update_settings(|s| s.gpu_acceleration = true);

    let mut found_mimic = false;
    let mut found_mcp = false;
    let mut found_api = false;
    let mut found_local = false;
    let mut found_hover_elem = false;
    let mut found_highlight_err = false;
    let mut found_hover_details = false;
    let mut found_auto_format = false;

    for cb in dialog.checkboxes() {
        if cb.text.contains("Allow Mimicking The whole AO3 Body page") {
            found_mimic = true;
        }
        if cb.text.contains("access this via MCP") {
            found_mcp = true;
        }
        if cb.text.contains("access this via API") {
            found_api = true;
        }
        if cb.text.contains("local LLMs") {
            found_local = true;
            assert!(cb.enabled);
            assert!(cb.text.contains("[Beta]"));
            assert!(cb.text.contains("MCP connector"));
        }
        if cb
            .text
            .contains("Show embedded element, tag, and CSS class info on hover")
        {
            found_hover_elem = true;
        }
        if cb
            .text
            .contains("Highlight errors, undefined classes, and warnings directly in editor")
        {
            found_highlight_err = true;
        }
        if cb
            .text
            .contains("Show detailed problem explanations when hovering over issues")
        {
            found_hover_details = true;
        }
        if cb
            .text
            .contains("Auto-format Raw HTML (split paragraphs across lines)")
        {
            found_auto_format = true;
        }
    }
    assert!(found_mimic);
    assert!(found_mcp);
    assert!(found_api);
    assert!(found_local);
    assert!(found_hover_elem);
    assert!(found_highlight_err);
    assert!(found_hover_details);
    assert!(found_auto_format);

    dialog.set_local_agents_checked(true);
    assert!(ThemeManager::instance().settings().local_llms_mcp_enabled);
    assert!(!dialog.is_local_details_card_hidden());

    dialog.set_local_agents_checked(false);
    assert!(!ThemeManager::instance().settings().local_llms_mcp_enabled);
    assert!(dialog.is_local_details_card_hidden());

    let local_llm_config = McpAgentInstaller::generate_local_llm_mcp_config_json();
    assert!(local_llm_config.contains("ensemble"));
    assert!(local_llm_config.contains("--mcp-bridge"));

    let local_llm_script = McpAgentInstaller::generate_local_llm_agent_script();
    assert!(local_llm_script.contains("--ollama"));
    assert!(local_llm_script.contains("--llama-cpp"));
    assert!(local_llm_script.contains("--mcp-bridge"));

    // Mutual exclusivity in SettingsDialogController
    dialog.set_reversi_checked(true);
    assert!(dialog.is_reversi_checked());
    assert!(!dialog.is_material_you_combo_enabled());
    assert_ne!(dialog.selected_flavor(), SkinFlavor::MaterialYou);

    dialog.set_selected_flavor(SkinFlavor::MaterialYou);
    assert_eq!(dialog.selected_flavor(), SkinFlavor::MaterialYou);
    assert!(!dialog.is_reversi_enabled());
    assert!(!dialog.is_reversi_checked());

    // 7. Test SkinFlavor geometry metrics
    ThemeManager::instance().set_skin_flavor(SkinFlavor::MaterialYou);
    assert_eq!(ThemeManager::instance().button_radius(), 20);
    assert_eq!(ThemeManager::instance().tab_radius(), 18);
    assert_eq!(ThemeManager::instance().card_radius(), 22);
    assert!(!ThemeManager::instance().selection_bg_css().is_empty());
    assert!(ThemeManager::instance()
        .get_editor_pane_style_sheet()
        .contains("selection-background-color"));
    assert!(ThemeManager::instance()
        .get_code_editor_style_sheet()
        .contains("selection-background-color"));

    ThemeManager::instance().set_skin_flavor(SkinFlavor::KdeUi);
    assert_eq!(ThemeManager::instance().button_radius(), 3);
    assert_eq!(ThemeManager::instance().tab_radius(), 3);
    assert_eq!(ThemeManager::instance().card_radius(), 4);
    assert_eq!(
        ThemeManager::instance().accent_color(),
        RgbColor::from_css_str("#3daee9").unwrap()
    );

    ThemeManager::instance().set_skin_flavor(SkinFlavor::StandardQt);
    assert_eq!(ThemeManager::instance().button_radius(), 4);
    assert_eq!(ThemeManager::instance().tab_radius(), 4);
    assert_eq!(ThemeManager::instance().card_radius(), 6);

    // 8. Test HelpGuideController 8 consolidated topics, legacy aliases, search, and content rendering
    let mut help = HelpGuideController::new();
    assert_eq!(help.topic_count(), 8);

    help.select_topic("plugins_dsl");
    assert!(help.to_html().contains("Sandboxed Scripting Engine"));
    assert!(help.to_html().contains("ui.create_window"));

    help.select_topic("work_skins");
    assert!(help.to_html().contains("Work Skin"));

    help.select_topic("chapter_manager");
    assert!(help.to_html().contains("Chapter Manager"));
    assert!(help.to_html().contains("Strict 1-Based"));

    help.select_topic("mcp_setup");
    assert!(help.to_html().contains("Model Context Protocol"));
    assert!(help.to_html().contains("--mcp-bridge"));

    help.select_topic("lore_graph");
    assert!(help.to_html().contains("Lore Graph"));
    assert!(help.to_html().contains(".grph"));

    help.select_topic("project_properties");
    assert!(help.to_html().contains("Project Properties"));
    assert!(help.to_html().contains("metadata.xml"));

    help.select_topic("image_embeds");
    assert!(help.to_html().contains("Image Embed"));
    assert!(help.to_html().contains("Copy AO3 HTML Snippet"));

    help.select_topic("agentic_data");
    assert!(help.to_html().contains("Multi-Agent Collaboration"));
    assert!(help.to_html().contains("5 MB"));

    help.select_topic("git_history");
    assert!(help.to_html().contains("Snapshots"));
    assert!(help.to_html().contains("history.cmt"));

    help.set_search_filter("Shortcuts");
    assert!(help.visible_topic_count() >= 1);

    help.set_search_filter("mcp");
    assert!(help.visible_topic_count() >= 1);

    help.clear_search_filter();
    assert_eq!(help.visible_topic_count(), 8);
    help.update_theme();

    // 9. Test MainMenuController project open/close state tracking
    let mut menu = MainMenuController::new();
    assert!(menu.save_btn_title.contains("Save Project"));
    assert!(!menu.save_btn_enabled);
    assert!(menu.continue_btn_hidden);
    assert!(menu.close_btn_hidden);

    menu.set_project_state(true, "My Epic AO3 Fic");
    assert!(menu.save_btn_enabled);
    assert!(!menu.continue_btn_hidden);
    assert!(menu.continue_subtext.contains("My Epic AO3 Fic"));
    assert!(!menu.close_btn_hidden);

    menu.set_project_state(false, "");
    assert!(!menu.save_btn_enabled);
    assert!(menu.continue_btn_hidden);
    assert!(menu.close_btn_hidden);

    // 10. Test SingleInstanceManager socket naming, PID tracking, and process lifecycle
    assert!(!SingleInstanceManager::socket_name().is_empty());
    assert!(!SingleInstanceManager::socket_path().is_empty());
    assert!(!SingleInstanceManager::runtime_dir().is_empty());
    assert!(!SingleInstanceManager::pid_file_path().is_empty());
    assert!(SingleInstanceManager::is_process_running(
        std::process::id()
    ));
    assert!(!SingleInstanceManager::is_process_running(99_999_999));
    assert!(SingleInstanceManager::is_ensemble_process(
        std::process::id()
    ));
    assert!(!SingleInstanceManager::is_ensemble_process(99_999_999));

    #[cfg(target_os = "linux")]
    {
        assert!(!SingleInstanceManager::is_ensemble_process(1));
        assert!(!SingleInstanceManager::is_process_running(1));
        assert!(!SingleInstanceManager::current_boot_id().is_empty());
    }

    // Stale boot ID PID file cleanup
    {
        let pid_path = PathBuf::from(SingleInstanceManager::pid_file_path());
        let stale_meta = json!({
            "pid": 12345,
            "bootId": "stale-boot-id-simulated-99999",
            "appPath": "/fake/path/ensemble"
        });
        fs::write(&pid_path, serde_json::to_string(&stale_meta).unwrap()).unwrap();
        assert!(pid_path.exists());

        let stale_action = SingleInstanceManager::check_and_handle(true, None);
        assert_eq!(stale_action, SingleInstanceAction::ProceedAsPrimary);
        assert!(!pid_path.exists());
    }

    let action = SingleInstanceManager::check_and_handle(true, None);
    assert!(
        action == SingleInstanceAction::ProceedAsPrimary
            || action == SingleInstanceAction::ExitApplication
    );

    // 11. Test McpServer lifecycle, loopback self-test, and McpAgentInstaller
    {
        let server = McpServer::new();
        assert!(!server.is_running());
        assert_eq!(server.actual_port(), 0);

        assert!(server.start(26345));
        assert!(server.is_running());
        assert!(server.actual_port() > 0);
        assert!(!server.url().is_empty());

        let info = McpServer::read_discovery_file();
        assert_eq!(info["name"], "ensemble-ao3-typewriter");
        assert_eq!(info["port"].as_u64().unwrap() as u16, server.actual_port());

        let test_res = server.test_loopback();
        assert!(test_res.success);
        assert!(test_res.latency_ms >= 0);

        let manual = McpAgentInstaller::generate_manual_markdown(
            "/usr/bin/ensemble",
            server.actual_port(),
        );
        assert!(manual.contains("# Ensemble MCP Server"));
        assert!(manual.contains("get_project_summary"));
        assert!(manual.contains("update_chapter_html"));
        assert!(manual.contains("test_firewall_connection"));

        server.stop();
        assert!(!server.is_running());
        assert_eq!(server.actual_port(), 0);
    }

    // 12. Test UpdateChecker & AboutDialogController
    assert!(UpdateChecker::is_version_newer("v1.2.2", "1.1.1"));
    assert!(UpdateChecker::is_version_newer("1.2.2", "1.2.1"));
    assert!(UpdateChecker::is_version_newer("v2.0.0", "1.9.9"));
    assert!(!UpdateChecker::is_version_newer("1.1.1", "1.1.1"));
    assert!(!UpdateChecker::is_version_newer("v1.1.0", "1.1.1"));
    assert!(!UpdateChecker::is_version_newer("1.0.5", "1.2.0"));
    assert!(!UpdateChecker::is_version_newer(
        "v1.2.2",
        "1.2.2 (Debug-dirty)"
    ));

    let sample_github_json = br####"{
        "tag_name": "v1.2.2",
        "name": "Ensemble Release v1.2.2",
        "html_url": "https://github.com/Mizumo-prjkt/ensemble/releases/tag/v1.2.2",
        "published_at": "2026-10-17T12:00:00Z",
        "body": "### Changes\n- Enhanced rich text editor\n- Added snapshots diff\n- Fixed chapter indexing"
    }"####;
    let info = UpdateChecker::parse_release_json(sample_github_json, "1.1.1");
    assert_eq!(info.tag_name, "v1.2.2");
    assert_eq!(info.version, "1.2.2");
    assert_eq!(info.name, "Ensemble Release v1.2.2");
    assert!(info.is_update_available);

    let info_up_to_date = UpdateChecker::parse_release_json(sample_github_json, "1.2.2");
    assert!(!info_up_to_date.is_update_available);

    let mut about_dlg = AboutDialogController::new();
    assert!(about_dlg.has_update_card);
    assert!(about_dlg.check_updates_btn_text.contains("Check"));
    assert!(about_dlg.get_update_btn_text.contains("Get"));
    ThemeManager::instance().set_theme(AppTheme::Light);
    about_dlg.update_theme();
    ThemeManager::instance().set_theme(AppTheme::Dark);
    about_dlg.update_theme();

    // 13. Test ProjectLoadingController progress bar, clamping, theme updates, and finish lifecycle
    let mut load_dlg = ProjectLoadingController::new("/home/user/stories/epic_fanfic.ao3proj");
    assert_eq!(load_dlg.progress_value(), 0);
    assert!(!load_dlg.current_status().is_empty());
    assert_eq!(load_dlg.window_title(), "Opening Project…");

    load_dlg.set_progress(25, "Decompressing project archive container…");
    assert_eq!(load_dlg.progress_value(), 25);
    assert_eq!(
        load_dlg.current_status(),
        "Decompressing project archive container…"
    );

    load_dlg.set_progress(150, "Clamped max");
    assert_eq!(load_dlg.progress_value(), 100);

    load_dlg.set_progress(-20, "Clamped min");
    assert_eq!(load_dlg.progress_value(), 0);

    for theme in [
        AppTheme::Light,
        AppTheme::Dark,
        AppTheme::Darker,
        AppTheme::Ao3Reversi,
    ] {
        ThemeManager::instance().set_theme(theme);
        load_dlg.update_theme();
    }

    load_dlg.finish();
    assert_eq!(load_dlg.progress_value(), 100);
    assert_eq!(load_dlg.current_status(), "Ready!");

    // 14. Test WebViewPreviewController AO3 Default vs Reversi preview document
    let mut preview = WebViewPreviewController::new();
    let doc_light = preview.compose_ao3_preview_document(
        "Chapter 1",
        "<p>Hello AO3!</p>",
        ".chat { color: red; }",
    );
    assert!(doc_light.contains("#ffffff"));
    assert!(doc_light.contains("#workskin"));
    preview.set_reversi_skin(true);
    let doc_reversi = preview.compose_ao3_preview_document(
        "Chapter 1",
        "<p>Hello AO3!</p>",
        ".chat { color: red; }",
    );
    assert!(doc_reversi.contains("#111111"));
}
