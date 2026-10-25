use crate::crypto::AesCipher;
use chrono::{DateTime, Utc};
use quick_xml::events::{BytesDecl, BytesEnd, BytesStart, BytesText, Event};
use quick_xml::{Reader, Writer};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::io::Cursor;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentModelHistoryEntry {
    pub id: String,
    pub timestamp: DateTime<Utc>,
    pub agent_name: String,
    pub model: String,
    pub role: String,
    pub action: String,
    pub summary: String,
    pub chapter_index: i32,
    pub word_count_delta: i32,
    pub details: String,
}

impl AgentModelHistoryEntry {
    pub fn to_json(&self) -> Value {
        let mut obj = json!({
            "id": self.id,
            "timestamp": self.timestamp.to_rfc3339(),
            "agentName": self.agent_name,
            "model": self.model,
            "role": self.role,
            "action": self.action,
            "summary": self.summary,
            "wordCountDelta": self.word_count_delta,
        });
        if let Some(map) = obj.as_object_mut() {
            if self.chapter_index >= 0 {
                map.insert("chapterIndex".to_string(), json!(self.chapter_index));
            }
            if !self.details.is_empty() {
                map.insert("details".to_string(), json!(self.details));
            }
        }
        obj
    }
}

#[derive(Debug, Clone)]
pub struct AgenticData {
    shared_files: BTreeMap<String, Vec<u8>>,
    agent_files: BTreeMap<String, BTreeMap<String, Vec<u8>>>,
    history_entries: Vec<AgentModelHistoryEntry>,
    last_changes_by_model: BTreeMap<String, AgentModelHistoryEntry>,
    agentifiers: Value,
    created_date: DateTime<Utc>,
    modified_date: DateTime<Utc>,
    project_identifier: String,
}

impl Default for AgenticData {
    fn default() -> Self {
        Self::new()
    }
}

impl AgenticData {
    pub const MAX_SHARED_BYTES: usize = 5 * 1024 * 1024; // 5 MB
    pub const MAX_LOCALIZED_BYTES: usize = 20 * 1024 * 1024; // 20 MB

    pub fn new() -> Self {
        let now = Utc::now();
        let pid = Uuid::new_v4().hyphenated().to_string();
        let mut s = Self {
            shared_files: BTreeMap::new(),
            agent_files: BTreeMap::new(),
            history_entries: Vec::new(),
            last_changes_by_model: BTreeMap::new(),
            agentifiers: Value::Null,
            created_date: now,
            modified_date: now,
            project_identifier: pid,
        };
        s.clear();
        s
    }

    pub fn clear(&mut self) {
        self.shared_files.clear();
        self.agent_files.clear();
        self.history_entries.clear();
        self.last_changes_by_model.clear();
        self.created_date = Utc::now();
        self.modified_date = self.created_date;
        self.project_identifier = Uuid::new_v4().hyphenated().to_string();
        self.ensure_default_agentifiers();
        self.ensure_default_history();
    }

    pub fn ensure_default_agentifiers(&mut self) {
        self.agentifiers = json!({
            "format": "ensemble-agentifiers-v1",
            "projectIdentifier": self.project_identifier,
            "created": self.created_date.to_rfc3339(),
            "modified": self.modified_date.to_rfc3339(),
            "agents": {}
        });
    }

    fn record_model_change(&mut self, entry: &AgentModelHistoryEntry) {
        let ag_key = entry.agent_name.to_lowercase();
        self.last_changes_by_model.insert(ag_key.clone(), entry.clone());
        let model_lower = entry.model.to_lowercase();
        if !entry.model.is_empty()
            && entry.model != "unknown-model"
            && entry.model != "assistant"
            && entry.model != "unspecified_model"
            && model_lower != ag_key
        {
            self.last_changes_by_model
                .insert(model_lower.clone(), entry.clone());
            self.last_changes_by_model
                .insert(format!("{}:{}", ag_key, model_lower), entry.clone());
        }
    }

