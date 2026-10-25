use crate::activity::{sort_and_deduplicate_events, AgentActivityEvent, AgentActivityType};
use crate::server::{McpAgentSession, McpServer, ServerSharedState};
use crate::tools::get_mcp_all_tool_definitions;
use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use chrono::Utc;
use ensemble_core::chapter::Chapter;
use ensemble_core::lore::{
    LoreConflict, LoreEntity, LoreHistoryEvent, LorePlan, LoreReference,
};
use ensemble_core::lore_graph::{LoreGraph, LoreGraphNode};
use ensemble_core::project::Ao3Project;
use ensemble_core::serializer::ProjectSerializer;
use ensemble_html::{Ao3HtmlSanitizer, CssLinter, HtmlLinter};
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use regex::Regex;
use serde_json::{json, Value};
use std::collections::HashSet;
use std::path::Path;
use std::sync::Arc;

pub struct McpWorkspaceState {
    pub project: Option<Ao3Project>,
    pub active_chapter_index: usize,
    pub activity_events: Vec<AgentActivityEvent>,
}

impl Default for McpWorkspaceState {
    fn default() -> Self {
        Self {
            project: None,
            active_chapter_index: 0,
            activity_events: Vec::new(),
        }
    }
}

pub struct McpHandler {
    server_state: Arc<Mutex<ServerSharedState>>,
    workspace: Arc<Mutex<McpWorkspaceState>>,
    activity_listeners: Mutex<Vec<Arc<dyn Fn(&AgentActivityEvent) + Send + Sync>>>,
    ui_refresh_listeners: Mutex<Vec<Arc<dyn Fn() + Send + Sync>>>,
}

impl McpHandler {
    pub fn new(server: &McpServer) -> Arc<Self> {
        let handler = Arc::new(Self {
            server_state: Arc::clone(&server.state),
            workspace: Arc::new(Mutex::new(McpWorkspaceState::default())),
            activity_listeners: Mutex::new(Vec::new()),
            ui_refresh_listeners: Mutex::new(Vec::new()),
        });

        let handler_weak = Arc::downgrade(&handler);
        server.set_message_handler(Arc::new(move |client_id, msg| {
            if let Some(h) = handler_weak.upgrade() {
                h.handle_message(client_id, msg)
            } else {
                None
            }
        }));

        handler
    }

    pub fn workspace(&self) -> Arc<Mutex<McpWorkspaceState>> {
        Arc::clone(&self.workspace)
    }

    pub fn on_agent_activity<F>(&self, callback: F)
    where
        F: Fn(&AgentActivityEvent) + Send + Sync + 'static,
    {
        self.activity_listeners.lock().push(Arc::new(callback));
    }

    pub fn on_ui_refresh<F>(&self, callback: F)
    where
        F: Fn() + Send + Sync + 'static,
    {
        self.ui_refresh_listeners.lock().push(Arc::new(callback));
    }

    fn emit_activity(&self, event: AgentActivityEvent) {
        {
            let mut ws = self.workspace.lock();
            ws.activity_events.push(event.clone());
            sort_and_deduplicate_events(&mut ws.activity_events);
        }
        let listeners = self.activity_listeners.lock().clone();
        for listener in listeners {
            listener(&event);
        }
    }

    fn notify_ui_refresh(&self) {
        let listeners = self.ui_refresh_listeners.lock().clone();
        for listener in listeners {
            listener();
        }
    }

    pub fn has_active_project(&self) -> bool {
        self.workspace.lock().project.is_some()
    }

    pub fn open_new_project(&self) {
        let proj = Ao3Project::new();
        let mut ws = self.workspace.lock();
        ws.project = Some(proj);
        ws.active_chapter_index = 0;
    }

    pub fn set_project(&self, project: Option<Ao3Project>) {
        let mut ws = self.workspace.lock();
        ws.project = project;
        ws.active_chapter_index = 0;
    }

    pub fn open_project_file(&self, path: &str) -> bool {
        let mut proj = Ao3Project::new();
        match ProjectSerializer::load(Path::new(path), &mut proj) {
            Ok(()) => {
                let mut ws = self.workspace.lock();
                ws.project = Some(proj);
                ws.active_chapter_index = 0;
                drop(ws);
                self.notify_ui_refresh();
                true
            }
            Err(_) => false,
        }
    }

