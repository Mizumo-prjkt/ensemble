use ensemble_html::linter::CodeDiagnosticIssue;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::rc::Rc;

pub fn show_problems_dialog<F>(
    parent: &impl IsA<gtk4::Window>,
    diagnostics: Vec<CodeDiagnosticIssue>,
    on_jump: F,
) where
    F: Fn(usize) + 'static,
{
    let on_jump = Rc::new(on_jump);
    let window = adw::Window::builder()
        .title("HTML Validation & Problems")
        .transient_for(parent)
        .modal(true)
        .default_width(700)
        .default_height(450)
        .build();

    let root = gtk4::Box::new(gtk4::Orientation::Vertical, 0);
    let header = adw::HeaderBar::new();
    let title = adw::WindowTitle::new(
        "Problems & Diagnostics",
        &format!("{} issues detected", diagnostics.len()),
    );
    header.set_title_widget(Some(&title));
    root.append(&header);

    if diagnostics.is_empty() {
        let status_page = adw::StatusPage::builder()
            .icon_name("object-select-symbolic")
            .title("No Problems Found")
            .description("All HTML tags and attributes comply with AO3 whitelist rules.")
            .vexpand(true)
            .build();
        root.append(&status_page);
    } else {
        let scrolled = gtk4::ScrolledWindow::builder()
            .vexpand(true)
            .hscrollbar_policy(gtk4::PolicyType::Never)
            .build();

        let list_box = gtk4::ListBox::builder()
            .selection_mode(gtk4::SelectionMode::None)
            .margin_top(12)
            .margin_bottom(12)
            .margin_start(16)
            .margin_end(16)
            .css_classes(["boxed-list"])
            .build();

        for diag in diagnostics {
            let row = adw::ActionRow::builder()
                .use_markup(false)
                .title(&diag.message)
                .subtitle(&format!("Line {}", diag.start_pos))
                .build();

            let icon_name = if diag.is_error {
                "dialog-error-symbolic"
            } else {
                "dialog-warning-symbolic"
            };
            let icon = gtk4::Image::from_icon_name(icon_name);
            row.add_prefix(&icon);

            let jump_btn = gtk4::Button::with_label("Jump");
            jump_btn.set_valign(gtk4::Align::Center);
            let line = diag.start_pos;
            let on_jump_clone = on_jump.clone();
            let win_weak = window.downgrade();
            jump_btn.connect_clicked(move |_| {
                on_jump_clone(line);
                if let Some(w) = win_weak.upgrade() {
                    w.close();
                }
            });
            row.add_suffix(&jump_btn);

            list_box.append(&row);
        }

        scrolled.set_child(Some(&list_box));
        root.append(&scrolled);
    }

    window.set_content(Some(&root));
    window.present();
}
