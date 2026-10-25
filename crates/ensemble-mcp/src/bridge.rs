use crate::server::{McpServer, DEFAULT_MCP_PORT};
use crate::tools::get_mcp_all_tool_definitions;
use futures_util::{SinkExt, StreamExt};
use serde_json::{json, Value};
use std::io::{self, BufRead, Write};
use std::path::Path;
use std::time::Duration;
use tokio_tungstenite::tungstenite::Message;

pub struct McpBridge;

impl McpBridge {
    pub fn resolve_server_url() -> String {
        let info = McpServer::read_discovery_file();
        if let Some(url) = info.get("url").and_then(|v| v.as_str()) {
            if !url.trim().is_empty() {
                return url.trim().to_string();
            }
        }
        let port = info
            .get("port")
            .and_then(|v| v.as_u64())
            .filter(|&p| p > 0 && p <= 65535)
            .map(|p| p as u16)
            .unwrap_or(DEFAULT_MCP_PORT);
        format!("ws://127.0.0.1:{}", port)
    }

    pub fn run_status_check() -> i32 {
        let disc_path = McpServer::discovery_file_path();
        let disc_exists = Path::new(&disc_path).exists();
        let info = McpServer::read_discovery_file();
        let ws_url = Self::resolve_server_url();

        println!("=== Ensemble MCP Diagnostic Status ===");
        println!("Discovery File : {}", disc_path);
        println!(
            "File Exists    : {}",
            if disc_exists { "YES" } else { "NO" }
        );
        if disc_exists {
            println!(
                "Recorded PID   : {}",
                info.get("pid").and_then(|v| v.as_u64()).unwrap_or(0)
            );
            println!(
                "Recorded Port  : {}",
                info.get("port").and_then(|v| v.as_u64()).unwrap_or(0)
            );
            println!(
                "Recorded URL   : {}",
                info.get("url").and_then(|v| v.as_str()).unwrap_or("")
            );
            println!(
                "Started At     : {}",
                info.get("startedAt").and_then(|v| v.as_str()).unwrap_or("")
            );
        }
        println!("Target WS URL  : {}", ws_url);

        let rt = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(rt) => rt,
            Err(e) => {
                println!("Status         : ERROR (Failed to initialize async runtime: {})", e);
                return 1;
            }
        };

        rt.block_on(async move {
            let connect_fut = tokio_tungstenite::connect_async(&ws_url);
            match tokio::time::timeout(Duration::from_millis(3000), connect_fut).await {
                Ok(Ok((mut ws, _))) => {
                    let ping = json!({
                        "jsonrpc": "2.0",
                        "id": "diag-ping-1",
                        "method": "ping"
                    });
                    if ws
                        .send(Message::Text(ping.to_string().into()))
                        .await
                        .is_ok()
                    {
                        if let Ok(Some(Ok(Message::Text(reply)))) =
                            tokio::time::timeout(Duration::from_millis(2000), ws.next()).await
                        {
                            println!("WebSocket Ping : PASS ({})", reply.trim());
                            println!("Overall Status : HEALTHY (Ensemble MCP Server is online and responsive)");
                            let _ = ws.close(None).await;
                            return 0;
                        }
                    }
                    println!("WebSocket Ping : TIMEOUT");
                    1
                }
                Ok(Err(e)) => {
                    println!("WebSocket Conn : FAIL ({})", e);
                    println!("Overall Status : OFFLINE (Start Ensemble and ensure MCP Server is enabled in Settings)");
                    1
                }
                Err(_) => {
                    println!("WebSocket Conn : TIMEOUT (3000 ms)");
                    1
                }
            }
        })
    }

    pub fn run_bridge() -> i32 {
        let rt = match tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            Ok(rt) => rt,
            Err(_) => return 1,
        };

        rt.block_on(async move {
            let stdin = io::stdin();
            let mut stdout = io::stdout();

            for line_res in stdin.lock().lines() {
                let line = match line_res {
                    Ok(l) => l,
                    Err(_) => break,
                };
                let trimmed = line.trim();
                if trimmed.is_empty() {
                    continue;
                }

                let ws_url = Self::resolve_server_url();
                let connected = tokio::time::timeout(
                    Duration::from_millis(1500),
                    tokio_tungstenite::connect_async(&ws_url),
                )
                .await;

                if let Ok(Ok((mut ws, _))) = connected {
                    if ws
                        .send(Message::Text(trimmed.to_string().into()))
                        .await
                        .is_ok()
                    {
                        // Check if notification (no id)
                        let parsed: Value = serde_json::from_str(trimmed).unwrap_or(Value::Null);
                        let is_notification = parsed.get("id").is_none()
                            && parsed
                                .get("method")
                                .and_then(|m| m.as_str())
                                .map(|m| {
                                    m == "notifications/initialized"
                                        || m == "initialized"
                                        || m.starts_with("notifications/")
                                })
                                .unwrap_or(false);

                        if !is_notification {
                            if let Ok(Some(Ok(Message::Text(resp)))) =
                                tokio::time::timeout(Duration::from_secs(15), ws.next()).await
                            {
                                let clean = resp.replace('\n', " ").replace('\r', "");
                                let _ = writeln!(stdout, "{}", clean.trim());
                                let _ = stdout.flush();
                            }
                        }
                        let _ = ws.close(None).await;
                        continue;
                    }
                }

                // Offline fallback handling so handshake & tool listing succeed even if GUI is closed
                if let Some(reply) = Self::handle_offline_message(trimmed) {
                    let clean = reply.replace('\n', " ").replace('\r', "");
                    let _ = writeln!(stdout, "{}", clean.trim());
                    let _ = stdout.flush();
                }
            }

            0
        })
    }

    pub fn handle_offline_message(raw_line: &str) -> Option<String> {
        let req: Value = match serde_json::from_str(raw_line) {
            Ok(v) => v,
            Err(_) => return None,
        };
        let method = req.get("method").and_then(|v| v.as_str()).unwrap_or("");
        if method == "notifications/initialized"
            || method == "initialized"
            || method.starts_with("notifications/")
        {
            return None;
        }
        let id = req.get("id").cloned().unwrap_or(Value::Null);
        let params = req.get("params").cloned().unwrap_or_else(|| json!({}));

        let resp = match method {
            "initialize" => {
                let ver = params
                    .get("protocolVersion")
                    .and_then(|v| v.as_str())
                    .filter(|s| !s.is_empty())
                    .unwrap_or("2024-11-05");
                json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "protocolVersion": ver,
                        "capabilities": {
                            "tools": { "listChanged": false },
                            "resources": { "subscribe": false, "listChanged": false },
                            "prompts": { "listChanged": false }
                        },
                        "serverInfo": {
                            "name": "ensemble-ao3-typewriter",
                            "version": env!("CARGO_PKG_VERSION")
                        }
                    }
                })
            }
            "ping" => json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {}
            }),
            "tools/list" => json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "tools": get_mcp_all_tool_definitions()
                }
            }),
            "resources/list" => json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": { "resources": [] }
            }),
            "prompts/list" => json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": { "prompts": [] }
            }),
            "tools/call" => json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "content": [
                        {
                            "type": "text",
                            "text": "Ensemble GUI is not currently running or its MCP WebSocket server is disabled. Please launch Ensemble and ensure MCP Server is enabled in Settings -> MCP Server."
                        }
                    ],
                    "isError": true
                }
            }),
            _ => json!({
                "jsonrpc": "2.0",
                "id": id,
                "error": {
                    "code": -32000,
                    "message": "Ensemble GUI is not currently running."
                }
            }),
        };

        serde_json::to_string(&resp).ok()
    }
}