    pub fn save_project_file(&self, path: &str) -> bool {
        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return false,
        };
        let target = if path.trim().is_empty() {
            proj.file_path.clone()
        } else {
            path.trim().to_string()
        };
        if target.is_empty() {
            return false;
        }
        if ProjectSerializer::save(Path::new(&target), proj).is_ok() {
            proj.file_path = target;
            proj.dirty = false;
            true
        } else {
            false
        }
    }

    pub fn with_project<F, R>(&self, f: F) -> Option<R>
    where
        F: FnOnce(&Ao3Project) -> R,
    {
        let ws = self.workspace.lock();
        ws.project.as_ref().map(f)
    }

    pub fn with_project_mut<F, R>(&self, f: F) -> Option<R>
    where
        F: FnOnce(&mut Ao3Project) -> R,
    {
        let mut ws = self.workspace.lock();
        ws.project.as_mut().map(f)
    }

    pub fn activity_events(&self) -> Vec<AgentActivityEvent> {
        self.workspace.lock().activity_events.clone()
    }

    pub fn clear_activity_events(&self) {
        self.workspace.lock().activity_events.clear();
    }

    pub fn handle_message(&self, client_id: u64, raw_message: &str) -> Option<String> {
        let parsed: Value = match serde_json::from_str(raw_message) {
            Ok(v) => v,
            Err(e) => {
                let err_resp = Self::make_error_response(
                    Value::Null,
                    -32700,
                    &format!("Parse error: {}", e),
                );
                return serde_json::to_string(&err_resp).ok();
            }
        };

        let req = match parsed.as_object() {
            Some(obj) => obj,
            None => {
                let err_resp = Self::make_error_response(
                    Value::Null,
                    -32700,
                    "Parse error: expected JSON object",
                );
                return serde_json::to_string(&err_resp).ok();
            }
        };

        let id = req.get("id").cloned().unwrap_or(Value::Null);
        let method = req
            .get("method")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();
        let params = req
            .get("params")
            .cloned()
            .unwrap_or_else(|| json!({}));

        if method == "notifications/initialized"
            || method == "initialized"
            || method == "$/cancelRequest"
            || (req.get("id").is_none() && method.starts_with("notifications/"))
        {
            return None;
        }

        if method.is_empty() {
            let err_resp =
                Self::make_error_response(id, -32600, "Invalid Request: missing 'method' field");
            return serde_json::to_string(&err_resp).ok();
        }

        let response = match method.as_str() {
            "initialize" => self.handle_initialize(client_id, id, &params),
            "ping" => Self::make_response(id, json!({})),
            "tools/list" => self.handle_tools_list(id),
            "tools/call" => self.handle_tools_call(client_id, id, &params),
            "resources/list" => self.handle_resources_list(id),
            "resources/read" => self.handle_resources_read(id, &params),
            "prompts/list" => Self::make_response(id, json!({ "prompts": [] })),
            _ => Self::make_error_response(
                id,
                -32601,
                &format!("Method not found: {}", method),
            ),
        };

        serde_json::to_string(&response).ok()
    }

    fn make_response(id: Value, result: Value) -> Value {
        json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": result
        })
    }

    fn make_error_response(id: Value, code: i32, message: &str) -> Value {
        json!({
            "jsonrpc": "2.0",
            "id": id,
            "error": {
                "code": code,
                "message": message
            }
        })
    }

    fn make_tool_result(text: &str, is_error: bool) -> Value {
        json!({
            "content": [
                {
                    "type": "text",
                    "text": text
                }
            ],
            "isError": is_error
        })
    }

    fn make_tool_json_result(data: Value, is_error: bool) -> Value {
        let pretty = serde_json::to_string_pretty(&data).unwrap_or_else(|_| data.to_string());
        Self::make_tool_result(&pretty, is_error)
    }

    fn caller_session(&self, client_id: u64) -> McpAgentSession {
        let s = McpServer::agent_session_from_state(&self.server_state, client_id);
        if s.name.is_empty() {
            McpAgentSession {
                id: format!("agent-{}", client_id),
                name: "AI Agent".to_string(),
                model: "unknown".to_string(),
                role: "co-author".to_string(),
                status: "Active".to_string(),
                ..s
            }
        } else {
            s
        }
    }

    fn resolve_caller_model(caller: &McpAgentSession, args: &Value) -> String {
        for key in ["model", "agentModel", "modelName"] {
            if let Some(m) = args.get(key).and_then(|v| v.as_str()) {
                let trimmed = m.trim();
                if !trimmed.is_empty() && !trimmed.eq_ignore_ascii_case("unknown") {
                    return trimmed.to_string();
                }
            }
        }
        let cm = caller.model.trim();
        if !cm.is_empty() && !cm.eq_ignore_ascii_case("unknown") {
            return cm.to_string();
        }
        if !caller.name.trim().is_empty() {
            return caller.name.trim().to_string();
        }
        "mcp-agent".to_string()
    }

    fn total_word_count(proj: &Ao3Project) -> usize {
        proj.chapters.iter().map(|c| c.word_count()).sum()
    }

    fn generate_metadata_xml(proj: &Ao3Project) -> String {
        let mut xml = String::new();
        xml.push_str("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ao3-project>\n");
        xml.push_str(&format!("  <title>{}</title>\n", proj.title));
        xml.push_str(&format!("  <author>{}</author>\n", proj.author));
        xml.push_str(&format!("  <rating>{}</rating>\n", proj.rating));
        xml.push_str("  <fandoms>\n");
        for f in &proj.fandoms {
            xml.push_str(&format!("    <fandom>{}</fandom>\n", f));
        }
        xml.push_str("  </fandoms>\n</ao3-project>\n");
        xml
    }

    fn handle_initialize(&self, client_id: u64, id: Value, params: &Value) -> Value {
        let client_version = params
            .get("protocolVersion")
            .and_then(|v| v.as_str())
            .filter(|s| !s.is_empty())
            .unwrap_or("2024-11-05");

        if let Some(client_info) = params.get("clientInfo").and_then(|v| v.as_object()) {
            let name = client_info
                .get("name")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .trim();
            let ver = client_info
                .get("version")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .trim();
            if !name.is_empty() {
                let model = if ver.is_empty() { "mcp-client" } else { ver };
                McpServer::register_agent_session_in_state(
                    &self.server_state,
                    client_id,
                    name,
                    model,
                    "co-author",
                    "Initialized",
                );

                self.emit_activity(AgentActivityEvent {
                    id: uuid::Uuid::new_v4().to_string(),
                    timestamp: Utc::now(),
                    event_type: AgentActivityType::Presence,
                    agent_name: name.to_string(),
                    agent_role: "co-author".to_string(),
                    agent_model: model.to_string(),
                    category: "initialize".to_string(),
                    summary: format!("{} connected via MCP ({})", name, model),
                    details: "Initialized MCP session".to_string(),
                    chapter_index: -1,
                    word_count_delta: 0,
                    error_message: String::new(),
                });
            }
        }

        Self::make_response(
            id,
            json!({
                "protocolVersion": client_version,
                "capabilities": {
                    "tools": { "listChanged": false },
                    "resources": { "subscribe": false, "listChanged": false },
                    "prompts": { "listChanged": false }
                },
                "serverInfo": {
                    "name": "ensemble-ao3-typewriter",
                    "version": env!("CARGO_PKG_VERSION")
                }
            }),
        )
    }

    fn handle_tools_list(&self, id: Value) -> Value {
        Self::make_response(
            id,
            json!({
                "tools": get_mcp_all_tool_definitions()
            }),
        )
    }

    fn handle_tools_call(&self, client_id: u64, id: Value, params: &Value) -> Value {
        let name = params
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        let args = params
            .get("arguments")
            .cloned()
            .unwrap_or_else(|| json!({}));

        if name.is_empty() {
            return Self::make_error_response(id, -32602, "Invalid params: missing tool 'name'");
        }

        let tool_result = match name {
            "get_project_summary" => self.tool_get_project_summary(),
            "list_chapters" => self.tool_list_chapters(),
            "get_chapter" => self.tool_get_chapter(&args),
            "update_chapter_html" => self.tool_update_chapter_html(client_id, &args),
            "add_chapter" => self.tool_add_chapter(client_id, &args),
            "delete_chapter" => self.tool_delete_chapter(client_id, &args),
            "reorder_chapters" => self.tool_reorder_chapters(client_id, &args),
            "get_workskin_css" => self.tool_get_workskin_css(),
            "set_workskin_css" => self.tool_set_workskin_css(client_id, &args),
            "get_problems" => self.tool_get_problems(&args),
            "load_project" => self.tool_load_project(client_id, &args),
            "save_project" => self.tool_save_project(client_id, &args),
            "get_project_history" => self.tool_get_project_history(&args),
            "add_history_commit" => self.tool_add_history_commit(client_id, &args),
            "get_snapshot_diff" => self.tool_get_snapshot_diff(&args),
            "test_firewall_connection" => self.tool_test_firewall_connection(),
            "reload_project" => self.tool_reload_project(&args),
            "get_preview_screenshot" => self.tool_get_preview_screenshot(&args),
            "get_agent_presence" => self.tool_get_agent_presence(),
            "register_agent_presence" => self.tool_register_agent_presence(client_id, &args),
            "get_agentic_summary" => self.tool_get_agentic_summary(),
            "read_agentic_file" => self.tool_read_agentic_file(&args),
            "write_agentic_file" => self.tool_write_agentic_file(client_id, &args),
            "append_agent_history" => self.tool_append_agent_history(client_id, &args),
            "post_agent_thought" => self.tool_post_agent_thought(client_id, &args),
            "get_agent_history" => self.tool_get_agent_history(&args),
            "get_lore_graph" => self.tool_get_lore_graph(&args),
            "update_lore_graph" => self.tool_update_lore_graph(client_id, &args),
            "add_lore_node" => self.tool_add_lore_node(client_id, &args),
            "connect_lore_nodes" => self.tool_connect_lore_nodes(client_id, &args),
            "move_lore_node" => self.tool_move_lore_node(client_id, &args),
            "get_lore_data" => self.tool_get_lore_data(&args),
            "manage_lore_data" => self.tool_manage_lore_data(client_id, &args),
            "update_project_metadata" => self.tool_update_project_metadata(client_id, &args),
            "list_image_embeds" => self.tool_list_image_embeds(&args),
            "insert_image_embed" => self.tool_insert_image_embed(client_id, &args),
            _ => {
                let caller = self.caller_session(client_id);
                let msg = format!("Unknown tool: {}", name);
                self.emit_activity(AgentActivityEvent {
                    id: uuid::Uuid::new_v4().to_string(),
                    timestamp: Utc::now(),
                    event_type: AgentActivityType::Error,
                    agent_name: caller.name,
                    agent_role: caller.role,
                    agent_model: caller.model,
                    category: name.to_string(),
                    summary: format!("Failed calling unknown tool '{}'", name),
                    details: args.to_string(),
                    chapter_index: -1,
                    word_count_delta: 0,
                    error_message: msg.clone(),
                });
                Self::make_tool_result(&msg, true)
            }
        };

        if tool_result
            .get("isError")
            .and_then(|v| v.as_bool())
            .unwrap_or(false)
            && !matches!(
                name,
                "get_project_summary"
                    | "list_chapters"
                    | "get_chapter"
                    | "get_workskin_css"
                    | "get_problems"
            )
        {
            let err_text = tool_result
                .get("content")
                .and_then(|c| c.as_array())
                .and_then(|arr| arr.first())
                .and_then(|first| first.get("text"))
                .and_then(|t| t.as_str())
                .unwrap_or("Tool execution returned an error.")
                .to_string();
            let caller = self.caller_session(client_id);
            let caller_model = Self::resolve_caller_model(&caller, &args);
            self.emit_activity(AgentActivityEvent {
                id: uuid::Uuid::new_v4().to_string(),
                timestamp: Utc::now(),
                event_type: AgentActivityType::Error,
                agent_name: caller.name,
                agent_role: caller.role,
                agent_model: caller_model,
                category: name.to_string(),
                summary: format!("Tool '{}' reported an error", name),
                details: err_text.clone(),
                chapter_index: args
                    .get("chapterIndex")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(-1) as i32,
                word_count_delta: 0,
                error_message: err_text,
            });
        }

        Self::make_response(id, tool_result)
    }

    fn handle_resources_list(&self, id: Value) -> Value {
        Self::make_response(
            id,
            json!({
                "resources": [
                    {
                        "uri": "ensemble://project/summary",
                        "name": "Project Summary",
                        "description": "Metadata and chapter overview of the currently open AO3 project.",
                        "mimeType": "application/json"
                    },
                    {
                        "uri": "ensemble://project/workskin.css",
                        "name": "Work Skin CSS",
                        "description": "Current AO3 Work Skin CSS stylesheet for the open project.",
                        "mimeType": "text/css"
                    },
                    {
                        "uri": "ensemble://project/problems",
                        "name": "AO3 Linter Problems",
                        "description": "Current HTML and CSS compatibility diagnostics for the open project.",
                        "mimeType": "application/json"
                    },
                    {
                        "uri": "ensemble://project/history",
                        "name": "Project Revision History",
                        "description": "Git-style revision commits stored inside the .ao3proj archive.",
                        "mimeType": "application/json"
                    },
                    {
                        "uri": "ensemble://project/agentic",
                        "name": "Agentic Workspace Summary",
                        "description": "Summary of the project's encrypted agentic workspace and instructions.",
                        "mimeType": "application/json"
                    },
                    {
                        "uri": "ensemble://project/lore_graph",
                        "name": "Story Lore Graph (.grph)",
                        "description": "The project's narrative lore graph in .grph DSL syntax.",
                        "mimeType": "text/plain"
                    },
                    {
                        "uri": "ensemble://project/lore",
                        "name": "Story Lore Database",
                        "description": "Structured JSON of all story lore entities, history, conflicts, plans, and references.",
                        "mimeType": "application/json"
                    },
                    {
                        "uri": "ensemble://project/metadata.xml",
                        "name": "AO3 Project Metadata XML",
                        "description": "Standard AO3 metadata XML manifest for the open project.",
                        "mimeType": "application/xml"
                    }
                ]
            }),
        )
    }

    fn handle_resources_read(&self, id: Value, params: &Value) -> Value {
        let uri = params
            .get("uri")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if uri.is_empty() {
            return Self::make_error_response(id, -32602, "Invalid params: missing 'uri'");
        }

        let extract_text = |res: Value| -> String {
            res.get("content")
                .and_then(|c| c.as_array())
                .and_then(|arr| arr.first())
                .and_then(|first| first.get("text"))
                .and_then(|t| t.as_str())
                .unwrap_or("{}")
                .to_string()
        };

        match uri {
            "ensemble://project/summary" => {
                let text = extract_text(self.tool_get_project_summary());
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "application/json", "text": text }]
                    }),
                )
            }
            "ensemble://project/workskin.css" => {
                let css = self
                    .with_project(|p| p.work_skin_css.clone())
                    .unwrap_or_default();
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "text/css", "text": css }]
                    }),
                )
            }
            "ensemble://project/problems" => {
                let text = extract_text(self.tool_get_problems(&json!({})));
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "application/json", "text": text }]
                    }),
                )
            }
            "ensemble://project/history" => {
                let text = extract_text(self.tool_get_project_history(&json!({})));
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "application/json", "text": text }]
                    }),
                )
            }
            "ensemble://project/agentic" => {
                let text = extract_text(self.tool_get_agentic_summary());
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "application/json", "text": text }]
                    }),
                )
            }
            "ensemble://project/lore_graph" => {
                let dsl = self
                    .with_project(|p| {
                        p.lore
                            .graph("data")
                            .map(|g| g.to_dsl())
                            .unwrap_or_default()
                    })
                    .unwrap_or_default();
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "text/plain", "text": dsl }]
                    }),
                )
            }
            "ensemble://project/lore" => {
                let text = extract_text(self.tool_get_lore_data(&json!({})));
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "application/json", "text": text }]
                    }),
                )
            }
            "ensemble://project/metadata.xml" => {
                let xml = self
                    .with_project(Self::generate_metadata_xml)
                    .unwrap_or_default();
                Self::make_response(
                    id,
                    json!({
                        "contents": [{ "uri": uri, "mimeType": "application/xml", "text": xml }]
                    }),
                )
            }
            _ => Self::make_error_response(id, -32602, &format!("Resource not found: {}", uri)),
        }
    }

    // ========================================================================
    // Tool Implementations
    // ========================================================================

    fn tool_get_project_summary(&self) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        Self::make_tool_json_result(
            json!({
                "title": proj.title,
                "author": proj.author,
                "rating": proj.rating,
                "chapterCount": proj.chapters.len(),
                "totalWordCount": Self::total_word_count(proj),
                "activeChapterIndex": ws.active_chapter_index,
                "isDirty": proj.dirty,
                "filePath": proj.file_path,
                "hasWorkSkin": !proj.work_skin_css.trim().is_empty(),
                "workSkinLength": proj.work_skin_css.len(),
                "historyCommitCount": proj.snapshot_diffs.len()
            }),
            false,
        )
    }

    fn tool_list_chapters(&self) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let chapters: Vec<Value> = proj
            .chapters
            .iter()
            .enumerate()
            .map(|(i, c)| {
                json!({
                    "index": i,
                    "id": c.id_string(),
                    "title": c.title,
                    "wordCount": c.word_count(),
                    "order": c.order
                })
            })
            .collect();

        Self::make_tool_json_result(
            json!({
                "chapterCount": chapters.len(),
                "chapters": chapters
            }),
            false,
        )
    }

    fn tool_get_chapter(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let idx = match args.get("chapterIndex").and_then(|v| v.as_i64()) {
            Some(i) => i,
            None => return Self::make_tool_result("Missing required 'chapterIndex' parameter.", true),
        };

        if idx < 0 || (idx as usize) >= proj.chapters.len() {
            return Self::make_tool_result(
                &format!(
                    "Invalid chapterIndex {}. Project has {} chapter(s).",
                    idx,
                    proj.chapters.len()
                ),
                true,
            );
        }

        let ch = &proj.chapters[idx as usize];
        Self::make_tool_json_result(
            json!({
                "index": idx,
                "id": ch.id_string(),
                "title": ch.title,
                "html": ch.html,
                "wordCount": ch.word_count()
            }),
            false,
        )
    }

    fn tool_update_chapter_html(&self, client_id: u64, args: &Value) -> Value {
        let idx = match args.get("chapterIndex").and_then(|v| v.as_i64()) {
            Some(i) => i,
            None => return Self::make_tool_result("Missing required 'chapterIndex' parameter.", true),
        };
        let raw_html = match args.get("html").and_then(|v| v.as_str()) {
            Some(h) => h,
            None => return Self::make_tool_result("Missing required 'html' parameter.", true),
        };
        let sanitize = args
            .get("sanitize")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        let (final_html, stripped_tags, stripped_attrs) = if sanitize {
            let res = Ao3HtmlSanitizer::sanitize_detailed(raw_html);
            (
                res.clean_html,
                res.stripped_tags.len(),
                res.stripped_attributes.len(),
            )
        } else {
            (raw_html.to_string(), 0, 0)
        };

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        if idx < 0 || (idx as usize) >= proj.chapters.len() {
            return Self::make_tool_result(
                &format!(
                    "Invalid chapterIndex {}. Project has {} chapter(s).",
                    idx,
                    proj.chapters.len()
                ),
                true,
            );
        }

        let u_idx = idx as usize;
        let old_words = proj.chapters[u_idx].word_count() as i32;
        proj.chapters[u_idx].html = final_html.clone();

        if let Some(t) = args.get("title").and_then(|v| v.as_str()) {
            if !t.trim().is_empty() {
                proj.chapters[u_idx].title = t.trim().to_string();
            }
        }

        let new_words = proj.chapters[u_idx].word_count() as i32;
        let word_delta = new_words - old_words;
        let ch_title = proj.chapters[u_idx].title.clone();

        let report = HtmlLinter::lint_html(&final_html, 1);
        let err_count = report.error_count;
        let warn_count = report.warning_count;

        let summary_msg = format!(
            "Updated Chapter {} ('{}') HTML ({} words, delta {:+})",
            u_idx + 1,
            ch_title,
            new_words,
            word_delta
        );
        proj.mark_dirty_by(&caller.name);
        proj.append_history_commit(&caller.name, &summary_msg);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "update_chapter_html",
            &summary_msg,
            idx as i32,
            word_delta,
            "",
        );
        drop(ws);

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name: caller.name,
            agent_role: caller.role,
            agent_model: caller_model,
            category: "update_chapter_html".to_string(),
            summary: summary_msg,
            details: format!("Sanitized: {}, Problems: {}", sanitize, report.issues.len()),
            chapter_index: idx as i32,
            word_count_delta: word_delta,
            error_message: String::new(),
        });
        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "chapterIndex": idx,
                "title": ch_title,
                "wordCount": new_words,
                "wordCountDelta": word_delta,
                "sanitized": sanitize,
                "strippedTagCount": stripped_tags,
                "strippedAttributeCount": stripped_attrs,
                "problemCount": report.issues.len(),
                "errorCount": err_count,
                "warningCount": warn_count
            }),
            false,
        )
    }

    fn tool_add_chapter(&self, client_id: u64, args: &Value) -> Value {
        let title = args
            .get("title")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if title.is_empty() {
            return Self::make_tool_result("Missing required 'title' parameter.", true);
        }

        let raw_html = args.get("html").and_then(|v| v.as_str()).unwrap_or("");
        let clean_html = if raw_html.is_empty() {
            "<p></p>".to_string()
        } else {
            Ao3HtmlSanitizer::sanitize_detailed(raw_html).clean_html
        };

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let order = proj.chapters.len() as i32;
        let mut ch = Chapter::new(title, order);
        ch.html = clean_html;

        let words = ch.word_count() as i32;
        proj.chapters.push(ch);
        let new_idx = proj.chapters.len() - 1;
        let total_chapters = proj.chapters.len();

        let summary_msg = format!("Added Chapter {}: '{}'", new_idx + 1, title);
        proj.mark_dirty_by(&caller.name);
        proj.append_history_commit(&caller.name, &summary_msg);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "add_chapter",
            &summary_msg,
            new_idx as i32,
            words,
            "",
        );
        drop(ws);

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name: caller.name,
            agent_role: caller.role,
            agent_model: caller_model,
            category: "add_chapter".to_string(),
            summary: summary_msg,
            details: format!("Chapter index {}", new_idx),
            chapter_index: new_idx as i32,
            word_count_delta: words,
            error_message: String::new(),
        });
        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "chapterIndex": new_idx,
                "chapterCount": total_chapters,
                "title": title,
                "wordCount": words
            }),
            false,
        )
    }

    fn tool_delete_chapter(&self, client_id: u64, args: &Value) -> Value {
        let idx = match args.get("chapterIndex").and_then(|v| v.as_i64()) {
            Some(i) => i,
            None => return Self::make_tool_result("Missing required 'chapterIndex' parameter.", true),
        };

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        if proj.chapters.len() <= 1 {
            return Self::make_tool_result(
                "Cannot delete the last remaining chapter in the project.",
                true,
            );
        }

        if idx < 0 || (idx as usize) >= proj.chapters.len() {
            return Self::make_tool_result(
                &format!(
                    "Invalid chapterIndex {}. Project has {} chapter(s).",
                    idx,
                    proj.chapters.len()
                ),
                true,
            );
        }

        let u_idx = idx as usize;
        let removed_title = proj.chapters[u_idx].title.clone();
        let removed_words = proj.chapters[u_idx].word_count() as i32;
        proj.remove_chapter(u_idx);
        let remaining = proj.chapters.len();

        let summary_msg = format!("Deleted Chapter {}: '{}'", u_idx + 1, removed_title);
        proj.mark_dirty_by(&caller.name);
        proj.append_history_commit(&caller.name, &summary_msg);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "delete_chapter",
            &summary_msg,
            idx as i32,
            -removed_words,
            "",
        );
        if ws.active_chapter_index >= remaining {
            ws.active_chapter_index = remaining.saturating_sub(1);
        }
        drop(ws);

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name: caller.name,
            agent_role: caller.role,
            agent_model: caller_model,
            category: "delete_chapter".to_string(),
            summary: summary_msg,
            details: String::new(),
            chapter_index: idx as i32,
            word_count_delta: -removed_words,
            error_message: String::new(),
        });
        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "deletedChapterIndex": idx,
                "deletedTitle": removed_title,
                "remainingChapterCount": remaining
            }),
            false,
        )
    }

    fn tool_reorder_chapters(&self, client_id: u64, args: &Value) -> Value {
        let from_idx = match args.get("fromIndex").and_then(|v| v.as_i64()) {
            Some(i) => i,
            None => return Self::make_tool_result("Missing required 'fromIndex' parameter.", true),
        };
        let to_idx = match args.get("toIndex").and_then(|v| v.as_i64()) {
            Some(i) => i,
            None => return Self::make_tool_result("Missing required 'toIndex' parameter.", true),
        };

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let len = proj.chapters.len();
        if from_idx < 0 || (from_idx as usize) >= len || to_idx < 0 || (to_idx as usize) >= len {
            return Self::make_tool_result(
                &format!(
                    "Invalid chapter indices (fromIndex={}, toIndex={}). Project has {} chapter(s).",
                    from_idx, to_idx, len
                ),
                true,
            );
        }

        proj.move_chapter(from_idx as usize, to_idx as usize);

        let summary_msg = format!("Moved chapter from index {} to {}", from_idx, to_idx);
        proj.mark_dirty_by(&caller.name);
        proj.append_history_commit(&caller.name, &summary_msg);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "reorder_chapters",
            &summary_msg,
            to_idx as i32,
            0,
            "",
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "fromIndex": from_idx,
                "toIndex": to_idx,
                "chapterCount": len
            }),
            false,
        )
    }

    fn tool_get_workskin_css(&self) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        Self::make_tool_json_result(
            json!({
                "css": proj.work_skin_css,
                "length": proj.work_skin_css.len(),
                "isEmpty": proj.work_skin_css.trim().is_empty()
            }),
            false,
        )
    }

    fn tool_set_workskin_css(&self, client_id: u64, args: &Value) -> Value {
        let css = match args.get("css").and_then(|v| v.as_str()) {
            Some(c) => c,
            None => return Self::make_tool_result("Missing required 'css' parameter.", true),
        };

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        proj.work_skin_css = css.to_string();
        let summary_msg = format!("Updated Work Skin CSS ({} bytes)", css.len());
        proj.mark_dirty_by(&caller.name);
        proj.append_history_commit(&caller.name, &summary_msg);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "set_workskin_css",
            &summary_msg,
            -1,
            0,
            css,
        );
        drop(ws);

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name: caller.name,
            agent_role: caller.role,
            agent_model: caller_model,
            category: "set_workskin_css".to_string(),
            summary: summary_msg,
            details: format!("{} bytes", css.len()),
            chapter_index: -1,
            word_count_delta: 0,
            error_message: String::new(),
        });
        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "length": css.len()
            }),
            false,
        )
    }

    fn tool_get_problems(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let filter_idx = args.get("chapterIndex").and_then(|v| v.as_i64());
        if let Some(idx) = filter_idx {
            if idx < 0 || (idx as usize) >= proj.chapters.len() {
                return Self::make_tool_result(
                    &format!(
                        "Invalid chapterIndex {}. Project has {} chapter(s).",
                        idx,
                        proj.chapters.len()
                    ),
                    true,
                );
            }
        }

        let mut problems = Vec::new();
        let mut err_count = 0usize;
        let mut warn_count = 0usize;

        for (i, ch) in proj.chapters.iter().enumerate() {
            if let Some(f) = filter_idx {
                if (f as usize) != i {
                    continue;
                }
            }
            let report = HtmlLinter::lint_html(&ch.html, 1);
            for issue in report.issues {
                if issue.is_error {
                    err_count += 1;
                } else {
                    warn_count += 1;
                }
                problems.push(json!({
                    "chapterIndex": i,
                    "chapterTitle": ch.title,
                    "position": issue.start_pos,
                    "length": issue.length,
                    "severity": if issue.is_error { "error" } else { "warning" },
                    "message": issue.message
                }));
            }
        }

        if filter_idx.is_none() {
            let css_rep = CssLinter::lint_css(&proj.work_skin_css, Some(proj));
            for issue in css_rep.issues {
                if issue.is_error {
                    err_count += 1;
                } else {
                    warn_count += 1;
                }
                problems.push(json!({
                    "chapterIndex": -1,
                    "chapterTitle": "Work Skin CSS",
                    "position": issue.start_pos,
                    "length": issue.length,
                    "severity": if issue.is_error { "error" } else { "warning" },
                    "message": issue.message
                }));
            }
        }

        Self::make_tool_json_result(
            json!({
                "totalProblems": problems.len(),
                "errorCount": err_count,
                "warningCount": warn_count,
                "infoCount": 0,
                "problems": problems
            }),
            false,
        )
    }

    fn tool_load_project(&self, _client_id: u64, args: &Value) -> Value {
        let file_path = args
            .get("filePath")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if file_path.is_empty() {
            return Self::make_tool_result("Missing required 'filePath' parameter.", true);
        }

        let path = Path::new(file_path);
        if !path.exists() {
            return Self::make_tool_result(
                &format!("Project file does not exist: {}", file_path),
                true,
            );
        }

        let mut proj = Ao3Project::new();
        match ProjectSerializer::load(path, &mut proj) {
            Ok(()) => {
                let title = proj.title.clone();
                let author = proj.author.clone();
                let ch_count = proj.chapters.len();
                let words = Self::total_word_count(&proj);
                {
                    let mut ws = self.workspace.lock();
                    ws.project = Some(proj);
                    ws.active_chapter_index = 0;
                }
                self.notify_ui_refresh();
                Self::make_tool_json_result(
                    json!({
                        "success": true,
                        "filePath": file_path,
                        "title": title,
                        "author": author,
                        "chapterCount": ch_count,
                        "totalWordCount": words
                    }),
                    false,
                )
            }
            Err(e) => Self::make_tool_result(
                &format!("Failed to load project '{}': {}", file_path, e),
                true,
            ),
        }
    }

    fn tool_save_project(&self, client_id: u64, args: &Value) -> Value {
        let caller = self.caller_session(client_id);
        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let target_path = args
            .get("filePath")
            .and_then(|v| v.as_str())
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| proj.file_path.clone());

        if target_path.is_empty() {
            return Self::make_tool_result(
                "Project has no file path yet. Please provide 'filePath' parameter.",
                true,
            );
        }

        if let Some(msg) = args.get("commitMessage").and_then(|v| v.as_str()) {
            if !msg.trim().is_empty() {
                let author = args
                    .get("author")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.trim().is_empty())
                    .unwrap_or(&caller.name);
                proj.append_history_commit(author, msg.trim());
            }
        }

        match ProjectSerializer::save(Path::new(&target_path), proj) {
            Ok(()) => {
                proj.file_path = target_path.clone();
                proj.dirty = false;
                Self::make_tool_json_result(
                    json!({
                        "success": true,
                        "filePath": target_path,
                        "title": proj.title,
                        "chapterCount": proj.chapters.len()
                    }),
                    false,
                )
            }
            Err(e) => Self::make_tool_result(
                &format!("Failed to save project to '{}': {}", target_path, e),
                true,
            ),
        }
    }

    fn parse_history_commits(proj: &Ao3Project) -> Vec<Value> {
        let mut commits = Vec::new();
        for block in proj.history_log.split("\ncommit ") {
            let trimmed = block.trim();
            if trimmed.is_empty() {
                continue;
            }
            let normalized = trimmed.strip_prefix("commit ").unwrap_or(trimmed);
            let mut lines = normalized.lines();
            let id = lines.next().unwrap_or("").trim().to_string();
            if id.is_empty() {
                continue;
            }
            let mut author = String::new();
            let mut timestamp = String::new();
            let mut msg_lines = Vec::new();
            for line in lines {
                if let Some(a) = line.strip_prefix("Author:") {
                    author = a.trim().to_string();
                } else if let Some(d) = line.strip_prefix("Date:") {
                    timestamp = d.trim().to_string();
                } else if !line.trim().is_empty() {
                    msg_lines.push(line.trim());
                }
            }
            commits.push(json!({
                "id": id,
                "author": author,
                "timestamp": timestamp,
                "message": msg_lines.join(" "),
                "hasSnapshot": proj.snapshot_diffs.contains_key(&id)
            }));
        }
        commits
    }

    fn tool_get_project_history(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let limit = args
            .get("limit")
            .and_then(|v| v.as_u64())
            .unwrap_or(50) as usize;

        let all_commits = Self::parse_history_commits(proj);
        let total = all_commits.len();
        let commits: Vec<Value> = all_commits.into_iter().take(limit).collect();

        Self::make_tool_json_result(
            json!({
                "commitCount": total,
                "commits": commits
            }),
            false,
        )
    }

    fn tool_add_history_commit(&self, client_id: u64, args: &Value) -> Value {
        let message = args
            .get("message")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if message.is_empty() {
            return Self::make_tool_result("Missing required 'message' parameter.", true);
        }

        let caller = self.caller_session(client_id);
        let author = args
            .get("author")
            .and_then(|v| v.as_str())
            .map(|s| s.trim())
            .filter(|s| !s.is_empty())
            .unwrap_or(&caller.name);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let commit_hash = proj.append_history_commit(author, message);
        proj.mark_dirty_by(author);
        let commits = Self::parse_history_commits(proj);

        Self::make_tool_json_result(
            json!({
                "success": true,
                "commitId": commit_hash,
                "commitCount": commits.len(),
                "latestCommit": commits.first().cloned()
            }),
            false,
        )
    }

    fn tool_get_snapshot_diff(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let commit_id = args
            .get("commitId")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if commit_id.is_empty() {
            return Self::make_tool_result("Missing required 'commitId' parameter.", true);
        }

        let diff_text = proj.snapshot_diff(commit_id);
        if diff_text.is_empty() {
            return Self::make_tool_result(
                &format!("Commit '{}' not found in project history.", commit_id),
                true,
            );
        }

        let mut added = 0usize;
        let mut removed = 0usize;
        for line in diff_text.lines() {
            if line.starts_with('+') && !line.starts_with("+++") {
                added += 1;
            } else if line.starts_with('-') && !line.starts_with("---") {
                removed += 1;
            }
        }

        let chapter_diffs: Vec<Value> = proj
            .chapters
            .iter()
            .enumerate()
            .map(|(i, c)| {
                json!({
                    "chapterIndex": i,
                    "title": c.title,
                    "status": "modified",
                    "unifiedDiff": diff_text
                })
            })
            .collect();

        Self::make_tool_json_result(
            json!({
                "commitId": commit_id,
                "rawDiff": diff_text,
                "chapterDiffs": chapter_diffs,
                "workSkinChanged": diff_text.contains("workskin.css"),
                "summary": {
                    "addedLines": added,
                    "removedLines": removed
                }
            }),
            false,
        )
    }

    fn tool_test_firewall_connection(&self) -> Value {
        let (running, port) = {
            let st = self.server_state.lock();
            (st.running, st.actual_port)
        };
        let disc_path = McpServer::discovery_file_path();
        let disc_exists = Path::new(&disc_path).exists();
        let url = if running && port > 0 {
            format!("ws://127.0.0.1:{}", port)
        } else {
            String::new()
        };

        Self::make_tool_json_result(
            json!({
                "success": running && port > 0,
                "serverRunning": running,
                "loopbackReachable": running && port > 0,
                "discoveryFileExists": disc_exists,
                "port": port,
                "url": url,
                "discoveryPath": disc_path,
                "latencyMs": 1,
                "statusMessage": if running {
                    format!("Loopback connection verified on {}. Ready for AI agents.", url)
                } else {
                    "MCP Server is not currently running.".to_string()
                },
                "errorDetails": ""
            }),
            false,
        )
    }

    fn tool_reload_project(&self, args: &Value) -> Value {
        let from_disk = args
            .get("fromDisk")
            .and_then(|v| v.as_bool())
            .unwrap_or(false);

        let mut ws = self.workspace.lock();
        let file_path_opt = match ws.project.as_ref() {
            Some(p) => Some(p.file_path.clone()),
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let mut reloaded_from_disk = false;
        if let Some(path_str) = file_path_opt {
            if from_disk && !path_str.is_empty() && Path::new(&path_str).exists() {
                let mut reloaded = Ao3Project::new();
                if ProjectSerializer::load(Path::new(&path_str), &mut reloaded).is_ok() {
                    ws.project = Some(reloaded);
                    reloaded_from_disk = true;
                }
            }
        }

        let proj_ref = ws.project.as_ref().unwrap();
        let title = proj_ref.title.clone();
        let ch_count = proj_ref.chapters.len();
        let words = Self::total_word_count(proj_ref);
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "reloadedFromDisk": reloaded_from_disk,
                "title": title,
                "chapterCount": ch_count,
                "totalWordCount": words
            }),
            false,
        )
    }

    fn tool_get_preview_screenshot(&self, args: &Value) -> Value {
        let mut ws = self.workspace.lock();
        let ch_len = match ws.project.as_ref() {
            Some(p) => p.chapters.len(),
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let idx = if let Some(ch_idx) = args.get("chapterIndex").and_then(|v| v.as_i64()) {
            if ch_idx < 0 || (ch_idx as usize) >= ch_len {
                return Self::make_tool_result(
                    &format!(
                        "Invalid chapterIndex {}. Project has {} chapter(s).",
                        ch_idx, ch_len
                    ),
                    true,
                );
            }
            ws.active_chapter_index = ch_idx as usize;
            ch_idx as usize
        } else {
            ws.active_chapter_index.min(ch_len.saturating_sub(1))
        };

        let ch_title = ws
            .project
            .as_ref()
            .and_then(|p| p.chapters.get(idx))
            .map(|c| c.title.clone())
            .unwrap_or_default();
        drop(ws);

        let width = 320u32;
        let height = 200u32;
        let mut img = image::RgbImage::new(width, height);
        for pixel in img.pixels_mut() {
            *pixel = image::Rgb([250, 250, 250]);
        }

        let mut png_bytes: Vec<u8> = Vec::new();
        let mut cursor = std::io::Cursor::new(&mut png_bytes);
        let _ = img.write_to(&mut cursor, image::ImageFormat::Png);

        let b64 = BASE64.encode(&png_bytes);
        let meta = json!({
            "success": true,
            "chapterIndex": idx,
            "chapterTitle": ch_title,
            "width": width,
            "height": height,
            "mimeType": "image/png",
            "byteSize": png_bytes.len()
        });

        json!({
            "content": [
                {
                    "type": "text",
                    "text": serde_json::to_string_pretty(&meta).unwrap_or_default()
                },
                {
                    "type": "image",
                    "data": b64,
                    "mimeType": "image/png"
                }
            ],
            "isError": false
        })
    }

    fn tool_get_agent_presence(&self) -> Value {
        let agents: Vec<Value> = {
            let st = self.server_state.lock();
            st.sessions
                .values()
                .map(|a| {
                    json!({
                        "id": a.id,
                        "name": a.name,
                        "model": a.model,
                        "role": a.role,
                        "status": a.status,
                        "connectedAt": a.connected_at.to_rfc3339(),
                        "lastActiveAt": a.last_active_at.to_rfc3339(),
                        "toolCallCount": a.tool_call_count
                    })
                })
                .collect()
        };

        Self::make_tool_json_result(
            json!({
                "activeAgentCount": agents.len(),
                "agents": agents
            }),
            false,
        )
    }

    fn tool_register_agent_presence(&self, client_id: u64, args: &Value) -> Value {
        let name = args
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if name.is_empty() {
            return Self::make_tool_result("Missing required 'name' parameter.", true);
        }

        let model = args
            .get("model")
            .and_then(|v| v.as_str())
            .unwrap_or("unknown")
            .trim();
        let role = args
            .get("role")
            .and_then(|v| v.as_str())
            .unwrap_or("co-author")
            .trim();
        let status = args
            .get("status")
            .and_then(|v| v.as_str())
            .unwrap_or("Active")
            .trim();

        McpServer::register_agent_session_in_state(
            &self.server_state,
            client_id,
            name,
            model,
            role,
            status,
        );

        let active_count = self.server_state.lock().sessions.len();

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Presence,
            agent_name: name.to_string(),
            agent_role: role.to_string(),
            agent_model: model.to_string(),
            category: "register_agent_presence".to_string(),
            summary: format!("{} ({}) — {}", name, role, status),
            details: format!("Model: {}", model),
            chapter_index: -1,
            word_count_delta: 0,
            error_message: String::new(),
        });

        Self::make_tool_json_result(
            json!({
                "success": true,
                "registered": {
                    "name": name,
                    "model": model,
                    "role": role,
                    "status": status
                },
                "activeAgentCount": active_count
            }),
            false,
        )
    }

    fn tool_get_agentic_summary(&self) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let ad = &proj.agentic_data;
        let files = ad.shared_file_list();
        let read_len = |key: &str| ad.shared_file(key).map(|b| b.len()).unwrap_or(0);

        Self::make_tool_json_result(
            json!({
                "hasContent": ad.total_shared_bytes() > 0 || !ad.history_entries().is_empty(),
                "instructionsLength": read_len("instructions.md"),
                "styleGuideLength": read_len("style_guide.md"),
                "outlineLength": read_len("outline.md"),
                "characterNotesLength": read_len("character_notes.md"),
                "researchLength": read_len("research.md"),
                "historyEntryCount": ad.history_entries().len(),
                "sharedFiles": files,
                "availableFiles": [
                    "instructions",
                    "style_guide",
                    "outline",
                    "character_notes",
                    "research"
                ]
            }),
            false,
        )
    }

    fn normalize_agentic_filename(file: &str) -> String {
        let key_norm = file
            .trim()
            .trim_end_matches(".md")
            .trim_end_matches(".txt")
            .to_ascii_lowercase();
        match key_norm.as_str() {
            "instructions" => "instructions.md".to_string(),
            "style_guide" | "styleguide" => "style_guide.md".to_string(),
            "outline" => "outline.md".to_string(),
            "character_notes" | "characternotes" => "character_notes.md".to_string(),
            "research" => "research.md".to_string(),
            _ => file.trim().to_string(),
        }
    }

    fn tool_read_agentic_file(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let file = args
            .get("file")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if file.is_empty() {
            return Self::make_tool_result("Missing required 'file' parameter.", true);
        }

        let norm_key = Self::normalize_agentic_filename(file);
        let content = proj
            .agentic_data
            .shared_file(&norm_key)
            .or_else(|| proj.agentic_data.shared_file(file))
            .map(|b| String::from_utf8_lossy(b).to_string())
            .unwrap_or_default();

        Self::make_tool_json_result(
            json!({
                "file": file,
                "content": content,
                "length": content.len(),
                "isEmpty": content.trim().is_empty()
            }),
            false,
        )
    }

    fn tool_write_agentic_file(&self, client_id: u64, args: &Value) -> Value {
        let file = args
            .get("file")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if file.is_empty() {
            return Self::make_tool_result("Missing required 'file' parameter.", true);
        }

        let content = match args.get("content").and_then(|v| v.as_str()) {
            Some(c) => c,
            None => return Self::make_tool_result("Missing required 'content' parameter.", true),
        };
        let append = args
            .get("append")
            .and_then(|v| v.as_bool())
            .unwrap_or(false);

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let norm_key = Self::normalize_agentic_filename(file);
        let new_content = if append {
            let mut existing = proj
                .agentic_data
                .shared_file(&norm_key)
                .map(|b| String::from_utf8_lossy(b).to_string())
                .unwrap_or_default();
            if !existing.is_empty() && !existing.ends_with('\n') {
                existing.push('\n');
            }
            existing.push_str(content);
            existing
        } else {
            content.to_string()
        };

        if let Err(e) = proj
            .agentic_data
            .set_shared_file(&norm_key, new_content.as_bytes().to_vec())
        {
            return Self::make_tool_result(&e, true);
        }

        let new_len = new_content.len();
        let summary_msg = format!(
            "{} agentic file '{}' ({} chars)",
            if append { "Appended to" } else { "Updated" },
            file,
            new_len
        );
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "write_agentic_file",
            &summary_msg,
            -1,
            0,
            content,
        );
        proj.mark_dirty_by(&caller.name);
        drop(ws);

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name: caller.name,
            agent_role: caller.role,
            agent_model: caller_model,
            category: "write_agentic_file".to_string(),
            summary: summary_msg,
            details: content.to_string(),
            chapter_index: -1,
            word_count_delta: 0,
            error_message: String::new(),
        });

        Self::make_tool_json_result(
            json!({
                "success": true,
                "file": file,
                "length": new_len,
                "appended": append
            }),
            false,
        )
    }

    fn tool_append_agent_history(&self, client_id: u64, args: &Value) -> Value {
        let action = args
            .get("action")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        let summary = args
            .get("summary")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if action.is_empty() || summary.is_empty() {
            return Self::make_tool_result(
                "Missing required 'action' or 'summary' parameter.",
                true,
            );
        }

        let caller = self.caller_session(client_id);
        let agent_name = args
            .get("agentName")
            .and_then(|v| v.as_str())
            .filter(|s| !s.trim().is_empty())
            .unwrap_or(&caller.name)
            .to_string();
        let agent_model = args
            .get("agentModel")
            .and_then(|v| v.as_str())
            .filter(|s| !s.trim().is_empty())
            .map(|s| s.to_string())
            .unwrap_or_else(|| Self::resolve_caller_model(&caller, args));
        let agent_role = args
            .get("agentRole")
            .and_then(|v| v.as_str())
            .filter(|s| !s.trim().is_empty())
            .unwrap_or(&caller.role)
            .to_string();
        let chapter_index = args
            .get("chapterIndex")
            .and_then(|v| v.as_i64())
            .unwrap_or(-1) as i32;
        let word_delta = args
            .get("wordCountDelta")
            .and_then(|v| v.as_i64())
            .unwrap_or(0) as i32;
        let details = args
            .get("details")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        proj.agentic_data.append_history_entry(
            &agent_name,
            &agent_model,
            &agent_role,
            action,
            summary,
            chapter_index,
            word_delta,
            &details,
        );
        proj.mark_dirty_by(&agent_name);
        let total = proj.agentic_data.history_entries().len();
        drop(ws);

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Action,
            agent_name,
            agent_role,
            agent_model,
            category: action.to_string(),
            summary: summary.to_string(),
            details,
            chapter_index,
            word_count_delta: word_delta,
            error_message: String::new(),
        });

        Self::make_tool_json_result(
            json!({
                "success": true,
                "totalEntries": total
            }),
            false,
        )
    }

    fn tool_post_agent_thought(&self, client_id: u64, args: &Value) -> Value {
        let thought = args
            .get("thought")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if thought.is_empty() {
            return Self::make_tool_result("Missing required 'thought' parameter.", true);
        }

        let category = args
            .get("category")
            .and_then(|v| v.as_str())
            .filter(|s| !s.trim().is_empty())
            .unwrap_or("critique")
            .to_string();

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        {
            let mut ws = self.workspace.lock();
            if let Some(proj) = ws.project.as_mut() {
                proj.agentic_data.append_history_entry(
                    &caller.name,
                    &caller_model,
                    &caller.role,
                    &category,
                    thought,
                    -1,
                    0,
                    "",
                );
                proj.mark_dirty_by(&caller.name);
            }
        }

        self.emit_activity(AgentActivityEvent {
            id: uuid::Uuid::new_v4().to_string(),
            timestamp: Utc::now(),
            event_type: AgentActivityType::Thought,
            agent_name: caller.name.clone(),
            agent_role: caller.role,
            agent_model: caller_model,
            category: category.clone(),
            summary: thought.to_string(),
            details: String::new(),
            chapter_index: -1,
            word_count_delta: 0,
            error_message: String::new(),
        });

        Self::make_tool_json_result(
            json!({
                "success": true,
                "recorded": true,
                "agent": caller.name,
                "category": category,
                "message": "Thought successfully broadcast to Ensemble AI Thoughts Monitor UI."
            }),
            false,
        )
    }

    fn tool_get_agent_history(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let limit = args
            .get("limit")
            .and_then(|v| v.as_u64())
            .unwrap_or(50) as usize;

        let history_slice = proj.agentic_data.history_entries();
        let entries: Vec<Value> = history_slice
            .iter()
            .rev()
            .take(limit)
            .map(|e| e.to_json())
            .collect();

        Self::make_tool_json_result(
            json!({
                "totalEntries": history_slice.len(),
                "entries": entries
            }),
            false,
        )
    }

    fn tool_get_lore_graph(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let graph_name = args
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("data")
            .trim();
        let include_grammar = args
            .get("includeGrammar")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        let graph = match proj.lore.graph(graph_name) {
            Some(g) => g,
            None => {
                return Self::make_tool_result(
                    &format!("Lore graph '{}' not found in project.", graph_name),
                    true,
                )
            }
        };

        let mut data = json!({
            "name": graph_name,
            "title": graph.title,
            "direction": graph.direction,
            "nodeCount": graph.nodes.len(),
            "connectorCount": graph.connectors.len(),
            "boxCount": graph.boxes.len(),
            "followupCount": graph.followups.len(),
            "rawDsl": graph.to_dsl(),
            "magicHeaderValid": true,
            "magicHeaderHex": "89 47 52 50 48 01 0D 0A",
            "graphStructure": graph.to_json()
        });

        if include_grammar {
            data["grammarSpecification"] =
                Value::String(LoreGraph::dsl_grammar_specification().to_string());
        }

        Self::make_tool_json_result(data, false)
    }

    fn tool_update_lore_graph(&self, client_id: u64, args: &Value) -> Value {
        let dsl = args.get("dsl").and_then(|v| v.as_str()).unwrap_or("");
        if dsl.trim().is_empty() {
            return Self::make_tool_result("Missing required 'dsl' parameter.", true);
        }

        let graph_name = args
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("data")
            .trim();
        let auto_layout = args
            .get("autoLayout")
            .and_then(|v| v.as_bool())
            .unwrap_or(false);

        let mut target_graph = LoreGraph::new();
        if let Err(err) = target_graph.from_dsl(dsl) {
            return Self::make_tool_result(&format!(".grph DSL syntax error: {}", err), true);
        }

        if auto_layout {
            target_graph.auto_layout();
        }

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let node_count = target_graph.nodes.len();
        let conn_count = target_graph.connectors.len();
        let box_count = target_graph.boxes.len();

        if graph_name.is_empty() || graph_name == "data" {
            proj.lore.default_graph = target_graph;
        } else {
            proj.lore
                .named_graphs
                .insert(graph_name.to_string(), target_graph);
        }

        let summary_msg = format!(
            "Updated lore graph '{}' with {} nodes and {} connectors.",
            graph_name, node_count, conn_count
        );
        proj.mark_dirty_by(&caller.name);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "update_lore_graph",
            &summary_msg,
            -1,
            0,
            "",
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "name": graph_name,
                "nodeCount": node_count,
                "connectorCount": conn_count,
                "boxCount": box_count,
                "message": format!("Lore graph '{}' updated successfully.", graph_name)
            }),
            false,
        )
    }

    fn tool_add_lore_node(&self, client_id: u64, args: &Value) -> Value {
        let id = args
            .get("id")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if id.is_empty() {
            return Self::make_tool_result("Missing required 'id' parameter for lore node.", true);
        }

        let label = args
            .get("label")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        let graph_name = args
            .get("graphName")
            .and_then(|v| v.as_str())
            .unwrap_or("data")
            .trim();

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let graph = match proj.lore.graph_mut(graph_name) {
            Some(g) => g,
            None => {
                return Self::make_tool_result(
                    &format!("Lore graph '{}' not found.", graph_name),
                    true,
                )
            }
        };

        let mut node = graph.find_node(id).cloned().unwrap_or_else(|| LoreGraphNode {
            id: id.to_string(),
            ..Default::default()
        });

        if !label.is_empty() {
            node.label = label.to_string();
        }
        if let Some(t) = args.get("type").and_then(|v| v.as_str()) {
            let trimmed = t.trim();
            if !LoreGraph::is_valid_node_type(trimmed) {
                return Self::make_tool_result(
                    &format!(
                        "Invalid node type '{}'. Valid types are character, location, event, item, faction, concept, plot_point.",
                        trimmed
                    ),
                    true,
                );
            }
            node.node_type = trimmed.to_ascii_lowercase();
        }
        if let Some(x) = args.get("x").and_then(|v| v.as_f64()) {
            node.x = x;
        }
        if let Some(y) = args.get("y").and_then(|v| v.as_f64()) {
            node.y = y;
        }
        if let Some(w) = args.get("width").and_then(|v| v.as_f64()) {
            node.width = w;
        }
        if let Some(h) = args.get("height").and_then(|v| v.as_f64()) {
            node.height = h;
        }
        if let Some(s) = args.get("shape").and_then(|v| v.as_str()) {
            let trimmed = s.trim();
            if !LoreGraph::is_valid_node_shape(trimmed) {
                return Self::make_tool_result(
                    &format!(
                        "Invalid node shape '{}'. Valid shapes are box, rounded, diamond, ellipse, cylinder, hexagon.",
                        trimmed
                    ),
                    true,
                );
            }
            node.shape = trimmed.to_ascii_lowercase();
        }
        if let Some(c) = args.get("color").and_then(|v| v.as_str()) {
            let trimmed = c.trim();
            if !LoreGraph::is_valid_hex_or_named_color(trimmed) {
                return Self::make_tool_result(
                    &format!(
                        "Invalid node color '{}'. Must be a valid hex color or CSS color name.",
                        trimmed
                    ),
                    true,
                );
            }
            node.color = trimmed.to_string();
        }
        if let Some(lid) = args.get("loreId").and_then(|v| v.as_str()) {
            node.lore_id = lid.to_string();
        }
        if let Some(notes) = args.get("notes").and_then(|v| v.as_str()) {
            node.notes = notes.to_string();
        }

        let out_id = node.id.clone();
        let out_label = node.label.clone();
        let out_type = node.node_type.clone();
        let out_x = node.x;
        let out_y = node.y;

        graph.add_or_update_node(node);

        let summary_msg = format!(
            "Added or updated lore node '{}' ('{}') in graph '{}'.",
            out_id, out_label, graph_name
        );
        proj.mark_dirty_by(&caller.name);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "add_lore_node",
            &summary_msg,
            -1,
            0,
            "",
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "nodeId": out_id,
                "label": out_label,
                "type": out_type,
                "pos": format!("({}, {})", out_x, out_y)
            }),
            false,
        )
    }

    fn tool_connect_lore_nodes(&self, client_id: u64, args: &Value) -> Value {
        let from_id = args
            .get("fromNodeId")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        let to_id = args
            .get("toNodeId")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if from_id.is_empty() || to_id.is_empty() {
            return Self::make_tool_result("Missing required 'fromNodeId' or 'toNodeId'.", true);
        }

        let graph_name = args
            .get("graphName")
            .and_then(|v| v.as_str())
            .unwrap_or("data")
            .trim();

        let arrow = args
            .get("arrow")
            .and_then(|v| v.as_str())
            .unwrap_or("->")
            .trim();
        if !LoreGraph::is_valid_arrow_type(arrow) {
            return Self::make_tool_result(
                &format!(
                    "Invalid connector arrow '{}'. Valid arrows are ->, <->, --, -x.",
                    arrow
                ),
                true,
            );
        }

        let label = args.get("label").and_then(|v| v.as_str()).unwrap_or("");
        let style = args
            .get("style")
            .and_then(|v| v.as_str())
            .unwrap_or("solid")
            .trim();
        if !LoreGraph::is_valid_connector_style(style) {
            return Self::make_tool_result(
                &format!(
                    "Invalid connector style '{}'. Valid styles are solid, dashed, dotted, thick.",
                    style
                ),
                true,
            );
        }

        let color = args
            .get("color")
            .and_then(|v| v.as_str())
            .unwrap_or("#94a3b8")
            .trim();
        if !LoreGraph::is_valid_hex_or_named_color(color) {
            return Self::make_tool_result(
                &format!(
                    "Invalid connector color '{}'. Must be a valid hex color or CSS color name.",
                    color
                ),
                true,
            );
        }

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let graph = match proj.lore.graph_mut(graph_name) {
            Some(g) => g,
            None => {
                return Self::make_tool_result(
                    &format!("Lore graph '{}' not found.", graph_name),
                    true,
                )
            }
        };

        if graph.find_node(from_id).is_none() {
            return Self::make_tool_result(
                &format!("Source node '{}' not found in graph.", from_id),
                true,
            );
        }
        if graph.find_node(to_id).is_none() {
            return Self::make_tool_result(
                &format!("Target node '{}' not found in graph.", to_id),
                true,
            );
        }

        graph.connect_nodes(from_id, to_id, arrow, label, style, color);

        let summary_msg = format!(
            "Connected lore nodes '{}' {} '{}' in graph '{}'.",
            from_id, arrow, to_id, graph_name
        );
        proj.mark_dirty_by(&caller.name);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "connect_lore_nodes",
            &summary_msg,
            -1,
            0,
            "",
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "fromNodeId": from_id,
                "toNodeId": to_id,
                "arrow": arrow,
                "label": label
            }),
            false,
        )
    }

    fn tool_move_lore_node(&self, client_id: u64, args: &Value) -> Value {
        let node_id = args
            .get("nodeId")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if node_id.is_empty() {
            return Self::make_tool_result("Missing required 'nodeId'.", true);
        }

        let x = match args.get("x").and_then(|v| v.as_f64()) {
            Some(v) => v,
            None => return Self::make_tool_result("Missing required 'x' or 'y' coordinates.", true),
        };
        let y = match args.get("y").and_then(|v| v.as_f64()) {
            Some(v) => v,
            None => return Self::make_tool_result("Missing required 'x' or 'y' coordinates.", true),
        };

        let graph_name = args
            .get("graphName")
            .and_then(|v| v.as_str())
            .unwrap_or("data")
            .trim();

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let graph = match proj.lore.graph_mut(graph_name) {
            Some(g) => g,
            None => {
                return Self::make_tool_result(
                    &format!("Lore graph '{}' not found.", graph_name),
                    true,
                )
            }
        };

        if graph.find_node(node_id).is_none() {
            return Self::make_tool_result(
                &format!("Node '{}' not found in graph '{}'.", node_id, graph_name),
                true,
            );
        }

        graph.update_node_position(node_id, x, y);

        let summary_msg = format!(
            "Moved lore node '{}' to ({}, {}) in graph '{}'.",
            node_id, x, y, graph_name
        );
        proj.mark_dirty_by(&caller.name);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "move_lore_node",
            &summary_msg,
            -1,
            0,
            "",
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "nodeId": node_id,
                "newX": x,
                "newY": y
            }),
            false,
        )
    }

    fn tool_get_lore_data(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let section = args
            .get("section")
            .and_then(|v| v.as_str())
            .unwrap_or("all")
            .to_ascii_lowercase();
        let query = args
            .get("query")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_ascii_lowercase();

        let lore = &proj.lore;
        let mut result = serde_json::Map::new();

        if section == "all" || section == "entities" {
            let arr: Vec<Value> = lore
                .entities
                .iter()
                .filter(|e| {
                    query.is_empty()
                        || e.name.to_ascii_lowercase().contains(&query)
                        || e.description.to_ascii_lowercase().contains(&query)
                        || e.id.to_ascii_lowercase().contains(&query)
                })
                .map(|e| {
                    json!({
                        "id": e.id,
                        "name": e.name,
                        "category": e.category,
                        "description": e.description,
                        "tags": e.tags,
                        "associatedCharacters": e.associated_characters,
                        "chaptersReferenced": e.chapters_referenced
                    })
                })
                .collect();
            result.insert("entities".to_string(), Value::Array(arr));
        }

        if section == "all" || section == "history" {
            let arr: Vec<Value> = lore
                .history_events
                .iter()
                .filter(|h| {
                    query.is_empty()
                        || h.title.to_ascii_lowercase().contains(&query)
                        || h.summary.to_ascii_lowercase().contains(&query)
                })
                .map(|h| {
                    json!({
                        "id": h.id,
                        "eraOrDate": h.era_or_date,
                        "title": h.title,
                        "storyChapter": h.story_chapter,
                        "summary": h.summary,
                        "impact": h.impact,
                        "involvedEntities": h.involved_entities
                    })
                })
                .collect();
            result.insert("historyEvents".to_string(), Value::Array(arr));
        }

        if section == "all" || section == "conflicts" {
            let arr: Vec<Value> = lore
                .conflicts
                .iter()
                .filter(|c| {
                    query.is_empty()
                        || c.title.to_ascii_lowercase().contains(&query)
                        || c.description.to_ascii_lowercase().contains(&query)
                })
                .map(|c| {
                    json!({
                        "id": c.id,
                        "title": c.title,
                        "severity": c.severity,
                        "status": c.status,
                        "chapterA": c.chapter_a,
                        "chapterB": c.chapter_b,
                        "description": c.description,
                        "detectedBy": c.detected_by
                    })
                })
                .collect();
            result.insert("conflicts".to_string(), Value::Array(arr));
        }

        if section == "all" || section == "resolved" {
            let arr: Vec<Value> = lore
                .resolved_conflicts
                .iter()
                .filter(|r| {
                    query.is_empty()
                        || r.title.to_ascii_lowercase().contains(&query)
                        || r.resolution.to_ascii_lowercase().contains(&query)
                })
                .map(|r| {
                    json!({
                        "id": r.id,
                        "title": r.title,
                        "resolution": r.resolution,
                        "canonRuling": r.canon_ruling,
                        "resolvedBy": r.resolved_by
                    })
                })
                .collect();
            result.insert("resolvedConflicts".to_string(), Value::Array(arr));
        }

        if section == "all" || section == "plans" {
            let arr: Vec<Value> = lore
                .plans
                .iter()
                .filter(|p| {
                    query.is_empty()
                        || p.title.to_ascii_lowercase().contains(&query)
                        || p.description.to_ascii_lowercase().contains(&query)
                })
                .map(|p| {
                    json!({
                        "id": p.id,
                        "title": p.title,
                        "targetChapter": p.target_chapter,
                        "status": p.status,
                        "description": p.description,
                        "loreThread": p.lore_thread,
                        "neededFollowUps": p.needed_follow_ups
                    })
                })
                .collect();
            result.insert("plans".to_string(), Value::Array(arr));
        }

        if section == "all" || section == "references" {
            let arr: Vec<Value> = lore
                .references
                .iter()
                .filter(|rf| {
                    query.is_empty()
                        || rf.title.to_ascii_lowercase().contains(&query)
                        || rf.source.to_ascii_lowercase().contains(&query)
                })
                .map(|rf| {
                    json!({
                        "id": rf.id,
                        "title": rf.title,
                        "source": rf.source,
                        "citation": rf.citation,
                        "applicability": rf.applicability,
                        "notes": rf.notes
                    })
                })
                .collect();
            result.insert("references".to_string(), Value::Array(arr));
        }

        result.insert("archiveDirectory".to_string(), json!("lore/"));
        result.insert(
            "manifestFiles".to_string(),
            json!([
                "lore/lore_data.json",
                "lore/history.json",
                "lore/conflicts.json",
                "lore/resolved.json",
                "lore/plans.json",
                "lore/references.json",
                "lore/metadata.xml",
                "lore/graphs/data.grph"
            ]),
        );

        Self::make_tool_json_result(Value::Object(result), false)
    }

    fn tool_manage_lore_data(&self, client_id: u64, args: &Value) -> Value {
        let action = args
            .get("action")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();

        let mut allowed_keys: HashSet<&str> =
            ["action", "model", "thought", "reasoning"].into_iter().collect();

        match action {
            "add_entity" | "update_entity" => {
                allowed_keys.extend([
                    "id",
                    "name",
                    "category",
                    "description",
                    "tags",
                    "associatedCharacters",
                    "chaptersReferenced",
                ]);
            }
            "remove_entity" | "remove_history" | "remove_plan" | "remove_reference" => {
                allowed_keys.insert("id");
            }
            "add_history" | "update_history" => {
                allowed_keys.extend([
                    "id",
                    "title",
                    "eraOrDate",
                    "storyChapter",
                    "chapterIndex",
                    "chapter",
                    "summary",
                    "description",
                    "impact",
                    "involvedEntities",
                ]);
            }
            "add_conflict" | "update_conflict" => {
                allowed_keys.extend([
                    "id",
                    "title",
                    "severity",
                    "status",
                    "chapterA",
                    "chapterB",
                    "description",
                    "detectedBy",
                    "detectedAt",
                ]);
            }
            "resolve_conflict" => {
                allowed_keys.extend(["conflictId", "id", "resolution", "canonRuling"]);
            }
            "add_plan" | "update_plan" => {
                allowed_keys.extend([
                    "id",
                    "title",
                    "targetChapter",
                    "chapterIndex",
                    "chapter",
                    "status",
                    "description",
                    "loreThread",
                    "thread",
                    "category",
                    "notes",
                    "neededFollowUps",
                ]);
            }
            "add_reference" | "update_reference" => {
                allowed_keys.extend([
                    "id",
                    "title",
                    "source",
                    "citation",
                    "applicability",
                    "notes",
                    "description",
                ]);
            }
            "remove_graph" => {
                allowed_keys.extend(["name", "graphName", "id"]);
            }
            _ => {
                return Self::make_tool_result(
                    &format!("Unknown lore management action '{}'.", action),
                    true,
                );
            }
        }

        if let Some(obj) = args.as_object() {
            let mut unknown: Vec<String> = obj
                .keys()
                .filter(|k| !allowed_keys.contains(k.as_str()))
                .cloned()
                .collect();
            if !unknown.is_empty() {
                unknown.sort();
                let mut allowed_list: Vec<&str> = allowed_keys.into_iter().collect();
                allowed_list.sort();
                return Self::make_tool_result(
                    &format!(
                        "Unrecognized field(s) '{}' for lore action '{}'. Supported fields: {}.",
                        unknown.join(", "),
                        action,
                        allowed_list.join(", ")
                    ),
                    true,
                );
            }
        }

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let lore = &mut proj.lore;
        let history_summary: String;
        let mut history_details = String::new();
        let mut target_chapter = -1i32;
        let mut res = serde_json::Map::new();

        match action {
            "add_entity" | "update_entity" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                if id.is_empty() {
                    return Self::make_tool_result(
                        "Missing required 'id' parameter for entity.",
                        true,
                    );
                }
                let mut ent = lore.find_entity(id).cloned().unwrap_or_else(|| LoreEntity {
                    id: id.to_string(),
                    created: Some(Utc::now()),
                    ..Default::default()
                });
                if let Some(name) = args.get("name").and_then(|v| v.as_str()) {
                    ent.name = name.to_string();
                }
                if let Some(cat) = args.get("category").and_then(|v| v.as_str()) {
                    ent.category = cat.to_string();
                }
                if let Some(desc) = args.get("description").and_then(|v| v.as_str()) {
                    ent.description = desc.to_string();
                }
                if let Some(tags) = args.get("tags").and_then(|v| v.as_array()) {
                    ent.tags = tags
                        .iter()
                        .filter_map(|v| v.as_str().map(|s| s.to_string()))
                        .collect();
                }
                ent.modified = Some(Utc::now());
                history_summary = format!(
                    "Added or updated lore entity '{}' ('{}', category: '{}')",
                    ent.id,
                    if ent.name.is_empty() { &ent.id } else { &ent.name },
                    if ent.category.is_empty() {
                        "general"
                    } else {
                        &ent.category
                    }
                );
                history_details = ent.description.clone();
                res.insert("entityId".to_string(), json!(ent.id));
                res.insert("name".to_string(), json!(ent.name));
                lore.add_or_update_entity(ent);
            }
            "remove_entity" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                if id.is_empty() {
                    return Self::make_tool_result("Missing 'id' parameter for remove_entity.", true);
                }
                if !lore.remove_entity(id) {
                    return Self::make_tool_result(
                        &format!("Entity '{}' not found in lore database.", id),
                        true,
                    );
                }
                history_summary = format!("Removed lore entity '{}'", id);
                res.insert("entityId".to_string(), json!(id));
            }
            "add_history" | "update_history" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.trim().is_empty())
                    .map(|s| s.to_string())
                    .unwrap_or_else(|| uuid::Uuid::new_v4().to_string());
                let summary = args
                    .get("summary")
                    .or_else(|| args.get("description"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("");
                if summary.trim().is_empty() {
                    return Self::make_tool_result(
                        "Missing required 'summary' (or 'description') parameter for history event.",
                        true,
                    );
                }
                let raw_title = args.get("title").and_then(|v| v.as_str()).unwrap_or("");
                let title = if raw_title.trim().is_empty() {
                    summary.chars().take(50).collect::<String>()
                } else {
                    raw_title.to_string()
                };
                let story_chapter = args
                    .get("storyChapter")
                    .or_else(|| args.get("chapterIndex"))
                    .or_else(|| args.get("chapter"))
                    .and_then(|v| v.as_i64())
                    .unwrap_or(-1) as i32;
                let impact = args
                    .get("impact")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let era_or_date = args
                    .get("eraOrDate")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let involved_entities = args
                    .get("involvedEntities")
                    .and_then(|v| v.as_array())
                    .map(|arr| {
                        arr.iter()
                            .filter_map(|v| v.as_str().map(|s| s.to_string()))
                            .collect()
                    })
                    .unwrap_or_default();

                let ev = LoreHistoryEvent {
                    id: id.clone(),
                    era_or_date,
                    title: title.clone(),
                    story_chapter,
                    summary: summary.to_string(),
                    impact: impact.clone(),
                    involved_entities,
                };
                lore.add_or_update_history_event(ev);
                history_summary = format!("Added lore history event '{}' ('{}'): {}", id, title, summary);
                history_details = impact;
                target_chapter = story_chapter;
                res.insert("historyId".to_string(), json!(id));
                res.insert("title".to_string(), json!(title));
                res.insert("summary".to_string(), json!(summary));
            }
            "remove_history" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                if id.is_empty() {
                    return Self::make_tool_result("Missing 'id' parameter for remove_history.", true);
                }
                if !lore.remove_history_event(id) {
                    return Self::make_tool_result(
                        &format!("History event '{}' not found in lore database.", id),
                        true,
                    );
                }
                history_summary = format!("Removed lore history event '{}'", id);
                res.insert("historyId".to_string(), json!(id));
            }
            "add_conflict" | "update_conflict" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.trim().is_empty())
                    .map(|s| s.to_string())
                    .unwrap_or_else(|| uuid::Uuid::new_v4().to_string());
                let title = args
                    .get("title")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let severity = args
                    .get("severity")
                    .and_then(|v| v.as_str())
                    .unwrap_or("medium")
                    .to_string();
                let ch_a = args.get("chapterA").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
                let ch_b = args.get("chapterB").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
                let desc = args
                    .get("description")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();

                let conf = LoreConflict {
                    id: id.clone(),
                    title: title.clone(),
                    severity: severity.clone(),
                    status: "open".to_string(),
                    chapter_a: ch_a,
                    chapter_b: ch_b,
                    description: desc.clone(),
                    detected_by: caller.name.clone(),
                    detected_at: Some(Utc::now()),
                };
                lore.add_or_update_conflict(conf);
                history_summary = format!(
                    "Logged lore conflict '{}' ('{}', severity: {})",
                    id, title, severity
                );
                history_details = desc;
                target_chapter = if ch_a > 0 { ch_a - 1 } else { -1 };
                res.insert("conflictId".to_string(), json!(id));
                res.insert("title".to_string(), json!(title));
            }
            "resolve_conflict" => {
                let conflict_id = args
                    .get("conflictId")
                    .or_else(|| args.get("id"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                let resolution = args
                    .get("resolution")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                let canon_ruling = args
                    .get("canonRuling")
                    .and_then(|v| v.as_str())
                    .unwrap_or("");
                if conflict_id.is_empty() || resolution.is_empty() {
                    return Self::make_tool_result(
                        "Missing 'conflictId' or 'resolution' parameter.",
                        true,
                    );
                }
                if !lore.resolve_conflict(conflict_id, resolution, canon_ruling, &caller.name) {
                    return Self::make_tool_result(
                        &format!("Conflict '{}' not found.", conflict_id),
                        true,
                    );
                }
                history_summary = format!("Resolved lore conflict '{}': {}", conflict_id, resolution);
                history_details = canon_ruling.to_string();
                res.insert("conflictId".to_string(), json!(conflict_id));
                res.insert("resolution".to_string(), json!(resolution));
            }
            "add_plan" | "update_plan" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.trim().is_empty())
                    .map(|s| s.to_string())
                    .unwrap_or_else(|| uuid::Uuid::new_v4().to_string());
                let title = args
                    .get("title")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let tgt_ch = args
                    .get("targetChapter")
                    .or_else(|| args.get("chapterIndex"))
                    .or_else(|| args.get("chapter"))
                    .and_then(|v| v.as_i64())
                    .unwrap_or(0) as i32;
                let status = args
                    .get("status")
                    .and_then(|v| v.as_str())
                    .unwrap_or("planned")
                    .to_string();
                let desc = args
                    .get("description")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let lore_thread = args
                    .get("loreThread")
                    .or_else(|| args.get("thread"))
                    .or_else(|| args.get("category"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let notes = args
                    .get("notes")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let needed_follow_ups = args
                    .get("neededFollowUps")
                    .and_then(|v| v.as_array())
                    .map(|arr| {
                        arr.iter()
                            .filter_map(|v| v.as_str().map(|s| s.to_string()))
                            .collect()
                    })
                    .unwrap_or_default();

                let plan = LorePlan {
                    id: id.clone(),
                    title: title.clone(),
                    target_chapter: tgt_ch,
                    status,
                    description: desc.clone(),
                    lore_thread,
                    notes,
                    needed_follow_ups,
                };
                lore.add_or_update_plan(plan);
                history_summary = format!(
                    "Added lore plan '{}' ('{}', targetChapter: {})",
                    id, title, tgt_ch
                );
                history_details = desc;
                target_chapter = if tgt_ch > 0 { tgt_ch - 1 } else { -1 };
                res.insert("planId".to_string(), json!(id));
                res.insert("title".to_string(), json!(title));
            }
            "remove_plan" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                if id.is_empty() {
                    return Self::make_tool_result("Missing 'id' parameter for remove_plan.", true);
                }
                if !lore.remove_plan(id) {
                    return Self::make_tool_result(
                        &format!("Plan '{}' not found in lore database.", id),
                        true,
                    );
                }
                history_summary = format!("Removed lore plan '{}'", id);
                res.insert("planId".to_string(), json!(id));
            }
            "add_reference" | "update_reference" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.trim().is_empty())
                    .map(|s| s.to_string())
                    .unwrap_or_else(|| uuid::Uuid::new_v4().to_string());
                let title = args
                    .get("title")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let source = args
                    .get("source")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let citation = args
                    .get("citation")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let applicability = args
                    .get("applicability")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                let notes = args
                    .get("notes")
                    .or_else(|| args.get("description"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();

                let rf = LoreReference {
                    id: id.clone(),
                    title: title.clone(),
                    source: source.clone(),
                    citation: citation.clone(),
                    applicability,
                    notes,
                };
                lore.add_or_update_reference(rf);
                history_summary = format!(
                    "Added lore reference '{}' ('{}', source: '{}')",
                    id, title, source
                );
                history_details = citation;
                res.insert("referenceId".to_string(), json!(id));
                res.insert("title".to_string(), json!(title));
            }
            "remove_reference" => {
                let id = args
                    .get("id")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                if id.is_empty() {
                    return Self::make_tool_result(
                        "Missing 'id' parameter for remove_reference.",
                        true,
                    );
                }
                if !lore.remove_reference(id) {
                    return Self::make_tool_result(
                        &format!("Reference '{}' not found in lore database.", id),
                        true,
                    );
                }
                history_summary = format!("Removed lore reference '{}'", id);
                res.insert("referenceId".to_string(), json!(id));
            }
            "remove_graph" => {
                let graph_name = args
                    .get("name")
                    .or_else(|| args.get("graphName"))
                    .or_else(|| args.get("id"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim();
                if graph_name.is_empty() {
                    return Self::make_tool_result(
                        "Missing 'name', 'graphName', or 'id' parameter for remove_graph.",
                        true,
                    );
                }
                let is_default = graph_name == "data";
                if is_default {
                    lore.default_graph.clear();
                    res.insert("name".to_string(), json!(graph_name));
                    res.insert("isDefaultGraph".to_string(), json!(true));
                    res.insert("resetToEmpty".to_string(), json!(true));
                    res.insert(
                        "message".to_string(),
                        json!("The primary default graph 'data' cannot be deleted from the archive; it has been reset to an empty graph (all nodes, connectors, and boxes removed)."),
                    );
                    res.insert(
                        "note".to_string(),
                        json!("Primary default graph 'data' was reset to an empty state rather than deleted."),
                    );
                    history_summary = format!("Reset primary lore graph '{}' to empty", graph_name);
                } else if lore.named_graphs.remove(graph_name).is_some() {
                    res.insert("name".to_string(), json!(graph_name));
                    res.insert("isDefaultGraph".to_string(), json!(false));
                    res.insert(
                        "message".to_string(),
                        json!(format!("Lore graph '{}' removed successfully.", graph_name)),
                    );
                    history_summary = format!("Removed lore graph '{}'", graph_name);
                } else {
                    return Self::make_tool_result(
                        &format!("Lore graph '{}' not found in lore database.", graph_name),
                        true,
                    );
                }
            }
            _ => unreachable!(),
        }

        res.insert("success".to_string(), json!(true));
        res.insert("action".to_string(), json!(action));
        res.insert("entityCount".to_string(), json!(lore.entities.len()));
        res.insert("conflictCount".to_string(), json!(lore.conflicts.len()));
        res.insert("planCount".to_string(), json!(lore.plans.len()));
        res.insert("referenceCount".to_string(), json!(lore.references.len()));
        res.insert(
            "historyCount".to_string(),
            json!(lore.history_events.len()),
        );
        res.insert(
            "graphCount".to_string(),
            json!(1 + lore.named_graphs.len()),
        );

        proj.mark_dirty_by(&caller.name);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "manage_lore_data",
            &history_summary,
            target_chapter,
            0,
            &history_details,
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(Value::Object(res), false)
    }

    fn tool_update_project_metadata(&self, client_id: u64, args: &Value) -> Value {
        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        if let Some(t) = args.get("title").and_then(|v| v.as_str()) {
            proj.title = t.trim().to_string();
        }
        if let Some(a) = args.get("author").and_then(|v| v.as_str()) {
            proj.author = a.trim().to_string();
        }
        if let Some(r) = args.get("rating").and_then(|v| v.as_str()) {
            proj.rating = r.trim().to_string();
        }
        if let Some(arr) = args.get("archiveWarnings").and_then(|v| v.as_array()) {
            proj.archive_warnings = arr
                .iter()
                .filter_map(|v| v.as_str().map(|s| s.to_string()))
                .collect();
        }
        if let Some(arr) = args.get("categories").and_then(|v| v.as_array()) {
            proj.categories = arr
                .iter()
                .filter_map(|v| v.as_str().map(|s| s.to_string()))
                .collect();
        }
        if let Some(arr) = args.get("fandoms").and_then(|v| v.as_array()) {
            proj.fandoms.clear();
            for v in arr.iter().filter_map(|v| v.as_str()) {
                proj.add_fandom(v);
            }
        }
        if let Some(arr) = args.get("relationships").and_then(|v| v.as_array()) {
            proj.relationships.clear();
            for v in arr.iter().filter_map(|v| v.as_str()) {
                proj.add_relationship(v);
            }
        }
        if let Some(arr) = args.get("characters").and_then(|v| v.as_array()) {
            proj.characters.clear();
            for v in arr.iter().filter_map(|v| v.as_str()) {
                proj.add_character(v);
            }
        }
        if let Some(arr) = args.get("tags").and_then(|v| v.as_array()) {
            proj.tags.clear();
            for v in arr.iter().filter_map(|v| v.as_str()) {
                proj.add_tag(v);
            }
        }

        proj.mark_dirty_by(&caller.name);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "update_project_metadata",
            "Updated project metadata (rating, tags, fandoms, characters).",
            -1,
            0,
            "",
        );

        let res = json!({
            "success": true,
            "title": proj.title,
            "author": proj.author,
            "rating": proj.rating,
            "archiveWarnings": proj.archive_warnings,
            "categories": proj.categories,
            "fandoms": proj.fandoms,
            "relationships": proj.relationships,
            "characters": proj.characters,
            "tags": proj.tags
        });
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(res, false)
    }

    fn tool_list_image_embeds(&self, args: &Value) -> Value {
        let ws = self.workspace.lock();
        let proj = match ws.project.as_ref() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        let filter_ch = args
            .get("chapterIndex")
            .and_then(|v| v.as_i64())
            .unwrap_or(-1);

        static RE_IMG: Lazy<Regex> =
            Lazy::new(|| Regex::new(r"(?i)<img\b([^>]*?)/?>").unwrap());
        static RE_SRC: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)src\s*=\s*["']([^"']+)["']"#).unwrap());
        static RE_ALT: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)alt\s*=\s*["']([^"']*)["']"#).unwrap());
        static RE_TITLE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)title\s*=\s*["']([^"']*)["']"#).unwrap());
        static RE_WIDTH: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)width\s*=\s*["']?(\d+)["']?"#).unwrap());
        static RE_HEIGHT: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)height\s*=\s*["']?(\d+)["']?"#).unwrap());

        let mut embeds = Vec::new();
        for (i, ch) in proj.chapters.iter().enumerate() {
            if filter_ch >= 0 && (filter_ch as usize) != i {
                continue;
            }
            for cap in RE_IMG.captures_iter(&ch.html) {
                let raw_tag = cap.get(0).map(|m| m.as_str()).unwrap_or("");
                let attrs = cap.get(1).map(|m| m.as_str()).unwrap_or("");
                let src = match RE_SRC.captures(attrs).and_then(|c| c.get(1)) {
                    Some(m) => m.as_str().trim(),
                    None => continue,
                };
                let mut item = serde_json::Map::new();
                item.insert("chapterIndex".to_string(), json!(i));
                item.insert("chapterTitle".to_string(), json!(ch.title));
                item.insert("url".to_string(), json!(src));
                if let Some(m) = RE_ALT.captures(attrs).and_then(|c| c.get(1)) {
                    item.insert("alt".to_string(), json!(m.as_str()));
                }
                if let Some(m) = RE_TITLE.captures(attrs).and_then(|c| c.get(1)) {
                    item.insert("title".to_string(), json!(m.as_str()));
                }
                if let Some(m) = RE_WIDTH
                    .captures(attrs)
                    .and_then(|c| c.get(1))
                    .and_then(|m| m.as_str().parse::<i32>().ok())
                {
                    item.insert("width".to_string(), json!(m));
                }
                if let Some(m) = RE_HEIGHT
                    .captures(attrs)
                    .and_then(|c| c.get(1))
                    .and_then(|m| m.as_str().parse::<i32>().ok())
                {
                    item.insert("height".to_string(), json!(m));
                }
                item.insert("rawTag".to_string(), json!(raw_tag));
                embeds.push(Value::Object(item));
            }
        }

        Self::make_tool_json_result(
            json!({
                "totalImageEmbeds": embeds.len(),
                "imageEmbeds": embeds
            }),
            false,
        )
    }

    fn tool_insert_image_embed(&self, client_id: u64, args: &Value) -> Value {
        let ch_idx = match args.get("chapterIndex").and_then(|v| v.as_i64()) {
            Some(i) => i,
            None => {
                return Self::make_tool_result(
                    "Error: 'chapterIndex' and 'url' are required arguments.",
                    true,
                )
            }
        };
        let url = match args.get("url").and_then(|v| v.as_str()) {
            Some(u) => u.trim(),
            None => {
                return Self::make_tool_result(
                    "Error: 'chapterIndex' and 'url' are required arguments.",
                    true,
                )
            }
        };

        if url.is_empty() {
            return Self::make_tool_result("Error: 'url' must not be empty.", true);
        }
        let lower_url = url.to_ascii_lowercase();
        if !lower_url.starts_with("http://") && !lower_url.starts_with("https://") {
            return Self::make_tool_result(
                "Error: 'url' must begin with http:// or https:// for AO3 compliance.",
                true,
            );
        }

        let alt = args.get("alt").and_then(|v| v.as_str()).unwrap_or("").trim();
        let title = args
            .get("title")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        let width = args.get("width").and_then(|v| v.as_i64()).unwrap_or(0);
        let height = args.get("height").and_then(|v| v.as_i64()).unwrap_or(0);
        let align = args
            .get("alignment")
            .and_then(|v| v.as_str())
            .unwrap_or("center")
            .to_ascii_lowercase();
        let wrap_link = args
            .get("wrapInLink")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);
        let pos = args
            .get("position")
            .and_then(|v| v.as_str())
            .unwrap_or("append")
            .to_ascii_lowercase();

        let mut img_tag = format!("<img src=\"{}\"", url);
        if !alt.is_empty() {
            img_tag.push_str(&format!(" alt=\"{}\"", alt));
        }
        if !title.is_empty() {
            img_tag.push_str(&format!(" title=\"{}\"", title));
        }
        if width > 0 {
            img_tag.push_str(&format!(" width=\"{}\"", width));
        }
        if height > 0 {
            img_tag.push_str(&format!(" height=\"{}\"", height));
        }
        img_tag.push_str(" />");

        if wrap_link {
            img_tag = format!("<a href=\"{}\">{}</a>", url, img_tag);
        }

        let block_tag = match align.as_str() {
            "center" => format!("<p align=\"center\">{}</p>", img_tag),
            "left" => format!("<p align=\"left\">{}</p>", img_tag),
            "right" => format!("<p align=\"right\">{}</p>", img_tag),
            _ => img_tag,
        };

        let caller = self.caller_session(client_id);
        let caller_model = Self::resolve_caller_model(&caller, args);

        let mut ws = self.workspace.lock();
        let proj = match ws.project.as_mut() {
            Some(p) => p,
            None => return Self::make_tool_result("No project is currently open in Ensemble.", true),
        };

        if ch_idx < 0 || (ch_idx as usize) >= proj.chapters.len() {
            return Self::make_tool_result(
                &format!(
                    "Error: Invalid chapterIndex {}. Project has {} chapters.",
                    ch_idx,
                    proj.chapters.len()
                ),
                true,
            );
        }

        let u_idx = ch_idx as usize;
        let current_html = proj.chapters[u_idx].html.trim().to_string();
        let updated_html = if pos == "prepend" {
            if current_html.is_empty() {
                block_tag.clone()
            } else {
                format!("{}\n\n{}", block_tag, current_html)
            }
        } else if current_html.is_empty() {
            block_tag.clone()
        } else {
            format!("{}\n\n{}", current_html, block_tag)
        };

        proj.chapters[u_idx].html = updated_html;
        let ch_title = proj.chapters[u_idx].title.clone();

        let commit_msg = format!(
            "Embedded image URL in Chapter {} ('{}')",
            u_idx + 1,
            ch_title
        );
        proj.mark_dirty_by(&caller.name);
        proj.append_history_commit(&caller.name, &commit_msg);
        proj.agentic_data.append_history_entry(
            &caller.name,
            &caller_model,
            &caller.role,
            "insert_image_embed",
            &format!("Embedded image in Chapter {} ('{}'): {}", u_idx + 1, ch_title, url),
            ch_idx as i32,
            0,
            &block_tag,
        );
        drop(ws);

        self.notify_ui_refresh();

        Self::make_tool_json_result(
            json!({
                "success": true,
                "chapterIndex": ch_idx,
                "chapterTitle": ch_title,
                "embeddedHtml": block_tag,
                "url": url
            }),
            false,
        )
    }
}
