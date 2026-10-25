use ensemble_core::AppPaths;
use ensemble_mcp::McpHandler;
use ensemble_plugin_engine::{
    analyze_url_security, ChapterSummary, PluginActionType, PluginHostContext, PluginLogger,
    PluginManager, PluginPendingAction,
};
use gtk4 as gtk;
use gtk4::gio;
use gtk4::glib;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;

#[derive(Clone)]
enum ExposedWidgetHandle {
    Label(gtk::Label),
    Input(gtk::Entry),
    TextArea(gtk::TextBuffer),
    Button(gtk::Button),
}

struct SandboxedPluginWindowState {
    window: adw::Window,
    content_box: gtk::Box,
    widgets: BTreeMap<String, ExposedWidgetHandle>,
    widget_values: Rc<RefCell<BTreeMap<String, String>>>,
}

#[derive(Clone)]
pub struct PluginUiHost {
    pub manager: Rc<RefCell<PluginManager>>,
    handler: Arc<McpHandler>,
    parent_window: gtk::Window,
    active_manager_window: Rc<RefCell<Option<gtk::Window>>>,
    plugin_windows: Rc<RefCell<BTreeMap<String, SandboxedPluginWindowState>>>,
    on_workspace_changed: Rc<dyn Fn()>,
    on_status_or_toast: Rc<dyn Fn(Option<String>, Option<String>)>,
}

impl PluginUiHost {
    pub fn new(
        parent: &impl IsA<gtk::Window>,
        handler: Arc<McpHandler>,
        on_workspace_changed: impl Fn() + 'static,
        on_status_or_toast: impl Fn(Option<String>, Option<String>) + 'static,
    ) -> Self {
        let mut mgr = PluginManager::new();
        mgr.initialize_and_load_all();
        Self {
            manager: Rc::new(RefCell::new(mgr)),
            handler,
            parent_window: parent.clone().upcast(),
            active_manager_window: Rc::new(RefCell::new(None)),
            plugin_windows: Rc::new(RefCell::new(BTreeMap::new())),
            on_workspace_changed: Rc::new(on_workspace_changed),
            on_status_or_toast: Rc::new(on_status_or_toast),
        }
    }

    fn effective_transient_parent(&self) -> gtk::Window {
        if let Some(ref mgr_win) = *self.active_manager_window.borrow() {
            if mgr_win.is_visible() {
                return mgr_win.clone();
            }
        }
        self.parent_window.clone()
    }

    pub fn close_plugin_window(&self, plugin_id: &str) {
        let win_opt = {
            let wins = self.plugin_windows.borrow();
            wins.get(plugin_id).map(|s| s.window.clone())
        };
        if let Some(w) = win_opt {
            w.set_visible(false);
        }
    }

    pub fn build_host_context(&self, plugin_id: Option<&str>) -> PluginHostContext {
        let mut ctx = PluginHostContext::default();
        {
            let ws = self.handler.workspace();
            let guard = ws.lock();
            if let Some(ref proj) = guard.project {
                let active_idx = guard
                    .active_chapter_index
                    .min(proj.chapters.len().saturating_sub(1));
                ctx.current_chapter_index = active_idx as i32;
                ctx.work_skin_css = proj.work_skin_css.clone();
                if let Some(ch) = proj.chapters.get(active_idx) {
                    ctx.current_chapter_title = ch.title.clone();
                    ctx.current_chapter_html = ch.html.clone();
                    ctx.current_plain_text = strip_html_tags(&ch.html);
                    ctx.word_count = ch.word_count();
                }
                for ch in &proj.chapters {
                    ctx.chapters.push(ChapterSummary {
                        title: ch.title.clone(),
                        html: ch.html.clone(),
                        word_count: ch.word_count(),
                    });
                }
            }
        }

        if let Some(pid) = plugin_id {
            let wins = self.plugin_windows.borrow();
            if let Some(win_state) = wins.get(pid) {
                let vals = win_state.widget_values.borrow();
                for (k, v) in vals.iter() {
                    ctx.ui_widget_values.insert(k.clone(), v.clone());
                }
            }
        }
        ctx
    }

    pub fn run_toolbar_action(&self, plugin_id: &str, func_name: &str) {
        let ctx = self.build_host_context(Some(plugin_id));
        {
            let mut mgr = self.manager.borrow_mut();
            mgr.trigger_toolbar_action(plugin_id, func_name, &ctx);
        }
        self.drain_and_apply_actions();
    }

    pub fn run_widget_callback(&self, plugin_id: &str, callback_fn: &str, widget_id: &str) {
        let ctx = self.build_host_context(Some(plugin_id));
        {
            let mut mgr = self.manager.borrow_mut();
            mgr.trigger_ui_widget_callback(plugin_id, callback_fn, widget_id, &ctx);
        }
        self.drain_and_apply_actions();
    }

    pub fn notify_chapter_changed(&self) {
        let ctx = self.build_host_context(None);
        {
            let mut mgr = self.manager.borrow_mut();
            mgr.notify_chapter_changed(&ctx);
        }
        self.drain_and_apply_actions();
    }

    pub fn drain_and_apply_actions(&self) {
        let (actions, errors, quarantines) = {
            let mut mgr = self.manager.borrow_mut();
            (
                mgr.take_emitted_actions(),
                mgr.take_emitted_errors(),
                mgr.take_emitted_quarantines(),
            )
        };

        let mut workspace_mutated = false;

        for action in actions {
            match action.action_type {
                PluginActionType::SetChapterHtml => {
                    let ws = self.handler.workspace();
                    let mut g = ws.lock();
                    let active_idx = g.active_chapter_index;
                    if let Some(ref mut proj) = g.project {
                        let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                        if let Some(ch) = proj.chapters.get_mut(idx) {
                            ch.ensure_loaded();
                            ch.set_html_modified(action.payload);
                            proj.dirty = true;
                            workspace_mutated = true;
                        }
                    }
                }
                PluginActionType::InsertHtmlAtCursor | PluginActionType::ReplaceSelection => {
                    let ws = self.handler.workspace();
                    let mut g = ws.lock();
                    let active_idx = g.active_chapter_index;
                    if let Some(ref mut proj) = g.project {
                        let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                        if let Some(ch) = proj.chapters.get_mut(idx) {
                            ch.ensure_loaded();
                            let mut next_html = ch.html.clone();
                            if !next_html.ends_with('\n') && !next_html.is_empty() {
                                next_html.push('\n');
                            }
                            next_html.push_str(&action.payload);
                            ch.set_html_modified(next_html);
                            proj.dirty = true;
                            workspace_mutated = true;
                        }
                    }
                }
                PluginActionType::SetWorkSkinCss => {
                    let ws = self.handler.workspace();
                    let mut g = ws.lock();
                    if let Some(ref mut proj) = g.project {
                        proj.work_skin_css = action.payload;
                        proj.dirty = true;
                        workspace_mutated = true;
                    }
                }
                PluginActionType::SetChapterTitle => {
                    let ws = self.handler.workspace();
                    let mut g = ws.lock();
                    let active_idx = g.active_chapter_index;
                    if let Some(ref mut proj) = g.project {
                        let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                        if let Some(ch) = proj.chapters.get_mut(idx) {
                            ch.title = action.payload;
                            ch.dirty_chapter = true;
                            proj.dirty = true;
                            workspace_mutated = true;
                        }
                    }
                }
                PluginActionType::ShowStatusBadge => {
                    (self.on_status_or_toast)(Some(action.payload), None);
                }
                PluginActionType::ClearStatusBadge => {
                    (self.on_status_or_toast)(Some(String::new()), None);
                }
                PluginActionType::ShowToastNotification => {
                    (self.on_status_or_toast)(None, Some(action.payload));
                }
                PluginActionType::UiCreateWindow
                | PluginActionType::UiAddLabel
                | PluginActionType::UiAddButton
                | PluginActionType::UiAddInput
                | PluginActionType::UiAddTextArea
                | PluginActionType::UiSetWidgetText
                | PluginActionType::UiShowWindow
                | PluginActionType::UiCloseWindow
                | PluginActionType::UiClearWindow
                | PluginActionType::UiShowMessageBox => {
                    self.apply_ui_action(&action);
                }
                PluginActionType::SendPluginMessage => {}
            }
        }

        if workspace_mutated {
            (self.on_workspace_changed)();
        }

        for (pid, err) in errors {
            (self.on_status_or_toast)(None, Some(format!("Plugin '{}' error: {}", pid, err)));
        }

        for q in quarantines {
            // Immediately close any open UI window belonging to the quarantined plugin
            self.close_plugin_window(&q.plugin_id);

            let transient_parent = self.effective_transient_parent();
            let dlg = adw::MessageDialog::builder()
                .transient_for(&transient_parent)
                .modal(true)
                .heading("Security Alert — Plugin Terminated & Quarantined")
                .body(&format!(
                    "Plugin '{}' ({}.er) triggered a strict security violation and was immediately terminated and quarantined:\n\n{}\n\nTarget / Character-ID Endpoint:\n{}",
                    q.plugin_name, q.plugin_id, q.reason, q.target_url
                ))
                .build();
            dlg.add_response("ok", "Understood");
            dlg.set_response_appearance("ok", adw::ResponseAppearance::Destructive);
            dlg.set_default_response(Some("ok"));
            dlg.present();
        }
    }

