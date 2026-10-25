use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use ensemble_core::lore_graph::GRPH_MAGIC_BYTES;
use ensemble_core::serializer::ProjectSerializer;
use ensemble_mcp::{
    sort_and_deduplicate_events, AgentActivityType, McpAgentInstaller, McpHandler, McpServer,
};
use futures_util::{SinkExt, StreamExt};
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use serde_json::{json, Value};
use std::fs;
use std::path::Path;
use std::time::Duration;
use tempfile::TempDir;
use tokio_tungstenite::tungstenite::Message;

static TEST_SERIAL_LOCK: Lazy<Mutex<()>> = Lazy::new(|| Mutex::new(()));

fn send_ws_request(url: &str, request: Value) -> Value {
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .expect("tokio runtime");

    rt.block_on(async move {
        let (mut ws, _) = tokio::time::timeout(
            Duration::from_secs(3),
            tokio_tungstenite::connect_async(url),
        )
        .await
        .expect("connect timeout")
        .expect("ws connect");

        ws.send(Message::Text(request.to_string().into()))
            .await
            .expect("ws send");

        let reply = tokio::time::timeout(Duration::from_secs(3), ws.next())
            .await
            .expect("recv timeout")
            .expect("stream item")
            .expect("ws message");

        let _ = ws.close(None).await;

        match reply {
            Message::Text(t) => serde_json::from_str(&t).unwrap_or_else(|_| json!({})),
            _ => json!({}),
        }
    })
}

fn call_tool(handler: &McpHandler, name: &str, args: Value) -> (Value, bool) {
    let req = json!({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": name,
            "arguments": args
        }
    });
    let raw_resp = handler
        .handle_message(1, &req.to_string())
        .expect("tool response");
    let resp: Value = serde_json::from_str(&raw_resp).expect("valid json-rpc response");
    let result = resp.get("result").cloned().unwrap_or_else(|| json!({}));
    let is_err = result
        .get("isError")
        .and_then(|v| v.as_bool())
        .unwrap_or(false);
    let text = result
        .get("content")
        .and_then(|c| c.as_array())
        .and_then(|arr| arr.first())
        .and_then(|first| first.get("text"))
        .and_then(|t| t.as_str())
        .unwrap_or("");
    let parsed_data = serde_json::from_str::<Value>(text)
        .unwrap_or_else(|_| json!({ "_rawText": text }));
    (parsed_data, is_err)
}

#[test]
fn test_server_starts_and_stops() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server = McpServer::new();
    assert!(!server.is_running());

    assert!(server.start(0));
    assert!(server.is_running());
    assert!(server.actual_port() > 0);
    assert!(server.url().starts_with("ws://127.0.0.1:"));

    server.stop();
    assert!(!server.is_running());
    assert_eq!(server.actual_port(), 0);
}

#[test]
fn test_server_writes_and_removes_discovery_file() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let temp_dir = TempDir::new().unwrap();
    std::env::set_var("ENSEMBLE_MCP_CONFIG_DIR", temp_dir.path());

    let server = McpServer::new();
    assert!(server.start(0));

    let disc_path = McpServer::discovery_file_path();
    assert!(Path::new(&disc_path).exists());

    let info = McpServer::read_discovery_file();
    assert_eq!(
        info.get("name").and_then(|v| v.as_str()),
        Some("ensemble-ao3-typewriter")
    );
    assert_eq!(
        info.get("protocol").and_then(|v| v.as_str()),
        Some("mcp")
    );
    assert_eq!(
        info.get("transport").and_then(|v| v.as_str()),
        Some("websocket")
    );
    assert_eq!(
        info.get("port").and_then(|v| v.as_u64()),
        Some(server.actual_port() as u64)
    );
    assert_eq!(
        info.get("url").and_then(|v| v.as_str()),
        Some(server.url().as_str())
    );
    assert_eq!(
        info.get("pid").and_then(|v| v.as_u64()),
        Some(std::process::id() as u64)
    );

    server.stop();
    assert!(!Path::new(&disc_path).exists());
    std::env::remove_var("ENSEMBLE_MCP_CONFIG_DIR");
}

