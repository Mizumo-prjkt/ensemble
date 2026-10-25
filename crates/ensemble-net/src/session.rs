use crate::parser::Ao3Parser;
use std::time::{Duration, Instant};

#[derive(Debug, Clone)]
pub struct Ao3Session {
    username: String,
    raw_cookie_header: String,
    last_request_time: Option<Instant>,
}

impl Default for Ao3Session {
    fn default() -> Self {
        Self::new()
    }
}

impl Ao3Session {
    pub const READ_COOLDOWN_SECS: u64 = 10;
    pub const WRITE_COOLDOWN_SECS: u64 = 60;

    pub fn new() -> Self {
        Self {
            username: String::new(),
            raw_cookie_header: String::new(),
            last_request_time: None,
        }
    }

    pub fn is_authenticated(&self) -> bool {
        !self.username.is_empty() && !self.raw_cookie_header.is_empty()
    }

    pub fn username(&self) -> &str {
        &self.username
    }

    pub fn raw_cookie_header(&self) -> &str {
        &self.raw_cookie_header
    }

    pub fn set_cookie_from_string(&mut self, raw_cookie: &str) {
        let trimmed = raw_cookie.trim();
        if trimmed.contains('=') {
            self.raw_cookie_header = trimmed.to_string();
        } else if !trimmed.is_empty() {
            self.raw_cookie_header = format!("_otwarchive_session={}", trimmed);
        } else {
            self.raw_cookie_header.clear();
        }
    }

    pub fn verify_and_set_cookie(
        &mut self,
        raw_input: &str,
        provided_user: &str,
    ) -> Result<String, String> {
        let cleaned = raw_input.trim();
        if cleaned.is_empty() {
            return Err("Cookie string cannot be empty.".to_string());
        }

        self.set_cookie_from_string(cleaned);

        if !provided_user.trim().is_empty() {
            self.username = provided_user.trim().to_string();
            return Ok(self.username.clone());
        }

        let html = self.authenticated_get("https://archiveofourown.org/")?;
        let html_str = String::from_utf8_lossy(&html);
        if html_str.contains("Lost Cookie Home") {
            return Err(
                "AO3 rejected session cookie (Lost Cookie Home). Please log in to AO3 in your browser and copy your fresh _otwarchive_session value.".to_string(),
            );
        }

        let user = Ao3Parser::parse_username(&html);
        if !user.is_empty() {
            self.username = user.clone();
            Ok(user)
        } else {
            Err("Connected to AO3, but could not detect logged-in username. Please check cookie validity.".to_string())
        }
    }

    pub fn remaining_cooldown_secs(&self, is_write_action: bool) -> u64 {
        let Some(last) = self.last_request_time else {
            return 0;
        };
        let cooldown = if is_write_action {
            Self::WRITE_COOLDOWN_SECS
        } else {
            Self::READ_COOLDOWN_SECS
        };
        let elapsed = last.elapsed().as_secs();
        cooldown.saturating_sub(elapsed)
    }

    pub fn authenticated_get(&mut self, url: &str) -> Result<Vec<u8>, String> {
        self.last_request_time = Some(Instant::now());

        let client = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(20))
            .user_agent(
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Ensemble/1.3.0",
            )
            .build()
            .map_err(|e| format!("Failed to initialize HTTP client: {}", e))?;

        let mut req = client
            .get(url)
            .header(
                reqwest::header::ACCEPT,
                "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            )
            .header(reqwest::header::ACCEPT_LANGUAGE, "en-US,en;q=0.9")
            .header(reqwest::header::ACCEPT_ENCODING, "identity");

        if !self.raw_cookie_header.is_empty() {
            req = req.header(reqwest::header::COOKIE, &self.raw_cookie_header);
        }

        let resp = req
            .send()
            .map_err(|e| format!("Failed to connect to AO3: {}", e))?;
        let bytes = resp
            .bytes()
            .map_err(|e| format!("Failed reading AO3 response: {}", e))?;
        Ok(bytes.to_vec())
    }

    pub fn logout(&mut self) {
        self.raw_cookie_header.clear();
        self.username.clear();
    }
}