    fn ensure_plugin_window(&self, plugin_id: &str, title: &str, width: i32, height: i32) {
        let transient_parent = self.effective_transient_parent();
        let mut wins = self.plugin_windows.borrow_mut();
        if let Some(existing) = wins.get_mut(plugin_id) {
            existing.window.set_title(Some(title));
            existing.window.set_transient_for(Some(&transient_parent));
            existing
                .window
                .set_default_size(width.clamp(320, 1100), height.clamp(220, 820));
            while let Some(child) = existing.content_box.first_child() {
                existing.content_box.remove(&child);
            }
            existing.widgets.clear();
            existing.widget_values.borrow_mut().clear();
            return;
        }

        let window = adw::Window::builder()
            .title(title)
            .transient_for(&transient_parent)
            .modal(false)
            .default_width(width.clamp(360, 1100))
            .default_height(height.clamp(240, 820))
            .build();

        let root = gtk::Box::new(gtk::Orientation::Vertical, 0);
        let header = adw::HeaderBar::new();
        let win_title = adw::WindowTitle::new(
            title,
            &format!("Plugin UI ({}.er) • Ensemble Safe UI SDK", plugin_id),
        );
        header.set_title_widget(Some(&win_title));

        let close_ui_btn = gtk::Button::builder()
            .label("Close")
            .tooltip_text("Close this plugin UI window")
            .css_classes(["flat"])
            .build();
        {
            let win_weak = window.downgrade();
            close_ui_btn.connect_clicked(move |_| {
                if let Some(w) = win_weak.upgrade() {
                    w.set_visible(false);
                }
            });
        }
        header.pack_start(&close_ui_btn);

        let sdk_badge = gtk::Label::builder()
            .label("🔒 Sandboxed UI")
            .css_classes(["caption", "dim-label"])
            .margin_end(8)
            .build();
        header.pack_end(&sdk_badge);
        root.append(&header);

        let scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .hexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Never)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .build();

        let clamp = adw::Clamp::builder()
            .maximum_size(680)
            .margin_top(16)
            .margin_bottom(20)
            .margin_start(18)
            .margin_end(18)
            .build();

        let content_box = gtk::Box::new(gtk::Orientation::Vertical, 12);
        clamp.set_child(Some(&content_box));
        scroll.set_child(Some(&clamp));
        root.append(&scroll);
        window.set_content(Some(&root));

        window.set_hide_on_close(true);
        window.connect_close_request(|w| {
            w.set_visible(false);
            glib::Propagation::Stop
        });

        wins.insert(
            plugin_id.to_string(),
            SandboxedPluginWindowState {
                window,
                content_box,
                widgets: BTreeMap::new(),
                widget_values: Rc::new(RefCell::new(BTreeMap::new())),
            },
        );
    }

    fn apply_ui_action(&self, action: &PluginPendingAction) {
        let pid = &action.source_plugin_id;
        match action.action_type {
            PluginActionType::UiCreateWindow => {
                self.ensure_plugin_window(pid, &action.payload, action.width, action.height);
            }
            PluginActionType::UiAddLabel => {
                let has_win = self.plugin_windows.borrow().contains_key(pid);
                if !has_win {
                    self.ensure_plugin_window(pid, pid, 440, 320);
                }
                let mut wins = self.plugin_windows.borrow_mut();
                if let Some(state) = wins.get_mut(pid) {
                    let lbl = gtk::Label::builder()
                        .use_markup(false)
                        .label(&action.secondary_payload)
                        .xalign(0.0)
                        .wrap(true)
                        .selectable(true)
                        .build();
                    state.content_box.append(&lbl);
                    state
                        .widgets
                        .insert(action.payload.clone(), ExposedWidgetHandle::Label(lbl));
                }
            }
            PluginActionType::UiAddInput => {
                let has_win = self.plugin_windows.borrow().contains_key(pid);
                if !has_win {
                    self.ensure_plugin_window(pid, pid, 440, 320);
                }
                let mut wins = self.plugin_windows.borrow_mut();
                if let Some(state) = wins.get_mut(pid) {
                    let entry = gtk::Entry::builder()
                        .placeholder_text(&action.secondary_payload)
                        .text(&action.tertiary_payload)
                        .hexpand(true)
                        .build();
                    let wid = action.payload.clone();
                    state
                        .widget_values
                        .borrow_mut()
                        .insert(wid.clone(), action.tertiary_payload.clone());

                    let vals = state.widget_values.clone();
                    let wid_clone = wid.clone();
                    entry.connect_changed(move |e| {
                        vals.borrow_mut()
                            .insert(wid_clone.clone(), e.text().to_string());
                    });

                    state.content_box.append(&entry);
                    state
                        .widgets
                        .insert(wid, ExposedWidgetHandle::Input(entry));
                }
            }
            PluginActionType::UiAddTextArea => {
                let has_win = self.plugin_windows.borrow().contains_key(pid);
                if !has_win {
                    self.ensure_plugin_window(pid, pid, 440, 360);
                }
                let mut wins = self.plugin_windows.borrow_mut();
                if let Some(state) = wins.get_mut(pid) {
                    let frame = gtk::Frame::new(None);
                    let scroll = gtk::ScrolledWindow::builder()
                        .min_content_height(110)
                        .max_content_height(220)
                        .vexpand(false)
                        .hexpand(true)
                        .propagate_natural_height(false)
                        .build();
                    let tv = gtk::TextView::builder()
                        .wrap_mode(gtk::WrapMode::WordChar)
                        .top_margin(8)
                        .bottom_margin(8)
                        .left_margin(10)
                        .right_margin(10)
                        .build();
                    let buf = tv.buffer();
                    buf.set_text(&action.secondary_payload);

                    let wid = action.payload.clone();
                    state
                        .widget_values
                        .borrow_mut()
                        .insert(wid.clone(), action.secondary_payload.clone());

                    let vals = state.widget_values.clone();
                    let wid_clone = wid.clone();
                    buf.connect_changed(move |b| {
                        let text = b.text(&b.start_iter(), &b.end_iter(), false).to_string();
                        vals.borrow_mut().insert(wid_clone.clone(), text);
                    });

                    scroll.set_child(Some(&tv));
                    frame.set_child(Some(&scroll));
                    state.content_box.append(&frame);
                    state
                        .widgets
                        .insert(wid, ExposedWidgetHandle::TextArea(buf));
                }
            }
            PluginActionType::UiAddButton => {
                let has_win = self.plugin_windows.borrow().contains_key(pid);
                if !has_win {
                    self.ensure_plugin_window(pid, pid, 440, 320);
                }
                let mut wins = self.plugin_windows.borrow_mut();
                if let Some(state) = wins.get_mut(pid) {
                    let btn = gtk::Button::builder()
                        .label(&action.secondary_payload)
                        .css_classes(["suggested-action", "pill"])
                        .halign(gtk::Align::Start)
                        .margin_top(4)
                        .build();

                    let wid = action.payload.clone();
                    let cb_fn = action.tertiary_payload.clone();
                    if !cb_fn.is_empty() {
                        let host_clone = self.clone();
                        let pid_str = pid.clone();
                        let wid_str = wid.clone();
                        btn.connect_clicked(move |_| {
                            let h = host_clone.clone();
                            let p = pid_str.clone();
                            let c = cb_fn.clone();
                            let w = wid_str.clone();
                            glib::idle_add_local_once(move || {
                                h.run_widget_callback(&p, &c, &w);
                            });
                        });
                    }

                    state.content_box.append(&btn);
                    state
                        .widgets
                        .insert(wid, ExposedWidgetHandle::Button(btn));
                }
            }
            PluginActionType::UiSetWidgetText => {
                let handle_opt = {
                    let mut wins = self.plugin_windows.borrow_mut();
                    if let Some(state) = wins.get_mut(pid) {
                        let wid = &action.payload;
                        let text = &action.secondary_payload;
                        state
                            .widget_values
                            .borrow_mut()
                            .insert(wid.clone(), text.clone());
                        state.widgets.get(wid).cloned()
                    } else {
                        None
                    }
                };
                if let Some(handle) = handle_opt {
                    let text = &action.secondary_payload;
                    match handle {
                        ExposedWidgetHandle::Label(l) => l.set_text(text),
                        ExposedWidgetHandle::Input(e) => e.set_text(text),
                        ExposedWidgetHandle::TextArea(b) => b.set_text(text),
                        ExposedWidgetHandle::Button(btn) => btn.set_label(text),
                    }
                }
            }
            PluginActionType::UiShowWindow => {
                let transient_parent = self.effective_transient_parent();
                let win_opt = {
                    let wins = self.plugin_windows.borrow();
                    wins.get(pid).map(|s| s.window.clone())
                };
                if let Some(w) = win_opt {
                    w.set_transient_for(Some(&transient_parent));
                    w.set_visible(true);
                    w.present();
                }
            }
            PluginActionType::UiCloseWindow => {
                self.close_plugin_window(pid);
            }
            PluginActionType::UiClearWindow => {
                let mut wins = self.plugin_windows.borrow_mut();
                if let Some(state) = wins.get_mut(pid) {
                    while let Some(child) = state.content_box.first_child() {
                        state.content_box.remove(&child);
                    }
                    state.widgets.clear();
                    state.widget_values.borrow_mut().clear();
                }
            }
            PluginActionType::UiShowMessageBox => {
                let transient_parent = self.effective_transient_parent();
                let dlg = adw::MessageDialog::builder()
                    .transient_for(&transient_parent)
                    .modal(true)
                    .heading(&action.payload)
                    .body(&action.secondary_payload)
                    .build();
                dlg.add_response("ok", "OK");
                dlg.set_default_response(Some("ok"));
                dlg.present();
            }
            _ => {}
        }
    }
}

