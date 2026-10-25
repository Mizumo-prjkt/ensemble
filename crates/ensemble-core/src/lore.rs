use crate::lore_graph::{LoreGraph, LoreGraphFollowup, LoreGraphNode};
use chrono::{DateTime, Utc};
use quick_xml::events::{BytesDecl, BytesEnd, BytesStart, BytesText, Event};
use quick_xml::{Reader, Writer};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::io::Cursor;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreEntity {
    pub id: String,
    pub name: String,
    pub category: String,
    pub description: String,
    pub tags: Vec<String>,
    pub associated_characters: Vec<String>,
    pub chapters_referenced: Vec<i32>,
    pub attributes: Value,
    pub created: Option<DateTime<Utc>>,
    pub modified: Option<DateTime<Utc>>,
}

impl Default for LoreEntity {
    fn default() -> Self {
        Self {
            id: String::new(),
            name: String::new(),
            category: "Concept".to_string(),
            description: String::new(),
            tags: Vec::new(),
            associated_characters: Vec::new(),
            chapters_referenced: Vec::new(),
            attributes: json!({}),
            created: Some(Utc::now()),
            modified: Some(Utc::now()),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreHistoryEvent {
    pub id: String,
    pub title: String,
    pub era_or_date: String,
    pub summary: String,
    pub impact: String,
    pub involved_entities: Vec<String>,
    pub story_chapter: i32,
}

impl Default for LoreHistoryEvent {
    fn default() -> Self {
        Self {
            id: String::new(),
            title: String::new(),
            era_or_date: String::new(),
            summary: String::new(),
            impact: String::new(),
            involved_entities: Vec::new(),
            story_chapter: -1,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreConflict {
    pub id: String,
    pub title: String,
    pub description: String,
    pub severity: String,
    pub status: String,
    pub chapter_a: i32,
    pub chapter_b: i32,
    pub detected_by: String,
    pub detected_at: Option<DateTime<Utc>>,
}

impl Default for LoreConflict {
    fn default() -> Self {
        Self {
            id: String::new(),
            title: String::new(),
            description: String::new(),
            severity: "medium".to_string(),
            status: "open".to_string(),
            chapter_a: 0,
            chapter_b: 0,
            detected_by: "user".to_string(),
            detected_at: Some(Utc::now()),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreResolvedConflict {
    pub id: String,
    pub title: String,
    pub resolution: String,
    pub canon_ruling: String,
    pub resolved_by: String,
    pub resolved_at: Option<DateTime<Utc>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LorePlan {
    pub id: String,
    pub title: String,
    pub description: String,
    pub target_chapter: i32,
    pub status: String,
    pub lore_thread: String,
    pub needed_follow_ups: Vec<String>,
    pub notes: String,
}

impl Default for LorePlan {
    fn default() -> Self {
        Self {
            id: String::new(),
            title: String::new(),
            description: String::new(),
            target_chapter: 0,
            status: "planned".to_string(),
            lore_thread: String::new(),
            needed_follow_ups: Vec::new(),
            notes: String::new(),
        }
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct LoreReference {
    pub id: String,
    pub title: String,
    pub source: String,
    pub citation: String,
    pub applicability: String,
    pub notes: String,
}

#[derive(Debug, Clone)]
pub struct LoreManager {
    pub entities: Vec<LoreEntity>,
    pub history_events: Vec<LoreHistoryEvent>,
    pub conflicts: Vec<LoreConflict>,
    pub resolved_conflicts: Vec<LoreResolvedConflict>,
    pub plans: Vec<LorePlan>,
    pub references: Vec<LoreReference>,
    pub default_graph: LoreGraph,
    pub named_graphs: BTreeMap<String, LoreGraph>,
    pub last_updated: DateTime<Utc>,
    pub last_modified_by: String,
}

impl Default for LoreManager {
    fn default() -> Self {
        Self::new()
    }
}

impl LoreManager {
    pub fn new() -> Self {
        let mut s = Self {
            entities: Vec::new(),
            history_events: Vec::new(),
            conflicts: Vec::new(),
            resolved_conflicts: Vec::new(),
            plans: Vec::new(),
            references: Vec::new(),
            default_graph: LoreGraph::new(),
            named_graphs: BTreeMap::new(),
            last_updated: Utc::now(),
            last_modified_by: "user".to_string(),
        };
        s.clear();
        s
    }

    pub fn clear(&mut self) {
        self.entities.clear();
        self.history_events.clear();
        self.conflicts.clear();
        self.resolved_conflicts.clear();
        self.plans.clear();
        self.references.clear();
        self.default_graph.clear();
        self.named_graphs.clear();
        self.last_updated = Utc::now();
        self.last_modified_by = "user".to_string();
    }

    pub fn ensure_defaults(&mut self) {
        self.last_updated = Utc::now();
        if self.default_graph.nodes.is_empty() {
            self.default_graph.title = "Story Lore & Narrative Arc".to_string();
            self.default_graph.direction = "LR".to_string();

            self.default_graph.add_or_update_node(LoreGraphNode {
                id: "main_protagonist".to_string(),
                label: "Protagonist".to_string(),
                node_type: "character".to_string(),
                x: 80.0,
                y: 120.0,
                color: "#3b82f6".to_string(),
                shape: "rounded".to_string(),
                ..Default::default()
            });

            self.default_graph.add_or_update_node(LoreGraphNode {
                id: "inciting_incident".to_string(),
                label: "Inciting Incident".to_string(),
                node_type: "event".to_string(),
                x: 360.0,
                y: 120.0,
                color: "#f59e0b".to_string(),
                shape: "diamond".to_string(),
                ..Default::default()
            });

            self.default_graph.add_or_update_node(LoreGraphNode {
                id: "key_setting".to_string(),
                label: "Primary Setting".to_string(),
                node_type: "location".to_string(),
                x: 640.0,
                y: 120.0,
                color: "#10b981".to_string(),
                shape: "box".to_string(),
                ..Default::default()
            });

            self.default_graph.connect_nodes(
                "main_protagonist",
                "inciting_incident",
                "->",
                "triggers",
                "solid",
                "#94a3b8",
            );
            self.default_graph.connect_nodes(
                "inciting_incident",
                "key_setting",
                "->",
                "takes place in",
                "solid",
                "#94a3b8",
            );

            self.default_graph.add_or_update_followup(LoreGraphFollowup {
                node_id: "inciting_incident".to_string(),
                target_chapter: 1,
                status: "pending".to_string(),
                note: "Establish character stakes and core premise.".to_string(),
            });
        }
    }

    pub fn add_or_update_entity(&mut self, entity: LoreEntity) {
        if let Some(existing) = self.entities.iter_mut().find(|e| e.id == entity.id) {
            *existing = entity;
        } else {
            self.entities.push(entity);
        }
        self.last_updated = Utc::now();
    }

    pub fn remove_entity(&mut self, id: &str) -> bool {
        let len = self.entities.len();
        self.entities.retain(|e| e.id != id);
        if self.entities.len() < len {
            self.last_updated = Utc::now();
            true
        } else {
            false
        }
    }

    pub fn find_entity(&self, id: &str) -> Option<&LoreEntity> {
        self.entities.iter().find(|e| e.id == id)
    }

    pub fn add_or_update_history_event(&mut self, event: LoreHistoryEvent) {
        if let Some(existing) = self.history_events.iter_mut().find(|e| e.id == event.id) {
            *existing = event;
        } else {
            self.history_events.push(event);
        }
        self.last_updated = Utc::now();
    }

    pub fn remove_history_event(&mut self, id: &str) -> bool {
        let len = self.history_events.len();
        self.history_events.retain(|e| e.id != id);
        self.history_events.len() < len
    }

    pub fn add_or_update_conflict(&mut self, conflict: LoreConflict) {
        if let Some(existing) = self.conflicts.iter_mut().find(|c| c.id == conflict.id) {
            *existing = conflict;
        } else {
            self.conflicts.push(conflict);
        }
        self.last_updated = Utc::now();
    }

    pub fn resolve_conflict(
        &mut self,
        conflict_id: &str,
        resolution: &str,
        canon_ruling: &str,
        resolved_by: &str,
    ) -> bool {
        if let Some(idx) = self.conflicts.iter().position(|c| c.id == conflict_id) {
            let c = self.conflicts.remove(idx);
            self.resolved_conflicts.push(LoreResolvedConflict {
                id: c.id,
                title: c.title,
                resolution: resolution.to_string(),
                canon_ruling: canon_ruling.to_string(),
                resolved_by: if resolved_by.is_empty() {
                    "user".to_string()
                } else {
                    resolved_by.to_string()
                },
                resolved_at: Some(Utc::now()),
            });
            self.last_updated = Utc::now();
            true
        } else {
            false
        }
    }

    pub fn add_or_update_plan(&mut self, plan: LorePlan) {
        if let Some(existing) = self.plans.iter_mut().find(|p| p.id == plan.id) {
            *existing = plan;
        } else {
            self.plans.push(plan);
        }
        self.last_updated = Utc::now();
    }

    pub fn remove_plan(&mut self, id: &str) -> bool {
        let len = self.plans.len();
        self.plans.retain(|p| p.id != id);
        self.plans.len() < len
    }

    pub fn add_or_update_reference(&mut self, reference: LoreReference) {
        if let Some(existing) = self.references.iter_mut().find(|r| r.id == reference.id) {
            *existing = reference;
        } else {
            self.references.push(reference);
        }
        self.last_updated = Utc::now();
    }

    pub fn remove_reference(&mut self, id: &str) -> bool {
        let len = self.references.len();
        self.references.retain(|r| r.id != id);
        self.references.len() < len
    }

    pub fn graph(&self, name: &str) -> Option<&LoreGraph> {
        if name.is_empty() || name == "data" {
            Some(&self.default_graph)
        } else {
            self.named_graphs.get(name)
        }
    }

    pub fn graph_mut(&mut self, name: &str) -> Option<&mut LoreGraph> {
        if name.is_empty() || name == "data" {
            Some(&mut self.default_graph)
        } else {
            self.named_graphs.get_mut(name)
        }
    }

    pub fn serialize_to_archive(&self, file_entries: &mut BTreeMap<String, Vec<u8>>) {
        file_entries.insert(
            "lore/lore_data.json".to_string(),
            self.serialize_entities_json(),
        );
        file_entries.insert(
            "lore/history.json".to_string(),
            self.serialize_history_json(),
        );
        file_entries.insert(
            "lore/conflicts.json".to_string(),
            self.serialize_conflicts_json(),
        );
        file_entries.insert(
            "lore/resolved.json".to_string(),
            self.serialize_resolved_json(),
        );
        file_entries.insert("lore/plans.json".to_string(), self.serialize_plans_json());
        file_entries.insert(
            "lore/references.json".to_string(),
            self.serialize_references_json(),
        );
        file_entries.insert(
            "lore/metadata.xml".to_string(),
            self.serialize_metadata_xml(),
        );
        file_entries.insert(
            "lore/graphs/data.grph".to_string(),
            self.default_graph.to_grph_bytes(),
        );
        for (name, g) in &self.named_graphs {
            file_entries.insert(format!("lore/graphs/{}.grph", name), g.to_grph_bytes());
        }
    }

    pub fn deserialize_from_archive(
        &mut self,
        entries: &BTreeMap<String, Vec<u8>>,
    ) -> Result<(), String> {
        self.clear();
        let mut has_any_lore = false;

        if let Some(b) = entries.get("lore/lore_data.json") {
            self.deserialize_entities_json(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/history.json") {
            self.deserialize_history_json(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/conflicts.json") {
            self.deserialize_conflicts_json(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/resolved.json") {
            self.deserialize_resolved_json(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/plans.json") {
            self.deserialize_plans_json(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/references.json") {
            self.deserialize_references_json(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/metadata.xml") {
            self.deserialize_metadata_xml(b);
            has_any_lore = true;
        }
        if let Some(b) = entries.get("lore/graphs/data.grph") {
            let _ = self.default_graph.from_grph_bytes(b);
            has_any_lore = true;
        }

        for (key, val) in entries {
            if let Some(rest) = key.strip_prefix("lore/graphs/") {
                if let Some(base_name) = rest.strip_suffix(".grph") {
                    if base_name != "data" {
                        let mut g = LoreGraph::new();
                        if g.from_grph_bytes(val).is_ok() {
                            self.named_graphs.insert(base_name.to_string(), g);
                        }
                    }
                }
            }
        }

        if !has_any_lore {
            self.ensure_defaults();
        }
        Ok(())
    }

    fn serialize_entities_json(&self) -> Vec<u8> {
        let arr: Vec<Value> = self
            .entities
            .iter()
            .map(|e| {
                json!({
                    "id": e.id,
                    "name": e.name,
                    "category": e.category,
                    "description": e.description,
                    "tags": e.tags,
                    "associatedCharacters": e.associated_characters,
                    "chaptersReferenced": e.chapters_referenced,
                    "attributes": e.attributes,
                })
            })
            .collect();
        let root = json!({
            "format": "ensemble-lore-data-v1",
            "lastUpdated": self.last_updated.to_rfc3339(),
            "entities": arr
        });
        serde_json::to_vec_pretty(&root).unwrap_or_default()
    }

    fn deserialize_entities_json(&mut self, bytes: &[u8]) {
        let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
            return;
        };
        let Some(arr) = doc.get("entities").and_then(|v| v.as_array()) else {
            return;
        };
        self.entities.clear();
        for item in arr {
            if let Some(o) = item.as_object() {
                let e = LoreEntity {
                    id: o.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    name: o.get("name").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    category: o
                        .get("category")
                        .and_then(|v| v.as_str())
                        .unwrap_or("Concept")
                        .to_string(),
                    description: o
                        .get("description")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    tags: o
                        .get("tags")
                        .and_then(|v| v.as_array())
                        .map(|a| {
                            a.iter()
                                .filter_map(|x| x.as_str().map(String::from))
                                .collect()
                        })
                        .unwrap_or_default(),
                    associated_characters: o
                        .get("associatedCharacters")
                        .and_then(|v| v.as_array())
                        .map(|a| {
                            a.iter()
                                .filter_map(|x| x.as_str().map(String::from))
                                .collect()
                        })
                        .unwrap_or_default(),
                    chapters_referenced: o
                        .get("chaptersReferenced")
                        .and_then(|v| v.as_array())
                        .map(|a| {
                            a.iter()
                                .filter_map(|x| x.as_i64().map(|n| n as i32))
                                .collect()
                        })
                        .unwrap_or_default(),
                    attributes: o.get("attributes").cloned().unwrap_or_else(|| json!({})),
                    created: None,
                    modified: None,
                };
                self.entities.push(e);
            }
        }
    }

    fn serialize_history_json(&self) -> Vec<u8> {
        let arr: Vec<Value> = self
            .history_events
            .iter()
            .map(|h| {
                json!({
                    "id": h.id,
                    "title": h.title,
                    "eraOrDate": h.era_or_date,
                    "summary": h.summary,
                    "impact": h.impact,
                    "storyChapter": h.story_chapter,
                    "involvedEntities": h.involved_entities,
                })
            })
            .collect();
        let root = json!({
            "format": "ensemble-lore-history-v1",
            "lastUpdated": self.last_updated.to_rfc3339(),
            "events": arr
        });
        serde_json::to_vec_pretty(&root).unwrap_or_default()
    }

    fn deserialize_history_json(&mut self, bytes: &[u8]) {
        let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
            return;
        };
        let Some(arr) = doc.get("events").and_then(|v| v.as_array()) else {
            return;
        };
        self.history_events.clear();
        for item in arr {
            if let Some(o) = item.as_object() {
                self.history_events.push(LoreHistoryEvent {
                    id: o.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    title: o.get("title").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    era_or_date: o
                        .get("eraOrDate")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    summary: o
                        .get("summary")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    impact: o
                        .get("impact")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    story_chapter: o
                        .get("storyChapter")
                        .and_then(|v| v.as_i64())
                        .unwrap_or(-1) as i32,
                    involved_entities: o
                        .get("involvedEntities")
                        .and_then(|v| v.as_array())
                        .map(|a| {
                            a.iter()
                                .filter_map(|x| x.as_str().map(String::from))
                                .collect()
                        })
                        .unwrap_or_default(),
                });
            }
        }
    }

    fn serialize_conflicts_json(&self) -> Vec<u8> {
        let arr: Vec<Value> = self
            .conflicts
            .iter()
            .map(|c| {
                json!({
                    "id": c.id,
                    "title": c.title,
                    "description": c.description,
                    "severity": c.severity,
                    "status": c.status,
                    "chapterA": c.chapter_a,
                    "chapterB": c.chapter_b,
                    "detectedBy": c.detected_by,
                })
            })
            .collect();
        let root = json!({
            "format": "ensemble-lore-conflicts-v1",
            "lastUpdated": self.last_updated.to_rfc3339(),
            "conflicts": arr
        });
        serde_json::to_vec_pretty(&root).unwrap_or_default()
    }

    fn deserialize_conflicts_json(&mut self, bytes: &[u8]) {
        let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
            return;
        };
        let Some(arr) = doc.get("conflicts").and_then(|v| v.as_array()) else {
            return;
        };
        self.conflicts.clear();
        for item in arr {
            if let Some(o) = item.as_object() {
                self.conflicts.push(LoreConflict {
                    id: o.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    title: o.get("title").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    description: o
                        .get("description")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    severity: o
                        .get("severity")
                        .and_then(|v| v.as_str())
                        .unwrap_or("medium")
                        .to_string(),
                    status: o
                        .get("status")
                        .and_then(|v| v.as_str())
                        .unwrap_or("open")
                        .to_string(),
                    chapter_a: o.get("chapterA").and_then(|v| v.as_i64()).unwrap_or(0) as i32,
                    chapter_b: o.get("chapterB").and_then(|v| v.as_i64()).unwrap_or(0) as i32,
                    detected_by: o
                        .get("detectedBy")
                        .and_then(|v| v.as_str())
                        .unwrap_or("user")
                        .to_string(),
                    detected_at: None,
                });
            }
        }
    }

    fn serialize_resolved_json(&self) -> Vec<u8> {
        let arr: Vec<Value> = self
            .resolved_conflicts
            .iter()
            .map(|r| {
                json!({
                    "id": r.id,
                    "title": r.title,
                    "resolution": r.resolution,
                    "canonRuling": r.canon_ruling,
                    "resolvedBy": r.resolved_by,
                })
            })
            .collect();
        let root = json!({
            "format": "ensemble-lore-resolved-v1",
            "lastUpdated": self.last_updated.to_rfc3339(),
            "resolved": arr
        });
        serde_json::to_vec_pretty(&root).unwrap_or_default()
    }

    fn deserialize_resolved_json(&mut self, bytes: &[u8]) {
        let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
            return;
        };
        let Some(arr) = doc.get("resolved").and_then(|v| v.as_array()) else {
            return;
        };
        self.resolved_conflicts.clear();
        for item in arr {
            if let Some(o) = item.as_object() {
                self.resolved_conflicts.push(LoreResolvedConflict {
                    id: o.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    title: o.get("title").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    resolution: o
                        .get("resolution")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    canon_ruling: o
                        .get("canonRuling")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    resolved_by: o
                        .get("resolvedBy")
                        .and_then(|v| v.as_str())
                        .unwrap_or("user")
                        .to_string(),
                    resolved_at: None,
                });
            }
        }
    }

    fn serialize_plans_json(&self) -> Vec<u8> {
        let arr: Vec<Value> = self
            .plans
            .iter()
            .map(|p| {
                json!({
                    "id": p.id,
                    "title": p.title,
                    "description": p.description,
                    "targetChapter": p.target_chapter,
                    "status": p.status,
                    "loreThread": p.lore_thread,
                    "neededFollowUps": p.needed_follow_ups,
                    "notes": p.notes,
                })
            })
            .collect();
        let root = json!({
            "format": "ensemble-lore-plans-v1",
            "lastUpdated": self.last_updated.to_rfc3339(),
            "plans": arr
        });
        serde_json::to_vec_pretty(&root).unwrap_or_default()
    }

    fn deserialize_plans_json(&mut self, bytes: &[u8]) {
        let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
            return;
        };
        let Some(arr) = doc.get("plans").and_then(|v| v.as_array()) else {
            return;
        };
        self.plans.clear();
        for item in arr {
            if let Some(o) = item.as_object() {
                self.plans.push(LorePlan {
                    id: o.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    title: o.get("title").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    description: o
                        .get("description")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    target_chapter: o
                        .get("targetChapter")
                        .and_then(|v| v.as_i64())
                        .unwrap_or(0) as i32,
                    status: o
                        .get("status")
                        .and_then(|v| v.as_str())
                        .unwrap_or("planned")
                        .to_string(),
                    lore_thread: o
                        .get("loreThread")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    needed_follow_ups: o
                        .get("neededFollowUps")
                        .and_then(|v| v.as_array())
                        .map(|a| {
                            a.iter()
                                .filter_map(|x| x.as_str().map(String::from))
                                .collect()
                        })
                        .unwrap_or_default(),
                    notes: o.get("notes").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                });
            }
        }
    }

    fn serialize_references_json(&self) -> Vec<u8> {
        let arr: Vec<Value> = self
            .references
            .iter()
            .map(|r| {
                json!({
                    "id": r.id,
                    "title": r.title,
                    "source": r.source,
                    "citation": r.citation,
                    "applicability": r.applicability,
                    "notes": r.notes,
                })
            })
            .collect();
        let root = json!({
            "format": "ensemble-lore-references-v1",
            "lastUpdated": self.last_updated.to_rfc3339(),
            "references": arr
        });
        serde_json::to_vec_pretty(&root).unwrap_or_default()
    }

    fn deserialize_references_json(&mut self, bytes: &[u8]) {
        let Ok(doc) = serde_json::from_slice::<Value>(bytes) else {
            return;
        };
        let Some(arr) = doc.get("references").and_then(|v| v.as_array()) else {
            return;
        };
        self.references.clear();
        for item in arr {
            if let Some(o) = item.as_object() {
                self.references.push(LoreReference {
                    id: o.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    title: o.get("title").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                    source: o
                        .get("source")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    citation: o
                        .get("citation")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    applicability: o
                        .get("applicability")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string(),
                    notes: o.get("notes").and_then(|v| v.as_str()).unwrap_or("").to_string(),
                });
            }
        }
    }

    pub fn serialize_metadata_xml(&self) -> Vec<u8> {
        let mut writer = Writer::new_with_indent(Cursor::new(Vec::new()), b' ', 4);
        let _ = writer.write_event(Event::Decl(BytesDecl::new("1.0", Some("UTF-8"), None)));

        let mut root = BytesStart::new("lore-metadata");
        root.push_attribute(("version", "1.0"));
        let _ = writer.write_event(Event::Start(root));

        let _ = writer.write_event(Event::Start(BytesStart::new("stats")));
        for (tag, val) in [
            ("entityCount", self.entities.len()),
            ("historyCount", self.history_events.len()),
            ("conflictCount", self.conflicts.len()),
            ("resolvedCount", self.resolved_conflicts.len()),
            ("planCount", self.plans.len()),
            ("referenceCount", self.references.len()),
            ("graphCount", 1 + self.named_graphs.len()),
            ("nodeCount", self.default_graph.nodes.len()),
            ("connectorCount", self.default_graph.connectors.len()),
        ] {
            let _ = writer.write_event(Event::Start(BytesStart::new(tag)));
            let _ = writer.write_event(Event::Text(BytesText::new(&val.to_string())));
            let _ = writer.write_event(Event::End(BytesEnd::new(tag)));
        }
        let _ = writer.write_event(Event::End(BytesEnd::new("stats")));

        let _ = writer.write_event(Event::Start(BytesStart::new("defaultGraph")));
        let _ = writer.write_event(Event::Text(BytesText::new("lore/graphs/data.grph")));
        let _ = writer.write_event(Event::End(BytesEnd::new("defaultGraph")));

        let _ = writer.write_event(Event::Start(BytesStart::new("lastUpdated")));
        let _ = writer.write_event(Event::Text(BytesText::new(&self.last_updated.to_rfc3339())));
        let _ = writer.write_event(Event::End(BytesEnd::new("lastUpdated")));

        let _ = writer.write_event(Event::Start(BytesStart::new("lastModifiedBy")));
        let _ = writer.write_event(Event::Text(BytesText::new(&self.last_modified_by)));
        let _ = writer.write_event(Event::End(BytesEnd::new("lastModifiedBy")));

        let _ = writer.write_event(Event::End(BytesEnd::new("lore-metadata")));
        writer.into_inner().into_inner()
    }

    pub fn deserialize_metadata_xml(&mut self, xml_bytes: &[u8]) -> bool {
        let mut reader = Reader::from_reader(xml_bytes);
        reader.config_mut().trim_text(true);
        let mut buf = Vec::new();
        let mut current_tag = String::new();
        loop {
            match reader.read_event_into(&mut buf) {
                Ok(Event::Start(e)) => {
                    current_tag = String::from_utf8_lossy(e.name().as_ref()).to_string();
                }
                Ok(Event::Text(t)) => {
                    let text = t.unescape().unwrap_or_default().to_string();
                    if current_tag == "lastUpdated" {
                        if let Ok(dt) = DateTime::parse_from_rfc3339(&text) {
                            self.last_updated = dt.with_timezone(&Utc);
                        }
                    } else if current_tag == "lastModifiedBy" {
                        self.last_modified_by = text;
                    }
                }
                Ok(Event::End(_)) => current_tag.clear(),
                Ok(Event::Eof) | Err(_) => break,
                _ => {}
            }
            buf.clear();
        }
        true
    }

    pub fn to_json_summary(&self) -> Value {
        let mut cat_counts = BTreeMap::new();
        for e in &self.entities {
            *cat_counts.entry(e.category.clone()).or_insert(0usize) += 1;
        }
        json!({
            "entityCount": self.entities.len(),
            "historyCount": self.history_events.len(),
            "openConflictCount": self.conflicts.len(),
            "resolvedConflictCount": self.resolved_conflicts.len(),
            "planCount": self.plans.len(),
            "referenceCount": self.references.len(),
            "graphNodesCount": self.default_graph.nodes.len(),
            "graphBoxesCount": self.default_graph.boxes.len(),
            "graphConnectorsCount": self.default_graph.connectors.len(),
            "lastUpdated": self.last_updated.to_rfc3339(),
            "entityCategories": cat_counts
        })
    }
}
