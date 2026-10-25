use crate::evaluator::ScriptEvaluator;
use crate::guard::{ResourceGuard, ResourceLimits};
use crate::lexer::ScriptLexer;
use crate::logger::PluginLogger;
use crate::parser::{AstNode, ScriptParser};
use crate::permissions::{PluginPermissions, PluginScanReport, PluginScanner};
use crate::sandbox::{PluginActionType, PluginHostContext, PluginPendingAction, PluginSandbox};
use crate::value::ScriptValue;
use ensemble_core::paths::AppPaths;
use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct PluginMetadata {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub file_path: String,
    pub enabled: bool,
    pub quarantined: bool,
    pub quarantine_reason: String,
    pub last_error: String,
    pub peak_memory_bytes: usize,
    pub last_exec_ms: u64,
    pub permissions: PluginPermissions,
    pub scan_report: PluginScanReport,
}

impl Default for PluginMetadata {
    fn default() -> Self {
        Self {
            id: String::new(),
            name: String::new(),
            version: "1.0.0".to_string(),
            author: "Local Author".to_string(),
            description: String::new(),
            file_path: String::new(),
            enabled: true,
            quarantined: false,
            quarantine_reason: String::new(),
            last_error: String::new(),
            peak_memory_bytes: 0,
            last_exec_ms: 0,
            permissions: PluginPermissions::default(),
            scan_report: PluginScanReport::default(),
        }
    }
}

#[derive(Debug, Clone)]
pub struct PluginToolbarAction {
    pub plugin_id: String,
    pub label: String,
    pub func_name: String,
}

struct LoadedPluginInstance {
    meta: PluginMetadata,
    secret_uidp: String,
    sandbox: PluginSandbox,
    guard: ResourceGuard,
    evaluator: Option<ScriptEvaluator>,
    #[allow(dead_code)]
    program: Option<AstNode>,
}

#[derive(Debug, Clone)]
pub struct SecurityQuarantineEvent {
    pub plugin_id: String,
    pub plugin_name: String,
    pub reason: String,
    pub target_url: String,
}

pub struct PluginManager {
    instances: BTreeMap<String, LoadedPluginInstance>,
    emitted_actions: Vec<PluginPendingAction>,
    emitted_errors: Vec<(String, String)>,
    emitted_quarantines: Vec<SecurityQuarantineEvent>,
}

impl PluginManager {
    pub fn new() -> Self {
        Self {
            instances: BTreeMap::new(),
            emitted_actions: Vec::new(),
            emitted_errors: Vec::new(),
            emitted_quarantines: Vec::new(),
        }
    }

    pub fn plugins_dir() -> PathBuf {
        let dir = AppPaths::plugins_dir();
        let _ = fs::create_dir_all(&dir);
        dir
    }

    pub fn manifest_path() -> PathBuf {
        Self::plugins_dir().join("manifest.json")
    }

    pub fn take_emitted_actions(&mut self) -> Vec<PluginPendingAction> {
        std::mem::take(&mut self.emitted_actions)
    }

    pub fn take_emitted_errors(&mut self) -> Vec<(String, String)> {
        std::mem::take(&mut self.emitted_errors)
    }

    pub fn take_emitted_quarantines(&mut self) -> Vec<SecurityQuarantineEvent> {
        std::mem::take(&mut self.emitted_quarantines)
    }

    pub fn initialize_and_load_all(&mut self) {
        let dir = Self::plugins_dir();
        let manifest_file = Self::manifest_path();

        let mut manifest_root: serde_json::Map<String, serde_json::Value> =
            if let Ok(bytes) = fs::read(&manifest_file) {
                serde_json::from_slice(&bytes).unwrap_or_default()
            } else {
                serde_json::Map::new()
            };

        let mut er_files: Vec<PathBuf> = Vec::new();
        if let Ok(entries) = fs::read_dir(&dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_file() && path.extension().and_then(|e| e.to_str()) == Some("er") {
                    er_files.push(path);
                }
            }
        }
        er_files.sort();

        if er_files.is_empty() && manifest_root.is_empty() {
            self.ensure_sample_plugin_exists();
            if let Ok(entries) = fs::read_dir(&dir) {
                for entry in entries.flatten() {
                    let path = entry.path();
                    if path.is_file() && path.extension().and_then(|e| e.to_str()) == Some("er") {
                        er_files.push(path);
                    }
                }
            }
            er_files.sort();
        }

