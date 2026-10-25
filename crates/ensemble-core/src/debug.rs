use crate::paths::AppPaths;
use chrono::Local;
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};
use sysinfo::System;

pub type AggressiveChecksAlertCallback = Box<dyn Fn(&str, usize) + Send + Sync + 'static>;

#[derive(Debug, Clone, Default)]
pub struct HardwareInfo {
    pub os_string: String,
    pub windowing_platform: String,
    pub cpu_model: String,
    pub cpu_arch: String,
    pub cpu_threads: usize,
    pub ram_total: String,
    pub ram_available: String,
    pub installed_gpus: Vec<String>,
    pub active_gpu_renderer: String,
    pub active_gpu_vendor: String,
    pub gl_version: String,
}

struct DebugState {
    log_buffer: String,
    last_throttled_msg: String,
    last_msg_time: Option<Instant>,
    suppressed_count: usize,
    alert_callback: Option<AggressiveChecksAlertCallback>,
}

static MCP_BRIDGE_MODE: AtomicBool = AtomicBool::new(false);
static INITIALIZED: AtomicBool = AtomicBool::new(false);

static DEBUG_STATE: Lazy<Mutex<DebugState>> = Lazy::new(|| {
    Mutex::new(DebugState {
        log_buffer: String::with_capacity(16384),
        last_throttled_msg: String::new(),
        last_msg_time: None,
        suppressed_count: 0,
        alert_callback: None,
    })
});

pub fn set_mcp_bridge_mode(enabled: bool) {
    MCP_BRIDGE_MODE.store(enabled, Ordering::SeqCst);
}

pub fn is_mcp_bridge_mode() -> bool {
    MCP_BRIDGE_MODE.load(Ordering::SeqCst)
}

pub fn is_debug_build() -> bool {
    cfg!(debug_assertions)
}

pub fn full_version() -> String {
    let build_type = if is_debug_build() { "Debug" } else { "Release" };
    format!("{} ({})", env!("CARGO_PKG_VERSION"), build_type)
}

pub fn set_aggressive_checks_alert_callback(cb: Option<AggressiveChecksAlertCallback>) {
    let mut state = DEBUG_STATE.lock();
    state.alert_callback = cb;
}

pub fn init_debug_system() {
    if INITIALIZED.swap(true, Ordering::SeqCst) {
        return;
    }
    AppPaths::ensure_directories_exist();
    let log_path = AppPaths::log_file_path();
    if let Ok(mut file) = OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(&log_path)
    {
        let _ = writeln!(
            file,
            "[{}] [INFO ] [app] Ensemble Debug Logging System Initialized ({})",
            Local::now().format("%H:%M:%S.%3f"),
            full_version()
        );
    }
}

pub fn log_message(level: &str, category: &str, msg: &str) {
    let is_warn_or_err = level == "WARN" || level == "WARN " || level == "CRIT" || level == "CRIT ";
    let time_str = Local::now().format("%H:%M:%S.%3f").to_string();
    let now = Instant::now();

    let mut callback_to_fire: Option<(String, usize)> = None;

    {
        let mut state = DEBUG_STATE.lock();

        if is_warn_or_err {
            let is_similar = msg == state.last_throttled_msg
                || (msg.contains("Regex") && state.last_throttled_msg.contains("Regex"))
                || (msg.contains("QRegularExpression")
                    && state.last_throttled_msg.contains("QRegularExpression"));
            let is_rapid = state
                .last_msg_time
                .map(|t| now.duration_since(t) < Duration::from_millis(1200))
                .unwrap_or(false);

            if is_similar && is_rapid {
                state.suppressed_count += 1;
                state.last_msg_time = Some(now);
                let count = state.suppressed_count;
                if count == 5 || count == 20 || count == 50 {
                    callback_to_fire = Some((msg.to_string(), count));
                }
                if let Some((alert_msg, cnt)) = callback_to_fire {
                    if let Some(ref cb) = state.alert_callback {
                        cb(&alert_msg, cnt);
                    }
                }
                return;
            } else {
                if state.suppressed_count > 0 {
                    let notice = format!(
                        "[{}] [WARN ] [app] ⏱ Throttled {} repeated warning/error messages to prevent UI lockup: \"{}\"\n",
                        time_str, state.suppressed_count, state.last_throttled_msg
                    );
                    state.log_buffer.push_str(&notice);
                    state.suppressed_count = 0;
                }
                state.last_throttled_msg = msg.to_string();
                state.last_msg_time = Some(now);
            }
        }

        let level_padded = format!("{:<5}", level);
        let formatted = format!("[{}] [{}] [{}] {}\n", time_str, level_padded, category, msg);
        state.log_buffer.push_str(&formatted);
        if state.log_buffer.len() > 200_000 {
            state.log_buffer.drain(..50_000);
        }

        if let Ok(mut file) = OpenOptions::new()
            .create(true)
            .append(true)
            .open(AppPaths::log_file_path())
        {
            let _ = file.write_all(formatted.as_bytes());
        }
    }
}

