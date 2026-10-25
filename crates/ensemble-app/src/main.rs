use clap::Parser;
use ensemble_app::{SingleInstanceAction, SingleInstanceManager};
use ensemble_app::window::EnsembleWindow;
use ensemble_core::debug::init_debug_system;
use ensemble_mcp::{McpAgentInstaller, McpBridge, McpHandler, McpServer};
use libadwaita as adw;
use libadwaita::prelude::*;
use std::path::PathBuf;
use std::sync::Arc;

#[derive(Parser, Debug)]
#[command(
    name = "ensemble",
    version = "1.3.0",
    about = "Ensemble — AO3 Fanfiction Typewriter (Native GTK4 & Libadwaita Edition)"
)]
struct CliArgs {
    /// Run as an MCP stdio JSON-RPC bridge connected to the active Ensemble instance
    #[arg(long = "mcp-bridge")]
    mcp_bridge: bool,

    /// Check MCP server status and print connection diagnostics
    #[arg(long = "mcp-status")]
    mcp_status: bool,

    /// Install MCP server configuration and manual into local AI agent directories
    #[arg(long = "send-mcp-manual-to-agents")]
    send_mcp_manual: bool,

    /// Disable hardware GPU acceleration for rendering
    #[arg(long = "disable-gpu")]
    disable_gpu: bool,

    /// Allow multiple Ensemble instances to run simultaneously
    #[arg(long = "allow-multiple-instances")]
    allow_multiple_instances: bool,

    /// Enable embedded MCP WebSocket server (enabled by default)
    #[arg(long = "enable-mcp", default_value_t = true)]
    enable_mcp: bool,

    /// Preferred port for the embedded MCP WebSocket server
    #[arg(long = "mcp-port", default_value_t = 26303)]
    mcp_port: u16,

    /// Optional .ao3proj file path to open on launch
    #[arg(value_name = "PROJECT_FILE")]
    project_file: Option<String>,
}

fn main() {
    let args = CliArgs::parse();

    init_debug_system();
    log::info!("Starting Ensemble v1.3.0 (Native GTK4 + Libadwaita Edition)");

    // 1. Handle --mcp-bridge mode (stdio bridge for AI agents)
    if args.mcp_bridge {
        let exit_code = McpBridge::run_bridge();
        std::process::exit(exit_code);
    }

    // 2. Handle --mcp-status mode
    if args.mcp_status {
        let exit_code = McpBridge::run_status_check();
        std::process::exit(exit_code);
    }

    // 3. Handle --send-mcp-manual-to-agents
    if args.send_mcp_manual {
        let exe = std::env::current_exe()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|_| "ensemble".to_string());
        let results = McpAgentInstaller::install_all(&exe, args.mcp_port);
        println!(
            "Installed MCP configurations: {} updated, {} manuals, {} errors",
            results.updated_configs.len(),
            results.installed_manuals.len(),
            results.errors.len()
        );
        std::process::exit(0);
    }

    // 4. Single instance check
    let single_instance_action = SingleInstanceManager::check_and_handle(
        args.allow_multiple_instances,
        args.project_file.as_deref(),
    );
    if single_instance_action == SingleInstanceAction::ExitApplication {
        log::info!("Forwarded request to running Ensemble instance. Exiting.");
        std::process::exit(0);
    }

    // 5. Initialize MCP Server and Handler
    let mcp_server = Arc::new(McpServer::new());
    let handler = McpHandler::new(&mcp_server);

    if args.enable_mcp {
        if mcp_server.start(args.mcp_port) {
            log::info!(
                "MCP WebSocket server listening at ws://127.0.0.1:{}",
                mcp_server.actual_port()
            );
        } else {
            log::warn!("Failed to bind MCP WebSocket server to port {}", args.mcp_port);
        }
    }

    // 6. Initialize Libadwaita & Run Application
    libadwaita::init().expect("Failed to initialize Libadwaita");

    let app = adw::Application::builder()
        .application_id("org.ensemble.Ensemble")
        .build();

    let project_file_to_open = args.project_file.clone();
    let handler_clone = handler.clone();

    app.connect_activate(move |app| {
        let win_rc = EnsembleWindow::new(app, handler_clone.clone());

        if let Some(ref path_str) = project_file_to_open {
            let path = PathBuf::from(path_str);
            if path.exists() {
                win_rc.open_file(path);
            } else {
                win_rc.sync_from_workspace();
            }
        } else {
            win_rc.sync_from_workspace();
        }

        win_rc.window.present();
    });

    let exit_status = app.run_with_args(&Vec::<String>::new());

    if args.enable_mcp {
        mcp_server.stop();
    }

    std::process::exit(exit_status.into());
}
