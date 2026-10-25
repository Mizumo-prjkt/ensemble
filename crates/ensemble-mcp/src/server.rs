use chrono::{DateTime, Utc};
use futures_util::{SinkExt, StreamExt};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::fs;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::{mpsc, watch};
use tokio_tungstenite::tungstenite::Message;

pub const DEFAULT_MCP_PORT: u16 = 26303;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct McpAgentSession {
    pub id: String,
    pub name: String,
    pub model: String,
    pub role: String,
    pub status: String,
    pub connected_at: DateTime<Utc>,
    pub last_active_at: DateTime<Utc>,
    pub tool_call_count: i32,
}

impl Default for McpAgentSession {
    fn default() -> Self {
        let now = Utc::now();
        Self {
            id: String::new(),
            name: String::new(),
            model: String::new(),
            role: "co-author".to_string(),
            status: "Connected".to_string(),
            connected_at: now,
            last_active_at: now,
            tool_call_count: 0,
        }
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct McpTestResult {
    pub success: bool,
    pub server_running: bool,
    pub loopback_reachable: bool,
    pub discovery_file_exists: bool,
    pub port: u16,
    pub url: String,
    pub discovery_path: String,
    pub latency_ms: i64,
    pub status_message: String,
    pub error_details: String,
}

pub type McpMessageHandler = Arc<dyn Fn(u64, &str) -> Option<String> + Send + Sync>;

pub(crate) struct ServerSharedState {
    pub running: bool,
    pub actual_port: u16,
    pub sessions: HashMap<u64, McpAgentSession>,
    pub client_senders: HashMap<u64, mpsc::UnboundedSender<String>>,
    pub message_handler: Option<McpMessageHandler>,
}

impl Default for ServerSharedState {
    fn default() -> Self {
        Self {
            running: false,
            actual_port: 0,
            sessions: HashMap::new(),
            client_senders: HashMap::new(),
            message_handler: None,
        }
    }
}

pub struct McpServer {
    pub(crate) state: Arc<Mutex<ServerSharedState>>,
    shutdown_tx: Mutex<Option<watch::Sender<bool>>>,
    worker_handle: Mutex<Option<std::thread::JoinHandle<()>>>,
}

impl Default for McpServer {
    fn default() -> Self {
        Self::new()
    }
}

impl McpServer {
    pub fn new() -> Self {
        Self {
            state: Arc::new(Mutex::new(ServerSharedState::default())),
            shutdown_tx: Mutex::new(None),
            worker_handle: Mutex::new(None),
        }
    }

    pub fn set_message_handler(&self, handler: McpMessageHandler) {
        self.state.lock().message_handler = Some(handler);
    }

    pub fn is_running(&self) -> bool {
        self.state.lock().running
    }

    pub fn actual_port(&self) -> u16 {
        self.state.lock().actual_port
    }

    pub fn url(&self) -> String {
        let state = self.state.lock();
        if !state.running || state.actual_port == 0 {
            return String::new();
        }
        format!("ws://127.0.0.1:{}", state.actual_port)
    }

    pub fn connected_client_count(&self) -> usize {
        self.state.lock().sessions.len()
    }

    pub fn connected_agents(&self) -> Vec<McpAgentSession> {
        self.state.lock().sessions.values().cloned().collect()
    }

    pub fn register_agent_session(
        &self,
        client_id: u64,
        name: &str,
        model: &str,
        role: &str,
        status: &str,
    ) {
        Self::register_agent_session_in_state(&self.state, client_id, name, model, role, status);
    }

    pub(crate) fn register_agent_session_in_state(
        state: &Arc<Mutex<ServerSharedState>>,
        client_id: u64,
        name: &str,
        model: &str,
        role: &str,
        status: &str,
    ) {
        let mut guard = state.lock();
        let entry = guard.sessions.entry(client_id).or_insert_with(|| {
            let mut s = McpAgentSession::default();
            s.id = format!("agent-{}", client_id);
            s
        });
        if !name.trim().is_empty() {
            entry.name = name.trim().to_string();
        }
        if !model.trim().is_empty() {
            entry.model = model.trim().to_string();
        }
        if !role.trim().is_empty() {
            entry.role = role.trim().to_string();
        }
        if !status.trim().is_empty() {
            entry.status = status.trim().to_string();
        }
        entry.last_active_at = Utc::now();
    }

    pub fn agent_session_for_client(&self, client_id: u64) -> McpAgentSession {
        Self::agent_session_from_state(&self.state, client_id)
    }

    pub(crate) fn agent_session_from_state(
        state: &Arc<Mutex<ServerSharedState>>,
        client_id: u64,
    ) -> McpAgentSession {
        state
            .lock()
            .sessions
            .get(&client_id)
            .cloned()
            .unwrap_or_default()
    }

    pub fn start(&self, preferred_port: u16) -> bool {
        if self.is_running() {
            self.stop();
        }

        let mut bound_listener: Option<TcpListener> = None;
        let base_port = if preferred_port == 0 {
            DEFAULT_MCP_PORT
        } else {
            preferred_port
        };

        for offset in 0..=20u16 {
            if let Some(candidate) = base_port.checked_add(offset) {
                if let Ok(listener) = TcpListener::bind(("127.0.0.1", candidate)) {
                    bound_listener = Some(listener);
                    break;
                }
            }
        }

        if bound_listener.is_none() {
            if let Ok(listener) = TcpListener::bind(("127.0.0.1", 0)) {
                bound_listener = Some(listener);
            }
        }

        let std_listener = match bound_listener {
            Some(l) => l,
            None => return false,
        };

        if std_listener.set_nonblocking(true).is_err() {
            return false;
        }

        let actual_port = match std_listener.local_addr() {
            Ok(addr) => addr.port(),
            Err(_) => return false,
        };

        {
            let mut state = self.state.lock();
            state.running = true;
            state.actual_port = actual_port;
            state.sessions.clear();
            state.client_senders.clear();
        }

        self.write_discovery_file_for_port(actual_port);

        let (shutdown_tx, shutdown_rx) = watch::channel(false);
        *self.shutdown_tx.lock() = Some(shutdown_tx);

        let state_clone = Arc::clone(&self.state);
        let handle = std::thread::Builder::new()
            .name(format!("ensemble-mcp-{}", actual_port))
            .spawn(move || {
                let rt = match tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()
                {
                    Ok(rt) => rt,
                    Err(_) => return,
                };

                rt.block_on(async move {
                    let listener = match tokio::net::TcpListener::from_std(std_listener) {
                        Ok(l) => l,
                        Err(_) => return,
                    };

                    let mut rx = shutdown_rx;
                    static NEXT_CLIENT_ID: AtomicU64 = AtomicU64::new(1);

                    loop {
                        tokio::select! {
                            changed = rx.changed() => {
                                if changed.is_err() || *rx.borrow() {
                                    break;
                                }
                            }
                            accept_res = listener.accept() => {
                                let (stream, _) = match accept_res {
                                    Ok(pair) => pair,
                                    Err(_) => continue,
                                };
                                let client_id = NEXT_CLIENT_ID.fetch_add(1, Ordering::Relaxed);
                                let state_for_conn = Arc::clone(&state_clone);
                                let mut conn_shutdown = rx.clone();

                                tokio::spawn(async move {
                                    let ws_stream = match tokio_tungstenite::accept_async(stream).await {
                                        Ok(ws) => ws,
                                        Err(_) => return,
                                    };

                                    let (mut ws_write, mut ws_read) = ws_stream.split();
                                    let (out_tx, mut out_rx) = mpsc::unbounded_channel::<String>();

                                    {
                                        let mut st = state_for_conn.lock();
                                        let now = Utc::now();
                                        let next_num = st.sessions.len() + 1;
                                        st.sessions.insert(
                                            client_id,
                                            McpAgentSession {
                                                id: format!("agent-{}", client_id),
                                                name: format!("Agent #{}", next_num),
                                                model: "unknown".to_string(),
                                                role: "co-author".to_string(),
                                                status: "Connected".to_string(),
                                                connected_at: now,
                                                last_active_at: now,
                                                tool_call_count: 0,
                                            },
                                        );
                                        st.client_senders.insert(client_id, out_tx.clone());
                                    }

                                    loop {
                                        tokio::select! {
                                            _ = conn_shutdown.changed() => {
                                                let _ = ws_write.close().await;
                                                break;
                                            }
                                            Some(outbound) = out_rx.recv() => {
                                                if ws_write.send(Message::Text(outbound.into())).await.is_err() {
                                                    break;
                                                }
                                            }
                                            msg_opt = ws_read.next() => {
                                                match msg_opt {
                                                    Some(Ok(Message::Text(text))) => {
                                                        let text_str: &str = &text;
                                                        let handler_opt = {
                                                            let mut st = state_for_conn.lock();
                                                            if let Some(sess) = st.sessions.get_mut(&client_id) {
                                                                sess.last_active_at = Utc::now();
                                                                sess.tool_call_count += 1;
                                                            }
                                                            st.message_handler.clone()
                                                        };
                                                        if let Some(handler) = handler_opt {
                                                            if let Some(resp) = handler(client_id, text_str) {
                                                                if !resp.is_empty() {
                                                                    if ws_write.send(Message::Text(resp.into())).await.is_err() {
                                                                        break;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                    Some(Ok(Message::Binary(bin))) => {
                                                        if let Ok(text_str) = std::str::from_utf8(&bin) {
                                                            let handler_opt = {
                                                                let mut st = state_for_conn.lock();
                                                                if let Some(sess) = st.sessions.get_mut(&client_id) {
                                                                    sess.last_active_at = Utc::now();
                                                                    sess.tool_call_count += 1;
                                                                }
                                                                st.message_handler.clone()
                                                            };
                                                            if let Some(handler) = handler_opt {
                                                                if let Some(resp) = handler(client_id, text_str) {
                                                                    if !resp.is_empty() {
                                                                        if ws_write.send(Message::Text(resp.into())).await.is_err() {
                                                                            break;
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                    Some(Ok(Message::Ping(payload))) => {
                                                        if ws_write.send(Message::Pong(payload)).await.is_err() {
                                                            break;
                                                        }
                                                    }
                                                    Some(Ok(Message::Close(_))) | None | Some(Err(_)) => {
                                                        break;
                                                    }
                                                    _ => {}
                                                }
                                            }
                                        }
                                    }

                                    let mut st = state_for_conn.lock();
                                    st.sessions.remove(&client_id);
                                    st.client_senders.remove(&client_id);
                                });
                            }
                        }
                    }
                });
            })
            .ok();

        *self.worker_handle.lock() = handle;
        true
    }

    pub fn stop(&self) {
        let port_before_stop = {
            let mut state = self.state.lock();
            if !state.running {
                return;
            }
            let p = state.actual_port;
            state.running = false;
            state.actual_port = 0;
            state.sessions.clear();
            state.client_senders.clear();
            p
        };

        if let Some(tx) = self.shutdown_tx.lock().take() {
            let _ = tx.send(true);
        }

        if let Some(handle) = self.worker_handle.lock().take() {
            let _ = handle.join();
        }

        self.remove_discovery_file(port_before_stop);
    }

    pub fn broadcast(&self, message: &str) {
        let senders: Vec<mpsc::UnboundedSender<String>> = {
            let st = self.state.lock();
            st.client_senders.values().cloned().collect()
        };
        for tx in senders {
            let _ = tx.send(message.to_string());
        }
    }

    pub fn send_to_client(&self, client_id: u64, message: &str) {
        let tx_opt = self.state.lock().client_senders.get(&client_id).cloned();
        if let Some(tx) = tx_opt {
            let _ = tx.send(message.to_string());
        }
    }

    pub fn discovery_file_path() -> String {
        if let Ok(override_dir) = std::env::var("ENSEMBLE_MCP_CONFIG_DIR") {
            let p = PathBuf::from(override_dir);
            let _ = fs::create_dir_all(&p);
            return p.join("mcp.json").to_string_lossy().to_string();
        }
        let base = ensemble_core::paths::AppPaths::app_config_dir();
        let _ = fs::create_dir_all(&base);
        base.join("mcp.json").to_string_lossy().to_string()
    }

    pub fn read_discovery_file() -> Value {
        let path = Self::discovery_file_path();
        let content = match fs::read_to_string(&path) {
            Ok(c) => c,
            Err(_) => return json!({}),
        };
        match serde_json::from_str::<Value>(&content) {
            Ok(Value::Object(map)) => Value::Object(map),
            _ => json!({}),
        }
    }

    fn write_discovery_file_for_port(&self, port: u16) {
        let path = Self::discovery_file_path();
        if let Some(parent) = std::path::Path::new(&path).parent() {
            let _ = fs::create_dir_all(parent);
        }

        let exe_path = std::env::current_exe()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|_| "ensemble".to_string());

        let obj = json!({
            "name": "ensemble-ao3-typewriter",
            "version": env!("CARGO_PKG_VERSION"),
            "protocol": "mcp",
            "transport": "websocket",
            "host": "127.0.0.1",
            "port": port,
            "url": format!("ws://127.0.0.1:{}", port),
            "pid": std::process::id(),
            "executablePath": exe_path,
            "bridgeArgs": ["--mcp-bridge"],
            "startedAt": Utc::now().to_rfc3339()
        });

        if let Ok(serialized) = serde_json::to_string_pretty(&obj) {
            let _ = fs::write(&path, serialized);
        }
    }

    fn remove_discovery_file(&self, expected_port: u16) {
        let path = Self::discovery_file_path();
        if std::path::Path::new(&path).exists() {
            let existing = Self::read_discovery_file();
            let file_port = existing.get("port").and_then(|v| v.as_u64()).unwrap_or(0) as u16;
            if expected_port == 0 || file_port == 0 || file_port == expected_port {
                let _ = fs::remove_file(&path);
            }
        }
    }

    pub fn test_loopback(&self) -> McpTestResult {
        let mut res = McpTestResult {
            server_running: self.is_running(),
            port: self.actual_port(),
            url: self.url(),
            discovery_path: Self::discovery_file_path(),
            discovery_file_exists: std::path::Path::new(&Self::discovery_file_path()).exists(),
            ..Default::default()
        };

        if !res.server_running || res.port == 0 {
            res.status_message = "MCP Server is not currently running.".to_string();
            res.error_details =
                "Enable the MCP server in Settings -> MCP Server first.".to_string();
            return res;
        }

        let start = Instant::now();
        let addr: std::net::SocketAddr = format!("127.0.0.1:{}", res.port).parse().unwrap();
        match TcpStream::connect_timeout(&addr, Duration::from_millis(2500)) {
            Ok(mut stream) => {
                let _ = stream.set_read_timeout(Some(Duration::from_millis(2500)));
                let _ = stream.set_write_timeout(Some(Duration::from_millis(2500)));
                let req = format!(
                    "GET / HTTP/1.1\r\n\
                     Host: 127.0.0.1:{}\r\n\
                     Upgrade: websocket\r\n\
                     Connection: Upgrade\r\n\
                     Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\
                     Sec-WebSocket-Version: 13\r\n\r\n",
                    res.port
                );
                if stream.write_all(req.as_bytes()).is_ok() {
                    let mut buf = [0u8; 512];
                    if let Ok(n) = stream.read(&mut buf) {
                        let resp = String::from_utf8_lossy(&buf[..n]);
                        if resp.contains("101") || resp.to_ascii_lowercase().contains("upgrade") {
                            res.latency_ms = start.elapsed().as_millis() as i64;
                            res.loopback_reachable = true;
                            res.success = true;
                            res.status_message = format!(
                                "Loopback connection verified on {} ({} ms). Ready for AI agents.",
                                res.url, res.latency_ms
                            );
                            return res;
                        }
                    }
                }
                res.latency_ms = start.elapsed().as_millis() as i64;
                res.loopback_reachable = true;
                res.success = true;
                res.status_message = format!(
                    "Loopback TCP reachable on {} ({} ms).",
                    res.url, res.latency_ms
                );
                res
            }
            Err(e) => {
                res.latency_ms = start.elapsed().as_millis() as i64;
                res.loopback_reachable = false;
                res.success = false;
                res.status_message = "Loopback connection failed.".to_string();
                res.error_details = format!("Socket error: {}", e);
                res
            }
        }
    }
}

impl Drop for McpServer {
    fn drop(&mut self) {
        self.stop();
    }
}
