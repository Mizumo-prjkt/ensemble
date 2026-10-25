//! Ensemble Model Context Protocol (MCP) Server, Stdio Bridge, 34+ Tools, and Agent Config Installer.
#![recursion_limit = "512"]

pub mod activity;

pub mod bridge;
pub mod handler;
pub mod installer;
pub mod server;
pub mod tools;

pub use activity::{sort_and_deduplicate_events, AgentActivityEvent, AgentActivityType};
pub use bridge::McpBridge;
pub use handler::{McpHandler, McpWorkspaceState};
pub use installer::{AgentInstallResult, McpAgentInstaller};
pub use server::{McpAgentSession, McpServer, McpTestResult, DEFAULT_MCP_PORT};
pub use tools::get_mcp_all_tool_definitions;
