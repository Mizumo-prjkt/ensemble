use chrono::{DateTime, Utc};
use once_cell::sync::Lazy;
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::time::Duration;

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ReleaseInfo {
    pub tag_name: String,
    pub version: String,
    pub name: String,
    pub html_url: String,
    pub body: String,
    pub published_at: Option<DateTime<Utc>>,
    pub is_update_available: bool,
}

pub struct UpdateChecker {
    current_version: String,
    api_url: String,
}

impl Default for UpdateChecker {
    fn default() -> Self {
        Self::new(env!("CARGO_PKG_VERSION"))
    }
}

impl UpdateChecker {
    pub fn new(current_version: impl Into<String>) -> Self {
        Self {
            current_version: current_version.into(),
            api_url: "https://api.github.com/repos/Mizumo-prjkt/ensemble/releases/latest"
                .to_string(),
        }
    }

    pub fn check_for_updates(&self) -> Result<ReleaseInfo, String> {
        let client = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(12))
            .user_agent("Ensemble-Updater/1.3.0 (Desktop)")
            .build()
            .map_err(|e| format!("Failed to initialize updater client: {}", e))?;

        let resp = client
            .get(&self.api_url)
            .header(reqwest::header::ACCEPT, "application/vnd.github.v3+json")
            .send()
            .map_err(|e| format!("Network error checking for updates: {}", e))?;

        let status = resp.status().as_u16();
        if status == 404 {
            return Err("No GitHub releases found for Mizumo-prjkt/ensemble.".to_string());
        }
        if status == 403 {
            return Err(
                "GitHub API rate limit exceeded or access denied. Please try again later."
                    .to_string(),
            );
        }
        if !resp.status().is_success() {
            return Err(format!("GitHub API returned HTTP {}", status));
        }

        let bytes = resp
            .bytes()
            .map_err(|e| format!("Failed reading GitHub release response: {}", e))?;
        let info = Self::parse_release_json(&bytes, &self.current_version);
        if info.tag_name.is_empty() {
            return Err("Failed to parse release information from GitHub.".to_string());
        }
        Ok(info)
    }

    pub fn parse_version_parts(ver_str: &str) -> [i32; 3] {
        static RE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r"(\d+)(?:\.(\d+))?(?:\.(\d+))?").unwrap());
        if let Some(caps) = RE.captures(ver_str) {
            let major = caps
                .get(1)
                .and_then(|m| m.as_str().parse::<i32>().ok())
                .unwrap_or(0);
            let minor = caps
                .get(2)
                .and_then(|m| m.as_str().parse::<i32>().ok())
                .unwrap_or(0);
            let patch = caps
                .get(3)
                .and_then(|m| m.as_str().parse::<i32>().ok())
                .unwrap_or(0);
            [major, minor, patch]
        } else {
            [0, 0, 0]
        }
    }

    pub fn is_version_newer(remote_ver: &str, current_ver: &str) -> bool {
        let remote = Self::parse_version_parts(remote_ver);
        let current = Self::parse_version_parts(current_ver);
        for i in 0..3 {
            if remote[i] > current[i] {
                return true;
            }
            if remote[i] < current[i] {
                return false;
            }
        }
        false
    }

    pub fn parse_release_json(json_data: &[u8], current_ver: &str) -> ReleaseInfo {
        let mut info = ReleaseInfo::default();
        let Ok(obj) = serde_json::from_slice::<serde_json::Value>(json_data) else {
            return info;
        };

        info.tag_name = obj
            .get("tag_name")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim()
            .to_string();
        info.name = obj
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim()
            .to_string();
        info.html_url = obj
            .get("html_url")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim()
            .to_string();
        info.body = obj
            .get("body")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();

        if let Some(pub_str) = obj.get("published_at").and_then(|v| v.as_str()) {
            if let Ok(dt) = DateTime::parse_from_rfc3339(pub_str) {
                info.published_at = Some(dt.with_timezone(&Utc));
            }
        }

        if info.html_url.is_empty() && !info.tag_name.is_empty() {
            info.html_url = format!(
                "https://github.com/Mizumo-prjkt/ensemble/releases/tag/{}",
                info.tag_name
            );
        } else if info.html_url.is_empty() {
            info.html_url = "https://github.com/Mizumo-prjkt/ensemble/releases/latest".to_string();
        }

        info.version = info
            .tag_name
            .trim_start_matches(['v', 'V'])
            .to_string();
        info.is_update_available = Self::is_version_newer(&info.version, current_ver);
        info
    }
}