    pub fn ensure_default_history(&mut self) {
        if self.history_entries.is_empty() {
            let entry = AgentModelHistoryEntry {
                id: Uuid::new_v4().hyphenated().to_string(),
                timestamp: self.created_date,
                agent_name: "system".to_string(),
                model: "ensemble-core".to_string(),
                role: "orchestrator".to_string(),
                action: "init".to_string(),
                summary: "Agentic data repository initialized for project.".to_string(),
                chapter_index: -1,
                word_count_delta: 0,
                details: String::new(),
            };
            self.record_model_change(&entry);
            self.history_entries.push(entry);
        }
    }

    pub fn sanitize_path(path: &str) -> String {
        let clean = path.replace('\\', "/");
        let clean = clean.trim_start_matches('/');
        let mut resolved: Vec<&str> = Vec::new();
        for part in clean.split('/').filter(|p| !p.is_empty()) {
            if part == "." {
                continue;
            }
            if part == ".." {
                resolved.pop();
            } else {
                resolved.push(part);
            }
        }
        resolved.join("/")
    }

    pub fn sanitize_agent_name(agent: &str) -> String {
        let name: String = agent
            .trim()
            .to_lowercase()
            .chars()
            .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
            .collect();
        if name.is_empty() {
            "default_agent".to_string()
        } else {
            name
        }
    }

    pub fn total_shared_bytes(&self) -> usize {
        self.shared_files.values().map(|v| v.len()).sum()
    }

    pub fn total_localized_bytes(&self) -> usize {
        self.agent_files
            .values()
            .flat_map(|m| m.values())
            .map(|v| v.len())
            .sum()
    }

    pub fn agent_bytes(&self, agent_name: &str) -> usize {
        let agent = Self::sanitize_agent_name(agent_name);
        self.agent_files
            .get(&agent)
            .map(|m| m.values().map(|v| v.len()).sum())
            .unwrap_or(0)
    }

    pub fn set_shared_file(&mut self, relative_path: &str, data: Vec<u8>) -> Result<(), String> {
        let clean_path = Self::sanitize_path(relative_path);
        if clean_path.is_empty() {
            return Err("Invalid or empty relative path for shared data file".to_string());
        }
        let old_size = self.shared_files.get(&clean_path).map(|v| v.len()).unwrap_or(0);
        let new_total = self.total_shared_bytes() - old_size + data.len();
        if new_total > Self::MAX_SHARED_BYTES {
            return Err(format!(
                "Shared agent data quota exceeded: {} bytes requested (maximum allowed is 5 MB / {} bytes)",
                new_total,
                Self::MAX_SHARED_BYTES
            ));
        }
        self.shared_files.insert(clean_path, data);
        self.modified_date = Utc::now();
        Ok(())
    }

    pub fn shared_file(&self, relative_path: &str) -> Option<&[u8]> {
        self.shared_files
            .get(&Self::sanitize_path(relative_path))
            .map(|v| v.as_slice())
    }

    pub fn has_shared_file(&self, relative_path: &str) -> bool {
        self.shared_files.contains_key(&Self::sanitize_path(relative_path))
    }

    pub fn remove_shared_file(&mut self, relative_path: &str) -> bool {
        let clean = Self::sanitize_path(relative_path);
        if self.shared_files.remove(&clean).is_some() {
            self.modified_date = Utc::now();
            true
        } else {
            false
        }
    }

    pub fn shared_file_list(&self) -> Vec<String> {
        self.shared_files.keys().cloned().collect()
    }

    pub fn shared_files(&self) -> &BTreeMap<String, Vec<u8>> {
        &self.shared_files
    }

