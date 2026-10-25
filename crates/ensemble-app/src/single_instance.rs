use chrono::Utc;
use serde_json::{json, Value};
use std::collections::hash_map::DefaultHasher;
use std::env;
use std::fs;
use std::hash::{Hash, Hasher};
use std::io::{Read, Write};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use sysinfo::{Pid, ProcessesToUpdate, System};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SingleInstanceAction {
    ProceedAsPrimary,
    ExitApplication,
}

pub struct SingleInstanceManager {
    is_listening: Arc<AtomicBool>,
    stop_flag: Arc<AtomicBool>,
    worker_handle: Option<std::thread::JoinHandle<()>>,
}

impl Default for SingleInstanceManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SingleInstanceManager {
    pub fn new() -> Self {
        Self {
            is_listening: Arc::new(AtomicBool::new(false)),
            stop_flag: Arc::new(AtomicBool::new(false)),
            worker_handle: None,
        }
    }

    pub fn runtime_dir() -> String {
        #[cfg(unix)]
        {
            if let Ok(xdg) = env::var("XDG_RUNTIME_DIR") {
                if !xdg.is_empty() && PathBuf::from(&xdg).exists() {
                    return xdg;
                }
            }
        }
        env::temp_dir().to_string_lossy().to_string()
    }

    pub fn socket_name() -> String {
        if let Ok(user) = env::var("USER").or_else(|_| env::var("USERNAME")) {
            if !user.trim().is_empty() {
                return format!("ensemble_app_socket_{}", user.trim());
            }
        }
        let mut hasher = DefaultHasher::new();
        env::temp_dir().hash(&mut hasher);
        format!("ensemble_app_socket_{:x}", hasher.finish())
    }

    pub fn socket_path() -> String {
        #[cfg(unix)]
        {
            PathBuf::from(Self::runtime_dir())
                .join(Self::socket_name())
                .to_string_lossy()
                .to_string()
        }
        #[cfg(not(unix))]
        {
            Self::socket_name()
        }
    }

    pub fn pid_file_path() -> String {
        PathBuf::from(Self::runtime_dir())
            .join(format!("{}.pid", Self::socket_name()))
            .to_string_lossy()
            .to_string()
    }

    pub fn current_boot_id() -> String {
        #[cfg(target_os = "linux")]
        {
            if let Ok(raw) = fs::read_to_string("/proc/sys/kernel/random/boot_id") {
                let id = raw.trim().to_string();
                if !id.is_empty() {
                    return id;
                }
            }
        }
        let uptime_secs = System::uptime();
        let now_secs = Utc::now().timestamp().max(0) as u64;
        let boot_minute = now_secs.saturating_sub(uptime_secs) / 60;
        boot_minute.to_string()
    }

    pub fn get_process_executable(pid: u32) -> String {
        if pid == 0 {
            return String::new();
        }
        #[cfg(target_os = "linux")]
        {
            let exe_link = format!("/proc/{}/exe", pid);
            if let Ok(target) = fs::read_link(&exe_link) {
                let s = target.to_string_lossy().to_string();
                if !s.is_empty() {
                    return s;
                }
            }
            let cmd_path = format!("/proc/{}/cmdline", pid);
            if let Ok(bytes) = fs::read(&cmd_path) {
                let first = bytes.split(|b| *b == 0).next().unwrap_or(&[]);
                if !first.is_empty() {
                    return String::from_utf8_lossy(first).to_string();
                }
            }
            let comm_path = format!("/proc/{}/comm", pid);
            if let Ok(comm) = fs::read_to_string(&comm_path) {
                let s = comm.trim().to_string();
                if !s.is_empty() {
                    return s;
                }
            }
        }
        let mut sys = System::new();
        let sys_pid = Pid::from_u32(pid);
        sys.refresh_processes(ProcessesToUpdate::Some(&[sys_pid]), true);
        if let Some(proc_info) = sys.process(sys_pid) {
            if let Some(exe) = proc_info.exe() {
                let s = exe.to_string_lossy().to_string();
                if !s.is_empty() {
                    return s;
                }
            }
            return proc_info.name().to_string_lossy().to_string();
        }
        String::new()
    }

