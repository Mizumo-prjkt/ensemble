use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum AgentActivityType {
    Presence,
    Thought,
    Action,
    Error,
}

impl AgentActivityType {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Presence => "presence",
            Self::Thought => "thought",
            Self::Action => "action",
            Self::Error => "error",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s.trim().to_ascii_lowercase().as_str() {
            "presence" => Self::Presence,
            "thought" => Self::Thought,
            "error" => Self::Error,
            _ => Self::Action,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentActivityEvent {
    pub id: String,
    pub timestamp: DateTime<Utc>,
    pub event_type: AgentActivityType,
    pub agent_name: String,
    pub agent_role: String,
    pub agent_model: String,
    pub category: String,
    pub summary: String,
    pub details: String,
    pub chapter_index: i32,
    pub word_count_delta: i32,
    pub error_message: String,
}

impl Default for AgentActivityEvent {
    fn default() -> Self {
        Self {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name: "AI Agent".to_string(),
            agent_role: "co-author".to_string(),
            agent_model: "unknown".to_string(),
            category: String::new(),
            summary: String::new(),
            details: String::new(),
            chapter_index: -1,
            word_count_delta: 0,
            error_message: String::new(),
        }
    }
}

/// Sorts events chronologically and deduplicates by `id` (or identical timestamp + summary + category).
pub fn sort_and_deduplicate_events(events: &mut Vec<AgentActivityEvent>) {
    events.sort_by(|a, b| a.timestamp.cmp(&b.timestamp));
    let mut seen_ids = std::collections::HashSet::new();
    let mut seen_tuples = std::collections::HashSet::new();
    events.retain(|ev| {
        if !ev.id.is_empty() && !seen_ids.insert(ev.id.clone()) {
            return false;
        }
        let tuple_key = (
            ev.timestamp.timestamp_millis(),
            ev.agent_name.clone(),
            ev.category.clone(),
            ev.summary.clone(),
        );
        seen_tuples.insert(tuple_key)
    });
}
