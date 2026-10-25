use ensemble_mcp::McpHandler;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::sync::Arc;

pub fn show_chapter_manager_dialog<F>(
    parent: &impl IsA<gtk4::Window>,
    handler: Arc<McpHandler>,
    on_changed: F,
) where
    F: Fn() + 'static,
{
    let on_changed = Arc::new(on_changed);
    let window = adw::Window::builder()
        .title("Chapter Manager")
        .transient_for(parent)
        .modal(true)
        .default_width(600)
        .default_height(500)
        .build();

    let root = gtk4::Box::new(gtk4::Orientation::Vertical, 0);
    let header = adw::HeaderBar::new();
    let title = adw::WindowTitle::new("Chapter Manager", "Reorder, rename, or add chapters");
    header.set_title_widget(Some(&title));
    root.append(&header);

    let content_box = gtk4::Box::new(gtk4::Orientation::Vertical, 12);
    content_box.set_margin_top(12);
    content_box.set_margin_bottom(12);
    content_box.set_margin_start(16);
    content_box.set_margin_end(16);

    let scrolled = gtk4::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk4::PolicyType::Never)
        .build();

    let list_box = gtk4::ListBox::builder()
        .selection_mode(gtk4::SelectionMode::Single)
        .css_classes(["boxed-list"])
        .build();

    scrolled.set_child(Some(&list_box));
    content_box.append(&scrolled);

    // Button toolbar
    let toolbar = gtk4::Box::new(gtk4::Orientation::Horizontal, 8);
    toolbar.set_halign(gtk4::Align::Center);

    let btn_up = gtk4::Button::with_label("▲ Move Up");
    let btn_down = gtk4::Button::with_label("▼ Move Down");
    let btn_add = gtk4::Button::with_label("+ Add Chapter");
    let btn_del = gtk4::Button::with_label("🗑 Delete");
    btn_del.add_css_class("destructive-action");

    toolbar.append(&btn_up);
    toolbar.append(&btn_down);
    toolbar.append(&btn_add);
    toolbar.append(&btn_del);
    content_box.append(&toolbar);

    root.append(&content_box);
    window.set_content(Some(&root));

    // Helper to populate list
    let populate_list = {
        let list_box = list_box.clone();
        let handler = handler.clone();
        Arc::new(move || {
            while let Some(child) = list_box.first_child() {
                list_box.remove(&child);
            }
            let ws = handler.workspace();
            let g = ws.lock();
            if let Some(ref proj) = g.project {
                if proj.chapters.is_empty() {
                    let empty_row = adw::ActionRow::builder()
                        .use_markup(false)
                        .title("No chapters yet")
                        .subtitle("Click '+ Add Chapter' below to get started")
                        .build();
                    list_box.append(&empty_row);
                } else {
                    for (idx, ch) in proj.chapters.iter().enumerate() {
                        let row = adw::ActionRow::builder()
                            .use_markup(false)
                            .title(&format!("{}. {}", idx + 1, ch.title))
                            .subtitle(&format!("{} words", ch.word_count()))
                            .build();
                        list_box.append(&row);
                    }
                }
            }
        })
    };

    populate_list();

    // Wire Up button
    {
        let list_box = list_box.clone();
        let handler = handler.clone();
        let pop = populate_list.clone();
        let cb = on_changed.clone();
        btn_up.connect_clicked(move |_| {
            if let Some(row) = list_box.selected_row() {
                let idx = row.index();
                if idx > 0 {
                    let ws = handler.workspace();
                    let mut g = ws.lock();
                    if let Some(ref mut proj) = g.project {
                        proj.chapters.swap(idx as usize, (idx - 1) as usize);
                        proj.dirty = true;
                    }
                    drop(g);
                    pop();
                    list_box.select_row(list_box.row_at_index(idx - 1).as_ref());
                    cb();
                }
            }
        });
    }

    // Wire Down button
    {
        let list_box = list_box.clone();
        let handler = handler.clone();
        let pop = populate_list.clone();
        let cb = on_changed.clone();
        btn_down.connect_clicked(move |_| {
            if let Some(row) = list_box.selected_row() {
                let idx = row.index() as usize;
                let ws = handler.workspace();
                let mut g = ws.lock();
                if let Some(ref mut proj) = g.project {
                    if idx + 1 < proj.chapters.len() {
                        proj.chapters.swap(idx, idx + 1);
                        proj.dirty = true;
                        drop(g);
                        pop();
                        list_box.select_row(list_box.row_at_index((idx + 1) as i32).as_ref());
                        cb();
                    }
                }
            }
        });
    }

    // Wire Add button
    {
        let handler = handler.clone();
        let pop = populate_list.clone();
        let cb = on_changed.clone();
        let list_box = list_box.clone();
        btn_add.connect_clicked(move |_| {
            let ws = handler.workspace();
            let mut g = ws.lock();
            if let Some(ref mut proj) = g.project {
                let next_idx = proj.chapters.len() + 1;
                proj.add_chapter(&format!("Chapter {}", next_idx));
                if let Some(ch) = proj.chapters.last_mut() {
                    ch.html = "<p></p>".to_string();
                }
                proj.dirty = true;
                drop(g);
                pop();
                list_box.select_row(list_box.row_at_index((next_idx - 1) as i32).as_ref());
                cb();
            }
        });
    }

    // Wire Delete button
    {
        let handler = handler.clone();
        let pop = populate_list.clone();
        let cb = on_changed.clone();
        let list_box = list_box.clone();
        btn_del.connect_clicked(move |_| {
            if let Some(row) = list_box.selected_row() {
                let idx = row.index() as usize;
                let ws = handler.workspace();
                let mut g = ws.lock();
                let active_idx = g.active_chapter_index;
                if let Some(ref mut proj) = g.project {
                    if idx < proj.chapters.len() {
                        proj.chapters.remove(idx);
                        proj.dirty = true;
                        let new_len = proj.chapters.len();
                        if active_idx >= new_len {
                            g.active_chapter_index = new_len.saturating_sub(1);
                        }
                        drop(g);
                        pop();
                        if new_len > 0 {
                            let new_sel = idx.min(new_len - 1);
                            list_box.select_row(list_box.row_at_index(new_sel as i32).as_ref());
                        }
                        cb();
                    }
                }
            }
        });
    }

    window.present();
}