struct BuiltinSamplePlugin {
    id: &'static str,
    filename: &'static str,
    name: &'static str,
    version: &'static str,
    category: &'static str,
    description: &'static str,
    source: &'static str,
}

const BUILTIN_SAMPLE_PLUGINS: &[BuiltinSamplePlugin] = &[
    BuiltinSamplePlugin {
        id: "ao3_dialogue_builder",
        filename: "ao3_dialogue_builder.er",
        name: "AO3 Chat Bubble & SMS Dialogue Builder",
        version: "1.2.0",
        category: "Built-in Sample • UI & Work Skin",
        description: "Sample plugin demonstrating Ensemble's exposed ui.* widgets to compose SMS/chat bubbles and append #workskin CSS.",
        source: concat!(
            "# @name: AO3 Chat Bubble & SMS Dialogue Builder (Sample)\n",
            "# @version: 1.2.0\n",
            "# @author: Ensemble Built-in Sample\n",
            "# @description: Interactive custom UI window to insert AO3 chat bubbles and Work Skin CSS.\n",
            "\n",
            "@on_toolbar_click(\"Open Chat Bubble Builder UI\")\n",
            "def open_chat_builder():\n",
            "    sel = editor.get_selection()\n",
            "    ui.create_window(\"AO3 Chat Bubble & SMS Builder\", 480, 420)\n",
            "    ui.add_label(\"lbl_head\", \"Compose an SMS or chat message bubble using Ensemble's exposed UI widgets:\")\n",
            "    ui.add_input(\"inp_sender\", \"Character Name (e.g. Character A)\", \"Character A\")\n",
            "    ui.add_input(\"inp_align\", \"Bubble Style: left or right\", \"left\")\n",
            "    ui.add_text_area(\"txt_msg\", sel)\n",
            "    ui.add_button(\"btn_add_bubble\", \"Insert Chat Bubble into Chapter\", \"on_insert_bubble\")\n",
            "    ui.add_button(\"btn_inject_css\", \"Add Chat Bubble CSS to Work Skin\", \"on_inject_css\")\n",
            "    ui.show_window()\n",
            "\n",
            "def on_insert_bubble(widget_id):\n",
            "    sender = ui.get_text(\"inp_sender\")\n",
            "    side = ui.get_text(\"inp_align\")\n",
            "    msg = ui.get_text(\"txt_msg\")\n",
            "    cls = \"chat-bubble-left\"\n",
            "    if side == \"right\":\n",
            "        cls = \"chat-bubble-right\"\n",
            "    html = '<div class=\"chat-row\"><p class=\"chat-sender\"><b>' + sender + '</b></p><p class=\"' + cls + '\">' + msg + '</p></div>'\n",
            "    editor.insert_html(html)\n",
            "    status.notify(\"Inserted chat bubble for \" + sender)\n",
            "\n",
            "def on_inject_css(widget_id):\n",
            "    css = editor.get_work_skin_css()\n",
            "    snippet = \"\\n#workskin .chat-row { margin: 10px 0; }\\n#workskin .chat-sender { font-size: 0.85em; opacity: 0.75; margin-bottom: 2px; }\\n#workskin .chat-bubble-left { display: inline-block; background: #e5e5ea; color: #111; padding: 8px 14px; border-radius: 16px 16px 16px 4px; max-width: 78%; }\\n#workskin .chat-bubble-right { display: inline-block; background: #3584e4; color: #fff; padding: 8px 14px; border-radius: 16px 16px 4px 16px; max-width: 78%; }\\n\"\n",
            "    editor.set_work_skin_css(css + snippet)\n",
            "    status.notify(\"Injected AO3 Chat Bubble rules into Work Skin CSS!\")\n",
        ),
    },
    BuiltinSamplePlugin {
        id: "reading_time_helper",
        filename: "reading_time_helper.er",
        name: "Reading Time & Dialogue Counter",
        version: "1.1.0",
        category: "Built-in Sample • Metrics & UI",
        description: "Sample plugin that calculates estimated reading time in the status bar and opens a Chapter Statistics window.",
        source: concat!(
            "# @name: Reading Time & Dialogue Counter (Sample)\n",
            "# @version: 1.1.0\n",
            "# @author: Ensemble Built-in Sample\n",
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
            "    mins = int(words / 200)\n",
            "    if mins < 1 and words > 0:\n",
            "        mins = 1\n",
            "    ui.create_window(\"Chapter Statistics — \" + title, 420, 300)\n",
            "    ui.add_label(\"lbl_title\", \"Chapter: \" + title)\n",
            "    ui.add_label(\"lbl_words\", \"Word Count: \" + str(words) + \" words\")\n",
            "    ui.add_label(\"lbl_chars\", \"Character Count: \" + str(len(text)) + \" characters\")\n",
            "    ui.add_label(\"lbl_time\", \"Estimated Reading Time: ~\" + str(mins) + \" min (at 200 WPM)\")\n",
            "    ui.show_window()\n",
        ),
    },
    BuiltinSamplePlugin {
        id: "scene_divider_tool",
        filename: "scene_divider_tool.er",
        name: "Aesthetic AO3 Scene Break Inserter",
        version: "1.0.0",
        category: "Built-in Sample • Formatting & UI",
        description: "Sample custom UI picker for inserting centered ornamental AO3 scene dividers and horizontal rules.",
        source: concat!(
            "# @name: Aesthetic AO3 Scene Break Inserter (Sample)\n",
            "# @version: 1.0.0\n",
            "# @author: Ensemble Built-in Sample\n",
            "# @description: Custom UI tool to insert ornamental AO3 scene breaks into the active chapter.\n",
            "\n",
            "@on_toolbar_click(\"Scene Break Inserter UI\")\n",
            "def open_divider_ui():\n",
            "    ui.create_window(\"AO3 Scene Break Inserter\", 420, 320)\n",
            "    ui.add_label(\"lbl_prompt\", \"Customize or insert an ornamental scene divider:\")\n",
            "    ui.add_input(\"inp_symbol\", \"Ornament symbols\", \"* * *\")\n",
            "    ui.add_button(\"btn_ornament\", \"Insert Ornamental Scene Break\", \"on_insert_ornament\")\n",
            "    ui.add_button(\"btn_hr\", \"Insert Standard <hr> Rule\", \"on_insert_hr\")\n",
            "    ui.show_window()\n",
            "\n",
            "def on_insert_ornament(widget_id):\n",
            "    sym = ui.get_text(\"inp_symbol\")\n",
            "    editor.insert_html('<p align=\"center\">' + sym + '</p>')\n",
            "    status.notify(\"Inserted ornamental scene break: \" + sym)\n",
            "\n",
            "def on_insert_hr(widget_id):\n",
            "    editor.insert_html(\"<hr />\")\n",
            "    status.notify(\"Inserted <hr /> scene break\")\n",
        ),
    },
    BuiltinSamplePlugin {
        id: "word_sprint_tracker",
        filename: "word_sprint_tracker.er",
        name: "Word Sprint & Goal Tracker",
        version: "1.0.0",
        category: "Built-in Sample • Storage & UI",
        description: "Sample plugin demonstrating isolated storage.* persistence and a live word-count goal tracker window.",
        source: concat!(
            "# @name: Word Sprint & Goal Tracker (Sample)\n",
            "# @version: 1.0.0\n",
            "# @author: Ensemble Built-in Sample\n",
            "# @description: Set chapter word goals stored in isolated storage with live progress UI.\n",
            "\n",
            "@on_toolbar_click(\"Word Goal Tracker UI\")\n",
            "def open_goal_ui():\n",
            "    saved_goal = storage.get(\"target_words\", \"2500\")\n",
            "    current = editor.get_word_count()\n",
            "    ui.create_window(\"Word Sprint & Goal Tracker\", 420, 310)\n",
            "    ui.add_label(\"lbl_curr\", \"Current Chapter Words: \" + str(current))\n",
            "    ui.add_input(\"inp_goal\", \"Target word count\", saved_goal)\n",
            "    ui.add_button(\"btn_save_goal\", \"Save & Update Goal Badge\", \"on_save_goal\")\n",
            "    ui.show_window()\n",
            "\n",
            "def on_save_goal(widget_id):\n",
            "    goal_str = ui.get_text(\"inp_goal\")\n",
            "    storage.set(\"target_words\", goal_str)\n",
            "    curr = editor.get_word_count()\n",
            "    status.set_badge(str(curr) + \" / \" + goal_str + \" words\")\n",
            "    status.notify(\"Updated chapter word goal to \" + goal_str + \" words!\")\n",
        ),
    },
    BuiltinSamplePlugin {
        id: "ao3_https_link_inspector",
        filename: "ao3_https_link_inspector.er",
        name: "HTTPS Endpoint & Homograph Link Inspector",
        version: "1.0.0",
        category: "Built-in Sample • HTTPS Network & Security",
        description: "Sample plugin demonstrating http.inspect_link() Character-ID homograph detection and mediated zero-redirect http.get() requests.",
        source: concat!(
            "# @name: HTTPS Endpoint & Homograph Link Inspector (Sample)\n",
            "# @version: 1.0.0\n",
            "# @author: Ensemble Built-in Sample\n",
            "# @description: Inspect URLs for Cyrillic/homograph Character IDs and test mediated HTTPS requests.\n",
            "\n",
            "@on_toolbar_click(\"HTTPS & Link Security UI\")\n",
            "def open_net_inspector():\n",
            "    ui.create_window(\"HTTPS & Character-ID Link Inspector\", 500, 420)\n",
            "    ui.add_label(\"lbl_info\", \"Enter an HTTPS URL to inspect its Unicode Character IDs or perform a mediated zero-redirect HTTPS request:\")\n",
            "    ui.add_input(\"inp_url\", \"https://archiveofourown.org\", \"https://archiveofourown.org\")\n",
            "    ui.add_button(\"btn_check_chars\", \"Inspect Link Character IDs\", \"on_inspect_link\")\n",
            "    ui.add_button(\"btn_fetch_https\", \"Perform Mediated HTTPS GET\", \"on_fetch_https\")\n",
            "    ui.add_text_area(\"txt_result\", \"Ready. Click 'Inspect Link Character IDs' or enable HTTPS Network Access + domain whitelist to test 'Perform Mediated HTTPS GET'.\")\n",
            "    ui.show_window()\n",
            "\n",
            "def on_inspect_link(widget_id):\n",
            "    target = ui.get_text(\"inp_url\")\n",
            "    info = http.inspect_link(target)\n",
            "    if info[\"homograph_detected\"]:\n",
            "        ui.set_text(\"txt_result\", \"SECURITY WARNING (Homograph Spoofing Attempt):\\n\" + info[\"warning\"] + \"\\n\\nCharacter-ID View: \" + info[\"character_id_link\"])\n",
            "    else:\n",
            "        ui.set_text(\"txt_result\", \"Link Safe: \" + str(info[\"safe\"]) + \"\\nHTTPS: \" + str(info[\"https\"]) + \"\\nHost: \" + info[\"host\"] + \"\\nHost Character IDs: \" + info[\"host_character_ids\"])\n",
            "\n",
            "def on_fetch_https(widget_id):\n",
            "    target = ui.get_text(\"inp_url\")\n",
            "    resp = http.get(target)\n",
            "    ui.set_text(\"txt_result\", \"HTTP Status: \" + str(resp[\"status\"]) + \" (OK: \" + str(resp[\"ok\"]) + \")\\nError: \" + resp[\"error\"] + \"\\nBytes received: \" + str(len(resp[\"body\"])))\n",
        ),
    },
];