    pub fn set_agent_file(
        &mut self,
        agent_name: &str,
        relative_path: &str,
        data: Vec<u8>,
    ) -> Result<(), String> {
        let agent = Self::sanitize_agent_name(agent_name);
        let clean_path = Self::sanitize_path(relative_path);
        if clean_path.is_empty() {
            return Err("Invalid relative path for localized agent file".to_string());
        }
        let old_size = self
            .agent_files
            .get(&agent)
            .and_then(|m| m.get(&clean_path))
            .map(|v| v.len())
            .unwrap_or(0);
        let new_total = self.total_localized_bytes() - old_size + data.len();
        if new_total > Self::MAX_LOCALIZED_BYTES {
            return Err(format!(
                "Localized agent data quota exceeded: {} bytes requested (maximum allowed is 20 MB / {} bytes)",
                new_total,
                Self::MAX_LOCALIZED_BYTES
            ));
        }
        self.agent_files
            .entry(agent)
            .or_default()
            .insert(clean_path, data);
        self.modified_date = Utc::now();
        Ok(())
    }

    pub fn agent_file(&self, agent_name: &str, relative_path: &str) -> Option<&[u8]> {
        let agent = Self::sanitize_agent_name(agent_name);
        self.agent_files
            .get(&agent)
            .and_then(|m| m.get(&Self::sanitize_path(relative_path)))
            .map(|v| v.as_slice())
    }

    pub fn has_agent_file(&self, agent_name: &str, relative_path: &str) -> bool {
        let agent = Self::sanitize_agent_name(agent_name);
        self.agent_files
            .get(&agent)
            .map(|m| m.contains_key(&Self::sanitize_path(relative_path)))
            .unwrap_or(false)
    }

    pub fn remove_agent_file(&mut self, agent_name: &str, relative_path: &str) -> bool {
        let agent = Self::sanitize_agent_name(agent_name);
        let clean = Self::sanitize_path(relative_path);
        if let Some(files) = self.agent_files.get_mut(&agent) {
            if files.remove(&clean).is_some() {
                if files.is_empty() {
                    self.agent_files.remove(&agent);
                }
                self.modified_date = Utc::now();
                return true;
            }
        }
        false
    }

    pub fn agent_names(&self) -> Vec<String> {
        self.agent_files.keys().cloned().collect()
    }

    pub fn agent_file_list(&self, agent_name: &str) -> Vec<String> {
        let agent = Self::sanitize_agent_name(agent_name);
        self.agent_files
            .get(&agent)
            .map(|m| m.keys().cloned().collect())
            .unwrap_or_default()
    }

    #[allow(clippy::too_many_arguments)]
    pub fn append_history_entry(
        &mut self,
        agent_name: &str,
        model: &str,
        role: &str,
        action: &str,
        summary: &str,
        chapter_index: i32,
        word_count_delta: i32,
        details: &str,
    ) {
        let now = Utc::now();
        let entry = AgentModelHistoryEntry {
            id: Uuid::new_v4().hyphenated().to_string(),
            timestamp: now,
            agent_name: if agent_name.is_empty() {
                "anonymous".to_string()
            } else {
                agent_name.to_string()
            },
            model: if model.is_empty() {
                "unknown-model".to_string()
            } else {
                model.to_string()
            },
            role: if role.is_empty() {
                "contributor".to_string()
            } else {
                role.to_string()
            },
            action: if action.is_empty() {
                "update".to_string()
            } else {
                action.to_string()
            },
            summary: summary.to_string(),
            chapter_index,
            word_count_delta,
            details: details.to_string(),
        };
        self.record_model_change(&entry);
        self.history_entries.push(entry);
        self.modified_date = now;
    }

    pub fn history_entries(&self) -> &[AgentModelHistoryEntry] {
        &self.history_entries
    }

    pub fn last_changes_by_model_json(&self) -> Value {
        let mut map = serde_json::Map::new();
        for (k, v) in &self.last_changes_by_model {
            map.insert(k.clone(), v.to_json());
        }
        Value::Object(map)
    }

