use crate::tools::get_mcp_all_tool_definitions;
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentInstallResult {
    pub success: bool,
    pub updated_configs: Vec<String>,
    pub installed_manuals: Vec<String>,
    pub skipped_targets: Vec<String>,
    pub errors: Vec<String>,
    pub manual_markdown: String,
}

pub struct McpAgentInstaller;

impl McpAgentInstaller {
    fn home_dir() -> PathBuf {
        if let Ok(h) = std::env::var("HOME") {
            if !h.trim().is_empty() {
                return PathBuf::from(h);
            }
        }
        dirs::home_dir().unwrap_or_else(|| PathBuf::from("."))
    }

    pub fn antigravity_config_path() -> String {
        Self::home_dir()
            .join(".gemini/antigravity/mcp_config.json")
            .to_string_lossy()
            .to_string()
    }

    pub fn claude_desktop_config_path() -> String {
        let home = Self::home_dir();
        if cfg!(target_os = "macos") {
            home.join("Library/Application Support/Claude/claude_desktop_config.json")
                .to_string_lossy()
                .to_string()
        } else if cfg!(target_os = "windows") {
            if let Ok(appdata) = std::env::var("APPDATA") {
                PathBuf::from(appdata)
                    .join("Claude/claude_desktop_config.json")
                    .to_string_lossy()
                    .to_string()
            } else {
                home.join("AppData/Roaming/Claude/claude_desktop_config.json")
                    .to_string_lossy()
                    .to_string()
            }
        } else {
            home.join(".config/Claude/claude_desktop_config.json")
                .to_string_lossy()
                .to_string()
        }
    }

    pub fn claude_code_config_path() -> String {
        Self::home_dir()
            .join(".claude.json")
            .to_string_lossy()
            .to_string()
    }

    pub fn local_llm_config_path() -> String {
        let base = if let Ok(h) = std::env::var("HOME") {
            PathBuf::from(h).join(".config/Ensemble")
        } else {
            ensemble_core::paths::AppPaths::app_config_dir()
        };
        let _ = fs::create_dir_all(&base);
        base.join("mcp_servers.json").to_string_lossy().to_string()
    }

    pub fn generate_manual_markdown(executable_path: &str, port: u16) -> String {
        let clean_exe = if executable_path.trim().is_empty() {
            "ensemble"
        } else {
            executable_path.trim()
        };

        let mut md = String::with_capacity(16384);
        md.push_str(
            "---\n\
             name: ensemble-mcp\n\
             description: Comprehensive reference and guide for Ensemble's Model Context Protocol (MCP) server. Use when querying project state, editing chapters, updating work skins, validating AO3 HTML/CSS, managing lore graphs and entities, logging agent audits, or interacting with the live Ensemble AO3 writing suite.\n\
             ---\n\n\
             # Ensemble MCP Server — AI Agent Reference Manual\n\n\
             Ensemble exposes a full **Model Context Protocol (MCP)** server (`ensemble-ao3-typewriter`) allowing AI coding and writing assistants (Google Antigravity, Claude Desktop, Claude Code, and Local LLMs) to inspect, edit, lint, and manage AO3 fanfiction projects (`.ao3proj`) in real time.\n\n\
             ## 1. Connection & Transport Architecture\n\n",
        );
        md.push_str(&format!(
            "- **Server Identifier**: `ensemble-ao3-typewriter`\n\
             - **Live WebSocket Endpoint**: `ws://127.0.0.1:{}`\n\
             - **Stdio Bridge Command**: `\"{}\" --mcp-bridge`\n\
             - **Diagnostic Check Command**: `\"{}\" --mcp-status`\n\n",
            port, clean_exe, clean_exe
        ));
        md.push_str(&format!(
            "```json\n\
             {{\n  \
               \"mcpServers\": {{\n    \
                 \"ensemble-ao3-typewriter\": {{\n      \
                   \"command\": \"{}\",\n      \
                   \"args\": [\"--mcp-bridge\"],\n      \
                   \"env\": {{\n        \
                     \"ENSEMBLE_MCP_PORT\": \"{}\",\n        \
                     \"ENSEMBLE_MCP_URL\": \"ws://127.0.0.1:{}\"\n      \
                   }}\n    \
                 }}\n  \
               }}\n\
             }}\n\
             ```\n\n",
            clean_exe.replace('\\', "\\\\"),
            port,
            port
        ));

        md.push_str("## 2. MCP Resources (`resources/list` & `resources/read`)\n\n");
        md.push_str(
            "| URI | MIME Type | Description |\n\
             |---|---|---|\n\
             | `ensemble://project/summary` | `application/json` | Project metadata, chapter count, total word count, dirty state |\n\
             | `ensemble://project/workskin.css` | `text/css` | Active AO3 Work Skin CSS stylesheet |\n\
             | `ensemble://project/problems` | `application/json` | Live AO3 HTML & CSS compatibility linter diagnostics |\n\
             | `ensemble://project/history` | `application/json` | Git-style revision commits stored in `.ao3proj` |\n\
             | `ensemble://project/agentic` | `application/json` | Encrypted agentic workspace summary |\n\
             | `ensemble://project/lore_graph` | `text/plain` | Narrative lore graph in `.grph` DSL syntax |\n\
             | `ensemble://project/lore` | `application/json` | Structured lore entities, timeline history, conflicts, plans, references |\n\
             | `ensemble://project/metadata.xml` | `application/xml` | Standard AO3 project metadata XML manifest |\n\n",
        );

        md.push_str("## 3. MCP Tools Reference (`tools/list` & `tools/call`)\n\n");
        if let Some(tools) = get_mcp_all_tool_definitions().as_array() {
            for t in tools {
                let name = t.get("name").and_then(|v| v.as_str()).unwrap_or("");
                let desc = t.get("description").and_then(|v| v.as_str()).unwrap_or("");
                md.push_str(&format!("### `{}`\n{}\n\n", name, desc));
            }
        }

        md
    }