fn schedule_ui_refresh(refresh_slot: &Rc<RefCell<Option<Rc<dyn Fn()>>>>) {
    let slot = refresh_slot.clone();
    glib::idle_add_local_once(move || {
        let cb_opt = slot.borrow().clone();
        if let Some(cb) = cb_opt {
            cb();
        }
    });
}

const DEFAULT_MARKETPLACE_REPO_URL: &str = "https://plugins.ensemble-ao3.org";

fn marketplace_repo_config_path() -> PathBuf {
    AppPaths::app_config_dir().join("marketplace_repo_url.txt")
}

pub fn read_marketplace_repo_url() -> String {
    if let Ok(text) = std::fs::read_to_string(marketplace_repo_config_path()) {
        let trimmed = text.trim();
        if !trimmed.is_empty() {
            return trimmed.to_string();
        }
    }
    DEFAULT_MARKETPLACE_REPO_URL.to_string()
}

pub fn write_marketplace_repo_url(url: &str) -> std::io::Result<()> {
    let dir = AppPaths::app_config_dir();
    let _ = std::fs::create_dir_all(&dir);
    std::fs::write(marketplace_repo_config_path(), url.trim().as_bytes())
}

pub fn show_plugin_marketplace_dialog(parent: &impl IsA<gtk::Window>) {
    let repo_url = read_marketplace_repo_url();
    let dlg = adw::Window::builder()
        .title("Ensemble Plugin Marketplace (WIP)")
        .transient_for(parent)
        .modal(false)
        .default_width(720)
        .default_height(500)
        .build();

    let root = gtk::Box::new(gtk::Orientation::Vertical, 0);
    let header = adw::HeaderBar::new();
    let title = adw::WindowTitle::new(
        "Ensemble Plugin Marketplace (WIP)",
        &format!("Repository: {} • Work in Progress", repo_url),
    );
    header.set_title_widget(Some(&title));

    let wip_badge = gtk::Label::builder()
        .label("WIP")
        .css_classes(["warning", "caption", "bold"])
        .margin_end(8)
        .valign(gtk::Align::Center)
        .build();
    header.pack_end(&wip_badge);
    root.append(&header);

    let top_bar = gtk::Box::new(gtk::Orientation::Horizontal, 8);
    top_bar.set_margin_top(12);
    top_bar.set_margin_start(16);
    top_bar.set_margin_end(16);

    let search = gtk::SearchEntry::builder()
        .placeholder_text("Search community plugins, themes, and AO3 tools… (WIP)")
        .hexpand(true)
        .sensitive(false)
        .build();
    top_bar.append(&search);

    let cat_btn = gtk::Button::builder()
        .label("All Categories (WIP)")
        .sensitive(false)
        .build();
    top_bar.append(&cat_btn);
    root.append(&top_bar);

    let status_page = adw::StatusPage::builder()
        .icon_name("system-software-install-symbolic")
        .title("Online Plugin Marketplace — Work in Progress (WIP)")
        .description(&format!(
            "The standalone Ensemble Plugin Marketplace is currently a Work in Progress (WIP).\n\nConfigured Repository Endpoint:\n{}\n\nYou can change the repository domain inside Plugin Manager → Marketplace (WIP), or import local .er scripts in Plugin Manager → Plugins.",
            repo_url
        ))
        .vexpand(true)
        .build();

    let close_btn = gtk::Button::builder()
        .label("Close")
        .css_classes(["suggested-action", "pill"])
        .halign(gtk::Align::Center)
        .build();
    {
        let dlg_weak = dlg.downgrade();
        close_btn.connect_clicked(move |_| {
            if let Some(w) = dlg_weak.upgrade() {
                w.close();
            }
        });
    }
    status_page.set_child(Some(&close_btn));
    root.append(&status_page);

    dlg.set_content(Some(&root));
    dlg.present();
}

pub fn show_plugin_manager_dialog(parent: &impl IsA<gtk::Window>) {
    let dummy_srv = ensemble_mcp::McpServer::new();
    let dummy_handler = McpHandler::new(&dummy_srv);
    let host = PluginUiHost::new(parent, dummy_handler, || {}, |_, _| {});
    show_plugin_manager_with_host(parent, host);
}