    pub fn is_ensemble_process(pid: u32) -> bool {
        if pid == 0 {
            return false;
        }
        if pid == std::process::id() {
            return true;
        }
        let exe_path = Self::get_process_executable(pid);
        if exe_path.is_empty() {
            return false;
        }
        let file_name = PathBuf::from(&exe_path)
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or(&exe_path)
            .to_ascii_lowercase();

        let current_app = env::current_exe()
            .ok()
            .and_then(|p| p.file_name().map(|s| s.to_string_lossy().to_ascii_lowercase()))
            .unwrap_or_default();

        if !current_app.is_empty()
            && (file_name == current_app || file_name.starts_with(&current_app))
        {
            return true;
        }
        if file_name.contains("ensemble") {
            return true;
        }
        if file_name.starts_with("test_") {
            return true;
        }
        false
    }

    pub fn is_process_running(pid: u32) -> bool {
        if pid == 0 {
            return false;
        }
        #[cfg(target_os = "linux")]
        {
            if !PathBuf::from(format!("/proc/{}", pid)).exists() {
                return false;
            }
            return Self::is_ensemble_process(pid);
        }
        #[cfg(not(target_os = "linux"))]
        {
            let mut sys = System::new();
            let sys_pid = Pid::from_u32(pid);
            sys.refresh_processes(ProcessesToUpdate::Some(&[sys_pid]), true);
            if sys.process(sys_pid).is_some() {
                return Self::is_ensemble_process(pid);
            }
            false
        }
    }

    pub fn terminate_process(pid: u32) -> bool {
        if pid == 0 || !Self::is_ensemble_process(pid) {
            return false;
        }
        let mut sys = System::new();
        let sys_pid = Pid::from_u32(pid);
        sys.refresh_processes(ProcessesToUpdate::Some(&[sys_pid]), true);
        if let Some(proc_info) = sys.process(sys_pid) {
            return proc_info.kill();
        }
        false
    }

    pub fn check_and_handle(
        disable_gui_prompt: bool,
        file_path_to_open: Option<&str>,
    ) -> SingleInstanceAction {
        let sock_path = Self::socket_path();
        let pid_path = Self::pid_file_path();
        let current_boot = Self::current_boot_id();

        let mut saved_pid: u32 = 0;
        let mut saved_boot_id = String::new();
        let mut saved_tcp_port: u16 = 0;

        if let Ok(raw) = fs::read_to_string(&pid_path) {
            let trimmed = raw.trim();
            if let Ok(val) = serde_json::from_str::<Value>(trimmed) {
                saved_pid = val.get("pid").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
                saved_boot_id = val
                    .get("bootId")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                saved_tcp_port = val.get("tcpPort").and_then(|v| v.as_u64()).unwrap_or(0) as u16;
            } else if let Ok(p) = trimmed.parse::<u32>() {
                saved_pid = p;
            }
        }

        if !saved_boot_id.is_empty() && !current_boot.is_empty() && saved_boot_id != current_boot {
            let _ = fs::remove_file(&sock_path);
            let _ = fs::remove_file(&pid_path);
            return SingleInstanceAction::ProceedAsPrimary;
        }

        // Attempt local connection (Unix stream on Unix, or loopback TCP if port recorded)
        if let Some(ack) = Self::try_notify_running_instance(
            &sock_path,
            saved_tcp_port,
            file_path_to_open.unwrap_or(""),
        ) {
            if ack || disable_gui_prompt || file_path_to_open.is_some() {
                return SingleInstanceAction::ExitApplication;
            }
        }

        if saved_pid > 0 && saved_pid != std::process::id() && Self::is_process_running(saved_pid) {
            if disable_gui_prompt {
                Self::terminate_process(saved_pid);
            }
        }

        let _ = fs::remove_file(&sock_path);
        let _ = fs::remove_file(&pid_path);
        SingleInstanceAction::ProceedAsPrimary
    }

