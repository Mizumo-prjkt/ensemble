use crate::paths::AppPaths;
use once_cell::sync::Lazy;
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;
use uuid::Uuid;

static TAG_STRIP_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"<[^>]*>").unwrap());

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Chapter {
    pub id: Uuid,
    pub title: String,
    pub order: i32,
    pub html: String,
    #[serde(skip)]
    pub unloaded: bool,
    #[serde(skip)]
    pub dirty_chapter: bool,
    #[serde(skip)]
    pub cached_word_count: Option<usize>,
    #[serde(skip)]
    pub cached_byte_len: usize,
}

impl Default for Chapter {
    fn default() -> Self {
        Self::new("Untitled Chapter", 0)
    }
}

impl Chapter {
    pub fn new(title: impl Into<String>, order: i32) -> Self {
        let html = "<p></p>".to_string();
        let len = html.len();
        Self {
            id: Uuid::new_v4(),
            title: title.into(),
            order,
            html,
            unloaded: false,
            dirty_chapter: false,
            cached_word_count: Some(0),
            cached_byte_len: len,
        }
    }

    pub fn with_id(id: Uuid, title: impl Into<String>, order: i32, html: impl Into<String>) -> Self {
        let html_str = html.into();
        let wc = Self::fast_word_count(&html_str);
        let len = html_str.len();
        Self {
            id,
            title: title.into(),
            order,
            html: html_str,
            unloaded: false,
            dirty_chapter: false,
            cached_word_count: Some(wc),
            cached_byte_len: len,
        }
    }

    pub fn id_string(&self) -> String {
        self.id.hyphenated().to_string()
    }

    pub fn clean_cache_path(&self) -> PathBuf {
        AppPaths::autosave_dir().join(format!("ch_{}_clean.html", self.id.simple()))
    }

    pub fn autosave_cache_path(&self) -> PathBuf {
        AppPaths::autosave_dir().join(format!("ch_{}_autosave.html", self.id.simple()))
    }

    /// Single-pass zero-allocation HTML word counter.
    /// Avoids allocating multiple multi-megabyte intermediate Strings on large chapters.
    pub fn fast_word_count(html: &str) -> usize {
        let bytes = html.as_bytes();
        let mut in_tag = false;
        let mut in_word = false;
        let mut count = 0usize;
        let mut i = 0;

        while i < bytes.len() {
            let b = bytes[i];
            if b == b'<' {
                in_tag = true;
                in_word = false;
                i += 1;
                continue;
            }
            if b == b'>' {
                in_tag = false;
                in_word = false;
                i += 1;
                continue;
            }
            if !in_tag {
                // Treat &nbsp; as whitespace separator
                if b == b'&' && bytes[i..].starts_with(b"&nbsp;") {
                    in_word = false;
                    i += 6;
                    continue;
                }
                if b.is_ascii_whitespace() {
                    in_word = false;
                } else if !in_word {
                    in_word = true;
                    count += 1;
                }
            }
            i += 1;
        }
        count
    }

    /// Offloads this chapter's HTML out of RAM.
    /// - If `dirty_chapter` is true (unsaved changes), writes to the autosave file on disk.
    /// - If `dirty_chapter` is false (changes were saved), ensures the clean cache exists on disk and unloads.
    pub fn unload_from_memory(&mut self) {
        if self.unloaded {
            return;
        }
        let _ = fs::create_dir_all(AppPaths::autosave_dir());
        let wc = Self::fast_word_count(&self.html);
        self.cached_word_count = Some(wc);
        self.cached_byte_len = self.html.len();

        if self.dirty_chapter {
            let _ = fs::write(self.autosave_cache_path(), self.html.as_bytes());
        } else {
            let clean_path = self.clean_cache_path();
            if !clean_path.exists() {
                let _ = fs::write(&clean_path, self.html.as_bytes());
            }
            let _ = fs::remove_file(self.autosave_cache_path());
        }

        self.html.clear();
        self.html.shrink_to_fit();
        self.unloaded = true;
    }

    /// Reloads this chapter's HTML back into memory from autosave (if dirty) or clean disk cache.
    pub fn ensure_loaded(&mut self) {
        if !self.unloaded {
            return;
        }
        let path_to_load = if self.dirty_chapter && self.autosave_cache_path().exists() {
            self.autosave_cache_path()
        } else {
            self.clean_cache_path()
        };

        if let Ok(content) = fs::read_to_string(&path_to_load) {
            let wc = Self::fast_word_count(&content);
            self.cached_byte_len = content.len();
            self.cached_word_count = Some(wc);
            self.html = content;
        }
        self.unloaded = false;
    }

    /// Returns the chapter's HTML for saving to `.ao3proj` without forcing permanent RAM retention.
    pub fn html_for_save(&self) -> String {
        if !self.unloaded {
            return self.html.clone();
        }
        let path = if self.dirty_chapter && self.autosave_cache_path().exists() {
            self.autosave_cache_path()
        } else {
            self.clean_cache_path()
        };
        fs::read_to_string(path).unwrap_or_default()
    }

    /// Marks this chapter as saved, promoting any autosave file to the clean disk cache.
    pub fn mark_saved(&mut self) {
        let _ = fs::create_dir_all(AppPaths::autosave_dir());
        let clean_path = self.clean_cache_path();
        let autosave_path = self.autosave_cache_path();

        if !self.unloaded {
            let _ = fs::write(&clean_path, self.html.as_bytes());
        } else if self.dirty_chapter && autosave_path.exists() {
            let _ = fs::copy(&autosave_path, &clean_path);
        }
        let _ = fs::remove_file(&autosave_path);
        self.dirty_chapter = false;
    }

    pub fn set_html_modified(&mut self, new_html: String) {
        let wc = Self::fast_word_count(&new_html);
        self.cached_byte_len = new_html.len();
        self.cached_word_count = Some(wc);
        self.html = new_html;
        self.unloaded = false;
        self.dirty_chapter = true;
    }

    pub fn to_plain_text(&self) -> String {
        let source = self.html_for_save();
        let with_newlines = source
            .replace("</p>", "\n")
            .replace("<br>", "\n")
            .replace("<br/>", "\n")
            .replace("<br />", "\n")
            .replace("</blockquote>", "\n");
        let stripped = TAG_STRIP_RE.replace_all(&with_newlines, "");
        stripped
            .replace("&nbsp;", " ")
            .replace("&amp;", "&")
            .replace("&lt;", "<")
            .replace("&gt;", ">")
            .replace("&quot;", "\"")
            .replace("&#39;", "'")
            .trim()
            .to_string()
    }

    pub fn word_count(&self) -> usize {
        if self.unloaded {
            return self.cached_word_count.unwrap_or(0);
        }
        if let Some(wc) = self.cached_word_count {
            if self.cached_byte_len == self.html.len() {
                return wc;
            }
        }
        Self::fast_word_count(&self.html)
    }
}