#[test]
fn test_server_port_fallback() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server1 = McpServer::new();
    assert!(server1.start(26350));
    let port1 = server1.actual_port();

    let server2 = McpServer::new();
    assert!(server2.start(port1));
    let port2 = server2.actual_port();

    assert_ne!(port1, port2);
    assert!(port2 > 0);
}

#[test]
fn test_server_loopback_test_when_running_and_stopped() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server = McpServer::new();
    let stopped_res = server.test_loopback();
    assert!(!stopped_res.success);
    assert!(!stopped_res.server_running);
    assert!(!stopped_res.loopback_reachable);

    assert!(server.start(0));
    let running_res = server.test_loopback();
    assert!(running_res.success);
    assert!(running_res.server_running);
    assert!(running_res.loopback_reachable);
    assert_eq!(running_res.port, server.actual_port());
    assert!(running_res.latency_ms >= 0);
}

#[test]
fn test_initialize_and_ping_and_unknown_method_over_websocket() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server = McpServer::new();
    let _handler = McpHandler::new(&server);
    assert!(server.start(0));

    let init_resp = send_ws_request(
        &server.url(),
        json!({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "clientInfo": { "name": "test-client", "version": "1.0" }
            }
        }),
    );
    assert_eq!(init_resp.get("id").and_then(|v| v.as_i64()), Some(1));
    let result = init_resp.get("result").expect("result");
    assert_eq!(
        result.get("protocolVersion").and_then(|v| v.as_str()),
        Some("2024-11-05")
    );
    assert_eq!(
        result
            .get("serverInfo")
            .and_then(|s| s.get("name"))
            .and_then(|v| v.as_str()),
        Some("ensemble-ao3-typewriter")
    );

    let ping_resp = send_ws_request(
        &server.url(),
        json!({
            "jsonrpc": "2.0",
            "id": 2,
            "method": "ping"
        }),
    );
    assert_eq!(ping_resp.get("id").and_then(|v| v.as_i64()), Some(2));
    assert!(ping_resp.get("result").unwrap().is_object());

    let unknown_resp = send_ws_request(
        &server.url(),
        json!({
            "jsonrpc": "2.0",
            "id": 3,
            "method": "nonexistent/method"
        }),
    );
    assert_eq!(
        unknown_resp
            .get("error")
            .and_then(|e| e.get("code"))
            .and_then(|v| v.as_i64()),
        Some(-32601)
    );

    let tools_resp = send_ws_request(
        &server.url(),
        json!({
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/list"
        }),
    );
    let tools = tools_resp
        .get("result")
        .and_then(|r| r.get("tools"))
        .and_then(|t| t.as_array())
        .expect("tools array");
    assert!(tools.len() >= 34);
    let has_tool = |name: &str| {
        tools
            .iter()
            .any(|t| t.get("name").and_then(|v| v.as_str()) == Some(name))
    };
    assert!(has_tool("get_project_summary"));
    assert!(has_tool("list_chapters"));
    assert!(has_tool("get_chapter"));
    assert!(has_tool("update_chapter_html"));
    assert!(has_tool("add_chapter"));
    assert!(has_tool("delete_chapter"));
    assert!(has_tool("reorder_chapters"));
    assert!(has_tool("get_workskin_css"));
    assert!(has_tool("set_workskin_css"));
    assert!(has_tool("get_problems"));
    assert!(has_tool("load_project"));
    assert!(has_tool("save_project"));
    assert!(has_tool("get_project_history"));
    assert!(has_tool("add_history_commit"));
    assert!(has_tool("get_snapshot_diff"));
    assert!(has_tool("test_firewall_connection"));
    assert!(has_tool("reload_project"));
    assert!(has_tool("get_preview_screenshot"));
}

