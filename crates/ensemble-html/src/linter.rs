use crate::whitelist::Ao3TagWhitelist;
use ensemble_core::Ao3Project;
use once_cell::sync::Lazy;
use quick_xml::events::Event;
use quick_xml::Reader;
use regex::Regex;
use std::collections::BTreeMap;

static STYLE_ATTR_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r#"(?i)\bstyle\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+)"#).unwrap());
static TAG_NAME_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"</?([a-zA-Z][\w:-]*)").unwrap());
static CSS_CLASS_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"\.([a-zA-Z0-9_-]+)").unwrap());
static HTML_CLASS_ATTR_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r#"\bclass\s*=\s*["']([^"']+)["']"#).unwrap());

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CodeDiagnosticIssue {
    pub start_pos: usize,
    pub length: usize,
    pub message: String,
    pub is_error: bool,
}

#[derive(Debug, Clone, Default)]
pub struct HtmlLintReport {
    pub issues: Vec<CodeDiagnosticIssue>,
    pub error_count: usize,
    pub warning_count: usize,
    pub summary_text: String,
    pub detail_lines: Vec<String>,
    pub details_html: String,
    pub revision: u64,
}

#[derive(Debug, Clone, Default)]
pub struct CssLintReport {
    pub issues: Vec<CodeDiagnosticIssue>,
    pub class_names: Vec<String>,
    pub duplicates: Vec<String>,
    pub orphaned_warnings: Vec<String>,
    pub summary_text: String,
    pub details_html: String,
}

pub struct HtmlLinter;

impl HtmlLinter {
    pub fn lint_html(text: &str, revision: u64) -> HtmlLintReport {
        if text.trim().is_empty() {
            return HtmlLintReport {
                revision,
                ..Default::default()
            };
        }

        let mut issues = Vec::new();
        let mut error_count = 0usize;
        let mut warning_count = 0usize;
        let mut detail_lines = Vec::new();

        // 1. Inline style attributes
        let mut style_count = 0usize;
        for m in STYLE_ATTR_RE.find_iter(text) {
            issues.push(CodeDiagnosticIssue {
                start_pos: m.start(),
                length: m.len(),
                is_error: false,
                message: "Inline style= attributes are disallowed by AO3. Use Work Skin CSS classes instead.".to_string(),
            });
            style_count += 1;
        }
        if style_count > 0 {
            warning_count += style_count;
            detail_lines.push(format!(
                "• <b>{} inline style= attribute{}:</b> Disallowed by AO3; use Work Skin CSS classes instead.",
                style_count,
                if style_count == 1 { "" } else { "s" }
            ));
        }

        // 2. Disallowed tags according to AO3 whitelist
        let mut disallowed_tag_counts: BTreeMap<String, usize> = BTreeMap::new();
        for cap in TAG_NAME_RE.captures_iter(text) {
            let full = cap.get(0).unwrap();
            let tag_name = cap[1].to_ascii_lowercase();
            if !Ao3TagWhitelist::is_allowed_tag(&tag_name) {
                *disallowed_tag_counts.entry(tag_name.clone()).or_insert(0) += 1;
                issues.push(CodeDiagnosticIssue {
                    start_pos: full.start(),
                    length: full.len(),
                    is_error: true,
                    message: format!("<{}> tags are blocked by AO3 security whitelist.", tag_name),
                });
                error_count += 1;
            }
        }
        for (tag_name, count) in &disallowed_tag_counts {
            detail_lines.push(format!(
                "• <b>Disallowed tag &lt;{}&gt; ({}x):</b> Blocked by AO3 security whitelist.",
                tag_name, count
            ));
        }

        // 3. Mismatched tags using XML stream check
        let wrapped = format!("<root>{}</root>", text);
        let mut reader = Reader::from_str(&wrapped);
        reader.config_mut().check_end_names = true;
        let mut buf = Vec::new();
        loop {
            match reader.read_event_into(&mut buf) {
                Ok(Event::Eof) => break,
                Err(e) => {
                    let err_msg = e.to_string();
                    let pos = (reader.buffer_position() as usize).saturating_sub(6);
                    let line_err = text[..pos.min(text.len())]
                        .bytes()
                        .filter(|&b| b == b'\n')
                        .count()
                        + 1;
                    error_count += 1;
                    detail_lines.push(format!(
                        "• <b>HTML Syntax Error near line {}:</b> {}",
                        line_err,
                        err_msg
                            .replace('&', "&amp;")
                            .replace('<', "&lt;")
                            .replace('>', "&gt;")
                    ));
                    issues.push(CodeDiagnosticIssue {
                        start_pos: pos.min(text.len().saturating_sub(1)),
                        length: 1,
                        is_error: true,
                        message: format!("HTML Syntax Error: {}", err_msg),
                    });
                    break;
                }
                _ => {}
            }
            buf.clear();
        }

        let total_issues = error_count + warning_count;
        let summary_text = if total_issues == 0 {
            String::new()
        } else if error_count > 0 && warning_count > 0 {
            format!(
                "⚠️ {} HTML issue{} detected ({} error{}, {} warning{})",
                total_issues,
                if total_issues == 1 { "" } else { "s" },
                error_count,
                if error_count == 1 { "" } else { "s" },
                warning_count,
                if warning_count == 1 { "" } else { "s" }
            )
        } else if error_count > 0 {
            format!(
                "❌ {} HTML error{} detected",
                error_count,
                if error_count == 1 { "" } else { "s" }
            )
        } else {
            format!(
                "⚠️ {} HTML warning{} detected",
                warning_count,
                if warning_count == 1 { "" } else { "s" }
            )
        };

        let details_html = detail_lines.join("<br>");

        HtmlLintReport {
            issues,
            error_count,
            warning_count,
            summary_text,
            detail_lines,
            details_html,
            revision,
        }
    }
}

