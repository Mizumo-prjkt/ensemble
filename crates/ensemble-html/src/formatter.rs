use crate::sanitizer::{escape_html_preserving_entities, Ao3HtmlSanitizer};
use crate::whitelist::Ao3TagWhitelist;
use once_cell::sync::Lazy;
use regex::Regex;

static P_BETWEEN_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)</p>\s*<p(\s+[^>]*)?>").unwrap());
static DIV_CLOSE_OPEN_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(?i)</(div|blockquote|h[1-6])>\s*<(div|blockquote|h[1-6]|p)(\s+[^>]*)?>").unwrap()
});
static P_DIV_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)</p>\s*<(div|blockquote|h[1-6])(\s+[^>]*)?>").unwrap());
static DIV_P_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)</(div|blockquote|h[1-6])>\s*<p(\s+[^>]*)?>").unwrap());
static LI_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"(?i)</li>\s*<li(\s+[^>]*)?>").unwrap());
static UL_LI_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)<(ul|ol)(\s+[^>]*)?>\s*<li(\s+[^>]*)?>").unwrap());
static LI_UL_CLOSE_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)</li>\s*</(ul|ol)>").unwrap());
static HR_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"(?i)\s*<hr\s*/?>\s*").unwrap());

static TOKEN_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?s)<!--.*?-->|<(/)?([a-zA-Z0-9:-]+)([^>]*)>").unwrap());
static CLASS_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r#"(?i)\bclass\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))"#).unwrap());
static ABBR_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"^([a-zA-Z0-9]+)(?:#([a-zA-Z0-9_-]+))?(?:\.([a-zA-Z0-9_.-]+))?$").unwrap()
});

pub struct HtmlFormatter;

impl HtmlFormatter {
    pub fn format_html_source(raw_html: &str) -> String {
        if raw_html.trim().is_empty() {
            return raw_html.to_string();
        }

        let mut html = raw_html.replace("\r\n", "\n").replace('\r', "\n");

        html = P_BETWEEN_RE
            .replace_all(&html, |caps: &regex::Captures| {
                let attrs = caps.get(1).map(|m| m.as_str()).unwrap_or("");
                format!("</p>\n\n<p{}>", attrs)
            })
            .to_string();

        html = DIV_CLOSE_OPEN_RE
            .replace_all(&html, |caps: &regex::Captures| {
                let close = &caps[1];
                let open = &caps[2];
                let attrs = caps.get(3).map(|m| m.as_str()).unwrap_or("");
                format!("</{}>\n\n<{}{}>", close, open, attrs)
            })
            .to_string();

        html = P_DIV_RE
            .replace_all(&html, |caps: &regex::Captures| {
                let open = &caps[1];
                let attrs = caps.get(2).map(|m| m.as_str()).unwrap_or("");
                format!("</p>\n\n<{}{}>", open, attrs)
            })
            .to_string();

        html = DIV_P_RE
            .replace_all(&html, |caps: &regex::Captures| {
                let close = &caps[1];
                let attrs = caps.get(2).map(|m| m.as_str()).unwrap_or("");
                format!("</{}>\n\n<p{}>", close, attrs)
            })
            .to_string();

        html = LI_RE
            .replace_all(&html, |caps: &regex::Captures| {
                let attrs = caps.get(1).map(|m| m.as_str()).unwrap_or("");
                format!("</li>\n  <li{}>", attrs)
            })
            .to_string();

        html = UL_LI_RE
            .replace_all(&html, |caps: &regex::Captures| {
                let list_tag = &caps[1];
                let list_attrs = caps.get(2).map(|m| m.as_str()).unwrap_or("");
                let li_attrs = caps.get(3).map(|m| m.as_str()).unwrap_or("");
                format!("<{}{}>\n  <li{}>", list_tag, list_attrs, li_attrs)
            })
            .to_string();

        html = LI_UL_CLOSE_RE
            .replace_all(&html, |caps: &regex::Captures| {
                format!("</li>\n</{}>", &caps[1])
            })
            .to_string();

        html = HR_RE.replace_all(&html, "\n\n<hr />\n\n").to_string();

        html.trim().to_string()
    }

    pub fn get_tag_path_at(text: &str, cursor_pos: usize) -> Vec<String> {
        if text.is_empty() || cursor_pos == 0 {
            return Vec::new();
        }

        let check_pos = cursor_pos.min(text.len());
        let mut tag_stack: Vec<String> = Vec::new();

        for cap in TOKEN_RE.captures_iter(text) {
            let full = cap.get(0).unwrap();
            if full.start() >= check_pos {
                break;
            }
            if full.as_str().starts_with("<!--") {
                continue;
            }

            let is_closing = cap.get(1).is_some();
            let tag_name = cap[2].to_ascii_lowercase();
            let attrs = cap.get(3).map(|m| m.as_str()).unwrap_or("");
            let is_self_closing =
                attrs.trim().ends_with('/') || Ao3TagWhitelist::is_void_tag(&tag_name);

            if is_closing {
                if let Some(pos) = tag_stack.iter().rposition(|existing| {
                    let existing_tag = existing.split('.').next().unwrap_or("");
                    existing_tag == tag_name
                }) {
                    tag_stack.truncate(pos);
                }
            } else if !is_self_closing {
                let mut display_tag = tag_name;
                if let Some(cm) = CLASS_RE.captures(attrs) {
                    let cls = cm
                        .get(1)
                        .or_else(|| cm.get(2))
                        .or_else(|| cm.get(3))
                        .map(|m| m.as_str())
                        .unwrap_or("");
                    if let Some(first_class) = cls.split_whitespace().next() {
                        if !first_class.is_empty() {
                            display_tag.push('.');
                            display_tag.push_str(first_class);
                        }
                    }
                }
                tag_stack.push(display_tag);
            }
        }

        tag_stack
    }