#[test]
fn test_project_and_chapter_tools_lifecycle() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server = McpServer::new();
    let handler = McpHandler::new(&server);

    // Without project open -> returns isError: true
    let (no_proj_data, no_proj_err) = call_tool(&handler, "get_project_summary", json!({}));
    assert!(no_proj_err);
    assert!(no_proj_data
        .get("_rawText")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .contains("No project is currently open"));

    handler.open_new_project();

    let (sum_data, sum_err) = call_tool(&handler, "get_project_summary", json!({}));
    assert!(!sum_err);
    assert_eq!(
        sum_data.get("title").and_then(|v| v.as_str()),
        Some("Untitled Work")
    );
    assert_eq!(sum_data.get("chapterCount").and_then(|v| v.as_u64()), Some(1));

    // Update chapter HTML with script sanitization
    let (upd_data, upd_err) = call_tool(
        &handler,
        "update_chapter_html",
        json!({
            "chapterIndex": 0,
            "title": "Prologue: The Beginning",
            "html": "<p>The stars shone brightly.</p><script>evil()</script>",
            "summary": "In which our story begins.",
            "sanitize": true
        }),
    );
    assert!(!upd_err);
    assert_eq!(upd_data.get("success").and_then(|v| v.as_bool()), Some(true));
    assert_eq!(
        upd_data.get("title").and_then(|v| v.as_str()),
        Some("Prologue: The Beginning")
    );
    assert_eq!(upd_data.get("wordCount").and_then(|v| v.as_i64()), Some(4));

    let (ch_data, ch_err) = call_tool(&handler, "get_chapter", json!({ "chapterIndex": 0 }));
    assert!(!ch_err);
    let ch_html = ch_data.get("html").and_then(|v| v.as_str()).unwrap_or("");
    assert!(ch_html.contains("stars shone brightly"));
    assert!(!ch_html.contains("<script"));

    // Add second chapter
    let (add_data, add_err) = call_tool(
        &handler,
        "add_chapter",
        json!({
            "title": "Chapter 2: The Journey",
            "html": "<p>They walked for miles across the valley.</p>"
        }),
    );
    assert!(!add_err);
    assert_eq!(add_data.get("chapterIndex").and_then(|v| v.as_u64()), Some(1));
    assert_eq!(add_data.get("chapterCount").and_then(|v| v.as_u64()), Some(2));

    // Reorder chapters (swap 0 and 1)
    let (reord_data, reord_err) = call_tool(
        &handler,
        "reorder_chapters",
        json!({ "fromIndex": 0, "toIndex": 1 }),
    );
    assert!(!reord_err);
    assert_eq!(reord_data.get("success").and_then(|v| v.as_bool()), Some(true));

    let (ch0_after, _) = call_tool(&handler, "get_chapter", json!({ "chapterIndex": 0 }));
    assert_eq!(
        ch0_after.get("title").and_then(|v| v.as_str()),
        Some("Chapter 2: The Journey")
    );

    // Delete chapter 1
    let (del_data, del_err) = call_tool(&handler, "delete_chapter", json!({ "chapterIndex": 1 }));
    assert!(!del_err);
    assert_eq!(
        del_data.get("remainingChapterCount").and_then(|v| v.as_u64()),
        Some(1)
    );

    // Cannot delete last remaining chapter
    let (_, del_last_err) = call_tool(&handler, "delete_chapter", json!({ "chapterIndex": 0 }));
    assert!(del_last_err);

    // Workskin CSS get and set
    let (ws_set, ws_err) = call_tool(
        &handler,
        "set_workskin_css",
        json!({ "css": "#workskin .chat { color: #ff5500; font-weight: bold; }" }),
    );
    assert!(!ws_err);
    assert_eq!(ws_set.get("success").and_then(|v| v.as_bool()), Some(true));

    let (ws_get, _) = call_tool(&handler, "get_workskin_css", json!({}));
    assert!(ws_get
        .get("css")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .contains("#workskin .chat"));

    // Save and load project roundtrip + history + snapshot diff
    let temp_dir = TempDir::new().unwrap();
    let save_path = temp_dir.path().join("mcp_test.ao3proj");
    let save_path_str = save_path.to_string_lossy().to_string();

    let (save_data, save_err) = call_tool(
        &handler,
        "save_project",
        json!({
            "filePath": save_path_str,
            "commitMessage": "Initial MCP save",
            "author": "Claude Agent"
        }),
    );
    assert!(!save_err);
    assert_eq!(save_data.get("success").and_then(|v| v.as_bool()), Some(true));
    assert!(save_path.exists());

    let (hist_data, hist_err) = call_tool(&handler, "get_project_history", json!({}));
    assert!(!hist_err);
    let commits = hist_data
        .get("commits")
        .and_then(|v| v.as_array())
        .expect("commits array");
    assert!(!commits.is_empty());
    let first_commit_id = commits[0]
        .get("id")
        .and_then(|v| v.as_str())
        .unwrap()
        .to_string();

    let (diff_data, diff_err) = call_tool(
        &handler,
        "get_snapshot_diff",
        json!({ "commitId": first_commit_id }),
    );
    assert!(!diff_err);
    assert!(diff_data.get("chapterDiffs").unwrap().is_array());
    assert!(diff_data.get("summary").unwrap().is_object());

    let (_, bad_diff_err) = call_tool(
        &handler,
        "get_snapshot_diff",
        json!({ "commitId": "nonexistent-commit-999" }),
    );
    assert!(bad_diff_err);

    // Reload project from disk
    let (reload_data, reload_err) =
        call_tool(&handler, "reload_project", json!({ "fromDisk": true }));
    assert!(!reload_err);
    assert_eq!(
        reload_data.get("reloadedFromDisk").and_then(|v| v.as_bool()),
        Some(true)
    );

    // Preview screenshot returns multi-item content [text, image]
    let sc_req = json!({
        "jsonrpc": "2.0",
        "id": 99,
        "method": "tools/call",
        "params": {
            "name": "get_preview_screenshot",
            "arguments": { "chapterIndex": 0 }
        }
    });
    let sc_raw = handler.handle_message(1, &sc_req.to_string()).unwrap();
    let sc_resp: Value = serde_json::from_str(&sc_raw).unwrap();
    let sc_content = sc_resp["result"]["content"].as_array().unwrap();
    assert_eq!(sc_content.len(), 2);
    assert_eq!(sc_content[0]["type"].as_str(), Some("text"));
    assert_eq!(sc_content[1]["type"].as_str(), Some("image"));
    assert_eq!(sc_content[1]["mimeType"].as_str(), Some("image/png"));
    let png_bytes = BASE64
        .decode(sc_content[1]["data"].as_str().unwrap())
        .unwrap();
    assert!(png_bytes.starts_with(b"\x89PNG\r\n\x1a\n"));
}