        let mut manifest_dirty = false;
        for file_path in er_files {
            let Some(plugin_id) = file_path
                .file_stem()
                .and_then(|s| s.to_str())
                .map(|s| s.to_string())
            else {
                continue;
            };

            let mut entry = manifest_root
                .get(&plugin_id)
                .and_then(|v| v.as_object())
                .cloned()
                .unwrap_or_default();

            let mut secret_uidp = entry
                .get("secret_uidp")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            if secret_uidp.is_empty() {
                secret_uidp = Self::generate_secret_uidp();
                entry.insert(
                    "secret_uidp".to_string(),
                    serde_json::Value::String(secret_uidp.clone()),
                );
                entry.insert("enabled".to_string(), serde_json::Value::Bool(true));
                manifest_root.insert(plugin_id.clone(), serde_json::Value::Object(entry.clone()));
                manifest_dirty = true;
            }

            let enabled = entry
                .get("enabled")
                .and_then(|v| v.as_bool())
                .unwrap_or(true);
            let quarantined = entry
                .get("quarantined")
                .and_then(|v| v.as_bool())
                .unwrap_or(false);
            let quarantine_reason = entry
                .get("quarantine_reason")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();

            let Ok(source) = fs::read_to_string(&file_path) else {
                continue;
            };

            let perms = Self::load_permissions_from_json(&entry);
            let _ = self.load_plugin_from_source(
                &plugin_id,
                &plugin_id,
                &source,
                &file_path.to_string_lossy(),
                &secret_uidp,
            );

            if let Some(inst) = self.instances.get_mut(&plugin_id) {
                let mut merged_perms = perms;
                for d in &inst.meta.scan_report.detected_domains {
                    merged_perms.allowed_domains.entry(d.clone()).or_insert(false);
                }
                inst.meta.permissions = merged_perms.clone();
                inst.sandbox.set_permissions(merged_perms);

                if quarantined {
                    inst.meta.quarantined = true;
                    inst.meta.enabled = false;
                    inst.meta.quarantine_reason = quarantine_reason.clone();
                    inst.meta.last_error = quarantine_reason;
                } else if !enabled {
                    inst.meta.enabled = false;
                }
            }
        }

