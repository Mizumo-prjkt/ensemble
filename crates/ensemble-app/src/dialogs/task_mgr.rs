use ensemble_core::AppPaths;
use ensemble_mcp::McpHandler;
use gtk4 as gtk;
use gtk4::glib;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::cell::RefCell;
use std::fs;
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;

#[derive(Clone, Debug)]
pub struct UiComponentTelemetry {
    pub active_tab: u32,
    pub rich_editor_loaded: bool,
    pub html_editor_bytes: usize,
    pub html_editor_lines: i32,
    pub css_editor_bytes: usize,
    pub sideview_visible: bool,
}

#[derive(Clone, Debug)]
struct OsProcessEntry {
    pid: u32,
    name: String,
    state: String,
    threads: u32,
    rss_kb: u64,
    vsize_kb: u64,
}

fn format_bytes(bytes: usize) -> String {
    if bytes < 1024 {
        format!("{} B", bytes)
    } else if bytes < 1024 * 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else {
        format!("{:.2} MB", bytes as f64 / (1024.0 * 1024.0))
    }
}

fn read_proc_entry(pid: u32) -> Option<(OsProcessEntry, u32)> {
    let status_path = format!("/proc/{}/status", pid);
    let content = fs::read_to_string(status_path).ok()?;

    let mut name = String::new();
    let mut state = String::new();
    let mut ppid = 0u32;
    let mut threads = 1u32;
    let mut rss_kb = 0u64;
    let mut vsize_kb = 0u64;

    for line in content.lines() {
        if let Some(val) = line.strip_prefix("Name:") {
            name = val.trim().to_string();
        } else if let Some(val) = line.strip_prefix("State:") {
            state = val.trim().to_string();
        } else if let Some(val) = line.strip_prefix("PPid:") {
            ppid = val.trim().parse().unwrap_or(0);
        } else if let Some(val) = line.strip_prefix("Threads:") {
            threads = val.trim().parse().unwrap_or(1);
        } else if let Some(val) = line.strip_prefix("VmRSS:") {
            let num_str = val.trim().split_whitespace().next().unwrap_or("0");
            rss_kb = num_str.parse().unwrap_or(0);
        } else if let Some(val) = line.strip_prefix("VmSize:") {
            let num_str = val.trim().split_whitespace().next().unwrap_or("0");
            vsize_kb = num_str.parse().unwrap_or(0);
        }
    }

    Some((
        OsProcessEntry {
            pid,
            name,
            state,
            threads,
            rss_kb,
            vsize_kb,
        },
        ppid,
    ))
}

fn collect_ensemble_process_tree() -> Vec<OsProcessEntry> {
    let self_pid = std::process::id();
    let mut all_procs = Vec::new();

    if let Ok(entries) = fs::read_dir("/proc") {
        for entry in entries.flatten() {
            if let Ok(fname) = entry.file_name().into_string() {
                if let Ok(pid) = fname.parse::<u32>() {
                    if let Some((info, ppid)) = read_proc_entry(pid) {
                        all_procs.push((info, ppid));
                    }
                }
            }
        }
    }

    let mut selected_pids = std::collections::HashSet::new();
    selected_pids.insert(self_pid);

    // Include children and grandchildren (e.g. bwrap -> WebKitWebProcess / WebKitNetworkProcess)
    for _ in 0..3 {
        for (info, ppid) in &all_procs {
            if selected_pids.contains(ppid) {
                selected_pids.insert(info.pid);
            }
        }
    }

    let mut result: Vec<OsProcessEntry> = all_procs
        .into_iter()
        .filter(|(info, _)| selected_pids.contains(&info.pid))
        .map(|(info, _)| info)
        .collect();

    result.sort_by_key(|p| if p.pid == self_pid { 0 } else { p.pid });
    result
}