    pub fn raw_model_history_xml(&self) -> String {
        let mut writer = Writer::new_with_indent(Cursor::new(Vec::new()), b' ', 4);
        let _ = writer.write_event(Event::Decl(BytesDecl::new("1.0", Some("UTF-8"), None)));

        let mut root = BytesStart::new("agent-model-history");
        root.push_attribute(("version", "1.0"));
        root.push_attribute(("projectIdentifier", self.project_identifier.as_str()));
        let _ = writer.write_event(Event::Start(root));

        for entry in &self.history_entries {
            let mut el = BytesStart::new("entry");
            el.push_attribute(("id", entry.id.as_str()));
            let ts = entry.timestamp.to_rfc3339();
            el.push_attribute(("timestamp", ts.as_str()));
            el.push_attribute(("agent", entry.agent_name.as_str()));
            el.push_attribute(("model", entry.model.as_str()));
            el.push_attribute(("role", entry.role.as_str()));
            el.push_attribute(("action", entry.action.as_str()));
            let ch_str = entry.chapter_index.to_string();
            if entry.chapter_index >= 0 {
                el.push_attribute(("chapterIndex", ch_str.as_str()));
            }
            let wc_str = entry.word_count_delta.to_string();
            if entry.word_count_delta != 0 {
                el.push_attribute(("wordCountDelta", wc_str.as_str()));
            }
            let _ = writer.write_event(Event::Start(el));

            let _ = writer.write_event(Event::Start(BytesStart::new("summary")));
            let _ = writer.write_event(Event::Text(BytesText::new(&entry.summary)));
            let _ = writer.write_event(Event::End(BytesEnd::new("summary")));

            if !entry.details.is_empty() {
                let _ = writer.write_event(Event::Start(BytesStart::new("details")));
                let _ = writer.write_event(Event::Text(BytesText::new(&entry.details)));
                let _ = writer.write_event(Event::End(BytesEnd::new("details")));
            }

            let _ = writer.write_event(Event::End(BytesEnd::new("entry")));
        }

        let _ = writer.write_event(Event::End(BytesEnd::new("agent-model-history")));
        String::from_utf8(writer.into_inner().into_inner()).unwrap_or_default()
    }

    pub fn set_raw_model_history_xml(&mut self, xml: &str) {
        self.history_entries.clear();
        self.last_changes_by_model.clear();

        let mut reader = Reader::from_str(xml);
        reader.config_mut().trim_text(true);
        let mut buf = Vec::new();
        let mut current_entry: Option<AgentModelHistoryEntry> = None;
        let mut current_tag = String::new();

        loop {
            match reader.read_event_into(&mut buf) {
                Ok(Event::Start(e)) => {
                    let name = String::from_utf8_lossy(e.name().as_ref()).to_string();
                    if name == "entry" {
                        let mut entry = AgentModelHistoryEntry {
                            id: String::new(),
                            timestamp: Utc::now(),
                            agent_name: String::new(),
                            model: String::new(),
                            role: String::new(),
                            action: String::new(),
                            summary: String::new(),
                            chapter_index: -1,
                            word_count_delta: 0,
                            details: String::new(),
                        };
                        for attr in e.attributes().flatten() {
                            let k = String::from_utf8_lossy(attr.key.as_ref());
                            let v = attr.unescape_value().unwrap_or_default().to_string();
                            match k.as_ref() {
                                "id" => entry.id = v,
                                "timestamp" => {
                                    if let Ok(dt) = DateTime::parse_from_rfc3339(&v) {
                                        entry.timestamp = dt.with_timezone(&Utc);
                                    }
                                }
                                "agent" => entry.agent_name = v,
                                "model" => entry.model = v,
                                "role" => entry.role = v,
                                "action" => entry.action = v,
                                "chapterIndex" => entry.chapter_index = v.parse().unwrap_or(-1),
                                "wordCountDelta" => entry.word_count_delta = v.parse().unwrap_or(0),
                                _ => {}
                            }
                        }
                        current_entry = Some(entry);
                    } else {
                        current_tag = name;
                    }
                }
                Ok(Event::Text(t)) => {
                    if let Some(ref mut entry) = current_entry {
                        let text = t.unescape().unwrap_or_default().to_string();
                        if current_tag == "summary" {
                            entry.summary = text;
                        } else if current_tag == "details" {
                            entry.details = text;
                        }
                    }
                }
                Ok(Event::End(e)) => {
                    let name = String::from_utf8_lossy(e.name().as_ref()).to_string();
                    if name == "entry" {
                        if let Some(entry) = current_entry.take() {
                            self.record_model_change(&entry);
                            self.history_entries.push(entry);
                        }
                    }
                    current_tag.clear();
                }
                Ok(Event::Eof) | Err(_) => break,
                _ => {}
            }
            buf.clear();
        }

        self.ensure_default_history();
    }