        if manifest_dirty {
            if let Ok(json_bytes) = serde_json::to_vec_pretty(&manifest_root) {
                let _ = fs::write(&manifest_file, json_bytes);
            }
        }
    }

    fn load_permissions_from_json(
        entry: &serde_json::Map<String, serde_json::Value>,
    ) -> PluginPermissions {
        let mut perms = PluginPermissions::default();
        if let Some(p_obj) = entry.get("permissions").and_then(|v| v.as_object()) {
            perms.allow_editor_read = p_obj
                .get("allow_editor_read")
                .and_then(|v| v.as_bool())
                .unwrap_or(true);
            perms.allow_editor_write = p_obj
                .get("allow_editor_write")
                .and_then(|v| v.as_bool())
                .unwrap_or(true);
            perms.allow_storage = p_obj
                .get("allow_storage")
                .and_then(|v| v.as_bool())
                .unwrap_or(true);
            perms.allow_ui = p_obj
                .get("allow_ui")
                .and_then(|v| v.as_bool())
                .unwrap_or(true);
            perms.allow_network = p_obj
                .get("allow_network")
                .and_then(|v| v.as_bool())
                .unwrap_or(false);
            if let Some(dom_obj) = p_obj.get("allowed_domains").and_then(|v| v.as_object()) {
                for (k, v) in dom_obj {
                    perms
                        .allowed_domains
                        .insert(k.to_lowercase(), v.as_bool().unwrap_or(false));
                }
            }
        }
        perms
    }

    pub fn load_plugin_from_file(&mut self, file_path: &Path) -> Result<(), String> {
        let content = fs::read_to_string(file_path)
            .map_err(|e| format!("Cannot open plugin file: {}", e))?;

        let plugin_id = file_path
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("plugin")
            .to_string();

        let dest_dir = Self::plugins_dir();
        let dest_path = dest_dir.join(
            file_path
                .file_name()
                .unwrap_or_else(|| std::ffi::OsStr::new("plugin.er")),
        );

        let final_path = if file_path != dest_path {
            let _ = fs::remove_file(&dest_path);
            if fs::copy(file_path, &dest_path).is_ok() {
                dest_path
            } else {
                file_path.to_path_buf()
            }
        } else {
            file_path.to_path_buf()
        };

        let secret = self.get_or_create_secret_uidp(&plugin_id);
        self.load_plugin_from_source(
            &plugin_id,
            &plugin_id,
            &content,
            &final_path.to_string_lossy(),
            &secret,
        )
    }

    pub fn load_plugin_from_source(
        &mut self,
        plugin_id: &str,
        name: &str,
        source_code: &str,
        file_path: &str,
        secret_uidp_override: &str,
    ) -> Result<(), String> {
        let mut meta = Self::parse_header_metadata(source_code, plugin_id, file_path);
        if meta.name == plugin_id && !name.is_empty() {
            meta.name = name.to_string();
        }

        meta.scan_report = PluginScanner::scan_script(source_code);
        for d in &meta.scan_report.detected_domains {
            meta.permissions
                .allowed_domains
                .entry(d.clone())
                .or_insert(false);
        }

        if let Some(existing) = self.instances.get(plugin_id) {
            let mut merged = existing.meta.permissions.clone();
            for d in &meta.scan_report.detected_domains {
                merged.allowed_domains.entry(d.clone()).or_insert(false);
            }
            meta.permissions = merged;
        }

        let secret = if secret_uidp_override.is_empty() {
            self.get_or_create_secret_uidp(plugin_id)
        } else {
            secret_uidp_override.to_string()
        };

        let sandbox = PluginSandbox::new(plugin_id, &secret);
        sandbox.set_permissions(meta.permissions.clone());
        let mut guard = ResourceGuard::new(ResourceLimits::default());

        let init_result = (|| -> Result<(ScriptEvaluator, AstNode), crate::guard::ScriptTrap> {
            let mut lexer = ScriptLexer::new(source_code);
            let tokens = lexer.tokenize()?;
            let mut parser = ScriptParser::new(tokens);
            let program = parser.parse_program()?;

            let root_env = sandbox.build_root_environment();
            let mut evaluator = ScriptEvaluator::new(root_env);
            guard.reset();
            evaluator.execute_program(&program, &mut guard)?;
            Ok((evaluator, program))
        })();

        match init_result {
            Ok((evaluator, program)) => {
                meta.peak_memory_bytes = guard.peak_memory_bytes();
                meta.last_exec_ms = guard.elapsed_ms();
                meta.last_error.clear();

                let inst = LoadedPluginInstance {
                    meta,
                    secret_uidp: secret,
                    sandbox,
                    guard,
                    evaluator: Some(evaluator),
                    program: Some(program),
                };
                self.instances.insert(plugin_id.to_string(), inst);
                PluginLogger::log_event(plugin_id, "LIFECYCLE", "loaded", true, file_path);
                self.check_and_handle_security_violation(plugin_id);
                self.save_manifest();
                Ok(())
            }
            Err(trap) => {
                let err_str = trap.to_string();
                meta.last_error = err_str.clone();
                meta.enabled = false;
                let inst = LoadedPluginInstance {
                    meta,
                    secret_uidp: secret,
                    sandbox,
                    guard,
                    evaluator: None,
                    program: None,
                };
                self.instances.insert(plugin_id.to_string(), inst);
                self.check_and_handle_security_violation(plugin_id);
                self.emitted_errors
                    .push((plugin_id.to_string(), err_str.clone()));
                Err(err_str)
            }
        }
    }

    pub fn unload_plugin(&mut self, plugin_id: &str) {
        self.instances.remove(plugin_id);
        PluginLogger::log_event(plugin_id, "LIFECYCLE", "unloaded", true, "");
    }

    pub fn uninstall_plugin(&mut self, plugin_id: &str) -> Result<(), String> {
        let Some(inst) = self.instances.remove(plugin_id) else {
            return Err(format!("Plugin '{}' not found.", plugin_id));
        };

        let file_path = inst.meta.file_path.clone();
        let secret = inst.secret_uidp.clone();

        if !file_path.is_empty() {
            let p = Path::new(&file_path);
            if p.exists() {
                fs::remove_file(p)
                    .map_err(|e| format!("Failed to delete plugin script file: {}", e))?;
            }
        }

        if !secret.is_empty() {
            let store_dir = AppPaths::plugin_store_dir().join(&secret);
            if store_dir.exists() {
                let _ = fs::remove_dir_all(&store_dir);
            }
        }

        let manifest_file = Self::manifest_path();
        if let Ok(bytes) = fs::read(&manifest_file) {
            if let Ok(mut root) =
                serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(&bytes)
            {
                root.remove(plugin_id);
                if let Ok(out) = serde_json::to_vec_pretty(&root) {
                    let _ = fs::write(&manifest_file, out);
                }
            }
        }

        PluginLogger::log_event(plugin_id, "LIFECYCLE", "uninstalled", true, &file_path);
        Ok(())
    }

    pub fn set_plugin_enabled(&mut self, plugin_id: &str, enabled: bool) {
        if let Some(inst) = self.instances.get_mut(plugin_id) {
            if enabled && inst.meta.quarantined {
                inst.meta.quarantined = false;
                inst.meta.quarantine_reason.clear();
                inst.sandbox.clear_security_violation();
                PluginLogger::log_event(
                    plugin_id,
                    "SECURITY",
                    "unquarantined_by_user",
                    true,
                    "",
                );
            }
            inst.meta.enabled = enabled;
            self.save_manifest();
        }
    }

    pub fn update_plugin_permissions(&mut self, plugin_id: &str, perms: PluginPermissions) {
        if let Some(inst) = self.instances.get_mut(plugin_id) {
            inst.meta.permissions = perms.clone();
            inst.sandbox.set_permissions(perms);
            PluginLogger::log_event(
                plugin_id,
                "PERMISSIONS",
                "updated",
                true,
                "User modified plugin permissions",
            );
            self.save_manifest();
        }
    }

    pub fn plugins(&self) -> Vec<PluginMetadata> {
        self.instances.values().map(|i| i.meta.clone()).collect()
    }

    pub fn toolbar_actions(&self) -> Vec<PluginToolbarAction> {
        let mut actions = Vec::new();
        for (id, inst) in &self.instances {
            if !inst.meta.enabled {
                continue;
            }
            if let Some(ref eval) = inst.evaluator {
                for (label, fn_node) in eval.on_toolbar_click_funcs() {
                    actions.push(PluginToolbarAction {
                        plugin_id: id.clone(),
                        label: label.clone(),
                        func_name: fn_node.name.clone(),
                    });
                }
            }
        }
        actions
    }

    pub fn notify_chapter_changed(&mut self, ctx: &PluginHostContext) {
        let ids: Vec<String> = self.instances.keys().cloned().collect();
        let mut all_actions = Vec::new();

        for id in ids {
            let mut trap_err = None;
            {
                let Some(inst) = self.instances.get_mut(&id) else {
                    continue;
                };
                if !inst.meta.enabled {
                    continue;
                }
                let Some(ref mut eval) = inst.evaluator else {
                    continue;
                };

                let mut merged_ctx = ctx.clone();
                merged_ctx.ui_widget_values = inst.sandbox.host_context().ui_widget_values;
                inst.sandbox.set_host_context(merged_ctx);

                let funcs = eval.on_chapter_change_funcs().to_vec();
                for fn_node in funcs {
                    inst.guard.reset();
                    let args = if fn_node.parameters.len() == 1 {
                        vec![ScriptValue::from_number(ctx.current_chapter_index as f64)]
                    } else {
                        Vec::new()
                    };
                    if let Err(trap) = eval.invoke_function(&fn_node, &args, &mut inst.guard) {
                        trap_err = Some(trap.to_string());
                        break;
                    }
                    inst.meta.peak_memory_bytes = inst
                        .meta
                        .peak_memory_bytes
                        .max(inst.guard.peak_memory_bytes());
                    inst.meta.last_exec_ms = inst.guard.elapsed_ms();
                }

                let pending = inst.sandbox.take_pending_actions();
                all_actions.extend(pending);
            }

            if self.check_and_handle_security_violation(&id) {
                continue;
            }
            if let Some(err) = trap_err {
                if let Some(inst) = self.instances.get_mut(&id) {
                    inst.meta.last_error = err.clone();
                }
                self.emitted_errors.push((id, err));
            }
        }

        if !all_actions.is_empty() {
            self.dispatch_pending_actions(all_actions, ctx);
        }
    }

    pub fn trigger_toolbar_action(
        &mut self,
        plugin_id: &str,
        func_name: &str,
        ctx: &PluginHostContext,
    ) {
        let mut all_actions = Vec::new();
        let mut trap_err = None;

        if let Some(inst) = self.instances.get_mut(plugin_id) {
            if !inst.meta.enabled {
                return;
            }
            if let Some(ref mut eval) = inst.evaluator {
                let mut merged_ctx = ctx.clone();
                merged_ctx.ui_widget_values = inst.sandbox.host_context().ui_widget_values;
                inst.sandbox.set_host_context(merged_ctx);

                let funcs = eval.on_toolbar_click_funcs().to_vec();
                for (_label, fn_node) in funcs {
                    if fn_node.name == func_name {
                        inst.guard.reset();
                        if let Err(trap) = eval.invoke_function(&fn_node, &[], &mut inst.guard) {
                            trap_err = Some(trap.to_string());
                        } else {
                            inst.meta.peak_memory_bytes = inst
                                .meta
                                .peak_memory_bytes
                                .max(inst.guard.peak_memory_bytes());
                            inst.meta.last_exec_ms = inst.guard.elapsed_ms();
                            inst.meta.last_error.clear();
                        }
                        break;
                    }
                }
                all_actions = inst.sandbox.take_pending_actions();
            }
        }

        if self.check_and_handle_security_violation(plugin_id) {
            return;
        }
        if let Some(err) = trap_err {
            if let Some(inst) = self.instances.get_mut(plugin_id) {
                inst.meta.last_error = err.clone();
            }
            self.emitted_errors.push((plugin_id.to_string(), err));
            return;
        }

        if !all_actions.is_empty() {
            self.dispatch_pending_actions(all_actions, ctx);
        }
    }

    pub fn trigger_ui_widget_callback(
        &mut self,
        plugin_id: &str,
        callback_func_name: &str,
        widget_id: &str,
        ctx: &PluginHostContext,
    ) {
        let mut all_actions = Vec::new();
        let mut trap_err = None;

        if let Some(inst) = self.instances.get_mut(plugin_id) {
            if !inst.meta.enabled || callback_func_name.is_empty() {
                return;
            }
            if let Some(ref mut eval) = inst.evaluator {
                let mut merged_ctx = ctx.clone();
                for (k, v) in inst.sandbox.host_context().ui_widget_values {
                    merged_ctx.ui_widget_values.entry(k).or_insert(v);
                }
                inst.sandbox.set_host_context(merged_ctx);

                let env = Arc::clone(eval.global_env());
                let has_fn = env.lock().has(callback_func_name);
                if has_fn {
                    let maybe_fn = env.lock().get(callback_func_name, 0);
                    if let Ok(ScriptValue::UserFunc(fn_node)) = maybe_fn {
                        inst.guard.reset();
                        let args = if fn_node.parameters.len() == 1 {
                            vec![ScriptValue::from_string(widget_id.to_string())]
                        } else {
                            Vec::new()
                        };
                        if let Err(trap) = eval.invoke_function(&fn_node, &args, &mut inst.guard) {
                            trap_err = Some(trap.to_string());
                        } else {
                            inst.meta.peak_memory_bytes = inst
                                .meta
                                .peak_memory_bytes
                                .max(inst.guard.peak_memory_bytes());
                            inst.meta.last_exec_ms = inst.guard.elapsed_ms();
                            inst.meta.last_error.clear();
                        }
                    }
                }
                all_actions = inst.sandbox.take_pending_actions();
            }
        }

        if self.check_and_handle_security_violation(plugin_id) {
            return;
        }
        if let Some(err) = trap_err {
            if let Some(inst) = self.instances.get_mut(plugin_id) {
                inst.meta.last_error = err.clone();
            }
            self.emitted_errors.push((plugin_id.to_string(), err));
            return;
        }

        if !all_actions.is_empty() {
            self.dispatch_pending_actions(all_actions, ctx);
        }
    }

    pub fn route_plugin_message(
        &mut self,
        source_plugin_id: &str,
        target_plugin_id: &str,
        topic: &str,
        payload: &str,
        ctx: &PluginHostContext,
    ) {
        if source_plugin_id == target_plugin_id {
            return;
        }

        let mut follow_up = Vec::new();
        let mut trap_err = None;

        if let Some(inst) = self.instances.get_mut(target_plugin_id) {
            if !inst.meta.enabled {
                return;
            }
            if let Some(ref mut eval) = inst.evaluator {
                inst.sandbox.set_host_context(ctx.clone());
                let funcs = eval.on_message_funcs().to_vec();
                for fn_node in funcs {
                    inst.guard.reset();
                    let mut args = Vec::new();
                    if fn_node.parameters.len() >= 1 {
                        args.push(ScriptValue::from_string(source_plugin_id.to_string()));
                    }
                    if fn_node.parameters.len() >= 2 {
                        args.push(ScriptValue::from_string(topic.to_string()));
                    }
                    if fn_node.parameters.len() >= 3 {
                        args.push(ScriptValue::from_string(payload.to_string()));
                    }
                    if let Err(trap) = eval.invoke_function(&fn_node, &args, &mut inst.guard) {
                        trap_err = Some(trap.to_string());
                        break;
                    }
                }
                follow_up = inst.sandbox.take_pending_actions();
            }
        }

        if self.check_and_handle_security_violation(target_plugin_id) {
            return;
        }
        if let Some(err) = trap_err {
            if let Some(inst) = self.instances.get_mut(target_plugin_id) {
                inst.meta.last_error = err.clone();
            }
            self.emitted_errors
                .push((target_plugin_id.to_string(), err));
            return;
        }

        for act in follow_up {
            if act.action_type != PluginActionType::SendPluginMessage {
                self.emitted_actions.push(act);
            }
        }
    }

    fn dispatch_pending_actions(
        &mut self,
        actions: Vec<PluginPendingAction>,
        ctx: &PluginHostContext,
    ) {
        let mut host_actions = Vec::new();
        let mut ipc_messages = Vec::new();

        for act in actions {
            if act.action_type == PluginActionType::SendPluginMessage {
                ipc_messages.push(act);
            } else {
                host_actions.push(act);
            }
        }

        if !host_actions.is_empty() {
            self.emitted_actions.extend(host_actions);
        }

        for msg in ipc_messages {
            self.route_plugin_message(
                &msg.source_plugin_id,
                &msg.target_plugin_id,
                &msg.payload,
                &msg.secondary_payload,
                ctx,
            );
        }
    }

    fn check_and_handle_security_violation(&mut self, plugin_id: &str) -> bool {
        let violation_info = if let Some(inst) = self.instances.get_mut(plugin_id) {
            if inst.sandbox.has_security_violation() {
                let reason = inst.sandbox.security_violation_reason();
                let target_url = inst.sandbox.security_violation_target_url();
                let plugin_name = if inst.meta.name.is_empty() {
                    plugin_id.to_string()
                } else {
                    inst.meta.name.clone()
                };

                inst.meta.enabled = false;
                inst.meta.quarantined = true;
                inst.meta.quarantine_reason = reason.clone();
                inst.meta.last_error = format!("QUARANTINED (Security Violation): {}", reason);
                let _ = inst.sandbox.take_pending_actions();
                Some((plugin_name, reason, target_url))
            } else {
                None
            }
        } else {
            None
        };

        if let Some((plugin_name, reason, target_url)) = violation_info {
            self.save_manifest();
            self.emitted_quarantines.push(SecurityQuarantineEvent {
                plugin_id: plugin_id.to_string(),
                plugin_name,
                reason,
                target_url,
            });
            true
        } else {
            false
        }
    }

    fn parse_header_metadata(
        source_code: &str,
        fallback_id: &str,
        file_path: &str,
    ) -> PluginMetadata {
        let mut meta = PluginMetadata {
            id: fallback_id.to_string(),
            name: fallback_id.to_string(),
            file_path: file_path.to_string(),
            ..Default::default()
        };

        for line in source_code.lines().take(25) {
            let trimmed = line.trim();
            if !trimmed.starts_with('#') {
                if !trimmed.is_empty() {
                    break;
                }
                continue;
            }
            let comment = trimmed[1..].trim();
            if let Some(colon) = comment.find(':') {
                let key = comment[..colon].trim().to_lowercase();
                let val = comment[colon + 1..].trim().to_string();
                match key.as_str() {
                    "@name" | "name" => meta.name = val,
                    "@version" | "version" => meta.version = val,
                    "@author" | "author" => meta.author = val,
                    "@description" | "description" => meta.description = val,
                    _ => {}
                }
            }
        }
        meta
    }

    fn generate_secret_uidp() -> String {
        let u1 = Uuid::new_v4().simple().to_string();
        let u2 = Uuid::new_v4().simple().to_string();
        format!("uidp_{}{}", u1, u2)
    }

    fn get_or_create_secret_uidp(&self, plugin_id: &str) -> String {
        if let Some(inst) = self.instances.get(plugin_id) {
            if !inst.secret_uidp.is_empty() {
                return inst.secret_uidp.clone();
            }
        }
        let manifest_file = Self::manifest_path();
        if let Ok(bytes) = fs::read(&manifest_file) {
            if let Ok(root) =
                serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(&bytes)
            {
                if let Some(s) = root
                    .get(plugin_id)
                    .and_then(|v| v.get("secret_uidp"))
                    .and_then(|v| v.as_str())
                {
                    if !s.is_empty() {
                        return s.to_string();
                    }
                }
            }
        }
        Self::generate_secret_uidp()
    }

    fn save_manifest(&self) {
        let manifest_file = Self::manifest_path();
        let mut root: serde_json::Map<String, serde_json::Value> =
            if let Ok(bytes) = fs::read(&manifest_file) {
                serde_json::from_slice(&bytes).unwrap_or_default()
            } else {
                serde_json::Map::new()
            };

        for (id, inst) in &self.instances {
            let mut entry = root
                .get(id)
                .and_then(|v| v.as_object())
                .cloned()
                .unwrap_or_default();
            entry.insert(
                "secret_uidp".to_string(),
                serde_json::Value::String(inst.secret_uidp.clone()),
            );
            entry.insert(
                "enabled".to_string(),
                serde_json::Value::Bool(inst.meta.enabled),
            );
            entry.insert(
                "quarantined".to_string(),
                serde_json::Value::Bool(inst.meta.quarantined),
            );
            entry.insert(
                "quarantine_reason".to_string(),
                serde_json::Value::String(inst.meta.quarantine_reason.clone()),
            );
            entry.insert(
                "name".to_string(),
                serde_json::Value::String(inst.meta.name.clone()),
            );
            entry.insert(
                "version".to_string(),
                serde_json::Value::String(inst.meta.version.clone()),
            );
            if let Ok(perms_val) = serde_json::to_value(&inst.meta.permissions) {
                entry.insert("permissions".to_string(), perms_val);
            }
            root.insert(id.clone(), serde_json::Value::Object(entry));
        }

        if let Ok(out) = serde_json::to_vec_pretty(&root) {
            let _ = fs::write(&manifest_file, out);
        }
    }

    fn ensure_sample_plugin_exists(&self) {
        let sample_path = Self::plugins_dir().join("reading_time_helper.er");
        if sample_path.exists() {
            return;
        }
        let sample = concat!(
            "# @name: Reading Time & Dialogue Counter\n",
            "# @version: 1.1.0\n",
            "# @author: Ensemble Team\n",
            "# @description: Calculates estimated reading time and opens a custom plugin stats window.\n",
            "\n",
            "@on_chapter_change\n",
            "def update_reading_badge(chapter_idx):\n",
            "    words = editor.get_word_count()\n",
            "    mins = int(words / 200)\n",
            "    if mins < 1 and words > 0:\n",
            "        mins = 1\n",
            "    status.set_badge(\"~\" + str(mins) + \" min read\")\n",
            "    storage.set(\"last_word_count\", str(words))\n",
            "\n",
            "@on_toolbar_click(\"Chapter Stats Window\")\n",
            "def show_stats_popup():\n",
            "    text = editor.get_plain_text()\n",
            "    words = editor.get_word_count()\n",
            "    title = editor.get_chapter_title()\n",
            "    ui.create_window(\"Chapter Statistics — \" + title, 420, 280)\n",
            "    ui.add_label(\"lbl_title\", \"Chapter: \" + title)\n",
            "    ui.add_label(\"lbl_words\", \"Word Count: \" + str(words))\n",
            "    ui.add_label(\"lbl_chars\", \"Character Count: \" + str(len(text)))\n",
            "    ui.show_window()\n"
        );
        let _ = fs::write(&sample_path, sample.as_bytes());
    }
}

impl Default for PluginManager {
    fn default() -> Self {
        Self::new()
    }
}