    pub fn format_tag_breadcrumbs(path: &[String]) -> String {
        if path.is_empty() {
            "🏷️ Tags: (root document)".to_string()
        } else {
            let joined = path
                .iter()
                .map(|p| format!("<{}>", p))
                .collect::<Vec<_>>()
                .join(" › ");
            format!("🏷️ Tags: {}", joined)
        }
    }

    pub fn wrap_or_insert_tag(
        selected: &str,
        tag: &str,
        default_class: &str,
        default_inner: &str,
    ) -> String {
        let class_attr = if default_class.is_empty() {
            String::new()
        } else {
            format!(" class=\"{}\"", default_class)
        };

        if tag == "hr" || tag == "br" {
            return format!("\n\n<{} />\n\n", tag);
        }

        if tag == "details" {
            let summary = "Spoiler / Author Note";
            let body = if selected.is_empty() {
                if default_inner.is_empty() {
                    "Hidden content here..."
                } else {
                    default_inner
                }
            } else {
                selected
            };
            return format!(
                "<details{}>\n  <summary>{}</summary>\n  <p>{}</p>\n</details>\n",
                class_attr, summary, body
            );
        }

        if tag == "ruby" {
            let base = if selected.is_empty() {
                "Base"
            } else {
                selected
            };
            let rt = if default_inner.is_empty() {
                "annotation"
            } else {
                default_inner
            };
            return format!("<ruby>{}<rt>{}</rt></ruby>", base, rt);
        }

        if tag == "div" {
            let body = if selected.is_empty() {
                if default_inner.is_empty() {
                    "Content here..."
                } else {
                    default_inner
                }
            } else {
                selected
            };
            return format!("<div{}>\n  {}\n</div>\n", class_attr, body);
        }

        if tag == "blockquote" {
            let body = if selected.is_empty() {
                if default_inner.is_empty() {
                    "Quoted text..."
                } else {
                    default_inner
                }
            } else {
                selected
            };
            return format!("<blockquote>\n  <p>{}</p>\n</blockquote>\n", body);
        }

        if tag == "a" {
            let body = if selected.is_empty() {
                if default_inner.is_empty() {
                    "link text"
                } else {
                    default_inner
                }
            } else {
                selected
            };
            return format!("<a href=\"#\">{}</a>", body);
        }

        format!("<{}{}>{}</{}>", tag, class_attr, selected, tag)
    }

    pub fn try_expand_autoblock(abbr: &str) -> Option<String> {
        let caps = ABBR_RE.captures(abbr)?;
        let tag_name = &caps[1];
        let raw_classes = caps.get(3).map(|m| m.as_str()).unwrap_or("");
        let class_attr = if raw_classes.is_empty() {
            String::new()
        } else {
            let classes: Vec<&str> = raw_classes.split('.').filter(|s| !s.is_empty()).collect();
            if classes.is_empty() {
                String::new()
            } else {
                format!(" class=\"{}\"", classes.join(" "))
            }
        };

        if Ao3TagWhitelist::is_void_tag(tag_name) {
            Some(format!("<{}{} />", tag_name, class_attr))
        } else {
            Some(format!("<{}{}>\n\n</{}>", tag_name, class_attr, tag_name))
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct RichTextInlineStyle {
    pub bold: bool,
    pub italic: bool,
    pub underline: bool,
    pub strike: bool,
    pub code: bool,
    pub superscript: bool,
    pub subscript: bool,
    pub link: String,
    pub css_class: String,
}

pub struct Ao3HtmlExporter;

impl Ao3HtmlExporter {
    pub fn wrap_inline(text: &str, style: &RichTextInlineStyle) -> String {
        let mut out = escape_html_preserving_entities(text).replace('"', "&quot;");
        if out.is_empty() {
            return out;
        }
        if style.code {
            out = format!("<code>{}</code>", out);
        }
        if style.superscript {
            out = format!("<sup>{}</sup>", out);
        }
        if style.subscript {
            out = format!("<sub>{}</sub>", out);
        }
        if style.strike {
            out = format!("<s>{}</s>", out);
        }
        if style.underline {
            out = format!("<u>{}</u>", out);
        }
        if style.italic {
            out = format!("<em>{}</em>", out);
        }
        if style.bold {
            out = format!("<strong>{}</strong>", out);
        }
        if !style.link.is_empty() {
            out = format!(
                "<a href=\"{}\">{}</a>",
                escape_html_preserving_entities(&style.link).replace('"', "&quot;"),
                out
            );
        }
        if !style.css_class.is_empty() {
            out = format!(
                "<span class=\"{}\">{}</span>",
                escape_html_preserving_entities(&style.css_class).replace('"', "&quot;"),
                out
            );
        }
        out
    }

    pub fn export_sanitized(raw_html: &str) -> String {
        Ao3HtmlSanitizer::sanitize(raw_html)
    }
}