pub fn emit_warning(category: &str, msg: &str) {
    log_message("WARN ", category, msg);
}

pub fn emit_info(category: &str, msg: &str) {
    log_message("INFO ", category, msg);
}

pub fn emit_debug(category: &str, msg: &str) {
    if is_debug_build() {
        log_message("DEBUG", category, msg);
    }
}

pub fn get_log_buffer() -> String {
    DEBUG_STATE.lock().log_buffer.clone()
}

pub fn clear_log_buffer() {
    DEBUG_STATE.lock().log_buffer.clear();
}

pub fn query_hardware_info() -> HardwareInfo {
    let mut sys = System::new_all();
    sys.refresh_all();

    let os_name = System::name().unwrap_or_else(|| std::env::consts::OS.to_string());
    let kernel = System::kernel_version().unwrap_or_else(|| "unknown".to_string());
    let os_string = format!("{} (Kernel {})", os_name, kernel);
    let cpu_arch = std::env::consts::ARCH.to_string();
    let cpu_threads = sys.cpus().len().max(1);
    let cpu_model = sys
        .cpus()
        .first()
        .map(|c| c.brand().trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| format!("{} ({} logical cores)", cpu_arch, cpu_threads));

    let total_gb = sys.total_memory() as f64 / (1024.0 * 1024.0 * 1024.0);
    let avail_gb = sys.available_memory() as f64 / (1024.0 * 1024.0 * 1024.0);
    let ram_total = format!("{:.2} GB", total_gb);
    let ram_available = format!("{:.2} GB", avail_gb);

    let windowing_platform = if cfg!(target_os = "windows") {
        "win32 (Slint + WebView2)".to_string()
    } else if cfg!(target_os = "macos") {
        "cocoa (Slint + WKWebView)".to_string()
    } else if std::env::var("WAYLAND_DISPLAY").is_ok() {
        "wayland".to_string()
    } else {
        "x11".to_string()
    };

    HardwareInfo {
        os_string,
        windowing_platform,
        cpu_model,
        cpu_arch,
        cpu_threads,
        ram_total,
        ram_available,
        installed_gpus: Vec::new(),
        active_gpu_renderer: String::new(),
        active_gpu_vendor: String::new(),
        gl_version: String::new(),
    }
}

pub fn format_hardware_report(info: &HardwareInfo) -> String {
    let mut out = String::new();
    out.push_str("--- System & Hardware Diagnostics ---\n");
    out.push_str(&format!("OS: {}\n", info.os_string));
    out.push_str(&format!("Platform: {}\n", info.windowing_platform));
    out.push_str(&format!(
        "CPU: {} [{}] ({} threads)\n",
        info.cpu_model, info.cpu_arch, info.cpu_threads
    ));
    out.push_str(&format!(
        "RAM: {} Total ({} Available)\n",
        info.ram_total, info.ram_available
    ));
    out.push_str("-------------------------------------");
    out
}

pub fn log_hardware_diagnostics() {
    let info = query_hardware_info();
    emit_info("app", "=================================================");
    emit_info("app", " System & Hardware Diagnostics");
    emit_info("app", &format!(" OS: {}", info.os_string));
    emit_info("app", &format!(" Windowing Platform: {}", info.windowing_platform));
    emit_info(
        "app",
        &format!(
            " CPU: {} [{}, {} threads]",
            info.cpu_model, info.cpu_arch, info.cpu_threads
        ),
    );
    emit_info(
        "app",
        &format!(" RAM: {} Total ({} Available)", info.ram_total, info.ram_available),
    );
    emit_info("app", "=================================================");
}
