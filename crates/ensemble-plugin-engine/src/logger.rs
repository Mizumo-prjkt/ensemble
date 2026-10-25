use chrono::Local;
use ensemble_core::debug::log_message;
use ensemble_core::paths::AppPaths;
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::PathBuf;

static LOG_MUTEX: Lazy<Mutex<()>> = Lazy::new(|| Mutex::new(()));

pub struct PluginLogger;

impl PluginLogger {
    pub fn log_file_path() -> PathBuf {
        let dir = AppPaths::plugins_dir();
        let _ = fs::create_dir_all(&dir);
        dir.join("plugin_activity.log")
    }

    pub fn log_event(plugin_id: &str, category: &str, action: &str, allowed: bool, details: &str) {
        let _guard = LOG_MUTEX.lock();
        let ts = Local::now().format("%Y-%m-%d %H:%M:%S%.3f");
        let status = if allowed { "ALLOWED" } else { "BLOCKED" };
        let line = if details.is_empty() {
            format!("[{}] [{}] [{}] [{}] {}", ts, status, plugin_id, category, action)
        } else {
            format!(
                "[{}] [{}] [{}] [{}] {} | {}",
                ts, status, plugin_id, category, action, details
            )
        };

        log_message("INFO", "plugin", &line);

        let path = Self::log_file_path();
        if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(&path) {
            let _ = writeln!(file, "{}", line);
        }
    }

    pub fn read_recent_logs(max_lines: usize) -> String {
        let _guard = LOG_MUTEX.lock();
        let path = Self::log_file_path();
        let Ok(content) = fs::read_to_string(&path) else {
            return "No plugin activity recorded yet.".to_string();
        };
        let lines: Vec<&str> = content.lines().collect();
        let start = lines.len().saturating_sub(max_lines);
        lines[start..].join("\n")
    }

    pub fn read_network_and_security_logs(max_lines: usize) -> String {
        let _guard = LOG_MUTEX.lock();
        let path = Self::log_file_path();
        let Ok(content) = fs::read_to_string(&path) else {
            return "No network requests or security events recorded yet.".to_string();
        };
        let filtered: Vec<&str> = content
            .lines()
            .filter(|line| {
                line.contains("[NETWORK]")
                    || line.contains("[HTTP_RESPONSE]")
                    || line.contains("[SECURITY_VIOLATION]")
                    || line.contains("[QUARANTINE")
                    || line.contains("[SECURITY]")
                    || line.contains("[PERMISSIONS]")
            })
            .collect();
        if filtered.is_empty() {
            return "No network requests or security events recorded yet.".to_string();
        }
        let start = filtered.len().saturating_sub(max_lines);
        filtered[start..].join("\n")
    }

    pub fn clear_logs() {
        let _guard = LOG_MUTEX.lock();
        let path = Self::log_file_path();
        let _ = fs::write(&path, b"");
    }
}
