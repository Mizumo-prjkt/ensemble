use crate::evaluator::Environment;
use crate::guard::ScriptTrap;
use crate::logger::PluginLogger;
use crate::permissions::PluginPermissions;
use crate::value::ScriptValue;
use ensemble_core::paths::AppPaths;
use parking_lot::Mutex;
use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

#[derive(Debug, Clone, Default)]
pub struct ChapterSummary {
    pub title: String,
    pub html: String,
    pub word_count: usize,
}

#[derive(Debug, Clone, Default)]
pub struct PluginHostContext {
    pub current_chapter_index: i32,
    pub current_chapter_title: String,
    pub current_chapter_html: String,
    pub current_plain_text: String,
    pub selected_text: String,
    pub work_skin_css: String,
    pub word_count: usize,
    pub chapters: Vec<ChapterSummary>,
    pub ui_widget_values: BTreeMap<String, String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PluginActionType {
    SetChapterHtml,
    InsertHtmlAtCursor,
    ReplaceSelection,
    SetWorkSkinCss,
    SetChapterTitle,
    ShowStatusBadge,
    ClearStatusBadge,
    ShowToastNotification,
    SendPluginMessage,
    UiCreateWindow,
    UiAddLabel,
    UiAddButton,
    UiAddInput,
    UiAddTextArea,
    UiSetWidgetText,
    UiShowWindow,
    UiCloseWindow,
    UiClearWindow,
    UiShowMessageBox,
}

#[derive(Debug, Clone)]
pub struct PluginPendingAction {
    pub action_type: PluginActionType,
    pub source_plugin_id: String,
    pub target_plugin_id: String,
    pub payload: String,
    pub secondary_payload: String,
    pub tertiary_payload: String,
    pub width: i32,
    pub height: i32,
}

impl Default for PluginPendingAction {
    fn default() -> Self {
        Self {
            action_type: PluginActionType::SetChapterHtml,
            source_plugin_id: String::new(),
            target_plugin_id: String::new(),
            payload: String::new(),
            secondary_payload: String::new(),
            tertiary_payload: String::new(),
            width: 420,
            height: 320,
        }
    }
}

struct SandboxInner {
    plugin_id: String,
    secret_uidp: String,
    permissions: PluginPermissions,
    context: PluginHostContext,
    pending_actions: Vec<PluginPendingAction>,
    security_violation: bool,
    violation_reason: String,
    violation_target_url: String,
}

impl SandboxInner {
    fn trigger_security_quarantine(&mut self, reason: &str, target_url: &str) {
        self.security_violation = true;
        self.violation_reason = reason.to_string();
        self.violation_target_url = target_url.to_string();
        PluginLogger::log_event(
            &self.plugin_id,
            "SECURITY_VIOLATION",
            reason,
            false,
            target_url,
        );
    }

    fn storage_dir(&self) -> PathBuf {
        let base = AppPaths::plugin_store_dir();
        let dir = base.join(&self.secret_uidp);
        let _ = fs::create_dir_all(&dir);
        dir
    }

    fn validate_storage_key(key: &str) -> Result<String, ScriptTrap> {
        if key.is_empty() || key.len() > 64 {
            return Err(ScriptTrap::new(
                "StorageError: key must be between 1 and 64 characters",
                0,
            ));
        }
        for c in key.chars() {
            if !c.is_ascii_alphanumeric() && c != '_' && c != '-' {
                return Err(ScriptTrap::new(
                    "SecurityError: storage key may only contain alphanumeric characters, '_', or '-' (path traversal blocked)",
                    0,
                ));
            }
        }
        Ok(key.to_string())
    }

    fn current_storage_bytes(&self) -> usize {
        let dir = self.storage_dir();
        let mut total = 0usize;
        if let Ok(entries) = fs::read_dir(dir) {
            for entry in entries.flatten() {
                if let Ok(meta) = entry.metadata() {
                    if meta.is_file() {
                        total += meta.len() as usize;
                    }
                }
            }
        }
        total
    }
}

#[derive(Clone)]
pub struct PluginSandbox {
    inner: Arc<Mutex<SandboxInner>>,
}

impl PluginSandbox {
    pub fn new(plugin_id: impl Into<String>, secret_uidp: impl Into<String>) -> Self {
        Self {
            inner: Arc::new(Mutex::new(SandboxInner {
                plugin_id: plugin_id.into(),
                secret_uidp: secret_uidp.into(),
                permissions: PluginPermissions::default(),
                context: PluginHostContext::default(),
                pending_actions: Vec::new(),
                security_violation: false,
                violation_reason: String::new(),
                violation_target_url: String::new(),
            })),
        }
    }

    pub fn set_permissions(&self, perms: PluginPermissions) {
        self.inner.lock().permissions = perms;
    }

    pub fn permissions(&self) -> PluginPermissions {
        self.inner.lock().permissions.clone()
    }

    pub fn has_security_violation(&self) -> bool {
        self.inner.lock().security_violation
    }

    pub fn security_violation_reason(&self) -> String {
        self.inner.lock().violation_reason.clone()
    }

    pub fn security_violation_target_url(&self) -> String {
        self.inner.lock().violation_target_url.clone()
    }

    pub fn clear_security_violation(&self) {
        let mut lock = self.inner.lock();
        lock.security_violation = false;
        lock.violation_reason.clear();
        lock.violation_target_url.clear();
    }