pub fn show_plugin_manager_with_host(parent: &impl IsA<gtk::Window>, host: PluginUiHost) {
    // Non-modal so custom Plugin UI windows opened from here can be interacted with and closed directly!
    let window = adw::Window::builder()
        .title("Ensemble Plugin Manager")
        .transient_for(parent)
        .modal(false)
        .default_width(880)
        .default_height(660)
        .build();

    *host.active_manager_window.borrow_mut() = Some(window.clone().upcast());
    {
        let active_mgr_ref = host.active_manager_window.clone();
        let parent_win = host.parent_window.clone();
        let wins_ref = host.plugin_windows.clone();
        window.connect_close_request(move |_| {
            *active_mgr_ref.borrow_mut() = None;
            let wins = wins_ref.borrow();
            for state in wins.values() {
                state.window.set_transient_for(Some(&parent_win));
            }
            glib::Propagation::Proceed
        });
    }

    let toast_overlay = adw::ToastOverlay::new();
    let root = gtk::Box::new(gtk::Orientation::Vertical, 0);

    let header = adw::HeaderBar::new();
    let view_stack = adw::ViewStack::new();
    let view_switcher = adw::ViewSwitcher::builder()
        .stack(&view_stack)
        .policy(adw::ViewSwitcherPolicy::Wide)
        .build();
    header.set_title_widget(Some(&view_switcher));

    let open_dir_btn = gtk::Button::builder()
        .icon_name("folder-open-symbolic")
        .tooltip_text("Open Plugins Folder in File Manager")
        .build();
    header.pack_end(&open_dir_btn);

    root.append(&header);

    // ========================================================================
    // TAB 1: Plugins (Installed & Built-in Samples)
    // ========================================================================
    let installed_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .propagate_natural_height(false)
        .build();
    let installed_clamp = adw::Clamp::builder()
        .maximum_size(780)
        .margin_top(16)
        .margin_bottom(24)
        .margin_start(16)
        .margin_end(16)
        .build();
    let installed_box = gtk::Box::new(gtk::Orientation::Vertical, 16);
    installed_clamp.set_child(Some(&installed_box));
    installed_scroll.set_child(Some(&installed_clamp));

    view_stack.add_titled_with_icon(
        &installed_scroll,
        Some("installed"),
        "Plugins",
        "application-x-addon-symbolic",
    );

    // ========================================================================
    // TAB 2: Internet & Security (Website Access & Request Logs)
    // ========================================================================
    let net_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .propagate_natural_height(false)
        .build();
    let net_clamp = adw::Clamp::builder()
        .maximum_size(780)
        .margin_top(16)
        .margin_bottom(24)
        .margin_start(16)
        .margin_end(16)
        .build();
    let net_box = gtk::Box::new(gtk::Orientation::Vertical, 16);
    net_clamp.set_child(Some(&net_box));
    net_scroll.set_child(Some(&net_clamp));

    view_stack.add_titled_with_icon(
        &net_scroll,
        Some("network_security"),
        "Internet & Security",
        "security-high-symbolic",
    );

    // ========================================================================
    // TAB 3: Marketplace Configuration (WIP)
    // ========================================================================
    let repo_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .propagate_natural_height(false)
        .build();
    let repo_clamp = adw::Clamp::builder()
        .maximum_size(780)
        .margin_top(16)
        .margin_bottom(24)
        .margin_start(16)
        .margin_end(16)
        .build();
    let repo_box = gtk::Box::new(gtk::Orientation::Vertical, 16);
    repo_clamp.set_child(Some(&repo_box));
    repo_scroll.set_child(Some(&repo_clamp));

    view_stack.add_titled_with_icon(
        &repo_scroll,
        Some("marketplace_config"),
        "Marketplace (WIP)",
        "system-software-install-symbolic",
    );

    toast_overlay.set_child(Some(&view_stack));
    root.append(&toast_overlay);
    window.set_content(Some(&root));

    // Open Plugins Folder button handler
    {
        let toast_ov = toast_overlay.clone();
        open_dir_btn.connect_clicked(move |_| {
            let pdir = AppPaths::plugins_dir();
            let _ = std::fs::create_dir_all(&pdir);
            let uri = format!("file://{}", pdir.display());
            if gio::AppInfo::launch_default_for_uri(&uri, None::<&gio::AppLaunchContext>).is_err() {
                toast_ov.add_toast(adw::Toast::new(&format!(
                    "Plugins directory: {}",
                    pdir.display()
                )));
            }
        });
    }

    // Shared UI refresh closure
    let refresh_all: Rc<RefCell<Option<Rc<dyn Fn()>>>> = Rc::new(RefCell::new(None));

    // File Picker helper for importing .er files
    let import_er_dialog: Rc<dyn Fn()> = {
        let win_clone = window.clone();
        let host_clone = host.clone();
        let toast_ov = toast_overlay.clone();
        let refresh_ref = refresh_all.clone();
        Rc::new(move || {
            let filter = gtk::FileFilter::new();
            filter.set_name(Some("Ensemble Plugin Scripts (*.er)"));
            filter.add_pattern("*.er");

            let all_filter = gtk::FileFilter::new();
            all_filter.set_name(Some("All Files"));
            all_filter.add_pattern("*");

            let filters = gio::ListStore::new::<gtk::FileFilter>();
            filters.append(&filter);
            filters.append(&all_filter);

            let dialog = gtk::FileDialog::builder()
                .title("Import Ensemble Plugin (.er)")
                .modal(true)
                .filters(&filters)
                .default_filter(&filter)
                .build();

            let host_inner = host_clone.clone();
            let toast_inner = toast_ov.clone();
            let refresh_inner = refresh_ref.clone();
            dialog.open(
                Some(&win_clone),
                gio::Cancellable::NONE,
                move |res| {
                    if let Ok(file) = res {
                        if let Some(path) = file.path() {
                            let load_res = {
                                let mut mgr = host_inner.manager.borrow_mut();
                                mgr.load_plugin_from_file(&path)
                            };
                            match load_res {
                                Ok(()) => {
                                    let fname = path
                                        .file_name()
                                        .unwrap_or_default()
                                        .to_string_lossy()
                                        .to_string();
                                    toast_inner.add_toast(adw::Toast::new(&format!(
                                        "Imported & activated plugin '{}'",
                                        fname
                                    )));
                                    host_inner.notify_chapter_changed();
                                }
                                Err(err) => {
                                    toast_inner.add_toast(adw::Toast::new(&format!(
                                        "Plugin import error: {}",
                                        err
                                    )));
                                }
                            }
                            schedule_ui_refresh(&refresh_inner);
                        }
                    }
                },
            );
        })
    };

    let rebuild_ui: Rc<dyn Fn()> = {
        let window_for_dlg = window.clone();
        let installed_box = installed_box.clone();
        let net_box = net_box.clone();
        let repo_box = repo_box.clone();
        let host = host.clone();
        let toast_overlay = toast_overlay.clone();
        let import_er_dialog = import_er_dialog.clone();
        let refresh_all_weak = refresh_all.clone();

        Rc::new(move || {
            while let Some(c) = installed_box.first_child() {
                installed_box.remove(&c);
            }
            while let Some(c) = net_box.first_child() {
                net_box.remove(&c);
            }
            while let Some(c) = repo_box.first_child() {
                repo_box.remove(&c);
            }

            let (plugins, toolbar_actions) = {
                let mgr = host.manager.borrow();
                (mgr.plugins(), mgr.toolbar_actions())
            };

            // ----------------------------------------------------------------
            // Populate Tab 1: Installed Plugins & Built-in Samples
            // ----------------------------------------------------------------
            let quick_group = adw::PreferencesGroup::builder()
                .title("Plugin Management &amp; Import")
                .description("Select any local .er script file to import it into Ensemble's sandboxed plugin directory.")
                .build();

            let import_row = adw::ActionRow::builder()
                .use_markup(false)
                .title("Import Local Plugin File (.er)")
                .subtitle(&format!(
                    "Copies the selected .er script into {}",
                    AppPaths::plugins_dir().display()
                ))
                .build();
            import_row.add_prefix(&gtk::Image::from_icon_name("document-open-symbolic"));

            let row_import_btn = gtk::Button::builder()
                .label("Select .er File…")
                .css_classes(["suggested-action"])
                .valign(gtk::Align::Center)
                .build();
            {
                let imp = import_er_dialog.clone();
                row_import_btn.connect_clicked(move |_| imp());
            }
            import_row.add_suffix(&row_import_btn);

            let reload_btn = gtk::Button::builder()
                .icon_name("view-refresh-symbolic")
                .tooltip_text("Reload all plugins from disk")
                .valign(gtk::Align::Center)
                .build();
            {
                let host_c = host.clone();
                let toast_c = toast_overlay.clone();
                let ref_c = refresh_all_weak.clone();
                reload_btn.connect_clicked(move |_| {
                    {
                        let mut mgr = host_c.manager.borrow_mut();
                        mgr.initialize_and_load_all();
                    }
                    toast_c.add_toast(adw::Toast::new("Reloaded all .er plugins from disk"));
                    schedule_ui_refresh(&ref_c);
                });
            }
            import_row.add_suffix(&reload_btn);
            quick_group.add(&import_row);
            installed_box.append(&quick_group);

            let list_group = adw::PreferencesGroup::builder()
                .title(&format!("Installed Plugins ({})", plugins.len()))
                .description("Use the switch to enable or disable a plugin. Expand a plugin for its actions, status, and permissions.")
                .build();

            if plugins.is_empty() {
                let empty_row = adw::ActionRow::builder()
                    .use_markup(false)
                    .title("No Plugins Installed")
                    .subtitle("Click 'Select .er File…' above or load one of the Built-in Samples below.")
                    .build();
                empty_row.add_prefix(&gtk::Image::from_icon_name("dialog-information-symbolic"));
                list_group.add(&empty_row);
            } else {
                for meta in &plugins {
                    let is_sample = BUILTIN_SAMPLE_PLUGINS.iter().any(|s| s.id == meta.id);
                    let has_url_requests = meta.scan_report.uses_network
                        || !meta.scan_report.detected_domains.is_empty()
                        || !meta.permissions.allowed_domains.is_empty();

                    let subtitle_str = if !meta.description.is_empty() {
                        format!("v{} by {} — {}", meta.version, meta.author, meta.description)
                    } else {
                        format!("v{} by {} ({}.er)", meta.version, meta.author, meta.id)
                    };

                    let exp_row = adw::ExpanderRow::builder()
                        .use_markup(false)
                        .title(&meta.name)
                        .subtitle(&subtitle_str)
                        .build();
                    exp_row.add_prefix(&gtk::Image::from_icon_name("application-x-addon-symbolic"));

                    // Main row container ONLY has the Enable / Disable switch toggle
                    let enable_switch = gtk::Switch::builder()
                        .active(meta.enabled)
                        .valign(gtk::Align::Center)
                        .tooltip_text("Enable or disable this plugin")
                        .build();
                    {
                        let host_sw = host.clone();
                        let pid = meta.id.clone();
                        let ref_c = refresh_all_weak.clone();
                        enable_switch.connect_state_set(move |_, state| {
                            {
                                let mut mgr = host_sw.manager.borrow_mut();
                                mgr.set_plugin_enabled(&pid, state);
                            }
                            if !state {
                                host_sw.close_plugin_window(&pid);
                            }
                            schedule_ui_refresh(&ref_c);
                            gtk::glib::Propagation::Proceed
                        });
                    }
                    exp_row.add_suffix(&enable_switch);

                    // --------------------------------------------------------
                    // Nested rows inside the collapsible area of ExpanderRow:
                    // --------------------------------------------------------

                    // 1. Plugin Action / Custom UI buttons inside collapsible list
                    let plugin_actions: Vec<_> = toolbar_actions
                        .iter()
                        .filter(|a| a.plugin_id == meta.id)
                        .cloned()
                        .collect();
                    for act in &plugin_actions {
                        let act_row = adw::ActionRow::builder()
                            .use_markup(false)
                            .title(&act.label)
                            .subtitle(&format!("Plugin Action • Callback: def {}()", act.func_name))
                            .build();
                        act_row.add_prefix(&gtk::Image::from_icon_name(
                            "media-playback-start-symbolic",
                        ));

                        let run_btn = gtk::Button::builder()
                            .icon_name("media-playback-start-symbolic")
                            .label(&act.label)
                            .css_classes(["suggested-action"])
                            .valign(gtk::Align::Center)
                            .build();
                        let host_run = host.clone();
                        let pid = act.plugin_id.clone();
                        let fn_name = act.func_name.clone();
                        let ref_c = refresh_all_weak.clone();
                        run_btn.connect_clicked(move |_| {
                            host_run.run_toolbar_action(&pid, &fn_name);
                            schedule_ui_refresh(&ref_c);
                        });
                        act_row.add_suffix(&run_btn);
                        exp_row.add_row(&act_row);
                    }

                    // 2. Status & Telemetry inside collapsible list
                    let (status_text, badge_class) = if meta.quarantined {
                        ("Quarantined (Security Violation)", "error")
                    } else if meta.enabled {
                        ("Active", "success")
                    } else {
                        ("Disabled", "dim-label")
                    };
                    let origin_label = if is_sample {
                        "Built-in Sample Script"
                    } else {
                        "Imported Local Plugin"
                    };
                    let mem_kb = (meta.peak_memory_bytes as f64) / 1024.0;
                    let status_row = adw::ActionRow::builder()
                        .use_markup(false)
                        .title(&format!("Status: {} • {}", status_text, origin_label))
                        .subtitle(&format!(
                            "Peak Heap: {:.1} KB / 8192 KB • Last Exec: {} ms / 250 ms • File: {}",
                            mem_kb, meta.last_exec_ms, meta.file_path
                        ))
                        .build();
                    let status_badge = gtk::Label::builder()
                        .label(status_text)
                        .css_classes([badge_class, "caption"])
                        .valign(gtk::Align::Center)
                        .build();
                    status_row.add_suffix(&status_badge);
                    exp_row.add_row(&status_row);

                    if !meta.last_error.is_empty() {
                        let err_row = adw::ActionRow::builder()
                            .use_markup(false)
                            .title("Last Runtime / Sandbox Trap")
                            .subtitle(&meta.last_error)
                            .css_classes(["error"])
                            .build();
                        exp_row.add_row(&err_row);
                    }

                    // Static URL / Homograph Security Findings
                    for finding in &meta.scan_report.url_security_findings {
                        if let Some(ref violation) = finding.violation_summary {
                            let warn_row = adw::ActionRow::builder()
                                .use_markup(false)
                                .title(&format!(
                                    "⚠️ Blocked Deceptive / Unsafe Link: {}",
                                    finding.character_id_representation
                                ))
                                .subtitle(violation)
                                .css_classes(["error"])
                                .build();
                            exp_row.add_row(&warn_row);
                        }
                    }

                    // 3. Permission toggles
                    let add_perm_toggle = |title: &str,
                                           subtitle: &str,
                                           current: bool,
                                           sensitive: bool,
                                           field: &'static str|
                     -> adw::ActionRow {
                        let r = adw::ActionRow::builder()
                            .use_markup(false)
                            .title(title)
                            .subtitle(subtitle)
                            .build();
                        let sw = gtk::Switch::builder()
                            .active(current && sensitive)
                            .sensitive(sensitive)
                            .valign(gtk::Align::Center)
                            .build();
                        if sensitive {
                            let host_p = host.clone();
                            let pid = meta.id.clone();
                            sw.connect_state_set(move |_, state| {
                                let mut mgr = host_p.manager.borrow_mut();
                                if let Some(p) = mgr.plugins().into_iter().find(|x| x.id == pid) {
                                    let mut perms = p.permissions;
                                    match field {
                                        "read" => perms.allow_editor_read = state,
                                        "write" => perms.allow_editor_write = state,
                                        "storage" => perms.allow_storage = state,
                                        "ui" => perms.allow_ui = state,
                                        "net" => perms.allow_network = state,
                                        _ => {}
                                    }
                                    mgr.update_plugin_permissions(&pid, perms);
                                }
                                gtk::glib::Propagation::Proceed
                            });
                        }
                        r.add_suffix(&sw);
                        r
                    };

                    exp_row.add_row(&add_perm_toggle(
                        "Exposed Ensemble UI Library (ui.*)",
                        "Allow plugin to build sandboxed windows, inputs, labels, and buttons",
                        meta.permissions.allow_ui,
                        true,
                        "ui",
                    ));
                    exp_row.add_row(&add_perm_toggle(
                        "Editor Read Access (editor.get_*)",
                        "Allow reading chapter HTML, plain text, selection, and word counts",
                        meta.permissions.allow_editor_read,
                        true,
                        "read",
                    ));
                    exp_row.add_row(&add_perm_toggle(
                        "Editor Write Access (editor.set_* / insert_html)",
                        "Allow modifying active chapter HTML, title, and Work Skin CSS",
                        meta.permissions.allow_editor_write,
                        true,
                        "write",
                    ));
                    exp_row.add_row(&add_perm_toggle(
                        "Isolated Key-Value Storage (storage.*)",
                        "Allow up to 5 MB of persistent key-value storage in isolated UIDP folder",
                        meta.permissions.allow_storage,
                        true,
                        "storage",
                    ));
                    exp_row.add_row(&add_perm_toggle(
                        "HTTPS Network Access (http.*)",
                        if has_url_requests {
                            "Allow strict zero-redirect HTTPS requests to approved domains"
                        } else {
                            "This plugin does not have URL requests."
                        },
                        meta.permissions.allow_network,
                        has_url_requests,
                        "net",
                    ));

                    // Uninstall Row
                    let uninstall_row = adw::ActionRow::builder()
                        .use_markup(false)
                        .title("Uninstall Plugin")
                        .subtitle("Deletes the .er script and its isolated UIDP storage folder")
                        .build();
                    let uninst_btn = gtk::Button::builder()
                        .icon_name("user-trash-symbolic")
                        .label("Uninstall")
                        .css_classes(["destructive-action"])
                        .valign(gtk::Align::Center)
                        .build();
                    {
                        let host_u = host.clone();
                        let pid = meta.id.clone();
                        let pname = meta.name.clone();
                        let toast_u = toast_overlay.clone();
                        let ref_c = refresh_all_weak.clone();
                        uninst_btn.connect_clicked(move |_| {
                            host_u.close_plugin_window(&pid);
                            let res = {
                                let mut mgr = host_u.manager.borrow_mut();
                                mgr.uninstall_plugin(&pid)
                            };
                            match res {
                                Ok(()) => {
                                    toast_u.add_toast(adw::Toast::new(&format!(
                                        "Uninstalled plugin '{}'",
                                        pname
                                    )));
                                }
                                Err(e) => {
                                    toast_u.add_toast(adw::Toast::new(&format!(
                                        "Uninstall failed: {}",
                                        e
                                    )));
                                }
                            }
                            schedule_ui_refresh(&ref_c);
                        });
                    }
                    uninstall_row.add_suffix(&uninst_btn);
                    exp_row.add_row(&uninstall_row);

                    list_group.add(&exp_row);
                }
            }
            installed_box.append(&list_group);

            // Built-in Plugin Samples Group (offline local samples bundled with Ensemble)
            let samples_group = adw::PreferencesGroup::builder()
                .title("Built-in Plugin Samples")
                .description("Starter .er scripts bundled with Ensemble.")
                .build();

            for item in BUILTIN_SAMPLE_PLUGINS {
                let is_installed = plugins.iter().any(|p| p.id == item.id);
                let row = adw::ActionRow::builder()
                    .use_markup(false)
                    .title(&format!("{} (v{})", item.name, item.version))
                    .subtitle(&format!("[{}] — {}", item.category, item.description))
                    .subtitle_lines(2)
                    .build();
                row.add_prefix(&gtk::Image::from_icon_name("text-x-script-symbolic"));

                let btn_label = if is_installed {
                    "Reset Sample"
                } else {
                    "Add Sample"
                };
                let inst_btn = gtk::Button::builder()
                    .label(btn_label)
                    .css_classes(if is_installed {
                        vec!["flat"]
                    } else {
                        vec!["suggested-action"]
                    })
                    .valign(gtk::Align::Center)
                    .build();

                let host_sample = host.clone();
                let toast_sample = toast_overlay.clone();
                let ref_c = refresh_all_weak.clone();
                let filename = item.filename;
                let source = item.source;
                let plugin_name = item.name;
                inst_btn.connect_clicked(move |_| {
                    let dest: PathBuf = AppPaths::plugins_dir().join(filename);
                    let _ = std::fs::create_dir_all(AppPaths::plugins_dir());
                    if let Err(e) = std::fs::write(&dest, source.as_bytes()) {
                        toast_sample.add_toast(adw::Toast::new(&format!(
                            "Failed to write sample plugin file: {}",
                            e
                        )));
                        return;
                    }
                    let load_res = {
                        let mut mgr = host_sample.manager.borrow_mut();
                        mgr.load_plugin_from_file(&dest)
                    };
                    match load_res {
                        Ok(()) => {
                            toast_sample.add_toast(adw::Toast::new(&format!(
                                "Added sample '{}' — expand it in Installed Plugins to run its actions.",
                                plugin_name
                            )));
                            host_sample.notify_chapter_changed();
                        }
                        Err(e) => {
                            toast_sample.add_toast(adw::Toast::new(&format!(
                                "Failed to load sample plugin: {}",
                                e
                            )));
                        }
                    }
                    schedule_ui_refresh(&ref_c);
                });

                row.add_suffix(&inst_btn);
                samples_group.add(&row);
            }
            installed_box.append(&samples_group);

            // ----------------------------------------------------------------
            // Populate Tab 2: Internet & Security (Manage Website Access & Request Logs)
            // ----------------------------------------------------------------
            let whitelist_group = adw::PreferencesGroup::builder()
                .title("Manage Plugins &amp; Website Access")
                .description("Control which plugins can connect to the internet and approve or block specific HTTPS websites. Non-HTTPS links, redirects, and deceptive lookalike domains are automatically blocked and quarantined.")
                .build();

            if plugins.is_empty() {
                let r = adw::ActionRow::builder()
                    .use_markup(false)
                    .title("No Plugins Installed")
                    .subtitle("Install or add a plugin in the Plugins tab to manage its website access.")
                    .build();
                whitelist_group.add(&r);
            } else {
                for meta in &plugins {
                    let mut all_websites: Vec<String> = meta.scan_report.detected_domains.clone();
                    for dom in meta.permissions.allowed_domains.keys() {
                        if !all_websites.contains(dom) {
                            all_websites.push(dom.clone());
                        }
                    }
                    let has_url_requests = meta.scan_report.uses_network
                        || !all_websites.is_empty()
                        || !meta.scan_report.url_security_findings.is_empty();

                    // If the plugin does not request any URLs, gray out the switch and say so directly
                    if !has_url_requests {
                        let no_net_row = adw::ActionRow::builder()
                            .use_markup(false)
                            .title(&meta.name)
                            .subtitle("This plugin does not have URL requests.")
                            .build();
                        no_net_row.add_prefix(&gtk::Image::from_icon_name(
                            "network-offline-symbolic",
                        ));
                        let disabled_sw = gtk::Switch::builder()
                            .active(false)
                            .sensitive(false)
                            .valign(gtk::Align::Center)
                            .tooltip_text("This plugin does not have URL requests.")
                            .build();
                        no_net_row.add_suffix(&disabled_sw);
                        whitelist_group.add(&no_net_row);
                        continue;
                    }

                    let websites_summary = if all_websites.is_empty() {
                        "Uses http.* API".to_string()
                    } else {
                        format!("Websites: {}", all_websites.join(", "))
                    };

                    let net_exp = adw::ExpanderRow::builder()
                        .use_markup(false)
                        .title(&meta.name)
                        .subtitle(&format!(
                            "{} • {}",
                            if meta.permissions.allow_network {
                                "Internet Allowed"
                            } else {
                                "Internet Blocked"
                            },
                            websites_summary
                        ))
                        .build();
                    net_exp.add_prefix(&gtk::Image::from_icon_name(
                        "network-transmit-receive-symbolic",
                    ));

                    // Only the master internet switch on the outer ExpanderRow
                    let net_sw = gtk::Switch::builder()
                        .active(meta.permissions.allow_network)
                        .valign(gtk::Align::Center)
                        .tooltip_text("Allow or block internet access for this plugin")
                        .build();
                    {
                        let host_n = host.clone();
                        let pid = meta.id.clone();
                        let ref_c = refresh_all_weak.clone();
                        net_sw.connect_state_set(move |_, state| {
                            {
                                let mut mgr = host_n.manager.borrow_mut();
                                if let Some(p) = mgr.plugins().into_iter().find(|x| x.id == pid) {
                                    let mut perms = p.permissions;
                                    perms.allow_network = state;
                                    mgr.update_plugin_permissions(&pid, perms);
                                }
                            }
                            schedule_ui_refresh(&ref_c);
                            gtk::glib::Propagation::Proceed
                        });
                    }
                    net_exp.add_suffix(&net_sw);

                    // Show any blocked deceptive/homograph URLs detected in this plugin
                    for finding in &meta.scan_report.url_security_findings {
                        if let Some(ref violation) = finding.violation_summary {
                            let warn_row = adw::ActionRow::builder()
                                .use_markup(false)
                                .title(&format!(
                                    "Blocked Unsafe / Spoofed Link: {}",
                                    finding.character_id_representation
                                ))
                                .subtitle(violation)
                                .css_classes(["error"])
                                .build();
                            net_exp.add_row(&warn_row);
                        }
                    }

                    // List detected & configured domains inside the collapsible area
                    for dom in &all_websites {
                        let is_allowed = meta
                            .permissions
                            .allowed_domains
                            .get(dom)
                            .copied()
                            .unwrap_or(false);
                        let is_detected = meta.scan_report.detected_domains.contains(dom);
                        let d_row = adw::ActionRow::builder()
                            .use_markup(false)
                            .title(&format!("https://{}", dom))
                            .subtitle(match (is_allowed, is_detected) {
                                (true, true) => "Allowed (Requested by plugin script)",
                                (true, false) => "Allowed (Custom whitelisted website)",
                                (false, true) => "Blocked (Requested by plugin script — toggle switch to allow)",
                                (false, false) => "Blocked",
                            })
                            .build();
                        let d_sw = gtk::Switch::builder()
                            .active(is_allowed)
                            .valign(gtk::Align::Center)
                            .tooltip_text("Allow or block requests to this website")
                            .build();
                        {
                            let host_d = host.clone();
                            let pid = meta.id.clone();
                            let d_key = dom.clone();
                            let ref_c = refresh_all_weak.clone();
                            d_sw.connect_state_set(move |_, state| {
                                {
                                    let mut mgr = host_d.manager.borrow_mut();
                                    if let Some(p) = mgr.plugins().into_iter().find(|x| x.id == pid) {
                                        let mut perms = p.permissions;
                                        if state {
                                            perms.allow_network = true;
                                        }
                                        perms.allowed_domains.insert(d_key.clone(), state);
                                        mgr.update_plugin_permissions(&pid, perms);
                                    }
                                }
                                schedule_ui_refresh(&ref_c);
                                gtk::glib::Propagation::Proceed
                            });
                        }
                        d_row.add_suffix(&d_sw);

                        if !is_detected {
                            let rm_btn = gtk::Button::builder()
                                .icon_name("user-trash-symbolic")
                                .css_classes(["flat"])
                                .tooltip_text("Remove website from list")
                                .valign(gtk::Align::Center)
                                .build();
                            let host_rm = host.clone();
                            let pid = meta.id.clone();
                            let d_key = dom.clone();
                            let ref_c = refresh_all_weak.clone();
                            rm_btn.connect_clicked(move |_| {
                                {
                                    let mut mgr = host_rm.manager.borrow_mut();
                                    if let Some(p) = mgr.plugins().into_iter().find(|x| x.id == pid) {
                                        let mut perms = p.permissions;
                                        perms.allowed_domains.remove(&d_key);
                                        mgr.update_plugin_permissions(&pid, perms);
                                    }
                                }
                                schedule_ui_refresh(&ref_c);
                            });
                            d_row.add_suffix(&rm_btn);
                        }
                        net_exp.add_row(&d_row);
                    }

                    // Add new domain row inside collapsible area
                    let add_dom_row = adw::ActionRow::builder()
                        .use_markup(false)
                        .title("Add Website Domain")
                        .subtitle("Allow HTTPS requests to a specific domain (e.g. archiveofourown.org)")
                        .build();
                    let dom_entry = gtk::Entry::builder()
                        .placeholder_text("archiveofourown.org")
                        .valign(gtk::Align::Center)
                        .width_chars(22)
                        .build();
                    let add_dom_btn = gtk::Button::builder()
                        .label("Add")
                        .css_classes(["suggested-action"])
                        .valign(gtk::Align::Center)
                        .build();
                    {
                        let host_add = host.clone();
                        let pid = meta.id.clone();
                        let entry_c = dom_entry.clone();
                        let toast_c = toast_overlay.clone();
                        let win_c = window_for_dlg.clone();
                        let ref_c = refresh_all_weak.clone();
                        add_dom_btn.connect_clicked(move |_| {
                            let raw_dom = entry_c.text().trim().to_string();
                            if raw_dom.is_empty() {
                                return;
                            }
                            let test_url = if raw_dom.contains("://") {
                                raw_dom.clone()
                            } else {
                                format!("https://{}", raw_dom.trim_start_matches("*."))
                            };
                            let sec = analyze_url_security(&test_url);
                            if sec.has_homograph_or_non_ascii || sec.is_punycode_idn {
                                let dlg = adw::MessageDialog::builder()
                                    .transient_for(&win_c)
                                    .modal(true)
                                    .heading("Blocked Deceptive Homograph Domain")
                                    .body(&format!(
                                        "Ensemble blocked adding this domain because it contains deceptive non-ASCII / homograph lookalike characters designed to misguide users:\n\nCharacter-ID View:\n{}\n\nHost Character IDs:\n{}",
                                        sec.character_id_representation,
                                        sec.host_character_id_dump
                                    ))
                                    .build();
                                dlg.add_response("ok", "Understood");
                                dlg.set_response_appearance("ok", adw::ResponseAppearance::Destructive);
                                dlg.present();
                                return;
                            }
                            let clean_dom = if raw_dom.starts_with("*.") {
                                format!("*.{}", sec.raw_host.to_lowercase())
                            } else {
                                sec.raw_host.to_lowercase()
                            };
                            if clean_dom.is_empty() {
                                toast_c.add_toast(adw::Toast::new("Invalid domain name"));
                                return;
                            }
                            {
                                let mut mgr = host_add.manager.borrow_mut();
                                if let Some(p) = mgr.plugins().into_iter().find(|x| x.id == pid) {
                                    let mut perms = p.permissions;
                                    perms.allow_network = true;
                                    perms.allowed_domains.insert(clean_dom.clone(), true);
                                    mgr.update_plugin_permissions(&pid, perms);
                                }
                            }
                            toast_c.add_toast(adw::Toast::new(&format!(
                                "Allowed HTTPS website '{}'",
                                clean_dom
                            )));
                            schedule_ui_refresh(&ref_c);
                        });
                    }
                    add_dom_row.add_suffix(&dom_entry);
                    add_dom_row.add_suffix(&add_dom_btn);
                    net_exp.add_row(&add_dom_row);

                    whitelist_group.add(&net_exp);
                }
            }
            net_box.append(&whitelist_group);

            // Request & Security Logs
            let log_group = adw::PreferencesGroup::builder()
                .title("Network Request &amp; Security Logs")
                .description("Live log of plugin HTTPS requests, responses, blocked redirects, and Character-ID spoofing alerts.")
                .build();

            let log_btn_row = gtk::Box::new(gtk::Orientation::Horizontal, 8);
            log_btn_row.set_margin_bottom(8);
            let refresh_log_btn = gtk::Button::builder()
                .icon_name("view-refresh-symbolic")
                .label("Refresh Logs")
                .build();
            let clear_log_btn = gtk::Button::builder()
                .icon_name("user-trash-symbolic")
                .label("Clear Logs")
                .css_classes(["destructive-action"])
                .build();
            log_btn_row.append(&refresh_log_btn);
            log_btn_row.append(&clear_log_btn);
            log_group.add(&log_btn_row);

            let log_frame = gtk::Frame::new(None);
            let log_scroll = gtk::ScrolledWindow::builder()
                .min_content_height(220)
                .max_content_height(340)
                .hexpand(true)
                .propagate_natural_height(false)
                .build();
            let log_tv = gtk::TextView::builder()
                .editable(false)
                .monospace(true)
                .wrap_mode(gtk::WrapMode::WordChar)
                .top_margin(8)
                .bottom_margin(8)
                .left_margin(10)
                .right_margin(10)
                .build();
            log_tv
                .buffer()
                .set_text(&PluginLogger::read_network_and_security_logs(200));
            log_scroll.set_child(Some(&log_tv));
            log_frame.set_child(Some(&log_scroll));
            log_group.add(&log_frame);

            {
                let buf = log_tv.buffer();
                refresh_log_btn.connect_clicked(move |_| {
                    buf.set_text(&PluginLogger::read_network_and_security_logs(200));
                });
            }
            {
                let buf = log_tv.buffer();
                clear_log_btn.connect_clicked(move |_| {
                    PluginLogger::clear_logs();
                    buf.set_text(&PluginLogger::read_network_and_security_logs(200));
                });
            }
            net_box.append(&log_group);

            // ----------------------------------------------------------------
            // Populate Tab 3: Marketplace Repository Configuration (WIP)
            // ----------------------------------------------------------------
            let current_repo_url = read_marketplace_repo_url();
            let repo_cfg_group = adw::PreferencesGroup::builder()
                .title("Plugin Marketplace Repository Configuration (WIP)")
                .description("Configure the HTTPS repository domain used by the Ensemble Plugin Marketplace. Online repository sync is currently a Work in Progress (WIP).")
                .build();

            let repo_domain_row = adw::ActionRow::builder()
                .use_markup(false)
                .title("Repository Domain URL")
                .subtitle("Must use https:// and standard ASCII domain characters (homographs & redirects are rejected)")
                .build();
            repo_domain_row.add_prefix(&gtk::Image::from_icon_name("network-server-symbolic"));

            let repo_entry = gtk::Entry::builder()
                .text(&current_repo_url)
                .placeholder_text(DEFAULT_MARKETPLACE_REPO_URL)
                .valign(gtk::Align::Center)
                .width_chars(28)
                .build();

            let save_repo_btn = gtk::Button::builder()
                .label("Save Domain")
                .css_classes(["suggested-action"])
                .valign(gtk::Align::Center)
                .build();

            let reset_repo_btn = gtk::Button::builder()
                .label("Reset")
                .valign(gtk::Align::Center)
                .tooltip_text("Reset to default official repository domain")
                .build();

            {
                let entry_c = repo_entry.clone();
                let toast_c = toast_overlay.clone();
                let win_c = window_for_dlg.clone();
                let ref_c = refresh_all_weak.clone();
                save_repo_btn.connect_clicked(move |_| {
                    let raw = entry_c.text().trim().to_string();
                    if raw.is_empty() {
                        return;
                    }
                    let normalized = if raw.contains("://") {
                        raw
                    } else {
                        format!("https://{}", raw)
                    };
                    let sec = analyze_url_security(&normalized);
                    if let Some(ref violation) = sec.violation_summary {
                        let dlg = adw::MessageDialog::builder()
                            .transient_for(&win_c)
                            .modal(true)
                            .heading("Blocked Unsafe or Deceptive Repository Domain")
                            .body(&format!(
                                "The repository domain could not be saved because it failed Ensemble's HTTPS & Character-ID security check:\n\nCharacter-ID View:\n{}\n\n{}",
                                sec.character_id_representation,
                                violation
                            ))
                            .build();
                        dlg.add_response("ok", "Understood");
                        dlg.set_response_appearance("ok", adw::ResponseAppearance::Destructive);
                        dlg.present();
                        return;
                    }
                    if let Err(e) = write_marketplace_repo_url(&normalized) {
                        toast_c.add_toast(adw::Toast::new(&format!(
                            "Failed to save repository domain: {}",
                            e
                        )));
                        return;
                    }
                    toast_c.add_toast(adw::Toast::new(&format!(
                        "Saved Marketplace repository domain: {}",
                        normalized
                    )));
                    schedule_ui_refresh(&ref_c);
                });
            }

            {
                let toast_c = toast_overlay.clone();
                let ref_c = refresh_all_weak.clone();
                reset_repo_btn.connect_clicked(move |_| {
                    let _ = write_marketplace_repo_url(DEFAULT_MARKETPLACE_REPO_URL);
                    toast_c.add_toast(adw::Toast::new("Reset repository domain to default"));
                    schedule_ui_refresh(&ref_c);
                });
            }

            repo_domain_row.add_suffix(&repo_entry);
            repo_domain_row.add_suffix(&save_repo_btn);
            repo_domain_row.add_suffix(&reset_repo_btn);
            repo_cfg_group.add(&repo_domain_row);

            let repo_status_row = adw::ActionRow::builder()
                .use_markup(false)
                .title("Repository Index Status")
                .subtitle(&format!(
                    "Configured Endpoint: {} • Status: Work in Progress (WIP)",
                    current_repo_url
                ))
                .build();
            let wip_lbl = gtk::Label::builder()
                .label("WIP")
                .css_classes(["warning", "caption", "bold"])
                .valign(gtk::Align::Center)
                .build();
            repo_status_row.add_suffix(&wip_lbl);
            repo_cfg_group.add(&repo_status_row);

            repo_box.append(&repo_cfg_group);
        })
    };

    *refresh_all.borrow_mut() = Some(rebuild_ui.clone());
    rebuild_ui();

    window.present();
}