#[test]
fn test_agentic_workspace_and_activity_monitor() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server = McpServer::new();
    let handler = McpHandler::new(&server);
    handler.open_new_project();

    let (reg_data, reg_err) = call_tool(
        &handler,
        "register_agent_presence",
        json!({
            "name": "Claude 3.7 Sonnet",
            "model": "claude-3-7-sonnet-20250219",
            "role": "co-author",
            "status": "Drafting Chapter 1"
        }),
    );
    assert!(!reg_err);
    assert_eq!(reg_data["success"].as_bool(), Some(true));

    let (pres_data, pres_err) = call_tool(&handler, "get_agent_presence", json!({}));
    assert!(!pres_err);
    assert_eq!(pres_data["activeAgentCount"].as_u64(), Some(1));

    let (w_data, w_err) = call_tool(
        &handler,
        "write_agentic_file",
        json!({
            "file": "instructions",
            "content": "Keep all dialogue in third-person limited POV."
        }),
    );
    assert!(!w_err);
    assert_eq!(w_data["success"].as_bool(), Some(true));

    let (r_data, r_err) = call_tool(
        &handler,
        "read_agentic_file",
        json!({ "file": "instructions" }),
    );
    assert!(!r_err);
    assert_eq!(
        r_data["content"].as_str(),
        Some("Keep all dialogue in third-person limited POV.")
    );

    let (th_data, th_err) = call_tool(
        &handler,
        "post_agent_thought",
        json!({
            "thought": "Checking pacing in scene 2.",
            "category": "pacing"
        }),
    );
    assert!(!th_err);
    assert_eq!(th_data["recorded"].as_bool(), Some(true));

    let events = handler.activity_events();
    assert!(events.len() >= 3);
    assert!(events
        .iter()
        .any(|e| e.event_type == AgentActivityType::Thought
            && e.summary == "Checking pacing in scene 2."));

    // Verify chronological sorting & deduplication helper
    let mut dup_events = events.clone();
    dup_events.push(events[0].clone());
    sort_and_deduplicate_events(&mut dup_events);
    assert_eq!(dup_events.len(), events.len());
}

