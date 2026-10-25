use ensemble_core::diff::DiffUtil;
use ensemble_mcp::McpHandler;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;
use webkit6::prelude::*;

const STANDARD_RATINGS: &[&str] = &[
    "Not Rated",
    "General Audiences",
    "Teen And Up",
    "Mature",
    "Explicit",
];

const STANDARD_WARNINGS: &[&str] = &[
    "Choose Not To Use Archive Warnings",
    "Graphic Depictions Of Violence",
    "Major Character Death",
    "No Archive Warnings Apply",
    "Rape/Non-Con",
    "Underage Sex",
];

const STANDARD_CATEGORIES: &[&str] = &[
    "F/F",
    "F/M",
    "Gen",
    "M/M",
    "Multi",
    "Other",
];

pub fn show_project_properties_dialog<F>(
    parent: &impl IsA<gtk4::Window>,
    handler: Arc<McpHandler>,
    on_saved: F,
) where
    F: Fn() + 'static,
{
    let window = adw::Window::builder()
        .title("Project Properties")
        .transient_for(parent)
        .modal(true)
        .default_width(780)
        .default_height(640)
        .build();

    let root = gtk4::Box::new(gtk4::Orientation::Vertical, 0);

    // HeaderBar with clean Save and Cancel buttons
    let header = adw::HeaderBar::new();
    let win_title = adw::WindowTitle::new("Project Properties", "Work Metadata & Snapshots");
    header.set_title_widget(Some(&win_title));

    let save_btn = gtk4::Button::builder()
        .label("Save")
        .css_classes(["suggested-action"])
        .build();

    header.pack_end(&save_btn);
    root.append(&header);

    // ViewSwitcher title bar / tabs
    let view_stack = adw::ViewStack::new();
    view_stack.set_vexpand(true);

    let switcher = adw::ViewSwitcher::builder()
        .stack(&view_stack)
        .policy(adw::ViewSwitcherPolicy::Wide)
        .build();
    let switcher_box = gtk4::Box::new(gtk4::Orientation::Horizontal, 0);
    switcher_box.set_halign(gtk4::Align::Center);
    switcher_box.set_margin_top(8);
    switcher_box.set_margin_bottom(8);
    switcher_box.append(&switcher);
    root.append(&switcher_box);

    // Extract current project state
    let (
        title_val,
        author_val,
        rating_val,
        fandoms_val,
        relationships_val,
        characters_val,
        tags_val,
        warnings_val,
        categories_val,
    ) = {
        let ws = handler.workspace();
        let g = ws.lock();
        if let Some(ref proj) = g.project {
            (
                proj.title.clone(),
                proj.author.clone(),
                proj.rating.clone(),
                proj.fandoms.join(", "),
                proj.relationships.join(", "),
                proj.characters.join(", "),
                proj.tags.join(", "),
                proj.archive_warnings.clone(),
                proj.categories.clone(),
            )
        } else {
            (
                String::new(),
                String::new(),
                "Not Rated".to_string(),
                String::new(),
                String::new(),
                String::new(),
                String::new(),
                Vec::new(),
                Vec::new(),
            )
        }
    };

    // ==========================================
    // Tab 1: Work Metadata & Tags
    // ==========================================
    let meta_scroll = gtk4::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk4::PolicyType::Never)
        .build();

    let meta_page_box = gtk4::Box::new(gtk4::Orientation::Vertical, 16);
    meta_page_box.set_margin_top(12);
    meta_page_box.set_margin_bottom(24);
    meta_page_box.set_margin_start(20);
    meta_page_box.set_margin_end(20);

    let meta_clamp = adw::Clamp::builder().maximum_size(700).build();

    let meta_content = gtk4::Box::new(gtk4::Orientation::Vertical, 16);

    // 1. Work Details Group
    let group_details = adw::PreferencesGroup::builder()
        .title("Work Details")
        .description("AO3 title, author, and content rating")
        .build();

    let title_row = adw::EntryRow::builder()
        .title("Title")
        .text(&title_val)
        .build();
    group_details.add(&title_row);

    let author_row = adw::EntryRow::builder()
        .title("Author / Pseud")
        .text(&author_val)
        .build();
    group_details.add(&author_row);

    let rating_model = gtk4::StringList::new(STANDARD_RATINGS);
    let rating_row = adw::ComboRow::builder()
        .title("Rating")
        .model(&rating_model)
        .build();

    let initial_rating_idx = STANDARD_RATINGS
        .iter()
        .position(|r| *r == rating_val)
        .unwrap_or(0);
    rating_row.set_selected(initial_rating_idx as u32);
    group_details.add(&rating_row);

    meta_content.append(&group_details);

    // 2. Tags & Fandoms Group
    let group_tags = adw::PreferencesGroup::builder()
        .title("AO3 Tags & Fandoms")
        .description("Comma-separated fandoms, relationships, and freeform tags")
        .build();

    let fandoms_row = adw::EntryRow::builder()
        .title("Fandoms (comma-separated)")
        .text(&fandoms_val)
        .build();
    group_tags.add(&fandoms_row);

    let rels_row = adw::EntryRow::builder()
        .title("Relationships (e.g. A/B)")
        .text(&relationships_val)
        .build();
    group_tags.add(&rels_row);

    let chars_row = adw::EntryRow::builder()
        .title("Characters (comma-separated)")
        .text(&characters_val)
        .build();
    group_tags.add(&chars_row);

    let tags_row = adw::EntryRow::builder()
        .title("Additional Tags")
        .text(&tags_val)
        .build();
    group_tags.add(&tags_row);

    meta_content.append(&group_tags);

    // 3. Archive Warnings Group
    let group_warnings = adw::PreferencesGroup::builder()
        .title("Archive Warnings")
        .description("Select all applicable AO3 archive warnings")
        .build();

    let mut warning_switches = Vec::new();
    for warning in STANDARD_WARNINGS {
        let is_active = warnings_val.iter().any(|w| w == *warning);
        let srow = adw::SwitchRow::builder()
            .title(*warning)
            .active(is_active)
            .build();
        group_warnings.add(&srow);
        warning_switches.push((warning.to_string(), srow));
    }
    meta_content.append(&group_warnings);

    // 4. Categories Group
    let group_cats = adw::PreferencesGroup::builder()
        .title("Categories")
        .description("Relationship orientation categories")
        .build();

    let mut category_switches = Vec::new();
    for cat in STANDARD_CATEGORIES {
        let is_active = categories_val.iter().any(|c| c == *cat);
        let srow = adw::SwitchRow::builder()
            .title(*cat)
            .active(is_active)
            .build();
        group_cats.add(&srow);
        category_switches.push((cat.to_string(), srow));
    }
    meta_content.append(&group_cats);

    meta_clamp.set_child(Some(&meta_content));
    meta_page_box.append(&meta_clamp);
    meta_scroll.set_child(Some(&meta_page_box));

    view_stack.add_titled(&meta_scroll, Some("metadata"), "Metadata & Tags");
    let child_page = view_stack.page(&meta_scroll);
    child_page.set_icon_name(Some("document-properties-symbolic"));

    // ==========================================
    // Tab 2: Snapshots & Diff Viewer
    // ==========================================
    let history_box = gtk4::Box::new(gtk4::Orientation::Vertical, 8);
    history_box.set_margin_top(8);
    history_box.set_margin_bottom(12);
    history_box.set_margin_start(14);
    history_box.set_margin_end(14);

    let hist_top_bar = gtk4::Box::new(gtk4::Orientation::Horizontal, 8);
    let hist_desc = gtk4::Label::builder()
        .label("Snapshot timeline stored in history.cmt and history/:")
        .xalign(0.0)
        .hexpand(true)
        .css_classes(["dim-label"])
        .build();
    hist_top_bar.append(&hist_desc);

    let new_snap_btn = gtk4::Button::builder()
        .label("➕ New Snapshot…")
        .tooltip_text("Create a new snapshot revision of the current project state")
        .build();
    hist_top_bar.append(&new_snap_btn);
    history_box.append(&hist_top_bar);

    let paned = gtk4::Paned::new(gtk4::Orientation::Vertical);
    paned.set_position(190);
    paned.set_vexpand(true);

    // Top: Snapshots List
    let snap_scroll = gtk4::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk4::PolicyType::Never)
        .build();
    let snap_list = gtk4::ListBox::builder()
        .selection_mode(gtk4::SelectionMode::Single)
        .css_classes(["boxed-list"])
        .build();
    snap_scroll.set_child(Some(&snap_list));
    paned.set_start_child(Some(&snap_scroll));

    // Bottom: Diff Viewer (WebKit WebView for authentic diff styling)
    let diff_view = webkit6::WebView::new();
    diff_view.set_hexpand(true);
    diff_view.set_vexpand(true);
    paned.set_end_child(Some(&diff_view));

    history_box.append(&paned);

    view_stack.add_titled(&history_box, Some("history"), "Snapshots");
    let child_page = view_stack.page(&history_box);
    child_page.set_icon_name(Some("document-open-recent-symbolic"));

    root.append(&view_stack);
    window.set_content(Some(&root));

    // Snapshot data structure for selection
    #[derive(Clone)]
    struct SnapshotEntry {
        hash: String,
        author: String,
        date: String,
        message: String,
    }

    let parsed_snapshots = Rc::new(RefCell::new(Vec::<SnapshotEntry>::new()));

    // Helper to refresh snapshots
    let refresh_snapshots = {
        let snap_list = snap_list.clone();
        let parsed_snapshots = parsed_snapshots.clone();
        let diff_view = diff_view.clone();
        let handler = handler.clone();
        Rc::new(move || {
            while let Some(child) = snap_list.first_child() {
                snap_list.remove(&child);
            }
            parsed_snapshots.borrow_mut().clear();

            let ws = handler.workspace();
            let g = ws.lock();
            if let Some(ref proj) = g.project {
                let log = &proj.history_log;
                let blocks: Vec<&str> = log.split("\n\n").filter(|b| !b.trim().is_empty()).collect();

                if blocks.is_empty() {
                    let empty_row = adw::ActionRow::builder()
                        .title("No snapshots recorded yet")
                        .subtitle("Create a snapshot or save your work to record revision history")
                        .build();
                    snap_list.append(&empty_row);
                    let empty_html = compose_diff_html("No snapshots", "N/A", "N/A", "No snapshots recorded yet.", "");
                    diff_view.load_html(&empty_html, None);
                    return;
                }

                let mut entries = Vec::new();
                for b in blocks {
                    let mut hash = String::new();
                    let mut author = "Unknown".to_string();
                    let mut date = "Unknown".to_string();
                    let mut message = String::new();

                    for line in b.lines() {
                        let trimmed = line.trim();
                        if trimmed.starts_with("commit ") {
                            hash = trimmed[7..].trim().to_string();
                        } else if trimmed.starts_with("Author: ") {
                            author = trimmed[8..].trim().to_string();
                        } else if trimmed.starts_with("Date: ") {
                            let raw_date = trimmed[6..].trim();
                            date = DiffUtil::format_iso_date_with_local(raw_date);
                        } else if trimmed.starts_with("    ") || (!trimmed.is_empty() && !trimmed.contains(':')) {
                            if !message.is_empty() {
                                message.push(' ');
                            }
                            message.push_str(trimmed);
                        }
                    }

                    if !hash.is_empty() {
                        let short_hash = if hash.len() > 12 { &hash[..12] } else { &hash };
                        let row = adw::ActionRow::builder()
                            .use_markup(false)
                            .title(&format!("📸 {} — {}", short_hash, message))
                            .subtitle(&format!("By {} on {}", author, date))
                            .build();
                        snap_list.append(&row);

                        entries.push(SnapshotEntry {
                            hash,
                            author,
                            date,
                            message,
                        });
                    }
                }

                *parsed_snapshots.borrow_mut() = entries;

                if let Some(first_row) = snap_list.row_at_index(0) {
                    snap_list.select_row(Some(&first_row));
                }
            }
        })
    };

    refresh_snapshots();

    // Wire snapshot selection to diff viewer
    {
        let parsed_snapshots = parsed_snapshots.clone();
        let diff_view = diff_view.clone();
        let handler = handler.clone();
        snap_list.connect_row_selected(move |_, row| {
            if let Some(r) = row {
                let idx = r.index() as usize;
                let list = parsed_snapshots.borrow();
                if let Some(entry) = list.get(idx) {
                    let ws = handler.workspace();
                    let g = ws.lock();
                    let diff_text = if let Some(ref proj) = g.project {
                        let short = if entry.hash.len() > 12 { &entry.hash[..12] } else { &entry.hash };
                        proj.snapshot_diffs.get(&entry.hash)
                            .or_else(|| proj.snapshot_diffs.get(short))
                            .cloned()
                            .unwrap_or_default()
                    } else {
                        String::new()
                    };

                    let html = compose_diff_html(
                        &entry.hash,
                        &entry.author,
                        &entry.date,
                        &entry.message,
                        &diff_text,
                    );
                    diff_view.load_html(&html, None);
                }
            }
        });
    }

    // Wire New Snapshot button
    {
        let parent_win = window.clone();
        let handler = handler.clone();
        let ref_snap = refresh_snapshots.clone();
        new_snap_btn.connect_clicked(move |_| {
            show_new_snapshot_dialog(&parent_win, handler.clone(), {
                let r = ref_snap.clone();
                move || {
                    r();
                }
            });
        });
    }

    // Record initial values to detect unsaved changes
    let init_title = title_row.text().to_string();
    let init_author = author_row.text().to_string();
    let init_rating_idx = rating_row.selected();
    let init_fandoms = fandoms_row.text().to_string();
    let init_rels = rels_row.text().to_string();
    let init_chars = chars_row.text().to_string();
    let init_tags = tags_row.text().to_string();
    let init_warnings: Vec<bool> = warning_switches.iter().map(|(_, sw)| sw.is_active()).collect();
    let init_categories: Vec<bool> = category_switches.iter().map(|(_, sw)| sw.is_active()).collect();

    let check_unsaved = {
        let title_row = title_row.clone();
        let author_row = author_row.clone();
        let rating_row = rating_row.clone();
        let fandoms_row = fandoms_row.clone();
        let rels_row = rels_row.clone();
        let chars_row = chars_row.clone();
        let tags_row = tags_row.clone();
        let warning_switches = warning_switches.clone();
        let category_switches = category_switches.clone();
        let init_warnings = init_warnings.clone();
        let init_categories = init_categories.clone();
        move || -> bool {
            if title_row.text().as_str() != init_title { return true; }
            if author_row.text().as_str() != init_author { return true; }
            if rating_row.selected() != init_rating_idx { return true; }
            if fandoms_row.text().as_str() != init_fandoms { return true; }
            if rels_row.text().as_str() != init_rels { return true; }
            if chars_row.text().as_str() != init_chars { return true; }
            if tags_row.text().as_str() != init_tags { return true; }
            for (idx, (_, sw)) in warning_switches.iter().enumerate() {
                if sw.is_active() != init_warnings.get(idx).copied().unwrap_or(false) {
                    return true;
                }
            }
            for (idx, (_, sw)) in category_switches.iter().enumerate() {
                if sw.is_active() != init_categories.get(idx).copied().unwrap_or(false) {
                    return true;
                }
            }
            false
        }
    };

    let on_saved = Arc::new(on_saved);
    let do_save = {
        let handler = handler.clone();
        let warning_switches = warning_switches.clone();
        let category_switches = category_switches.clone();
        let rating_row = rating_row.clone();
        let title_row = title_row.clone();
        let author_row = author_row.clone();
        let fandoms_row = fandoms_row.clone();
        let rels_row = rels_row.clone();
        let chars_row = chars_row.clone();
        let tags_row = tags_row.clone();
        let on_saved = on_saved.clone();
        move || {
            let ws = handler.workspace();
            let mut g = ws.lock();
            if let Some(ref mut proj) = g.project {
                proj.title = title_row.text().to_string();
                proj.author = author_row.text().to_string();

                let rating_idx = rating_row.selected() as usize;
                if let Some(r) = STANDARD_RATINGS.get(rating_idx) {
                    proj.rating = r.to_string();
                }

                proj.fandoms = fandoms_row
                    .text()
                    .split(',')
                    .map(|s| s.trim().to_string())
                    .filter(|s| !s.is_empty())
                    .collect();

                proj.relationships = rels_row
                    .text()
                    .split(',')
                    .map(|s| s.trim().to_string())
                    .filter(|s| !s.is_empty())
                    .collect();

                proj.characters = chars_row
                    .text()
                    .split(',')
                    .map(|s| s.trim().to_string())
                    .filter(|s| !s.is_empty())
                    .collect();

                proj.tags = tags_row
                    .text()
                    .split(',')
                    .map(|s| s.trim().to_string())
                    .filter(|s| !s.is_empty())
                    .collect();

                proj.archive_warnings = warning_switches
                    .iter()
                    .filter(|(_, sw)| sw.is_active())
                    .map(|(w, _)| w.clone())
                    .collect();

                proj.categories = category_switches
                    .iter()
                    .filter(|(_, sw)| sw.is_active())
                    .map(|(c, _)| c.clone())
                    .collect();

                proj.dirty = true;
            }
            drop(g);
            on_saved();
        }
    };

    let force_close = Rc::new(RefCell::new(false));

    // Wire Save button
    {
        let win = window.clone();
        let do_save = do_save.clone();
        let force_close = force_close.clone();
        save_btn.connect_clicked(move |_| {
            do_save();
            *force_close.borrow_mut() = true;
            win.close();
        });
    }

    // Intercept window close ('X' button) to check for unsaved changes
    {
        let win = window.clone();
        let check_unsaved = check_unsaved.clone();
        let do_save = do_save.clone();
        let force_close = force_close.clone();
        window.connect_close_request(move |_| {
            if *force_close.borrow() {
                return gtk4::glib::Propagation::Proceed;
            }
            if !check_unsaved() {
                return gtk4::glib::Propagation::Proceed;
            }

            let dlg = adw::MessageDialog::builder()
                .transient_for(&win)
                .modal(true)
                .heading("Save Changes?")
                .body("You have unsaved changes to project properties. Do you want to save them before closing?")
                .build();
            dlg.add_response("cancel", "Cancel");
            dlg.add_response("discard", "Discard");
            dlg.add_response("save", "Save");
            dlg.set_response_appearance("save", adw::ResponseAppearance::Suggested);
            dlg.set_response_appearance("discard", adw::ResponseAppearance::Destructive);
            dlg.set_default_response(Some("save"));
            dlg.set_close_response("cancel");

            let win_clone = win.clone();
            let do_save = do_save.clone();
            let force_close = force_close.clone();
            dlg.connect_response(None, move |_, response| {
                if response == "save" {
                    do_save();
                    *force_close.borrow_mut() = true;
                    win_clone.close();
                } else if response == "discard" {
                    *force_close.borrow_mut() = true;
                    win_clone.close();
                }
            });
            dlg.present();

            gtk4::glib::Propagation::Stop
        });
    }

    window.present();
}

