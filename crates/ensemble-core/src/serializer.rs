use crate::agentic::AgenticData;
use crate::archive::{
    ZstdArchive, ZIP_EMPTY_MAGIC, ZIP_MAGIC, ZIP_SPANNED_MAGIC, ZSTD_MAGIC,
};
use crate::chapter::Chapter;
use crate::project::Ao3Project;
use chrono::{DateTime, Utc};
use quick_xml::events::{BytesDecl, BytesEnd, BytesStart, BytesText, Event};
use quick_xml::{Reader, Writer};
use serde_json::Value;
use std::collections::{BTreeMap, HashSet};
use std::fs::{self, File};
use std::io::{Cursor, Read};
use std::path::Path;
use uuid::Uuid;

const LEGACY_FORMAT_VERSION: i64 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProjectFormatType {
    Unknown,
    ModernArchiveZstd,
    ModernArchiveZip,
    LegacyJsonV1,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ValidationStage {
    None,
    FileAccess,
    MagicBytesCheck,
    StructureCheck,
    RenderCheck,
}

#[derive(Debug, Clone)]
pub struct ProjectLoadReport {
    pub success: bool,
    pub is_legacy: bool,
    pub format_type: ProjectFormatType,
    pub failed_stage: ValidationStage,
    pub file_path: String,
    pub file_size: u64,
    pub raw_header_bytes: Vec<u8>,
    pub header_hex: String,
    pub header_ascii: String,
    pub summary_error: String,
    pub detailed_explanation: String,
    pub remediation_hint: String,
}

impl Default for ProjectLoadReport {
    fn default() -> Self {
        Self {
            success: false,
            is_legacy: false,
            format_type: ProjectFormatType::Unknown,
            failed_stage: ValidationStage::None,
            file_path: String::new(),
            file_size: 0,
            raw_header_bytes: Vec::new(),
            header_hex: String::new(),
            header_ascii: String::new(),
            summary_error: String::new(),
            detailed_explanation: String::new(),
            remediation_hint: String::new(),
        }
    }
}

pub struct ProjectSerializer;

impl ProjectSerializer {
    pub fn format_hex_dump(bytes: &[u8]) -> String {
        let count = bytes.len().min(64);
        let mut parts: Vec<String> = Vec::with_capacity(count);
        for b in &bytes[..count] {
            parts.push(format!("{:02X}", b));
        }
        let mut hex = parts.join(" ");
        if bytes.len() > 64 {
            hex.push_str(" ...");
        }
        hex
    }

    pub fn format_ascii_preview(bytes: &[u8]) -> String {
        let count = bytes.len().min(64);
        let mut ascii = String::with_capacity(count + 3);
        for &b in &bytes[..count] {
            if (32..=126).contains(&b) {
                ascii.push(b as char);
            } else {
                ascii.push('.');
            }
        }
        if bytes.len() > 64 {
            ascii.push_str("...");
        }
        ascii
    }

    pub fn load_with_validation(
        path: impl AsRef<Path>,
        project: &mut Ao3Project,
        mut progress: Option<&mut dyn FnMut(i32, &str)>,
    ) -> ProjectLoadReport {
        let path_ref = path.as_ref();
        let path_str = path_ref.to_string_lossy().to_string();
        let mut report = ProjectLoadReport {
            file_path: path_str.clone(),
            ..Default::default()
        };

        if let Some(ref mut cb) = progress {
            cb(10, "Checking file integrity & magic signatures…");
        }

        // STAGE 0 & 1: File Access and Magic Bytes Check
        if !path_ref.exists() {
            report.failed_stage = ValidationStage::FileAccess;
            report.summary_error = format!("Project file not found: {}", path_str);
            report.detailed_explanation = format!(
                "============================================================\n\
                  ENSEMBLE PROJECT VERIFICATION AUDIT\n\
                 ============================================================\n\
                 File Path: {}\n\
                 Status: FILE_NOT_FOUND\n\n\
                 [Stage 0: File Access Verification]\n\
                 Result: FAILED\n\
                 Details: The operating system reported that no file exists at this path.\n",
                path_str
            );
            report.remediation_hint =
                "Please verify the file path and confirm that the file exists.".to_string();
            return report;
        }

        let mut file = match File::open(path_ref) {
            Ok(f) => f,
            Err(e) => {
                report.failed_stage = ValidationStage::FileAccess;
                report.summary_error = format!("Cannot open project file: {}", e);
                report.detailed_explanation = format!(
                    "============================================================\n\
                      ENSEMBLE PROJECT VERIFICATION AUDIT\n\
                     ============================================================\n\
                     File Path: {}\n\
                     Status: ACCESS_DENIED / OPEN_FAILED\n\
                     OS Error: {}\n",
                    path_str, e
                );
                report.remediation_hint =
                    "Check file read permissions or close conflicting programs.".to_string();
                return report;
            }
        };

        report.file_size = file.metadata().map(|m| m.len()).unwrap_or(0);
        if report.file_size == 0 {
            report.failed_stage = ValidationStage::MagicBytesCheck;
            report.summary_error = "Project file is empty (0 bytes).".to_string();
            report.detailed_explanation = format!(
                "============================================================\n\
                  ENSEMBLE PROJECT VERIFICATION AUDIT\n\
                 ============================================================\n\
                 File Path: {}\n\
                 File Size: 0 bytes\n\
                 Status: EMPTY_FILE\n",
                path_str
            );
            report.remediation_hint =
                "The file contains 0 bytes and cannot be loaded.".to_string();
            return report;
        }

        let mut header_buf = vec![0u8; 64.min(report.file_size as usize)];
        let _ = file.read_exact(&mut header_buf);
        drop(file);

        report.header_hex = Self::format_hex_dump(&header_buf);
        report.header_ascii = Self::format_ascii_preview(&header_buf);
        report.raw_header_bytes = header_buf;

        if report.raw_header_bytes.starts_with(&ZSTD_MAGIC) {
            report.format_type = ProjectFormatType::ModernArchiveZstd;
            report.is_legacy = false;
        } else if report.raw_header_bytes.starts_with(&ZIP_MAGIC)
            || report.raw_header_bytes.starts_with(&ZIP_EMPTY_MAGIC)
            || report.raw_header_bytes.starts_with(&ZIP_SPANNED_MAGIC)
        {
            report.format_type = ProjectFormatType::ModernArchiveZip;
            report.is_legacy = false;
        } else {
            let mut trimmed = report.raw_header_bytes.as_slice();
            if trimmed.starts_with(b"\xEF\xBB\xBF") {
                trimmed = &trimmed[3..];
            }
            while let Some(&b) = trimmed.first() {
                if b.is_ascii_whitespace() {
                    trimmed = &trimmed[1..];
                } else {
                    break;
                }
            }
            if trimmed.first().copied() == Some(b'{') || trimmed.first().copied() == Some(b'[') {
                report.format_type = ProjectFormatType::LegacyJsonV1;
                report.is_legacy = true;
            }
        }

        if report.format_type == ProjectFormatType::Unknown {
            report.failed_stage = ValidationStage::MagicBytesCheck;
            report.summary_error =
                "Unrecognized file format: Magic Bytes check failed.".to_string();
            report.detailed_explanation = format!(
                "============================================================\n\
                  ENSEMBLE PROJECT VERIFICATION AUDIT\n\
                 ============================================================\n\
                 File Path: {}\n\
                 File Size: {} bytes\n\
                 Status: INVALID_MAGIC_BYTES\n\n\
                 [Stage 1: Magic Bytes Verification]\n\
                 Result: FAILED\n\n\
                 Detected File Header:\n\
                   Hex:   {}\n\
                   ASCII: {}\n",
                path_str, report.file_size, report.header_hex, report.header_ascii
            );
            report.remediation_hint =
                "The file does not match any Ensemble project signature.".to_string();
            return report;
        }

        // STAGE 2: Structural Integrity Check
        if matches!(
            report.format_type,
            ProjectFormatType::ModernArchiveZstd | ProjectFormatType::ModernArchiveZip
        ) {
            if let Some(ref mut cb) = progress {
                cb(25, "Decompressing project archive container…");
            }

            let entries = match ZstdArchive::decompress_archive(path_ref) {
                Ok(e) => e,
                Err(decompress_err) => {
                    report.failed_stage = ValidationStage::StructureCheck;
                    report.summary_error =
                        "Archive extraction failed: corrupted or truncated container.".to_string();
                    report.detailed_explanation = format!(
                        "============================================================\n\
                          ENSEMBLE PROJECT VERIFICATION AUDIT\n\
                         ============================================================\n\
                         File Path: {}\n\
                         Status: DECOMPRESSION_FAILED\n\
                         Extraction Details: {}\n",
                        path_str, decompress_err
                    );
                    report.remediation_hint =
                        "The project archive data is corrupted or truncated.".to_string();
                    return report;
                }
            };

            let Some(manifest_bytes) = entries.get("metadata.xml") else {
                report.failed_stage = ValidationStage::StructureCheck;
                report.summary_error =
                    "Archive is missing required 'metadata.xml' descriptor.".to_string();
                let mut found_list = String::new();
                for (k, v) in &entries {
                    found_list.push_str(&format!("  • {} ({} bytes)\n", k, v.len()));
                }
                report.detailed_explanation = format!(
                    "============================================================\n\
                      ENSEMBLE PROJECT VERIFICATION AUDIT\n\
                     ============================================================\n\
                     File Path: {}\n\
                     Status: MISSING_MANIFEST\n\n\
                     [Stage 2: Structural Integrity Check]\n\
                     Result: FAILED\n\
                     Missing Manifest: metadata.xml\n\n\
                     Archive Contents Found:\n{}\n",
                    path_str, found_list
                );
                report.remediation_hint =
                    "The archive is missing the metadata.xml project descriptor.".to_string();
                return report;
            };

            // Parse metadata.xml
            let mut reader = Reader::from_reader(manifest_bytes.as_slice());
            reader.config_mut().trim_text(true);
            let mut buf = Vec::new();
            let mut root_tag_checked = false;

            #[derive(Default)]
            struct ChapMeta {
                id_str: String,
                order: i32,
                file_rel_path: String,
                title: String,
            }

            let mut chap_metas: Vec<ChapMeta> = Vec::new();
            let mut current_chap: Option<ChapMeta> = None;
            let mut current_tag = String::new();

            let mut parsed_title = "Untitled Work".to_string();
            let mut parsed_author = String::new();
            let mut parsed_rating = "Not Rated".to_string();
            let mut parsed_warnings = Vec::new();
            let mut parsed_categories = Vec::new();
            let mut parsed_fandoms = Vec::new();
            let mut parsed_relationships = Vec::new();
            let mut parsed_characters = Vec::new();
            let mut parsed_tags = Vec::new();
            let mut parsed_created = Utc::now();
            let mut parsed_modified = Utc::now();
            let mut parsed_last_mod_by = String::new();
            let mut parsed_last_mod_at: Option<DateTime<Utc>> = None;

            loop {
                match reader.read_event_into(&mut buf) {
                    Ok(Event::Start(e)) => {
                        let name = String::from_utf8_lossy(e.name().as_ref()).to_string();
                        if !root_tag_checked {
                            root_tag_checked = true;
                            if name != "ao3project" && name != "project" {
                                report.failed_stage = ValidationStage::StructureCheck;
                                report.summary_error = format!(
                                    "Invalid manifest root element: <{}> (expected <ao3project> or <project>).",
                                    name
                                );
                                report.detailed_explanation = format!(
                                    "Status: INVALID_SCHEMA_ROOT\nEncountered root tag: <{}>\n",
                                    name
                                );
                                return report;
                            }
                        } else if name == "chapter" {
                            let mut cm = ChapMeta::default();
                            for attr in e.attributes().flatten() {
                                let k = String::from_utf8_lossy(attr.key.as_ref());
                                let v = attr.unescape_value().unwrap_or_default().to_string();
                                match k.as_ref() {
                                    "id" => cm.id_str = v,
                                    "order" => cm.order = v.parse().unwrap_or(0),
                                    "file" => cm.file_rel_path = v,
                                    _ => {}
                                }
                            }
                            current_chap = Some(cm);
                        }
                        current_tag = name;
                    }
                    Ok(Event::Text(t)) => {
                        let text = t.unescape().unwrap_or_default().to_string();
                        if let Some(ref mut cm) = current_chap {
                            cm.title.push_str(&text);
                        } else {
                            match current_tag.as_str() {
                                "title" => parsed_title = text,
                                "author" => parsed_author = text,
                                "rating" => parsed_rating = text,
                                "warning" => parsed_warnings.push(text),
                                "category" => parsed_categories.push(text),
                                "fandom" => {
                                    if !text.is_empty() && !parsed_fandoms.contains(&text) {
                                        parsed_fandoms.push(text);
                                    }
                                }
                                "relationship" => {
                                    if !text.is_empty() && !parsed_relationships.contains(&text) {
                                        parsed_relationships.push(text);
                                    }
                                }
                                "character" => {
                                    if !text.is_empty() && !parsed_characters.contains(&text) {
                                        parsed_characters.push(text);
                                    }
                                }
                                "tag" => {
                                    if !text.is_empty() && !parsed_tags.contains(&text) {
                                        parsed_tags.push(text);
                                    }
                                }
                                "created" => {
                                    if let Ok(dt) = DateTime::parse_from_rfc3339(&text) {
                                        parsed_created = dt.with_timezone(&Utc);
                                    }
                                }
                                "modified" => {
                                    if let Ok(dt) = DateTime::parse_from_rfc3339(&text) {
                                        parsed_modified = dt.with_timezone(&Utc);
                                    }
                                }
                                "lastModifiedBy" => parsed_last_mod_by = text,
                                "lastModifiedAt" => {
                                    if let Ok(dt) = DateTime::parse_from_rfc3339(&text) {
                                        parsed_last_mod_at = Some(dt.with_timezone(&Utc));
                                    }
                                }
                                _ => {}
                            }
                        }
                    }
                    Ok(Event::End(e)) => {
                        let name = String::from_utf8_lossy(e.name().as_ref()).to_string();
                        if name == "chapter" {
                            if let Some(cm) = current_chap.take() {
                                chap_metas.push(cm);
                            }
                        }
                        current_tag.clear();
                    }
                    Ok(Event::Eof) => break,
                    Err(e) => {
                        report.failed_stage = ValidationStage::StructureCheck;
                        report.summary_error = format!("Invalid XML in metadata.xml: {}", e);
                        report.detailed_explanation =
                            format!("Status: XML_PARSE_ERROR\nXML Error: {}\n", e);
                        return report;
                    }
                    _ => {}
                }
                buf.clear();
            }

            if !root_tag_checked {
                report.failed_stage = ValidationStage::StructureCheck;
                report.summary_error = "Invalid XML in metadata.xml: Empty document".to_string();
                return report;
            }

            // STAGE 3: Populate Project Model
            if let Some(ref mut cb) = progress {
                cb(50, "Reading project metadata & chapter structure…");
            }

            project.reset_to_new();
            project.chapters.clear();

            if let Err(agentic_err) = project.agentic_data.deserialize_from_archive(&entries) {
                report.failed_stage = ValidationStage::StructureCheck;
                report.summary_error = format!("Agentic data validation failed: {}", agentic_err);
                report.detailed_explanation = format!(
                    "Status: AGENTIC_DATA_QUOTA_EXCEEDED\nDetails: {}\n",
                    agentic_err
                );
                return report;
            }

            project.title = parsed_title;
            project.author = parsed_author;
            project.rating = parsed_rating;
            project.archive_warnings = parsed_warnings;
            project.categories = parsed_categories;
            project.fandoms = parsed_fandoms;
            project.relationships = parsed_relationships;
            project.characters = parsed_characters;
            project.tags = parsed_tags;
            project.created_date = parsed_created;
            project.modified_date = parsed_modified;
            project.last_modified_by = parsed_last_mod_by;
            project.last_modified_at = parsed_last_mod_at;

            if let Some(hist) = entries.get("history.cmt") {
                project.history_log = String::from_utf8_lossy(hist).to_string();
            } else {
                project.history_log.clear();
            }

            project.snapshot_diffs.clear();
            for (k, v) in &entries {
                if let Some(rest) = k.strip_prefix("history/") {
                    if let Some(hash) = rest.strip_suffix(".diff") {
                        if !hash.is_empty() {
                            project
                                .snapshot_diffs
                                .insert(hash.to_string(), String::from_utf8_lossy(v).to_string());
                        }
                    }
                }
            }

            if let Some(css) = entries.get("stylesheet/workskin.css") {
                project.work_skin_css = String::from_utf8_lossy(css).to_string();
            }

            let _ = project.lore.deserialize_from_archive(&entries);

            let mut has_non_zero = false;
            let mut seen_orders = HashSet::new();
            let mut has_duplicate_orders = false;
            for meta in &chap_metas {
                if meta.order != 0 {
                    has_non_zero = true;
                }
                if !seen_orders.insert(meta.order) {
                    has_duplicate_orders = true;
                }
            }
            let needs_reorder =
                (!has_non_zero && chap_metas.len() > 1) || (has_duplicate_orders && chap_metas.len() > 1);

            if let Some(ref mut cb) = progress {
                cb(70, "Unpacking chapters and story content…");
            }

            for (fallback_order, meta) in chap_metas.into_iter().enumerate() {
                let id = Uuid::parse_str(&meta.id_str).unwrap_or_else(|_| Uuid::new_v4());
                let title = if meta.title.is_empty() {
                    "Untitled Chapter".to_string()
                } else {
                    meta.title
                };
                let order = if needs_reorder {
                    fallback_order as i32
                } else {
                    meta.order
                };
                let html = entries
                    .get(&meta.file_rel_path)
                    .map(|b| String::from_utf8_lossy(b).to_string())
                    .unwrap_or_else(|| "<p></p>".to_string());
                project.chapters.push(Chapter::with_id(id, title, order, html));
            }

            if !project.chapters.is_empty() {
                project.sort_chapters_by_order();
                if needs_reorder {
                    for (i, ch) in project.chapters.iter_mut().enumerate() {
                        ch.order = i as i32;
                    }
                }
            }

            if let Some(ref mut cb) = progress {
                cb(85, "Loading lore graph and project history…");
            }

            if project.history_log.trim().is_empty() {
                let author = if project.author.is_empty() {
                    "Anonymous".to_string()
                } else {
                    project.author.clone()
                };
                project.append_history_commit(&author, "Initial project import / baseline commit");
            }
            if project.last_modified_by.is_empty() {
                project.last_modified_by = if project.author.is_empty() {
                    "Author".to_string()
                } else {
                    project.author.clone()
                };
            }
            if project.last_modified_at.is_none() {
                project.last_modified_at = Some(project.modified_date);
            }
            project.agentic_data.ensure_default_history();

            project.active_chapter_index = 0;
            project.file_path = path_str;
            project.dirty = false;
            project.is_legacy_format = false;
            project.last_snapshot_state = project.current_project_state();

            if let Some(ref mut cb) = progress {
                cb(90, "Project data unpacked successfully.");
            }

            report.success = true;
            return report;
        }

        // STAGE 2 & 3 FOR LEGACY JSON V1
        if report.format_type == ProjectFormatType::LegacyJsonV1 {
            if let Some(ref mut cb) = progress {
                cb(25, "Reading legacy JSON project file…");
            }

            let raw_bytes = match fs::read(path_ref) {
                Ok(b) => b,
                Err(e) => {
                    report.failed_stage = ValidationStage::FileAccess;
                    report.summary_error = format!("Could not read legacy JSON file: {}", e);
                    return report;
                }
            };

            let doc: Value = match serde_json::from_slice(&raw_bytes) {
                Ok(v) => v,
                Err(e) => {
                    report.failed_stage = ValidationStage::StructureCheck;
                    report.summary_error = format!("Legacy JSON syntax error: {}", e);
                    report.detailed_explanation =
                        format!("Status: JSON_SYNTAX_ERROR\nJSON Parse Error: {}\n", e);
                    return report;
                }
            };

            let Some(root) = doc.as_object() else {
                report.failed_stage = ValidationStage::StructureCheck;
                report.summary_error = "Legacy project JSON root must be an object.".to_string();
                return report;
            };

            let version = root
                .get("formatVersion")
                .and_then(|v| v.as_i64())
                .unwrap_or(0);
            if version != LEGACY_FORMAT_VERSION {
                report.failed_stage = ValidationStage::StructureCheck;
                report.summary_error = format!(
                    "Unsupported legacy format version: {} (expected {}).",
                    version, LEGACY_FORMAT_VERSION
                );
                return report;
            }

            if let Some(ref mut cb) = progress {
                cb(55, "Parsing JSON data and chapter schema…");
            }

            project.reset_to_new();
            project.title = root
                .get("title")
                .and_then(|v| v.as_str())
                .unwrap_or("Untitled Work")
                .to_string();
            project.work_skin_css = root
                .get("workSkinCss")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            project.chapters.clear();

            if let Some(ch_arr) = root.get("chapters").and_then(|v| v.as_array()) {
                if ch_arr.is_empty() {
                    project.add_chapter("Chapter 1");
                } else {
                    if let Some(ref mut cb) = progress {
                        cb(75, "Populating story chapters…");
                    }
                    for val in ch_arr {
                        let Some(ch_obj) = val.as_object() else {
                            report.failed_stage = ValidationStage::StructureCheck;
                            report.summary_error =
                                "Invalid chapter entry in legacy JSON.".to_string();
                            return report;
                        };
                        let id_str = ch_obj.get("id").and_then(|v| v.as_str()).unwrap_or("");
                        let id = if id_str.is_empty() {
                            Uuid::new_v4()
                        } else {
                            match Uuid::parse_str(id_str) {
                                Ok(u) => u,
                                Err(_) => {
                                    report.failed_stage = ValidationStage::StructureCheck;
                                    report.summary_error =
                                        format!("Invalid chapter id: {}", id_str);
                                    return report;
                                }
                            }
                        };
                        let title = ch_obj
                            .get("title")
                            .and_then(|v| v.as_str())
                            .unwrap_or("Untitled Chapter");
                        let order =
                            ch_obj.get("order").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
                        let html = ch_obj.get("html").and_then(|v| v.as_str()).unwrap_or("");
                        project
                            .chapters
                            .push(Chapter::with_id(id, title, order, html));
                    }
                }
            } else {
                project.add_chapter("Chapter 1");
            }

            project.sort_chapters_by_order();
            project.active_chapter_index = 0;
            project.file_path = path_str;
            project.dirty = false;
            project.is_legacy_format = true;

            if let Some(ref mut cb) = progress {
                cb(90, "Legacy project data loaded successfully.");
            }

            report.success = true;
            report.is_legacy = true;
            return report;
        }

        report
    }

    pub fn load(path: impl AsRef<Path>, project: &mut Ao3Project) -> Result<(), String> {
        let report = Self::load_with_validation(path, project, None);
        if report.success {
            Ok(())
        } else {
            Err(report.summary_error)
        }
    }

    pub fn save(path: impl AsRef<Path>, project: &Ao3Project) -> Result<(), String> {
        let mut file_entries: BTreeMap<String, Vec<u8>> = BTreeMap::new();

        // 1. Build metadata.xml
        let mut writer = Writer::new_with_indent(Cursor::new(Vec::new()), b' ', 4);
        let _ = writer.write_event(Event::Decl(BytesDecl::new("1.0", Some("UTF-8"), None)));

        let mut root = BytesStart::new("ao3project");
        root.push_attribute(("version", "2.0"));
        let _ = writer.write_event(Event::Start(root));

        let _ = writer.write_event(Event::Start(BytesStart::new("metadata")));
        Self::write_text_el(&mut writer, "title", &project.title);
        Self::write_text_el(&mut writer, "author", &project.author);
        let rating = if project.rating.is_empty() {
            "Not Rated"
        } else {
            &project.rating
        };
        Self::write_text_el(&mut writer, "rating", rating);

        if !project.archive_warnings.is_empty() {
            let _ = writer.write_event(Event::Start(BytesStart::new("archiveWarnings")));
            for w in &project.archive_warnings {
                Self::write_text_el(&mut writer, "warning", w);
            }
            let _ = writer.write_event(Event::End(BytesEnd::new("archiveWarnings")));
        }
        if !project.categories.is_empty() {
            let _ = writer.write_event(Event::Start(BytesStart::new("categories")));
            for c in &project.categories {
                Self::write_text_el(&mut writer, "category", c);
            }
            let _ = writer.write_event(Event::End(BytesEnd::new("categories")));
        }
        if !project.fandoms.is_empty() {
            let _ = writer.write_event(Event::Start(BytesStart::new("fandoms")));
            for f in &project.fandoms {
                Self::write_text_el(&mut writer, "fandom", f);
            }
            let _ = writer.write_event(Event::End(BytesEnd::new("fandoms")));
        }
        if !project.relationships.is_empty() {
            let _ = writer.write_event(Event::Start(BytesStart::new("relationships")));
            for r in &project.relationships {
                Self::write_text_el(&mut writer, "relationship", r);
            }
            let _ = writer.write_event(Event::End(BytesEnd::new("relationships")));
        }
        if !project.characters.is_empty() {
            let _ = writer.write_event(Event::Start(BytesStart::new("characters")));
            for ch in &project.characters {
                Self::write_text_el(&mut writer, "character", ch);
            }
            let _ = writer.write_event(Event::End(BytesEnd::new("characters")));
        }
        if !project.tags.is_empty() {
            let _ = writer.write_event(Event::Start(BytesStart::new("tags")));
            for t in &project.tags {
                Self::write_text_el(&mut writer, "tag", t);
            }
            let _ = writer.write_event(Event::End(BytesEnd::new("tags")));
        }

        Self::write_text_el(&mut writer, "created", &project.created_date.to_rfc3339());
        Self::write_text_el(&mut writer, "modified", &Utc::now().to_rfc3339());
        if !project.last_modified_by.is_empty() {
            Self::write_text_el(&mut writer, "lastModifiedBy", &project.last_modified_by);
        }
        if let Some(dt) = project.last_modified_at {
            Self::write_text_el(&mut writer, "lastModifiedAt", &dt.to_rfc3339());
        }
        let _ = writer.write_event(Event::End(BytesEnd::new("metadata")));

        // Chapters
        let _ = writer.write_event(Event::Start(BytesStart::new("chapters")));
        for (idx, ch) in project.chapters.iter().enumerate() {
            let ch_file_name = format!("chapters/chapter_{}.html", idx + 1);
            let mut ch_el = BytesStart::new("chapter");
            let id_str = ch.id_string();
            let order_str = ch.order.to_string();
            ch_el.push_attribute(("id", id_str.as_str()));
            ch_el.push_attribute(("order", order_str.as_str()));
            ch_el.push_attribute(("file", ch_file_name.as_str()));
            let _ = writer.write_event(Event::Start(ch_el));
            let _ = writer.write_event(Event::Text(BytesText::new(&ch.title)));
            let _ = writer.write_event(Event::End(BytesEnd::new("chapter")));

            file_entries.insert(ch_file_name, ch.html_for_save().into_bytes());
        }
        let _ = writer.write_event(Event::End(BytesEnd::new("chapters")));

        // Stylesheets
        let _ = writer.write_event(Event::Start(BytesStart::new("stylesheets")));
        let mut ss_el = BytesStart::new("stylesheet");
        ss_el.push_attribute(("file", "stylesheet/workskin.css"));
        let _ = writer.write_event(Event::Empty(ss_el));
        let _ = writer.write_event(Event::End(BytesEnd::new("stylesheets")));

        // Agentic-datas manifest
        let mut ag_el = BytesStart::new("agentic-datas");
        let shared_b = project.agentic_data.total_shared_bytes().to_string();
        let loc_b = project.agentic_data.total_localized_bytes().to_string();
        let max_sh = AgenticData::MAX_SHARED_BYTES.to_string();
        let max_loc = AgenticData::MAX_LOCALIZED_BYTES.to_string();
        ag_el.push_attribute(("enabled", "true"));
        ag_el.push_attribute(("index", "agentic-datas/index.json"));
        ag_el.push_attribute(("history", "agentic-datas/agent_model_history.xml"));
        ag_el.push_attribute(("agentifiers", "agentic-datas/agentifiers.json"));
        ag_el.push_attribute(("sharedBytes", shared_b.as_str()));
        ag_el.push_attribute(("localizedBytes", loc_b.as_str()));
        ag_el.push_attribute(("maxSharedBytes", max_sh.as_str()));
        ag_el.push_attribute(("maxLocalizedBytes", max_loc.as_str()));
        let _ = writer.write_event(Event::Empty(ag_el));

        // Lore manifest
        let mut lore_el = BytesStart::new("lore");
        let ent_cnt = project.lore.entities.len().to_string();
        let conf_cnt = project.lore.conflicts.len().to_string();
        let plan_cnt = project.lore.plans.len().to_string();
        lore_el.push_attribute(("enabled", "true"));
        lore_el.push_attribute(("version", "1.0"));
        lore_el.push_attribute(("defaultGraph", "lore/graphs/data.grph"));
        lore_el.push_attribute(("metadata", "lore/metadata.xml"));
        lore_el.push_attribute(("entityCount", ent_cnt.as_str()));
        lore_el.push_attribute(("conflictCount", conf_cnt.as_str()));
        lore_el.push_attribute(("planCount", plan_cnt.as_str()));
        let _ = writer.write_event(Event::Empty(lore_el));

        let _ = writer.write_event(Event::End(BytesEnd::new("ao3project")));
        file_entries.insert(
            "metadata.xml".to_string(),
            writer.into_inner().into_inner(),
        );

        // 2. history.cmt and history/*.diff
        file_entries.insert(
            "history.cmt".to_string(),
            project.history_log.as_bytes().to_vec(),
        );
        for (hash, diff_text) in &project.snapshot_diffs {
            file_entries.insert(
                format!("history/{}.diff", hash),
                diff_text.as_bytes().to_vec(),
            );
        }

        // 3. stylesheet/workskin.css
        file_entries.insert(
            "stylesheet/workskin.css".to_string(),
            project.work_skin_css.as_bytes().to_vec(),
        );

        // 4. agentic-datas/*
        project.agentic_data.serialize_to_archive(&mut file_entries);

        // 5. lore/*
        project.lore.serialize_to_archive(&mut file_entries);

        // 6. Compress using ZstdArchive
        ZstdArchive::compress_archive(path, &file_entries)
    }

    fn write_text_el(writer: &mut Writer<Cursor<Vec<u8>>>, tag: &str, text: &str) {
        let _ = writer.write_event(Event::Start(BytesStart::new(tag)));
        let _ = writer.write_event(Event::Text(BytesText::new(text)));
        let _ = writer.write_event(Event::End(BytesEnd::new(tag)));
    }
}
