use crate::parser::Ao3Parser;
use crate::session::Ao3Session;
use crate::types::{Ao3FullWork, Ao3Pseud, Ao3WorkSkin, Ao3WorkSummary};
use std::io::{Read, Write};
use std::net::TcpListener;
use std::sync::mpsc::{self, Receiver};
use std::thread;

pub struct Ao3Client;

impl Ao3Client {
    pub fn fetch_pseuds(session: &mut Ao3Session) -> Result<Vec<Ao3Pseud>, String> {
        if !session.is_authenticated() {
            return Err("User is not authenticated with AO3.".to_string());
        }
        let url = format!(
            "https://archiveofourown.org/users/{}/pseuds",
            session.username()
        );
        let html = session.authenticated_get(&url)?;
        Ok(Ao3Parser::parse_pseuds_list(&html))
    }

    pub fn fetch_works_list(
        session: &mut Ao3Session,
        pseud: &str,
        include_drafts: bool,
    ) -> Result<Vec<Ao3WorkSummary>, String> {
        if !session.is_authenticated() {
            return Err("User is not authenticated with AO3.".to_string());
        }
        let url = if !pseud.trim().is_empty() {
            format!(
                "https://archiveofourown.org/users/{}/pseuds/{}/works",
                session.username(),
                pseud.trim()
            )
        } else {
            format!(
                "https://archiveofourown.org/users/{}/works",
                session.username()
            )
        };
        let html = session.authenticated_get(&url)?;
        let mut works = Ao3Parser::parse_works_list(&html);

        if include_drafts {
            let drafts_url = format!(
                "https://archiveofourown.org/users/{}/drafts",
                session.username()
            );
            if let Ok(d_html) = session.authenticated_get(&drafts_url) {
                let mut drafts = Ao3Parser::parse_works_list(&d_html);
                for d in &mut drafts {
                    d.is_draft = true;
                }
                works.extend(drafts);
            }
        }
        Ok(works)
    }

    pub fn fetch_work_skins(session: &mut Ao3Session) -> Result<Vec<Ao3WorkSkin>, String> {
        if !session.is_authenticated() {
            return Err("User is not authenticated with AO3.".to_string());
        }
        let url = format!(
            "https://archiveofourown.org/users/{}/skins",
            session.username()
        );
        let html = session.authenticated_get(&url)?;
        Ok(Ao3Parser::parse_skins_list(&html))
    }

    pub fn fetch_skin_css(session: &mut Ao3Session, skin_id: i32) -> Result<String, String> {
        if !session.is_authenticated() {
            return Err("User is not authenticated with AO3.".to_string());
        }
        let url = format!("https://archiveofourown.org/skins/{}", skin_id);
        let html = session.authenticated_get(&url)?;
        Ok(Ao3Parser::parse_skin_css(&html))
    }

    pub fn fetch_full_work(session: &mut Ao3Session, work_id: i32) -> Result<Ao3FullWork, String> {
        if !session.is_authenticated() {
            return Err("User is not authenticated with AO3.".to_string());
        }
        let url = format!(
            "https://archiveofourown.org/works/{}?view_full_work=true",
            work_id
        );
        let html = session.authenticated_get(&url)?;
        let mut full_work = Ao3Parser::parse_full_work(&html);
        full_work.summary.work_id = work_id;
        Ok(full_work)
    }
}

pub struct Ao3AuthServer {
    port: u16,
    rx: Receiver<String>,
}

impl Ao3AuthServer {
    pub fn start() -> Result<Self, String> {
        let listener = TcpListener::bind("127.0.0.1:0")
            .map_err(|e| format!("Failed to bind local auth server: {}", e))?;
        let port = listener
            .local_addr()
            .map_err(|e| format!("Failed to get local port: {}", e))?
            .port();
        let (tx, rx) = mpsc::channel();

        thread::spawn(move || {
            for stream in listener.incoming().flatten() {
                let mut stream = stream;
                let mut buf = [0u8; 4096];
                if let Ok(n) = stream.read(&mut buf) {
                    let req = String::from_utf8_lossy(&buf[..n]);
                    if let Some(idx) = req.find("cookie=") {
                        let rest = &req[idx + 7..];
                        let end = rest
                            .find(['&', ' ', '\r', '\n'])
                            .unwrap_or(rest.len());
                        let raw_cookie = url::form_urlencoded::parse(
                            format!("c={}", &rest[..end]).as_bytes(),
                        )
                        .next()
                        .map(|(_, v)| v.into_owned())
                        .unwrap_or_else(|| rest[..end].to_string());

                        let resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n<html><body style='background:#1e1e2e;color:#cdd6f4;font-family:sans-serif;text-align:center;padding:40px;'><h2>AO3 Cookie Received by Ensemble!</h2><p>You may now close this tab and return to Ensemble.</p></body></html>";
                        let _ = stream.write_all(resp.as_bytes());
                        let _ = tx.send(raw_cookie);
                        break;
                    }
                }
                let resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nEnsemble Auth Receiver Ready";
                let _ = stream.write_all(resp.as_bytes());
            }
        });

        Ok(Self { port, rx })
    }

    pub fn port(&self) -> u16 {
        self.port
    }

    pub fn try_recv_cookie(&self) -> Option<String> {
        self.rx.try_recv().ok()
    }
}