    pub fn upsert_mcp_server_in_json_file(
        file_path: &str,
        executable_path: &str,
        port: u16,
    ) -> Result<(), String> {
        let path = Path::new(file_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| format!("Failed to create directory {:?}: {}", parent, e))?;
        }

        let mut root_obj: Map<String, Value> = if path.exists() {
            match fs::read_to_string(path) {
                Ok(raw) if !raw.trim().is_empty() => serde_json::from_str::<Value>(&raw)
                    .ok()
                    .and_then(|v| v.as_object().cloned())
                    .unwrap_or_default(),
                _ => Map::new(),
            }
        } else {
            Map::new()
        };

        let mut mcp_servers = root_obj
            .get("mcpServers")
            .and_then(|v| v.as_object())
            .cloned()
            .unwrap_or_default();

        let server_entry = json!({
            "command": executable_path,
            "args": ["--mcp-bridge"],
            "env": {
                "ENSEMBLE_MCP_PORT": port.to_string(),
                "ENSEMBLE_MCP_URL": format!("ws://127.0.0.1:{}", port)
            }
        });

        mcp_servers.insert("ensemble-ao3-typewriter".to_string(), server_entry);
        root_obj.insert("mcpServers".to_string(), Value::Object(mcp_servers));

        let serialized = serde_json::to_string_pretty(&Value::Object(root_obj))
            .map_err(|e| format!("JSON serialization error: {}", e))?;
        fs::write(path, serialized).map_err(|e| format!("Failed to write {}: {}", file_path, e))
    }

    pub fn install_all(executable_path: &str, port: u16) -> AgentInstallResult {
        let clean_exe = if executable_path.trim().is_empty() {
            std::env::current_exe()
                .map(|p| p.to_string_lossy().to_string())
                .unwrap_or_else(|_| "ensemble".to_string())
        } else {
            executable_path.trim().to_string()
        };

        let manual = Self::generate_manual_markdown(&clean_exe, port);
        let mut res = AgentInstallResult {
            manual_markdown: manual.clone(),
            ..Default::default()
        };

        let config_targets = [
            Self::antigravity_config_path(),
            Self::claude_desktop_config_path(),
            Self::claude_code_config_path(),
            Self::local_llm_config_path(),
        ];

        for target in config_targets {
            match Self::upsert_mcp_server_in_json_file(&target, &clean_exe, port) {
                Ok(()) => res.updated_configs.push(target),
                Err(e) => res.errors.push(e),
            }
        }

        let home = Self::home_dir();
        let manual_targets = [
            home.join(".gemini/antigravity/skills/ensemble-mcp/SKILL.md"),
            home.join(".gemini/skills/ensemble-mcp/SKILL.md"),
            home.join(".claude/skills/ensemble-mcp/SKILL.md"),
            PathBuf::from(Self::local_llm_config_path())
                .parent()
                .unwrap_or_else(|| Path::new("."))
                .join("MCP_AGENT_MANUAL.md"),
        ];

        for m_path in manual_targets {
            if let Some(parent) = m_path.parent() {
                let _ = fs::create_dir_all(parent);
            }
            let path_str = m_path.to_string_lossy().to_string();
            match fs::write(&m_path, &manual) {
                Ok(()) => res.installed_manuals.push(path_str),
                Err(e) => res.errors.push(format!("Failed to write {}: {}", path_str, e)),
            }
        }

        res.success = res.errors.is_empty() && !res.updated_configs.is_empty();
        res
    }

    pub fn check_and_sync_agent_configs(executable_path: &str, port: u16) -> AgentInstallResult {
        Self::install_all(executable_path, port)
    }

    pub fn generate_local_llm_mcp_config_json() -> String {
        let exe = std::env::current_exe()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|_| "ensemble".to_string());
        serde_json::to_string_pretty(&json!({
            "mcpServers": {
                "ensemble": {
                    "command": exe,
                    "args": ["--mcp-bridge"],
                    "env": {
                        "ENSEMBLE_MCP_PORT": "26303"
                    }
                }
            }
        }))
        .unwrap_or_default()
    }

    pub fn generate_local_llm_agent_script() -> String {
        r#"#!/usr/bin/env python3
"""
Ensemble Local LLM MCP Connector Script
Supports --ollama and --llama-cpp backends over --mcp-bridge stdio transport.
"""
import argparse
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser(description="Ensemble Local LLM MCP Bridge")
    parser.add_argument("--ollama", type=str, help="Ollama model name (e.g., llama3.1)")
    parser.add_argument("--llama-cpp", type=str, help="llama.cpp server URL")
    parser.add_argument("--ensemble-bin", type=str, default="ensemble", help="Path to ensemble executable")
    args = parser.parse_args()

    proc = subprocess.Popen(
        [args.ensemble_bin, "--mcp-bridge"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True
    )
    return 0

if __name__ == "__main__":
    sys.exit(main())
"#
        .to_string()
    }

    pub fn sync_server_config(
        file_path: &str,
        executable_path: &str,
    ) -> Result<(bool, bool, String), String> {
        let path = Path::new(file_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| format!("Failed to create directory {:?}: {}", parent, e))?;
        }

        let mut root_obj: Map<String, Value> = if path.exists() {
            match fs::read_to_string(path) {
                Ok(raw) if !raw.trim().is_empty() => serde_json::from_str::<Value>(&raw)
                    .ok()
                    .and_then(|v| v.as_object().cloned())
                    .unwrap_or_default(),
                _ => Map::new(),
            }
        } else {
            Map::new()
        };

        let mut mcp_servers = root_obj
            .get("mcpServers")
            .and_then(|v| v.as_object())
            .cloned()
            .unwrap_or_default();

        let mut was_overwritten = false;
        let mut was_added = false;
        let mut old_path = String::new();

        if let Some(existing) = mcp_servers.get("ensemble").and_then(|v| v.as_object()) {
            let current_cmd = existing
                .get("command")
                .and_then(|v| v.as_str())
                .unwrap_or("");
            let current_args = existing
                .get("args")
                .and_then(|v| v.as_array())
                .cloned()
                .unwrap_or_default();
            let args_match = current_args.len() == 1
                && current_args[0].as_str() == Some("--mcp-bridge");
            if current_cmd == executable_path && args_match {
                return Ok((false, false, current_cmd.to_string()));
            }
            old_path = current_cmd.to_string();
            was_overwritten = true;
        } else {
            was_added = true;
        }

        mcp_servers.insert(
            "ensemble".to_string(),
            json!({
                "command": executable_path,
                "args": ["--mcp-bridge"]
            }),
        );
        root_obj.insert("mcpServers".to_string(), Value::Object(mcp_servers));
        let serialized = serde_json::to_string_pretty(&Value::Object(root_obj))
            .map_err(|e| format!("JSON serialization error: {}", e))?;
        fs::write(path, serialized).map_err(|e| format!("Failed to write {}: {}", file_path, e))?;
        Ok((was_overwritten, was_added, old_path))
    }
}

