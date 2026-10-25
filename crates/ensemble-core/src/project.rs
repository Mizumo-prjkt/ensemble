use crate::agentic::AgenticData;
use crate::chapter::Chapter;
use crate::diff::DiffUtil;
use crate::lore::LoreManager;
use chrono::{DateTime, Utc};
use once_cell::sync::Lazy;
use regex::Regex;
use std::collections::BTreeMap;
use uuid::Uuid;

static COMMIT_HASH_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"commit ([0-9a-fA-F\-]+)").unwrap());
static NON_ALNUM_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"[^a-zA-Z0-9_]").unwrap());

pub const DEFAULT_WORK_SKIN_CSS: &str =
    include_str!("../../../resources/default-work-skin.css");

#[derive(Debug, Clone)]
pub struct Ao3Project {
    pub title: String,
    pub author: String,
    pub rating: String,
    pub archive_warnings: Vec<String>,
    pub categories: Vec<String>,
    pub fandoms: Vec<String>,
    pub relationships: Vec<String>,
    pub characters: Vec<String>,
    pub tags: Vec<String>,
    pub created_date: DateTime<Utc>,
    pub modified_date: DateTime<Utc>,
    pub last_modified_by: String,
    pub last_modified_at: Option<DateTime<Utc>>,
    pub history_log: String,
    pub snapshot_diffs: BTreeMap<String, String>,
    pub last_snapshot_state: BTreeMap<String, String>,
    pub work_skin_css: String,
    pub agentic_data: AgenticData,
    pub lore: LoreManager,
    pub chapters: Vec<Chapter>,
    pub active_chapter_index: usize,
    pub dirty: bool,
    pub is_legacy_format: bool,
    pub file_path: String,
}

impl Default for Ao3Project {
    fn default() -> Self {
        Self::new()
    }
}

impl Ao3Project {
    pub fn new() -> Self {
        let now = Utc::now();
        let mut p = Self {
            title: "Untitled Work".to_string(),
            author: "Anonymous".to_string(),
            rating: "Not Rated".to_string(),
            archive_warnings: Vec::new(),
            categories: Vec::new(),
            fandoms: Vec::new(),
            relationships: Vec::new(),
            characters: Vec::new(),
            tags: Vec::new(),
            created_date: now,
            modified_date: now,
            last_modified_by: String::new(),
            last_modified_at: None,
            history_log: String::new(),
            snapshot_diffs: BTreeMap::new(),
            last_snapshot_state: BTreeMap::new(),
            work_skin_css: DEFAULT_WORK_SKIN_CSS.to_string(),
            agentic_data: AgenticData::new(),
            lore: LoreManager::new(),
            chapters: Vec::new(),
            active_chapter_index: 0,
            dirty: false,
            is_legacy_format: false,
            file_path: String::new(),
        };
        p.reset_to_new();
        p
    }

    pub fn reset_to_new(&mut self) {
        let now = Utc::now();
        self.title = "Untitled Work".to_string();
        self.author = "Anonymous".to_string();
        self.rating = "Not Rated".to_string();
        self.archive_warnings.clear();
        self.categories.clear();
        self.fandoms.clear();
        self.relationships.clear();
        self.characters.clear();
        self.tags.clear();
        self.created_date = now;
        self.modified_date = now;
        self.last_modified_by.clear();
        self.last_modified_at = None;
        self.history_log.clear();
        self.snapshot_diffs.clear();
        self.last_snapshot_state.clear();
        self.file_path.clear();
        self.dirty = false;
        self.is_legacy_format = false;
        self.active_chapter_index = 0;
        self.chapters.clear();
        self.agentic_data.clear();
        self.lore.clear();
        self.lore.ensure_defaults();
        self.work_skin_css = DEFAULT_WORK_SKIN_CSS.to_string();

        let mut ch = Chapter::new("Chapter 1", 0);
        ch.html = "<p></p>".to_string();
        self.chapters.push(ch);

        let author = self.author.clone();
        self.append_history_commit(&author, "Initial project creation");
    }

    pub fn add_character(&mut self, character: &str) {
        if !character.is_empty() && !self.characters.iter().any(|c| c == character) {
            self.characters.push(character.to_string());
        }
    }

    pub fn add_relationship(&mut self, rel: &str) {
        if !rel.is_empty() && !self.relationships.iter().any(|r| r == rel) {
            self.relationships.push(rel.to_string());
        }
    }

    pub fn add_fandom(&mut self, fandom: &str) {
        if !fandom.is_empty() && !self.fandoms.iter().any(|f| f == fandom) {
            self.fandoms.push(fandom.to_string());
        }
    }

    pub fn add_tag(&mut self, tag: &str) {
        if !tag.is_empty() && !self.tags.iter().any(|t| t == tag) {
            self.tags.push(tag.to_string());
        }
    }

    pub fn active_chapter(&self) -> Option<&Chapter> {
        self.chapters.get(self.active_chapter_index)
    }

