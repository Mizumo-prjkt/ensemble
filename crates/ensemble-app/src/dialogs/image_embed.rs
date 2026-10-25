use gtk4 as gtk;
use gtk4::gdk;
use gtk4::glib;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::cell::Cell;
use std::rc::Rc;
use std::time::Duration;
use webkit6::prelude::*;

fn escape_html_attr(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    for c in text.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            _ => out.push(c),
        }
    }
    out
}

pub fn generate_ao3_image_html(
    url: &str,
    alt: &str,
    title: &str,
    alignment: &str,
    width: i32,
    height: i32,
    wrap_in_link: bool,
) -> String {
    let url = url.trim();
    if url.is_empty() {
        return String::new();
    }

    let mut img = format!("<img src=\"{}\"", escape_html_attr(url));
    if !alt.trim().is_empty() {
        img.push_str(&format!(" alt=\"{}\"", escape_html_attr(alt.trim())));
    }
    if !title.trim().is_empty() {
        img.push_str(&format!(" title=\"{}\"", escape_html_attr(title.trim())));
    }
    if width > 0 {
        img.push_str(&format!(" width=\"{}\"", width));
    }
    if height > 0 {
        img.push_str(&format!(" height=\"{}\"", height));
    }
    img.push_str(" />");

    if wrap_in_link {
        img = format!("<a href=\"{}\">{}</a>", escape_html_attr(url), img);
    }

    match alignment {
        "center" => format!("<p align=\"center\">{}</p>", img),
        "left" => format!("<p align=\"left\">{}</p>", img),
        "right" => format!("<p align=\"right\">{}</p>", img),
        _ => img,
    }
}