fn show_new_snapshot_dialog<F>(
    parent: &impl IsA<gtk4::Window>,
    handler: Arc<McpHandler>,
    on_created: F,
) where
    F: Fn() + 'static,
{
    let dialog = adw::Window::builder()
        .title("Create Project Snapshot")
        .transient_for(parent)
        .modal(true)
        .default_width(450)
        .default_height(220)
        .build();

    let root = gtk4::Box::new(gtk4::Orientation::Vertical, 12);
    let header = adw::HeaderBar::new();
    header.set_title_widget(Some(&adw::WindowTitle::new("New Snapshot", "Save project snapshot point")));
    root.append(&header);

    let content = gtk4::Box::new(gtk4::Orientation::Vertical, 12);
    content.set_margin_start(18);
    content.set_margin_end(18);
    content.set_margin_top(8);
    content.set_margin_bottom(18);

    let desc_label = gtk4::Label::builder()
        .label("Enter a description or commit message for this snapshot:")
        .xalign(0.0)
        .wrap(true)
        .build();
    content.append(&desc_label);

    let entry = gtk4::Entry::builder()
        .placeholder_text("Snapshot message (e.g. Completed Chapter 2 edit)")
        .hexpand(true)
        .build();
    content.append(&entry);

    let btn_box = gtk4::Box::new(gtk4::Orientation::Horizontal, 8);
    btn_box.set_halign(gtk4::Align::End);

    let cancel_btn = gtk4::Button::with_label("Cancel");
    let create_btn = gtk4::Button::builder()
        .label("Create Snapshot")
        .css_classes(["suggested-action"])
        .build();

    {
        let d = dialog.clone();
        cancel_btn.connect_clicked(move |_| {
            d.close();
        });
    }

    {
        let d = dialog.clone();
        let entry_clone = entry.clone();
        create_btn.connect_clicked(move |_| {
            let msg = entry_clone.text().trim().to_string();
            if !msg.is_empty() {
                let ws = handler.workspace();
                let mut g = ws.lock();
                if let Some(ref mut proj) = g.project {
                    let author = proj.author.clone();
                    proj.append_history_commit(&author, &msg);
                    proj.dirty = true;
                }
                drop(g);
                on_created();
                d.close();
            }
        });
    }

    btn_box.append(&cancel_btn);
    btn_box.append(&create_btn);
    content.append(&btn_box);

    root.append(&content);
    dialog.set_content(Some(&root));
    dialog.present();
}