    pub fn active_chapter_mut(&mut self) -> Option<&mut Chapter> {
        self.chapters.get_mut(self.active_chapter_index)
    }

    pub fn add_chapter(&mut self, title: &str) {
        let order = self.chapters.len() as i32;
        let chapter_title = if title.is_empty() {
            format!("Chapter {}", self.chapters.len() + 1)
        } else {
            title.to_string()
        };
        self.chapters.push(Chapter::new(chapter_title, order));
        self.active_chapter_index = self.chapters.len() - 1;
        self.dirty = true;
    }

    pub fn remove_chapter(&mut self, index: usize) -> Option<Chapter> {
        if index >= self.chapters.len() {
            return None;
        }
        let ch = self.chapters.remove(index);
        for (i, c) in self.chapters.iter_mut().enumerate() {
            c.order = i as i32;
        }
        if self.chapters.is_empty() {
            self.active_chapter_index = 0;
        } else if self.active_chapter_index >= self.chapters.len() {
            self.active_chapter_index = self.chapters.len() - 1;
        } else if self.active_chapter_index > index {
            self.active_chapter_index -= 1;
        }
        self.dirty = true;
        Some(ch)
    }

    pub fn move_chapter(&mut self, from: usize, to: usize) {
        if from >= self.chapters.len() || to >= self.chapters.len() || from == to {
            return;
        }
        let ch = self.chapters.remove(from);
        self.chapters.insert(to, ch);
        for (i, c) in self.chapters.iter_mut().enumerate() {
            c.order = i as i32;
        }
        if self.active_chapter_index == from {
            self.active_chapter_index = to;
        } else if from < self.active_chapter_index && to >= self.active_chapter_index {
            self.active_chapter_index -= 1;
        } else if from > self.active_chapter_index && to <= self.active_chapter_index {
            self.active_chapter_index += 1;
        }
        self.dirty = true;
    }

    pub fn sort_chapters_by_order(&mut self) {
        self.chapters.sort_by_key(|c| c.order);
    }

    pub fn mark_dirty_by(&mut self, modifier: &str) {
        self.dirty = true;
        self.last_modified_by = if modifier.is_empty() {
            "user".to_string()
        } else {
            modifier.to_string()
        };
        let now = Utc::now();
        self.last_modified_at = Some(now);
        self.modified_date = now;
    }

    pub fn latest_commit_hash(&self) -> String {
        COMMIT_HASH_RE
            .captures(&self.history_log)
            .and_then(|c| c.get(1))
            .map(|m| m.as_str().trim().to_string())
            .unwrap_or_default()
    }

    pub fn current_project_state(&self) -> BTreeMap<String, String> {
        let mut state = BTreeMap::new();
        let mut meta_text = String::new();
        meta_text.push_str(&format!("Title: {}\n", self.title));
        meta_text.push_str(&format!("Author: {}\n", self.author));
        meta_text.push_str(&format!("Rating: {}\n", self.rating));
        meta_text.push_str(&format!(
            "Archive Warnings: {}\n",
            self.archive_warnings.join(", ")
        ));
        meta_text.push_str(&format!("Categories: {}\n", self.categories.join(", ")));
        meta_text.push_str(&format!("Fandoms: {}\n", self.fandoms.join(", ")));
        meta_text.push_str(&format!(
            "Relationships: {}\n",
            self.relationships.join(", ")
        ));
        meta_text.push_str(&format!("Characters: {}\n", self.characters.join(", ")));
        meta_text.push_str(&format!("Tags: {}\n", self.tags.join(", ")));
        state.insert("metadata.txt".to_string(), meta_text);

        state.insert(
            "stylesheet/workskin.css".to_string(),
            self.work_skin_css.clone(),
        );

        for (i, ch) in self.chapters.iter().enumerate() {
            let simplified = ch.title.split_whitespace().collect::<Vec<_>>().join(" ");
            let safe_title = NON_ALNUM_RE.replace_all(&simplified, "_");
            let ch_key = format!("chapters/{:03}_{}.html", i + 1, safe_title);
            let ch_content = format!("<!-- Title: {} -->\n{}", ch.title, ch.html_for_save());
            state.insert(ch_key, ch_content);
        }

        state
    }

    /// Unloads all chapters except `active_chapter_index` out of memory onto disk cache / autosave.
    pub fn offload_inactive_chapters(&mut self) {
        let active = self.active_chapter_index.min(self.chapters.len().saturating_sub(1));
        for (idx, ch) in self.chapters.iter_mut().enumerate() {
            if idx == active {
                ch.ensure_loaded();
            } else {
                ch.unload_from_memory();
            }
        }
    }

