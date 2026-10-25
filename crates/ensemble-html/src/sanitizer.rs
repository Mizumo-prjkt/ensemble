use crate::whitelist::Ao3TagWhitelist;
use once_cell::sync::Lazy;
use regex::Regex;
use std::collections::BTreeSet;

static ENTITY_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"^&([a-zA-Z0-9]+|#[0-9]+|#x[a-fA-F0-9]+);").unwrap());
static COMMENT_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"(?s)<!--.*?-->").unwrap());
static ATTR_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r#"(?i)([a-zA-Z0-9_:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>/]+))"#).unwrap()
});
static TAG_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)</?([a-zA-Z][\w:-]*)([^>]*)/?>").unwrap());
static SCRIPT_STYLE_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)</?(?:script|style)[^>]*>").unwrap());
static STYLE_ATTR_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r#"(?i)\sstyle\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)"#).unwrap());

static DANGEROUS_TAG_RES: Lazy<Vec<(&'static str, Regex)>> = Lazy::new(|| {
    [
        "script", "style", "iframe", "object", "embed", "noscript",
    ]
    .into_iter()
    .map(|tag| {
        let pattern = format!(
            r"(?is)<{t}\b[^>]*>.*?</{t}\b[^>]*>|<{t}\b[^>]*/>|<{t}\b[^>]*>",
            t = tag
        );
        (tag, Regex::new(&pattern).unwrap())
    })
    .collect()
});

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SanitizeResult {
    pub clean_html: String,
    pub was_modified: bool,
    pub stripped_tags: Vec<String>,
    pub stripped_attributes: Vec<String>,
}

pub fn escape_html_preserving_entities(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut idx = 0;
    let bytes = text.as_bytes();
    while idx < bytes.len() {
        let ch = text[idx..].chars().next().unwrap();
        let ch_len = ch.len_utf8();
        match ch {
            '&' => {
                if let Some(m) = ENTITY_RE.find(&text[idx..]) {
                    out.push_str(m.as_str());
                    idx += m.end();
                    continue;
                } else {
                    out.push_str("&amp;");
                }
            }
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            _ => out.push(ch),
        }
        idx += ch_len;
    }
    out
}

fn unescape_quote_entities(text: &str) -> String {
    let mut out = text.to_string();
    while out.contains("&quot;") {
        out = out.replace("&amp;quot;", "&quot;");
        out = out.replace("&quot;", "\"");
    }
    while out.contains("&apos;") || out.contains("&#39;") {
        out = out.replace("&amp;apos;", "&apos;");
        out = out.replace("&apos;", "'");
        out = out.replace("&#39;", "'");
    }
    out
}

fn filter_attributes(attr_blob: &str, stripped_attrs: &mut BTreeSet<String>) -> String {
    let mut kept = Vec::new();
    for cap in ATTR_RE.captures_iter(attr_blob) {
        let name = cap[1].to_ascii_lowercase();
        if !Ao3TagWhitelist::is_allowed_attribute(&name) {
            stripped_attrs.insert(name);
            continue;
        }
        let val = cap
            .get(2)
            .or_else(|| cap.get(3))
            .or_else(|| cap.get(4))
            .map(|m| m.as_str())
            .unwrap_or("");
        let escaped_val = escape_html_preserving_entities(val).replace('"', "&quot;");
        kept.push(format!("{}=\"{}\"", name, escaped_val));
    }
    kept.join(" ")
}

pub struct Ao3HtmlSanitizer;

impl Ao3HtmlSanitizer {
    pub fn sanitize_detailed(html: &str) -> SanitizeResult {
        let mut result = SanitizeResult::default();
        let mut stripped_tags_set = BTreeSet::new();
        let mut stripped_attrs_set = BTreeSet::new();

        let mut cleaned = unescape_quote_entities(html);
        if cleaned.contains("<!--") {
            result.was_modified = true;
            cleaned = COMMENT_RE.replace_all(&cleaned, "").to_string();
        }

        for (tag_name, re) in DANGEROUS_TAG_RES.iter() {
            if re.is_match(&cleaned) {
                stripped_tags_set.insert((*tag_name).to_string());
                result.was_modified = true;
                cleaned = re.replace_all(&cleaned, "").to_string();
            }
        }

        if cleaned.trim().is_empty() {
            result.clean_html = "<p></p>".to_string();
            result.was_modified = html != result.clean_html;
            result.stripped_tags = stripped_tags_set.into_iter().collect();
            result.stripped_attributes = stripped_attrs_set.into_iter().collect();
            return result;
        }

        let mut out = String::with_capacity(cleaned.len());
        let mut open_tags: Vec<String> = Vec::new();
        let mut last_pos = 0;

        for cap in TAG_RE.captures_iter(&cleaned) {
            let full_match = cap.get(0).unwrap();
            out.push_str(&escape_html_preserving_entities(
                &cleaned[last_pos..full_match.start()],
            ));

            let raw_tag = &cap[1];
            let tag = Ao3TagWhitelist::normalize_tag(raw_tag);
            let closing = full_match.as_str().starts_with("</");
            let self_closing =
                full_match.as_str().ends_with("/>") || Ao3TagWhitelist::is_void_tag(&tag);
            let attr_str = cap.get(2).map(|m| m.as_str().trim()).unwrap_or("");
            let attrs = filter_attributes(attr_str, &mut stripped_attrs_set);

            if !Ao3TagWhitelist::is_allowed_tag(&tag) {
                stripped_tags_set.insert(raw_tag.to_ascii_lowercase());
                last_pos = full_match.end();
                continue;
            }

            if closing {
                while let Some(top) = open_tags.last() {
                    if top == &tag {
                        break;
                    }
                    let popped = open_tags.pop().unwrap();
                    out.push_str(&format!("</{}>", popped));
                }
                if open_tags.last().map(|s| s == &tag).unwrap_or(false) {
                    open_tags.pop();
                    out.push_str(&format!("</{}>", tag));
                }
            } else if self_closing {
                if attrs.is_empty() {
                    out.push_str(&format!("<{} />", tag));
                } else {
                    out.push_str(&format!("<{} {} />", tag, attrs));
                }
            } else {
                if attrs.is_empty() {
                    out.push_str(&format!("<{}>", tag));
                } else {
                    out.push_str(&format!("<{} {}>", tag, attrs));
                }
                open_tags.push(tag);
            }

            last_pos = full_match.end();
        }

        out.push_str(&escape_html_preserving_entities(&cleaned[last_pos..]));
        while let Some(popped) = open_tags.pop() {
            out.push_str(&format!("</{}>", popped));
        }

        if SCRIPT_STYLE_RE.is_match(&out) {
            stripped_tags_set.insert("script".to_string());
            out = SCRIPT_STYLE_RE.replace_all(&out, "").to_string();
        }

        if STYLE_ATTR_RE.is_match(&out) {
            stripped_attrs_set.insert("style".to_string());
            out = STYLE_ATTR_RE.replace_all(&out, "").to_string();
        }

        if out.trim().is_empty() {
            out = "<p></p>".to_string();
        }

        result.clean_html = out;
        result.stripped_tags = stripped_tags_set.into_iter().collect();
        result.stripped_attributes = stripped_attrs_set.into_iter().collect();
        result.was_modified = (result.clean_html != html)
            || !result.stripped_tags.is_empty()
            || !result.stripped_attributes.is_empty();

        result
    }

    pub fn sanitize(html: &str) -> String {
        Self::sanitize_detailed(html).clean_html
    }
}