    pub fn agentifiers_json(&self) -> &Value {
        &self.agentifiers
    }

    pub fn set_agentifiers_json(&mut self, json: Value) {
        self.agentifiers = json;
        self.modified_date = Utc::now();
    }

    pub fn register_agent_identifier(
        &mut self,
        agent_name: &str,
        model_family: &str,
        role: &str,
        notes: &str,
    ) {
        let agent = Self::sanitize_agent_name(agent_name);
        let now = Utc::now().to_rfc3339();
        let mut agent_obj = json!({
            "agentId": Uuid::new_v4().hyphenated().to_string(),
            "registeredAt": now,
            "modelFamily": if model_family.is_empty() { "generic" } else { model_family },
            "role": if role.is_empty() { "co-author" } else { role },
        });
        if !notes.is_empty() {
            if let Some(m) = agent_obj.as_object_mut() {
                m.insert("notes".to_string(), json!(notes));
            }
        }
        if let Some(root) = self.agentifiers.as_object_mut() {
            let agents = root
                .entry("agents")
                .or_insert_with(|| json!({}))
                .as_object_mut();
            if let Some(ag_map) = agents {
                ag_map.insert(agent, agent_obj);
            }
            root.insert("modified".to_string(), json!(now));
        }
    }

    pub fn export_encrypted_agentifiers(&self, passphrase: &[u8]) -> Vec<u8> {
        let clear = serde_json::to_vec(&self.agentifiers).unwrap_or_default();
        AesCipher::encrypt_envelope(&clear, passphrase).unwrap_or_default()
    }

    pub fn import_encrypted_agentifiers(
        &mut self,
        encrypted_envelope: &[u8],
        passphrase: &[u8],
    ) -> Result<(), String> {
        let clear = AesCipher::decrypt_envelope(encrypted_envelope, passphrase)?;
        let val: Value = serde_json::from_slice(&clear)
            .map_err(|e| format!("Decrypted agentifiers content is not valid JSON: {}", e))?;
        if !val.is_object() {
            return Err("Decrypted agentifiers root is not a JSON object".to_string());
        }
        self.agentifiers = val;
        Ok(())
    }

    pub fn generate_index_json(&self) -> Value {
        let shared_arr: Vec<Value> = self
            .shared_files
            .iter()
            .map(|(k, v)| json!({ "path": k, "size": v.len() }))
            .collect();

        let mut agents_obj = serde_json::Map::new();
        for (agent, files) in &self.agent_files {
            let files_arr: Vec<Value> = files
                .iter()
                .map(|(k, v)| json!({ "path": k, "size": v.len() }))
                .collect();
            agents_obj.insert(
                agent.clone(),
                json!({
                    "bytes": self.agent_bytes(agent),
                    "files": files_arr
                }),
            );
        }

        let shared_bytes = self.total_shared_bytes();
        let localized_bytes = self.total_localized_bytes();

        json!({
            "version": "1.0",
            "projectIdentifier": self.project_identifier,
            "lastUpdated": self.modified_date.to_rfc3339(),
            "limits": {
                "maxSharedBytes": Self::MAX_SHARED_BYTES,
                "maxLocalizedBytes": Self::MAX_LOCALIZED_BYTES
            },
            "usage": {
                "sharedBytes": shared_bytes,
                "localizedBytes": localized_bytes,
                "totalBytes": shared_bytes + localized_bytes
            },
            "sharedFiles": shared_arr,
            "agents": agents_obj
        })
    }