pub struct CssLinter;

impl CssLinter {
    pub fn lint_css(css: &str, project: Option<&Ao3Project>) -> CssLintReport {
        let mut class_names: Vec<String> = Vec::new();
        let mut duplicates: Vec<String> = Vec::new();
        let mut issues: Vec<CodeDiagnosticIssue> = Vec::new();

        for cap in CSS_CLASS_RE.captures_iter(css) {
            let full = cap.get(0).unwrap();
            let mut class_name = cap[1].to_string();
            if let Some(colon_idx) = class_name.find(':') {
                class_name.truncate(colon_idx);
            }
            if !class_name.is_empty() {
                if class_names.contains(&class_name) {
                    if !duplicates.contains(&class_name) {
                        duplicates.push(class_name.clone());
                    }
                    issues.push(CodeDiagnosticIssue {
                        start_pos: full.start(),
                        length: full.len(),
                        is_error: false,
                        message: format!(
                            "Duplicate selector '.{}' is declared multiple times in this Work Skin.",
                            class_name
                        ),
                    });
                } else {
                    class_names.push(class_name);
                }
            }
        }

        // Unclosed comments /* ...
        let mut search_from = 0;
        while let Some(rel_start) = css[search_from..].find("/*") {
            let comment_start = search_from + rel_start;
            if let Some(rel_end) = css[comment_start + 2..].find("*/") {
                search_from = comment_start + 2 + rel_end + 2;
            } else {
                issues.push(CodeDiagnosticIssue {
                    start_pos: comment_start,
                    length: (css.len() - comment_start).max(2),
                    is_error: true,
                    message: "Unclosed CSS comment: missing '*/'".to_string(),
                });
                break;
            }
        }

        // Unclosed block braces { ... }
        let mut open_braces = 0i32;
        let mut last_open_brace_pos = None;
        for (i, b) in css.bytes().enumerate() {
            if b == b'{' {
                open_braces += 1;
                last_open_brace_pos = Some(i);
            } else if b == b'}' {
                if open_braces > 0 {
                    open_braces -= 1;
                } else {
                    issues.push(CodeDiagnosticIssue {
                        start_pos: i,
                        length: 1,
                        is_error: true,
                        message: "Unexpected closing brace '}' without opening '{'".to_string(),
                    });
                }
            }
        }
        if open_braces > 0 {
            if let Some(pos) = last_open_brace_pos {
                issues.push(CodeDiagnosticIssue {
                    start_pos: pos,
                    length: 1,
                    is_error: true,
                    message: "Unclosed block: missing closing brace '}'".to_string(),
                });
            }
        }

        // Missing classes used across chapters
        let mut orphaned_warnings: Vec<String> = Vec::new();
        if let Some(proj) = project {
            let mut used_classes: BTreeMap<String, Vec<String>> = BTreeMap::new();
            for ch in &proj.chapters {
                for cap in HTML_CLASS_ATTR_RE.captures_iter(&ch.html) {
                    for cls in cap[1].split_whitespace() {
                        if !cls.is_empty() {
                            let list = used_classes.entry(cls.to_string()).or_default();
                            if !list.contains(&ch.title) {
                                list.push(ch.title.clone());
                            }
                        }
                    }
                }
            }

            for (cls, chapters_using) in &used_classes {
                if !class_names.contains(cls) {
                    if chapters_using.len() <= 2 {
                        orphaned_warnings.push(format!(
                            "• Class '.{}' is used in: {}",
                            cls,
                            chapters_using.join(", ")
                        ));
                    } else {
                        orphaned_warnings.push(format!(
                            "• Class '.{}' is used across {} chapters",
                            cls,
                            chapters_using.len()
                        ));
                    }
                }
            }
        }

        let total_issues = issues.len() + orphaned_warnings.len();
        let mut summary_text = String::new();
        let mut details_html = String::new();

        if total_issues > 0 {
            summary_text = format!(
                "⚠️ {} CSS issue{} detected",
                total_issues,
                if total_issues == 1 { "" } else { "s" }
            );
            if !duplicates.is_empty() && !orphaned_warnings.is_empty() {
                summary_text.push_str(&format!(
                    " ({} duplicate selector{}, {} missing class{})",
                    duplicates.len(),
                    if duplicates.len() == 1 { "" } else { "s" },
                    orphaned_warnings.len(),
                    if orphaned_warnings.len() == 1 { "" } else { "es" }
                ));
            } else if !duplicates.is_empty() {
                summary_text.push_str(&format!(
                    " ({} duplicate selector{})",
                    duplicates.len(),
                    if duplicates.len() == 1 { "" } else { "s" }
                ));
            } else if !orphaned_warnings.is_empty() {
                summary_text.push_str(&format!(
                    " ({} missing class{})",
                    orphaned_warnings.len(),
                    if orphaned_warnings.len() == 1 { "" } else { "es" }
                ));
            }

            let mut detail_parts = Vec::new();
            if !duplicates.is_empty() {
                detail_parts.push(format!(
                    "<b>Duplicate Selectors:</b><br>{}",
                    duplicates.join(", ")
                ));
            }
            if !orphaned_warnings.is_empty() {
                detail_parts.push(format!(
                    "<b>Missing CSS Classes used in Chapters:</b><br>{}",
                    orphaned_warnings.join("<br>")
                ));
            }
            details_html = detail_parts.join("<br><br>");
        }

        CssLintReport {
            issues,
            class_names,
            duplicates,
            orphaned_warnings,
            summary_text,
            details_html,
        }
    }

