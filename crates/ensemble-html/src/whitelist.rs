use once_cell::sync::Lazy;
use std::collections::{HashMap, HashSet};

static ALLOWED_TAGS: Lazy<HashSet<&'static str>> = Lazy::new(|| {
    [
        "a",
        "abbr",
        "acronym",
        "address",
        "b",
        "big",
        "blockquote",
        "br",
        "caption",
        "center",
        "cite",
        "code",
        "col",
        "colgroup",
        "dd",
        "del",
        "details",
        "dfn",
        "div",
        "dl",
        "dt",
        "em",
        "figcaption",
        "figure",
        "h1",
        "h2",
        "h3",
        "h4",
        "h5",
        "h6",
        "hr",
        "i",
        "img",
        "ins",
        "kbd",
        "li",
        "ol",
        "p",
        "pre",
        "q",
        "rp",
        "rt",
        "ruby",
        "s",
        "samp",
        "small",
        "span",
        "strike",
        "strong",
        "sub",
        "summary",
        "sup",
        "table",
        "tbody",
        "td",
        "tfoot",
        "th",
        "thead",
        "title",
        "tr",
        "tt",
        "u",
        "ul",
        "var",
    ]
    .into_iter()
    .collect()
});

static ALLOWED_ATTRIBUTES: Lazy<HashSet<&'static str>> = Lazy::new(|| {
    [
        "align", "alt", "axis", "class", "height", "href", "name", "src", "title", "width",
    ]
    .into_iter()
    .collect()
});

static TAG_NORMALIZATIONS: Lazy<HashMap<&'static str, &'static str>> = Lazy::new(|| {
    [("b", "strong"), ("i", "em"), ("strike", "s")]
        .into_iter()
        .collect()
});

static VOID_TAGS: Lazy<HashSet<&'static str>> = Lazy::new(|| {
    ["br", "hr", "img", "col", "input", "wbr"]
        .into_iter()
        .collect()
});

pub struct Ao3TagWhitelist;

impl Ao3TagWhitelist {
    pub fn allowed_tags() -> &'static HashSet<&'static str> {
        &ALLOWED_TAGS
    }

    pub fn is_allowed_tag(tag: &str) -> bool {
        ALLOWED_TAGS.contains(tag.to_ascii_lowercase().as_str())
    }

    pub fn allowed_attributes() -> &'static HashSet<&'static str> {
        &ALLOWED_ATTRIBUTES
    }

    pub fn is_allowed_attribute(attr: &str) -> bool {
        ALLOWED_ATTRIBUTES.contains(attr.to_ascii_lowercase().as_str())
    }

    pub fn tag_normalizations() -> &'static HashMap<&'static str, &'static str> {
        &TAG_NORMALIZATIONS
    }

    pub fn normalize_tag(tag: &str) -> String {
        let lower = tag.to_ascii_lowercase();
        TAG_NORMALIZATIONS
            .get(lower.as_str())
            .map(|s| (*s).to_string())
            .unwrap_or(lower)
    }

    pub fn is_void_tag(tag: &str) -> bool {
        VOID_TAGS.contains(tag.to_ascii_lowercase().as_str())
    }
}