#[test]
fn test_lore_graph_and_lore_data_and_metadata_and_image_embeds() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let server = McpServer::new();
    let handler = McpHandler::new(&server);
    handler.open_new_project();

    // 1. get_lore_graph
    let (lg_data, lg_err) = call_tool(&handler, "get_lore_graph", json!({}));
    assert!(!lg_err);
    assert_eq!(lg_data["magicHeaderValid"].as_bool(), Some(true));
    assert_eq!(
        lg_data["magicHeaderHex"].as_str(),
        Some("89 47 52 50 48 01 0D 0A")
    );
    assert!(lg_data["grammarSpecification"]
        .as_str()
        .unwrap_or("")
        .contains("89 47 52 50 48 01 0D 0A"));

    // 2. add_lore_node, connect_lore_nodes, move_lore_node
    let (_, n1_err) = call_tool(
        &handler,
        "add_lore_node",
        json!({
            "id": "hero",
            "label": "Aria Vance",
            "type": "character",
            "x": 100,
            "y": 150,
            "shape": "rounded",
            "color": "#3b82f6"
        }),
    );
    assert!(!n1_err);

    let (_, n2_err) = call_tool(
        &handler,
        "add_lore_node",
        json!({
            "id": "sanctuary",
            "label": "The Astral Sanctuary",
            "type": "location",
            "x": 420,
            "y": 150,
            "shape": "hexagon",
            "color": "#10b981"
        }),
    );
    assert!(!n2_err);

    let (_, conn_err) = call_tool(
        &handler,
        "connect_lore_nodes",
        json!({
            "fromNodeId": "hero",
            "toNodeId": "sanctuary",
            "arrow": "->",
            "label": "discovers",
            "style": "dashed"
        }),
    );
    assert!(!conn_err);

    let (mv_data, mv_err) = call_tool(
        &handler,
        "move_lore_node",
        json!({
            "nodeId": "hero",
            "x": 250.5,
            "y": 310.0
        }),
    );
    assert!(!mv_err);
    assert_eq!(mv_data["newX"].as_f64(), Some(250.5));

    // Semantic validation rejects invalid node type or missing target node
    let (_, bad_type_err) = call_tool(
        &handler,
        "add_lore_node",
        json!({ "id": "bad", "label": "Bad", "type": "spaceship" }),
    );
    assert!(bad_type_err);

    let (_, bad_conn_err) = call_tool(
        &handler,
        "connect_lore_nodes",
        json!({ "fromNodeId": "hero", "toNodeId": "missing_node" }),
    );
    assert!(bad_conn_err);

    // 3. manage_lore_data CRUD
    let (_, ent_err) = call_tool(
        &handler,
        "manage_lore_data",
        json!({
            "action": "add_entity",
            "id": "aria",
            "name": "Aria Vance",
            "category": "character",
            "description": "Star-navigator of the Seventh Fleet.",
            "tags": ["protagonist", "navigator"]
        }),
    );
    assert!(!ent_err);

    // Rejects unknown fields in manage_lore_data
    let (bad_field_data, bad_field_err) = call_tool(
        &handler,
        "manage_lore_data",
        json!({
            "action": "add_history",
            "title": "The Shattering",
            "event": "Wrong field name"
        }),
    );
    assert!(bad_field_err);
    assert!(bad_field_data["_rawText"]
        .as_str()
        .unwrap_or("")
        .contains("Unrecognized field(s) 'event'"));

    // Reset default graph 'data' via remove_graph
    let (rm_graph_data, rm_graph_err) = call_tool(
        &handler,
        "manage_lore_data",
        json!({ "action": "remove_graph", "name": "data" }),
    );
    assert!(!rm_graph_err);
    assert_eq!(rm_graph_data["isDefaultGraph"].as_bool(), Some(true));
    assert_eq!(rm_graph_data["resetToEmpty"].as_bool(), Some(true));

    // 4. update_project_metadata
    let (meta_data, meta_err) = call_tool(
        &handler,
        "update_project_metadata",
        json!({
            "title": "Echoes of the Astral Sea",
            "author": "StarlightScribe",
            "rating": "Teen And Up Audiences",
            "fandoms": ["Original Work", "Astral Chronicles"]
        }),
    );
    assert!(!meta_err);
    assert_eq!(
        meta_data["title"].as_str(),
        Some("Echoes of the Astral Sea")
    );

    // 5. insert_image_embed & list_image_embeds
    let (img_ins, img_err) = call_tool(
        &handler,
        "insert_image_embed",
        json!({
            "chapterIndex": 0,
            "url": "https://images.example.com/art/scene1.png",
            "alt": "Concept art of scene 1",
            "title": "Scene 1 Illustration",
            "width": 800,
            "height": 600,
            "alignment": "center",
            "wrapInLink": true
        }),
    );
    assert!(!img_err);
    assert_eq!(img_ins["success"].as_bool(), Some(true));

    let (img_list, list_err) = call_tool(&handler, "list_image_embeds", json!({}));
    assert!(!list_err);
    assert_eq!(img_list["totalImageEmbeds"].as_u64(), Some(1));
    assert_eq!(
        img_list["imageEmbeds"][0]["url"].as_str(),
        Some("https://images.example.com/art/scene1.png")
    );

    // Verify binary .grph header persists inside .ao3proj archive
    let temp_dir = TempDir::new().unwrap();
    let archive_path = temp_dir.path().join("lore_test.ao3proj");
    assert!(handler.save_project_file(&archive_path.to_string_lossy()));
    let mut loaded = ensemble_core::project::Ao3Project::new();
    ProjectSerializer::load(&archive_path, &mut loaded).expect("load archive");
    let raw_grph = loaded.lore.graph("data").unwrap().to_grph_bytes();
    assert!(raw_grph.starts_with(&GRPH_MAGIC_BYTES));
}

