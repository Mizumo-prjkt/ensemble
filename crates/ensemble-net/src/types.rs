use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Ao3Pseud {
    pub name: String,
    pub url: String,
    pub is_default: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Ao3WorkSummary {
    pub work_id: i32,
    pub title: String,
    pub pseud: String,
    pub fandom: String,
    pub word_count: usize,
    pub chapter_count: usize,
    /// -1 = "?"
    pub total_chapters: i32,
    pub last_updated: Option<DateTime<Utc>>,
    pub is_complete: bool,
    pub is_draft: bool,

    // AO3 Metadata & Tags
    pub rating: String,
    pub archive_warnings: Vec<String>,
    pub categories: Vec<String>,
    pub fandoms: Vec<String>,
    pub relationships: Vec<String>,
    pub characters: Vec<String>,
    pub tags: Vec<String>,
    pub summary_text: String,
}

impl Default for Ao3WorkSummary {
    fn default() -> Self {
        Self {
            work_id: 0,
            title: String::new(),
            pseud: String::new(),
            fandom: String::new(),
            word_count: 0,
            chapter_count: 0,
            total_chapters: 0,
            last_updated: None,
            is_complete: false,
            is_draft: false,
            rating: "Not Rated".to_string(),
            archive_warnings: Vec::new(),
            categories: Vec::new(),
            fandoms: Vec::new(),
            relationships: Vec::new(),
            characters: Vec::new(),
            tags: Vec::new(),
            summary_text: String::new(),
        }
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Ao3ChapterContent {
    pub chapter_number: usize,
    pub title: String,
    pub body_html: String,
    pub notes_begin: String,
    pub notes_end: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Ao3ImageEmbed {
    pub url: String,
    pub alt: String,
    pub title: String,
    pub chapter_number: usize,
}

impl Default for Ao3ImageEmbed {
    fn default() -> Self {
        Self {
            url: String::new(),
            alt: String::new(),
            title: String::new(),
            chapter_number: 1,
        }
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Ao3FullWork {
    pub summary: Ao3WorkSummary,
    pub chapters: Vec<Ao3ChapterContent>,
    pub work_skin_css: String,
    pub embedded_images: Vec<Ao3ImageEmbed>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Ao3WorkSkin {
    pub skin_id: i32,
    pub name: String,
    pub css_content: String,
    pub is_work_skin: bool,
}