    pub fn set_host_context(&self, ctx: PluginHostContext) {
        self.inner.lock().context = ctx;
    }

    pub fn host_context(&self) -> PluginHostContext {
        self.inner.lock().context.clone()
    }

    pub fn take_pending_actions(&self) -> Vec<PluginPendingAction> {
        std::mem::take(&mut self.inner.lock().pending_actions)
    }

    pub fn plugin_id(&self) -> String {
        self.inner.lock().plugin_id.clone()
    }

    pub fn build_root_environment(&self) -> Arc<Mutex<Environment>> {
        let env = Arc::new(Mutex::new(Environment::new(None)));
        {
            let mut e = env.lock();

            // Built-in functions
            e.define(
                "len",
                ScriptValue::from_native(
                    "len",
                    Arc::new(|args, _guard| {
                        if args.len() != 1 {
                            return Err(ScriptTrap::new(
                                "TypeError: len() takes exactly one argument",
                                0,
                            ));
                        }
                        match &args[0] {
                            ScriptValue::String(s) => {
                                Ok(ScriptValue::from_number(s.chars().count() as f64))
                            }
                            ScriptValue::List(l) => {
                                Ok(ScriptValue::from_number(l.lock().len() as f64))
                            }
                            ScriptValue::Dict(d) => {
                                Ok(ScriptValue::from_number(d.lock().len() as f64))
                            }
                            _ => Err(ScriptTrap::new(
                                "TypeError: object of this type has no len()",
                                0,
                            )),
                        }
                    }),
                ),
            );

            e.define(
                "str",
                ScriptValue::from_native(
                    "str",
                    Arc::new(|args, guard| {
                        if args.is_empty() {
                            return Ok(ScriptValue::from_string(""));
                        }
                        let s = args[0].to_display_string();
                        guard.alloc_bytes(s.len(), 0)?;
                        Ok(ScriptValue::from_string(s))
                    }),
                ),
            );

            e.define(
                "int",
                ScriptValue::from_native(
                    "int",
                    Arc::new(|args, _guard| {
                        if args.is_empty() {
                            return Ok(ScriptValue::from_number(0.0));
                        }
                        Ok(ScriptValue::from_number(
                            (args[0].as_number() as i64) as f64,
                        ))
                    }),
                ),
            );

            e.define(
                "float",
                ScriptValue::from_native(
                    "float",
                    Arc::new(|args, _guard| {
                        if args.is_empty() {
                            return Ok(ScriptValue::from_number(0.0));
                        }
                        Ok(ScriptValue::from_number(args[0].as_number()))
                    }),
                ),
            );

            e.define(
                "round",
                ScriptValue::from_native(
                    "round",
                    Arc::new(|args, _guard| {
                        if args.is_empty() {
                            return Err(ScriptTrap::new(
                                "TypeError: round() requires at least 1 argument",
                                0,
                            ));
                        }
                        let val = args[0].as_number();
                        let digits = if args.len() >= 2 {
                            args[1].as_number() as i32
                        } else {
                            0
                        };
                        if digits <= 0 {
                            return Ok(ScriptValue::from_number(val.round()));
                        }
                        let factor = 10f64.powi(digits);
                        Ok(ScriptValue::from_number((val * factor).round() / factor))
                    }),
                ),
            );

            e.define(
                "min",
                ScriptValue::from_native(
                    "min",
                    Arc::new(|args, _guard| {
                        if args.len() < 2 {
                            return Err(ScriptTrap::new(
                                "TypeError: min() requires at least 2 arguments",
                                0,
                            ));
                        }
                        let mut m = args[0].as_number();
                        for a in &args[1..] {
                            m = m.min(a.as_number());
                        }
                        Ok(ScriptValue::from_number(m))
                    }),
                ),
            );

            e.define(
                "max",
                ScriptValue::from_native(
                    "max",
                    Arc::new(|args, _guard| {
                        if args.len() < 2 {
                            return Err(ScriptTrap::new(
                                "TypeError: max() requires at least 2 arguments",
                                0,
                            ));
                        }
                        let mut m = args[0].as_number();
                        for a in &args[1..] {
                            m = m.max(a.as_number());
                        }
                        Ok(ScriptValue::from_number(m))
                    }),
                ),
            );

            e.define(
                "abs",
                ScriptValue::from_native(
                    "abs",
                    Arc::new(|args, _guard| {
                        if args.len() != 1 {
                            return Err(ScriptTrap::new(
                                "TypeError: abs() takes exactly 1 argument",
                                0,
                            ));
                        }
                        Ok(ScriptValue::from_number(args[0].as_number().abs()))
                    }),
                ),
            );

            e.define(
                "range",
                ScriptValue::from_native(
                    "range",
                    Arc::new(|args, guard| {
                        if args.is_empty() || args.len() > 3 {
                            return Err(ScriptTrap::new(
                                "TypeError: range() expects 1 to 3 arguments",
                                0,
                            ));
                        }
                        let mut start = 0i64;
                        let stop: i64;
                        let mut step = 1i64;
                        if args.len() == 1 {
                            stop = args[0].as_number() as i64;
                        } else if args.len() == 2 {
                            start = args[0].as_number() as i64;
                            stop = args[1].as_number() as i64;
                        } else {
                            start = args[0].as_number() as i64;
                            stop = args[1].as_number() as i64;
                            step = args[2].as_number() as i64;
                        }
                        if step == 0 {
                            return Err(ScriptTrap::new(
                                "ValueError: range() arg 3 must not be zero",
                                0,
                            ));
                        }
                        let count = if (step > 0 && stop > start) || (step < 0 && stop < start) {
                            ((stop - start).abs() / step.abs()) as usize
                        } else {
                            0
                        };
                        if count > 10_000 {
                            return Err(ScriptTrap::new(
                                "ResourceError: range() cannot exceed 10,000 items in sandbox",
                                0,
                            ));
                        }
                        guard.alloc_bytes(count * 32, 0)?;
                        let mut list = Vec::with_capacity(count);
                        let mut i = start;
                        while (step > 0 && i < stop) || (step < 0 && i > stop) {
                            list.push(ScriptValue::from_number(i as f64));
                            i += step;
                        }
                        Ok(ScriptValue::from_list(list))
                    }),
                ),
            );

            let inner_print = Arc::clone(&self.inner);
            e.define(
                "print",
                ScriptValue::from_native(
                    "print",
                    Arc::new(move |args, _guard| {
                        let msg = args
                            .iter()
                            .map(|a| a.to_display_string())
                            .collect::<Vec<_>>()
                            .join(" ");
                        let pid = inner_print.lock().plugin_id.clone();
                        PluginLogger::log_event(&pid, "CONSOLE", "print", true, &msg);
                        Ok(ScriptValue::None)
                    }),
                ),
            );

            // 1. `editor` namespace
            e.define("editor", self.build_editor_namespace());

            // 2. `storage` namespace
            e.define("storage", self.build_storage_namespace());

            // 3. `status` namespace
            e.define("status", self.build_status_namespace());

            // 4. `plugins` namespace
            e.define("plugins", self.build_plugins_namespace());

            // 5. `ui` namespace
            e.define("ui", self.build_ui_namespace());

            // 6. `http` namespace
            e.define("http", self.build_http_namespace());
        }
        env
    }