pub fn show_image_embed_dialog<F>(parent: &adw::ApplicationWindow, on_insert: F)
where
    F: Fn(String) + 'static,
{
    let win = adw::Window::builder()
        .title("Insert Image Embed (AO3 URL Attachment)")
        .transient_for(parent)
        .modal(true)
        .default_width(660)
        .default_height(700)
        .build();

    let root = gtk::Box::new(gtk::Orientation::Vertical, 0);
    let header = adw::HeaderBar::new();
    root.append(&header);

    let scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .build();

    let content = gtk::Box::new(gtk::Orientation::Vertical, 12);
    content.set_margin_top(14);
    content.set_margin_bottom(14);
    content.set_margin_start(16);
    content.set_margin_end(16);

    // 1. Info Banner explaining AO3's hosted image URL requirement
    let banner_frame = gtk::Frame::new(None);
    banner_frame.add_css_class("card");
    let banner_box = gtk::Box::new(gtk::Orientation::Horizontal, 10);
    banner_box.set_margin_top(10);
    banner_box.set_margin_bottom(10);
    banner_box.set_margin_start(12);
    banner_box.set_margin_end(12);

    let info_icon = gtk::Image::from_icon_name("dialog-information-symbolic");
    info_icon.set_valign(gtk::Align::Start);
    banner_box.append(&info_icon);

    let banner_lbl = gtk::Label::builder()
        .use_markup(true)
        .label(
            "<b>AO3 Image Requirement:</b> Archive of Our Own does not host image files directly. \
             Images must be hosted online (e.g., Squidge Images, Postimages, Imgur, personal host) and embedded via URL. \
             The generated HTML code is 100% AO3-compliant and ready to insert into Rich Text or Raw HTML.",
        )
        .wrap(true)
        .xalign(0.0)
        .hexpand(true)
        .css_classes(["caption"])
        .build();
    banner_box.append(&banner_lbl);
    banner_frame.set_child(Some(&banner_box));
    content.append(&banner_frame);

    // 2. Group 1: Image Source & Details
    let source_group = adw::PreferencesGroup::builder()
        .title("Image Source & Accessibility")
        .build();

    let url_row = adw::ActionRow::builder()
        .title("Image URL")
        .subtitle("Public https:// or http:// image link")
        .build();
    let url_entry = gtk::Entry::builder()
        .placeholder_text("https://example.com/illustration.png")
        .hexpand(true)
        .valign(gtk::Align::Center)
        .width_chars(30)
        .build();
    let fetch_btn = gtk::Button::builder()
        .label("Test / Preview")
        .tooltip_text("Load remote image into the preview box below")
        .valign(gtk::Align::Center)
        .css_classes(["flat"])
        .build();
    url_row.add_suffix(&url_entry);
    url_row.add_suffix(&fetch_btn);
    source_group.add(&url_row);

    let alt_row = adw::ActionRow::builder()
        .title("Alt Text")
        .subtitle("Screen reader description for AO3 accessibility")
        .build();
    let alt_entry = gtk::Entry::builder()
        .placeholder_text("Brief description of the artwork or scene")
        .hexpand(true)
        .valign(gtk::Align::Center)
        .width_chars(34)
        .build();
    alt_row.add_suffix(&alt_entry);
    source_group.add(&alt_row);

    let title_row = adw::ActionRow::builder()
        .title("Hover Title")
        .subtitle("Optional tooltip text shown on mouse hover")
        .build();
    let title_entry = gtk::Entry::builder()
        .placeholder_text("Optional hover tooltip text")
        .hexpand(true)
        .valign(gtk::Align::Center)
        .width_chars(34)
        .build();
    title_row.add_suffix(&title_entry);
    source_group.add(&title_row);

    content.append(&source_group);

    // 3. Group 2: Layout & Dimensions
    let layout_group = adw::PreferencesGroup::builder()
        .title("Layout & Dimensions")
        .build();

    let align_row = adw::ActionRow::builder()
        .title("Alignment")
        .subtitle("Paragraph alignment wrapper on AO3")
        .build();
    let align_model = gtk::StringList::new(&[
        "Centered (Recommended on AO3)",
        "Inline (Flowing with text)",
        "Left-aligned",
        "Right-aligned",
    ]);
    let align_dropdown = gtk::DropDown::new(Some(align_model), gtk::Expression::NONE);
    align_dropdown.set_selected(0);
    align_dropdown.set_valign(gtk::Align::Center);
    align_row.add_suffix(&align_dropdown);
    layout_group.add(&align_row);

    let dim_row = adw::ActionRow::builder()
        .title("Dimensions (px)")
        .subtitle("Leave at 0 for automatic / responsive sizing")
        .build();
    let dim_box = gtk::Box::new(gtk::Orientation::Horizontal, 6);
    dim_box.set_valign(gtk::Align::Center);

    let w_lbl = gtk::Label::new(Some("W:"));
    w_lbl.add_css_class("dim-label");
    let width_spin = gtk::SpinButton::with_range(0.0, 9999.0, 10.0);
    width_spin.set_value(0.0);
    width_spin.set_tooltip_text(Some("Width in pixels (0 = Auto)"));

    let h_lbl = gtk::Label::new(Some("H:"));
    h_lbl.add_css_class("dim-label");
    let height_spin = gtk::SpinButton::with_range(0.0, 9999.0, 10.0);
    height_spin.set_value(0.0);
    height_spin.set_tooltip_text(Some("Height in pixels (0 = Auto)"));

    let lock_aspect_check = gtk::CheckButton::builder()
        .label("Lock Ratio")
        .active(true)
        .tooltip_text("Maintain aspect ratio when adjusting width or height")
        .build();

    dim_box.append(&w_lbl);
    dim_box.append(&width_spin);
    dim_box.append(&h_lbl);
    dim_box.append(&height_spin);
    dim_box.append(&lock_aspect_check);
    dim_row.add_suffix(&dim_box);
    layout_group.add(&dim_row);

    let link_row = adw::ActionRow::builder()
        .title("Clickable Full-Size Link")
        .subtitle("Wrap image in <a href=\"...\"> so readers can open full resolution")
        .build();
    let wrap_link_check = gtk::Switch::builder()
        .active(false)
        .valign(gtk::Align::Center)
        .build();
    link_row.add_suffix(&wrap_link_check);
    link_row.set_activatable_widget(Some(&wrap_link_check));
    layout_group.add(&link_row);

    content.append(&layout_group);

    // 4. Live Image Preview & AO3 HTML Code Snippet
    let preview_group = adw::PreferencesGroup::builder()
        .title("Live Image Preview & AO3 HTML Snippet")
        .build();

    let status_lbl = gtk::Label::builder()
        .label("Enter an https:// or http:// image URL above to preview and generate AO3 HTML.")
        .xalign(0.0)
        .css_classes(["dim-label", "caption"])
        .margin_bottom(4)
        .build();
    preview_group.add(&status_lbl);

    let preview_frame = gtk::Frame::new(None);
    preview_frame.add_css_class("view");
    let preview_webview = webkit6::WebView::builder()
        .vexpand(false)
        .hexpand(true)
        .build();
    preview_webview.set_size_request(-1, 160);
    preview_webview.load_html(
        "<!DOCTYPE html><html><body style='margin:0;display:flex;align-items:center;justify-content:center;height:100vh;font-family:sans-serif;color:#888;background:#1e1e1e;'><div style='text-align:center;'>🖼️ Image preview will appear here</div></body></html>",
        None,
    );
    preview_frame.set_child(Some(&preview_webview));
    preview_group.add(&preview_frame);

    let snippet_box = gtk::Box::new(gtk::Orientation::Horizontal, 8);
    snippet_box.set_margin_top(8);

    let snippet_entry = gtk::Entry::builder()
        .editable(false)
        .hexpand(true)
        .text("<img src=\"...\" alt=\"...\" />")
        .css_classes(["monospace"])
        .build();

    let copy_snippet_btn = gtk::Button::builder()
        .label("📋 Copy AO3 HTML")
        .tooltip_text("Copy generated AO3 <img> HTML snippet to clipboard")
        .build();

    snippet_box.append(&snippet_entry);
    snippet_box.append(&copy_snippet_btn);
    preview_group.add(&snippet_box);

    content.append(&preview_group);

    // 5. Bottom Action Bar
    let action_bar = gtk::Box::new(gtk::Orientation::Horizontal, 8);
    action_bar.set_halign(gtk::Align::End);
    action_bar.set_margin_top(6);

    let cancel_btn = gtk::Button::builder()
        .label("Cancel")
        .build();
    let insert_btn = gtk::Button::builder()
        .label("🖼️ Insert Image")
        .css_classes(["suggested-action"])
        .build();

    action_bar.append(&cancel_btn);
    action_bar.append(&insert_btn);
    content.append(&action_bar);

    scroll.set_child(Some(&content));
    root.append(&scroll);
    win.set_content(Some(&root));

    let align_code_for = |idx: u32| -> &'static str {
        match idx {
            0 => "center",
            1 => "inline",
            2 => "left",
            3 => "right",
            _ => "center",
        }
    };

    let build_current_snippet: Rc<dyn Fn() -> String> = {
        let u = url_entry.clone();
        let a = alt_entry.clone();
        let t = title_entry.clone();
        let al = align_dropdown.clone();
        let w = width_spin.clone();
        let h = height_spin.clone();
        let wl = wrap_link_check.clone();
        Rc::new(move || {
            let align_code = align_code_for(al.selected());
            generate_ao3_image_html(
                &u.text(),
                &a.text(),
                &t.text(),
                align_code,
                w.value_as_int(),
                h.value_as_int(),
                wl.is_active(),
            )
        })
    };

    let update_snippet_ui: Rc<dyn Fn()> = {
        let build_fn = build_current_snippet.clone();
        let snip_e = snippet_entry.clone();
        let stat_l = status_lbl.clone();
        let u = url_entry.clone();
        Rc::new(move || {
            let s = build_fn();
            if s.is_empty() {
                snip_e.set_text("<img src=\"...\" alt=\"...\" />");
                stat_l.set_text(
                    "Enter an https:// or http:// image URL above to preview and generate AO3 HTML.",
                );
            } else {
                snip_e.set_text(&s);
                let raw_url = u.text().trim().to_string();
                if !raw_url.starts_with("https://") && !raw_url.starts_with("http://") {
                    stat_l.set_text(
                        "⚠️ Warning: AO3 requires public https:// or http:// image URLs.",
                    );
                } else {
                    stat_l.set_text("✓ Ready to insert AO3-compliant image HTML.");
                }
            }
        })
    };

    let update_live_preview: Rc<dyn Fn()> = {
        let build_fn = build_current_snippet.clone();
        let p_wv = preview_webview.clone();
        let u = url_entry.clone();
        Rc::new(move || {
            let raw_url = u.text().trim().to_string();
            if raw_url.is_empty() {
                p_wv.load_html(
                    "<!DOCTYPE html><html><body style='margin:0;display:flex;align-items:center;justify-content:center;height:100vh;font-family:sans-serif;color:#888;background:#1e1e1e;'><div style='text-align:center;'>🖼️ Image preview will appear here</div></body></html>",
                    None,
                );
                return;
            }
            let snippet = build_fn();
            let html_doc = format!(
                "<!DOCTYPE html><html><head><meta charset='utf-8'><style>body {{ margin: 8px; font-family: sans-serif; background: #1e1e1e; color: #e0e0e0; }} img {{ max-width: 100%; max-height: 135px; object-fit: contain; border-radius: 4px; }}</style></head><body>{}</body></html>",
                snippet
            );
            p_wv.load_html(&html_doc, None);
        })
    };

    // Wire live updates
    {
        let up = update_snippet_ui.clone();
        url_entry.connect_changed(move |_| up());
    }
    {
        let up = update_snippet_ui.clone();
        let up_prev = update_live_preview.clone();
        url_entry.connect_activate(move |_| {
            up();
            up_prev();
        });
    }
    {
        let up = update_snippet_ui.clone();
        let up_prev = update_live_preview.clone();
        fetch_btn.connect_clicked(move |_| {
            up();
            up_prev();
        });
    }
    {
        let up = update_snippet_ui.clone();
        alt_entry.connect_changed(move |_| up());
    }
    {
        let up = update_snippet_ui.clone();
        title_entry.connect_changed(move |_| up());
    }
    {
        let up = update_snippet_ui.clone();
        align_dropdown.connect_selected_notify(move |_| up());
    }
    {
        let up = update_snippet_ui.clone();
        wrap_link_check.connect_active_notify(move |_| up());
    }

    let syncing_dims = Rc::new(Cell::new(false));
    {
        let up = update_snippet_ui.clone();
        let lock = lock_aspect_check.clone();
        let h_spin = height_spin.clone();
        let sync = syncing_dims.clone();
        width_spin.connect_value_changed(move |w_spin| {
            if sync.get() {
                return;
            }
            if lock.is_active() {
                sync.set(true);
                let w = w_spin.value_as_int();
                if w == 0 {
                    h_spin.set_value(0.0);
                }
                sync.set(false);
            }
            up();
        });
    }
    {
        let up = update_snippet_ui.clone();
        let lock = lock_aspect_check.clone();
        let w_spin = width_spin.clone();
        let sync = syncing_dims.clone();
        height_spin.connect_value_changed(move |h_spin| {
            if sync.get() {
                return;
            }
            if lock.is_active() {
                sync.set(true);
                let h = h_spin.value_as_int();
                if h == 0 {
                    w_spin.set_value(0.0);
                }
                sync.set(false);
            }
            up();
        });
    }

    // Copy snippet button
    {
        let build_fn = build_current_snippet.clone();
        let c_btn = copy_snippet_btn.clone();
        copy_snippet_btn.connect_clicked(move |_| {
            let s = build_fn();
            if !s.is_empty() {
                if let Some(display) = gdk::Display::default() {
                    display.clipboard().set_text(&s);
                }
                c_btn.set_label("✓ Copied!");
                let c_btn_reset = c_btn.clone();
                glib::timeout_add_local_once(Duration::from_millis(1800), move || {
                    c_btn_reset.set_label("📋 Copy AO3 HTML");
                });
            }
        });
    }

    // Cancel & Insert buttons
    {
        let w = win.clone();
        cancel_btn.connect_clicked(move |_| {
            w.close();
        });
    }
    {
        let w = win.clone();
        let build_fn = build_current_snippet.clone();
        let u = url_entry.clone();
        let stat_l = status_lbl.clone();
        let on_insert = Rc::new(on_insert);
        insert_btn.connect_clicked(move |_| {
            let raw_url = u.text().trim().to_string();
            if raw_url.is_empty() {
                stat_l.set_text("⚠️ Please enter an image URL before inserting.");
                u.grab_focus();
                return;
            }
            let snippet = build_fn();
            if !snippet.is_empty() {
                on_insert(snippet);
                w.close();
            }
        });
    }

    win.present();
}