    pub fn validate_css(css: &str) -> Result<(), Vec<String>> {
        if css.trim().is_empty() {
            return Ok(());
        }

        let get_line_num = |pos: usize| -> usize {
            css[..pos.min(css.len())]
                .bytes()
                .filter(|&b| b == b'\n')
                .count()
                + 1
        };

        let mut err_list: Vec<String> = Vec::new();

        // 1. Check unclosed comments
        let mut search_from = 0;
        while let Some(rel_start) = css[search_from..].find("/*") {
            let comment_start = search_from + rel_start;
            if let Some(rel_end) = css[comment_start + 2..].find("*/") {
                search_from = comment_start + 2 + rel_end + 2;
            } else {
                err_list.push(format!(
                    "Line {}: Unclosed CSS comment (missing '*/').",
                    get_line_num(comment_start)
                ));
                break;
            }
        }

        // 2. Build masked copy of CSS
        let mut masked: Vec<u8> = css.as_bytes().to_vec();
        let mut c_start_search = 0;
        while let Some(rel) = css[c_start_search..].find("/*") {
            let c_start = c_start_search + rel;
            if let Some(rel_end) = css[c_start + 2..].find("*/") {
                let c_end = c_start + 2 + rel_end;
                for b in &mut masked[c_start..c_end + 2] {
                    if *b != b'\n' {
                        *b = b' ';
                    }
                }
                c_start_search = c_end + 2;
            } else {
                for b in &mut masked[c_start..] {
                    if *b != b'\n' {
                        *b = b' ';
                    }
                }
                break;
            }
        }

        // Mask string literals
        let mut in_quote = false;
        let mut quote_char = b'"';
        let mut q_start = 0;
        for i in 0..masked.len() {
            let ch = masked[i];
            if !in_quote {
                if ch == b'"' || ch == b'\'' {
                    in_quote = true;
                    quote_char = ch;
                    q_start = i;
                    masked[i] = b' ';
                }
            } else if ch == quote_char && (i == 0 || masked[i - 1] != b'\\') {
                in_quote = false;
                masked[i] = b' ';
            } else if ch == b'\n' {
                err_list.push(format!(
                    "Line {}: Unclosed string literal.",
                    get_line_num(q_start)
                ));
                in_quote = false;
            } else {
                masked[i] = b' ';
            }
        }
        if in_quote {
            err_list.push(format!(
                "Line {}: Unclosed string literal.",
                get_line_num(q_start)
            ));
        }

        // 3. Brace balance and declaration checking
        let mut brace_depth = 0i32;
        let mut last_open_brace_pos = None;
        let mut current_decl = String::new();
        let mut decl_start_pos = 0usize;

        let css_bytes = css.as_bytes();
        for i in 0..masked.len() {
            let ch = masked[i];
            if ch == b'{' {
                brace_depth += 1;
                last_open_brace_pos = Some(i);
                current_decl.clear();
                decl_start_pos = i + 1;
            } else if ch == b'}' {
                if brace_depth > 0 {
                    let trimmed = current_decl.trim();
                    if !trimmed.is_empty() && !trimmed.contains('{') {
                        if !trimmed.starts_with('@') && !trimmed.contains(':') {
                            err_list.push(format!(
                                "Line {}: Malformed CSS declaration '{}' (missing colon ':' separating property from value).",
                                get_line_num(decl_start_pos),
                                &trimmed[..trimmed.len().min(40)]
                            ));
                        } else if trimmed.starts_with(':') {
                            err_list.push(format!(
                                "Line {}: Missing CSS property name before colon ':'.",
                                get_line_num(decl_start_pos)
                            ));
                        }
                    }
                    current_decl.clear();
                    brace_depth -= 1;
                } else {
                    err_list.push(format!(
                        "Line {}: Unexpected closing brace '}}' without opening '{{'.",
                        get_line_num(i)
                    ));
                }
            } else if ch == b';' {
                if brace_depth > 0 {
                    let trimmed = current_decl.trim();
                    if !trimmed.is_empty() && !trimmed.contains('{') {
                        if !trimmed.starts_with('@') && !trimmed.contains(':') {
                            err_list.push(format!(
                                "Line {}: Malformed CSS declaration '{}' (missing colon ':' separating property from value).",
                                get_line_num(decl_start_pos),
                                &trimmed[..trimmed.len().min(40)]
                            ));
                        } else if trimmed.starts_with(':') {
                            err_list.push(format!(
                                "Line {}: Missing CSS property name before colon ':'.",
                                get_line_num(decl_start_pos)
                            ));
                        }
                    }
                    current_decl.clear();
                    decl_start_pos = i + 1;
                }
            } else if brace_depth > 0 {
                current_decl.push(css_bytes[i] as char);
            }
        }

        if brace_depth > 0 {
            if let Some(pos) = last_open_brace_pos {
                err_list.push(format!(
                    "Line {}: Unclosed CSS block (missing closing brace '}}').",
                    get_line_num(pos)
                ));
            }
        }

        if err_list.is_empty() {
            Ok(())
        } else {
            Err(err_list)
        }
    }
}