#[test]
fn test_agent_installer_generates_manual_and_preserves_existing_configs() {
    let _guard = TEST_SERIAL_LOCK.lock();
    let manual = McpAgentInstaller::generate_manual_markdown("/usr/local/bin/ensemble", 26303);
    assert!(manual.contains("Ensemble MCP Server"));
    assert!(manual.contains("ensemble-ao3-typewriter"));
    assert!(manual.contains("/usr/local/bin/ensemble"));
    assert!(manual.contains("--mcp-bridge"));
    assert!(manual.contains("get_project_summary"));
    assert!(manual.contains("get_lore_graph"));
    assert!(manual.contains("ensemble://project/metadata.xml"));

    let temp_home = TempDir::new().unwrap();
    let old_home = std::env::var("HOME").ok();
    std::env::set_var("HOME", temp_home.path());

    // Pre-populate .claude.json with an existing server
    let claude_code_path = temp_home.path().join(".claude.json");
    let existing = json!({
        "customUserSetting": true,
        "mcpServers": {
            "other-existing-server": {
                "command": "/usr/bin/npx",
                "args": ["-y", "@modelcontextprotocol/server-filesystem"]
            }
        }
    });
    fs::write(
        &claude_code_path,
        serde_json::to_string_pretty(&existing).unwrap(),
    )
    .unwrap();

    let res = McpAgentInstaller::install_all("/opt/ensemble/bin/ensemble", 26310);
    assert!(res.success);
    assert!(res.updated_configs.len() >= 3);
    assert!(res.installed_manuals.len() >= 2);

    let updated_raw = fs::read_to_string(&claude_code_path).unwrap();
    let updated_json: Value = serde_json::from_str(&updated_raw).unwrap();
    assert_eq!(updated_json["customUserSetting"].as_bool(), Some(true));
    assert!(updated_json["mcpServers"]["other-existing-server"].is_object());
    assert_eq!(
        updated_json["mcpServers"]["ensemble-ao3-typewriter"]["command"].as_str(),
        Some("/opt/ensemble/bin/ensemble")
    );
    assert_eq!(
        updated_json["mcpServers"]["ensemble-ao3-typewriter"]["env"]["ENSEMBLE_MCP_PORT"].as_str(),
        Some("26310")
    );

    let agy_skill = temp_home
        .path()
        .join(".gemini/antigravity/skills/ensemble-mcp/SKILL.md");
    assert!(agy_skill.exists());
    let skill_text = fs::read_to_string(&agy_skill).unwrap();
    assert!(skill_text.starts_with("---\nname: ensemble-mcp"));

    if let Some(h) = old_home {
        std::env::set_var("HOME", h);
    }
}
