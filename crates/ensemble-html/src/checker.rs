use once_cell::sync::Lazy;
use regex::Regex;
use std::collections::{BTreeMap, HashSet};
use std::io::Write;
use std::process::{Command, Stdio};

static WORD_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"\b[a-zA-Z]{2,}\b").unwrap());
static REPEAT_WORD_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(?i)\b(?:the\s+the|and\s+and|is\s+is|in\s+in|of\s+of|to\s+to|that\s+that|it\s+it|for\s+for|on\s+on|with\s+with|as\s+as|at\s+at|by\s+by|from\s+from)\b").unwrap()
});
static SPACE_BEFORE_PUNCT_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"\s+[,.!?;:]").unwrap());
static MISSING_SPACE_AFTER_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"\b[a-zA-Z]+[,;][a-zA-Z]+\b").unwrap());

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CheckIssueType {
    SpellingError,
    GrammarWarning,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CheckResult {
    pub start_pos: usize,
    pub length: usize,
    pub issue_type: CheckIssueType,
    pub word: String,
    pub message: String,
}

pub struct SpellChecker {
    aspell_cmd: Option<&'static str>,
    custom_dictionary: HashSet<String>,
}

impl Default for SpellChecker {
    fn default() -> Self {
        Self::new()
    }
}

impl SpellChecker {
    pub fn new() -> Self {
        let aspell_cmd = Self::detect_backend();
        Self {
            aspell_cmd,
            custom_dictionary: HashSet::new(),
        }
    }

    fn detect_backend() -> Option<&'static str> {
        if Command::new("aspell")
            .arg("version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
        {
            return Some("aspell");
        }
        if Command::new("hunspell")
            .arg("-v")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
        {
            return Some("hunspell");
        }
        None
    }

    pub fn add_custom_word(&mut self, word: &str) {
        self.custom_dictionary.insert(word.to_ascii_lowercase());
    }

    pub fn check_text(&self, text: &str) -> Vec<CheckResult> {
        let mut results = Vec::new();
        if text.is_empty() {
            return results;
        }

        results.extend(Self::check_grammar(text));

        let mut word_positions: BTreeMap<String, Vec<usize>> = BTreeMap::new();
        let mut unique_words: Vec<String> = Vec::new();

        for m in WORD_RE.find_iter(text) {
            let word = m.as_str();
            let pos = m.start();

            if self.custom_dictionary.contains(&word.to_ascii_lowercase()) {
                continue;
            }

            if let Some(first_ch) = word.chars().next() {
                if first_ch.is_uppercase() && pos > 0 {
                    let prev_char = text[..pos].chars().next_back().unwrap_or(' ');
                    if !matches!(prev_char, '.' | '!' | '?' | '"' | '\n') {
                        continue;
                    }
                }
            }

            if !word_positions.contains_key(word) {
                unique_words.push(word.to_string());
            }
            word_positions
                .entry(word.to_string())
                .or_default()
                .push(pos);
        }

        if let Some(cmd) = self.aspell_cmd {
            if !unique_words.is_empty() {
                let misspelled = Self::check_spelling_with_backend(cmd, &unique_words);
                for bad_word in misspelled {
                    if let Some(positions) = word_positions.get(&bad_word) {
                        for &pos in positions {
                            results.push(CheckResult {
                                start_pos: pos,
                                length: bad_word.len(),
                                issue_type: CheckIssueType::SpellingError,
                                word: bad_word.clone(),
                                message: format!("Possible spelling mistake: '{}'", bad_word),
                            });
                        }
                    }
                }
            }
        }

        results
    }

    pub fn check_grammar(text: &str) -> Vec<CheckResult> {
        let mut results = Vec::new();

        for m in REPEAT_WORD_RE.find_iter(text) {
            results.push(CheckResult {
                start_pos: m.start(),
                length: m.len(),
                issue_type: CheckIssueType::GrammarWarning,
                word: m.as_str().to_string(),
                message: format!("Repeated word: '{}'", m.as_str()),
            });
        }

        for m in SPACE_BEFORE_PUNCT_RE.find_iter(text) {
            results.push(CheckResult {
                start_pos: m.start(),
                length: m.len(),
                issue_type: CheckIssueType::GrammarWarning,
                word: m.as_str().to_string(),
                message: format!("Unexpected space before punctuation: '{}'", m.as_str()),
            });
        }

        for m in MISSING_SPACE_AFTER_RE.find_iter(text) {
            results.push(CheckResult {
                start_pos: m.start(),
                length: m.len(),
                issue_type: CheckIssueType::GrammarWarning,
                word: m.as_str().to_string(),
                message: format!("Missing space after punctuation: '{}'", m.as_str()),
            });
        }

        results
    }

    fn check_spelling_with_backend(cmd: &str, words: &[String]) -> HashSet<String> {
        let mut misspelled = HashSet::new();
        let Ok(mut child) = Command::new(cmd)
            .args(["-a", "--lang=en_US"])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
        else {
            return misspelled;
        };

        if let Some(mut stdin) = child.stdin.take() {
            for word in words {
                let _ = writeln!(stdin, "^{}", word);
            }
        }

        if let Ok(output) = child.wait_with_output() {
            let stdout = String::from_utf8_lossy(&output.stdout);
            for line in stdout.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with('&') || trimmed.starts_with('#') {
                    let parts: Vec<&str> = trimmed.split_whitespace().collect();
                    if parts.len() >= 2 {
                        misspelled.insert(parts[1].to_string());
                    }
                }
            }
        }

        misspelled
    }
}
