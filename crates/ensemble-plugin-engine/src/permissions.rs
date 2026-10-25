use once_cell::sync::Lazy;
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginPermissions {
    pub allow_editor_read: bool,
    pub allow_editor_write: bool,
    pub allow_storage: bool,
    pub allow_ui: bool,
    pub allow_network: bool,
    /// Per-domain allow/deny map (domain -> true if allowed, false if denied)
    pub allowed_domains: BTreeMap<String, bool>,
}

impl Default for PluginPermissions {
    fn default() -> Self {
        Self {
            allow_editor_read: true,
            allow_editor_write: true,
            allow_storage: true,
            allow_ui: true,
            allow_network: false,
            allowed_domains: BTreeMap::new(),
        }
    }
}

impl PluginPermissions {
    pub fn is_domain_allowed(&self, host: &str) -> bool {
        if !self.allow_network {
            return false;
        }
        let clean_host = host.trim().to_lowercase();
        if clean_host.is_empty() {
            return false;
        }
        // Explicitly denied exact host takes precedence
        if let Some(&allowed) = self.allowed_domains.get(&clean_host) {
            return allowed;
        }
        // Check wildcard domain entries like "*.example.com" or "*"
        for (pattern, &allowed) in &self.allowed_domains {
            if !allowed {
                continue;
            }
            let clean_pat = pattern.trim().to_lowercase();
            if clean_pat == "*" {
                return true;
            }
            if let Some(suffix) = clean_pat.strip_prefix("*.") {
                if clean_host == suffix || clean_host.ends_with(&format!(".{}", suffix)) {
                    return true;
                }
            }
        }
        false
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SuspiciousCharInfo {
    pub ch: char,
    pub char_index: usize,
    pub codepoint_hex: String,
    pub codepoint_dec: u32,
    pub script_name: String,
    pub spoofed_ascii: Option<char>,
    pub spoofed_ascii_hex: Option<String>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct UrlSecurityAnalysis {
    pub raw_url: String,
    pub is_valid_url: bool,
    pub scheme: String,
    pub is_https: bool,
    pub raw_host: String,
    pub parsed_host: String,
    pub has_homograph_or_non_ascii: bool,
    pub is_punycode_idn: bool,
    pub suspicious_chars: Vec<SuspiciousCharInfo>,
    pub character_id_representation: String,
    pub host_character_id_dump: String,
    pub violation_summary: Option<String>,
}

fn identify_homograph_lookalike(ch: char) -> (&'static str, Option<char>) {
    let cp = ch as u32;
    let lookalike = match ch {
        // Cyrillic lowercase & uppercase homographs that mimic Latin letters
        '\u{0430}' => Some('a'), // Cyrillic Small Letter A
        '\u{0410}' => Some('A'), // Cyrillic Capital Letter A
        '\u{0432}' => Some('b'), // Cyrillic Small Letter Ve
        '\u{0412}' => Some('B'), // Cyrillic Capital Letter Ve
        '\u{0441}' => Some('c'), // Cyrillic Small Letter Es
        '\u{0421}' => Some('C'), // Cyrillic Capital Letter Es
        '\u{0435}' => Some('e'), // Cyrillic Small Letter Ie
        '\u{0415}' => Some('E'), // Cyrillic Capital Letter Ie
        '\u{0454}' => Some('e'), // Ukrainian Ie
        '\u{0456}' => Some('i'), // Byelorussian-Ukrainian I
        '\u{0406}' => Some('I'), // Capital Byelorussian-Ukrainian I
        '\u{0458}' => Some('j'), // Cyrillic Small Letter Je
        '\u{0408}' => Some('J'), // Cyrillic Capital Letter Je
        '\u{043A}' => Some('k'), // Cyrillic Small Letter Ka
        '\u{041A}' => Some('K'), // Cyrillic Capital Letter Ka
        '\u{043C}' => Some('m'), // Cyrillic Small Letter Em
        '\u{041C}' => Some('M'), // Cyrillic Capital Letter Em
        '\u{043D}' => Some('h'), // Cyrillic Small Letter En
        '\u{041D}' => Some('H'), // Cyrillic Capital Letter En
        '\u{043E}' => Some('o'), // Cyrillic Small Letter O
        '\u{041E}' => Some('O'), // Cyrillic Capital Letter O
        '\u{0440}' => Some('p'), // Cyrillic Small Letter Er
        '\u{0420}' => Some('P'), // Cyrillic Capital Letter Er
        '\u{0455}' => Some('s'), // Cyrillic Small Letter Dze
        '\u{0405}' => Some('S'), // Cyrillic Capital Letter Dze
        '\u{0442}' => Some('t'), // Cyrillic Small Letter Te
        '\u{0422}' => Some('T'), // Cyrillic Capital Letter Te
        '\u{0443}' => Some('y'), // Cyrillic Small Letter U
        '\u{0423}' => Some('Y'), // Cyrillic Capital Letter U
        '\u{0445}' => Some('x'), // Cyrillic Small Letter Ha
        '\u{0425}' => Some('X'), // Cyrillic Capital Letter Ha
        '\u{04BB}' => Some('h'), // Cyrillic Small Letter Shha
        // Greek homographs that mimic Latin letters
        '\u{03B1}' => Some('a'), // Greek Small Letter Alpha
        '\u{0391}' => Some('A'), // Greek Capital Letter Alpha
        '\u{0392}' => Some('B'), // Greek Capital Letter Beta
        '\u{0395}' => Some('E'), // Greek Capital Letter Epsilon
        '\u{03B5}' => Some('e'), // Greek Small Letter Epsilon
        '\u{0397}' => Some('H'), // Greek Capital Letter Eta
        '\u{0399}' => Some('I'), // Greek Capital Letter Iota
        '\u{03B9}' => Some('i'), // Greek Small Letter Iota
        '\u{039A}' => Some('K'), // Greek Capital Letter Kappa
        '\u{03BA}' => Some('k'), // Greek Small Letter Kappa
        '\u{039C}' => Some('M'), // Greek Capital Letter Mu
        '\u{039D}' => Some('N'), // Greek Capital Letter Nu
        '\u{03BD}' => Some('v'), // Greek Small Letter Nu
        '\u{039F}' => Some('O'), // Greek Capital Letter Omicron
        '\u{03BF}' => Some('o'), // Greek Small Letter Omicron
        '\u{03A1}' => Some('P'), // Greek Capital Letter Rho
        '\u{03C1}' => Some('p'), // Greek Small Letter Rho
        '\u{03A4}' => Some('T'), // Greek Capital Letter Tau
        '\u{03C4}' => Some('t'), // Greek Small Letter Tau
        '\u{03A5}' => Some('Y'), // Greek Capital Letter Upsilon
        '\u{03C5}' => Some('u'), // Greek Small Letter Upsilon
        '\u{03A7}' => Some('X'), // Greek Capital Letter Chi
        '\u{03C7}' => Some('x'), // Greek Small Letter Chi
        '\u{0396}' => Some('Z'), // Greek Capital Letter Zeta
        _ => None,
    };

    let script = match cp {
        0x0400..=0x052F | 0x2DE0..=0x2DFF | 0xA640..=0xA69F => "Cyrillic",
        0x0370..=0x03FF | 0x1F00..=0x1FFF => "Greek",
        0x0530..=0x058F => "Armenian",
        0x0590..=0x05FF => "Hebrew",
        0x0600..=0x06FF => "Arabic",
        0x13A0..=0x13FF => "Cherokee",
        0x0080..=0x024F => "Latin Extended / Diacritic",
        0x2000..=0x206F | 0xFEFF => "Zero-Width / BiDi Formatting Control",
        0xFF00..=0xFFEF => "Fullwidth / Halfwidth Form",
        0x0000..=0x0020 | 0x007F => "Control / Whitespace Character",
        _ => "Non-ASCII Unicode",
    };

    (script, lookalike)
}

/// Converts a URL or hostname into an explicit Character-ID representation
/// where non-ASCII / deceptive characters are replaced by their Unicode codepoint IDs,
/// alongside a per-character codepoint breakdown.
pub fn format_link_as_character_ids(raw_url: &str) -> String {
    let mut out = String::with_capacity(raw_url.len() + 32);
    for ch in raw_url.chars() {
        if ch.is_ascii_graphic() {
            out.push(ch);
        } else {
            let (script, lookalike) = identify_homograph_lookalike(ch);
            if let Some(ascii_eq) = lookalike {
                out.push_str(&format!(
                    "[CHAR_ID:U+{:04X} ({} lookalike for ASCII '{}' U+{:04X})]",
                    ch as u32, script, ascii_eq, ascii_eq as u32
                ));
            } else {
                out.push_str(&format!("[CHAR_ID:U+{:04X} ({})]", ch as u32, script));
            }
        }
    }
    out
}

fn extract_raw_host_and_scheme(raw_url: &str) -> (String, String) {
    let trimmed = raw_url.trim();
    let (scheme, after_scheme) = if let Some(idx) = trimmed.find("://") {
        (trimmed[..idx].to_lowercase(), &trimmed[idx + 3..])
    } else if let Some(idx) = trimmed.find(':') {
        let candidate = &trimmed[..idx];
        if candidate.chars().all(|c| c.is_ascii_alphanumeric() || c == '+' || c == '-' || c == '.') {
            (candidate.to_lowercase(), &trimmed[idx + 1..])
        } else {
            (String::new(), trimmed)
        }
    } else {
        (String::new(), trimmed)
    };

    let authority = after_scheme
        .split(['/', '?', '#'])
        .next()
        .unwrap_or("");
    let host_port = authority.rsplit('@').next().unwrap_or(authority);
    let host = if host_port.starts_with('[') {
        host_port.to_string()
    } else {
        host_port.split(':').next().unwrap_or(host_port).to_string()
    };

    (scheme, host)
}

pub fn analyze_url_security(raw_url: &str) -> UrlSecurityAnalysis {
    let trimmed = raw_url.trim();
    let (raw_scheme, raw_host) = extract_raw_host_and_scheme(trimmed);

    let mut suspicious_chars = Vec::new();
    for (idx, ch) in trimmed.chars().enumerate() {
        if !ch.is_ascii_graphic() {
            let (script_name, spoofed_ascii) = identify_homograph_lookalike(ch);
            suspicious_chars.push(SuspiciousCharInfo {
                ch,
                char_index: idx,
                codepoint_hex: format!("U+{:04X}", ch as u32),
                codepoint_dec: ch as u32,
                script_name: script_name.to_string(),
                spoofed_ascii,
                spoofed_ascii_hex: spoofed_ascii.map(|a| format!("U+{:04X}", a as u32)),
            });
        }
    }

    let parsed_opt = url::Url::parse(trimmed).ok();
    let is_valid_url = parsed_opt.is_some() && !raw_scheme.is_empty();
    let scheme = if !raw_scheme.is_empty() {
        raw_scheme
    } else {
        parsed_opt
            .as_ref()
            .map(|u| u.scheme().to_lowercase())
            .unwrap_or_default()
    };
    let is_https = scheme == "https";
    let parsed_host = parsed_opt
        .as_ref()
        .and_then(|u| u.host_str())
        .unwrap_or("")
        .to_lowercase();

    let is_punycode_idn = raw_host
        .split('.')
        .any(|label| label.to_ascii_lowercase().starts_with("xn--"))
        || parsed_host
            .split('.')
            .any(|label| label.to_ascii_lowercase().starts_with("xn--"));

    let has_homograph_or_non_ascii = !suspicious_chars.is_empty() || is_punycode_idn;
    let character_id_representation = format_link_as_character_ids(trimmed);

    let host_target = if !raw_host.is_empty() {
        &raw_host
    } else {
        &parsed_host
    };
    let host_character_id_dump = host_target
        .chars()
        .map(|c| {
            if c.is_ascii_graphic() {
                format!("'{}'(U+{:04X})", c, c as u32)
            } else {
                let (script, lookalike) = identify_homograph_lookalike(c);
                if let Some(la) = lookalike {
                    format!(
                        "[SPOOFED:U+{:04X} {}!='{}'(U+{:04X})]",
                        c as u32, script, la, la as u32
                    )
                } else {
                    format!("[NON_ASCII:U+{:04X} {}]", c as u32, script)
                }
            }
        })
        .collect::<Vec<_>>()
        .join(" ");

    let violation_summary = if !suspicious_chars.is_empty() {
        let details = suspicious_chars
            .iter()
            .map(|sc| {
                if let (Some(ascii_ch), Some(ascii_hex)) =
                    (sc.spoofed_ascii, sc.spoofed_ascii_hex.as_ref())
                {
                    format!(
                        "Character ID {} (decimal {}, {} script) disguising as ASCII '{}' ({}) at position {}",
                        sc.codepoint_hex, sc.codepoint_dec, sc.script_name, ascii_ch, ascii_hex, sc.char_index
                    )
                } else {
                    format!(
                        "Character ID {} (decimal {}, {} script) at position {}",
                        sc.codepoint_hex, sc.codepoint_dec, sc.script_name, sc.char_index
                    )
                }
            })
            .collect::<Vec<_>>()
            .join("; ");

        Some(format!(
            "Deceptive Homograph / Spoofed Link Detected! Instead of rendering the misleading link text, Ensemble invoked its raw Character IDs:\n  Link Character-ID View: {}\n  Host Character-ID Map: {}\n  Detected Spoofed Character(s): {}\n\nThis is strictly not allowed in Ensemble as it is an attempt to misguide users into connecting to a spoofed or malicious service.",
            character_id_representation, host_character_id_dump, details
        ))
    } else if is_punycode_idn {
        Some(format!(
            "Deceptive IDN Punycode Homograph Link Detected! Host '{}' (Link Character-ID View: {}) uses Punycode ('xn--') internationalized domain encoding. This is not allowed in Ensemble as it is an attempt to misguide users with lookalike characters.",
            parsed_host, character_id_representation
        ))
    } else if is_valid_url && !is_https {
        Some(format!(
            "Non-HTTPS communication attempted via '{}://' ('{}'). All plugin communications must strictly use HTTPS. Attempts to degrade communication protections are prohibited.",
            scheme, character_id_representation
        ))
    } else {
        None
    };

    UrlSecurityAnalysis {
        raw_url: trimmed.to_string(),
        is_valid_url,
        scheme,
        is_https,
        raw_host,
        parsed_host,
        has_homograph_or_non_ascii,
        is_punycode_idn,
        suspicious_chars,
        character_id_representation,
        host_character_id_dump,
        violation_summary,
    }
}

#[derive(Debug, Clone, Default)]
pub struct PluginScanReport {
    pub uses_editor_read: bool,
    pub uses_editor_write: bool,
    pub uses_storage: bool,
    pub uses_ui: bool,
    pub uses_network: bool,
    pub detected_urls: Vec<String>,
    pub detected_domains: Vec<String>,
    pub url_security_findings: Vec<UrlSecurityAnalysis>,
}

pub struct PluginScanner;

impl PluginScanner {
    pub fn scan_script(source_code: &str) -> PluginScanReport {
        static URL_RE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)[a-z][a-z0-9+.-]*://[^\s"'<>()]+"#).unwrap());
        static HTTP_CALL_RE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"http\.(?:get|post|inspect_link)\s*\(\s*["']([^"']+)["']"#).unwrap()
        });
        static DOMAIN_RE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(
                r#"\b(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)+(?:com|org|net|io|dev|app|edu|gov|co|me|info|ai|xyz|gg|tv|cc|uk|jp|de|fr|ru|cn|au|ca|br|in)\b"#,
            )
            .unwrap()
        });

        let mut report = PluginScanReport::default();

        // 1. Editor read / write detection
        if source_code.contains("editor.get_html")
            || source_code.contains("editor.get_plain_text")
            || source_code.contains("editor.get_selection")
            || source_code.contains("editor.get_chapter_title")
            || source_code.contains("editor.get_chapter_count")
            || source_code.contains("editor.get_chapter_info")
            || source_code.contains("editor.get_work_skin_css")
            || source_code.contains("editor.get_word_count")
            || source_code.contains("editor.get_chapter_text")
            || source_code.contains("editor.get_all_chapters")
            || source_code.contains("editor.get_chapter")
        {
            report.uses_editor_read = true;
        }

        if source_code.contains("editor.set_html")
            || source_code.contains("editor.insert_html")
            || source_code.contains("editor.replace_selection")
            || source_code.contains("editor.set_work_skin_css")
            || source_code.contains("editor.set_chapter_title")
            || source_code.contains("editor.insert_text")
            || source_code.contains("editor.set_chapter_text")
        {
            report.uses_editor_write = true;
        }

        // 2. Storage detection
        if source_code.contains("storage.") {
            report.uses_storage = true;
        }

        // 3. UI window / compositing detection
        if source_code.contains("ui.") {
            report.uses_ui = true;
        }

        // 4. Network & URL / Domain detection
        if source_code.contains("http.") {
            report.uses_network = true;
        }

        let mut candidate_urls = Vec::new();
        for m in URL_RE.find_iter(source_code) {
            let mut url_str = m.as_str().to_string();
            while url_str.ends_with(['.', ',', ';', ')', ']', '\'', '"']) {
                url_str.pop();
            }
            if !url_str.is_empty() && !candidate_urls.contains(&url_str) {
                candidate_urls.push(url_str);
            }
        }
        for caps in HTTP_CALL_RE.captures_iter(source_code) {
            if let Some(m) = caps.get(1) {
                let target = m.as_str().trim().to_string();
                if !target.is_empty() && !candidate_urls.contains(&target) {
                    candidate_urls.push(target);
                }
            }
        }

        for url_str in candidate_urls {
            if !report.detected_urls.contains(&url_str) {
                report.detected_urls.push(url_str.clone());
            }
            let analysis = analyze_url_security(&url_str);
            if !analysis.raw_host.is_empty() && !analysis.has_homograph_or_non_ascii {
                let host_lower = analysis.raw_host.to_lowercase();
                if !report.detected_domains.contains(&host_lower) {
                    report.detected_domains.push(host_lower);
                }
            }
            report.uses_network = true;
            report.url_security_findings.push(analysis);
        }

        // Scan for domain-like patterns inside strings if http.* is used
        if report.uses_network {
            for m in DOMAIN_RE.find_iter(source_code) {
                let d = m.as_str().to_lowercase();
                if d != "editor.get"
                    && d != "storage.get"
                    && d != "http.get"
                    && d != "http.post"
                    && !d.ends_with(".er")
                {
                    if !report.detected_domains.contains(&d) {
                        report.detected_domains.push(d);
                    }
                }
            }
        }

        report.detected_domains.sort();
        report
    }
}