    /// Switches active chapter while unloading the previous chapter out of RAM
    /// (to autosave if unsaved, or clean unload if saved) and loading the target chapter.
    pub fn switch_chapter_with_offload(&mut self, new_index: usize) {
        if new_index >= self.chapters.len() {
            return;
        }
        let prev = self.active_chapter_index;
        if prev < self.chapters.len() && prev != new_index {
            if let Some(prev_ch) = self.chapters.get_mut(prev) {
                prev_ch.unload_from_memory();
            }
        }
        self.active_chapter_index = new_index;
        if let Some(target_ch) = self.chapters.get_mut(new_index) {
            target_ch.ensure_loaded();
        }
    }

    pub fn mark_all_chapters_saved(&mut self) {
        self.dirty = false;
        for ch in &mut self.chapters {
            ch.mark_saved();
        }
    }

    pub fn memory_stats(&self) -> ProjectMemoryStats {
        let mut loaded_chapters = 0usize;
        let mut offloaded_chapters = 0usize;
        let mut autosaved_dirty_chapters = 0usize;
        let mut active_chapter_ram_bytes = 0usize;
        let mut offloaded_disk_bytes = 0usize;

        for ch in &self.chapters {
            if ch.unloaded {
                offloaded_chapters += 1;
                offloaded_disk_bytes += ch.cached_byte_len;
            } else {
                loaded_chapters += 1;
                active_chapter_ram_bytes += ch.html.capacity().max(ch.html.len());
            }
            if ch.dirty_chapter {
                autosaved_dirty_chapters += 1;
            }
        }

        let work_skin_css_bytes = self.work_skin_css.capacity().max(self.work_skin_css.len());
        let mut metadata_and_snapshots_bytes = self.title.len()
            + self.author.len()
            + self.history_log.len();
        for (k, v) in &self.snapshot_diffs {
            metadata_and_snapshots_bytes += k.len() + v.len();
        }
        for (k, v) in &self.last_snapshot_state {
            metadata_and_snapshots_bytes += k.len() + v.len();
        }

        let lore_and_agentic_bytes = self.agentic_data.total_shared_bytes()
            + self.agentic_data.total_localized_bytes()
            + (self.lore.entities.len() * 256);

        ProjectMemoryStats {
            loaded_chapters,
            offloaded_chapters,
            autosaved_dirty_chapters,
            active_chapter_ram_bytes,
            offloaded_disk_bytes,
            work_skin_css_bytes,
            metadata_and_snapshots_bytes,
            lore_and_agentic_bytes,
        }
    }

    pub fn append_history_commit(&mut self, author: &str, message: &str) -> String {
        let parent_hash = self.latest_commit_hash();
        let full_uuid = Uuid::new_v4().hyphenated().to_string();
        let commit_hash = full_uuid.chars().take(12).collect::<String>();
        let timestamp = Utc::now().to_rfc3339();

        let commit_author = if author.trim().is_empty() {
            let base = if self.author.is_empty() {
                "Anonymous"
            } else {
                &self.author
            };
            DiffUtil::default_author_tag(base)
        } else if !author.contains('@') {
            DiffUtil::default_author_tag(author.trim())
        } else {
            author.trim().to_string()
        };

        let current_state = self.current_project_state();
        let diff_body = if self.last_snapshot_state.is_empty() && self.snapshot_diffs.is_empty() {
            let empty_state = BTreeMap::new();
            DiffUtil::create_project_diff(&empty_state, &current_state, 3)
        } else {
            DiffUtil::create_project_diff(&self.last_snapshot_state, &current_state, 3)
        };

        let mut full_diff = String::new();
        full_diff.push_str(&format!("# Snapshot: {}\n", commit_hash));
        if !parent_hash.is_empty() {
            full_diff.push_str(&format!("# Parent:   {}\n", parent_hash));
        }
        full_diff.push_str(&format!("# Author:   {}\n", commit_author));
        full_diff.push_str(&format!("# Date:     {}\n", timestamp));
        full_diff.push_str(&format!("# Message:  {}\n\n", message.trim()));
        full_diff.push_str(&diff_body);

        self.snapshot_diffs.insert(commit_hash.clone(), full_diff);
        self.last_snapshot_state = current_state;

        let entry = format!(
            "commit {}\nAuthor: {}\nDate:   {}\n\n    {}\n\n",
            commit_hash,
            commit_author,
            timestamp,
            message.trim()
        );
        self.history_log = format!("{}{}", entry, self.history_log);
        commit_hash
    }

    pub fn snapshot_diff(&self, hash: &str) -> String {
        self.snapshot_diffs.get(hash).cloned().unwrap_or_default()
    }
}

#[derive(Debug, Clone, Default)]
pub struct ProjectMemoryStats {
    pub loaded_chapters: usize,
    pub offloaded_chapters: usize,
    pub autosaved_dirty_chapters: usize,
    pub active_chapter_ram_bytes: usize,
    pub offloaded_disk_bytes: usize,
    pub work_skin_css_bytes: usize,
    pub metadata_and_snapshots_bytes: usize,
    pub lore_and_agentic_bytes: usize,
}
