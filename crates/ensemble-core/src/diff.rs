use chrono::{DateTime, Local};
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use similar::TextDiff;
use std::collections::{BTreeMap, HashMap};
use sysinfo::System;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RgbColor {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl RgbColor {
    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }

    pub fn to_hex(&self) -> String {
        format!("#{:02x}{:02x}{:02x}", self.r, self.g, self.b)
    }

    pub fn from_css_str(s: &str) -> Option<Self> {
        let trimmed = s.trim().trim_end_matches(';').trim();
        if trimmed.is_empty() {
            return None;
        }
        if let Some(hex) = trimmed.strip_prefix('#') {
            match hex.len() {
                3 => {
                    let r = u8::from_str_radix(&hex[0..1].repeat(2), 16).ok()?;
                    let g = u8::from_str_radix(&hex[1..2].repeat(2), 16).ok()?;
                    let b = u8::from_str_radix(&hex[2..3].repeat(2), 16).ok()?;
                    return Some(Self::new(r, g, b));
                }
                6 | 8 => {
                    let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
                    let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
                    let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
                    return Some(Self::new(r, g, b));
                }
                _ => return None,
            }
        }
        match trimmed.to_ascii_lowercase().as_str() {
            "black" => Some(Self::new(0, 0, 0)),
            "white" => Some(Self::new(255, 255, 255)),
            "red" => Some(Self::new(255, 0, 0)),
            "green" => Some(Self::new(0, 128, 0)),
            "blue" => Some(Self::new(0, 0, 255)),
            "yellow" => Some(Self::new(255, 255, 0)),
            "cyan" | "aqua" => Some(Self::new(0, 255, 255)),
            "magenta" | "fuchsia" => Some(Self::new(255, 0, 255)),
            "gray" | "grey" => Some(Self::new(128, 128, 128)),
            "orange" => Some(Self::new(255, 165, 0)),
            "purple" => Some(Self::new(128, 0, 128)),
            "pink" => Some(Self::new(255, 192, 203)),
            "teal" => Some(Self::new(0, 128, 128)),
            "navy" => Some(Self::new(0, 0, 128)),
            "gold" => Some(Self::new(255, 215, 0)),
            _ => None,
        }
    }
}

static COLOR_CACHE: Lazy<Mutex<HashMap<(String, String), Option<RgbColor>>>> =
    Lazy::new(|| Mutex::new(HashMap::new()));

pub struct DiffUtil;

impl DiffUtil {
    /// Computes a unified diff between `old_text` and `new_text` for a single file.
    pub fn create_unified_diff(
        old_path: &str,
        old_text: &str,
        new_path: &str,
        new_text: &str,
        context_lines: usize,
    ) -> String {
        if old_text == new_text {
            return String::new();
        }
        let diff = TextDiff::from_lines(old_text, new_text);
        diff.unified_diff()
            .context_radius(context_lines)
            .header(&format!("a/{}", old_path), &format!("b/{}", new_path))
            .to_string()
    }

    /// Formats a full project diff between two state maps (`relative_path -> text_content`).
    pub fn create_project_diff(
        old_state: &BTreeMap<String, String>,
        new_state: &BTreeMap<String, String>,
        context_lines: usize,
    ) -> String {
        let mut all_keys: Vec<String> = old_state
            .keys()
            .chain(new_state.keys())
            .cloned()
            .collect();
        all_keys.sort();
        all_keys.dedup();

        let mut full_diff = String::new();
        for key in all_keys {
            let old_val = old_state.get(&key).map(|s| s.as_str()).unwrap_or("");
            let new_val = new_state.get(&key).map(|s| s.as_str()).unwrap_or("");
            if old_val != new_val {
                let file_diff =
                    Self::create_unified_diff(&key, old_val, &key, new_val, context_lines);
                if !file_diff.is_empty() {
                    if !full_diff.is_empty() && !full_diff.ends_with('\n') {
                        full_diff.push('\n');
                    }
                    full_diff.push_str(&file_diff);
                }
            }
        }
        full_diff
    }