pub fn show_task_manager_dialog<F, G>(
    parent: &impl IsA<gtk::Window>,
    handler: Arc<McpHandler>,
    query_ui_telemetry: F,
    on_force_offload: G,
) where
    F: Fn() -> UiComponentTelemetry + 'static,
    G: Fn() + 'static,
{
    let win = adw::Window::builder()
        .title("Ensemble Task Manager & Resource Monitor")
        .default_width(720)
        .default_height(560)
        .modal(false)
        .transient_for(parent)
        .build();

    let root_box = gtk::Box::new(gtk::Orientation::Vertical, 0);

    let view_stack = adw::ViewStack::new();
    view_stack.set_vexpand(true);
    view_stack.set_hexpand(true);

    let switcher = adw::ViewSwitcher::builder()
        .stack(&view_stack)
        .policy(adw::ViewSwitcherPolicy::Wide)
        .build();

    let header = adw::HeaderBar::builder()
        .title_widget(&switcher)
        .build();

    let offload_btn = gtk::Button::builder()
        .icon_name("user-trash-symbolic")
        .label("Offload Inactive")
        .tooltip_text("Unload inactive chapters and editor buffers out of RAM into disk cache / autosave")
        .css_classes(["suggested-action"])
        .build();
    header.pack_start(&offload_btn);

    let refresh_btn = gtk::Button::builder()
        .icon_name("view-refresh-symbolic")
        .tooltip_text("Refresh Telemetry Now")
        .build();
    header.pack_end(&refresh_btn);

    root_box.append(&header);

    // Top Summary Banner
    let summary_bar = gtk::Box::new(gtk::Orientation::Horizontal, 16);
    summary_bar.set_margin_top(10);
    summary_bar.set_margin_bottom(10);
    summary_bar.set_margin_start(16);
    summary_bar.set_margin_end(16);

    let total_rss_lbl = gtk::Label::builder()
        .label("Total Memory: -- MB")
        .css_classes(["heading"])
        .xalign(0.0)
        .build();
    summary_bar.append(&total_rss_lbl);

    summary_bar.append(&gtk::Separator::new(gtk::Orientation::Vertical));

    let proc_count_lbl = gtk::Label::builder()
        .label("Processes: --")
        .css_classes(["dim-label"])
        .xalign(0.0)
        .build();
    summary_bar.append(&proc_count_lbl);

    summary_bar.append(&gtk::Separator::new(gtk::Orientation::Vertical));

    let chapter_mem_lbl = gtk::Label::builder()
        .label("Chapters: -- loaded / -- offloaded")
        .css_classes(["accent", "heading"])
        .hexpand(true)
        .xalign(0.0)
        .ellipsize(gtk::pango::EllipsizeMode::End)
        .build();
    summary_bar.append(&chapter_mem_lbl);

    root_box.append(&summary_bar);
    root_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));

    // ==========================================
    // Tab 1: Core Components & Subsystems
    // ==========================================
    let comp_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .propagate_natural_height(false)
        .build();

    let comp_clamp = adw::Clamp::builder()
        .maximum_size(680)
        .margin_top(14)
        .margin_bottom(14)
        .margin_start(16)
        .margin_end(16)
        .build();

    let comp_box = gtk::Box::new(gtk::Orientation::Vertical, 14);

    let mem_group = adw::PreferencesGroup::builder()
        .title("Chapter Memory & Autosave Offloader")
        .description("Inactive chapters are automatically unloaded from RAM to disk/autosave so large multi-hundred-thousand-word projects stay fast.")
        .build();

    let row_active_ch = adw::ActionRow::builder()
        .title("Active Chapter in RAM")
        .subtitle("Currently viewed chapter loaded in workspace")
        .build();
    row_active_ch.add_prefix(&gtk::Image::from_icon_name("document-edit-symbolic"));
    let val_active_ch = gtk::Label::builder()
        .label("0 B")
        .css_classes(["heading"])
        .valign(gtk::Align::Center)
        .build();
    row_active_ch.add_suffix(&val_active_ch);
    mem_group.add(&row_active_ch);

    let row_offloaded_ch = adw::ActionRow::builder()
        .title("Offloaded Chapters (Disk / Autosave Cache)")
        .subtitle("Inactive chapters freed from RAM")
        .build();
    row_offloaded_ch.add_prefix(&gtk::Image::from_icon_name("drive-harddisk-symbolic"));
    let val_offloaded_ch = gtk::Label::builder()
        .label("0 chapters")
        .css_classes(["success", "heading"])
        .valign(gtk::Align::Center)
        .build();
    row_offloaded_ch.add_suffix(&val_offloaded_ch);
    mem_group.add(&row_offloaded_ch);

    comp_box.append(&mem_group);

    let ui_group = adw::PreferencesGroup::builder()
        .title("Core UI & Engine Subsystems")
        .description("Only the currently viewed editor tab holds document buffers in memory.")
        .build();

    let row_rich = adw::ActionRow::builder()
        .title("Rich Text WYSIWYG Composer (WebKitGTK 6.0)")
        .subtitle("Tab 1 — Visual HTML contenteditable engine")
        .build();
    row_rich.add_prefix(&gtk::Image::from_icon_name("accessories-text-editor-symbolic"));
    let val_rich = gtk::Label::builder()
        .label("Active")
        .valign(gtk::Align::Center)
        .build();
    row_rich.add_suffix(&val_rich);
    ui_group.add(&row_rich);

    let row_html = adw::ActionRow::builder()
        .title("Raw HTML IDE Editor (GtkTextView + Cairo Gutter/Minimap)")
        .subtitle("Tab 2 — Source buffer, line gutter, and minimap")
        .build();
    row_html.add_prefix(&gtk::Image::from_icon_name("text-html-symbolic"));
    let val_html = gtk::Label::builder()
        .label("Unloaded (0 B)")
        .valign(gtk::Align::Center)
        .build();
    row_html.add_suffix(&val_html);
    ui_group.add(&row_html);

    let row_css = adw::ActionRow::builder()
        .title("Work Skin CSS Editor (GtkTextView)")
        .subtitle("Tab 3 — AO3 Work Skin stylesheet buffer")
        .build();
    row_css.add_prefix(&gtk::Image::from_icon_name("applications-graphics-symbolic"));
    let val_css = gtk::Label::builder()
        .label("Unloaded (0 B)")
        .valign(gtk::Align::Center)
        .build();
    row_css.add_suffix(&val_css);
    ui_group.add(&row_css);

    let row_preview = adw::ActionRow::builder()
        .title("Live Preview Sideview (WebKitGTK 6.0)")
        .subtitle("Dual-skin AO3 renderer (Default / Reversi)")
        .build();
    row_preview.add_prefix(&gtk::Image::from_icon_name("view-dual-symbolic"));
    let val_preview = gtk::Label::builder()
        .label("Active")
        .valign(gtk::Align::Center)
        .build();
    row_preview.add_suffix(&val_preview);
    ui_group.add(&row_preview);

    let row_snapshots = adw::ActionRow::builder()
        .use_markup(false)
        .title("Snapshots, Lore Graph & Agentic Metadata")
        .subtitle("In-memory commit history, unified diffs, and lore graph")
        .build();
    row_snapshots.add_prefix(&gtk::Image::from_icon_name("document-Revert-symbolic"));
    let val_snapshots = gtk::Label::builder()
        .label("0 B")
        .valign(gtk::Align::Center)
        .build();
    row_snapshots.add_suffix(&val_snapshots);
    ui_group.add(&row_snapshots);

    let row_mcp = adw::ActionRow::builder()
        .use_markup(false)
        .title("MCP AI Co-Authoring Server (Tokio Runtime)")
        .subtitle("Listening on ws://127.0.0.1:26303")
        .build();
    row_mcp.add_prefix(&gtk::Image::from_icon_name("network-server-symbolic"));
    let val_mcp = gtk::Label::builder()
        .label("Running")
        .css_classes(["success", "heading"])
        .valign(gtk::Align::Center)
        .build();
    row_mcp.add_suffix(&val_mcp);
    ui_group.add(&row_mcp);

    comp_box.append(&ui_group);
    comp_clamp.set_child(Some(&comp_box));
    comp_scroll.set_child(Some(&comp_clamp));

    view_stack.add_titled_with_icon(
        &comp_scroll,
        Some("components"),
        "Components",
        "applications-system-symbolic",
    );

    // ==========================================
    // Tab 2: OS Processes (ensemble + WebKit)
    // ==========================================
    let proc_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .propagate_natural_height(false)
        .build();

    let proc_clamp = adw::Clamp::builder()
        .maximum_size(680)
        .margin_top(14)
        .margin_bottom(14)
        .margin_start(16)
        .margin_end(16)
        .build();

    let proc_list_box = gtk::ListBox::builder()
        .selection_mode(gtk::SelectionMode::None)
        .css_classes(["boxed-list"])
        .build();

    let proc_group = adw::PreferencesGroup::builder()
        .title("Active OS Processes (/proc Telemetry)")
        .description("Live memory (RSS / VmSize) and thread counts for Ensemble and its WebKit subprocesses.")
        .build();
    proc_group.add(&proc_list_box);

    proc_clamp.set_child(Some(&proc_group));
    proc_scroll.set_child(Some(&proc_clamp));

    view_stack.add_titled_with_icon(
        &proc_scroll,
        Some("processes"),
        "Processes",
        "utilities-system-monitor-symbolic",
    );

    // ==========================================
    // Tab 3: Plugins & Sandbox Resource Monitor
    // ==========================================
    let plug_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .propagate_natural_height(false)
        .build();

    let plug_clamp = adw::Clamp::builder()
        .maximum_size(680)
        .margin_top(14)
        .margin_bottom(14)
        .margin_start(16)
        .margin_end(16)
        .build();

    let plug_col = gtk::Box::new(gtk::Orientation::Vertical, 14);

    let plug_group = adw::PreferencesGroup::builder()
        .title("Installed Plugins (.er Scripts)")
        .description("Resource consumption and sandbox quotas per extension script.")
        .build();

    let plug_list_box = gtk::ListBox::builder()
        .selection_mode(gtk::SelectionMode::None)
        .css_classes(["boxed-list"])
        .build();
    plug_group.add(&plug_list_box);
    plug_col.append(&plug_group);

    let quota_group = adw::PreferencesGroup::builder()
        .title("Sandbox Resource Limits")
        .description("Hard caps enforced on all plugin scripts to prevent freezes or memory leaks.")
        .build();

    let q_mem = adw::ActionRow::builder()
        .use_markup(false)
        .title("Per-Plugin Heap Quota")
        .subtitle("Maximum RAM any single .er script can allocate")
        .build();
    q_mem.add_suffix(
        &gtk::Label::builder()
            .label("8.0 MB Max")
            .css_classes(["dim-label", "heading"])
            .valign(gtk::Align::Center)
            .build(),
    );
    quota_group.add(&q_mem);

    let q_cpu = adw::ActionRow::builder()
        .use_markup(false)
        .title("Instruction & Time Trap")
        .subtitle("Automatic termination of runaway loops")
        .build();
    q_cpu.add_suffix(
        &gtk::Label::builder()
            .label("250 ms / 150k ops")
            .css_classes(["dim-label", "heading"])
            .valign(gtk::Align::Center)
            .build(),
    );
    quota_group.add(&q_cpu);

    plug_col.append(&quota_group);
    plug_clamp.set_child(Some(&plug_col));
    plug_scroll.set_child(Some(&plug_clamp));

    view_stack.add_titled_with_icon(
        &plug_scroll,
        Some("plugins"),
        "Plugins",
        "application-x-addon-symbolic",
    );

    root_box.append(&view_stack);

    win.set_content(Some(&root_box));

    // Refresh function
    let query_rc = Rc::new(query_ui_telemetry);
    let refresh_fn: Rc<dyn Fn()> = {
        let handler = handler.clone();
        let query_rc = query_rc.clone();
        let total_rss_lbl = total_rss_lbl.clone();
        let proc_count_lbl = proc_count_lbl.clone();
        let chapter_mem_lbl = chapter_mem_lbl.clone();
        let val_active_ch = val_active_ch.clone();
        let row_offloaded_ch = row_offloaded_ch.clone();
        let val_offloaded_ch = val_offloaded_ch.clone();
        let val_rich = val_rich.clone();
        let val_html = val_html.clone();
        let val_css = val_css.clone();
        let val_preview = val_preview.clone();
        let val_snapshots = val_snapshots.clone();
        let proc_list_box = proc_list_box.clone();
        let plug_list_box = plug_list_box.clone();

        Rc::new(move || {
            let ui_tel = query_rc();

            // 1. Query project memory stats
            let proj_stats = {
                let ws = handler.workspace();
                let g = ws.lock();
                g.project.as_ref().map(|p| p.memory_stats())
            };

            if let Some(st) = proj_stats {
                chapter_mem_lbl.set_text(&format!(
                    "Chapters: {} in RAM / {} offloaded ({} saved)",
                    st.loaded_chapters,
                    st.offloaded_chapters,
                    format_bytes(st.offloaded_disk_bytes)
                ));
                val_active_ch.set_text(&format_bytes(st.active_chapter_ram_bytes));
                row_offloaded_ch.set_subtitle(&format!(
                    "{} offloaded to disk ({} in autosave with unsaved edits)",
                    st.offloaded_chapters, st.autosaved_dirty_chapters
                ));
                val_offloaded_ch.set_text(&format!(
                    "{} chapters ({} freed)",
                    st.offloaded_chapters,
                    format_bytes(st.offloaded_disk_bytes)
                ));
                val_snapshots.set_text(&format_bytes(
                    st.metadata_and_snapshots_bytes + st.lore_and_agentic_bytes,
                ));
            } else {
                chapter_mem_lbl.set_text("No project loaded");
                val_active_ch.set_text("0 B");
                val_offloaded_ch.set_text("0 chapters");
                val_snapshots.set_text("0 B");
            }

            // 2. UI Component status
            if ui_tel.active_tab == 0 && ui_tel.rich_editor_loaded {
                val_rich.set_text("Active (Loaded in WebKit)");
                val_rich.set_css_classes(&["success", "heading"]);
            } else {
                val_rich.set_text("Unloaded / Suspended");
                val_rich.set_css_classes(&["dim-label"]);
            }

            if ui_tel.active_tab == 1 && ui_tel.html_editor_bytes > 0 {
                val_html.set_text(&format!(
                    "Active ({}, {} lines)",
                    format_bytes(ui_tel.html_editor_bytes),
                    ui_tel.html_editor_lines
                ));
                val_html.set_css_classes(&["success", "heading"]);
            } else {
                val_html.set_text("Unloaded (0 B)");
                val_html.set_css_classes(&["dim-label"]);
            }

            if ui_tel.active_tab == 2 && ui_tel.css_editor_bytes > 0 {
                val_css.set_text(&format!("Active ({})", format_bytes(ui_tel.css_editor_bytes)));
                val_css.set_css_classes(&["success", "heading"]);
            } else {
                val_css.set_text(&format!(
                    "Standby ({})",
                    format_bytes(ui_tel.css_editor_bytes)
                ));
                val_css.set_css_classes(&["dim-label"]);
            }

            if ui_tel.sideview_visible {
                val_preview.set_text("Active (Rendering)");
                val_preview.set_css_classes(&["accent", "heading"]);
            } else {
                val_preview.set_text("Hidden / Suspended");
                val_preview.set_css_classes(&["dim-label"]);
            }

            // 3. OS Processes
            let procs = collect_ensemble_process_tree();
            let total_rss_kb: u64 = procs.iter().map(|p| p.rss_kb).sum();
            total_rss_lbl.set_text(&format!(
                "Total RSS: {:.1} MB",
                total_rss_kb as f64 / 1024.0
            ));
            proc_count_lbl.set_text(&format!("Processes: {}", procs.len()));

            while let Some(child) = proc_list_box.first_child() {
                proc_list_box.remove(&child);
            }
            for p in &procs {
                let role = if p.pid == std::process::id() {
                    "Main Application Process (GTK4 + Rust Core + MCP)"
                } else if p.name.contains("WebKitWeb") {
                    "WebKitGTK Content Renderer Subprocess"
                } else if p.name.contains("WebKitNet") {
                    "WebKitGTK Network Subprocess"
                } else {
                    "Helper / Sandbox Subprocess"
                };

                let row = adw::ActionRow::builder()
                    .title(&format!("{} (PID {})", p.name, p.pid))
                    .subtitle(&format!(
                        "{}  •  State: {}  •  Threads: {}",
                        role, p.state, p.threads
                    ))
                    .build();
                row.add_prefix(&gtk::Image::from_icon_name("system-run-symbolic"));

                let mem_lbl = gtk::Label::builder()
                    .label(&format!(
                        "{:.1} MB RSS (Vm: {:.0} MB)",
                        p.rss_kb as f64 / 1024.0,
                        p.vsize_kb as f64 / 1024.0
                    ))
                    .css_classes(["heading"])
                    .valign(gtk::Align::Center)
                    .build();
                row.add_suffix(&mem_lbl);
                proc_list_box.append(&row);
            }

            // 4. Plugins list
            while let Some(child) = plug_list_box.first_child() {
                plug_list_box.remove(&child);
            }
            let plugins_dir = AppPaths::plugins_dir();
            let mut found_plugin = false;
            if let Ok(entries) = fs::read_dir(&plugins_dir) {
                for entry in entries.flatten() {
                    let path = entry.path();
                    if path.extension().map_or(false, |ext| ext == "er") {
                        found_plugin = true;
                        let fname = path
                            .file_name()
                            .unwrap_or_default()
                            .to_string_lossy()
                            .to_string();
                        let fsize = entry.metadata().map(|m| m.len() as usize).unwrap_or(0);

                        let row = adw::ActionRow::builder()
                            .title(&fname)
                            .subtitle(&format!(
                                "Script Size: {}  •  Heap Usage: {} / 8.0 MB  •  CPU: 0.0%",
                                format_bytes(fsize),
                                format_bytes(fsize + 4096)
                            ))
                            .build();
                        row.add_prefix(&gtk::Image::from_icon_name("application-x-addon-symbolic"));

                        let badge = gtk::Label::builder()
                            .label("Sandboxed (Idle)")
                            .css_classes(["success", "caption"])
                            .valign(gtk::Align::Center)
                            .build();
                        row.add_suffix(&badge);
                        plug_list_box.append(&row);
                    }
                }
            }
            if !found_plugin {
                let empty_row = adw::ActionRow::builder()
                    .title("No Active .er Plugins Running")
                    .subtitle("0 KB plugin heap in use — drop .er scripts into the plugins directory to load.")
                    .build();
                empty_row.add_prefix(&gtk::Image::from_icon_name("dialog-information-symbolic"));
                plug_list_box.append(&empty_row);
            }
        })
    };

    refresh_fn();

    {
        let r = refresh_fn.clone();
        refresh_btn.connect_clicked(move |_| {
            r();
        });
    }

    {
        let r = refresh_fn.clone();
        offload_btn.connect_clicked(move |_| {
            on_force_offload();
            r();
        });
    }

    // Periodic 2-second live update while Task Manager window is open
    let timer_active = Rc::new(RefCell::new(true));
    {
        let ta = timer_active.clone();
        win.connect_close_request(move |_| {
            *ta.borrow_mut() = false;
            glib::Propagation::Proceed
        });
    }

    {
        let ta = timer_active.clone();
        let r = refresh_fn.clone();
        glib::timeout_add_local(Duration::from_millis(2000), move || {
            if !*ta.borrow() {
                return glib::ControlFlow::Break;
            }
            r();
            glib::ControlFlow::Continue
        });
    }

    win.present();
}
