use std::fs;
use std::path::PathBuf;

/// Cross-platform application directory and file path resolution.
/// Replaces POSIX-only path assumptions with OS-native standard directories:
/// - Linux: `~/.local/share/Ensemble` and `~/.config/Ensemble`
/// - Windows: `%LOCALAPPDATA%\Ensemble`
/// - macOS: `~/Library/Application Support/Ensemble`
pub struct AppPaths;

impl AppPaths {
    pub fn app_data_dir() -> PathBuf {
        if let Ok(override_dir) = std::env::var("ENSEMBLE_DATA_DIR") {
            if !override_dir.trim().is_empty() {
                return PathBuf::from(override_dir);
            }
        }
        dirs::data_local_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("Ensemble")
    }

    pub fn app_config_dir() -> PathBuf {
        if let Ok(override_dir) = std::env::var("ENSEMBLE_CONFIG_DIR") {
            if !override_dir.trim().is_empty() {
                return PathBuf::from(override_dir);
            }
        }
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("Ensemble")
    }

    pub fn log_file_path() -> PathBuf {
        let base = dirs::data_local_dir().unwrap_or_else(|| PathBuf::from("."));
        base.join("ensemble-debug.log")
    }

    pub fn session_file_path() -> PathBuf {
        Self::app_data_dir().join("session.json")
    }

    pub fn autosave_dir() -> PathBuf {
        Self::app_data_dir().join("autosave")
    }

    pub fn plugins_dir() -> PathBuf {
        Self::app_data_dir().join("plugins")
    }

    pub fn plugin_store_dir() -> PathBuf {
        Self::app_data_dir().join("plugin_store")
    }

    pub fn settings_file_path() -> PathBuf {
        Self::app_config_dir().join("settings.json")
    }

    pub fn mcp_discovery_file_path() -> PathBuf {
        Self::app_config_dir().join("mcp.json")
    }

    pub fn ensure_directories_exist() {
        for dir in [
            Self::app_data_dir(),
            Self::app_config_dir(),
            Self::autosave_dir(),
            Self::plugins_dir(),
            Self::plugin_store_dir(),
        ] {
            let _ = fs::create_dir_all(dir);
        }
    }
}