    /// Extracts CSS rule declarations for a given class name (including hyphenated classes)
    /// using a direct CSS block tokenizer with zero dynamic regex compilation hazards.
    pub fn extract_css_class_declarations(css: &str, class_name: &str) -> String {
        let trimmed_class = class_name.trim();
        if trimmed_class.is_empty() || css.is_empty() {
            return String::new();
        }

        let target = format!(".{}", trimmed_class);
        let mut all_rules: Vec<String> = Vec::new();
        let bytes = css.as_bytes();
        let mut pos = 0;

        while pos < bytes.len() {
            let Some(rel_open) = css[pos..].find('{') else {
                break;
            };
            let open_brace = pos + rel_open;
            let Some(rel_close) = css[open_brace + 1..].find('}') else {
                break;
            };
            let close_brace = open_brace + 1 + rel_close;

            let selector_start = css[..open_brace]
                .rfind('}')
                .map(|idx| idx + 1)
                .unwrap_or(0);
            let selector = css[selector_start..open_brace].trim();
            let decls = css[open_brace + 1..close_brace].trim();

            let mut matches = false;
            let mut search_from = 0;
            while let Some(rel_target) = selector[search_from..].find(&target) {
                let target_pos = search_from + rel_target;
                let start_ok = if target_pos == 0 {
                    true
                } else {
                    let prev = selector[..target_pos].chars().next_back().unwrap_or(' ');
                    prev.is_whitespace()
                        || matches!(prev, ',' | '>' | '+' | '~' | '#' | '*' | ':' | ']')
                };

                let after_pos = target_pos + target.len();
                let end_ok = if after_pos >= selector.len() {
                    true
                } else {
                    let next = selector[after_pos..].chars().next().unwrap_or(' ');
                    next.is_whitespace()
                        || matches!(next, ',' | ':' | '.' | '[' | '#' | '>' | '+' | '~' | '{')
                };

                if start_ok && end_ok {
                    matches = true;
                    break;
                }
                search_from = target_pos + target.len();
            }

            if matches {
                let simplified: String = decls.split_whitespace().collect::<Vec<_>>().join(" ");
                if !simplified.is_empty() && !all_rules.contains(&simplified) {
                    all_rules.push(simplified);
                }
            }

            pos = close_brace + 1;
        }

        all_rules.join(" | ")
    }

    /// Extracts the foreground `color` property from CSS for a given class name.
    /// Properly ignores `background-color` and `border-color`.
    pub fn extract_css_color(css: &str, class_name: &str) -> Option<RgbColor> {
        let trimmed_class = class_name.trim();
        if trimmed_class.is_empty() || css.is_empty() {
            return None;
        }

        let cache_key = (css.to_string(), trimmed_class.to_string());
        {
            let cache = COLOR_CACHE.lock();
            if let Some(cached) = cache.get(&cache_key) {
                return cached.clone();
            }
        }

        let decls = Self::extract_css_class_declarations(css, trimmed_class);
        if decls.is_empty() {
            let mut cache = COLOR_CACHE.lock();
            if cache.len() > 500 {
                cache.clear();
            }
            cache.insert(cache_key, None);
            return None;
        }

        let mut found_color = None;
        for part in decls.split(|c| c == ';' || c == '|') {
            let prop = part.trim();
            if let Some((k, v)) = prop.split_once(':') {
                if k.trim().eq_ignore_ascii_case("color") {
                    let clean_val = v.split('!').next().unwrap_or(v).trim();
                    if let Some(col) = RgbColor::from_css_str(clean_val) {
                        found_color = Some(col);
                        break;
                    }
                }
            }
        }

        {
            let mut cache = COLOR_CACHE.lock();
            if cache.len() > 500 {
                cache.clear();
            }
            cache.insert(cache_key, found_color.clone());
        }

        found_color
    }

    /// Returns a system author tag formatted as `<username>@<hostname>`.
    pub fn default_author_tag(custom_name: &str) -> String {
        let user = if !custom_name.trim().is_empty() {
            custom_name.trim().to_string()
        } else {
            std::env::var("USER")
                .or_else(|_| std::env::var("USERNAME"))
                .unwrap_or_else(|_| "author".to_string())
        };
        let host = System::host_name().unwrap_or_else(|| "workstation".to_string());
        format!("{}@{}", user, host)
    }

    /// Formats an ISO 8601 UTC timestamp with local timezone representation.
    pub fn format_iso_date_with_local(iso_date: &str) -> String {
        if let Ok(dt) = DateTime::parse_from_rfc3339(iso_date) {
            let local_dt: DateTime<Local> = DateTime::from(dt);
            format!(
                "{} ({})",
                iso_date,
                local_dt.format("%b %d, %Y, %I:%M %p %:z")
            )
        } else {
            iso_date.to_string()
        }
    }
}