fn compose_diff_html(
    hash: &str,
    author: &str,
    date: &str,
    message: &str,
    diff_text: &str,
) -> String {
    let mut html = String::new();
    html.push_str(r#"<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  :root {
    color-scheme: light dark;
  }
  body {
    font-family: 'JetBrains Mono', 'Fira Code', 'DejaVu Sans Mono', monospace;
    font-size: 11px;
    line-height: 1.48;
    margin: 0;
    padding: 12px 16px;
    background-color: transparent;
    color: CanvasText;
  }
  .header-box {
    padding: 10px 14px;
    background: rgba(128, 128, 128, 0.08);
    border-radius: 8px;
    margin-bottom: 12px;
  }
  .hash { color: #a855f7; font-weight: bold; }
  .author { color: #38bdf8; }
  .date { color: #fbbf24; }
  .msg-banner {
    margin: 8px 0 0 0;
    padding: 6px 10px;
    background: rgba(56, 189, 248, 0.12);
    border-left: 3px solid #38bdf8;
    border-radius: 0 4px 4px 0;
    font-weight: 600;
  }
  .diff-container {
    border-radius: 6px;
    overflow: hidden;
  }
  .diff-add {
    color: #4ade80;
    background: rgba(74, 222, 128, 0.14);
    padding: 1px 4px;
    white-space: pre-wrap;
  }
  .diff-del {
    color: #f87171;
    background: rgba(248, 113, 113, 0.14);
    padding: 1px 4px;
    white-space: pre-wrap;
  }
  .diff-hunk {
    color: #38bdf8;
    font-weight: bold;
    background: rgba(56, 189, 248, 0.1);
    margin-top: 4px;
    padding: 2px 4px;
    white-space: pre-wrap;
  }
  .diff-file {
    color: #93c5fd;
    font-weight: bold;
    padding: 1px 4px;
    white-space: pre-wrap;
  }
  .diff-ctx {
    color: rgba(160, 160, 160, 0.85);
    padding: 1px 4px;
    white-space: pre-wrap;
  }
  .warning-box {
    margin-top: 8px;
    padding: 12px 14px;
    background: rgba(239, 68, 68, 0.08);
    border: 1px dashed rgba(239, 68, 68, 0.4);
    border-radius: 6px;
    color: #fca5a5;
    font-family: system-ui, sans-serif;
  }
</style>
</head>
<body>
"#);

    let short_hash = if hash.len() > 12 { &hash[..12] } else { hash };
    html.push_str(&format!(
        r#"<div class="header-box">
  <div><b>Snapshot:</b> <span class="hash">{}</span></div>
  <div><b>Author:</b>   <span class="author">{}</span></div>
  <div><b>Date:</b>     <span class="date">{}</span></div>
  <div class="msg-banner">{}</div>
</div>
"#,
        escape_html(short_hash),
        escape_html(author),
        escape_html(date),
        escape_html(message),
    ));

    if !diff_text.trim().is_empty() {
        html.push_str(r#"<div style="font-weight: bold; margin-bottom: 6px; opacity: 0.8;">📄 Line-by-Line Changes (Diff):</div><div class="diff-container">"#);
        for line in diff_text.lines() {
            if line.starts_with('+') && !line.starts_with("+++") {
                html.push_str(&format!(r#"<div class="diff-add">{}</div>"#, escape_html(line)));
            } else if line.starts_with('-') && !line.starts_with("---") {
                html.push_str(&format!(r#"<div class="diff-del">{}</div>"#, escape_html(line)));
            } else if line.starts_with("@@") {
                html.push_str(&format!(r#"<div class="diff-hunk">{}</div>"#, escape_html(line)));
            } else if line.starts_with("---") || line.starts_with("+++") {
                html.push_str(&format!(r#"<div class="diff-file">{}</div>"#, escape_html(line)));
            } else if line.starts_with('#') {
                continue;
            } else {
                html.push_str(&format!(r#"<div class="diff-ctx">{}</div>"#, escape_html(line)));
            }
        }
        html.push_str("</div>");
    } else {
        html.push_str(r#"<div class="warning-box">
⚠️ <strong>No diff snapshot available for this commit.</strong><br/>
The diff file may have been created before snapshots were enabled in this project.
</div>"#);
    }

    html.push_str("</body></html>");
    html
}

fn escape_html(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}