    pub fn serialize_to_archive(&self, archive_entries: &mut BTreeMap<String, Vec<u8>>) {
        let index_bytes =
            serde_json::to_vec_pretty(&self.generate_index_json()).unwrap_or_default();
        archive_entries.insert("agentic-datas/index.json".to_string(), index_bytes);
        archive_entries.insert(
            "agentic-datas/agent_model_history.xml".to_string(),
            self.raw_model_history_xml().into_bytes(),
        );
        archive_entries.insert(
            "agentic-datas/agentifiers.json".to_string(),
            self.export_encrypted_agentifiers(&[]),
        );

        for (k, v) in &self.shared_files {
            archive_entries.insert(format!("agentic-datas/shared/{}", k), v.clone());
        }
        for (agent, files) in &self.agent_files {
            for (k, v) in files {
                archive_entries.insert(format!("agentic-datas/{}/{}", agent, k), v.clone());
            }
        }
    }

    pub fn deserialize_from_archive(
        &mut self,
        archive_entries: &BTreeMap<String, Vec<u8>>,
    ) -> Result<(), String> {
        self.clear();

        let mut incoming_shared_bytes = 0usize;
        let mut incoming_localized_bytes = 0usize;

        for (key, val) in archive_entries {
            if key.starts_with("agentic-datas/shared/") {
                incoming_shared_bytes += val.len();
            } else if key.starts_with("agentic-datas/")
                && key != "agentic-datas/index.json"
                && key != "agentic-datas/agent_model_history.xml"
                && key != "agentic-datas/agentifiers.json"
            {
                incoming_localized_bytes += val.len();
            }
        }

        if incoming_shared_bytes > Self::MAX_SHARED_BYTES {
            return Err(format!(
                "Archive shared agent data quota exceeded: {} bytes (max allowed is 5 MB / {} bytes)",
                incoming_shared_bytes,
                Self::MAX_SHARED_BYTES
            ));
        }

        if incoming_localized_bytes > Self::MAX_LOCALIZED_BYTES {
            return Err(format!(
                "Archive localized agent data quota exceeded: {} bytes (max allowed is 20 MB / {} bytes)",
                incoming_localized_bytes,
                Self::MAX_LOCALIZED_BYTES
            ));
        }

        if let Some(index_bytes) = archive_entries.get("agentic-datas/index.json") {
            if let Ok(val) = serde_json::from_slice::<Value>(index_bytes) {
                if let Some(pid) = val.get("projectIdentifier").and_then(|v| v.as_str()) {
                    self.project_identifier = pid.to_string();
                }
            }
        }

        if let Some(hist_bytes) = archive_entries.get("agentic-datas/agent_model_history.xml") {
            let xml = String::from_utf8_lossy(hist_bytes);
            self.set_raw_model_history_xml(&xml);
        }

        if let Some(ag_bytes) = archive_entries.get("agentic-datas/agentifiers.json") {
            let _ = self.import_encrypted_agentifiers(ag_bytes, &[]);
        }

        let shared_prefix = "agentic-datas/shared/";
        let agentic_prefix = "agentic-datas/";
        for (key, val) in archive_entries {
            if let Some(rel) = key.strip_prefix(shared_prefix) {
                let clean = Self::sanitize_path(rel);
                if !clean.is_empty() {
                    self.shared_files.insert(clean, val.clone());
                }
            } else if let Some(sub) = key.strip_prefix(agentic_prefix) {
                if key != "agentic-datas/index.json"
                    && key != "agentic-datas/agent_model_history.xml"
                    && key != "agentic-datas/agentifiers.json"
                {
                    if let Some((agent_raw, rel_raw)) = sub.split_once('/') {
                        let agent = Self::sanitize_agent_name(agent_raw);
                        let rel = Self::sanitize_path(rel_raw);
                        if !rel.is_empty() {
                            self.agent_files
                                .entry(agent)
                                .or_default()
                                .insert(rel, val.clone());
                        }
                    }
                }
            }
        }

        Ok(())
    }
}