    fn build_editor_namespace(&self) -> ScriptValue {
        let mut ns = BTreeMap::new();

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_html".to_string(),
            ScriptValue::from_native(
                "editor.get_html",
                Arc::new(move |_args, guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "EDITOR",
                            "get_html",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    PluginLogger::log_event(&lock.plugin_id, "EDITOR", "get_html", true, "");
                    let html = lock.context.current_chapter_html.clone();
                    guard.alloc_bytes(html.len(), 0)?;
                    Ok(ScriptValue::from_string(html))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "set_html".to_string(),
            ScriptValue::from_native(
                "editor.set_html",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_editor_write {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "EDITOR",
                            "set_html",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: editor write access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: editor.set_html(html) requires 1 argument",
                            0,
                        ));
                    }
                    let html = args[0].as_string();
                    PluginLogger::log_event(
                        &lock.plugin_id,
                        "EDITOR",
                        "set_html",
                        true,
                        &format!("bytes={}", html.len()),
                    );
                    lock.context.current_chapter_html = html.clone();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::SetChapterHtml,
                        source_plugin_id: pid,
                        payload: html,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_plain_text".to_string(),
            ScriptValue::from_native(
                "editor.get_plain_text",
                Arc::new(move |_args, guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "EDITOR",
                            "get_plain_text",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    PluginLogger::log_event(&lock.plugin_id, "EDITOR", "get_plain_text", true, "");
                    let txt = lock.context.current_plain_text.clone();
                    guard.alloc_bytes(txt.len(), 0)?;
                    Ok(ScriptValue::from_string(txt))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_selection".to_string(),
            ScriptValue::from_native(
                "editor.get_selection",
                Arc::new(move |_args, guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "EDITOR",
                            "get_selection",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    PluginLogger::log_event(&lock.plugin_id, "EDITOR", "get_selection", true, "");
                    let sel = lock.context.selected_text.clone();
                    guard.alloc_bytes(sel.len(), 0)?;
                    Ok(ScriptValue::from_string(sel))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "replace_selection".to_string(),
            ScriptValue::from_native(
                "editor.replace_selection",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_editor_write {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "EDITOR",
                            "replace_selection",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: editor write access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: editor.replace_selection(text) requires 1 argument",
                            0,
                        ));
                    }
                    let payload = args[0].as_string();
                    PluginLogger::log_event(
                        &lock.plugin_id,
                        "EDITOR",
                        "replace_selection",
                        true,
                        "",
                    );
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::ReplaceSelection,
                        source_plugin_id: pid,
                        payload,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "insert_html".to_string(),
            ScriptValue::from_native(
                "editor.insert_html",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_editor_write {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "EDITOR",
                            "insert_html",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: editor write access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: editor.insert_html(html) requires 1 argument",
                            0,
                        ));
                    }
                    let payload = args[0].as_string();
                    PluginLogger::log_event(&lock.plugin_id, "EDITOR", "insert_html", true, "");
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::InsertHtmlAtCursor,
                        source_plugin_id: pid,
                        payload,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_chapter_title".to_string(),
            ScriptValue::from_native(
                "editor.get_chapter_title",
                Arc::new(move |_args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    Ok(ScriptValue::from_string(
                        lock.context.current_chapter_title.clone(),
                    ))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "set_chapter_title".to_string(),
            ScriptValue::from_native(
                "editor.set_chapter_title",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_editor_write {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor write access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: editor.set_chapter_title(title) requires 1 argument",
                            0,
                        ));
                    }
                    let title = args[0].as_string();
                    lock.context.current_chapter_title = title.clone();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::SetChapterTitle,
                        source_plugin_id: pid,
                        payload: title,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_chapter_count".to_string(),
            ScriptValue::from_native(
                "editor.get_chapter_count",
                Arc::new(move |_args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    Ok(ScriptValue::from_number(lock.context.chapters.len() as f64))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_chapter_info".to_string(),
            ScriptValue::from_native(
                "editor.get_chapter_info",
                Arc::new(move |args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: editor.get_chapter_info(index) requires 1 argument",
                            0,
                        ));
                    }
                    let idx = args[0].as_number() as isize;
                    if idx < 0 || (idx as usize) >= lock.context.chapters.len() {
                        return Err(ScriptTrap::new(
                            "IndexError: chapter index out of range",
                            0,
                        ));
                    }
                    let ch = &lock.context.chapters[idx as usize];
                    let mut d = BTreeMap::new();
                    d.insert("title".to_string(), ScriptValue::from_string(ch.title.clone()));
                    d.insert(
                        "word_count".to_string(),
                        ScriptValue::from_number(ch.word_count as f64),
                    );
                    d.insert("html".to_string(), ScriptValue::from_string(ch.html.clone()));
                    Ok(ScriptValue::from_dict(d))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_work_skin_css".to_string(),
            ScriptValue::from_native(
                "editor.get_work_skin_css",
                Arc::new(move |_args, guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    let css = lock.context.work_skin_css.clone();
                    guard.alloc_bytes(css.len(), 0)?;
                    Ok(ScriptValue::from_string(css))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "set_work_skin_css".to_string(),
            ScriptValue::from_native(
                "editor.set_work_skin_css",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_editor_write {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor write access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: editor.set_work_skin_css(css) requires 1 argument",
                            0,
                        ));
                    }
                    let css = args[0].as_string();
                    lock.context.work_skin_css = css.clone();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::SetWorkSkinCss,
                        source_plugin_id: pid,
                        payload: css,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_word_count".to_string(),
            ScriptValue::from_native(
                "editor.get_word_count",
                Arc::new(move |_args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_editor_read {
                        return Err(ScriptTrap::new(
                            "PermissionError: editor read access is disabled for this plugin",
                            0,
                        ));
                    }
                    Ok(ScriptValue::from_number(lock.context.word_count as f64))
                }),
            ),
        );

        ScriptValue::from_dict(ns)
    }

    fn build_storage_namespace(&self) -> ScriptValue {
        let mut ns = BTreeMap::new();

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get".to_string(),
            ScriptValue::from_native(
                "storage.get",
                Arc::new(move |args, guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_storage {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "STORAGE",
                            "get",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: storage access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.is_empty() {
                        return Err(ScriptTrap::new(
                            "TypeError: storage.get(key, [default]) requires at least 1 argument",
                            0,
                        ));
                    }
                    let key = SandboxInner::validate_storage_key(&args[0].as_string())?;
                    PluginLogger::log_event(&lock.plugin_id, "STORAGE", "get", true, &key);
                    let path = lock.storage_dir().join(format!("{}.dat", key));
                    let Ok(data) = fs::read_to_string(&path) else {
                        if args.len() >= 2 {
                            return Ok(args[1].clone());
                        }
                        return Ok(ScriptValue::None);
                    };
                    guard.alloc_bytes(data.len(), 0)?;
                    Ok(ScriptValue::from_string(data))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "set".to_string(),
            ScriptValue::from_native(
                "storage.set",
                Arc::new(move |args, guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_storage {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "STORAGE",
                            "set",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: storage access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 2 {
                        return Err(ScriptTrap::new(
                            "TypeError: storage.set(key, value) requires 2 arguments",
                            0,
                        ));
                    }
                    let key = SandboxInner::validate_storage_key(&args[0].as_string())?;
                    let val = args[1].as_string();

                    let current_usage = lock.current_storage_bytes();
                    if current_usage + val.len() > guard.limits().max_disk_storage_bytes {
                        return Err(ScriptTrap::new(
                            "StorageQuotaError: plugin exceeded its 5 MB isolated storage quota",
                            0,
                        ));
                    }

                    PluginLogger::log_event(&lock.plugin_id, "STORAGE", "set", true, &key);
                    let path = lock.storage_dir().join(format!("{}.dat", key));
                    if fs::write(&path, val.as_bytes()).is_err() {
                        return Ok(ScriptValue::from_bool(false));
                    }
                    Ok(ScriptValue::from_bool(true))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "remove".to_string(),
            ScriptValue::from_native(
                "storage.remove",
                Arc::new(move |args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_storage {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "STORAGE",
                            "remove",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: storage access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: storage.remove(key) requires 1 argument",
                            0,
                        ));
                    }
                    let key = SandboxInner::validate_storage_key(&args[0].as_string())?;
                    PluginLogger::log_event(&lock.plugin_id, "STORAGE", "remove", true, &key);
                    let path = lock.storage_dir().join(format!("{}.dat", key));
                    Ok(ScriptValue::from_bool(fs::remove_file(&path).is_ok()))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "list_keys".to_string(),
            ScriptValue::from_native(
                "storage.list_keys",
                Arc::new(move |_args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_storage {
                        return Err(ScriptTrap::new(
                            "PermissionError: storage access is disabled for this plugin",
                            0,
                        ));
                    }
                    let mut keys = Vec::new();
                    if let Ok(entries) = fs::read_dir(lock.storage_dir()) {
                        for entry in entries.flatten() {
                            let path = entry.path();
                            if path.extension().and_then(|e| e.to_str()) == Some("dat") {
                                if let Some(stem) = path.file_stem().and_then(|s| s.to_str()) {
                                    keys.push(ScriptValue::from_string(stem.to_string()));
                                }
                            }
                        }
                    }
                    Ok(ScriptValue::from_list(keys))
                }),
            ),
        );

        ScriptValue::from_dict(ns)
    }

    fn build_status_namespace(&self) -> ScriptValue {
        let mut ns = BTreeMap::new();

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "set_badge".to_string(),
            ScriptValue::from_native(
                "status.set_badge",
                Arc::new(move |args, _guard| {
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: status.set_badge(text) requires 1 argument",
                            0,
                        ));
                    }
                    let mut lock = inner.lock();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::ShowStatusBadge,
                        source_plugin_id: pid,
                        payload: args[0].as_string(),
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "clear_badge".to_string(),
            ScriptValue::from_native(
                "status.clear_badge",
                Arc::new(move |_args, _guard| {
                    let mut lock = inner.lock();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::ClearStatusBadge,
                        source_plugin_id: pid,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "notify".to_string(),
            ScriptValue::from_native(
                "status.notify",
                Arc::new(move |args, _guard| {
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: status.notify(message) requires 1 argument",
                            0,
                        ));
                    }
                    let mut lock = inner.lock();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::ShowToastNotification,
                        source_plugin_id: pid,
                        payload: args[0].as_string(),
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        ScriptValue::from_dict(ns)
    }

    fn build_plugins_namespace(&self) -> ScriptValue {
        let mut ns = BTreeMap::new();

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_self_id".to_string(),
            ScriptValue::from_native(
                "plugins.get_self_id",
                Arc::new(move |_args, _guard| {
                    Ok(ScriptValue::from_string(inner.lock().plugin_id.clone()))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "send_message".to_string(),
            ScriptValue::from_native(
                "plugins.send_message",
                Arc::new(move |args, _guard| {
                    if args.len() < 2 {
                        return Err(ScriptTrap::new(
                            "TypeError: plugins.send_message(target_plugin_id, topic, [payload]) requires at least 2 arguments",
                            0,
                        ));
                    }
                    let mut lock = inner.lock();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::SendPluginMessage,
                        source_plugin_id: pid,
                        target_plugin_id: args[0].as_string(),
                        payload: args[1].as_string(),
                        secondary_payload: if args.len() >= 3 {
                            args[2].as_string()
                        } else {
                            String::new()
                        },
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        ScriptValue::from_dict(ns)
    }

    fn build_ui_namespace(&self) -> ScriptValue {
        let mut ns = BTreeMap::new();

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "create_window".to_string(),
            ScriptValue::from_native(
                "ui.create_window",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "UI",
                            "create_window",
                            false,
                            "Permission denied",
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: UI window creation is disabled for this plugin",
                            0,
                        ));
                    }
                    let title = if !args.is_empty() {
                        args[0].as_string()
                    } else {
                        lock.plugin_id.clone()
                    };
                    let width = if args.len() >= 2 {
                        (args[1].as_number() as i32).clamp(200, 1200)
                    } else {
                        440
                    };
                    let height = if args.len() >= 3 {
                        (args[2].as_number() as i32).clamp(150, 900)
                    } else {
                        360
                    };
                    PluginLogger::log_event(&lock.plugin_id, "UI", "create_window", true, &title);
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiCreateWindow,
                        source_plugin_id: pid,
                        payload: title,
                        width,
                        height,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "add_label".to_string(),
            ScriptValue::from_native(
                "ui.add_label",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() < 2 {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.add_label(widget_id, text) requires 2 arguments",
                            0,
                        ));
                    }
                    let wid = args[0].as_string();
                    let text = args[1].as_string();
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiAddLabel,
                        source_plugin_id: pid,
                        payload: wid,
                        secondary_payload: text,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "add_button".to_string(),
            ScriptValue::from_native(
                "ui.add_button",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() < 2 {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.add_button(widget_id, label, [callback_func_name]) requires at least 2 arguments",
                            0,
                        ));
                    }
                    let wid = args[0].as_string();
                    let label = args[1].as_string();
                    let cb = if args.len() >= 3 {
                        args[2].as_string()
                    } else {
                        String::new()
                    };
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiAddButton,
                        source_plugin_id: pid,
                        payload: wid,
                        secondary_payload: label,
                        tertiary_payload: cb,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "add_input".to_string(),
            ScriptValue::from_native(
                "ui.add_input",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.is_empty() {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.add_input(widget_id, [placeholder], [default_value]) requires at least 1 argument",
                            0,
                        ));
                    }
                    let wid = args[0].as_string();
                    let ph = if args.len() >= 2 {
                        args[1].as_string()
                    } else {
                        String::new()
                    };
                    let def = if args.len() >= 3 {
                        args[2].as_string()
                    } else {
                        String::new()
                    };
                    lock.context.ui_widget_values.insert(wid.clone(), def.clone());
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiAddInput,
                        source_plugin_id: pid,
                        payload: wid,
                        secondary_payload: ph,
                        tertiary_payload: def,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "add_text_area".to_string(),
            ScriptValue::from_native(
                "ui.add_text_area",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.is_empty() {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.add_text_area(widget_id, [default_value]) requires at least 1 argument",
                            0,
                        ));
                    }
                    let wid = args[0].as_string();
                    let def = if args.len() >= 2 {
                        args[1].as_string()
                    } else {
                        String::new()
                    };
                    lock.context.ui_widget_values.insert(wid.clone(), def.clone());
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiAddTextArea,
                        source_plugin_id: pid,
                        payload: wid,
                        secondary_payload: def,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "set_text".to_string(),
            ScriptValue::from_native(
                "ui.set_text",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() < 2 {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.set_text(widget_id, text) requires 2 arguments",
                            0,
                        ));
                    }
                    let wid = args[0].as_string();
                    let text = args[1].as_string();
                    lock.context
                        .ui_widget_values
                        .insert(wid.clone(), text.clone());
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiSetWidgetText,
                        source_plugin_id: pid,
                        payload: wid,
                        secondary_payload: text,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "get_text".to_string(),
            ScriptValue::from_native(
                "ui.get_text",
                Arc::new(move |args, _guard| {
                    let lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.get_text(widget_id) requires 1 argument",
                            0,
                        ));
                    }
                    let wid = args[0].as_string();
                    let val = lock
                        .context
                        .ui_widget_values
                        .get(&wid)
                        .cloned()
                        .unwrap_or_default();
                    Ok(ScriptValue::from_string(val))
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "show_window".to_string(),
            ScriptValue::from_native(
                "ui.show_window",
                Arc::new(move |_args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    PluginLogger::log_event(&lock.plugin_id, "UI", "show_window", true, "");
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiShowWindow,
                        source_plugin_id: pid,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "close_window".to_string(),
            ScriptValue::from_native(
                "ui.close_window",
                Arc::new(move |_args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiCloseWindow,
                        source_plugin_id: pid,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "clear_window".to_string(),
            ScriptValue::from_native(
                "ui.clear_window",
                Arc::new(move |_args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    let pid = lock.plugin_id.clone();
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiClearWindow,
                        source_plugin_id: pid,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        let inner = Arc::clone(&self.inner);
        ns.insert(
            "message_box".to_string(),
            ScriptValue::from_native(
                "ui.message_box",
                Arc::new(move |args, _guard| {
                    let mut lock = inner.lock();
                    if !lock.permissions.allow_ui {
                        return Err(ScriptTrap::new(
                            "PermissionError: UI access is disabled for this plugin",
                            0,
                        ));
                    }
                    if args.is_empty() {
                        return Err(ScriptTrap::new(
                            "TypeError: ui.message_box(title, [message]) requires at least 1 argument",
                            0,
                        ));
                    }
                    let pid = lock.plugin_id.clone();
                    let payload = args[0].as_string();
                    let secondary = if args.len() >= 2 {
                        args[1].as_string()
                    } else {
                        args[0].as_string()
                    };
                    lock.pending_actions.push(PluginPendingAction {
                        action_type: PluginActionType::UiShowMessageBox,
                        source_plugin_id: pid,
                        payload,
                        secondary_payload: secondary,
                        ..Default::default()
                    });
                    Ok(ScriptValue::None)
                }),
            ),
        );

        ScriptValue::from_dict(ns)
    }

    fn build_http_namespace(&self) -> ScriptValue {
        let mut ns = BTreeMap::new();

        let inner = Arc::clone(&self.inner);
        let check_and_perform_http = Arc::new(
            move |method: &str,
                  url_str: &str,
                  body: &str,
                  content_type: &str,
                  custom_headers: &BTreeMap<String, String>,
                  guard: &mut crate::guard::ResourceGuard|
                  -> Result<ScriptValue, ScriptTrap> {
                let analysis = crate::permissions::analyze_url_security(url_str);

                let (plugin_id, parsed_url) = {
                    let mut lock = inner.lock();

                    // 1. Homograph / Cyrillic / Non-ASCII Character-ID Spoofing Check (Always enforced!)
                    if analysis.has_homograph_or_non_ascii || analysis.is_punycode_idn {
                        let violation_msg = analysis.violation_summary.clone().unwrap_or_else(|| {
                            format!(
                                "Deceptive Homograph / Spoofed Link Detected! Link Character-ID View: {} (Host Character IDs: {}). This is not allowed as it is an attempt to misguide users.",
                                analysis.character_id_representation,
                                analysis.host_character_id_dump
                            )
                        });
                        lock.trigger_security_quarantine(
                            &violation_msg,
                            &analysis.character_id_representation,
                        );
                        return Err(ScriptTrap::new(
                            format!(
                                "SecurityError: {} The plugin was forcibly terminated and quarantined.",
                                violation_msg
                            ),
                            0,
                        ));
                    }

                    // 2. Parse URL and enforce strict HTTPS-only protocol (Always enforced before network check!)
                    let parsed = url::Url::parse(url_str).map_err(|_| {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "NETWORK",
                            method,
                            false,
                            &format!("Invalid URL: {}", analysis.character_id_representation),
                        );
                        ScriptTrap::new(
                            format!(
                                "NetworkError: Invalid URL '{}' passed to http mediator",
                                analysis.character_id_representation
                            ),
                            0,
                        )
                    })?;

                    let scheme = parsed.scheme().to_lowercase();
                    if scheme != "https" {
                        let violation_msg = format!(
                            "Security Violation: Non-HTTPS communication attempted via '{}://' ('{}'). All plugin communications must strictly use HTTPS. Attempts to degrade communication protections are prohibited.",
                            scheme, analysis.character_id_representation
                        );
                        lock.trigger_security_quarantine(
                            &violation_msg,
                            &analysis.character_id_representation,
                        );
                        return Err(ScriptTrap::new(
                            format!(
                                "SecurityError: {} The plugin was forcibly terminated and quarantined.",
                                violation_msg
                            ),
                            0,
                        ));
                    }

                    let host = parsed.host_str().unwrap_or("").to_lowercase();
                    if host.is_empty() {
                        return Err(ScriptTrap::new(
                            "NetworkError: Missing hostname in URL",
                            0,
                        ));
                    }

                    // 3. Permission & Domain Whitelist Checks
                    if !lock.permissions.allow_network {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "NETWORK",
                            method,
                            false,
                            &format!(
                                "Network access disabled for plugin. Endpoint: {}",
                                analysis.character_id_representation
                            ),
                        );
                        return Err(ScriptTrap::new(
                            "PermissionError: Internet access is disabled for this plugin in Plugin Manager",
                            0,
                        ));
                    }

                    if !lock.permissions.is_domain_allowed(&host) {
                        PluginLogger::log_event(
                            &lock.plugin_id,
                            "NETWORK",
                            method,
                            false,
                            &format!(
                                "Domain '{}' not in allowed whitelist ({})",
                                host, analysis.character_id_representation
                            ),
                        );
                        return Err(ScriptTrap::new(
                            format!(
                                "PermissionError: Domain '{}' is not authorized for this plugin. Approve it in Plugin Manager -> Internet & Domain Whitelist.",
                                host
                            ),
                            0,
                        ));
                    }

                    PluginLogger::log_event(
                        &lock.plugin_id,
                        "NETWORK",
                        method,
                        true,
                        &analysis.character_id_representation,
                    );
                    (lock.plugin_id.clone(), parsed)
                };

                // 4. Build strict zero-redirect HTTPS client (matching Qt ManualRedirectPolicy)
                let client = reqwest::blocking::Client::builder()
                    .redirect(reqwest::redirect::Policy::none())
                    .https_only(true)
                    .timeout(Duration::from_secs(10))
                    .user_agent(format!("EnsemblePluginEngine/1.0 (Plugin: {})", plugin_id))
                    .build()
                    .map_err(|e| ScriptTrap::new(format!("NetworkError: {}", e), 0))?;

                let mut req_builder = if method == "POST" {
                    client
                        .post(parsed_url.clone())
                        .header(reqwest::header::CONTENT_TYPE, content_type)
                        .body(body.to_string())
                } else {
                    client.get(parsed_url.clone())
                };

                for (hk, hv) in custom_headers {
                    req_builder = req_builder.header(hk.as_str(), hv.as_str());
                }

                let resp = match req_builder.send() {
                    Ok(r) => r,
                    Err(err) => {
                        let mut res = BTreeMap::new();
                        res.insert("status".to_string(), ScriptValue::from_number(0.0));
                        res.insert("ok".to_string(), ScriptValue::from_bool(false));
                        res.insert("body".to_string(), ScriptValue::from_string(""));
                        res.insert(
                            "error".to_string(),
                            ScriptValue::from_string(err.to_string()),
                        );
                        return Ok(ScriptValue::from_dict(res));
                    }
                };

                let status_code = resp.status().as_u16();
                let redirect_header = resp
                    .headers()
                    .get(reqwest::header::LOCATION)
                    .or_else(|| resp.headers().get(reqwest::header::REFRESH))
                    .and_then(|v| v.to_str().ok())
                    .unwrap_or("")
                    .to_string();

                // 5. Anti-Redirection / Anti-Diversion Enforcement:
                // Any 3xx redirect (except 304 Not Modified) or Location/Refresh header terminates & quarantines the plugin.
                if ((300..400).contains(&status_code) && status_code != 304)
                    || !redirect_header.is_empty()
                {
                    let raw_target = if redirect_header.is_empty() {
                        "<unknown diversion endpoint>".to_string()
                    } else if let Ok(resolved) = parsed_url.join(&redirect_header) {
                        resolved.to_string()
                    } else {
                        redirect_header
                    };
                    let target_analysis = crate::permissions::analyze_url_security(&raw_target);
                    let safe_target_display = if target_analysis.has_homograph_or_non_ascii {
                        format!(
                            "{} [Spoofed Character IDs: {}]",
                            target_analysis.character_id_representation,
                            target_analysis.host_character_id_dump
                        )
                    } else {
                        target_analysis.character_id_representation
                    };

                    let violation_msg = format!(
                        "Security Warning: The plugin or plugin provider or the link that the plugin used ({}) attempted to redivert to a different website ({}). Ensemble does not cater to malicious or redirecting services. The plugin was forcibly terminated and quarantined for a security violation. Report this immediately to Ensemble developers regarding this incident.",
                        analysis.character_id_representation, safe_target_display
                    );
                    inner
                        .lock()
                        .trigger_security_quarantine(&violation_msg, &safe_target_display);
                    return Err(ScriptTrap::new(
                        format!("SecurityError: {}", violation_msg),
                        0,
                    ));
                }

                let mut raw_bytes = resp
                    .bytes()
                    .map(|b| b.to_vec())
                    .unwrap_or_default();
                if raw_bytes.len() > 512 * 1024 {
                    raw_bytes.truncate(512 * 1024);
                }
                guard.alloc_bytes(raw_bytes.len(), 0)?;
                let body_str = String::from_utf8_lossy(&raw_bytes).into_owned();

                PluginLogger::log_event(
                    &plugin_id,
                    "HTTP_RESPONSE",
                    method,
                    true,
                    &format!("Status: {}, size: {} bytes", status_code, raw_bytes.len()),
                );

                let mut res = BTreeMap::new();
                res.insert(
                    "status".to_string(),
                    ScriptValue::from_number(status_code as f64),
                );
                res.insert(
                    "ok".to_string(),
                    ScriptValue::from_bool((200..300).contains(&status_code)),
                );
                res.insert("body".to_string(), ScriptValue::from_string(body_str));
                res.insert("error".to_string(), ScriptValue::from_string(""));
                Ok(ScriptValue::from_dict(res))
            },
        );

        let http_fn = Arc::clone(&check_and_perform_http);
        ns.insert(
            "get".to_string(),
            ScriptValue::from_native(
                "http.get",
                Arc::new(move |args, guard| {
                    if args.is_empty() || args.len() > 2 {
                        return Err(ScriptTrap::new(
                            "TypeError: http.get(url, [headers_dict]) requires 1 or 2 arguments",
                            0,
                        ));
                    }
                    let mut headers = BTreeMap::new();
                    if args.len() == 2 {
                        if let ScriptValue::Dict(d) = &args[1] {
                            for (k, v) in d.lock().iter() {
                                headers.insert(k.clone(), v.as_string());
                            }
                        }
                    }
                    http_fn("GET", &args[0].as_string(), "", "text/plain", &headers, guard)
                }),
            ),
        );

        let http_fn = Arc::clone(&check_and_perform_http);
        ns.insert(
            "post".to_string(),
            ScriptValue::from_native(
                "http.post",
                Arc::new(move |args, guard| {
                    if args.is_empty() || args.len() > 3 {
                        return Err(ScriptTrap::new(
                            "TypeError: http.post(url, [body], [content_type_or_headers]) requires 1 to 3 arguments",
                            0,
                        ));
                    }
                    let body = if args.len() >= 2 {
                        args[1].as_string()
                    } else {
                        String::new()
                    };
                    let mut ct = "application/x-www-form-urlencoded".to_string();
                    let mut headers = BTreeMap::new();
                    if args.len() >= 3 {
                        match &args[2] {
                            ScriptValue::Dict(d) => {
                                for (k, v) in d.lock().iter() {
                                    if k.eq_ignore_ascii_case("content-type") {
                                        ct = v.as_string();
                                    } else {
                                        headers.insert(k.clone(), v.as_string());
                                    }
                                }
                            }
                            other => {
                                ct = other.as_string();
                            }
                        }
                    }
                    http_fn("POST", &args[0].as_string(), &body, &ct, &headers, guard)
                }),
            ),
        );

        ns.insert(
            "inspect_link".to_string(),
            ScriptValue::from_native(
                "http.inspect_link",
                Arc::new(move |args, _guard| {
                    if args.len() != 1 {
                        return Err(ScriptTrap::new(
                            "TypeError: http.inspect_link(url) requires 1 argument",
                            0,
                        ));
                    }
                    let raw = args[0].as_string();
                    let analysis = crate::permissions::analyze_url_security(&raw);
                    let mut d = BTreeMap::new();
                    let safe = analysis.is_valid_url
                        && analysis.is_https
                        && !analysis.has_homograph_or_non_ascii
                        && !analysis.is_punycode_idn;
                    d.insert("safe".to_string(), ScriptValue::from_bool(safe));
                    d.insert("https".to_string(), ScriptValue::from_bool(analysis.is_https));
                    d.insert("host".to_string(), ScriptValue::from_string(analysis.raw_host));
                    d.insert(
                        "homograph_detected".to_string(),
                        ScriptValue::from_bool(
                            analysis.has_homograph_or_non_ascii || analysis.is_punycode_idn,
                        ),
                    );
                    d.insert(
                        "character_id_link".to_string(),
                        ScriptValue::from_string(analysis.character_id_representation),
                    );
                    d.insert(
                        "host_character_ids".to_string(),
                        ScriptValue::from_string(analysis.host_character_id_dump),
                    );
                    d.insert(
                        "warning".to_string(),
                        ScriptValue::from_string(analysis.violation_summary.unwrap_or_default()),
                    );
                    Ok(ScriptValue::from_dict(d))
                }),
            ),
        );

        ScriptValue::from_dict(ns)
    }
}