fn strip_html_tags(html: &str) -> String {
    let mut out = String::with_capacity(html.len());
    let mut in_tag = false;
    for ch in html.chars() {
        match ch {
            '<' => in_tag = true,
            '>' => in_tag = false,
            _ if !in_tag => out.push(ch),
            _ => {}
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builtin_sample_plugins_load_and_execute_cleanly() {
        let mut mgr = PluginManager::new();
        let ctx = PluginHostContext {
            current_chapter_index: 0,
            current_chapter_title: "Chapter 1: Test".to_string(),
            current_chapter_html: "<p>Hello world</p>".to_string(),
            current_plain_text: "Hello world".to_string(),
            selected_text: "Hello".to_string(),
            work_skin_css: String::new(),
            word_count: 420,
            chapters: vec![ChapterSummary {
                title: "Chapter 1: Test".to_string(),
                html: "<p>Hello world</p>".to_string(),
                word_count: 420,
            }],
            ui_widget_values: BTreeMap::new(),
        };

        for sample in BUILTIN_SAMPLE_PLUGINS {
            let res = mgr.load_plugin_from_source(
                sample.id,
                sample.name,
                sample.source,
                sample.filename,
                "uidp_test_sample_secret",
            );
            assert!(
                res.is_ok(),
                "Built-in sample '{}' failed to load: {:?}",
                sample.id,
                res.err()
            );
        }

        mgr.notify_chapter_changed(&ctx);
        let actions = mgr.toolbar_actions();
        assert!(!actions.is_empty());

        for act in actions {
            mgr.trigger_toolbar_action(&act.plugin_id, &act.func_name, &ctx);
            let errs = mgr.take_emitted_errors();
            assert!(
                errs.is_empty(),
                "Sample action {}.{} emitted error: {:?}",
                act.plugin_id,
                act.func_name,
                errs
            );
        }
    }
}