    fn try_notify_running_instance(
        sock_path: &str,
        tcp_port: u16,
        file_path_to_open: &str,
    ) -> Option<bool> {
        let cmd = if !file_path_to_open.trim().is_empty() {
            format!("OPEN:{}\n", file_path_to_open.trim())
        } else {
            "RAISE\n".to_string()
        };

        #[cfg(unix)]
        {
            if let Ok(mut stream) = std::os::unix::net::UnixStream::connect(sock_path) {
                let _ = stream.set_read_timeout(Some(Duration::from_millis(1500)));
                let _ = stream.set_write_timeout(Some(Duration::from_millis(500)));
                let _ = stream.write_all(cmd.as_bytes());
                let _ = stream.flush();
                let mut resp = String::new();
                let _ = stream.read_to_string(&mut resp);
                return Some(resp.contains("STATUS:OK"));
            }
        }

        if tcp_port > 0 {
            if let Ok(mut stream) = std::net::TcpStream::connect_timeout(
                &std::net::SocketAddr::from(([127, 0, 0, 1], tcp_port)),
                Duration::from_millis(600),
            ) {
                let _ = stream.set_read_timeout(Some(Duration::from_millis(1500)));
                let _ = stream.set_write_timeout(Some(Duration::from_millis(500)));
                let _ = stream.write_all(cmd.as_bytes());
                let _ = stream.flush();
                let mut resp = String::new();
                let _ = stream.read_to_string(&mut resp);
                return Some(resp.contains("STATUS:OK"));
            }
        }

        None
    }

    pub fn start_listening(&mut self) -> bool {
        self.stop_listening();
        let sock_path = Self::socket_path();
        let pid_path = Self::pid_file_path();
        let _ = fs::remove_file(&sock_path);

        let tcp_listener = std::net::TcpListener::bind("127.0.0.1:0").ok();
        let tcp_port = tcp_listener
            .as_ref()
            .and_then(|l| l.local_addr().ok())
            .map(|a| a.port())
            .unwrap_or(0);

        #[cfg(unix)]
        let unix_listener = {
            let l = std::os::unix::net::UnixListener::bind(&sock_path).ok();
            if let Some(ref listener) = l {
                let _ = listener.set_nonblocking(true);
            }
            l
        };

        if let Some(ref listener) = tcp_listener {
            let _ = listener.set_nonblocking(true);
        }

        let meta = json!({
            "pid": std::process::id(),
            "bootId": Self::current_boot_id(),
            "appPath": env::current_exe().ok().map(|p| p.to_string_lossy().to_string()).unwrap_or_default(),
            "tcpPort": tcp_port,
            "startedAt": Utc::now().timestamp()
        });
        let _ = fs::write(&pid_path, serde_json::to_string(&meta).unwrap_or_default());

        self.is_listening.store(true, Ordering::SeqCst);
        self.stop_flag.store(false, Ordering::SeqCst);
        let stop_clone = Arc::clone(&self.stop_flag);

        let handle = std::thread::spawn(move || {
            while !stop_clone.load(Ordering::SeqCst) {
                #[cfg(unix)]
                if let Some(ref ul) = unix_listener {
                    if let Ok((mut stream, _)) = ul.accept() {
                        let mut buf = [0u8; 1024];
                        let _ = stream.read(&mut buf);
                        let reply = format!("PID:{}\nSTATUS:OK\n", std::process::id());
                        let _ = stream.write_all(reply.as_bytes());
                        let _ = stream.flush();
                    }
                }
                if let Some(ref tl) = tcp_listener {
                    if let Ok((mut stream, _)) = tl.accept() {
                        let mut buf = [0u8; 1024];
                        let _ = stream.read(&mut buf);
                        let reply = format!("PID:{}\nSTATUS:OK\n", std::process::id());
                        let _ = stream.write_all(reply.as_bytes());
                        let _ = stream.flush();
                    }
                }
                std::thread::sleep(Duration::from_millis(50));
            }
        });
        self.worker_handle = Some(handle);
        true
    }

    pub fn stop_listening(&mut self) {
        if self.is_listening.swap(false, Ordering::SeqCst) {
            self.stop_flag.store(true, Ordering::SeqCst);
            if let Some(h) = self.worker_handle.take() {
                let _ = h.join();
            }
            let _ = fs::remove_file(Self::socket_path());
            let _ = fs::remove_file(Self::pid_file_path());
        }
    }
}

impl Drop for SingleInstanceManager {
    fn drop(&mut self) {
        self.stop_listening();
    }
}
