use gtk4 as gtk;
use gtk4::gdk;
use gtk4::gdk_pixbuf::Pixbuf;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::path::PathBuf;

fn resolve_icon_path() -> Option<PathBuf> {
    let candidates = [
        PathBuf::from("resources/icon.png"),
        PathBuf::from("../resources/icon.png"),
        PathBuf::from("../../resources/icon.png"),
        PathBuf::from("/usr/share/icons/hicolor/256x256/apps/ensemble.png"),
    ];
    for c in candidates {
        if c.exists() {
            return Some(c);
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            for rel in ["resources/icon.png", "../resources/icon.png", "../../resources/icon.png"] {
                let p = dir.join(rel);
                if p.exists() {
                    return Some(p);
                }
            }
        }
    }
    None
}

struct CreditEntry {
    name: &'static str,
    version: &'static str,
    author: &'static str,
    license_short: &'static str,
    url: &'static str,
    description: &'static str,
    license_text: &'static str,
}

const MIT_LICENSE_TEXT: &str = "\
MIT License

Copyright (c) 2026 Mizumo-prjkt & Ensemble Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the \"Software\"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.";

const CREDITS: &[CreditEntry] = &[
    CreditEntry {
        name: "Ensemble (AO3 Typewriter)",
        version: "1.3.0",
        author: "Mizumo-prjkt",
        license_short: "MIT License",
        url: "https://github.com/Mizumo-prjkt/ensemble",
        description: "Next-Gen Fanfiction Editor & Work Skin Composer for Archive of Our Own (AO3) authors.",
        license_text: MIT_LICENSE_TEXT,
    },
    CreditEntry {
        name: "GTK 4 & Libadwaita",
        version: "4.x / 1.x",
        author: "The GNOME Project",
        license_short: "LGPL-2.1-or-later",
        url: "https://www.gtk.org",
        description: "Multi-platform toolkit and modern adaptive building blocks used for Ensemble's native Linux desktop interface.",
        license_text: "GNU Lesser General Public License v2.1 or later (LGPL-2.1-or-later)\n\n\
This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.\n\n\
This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.",
    },
    CreditEntry {
        name: "WebKitGTK",
        version: "6.0",
        author: "The WebKit Open Source Project",
        license_short: "LGPL-2.1+ & BSD-2-Clause",
        url: "https://webkitgtk.org",
        description: "Web rendering engine powering the WYSIWYG Rich Text Composer and Live AO3 Work Skin Preview.",
        license_text: "WebKitGTK is licensed under the GNU Lesser General Public License (LGPL) v2.1+ and the 2-Clause BSD License.\n\n\
Copyright (C) The WebKit Open Source Project. All rights reserved.",
    },
    CreditEntry {
        name: "Zstandard (zstd)",
        version: "1.5+",
        author: "Meta Platforms, Inc.",
        license_short: "BSD-3-Clause",
        url: "https://facebook.github.io/zstd/",
        description: "Fast real-time lossless compression algorithm used for .ao3proj archive containers.",
        license_text: "BSD License\n\n\
For Zstandard software\n\n\
Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.\n\n\
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:\n\
1. Redistributions of source code must retain the above copyright notice, this list of conditions, and the following disclaimer.\n\
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions, and the following disclaimer in the documentation and/or other materials provided with the distribution.\n\
3. Neither the name Meta nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.",
    },
    CreditEntry {
        name: "Rust, Tokio & Serde",
        version: "1.x",
        author: "The Rust Project, Tokio & Serde Contributors",
        license_short: "MIT OR Apache-2.0",
        url: "https://www.rust-lang.org",
        description: "Core language runtime, asynchronous I/O engine for the MCP server, and project serialization.",
        license_text: "Licensed under either of:\n\
- Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)\n\
- MIT license (http://opensource.org/licenses/MIT)\n\
at your option.",
    },
    CreditEntry {
        name: "Dracula Color Palette",
        version: "1.0",
        author: "Zeno Rocha",
        license_short: "MIT License",
        url: "https://draculatheme.com",
        description: "High-contrast dark syntax color inspiration for HTML & CSS code editing.",
        license_text: "MIT License\n\n\
Copyright (c) 2013-present Zeno Rocha <hi@zenorocha.com>\n\n\
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software.",
    },
];

pub fn show_about_dialog(parent: &impl IsA<gtk::Window>) {
    let win = adw::Window::builder()
        .title("About Ensemble")
        .default_width(600)
        .default_height(540)
        .modal(true)
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
    root_box.append(&header);

    // ==========================================
    // Tab 1: About Ensemble & MIT License
    // ==========================================
    let about_scroll = gtk::ScrolledWindow::builder()
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .propagate_natural_height(false)
        .vexpand(true)
        .build();

    let clamp = adw::Clamp::builder()
        .maximum_size(540)
        .margin_top(20)
        .margin_bottom(20)
        .margin_start(20)
        .margin_end(20)
        .build();

    let about_col = gtk::Box::new(gtk::Orientation::Vertical, 16);

    // 1. Hero Header (App Icon + Title + Version Pill + Subtitle)
    let hero_box = gtk::Box::new(gtk::Orientation::Horizontal, 16);
    hero_box.set_valign(gtk::Align::Center);

    let icon_img = if let Some(path) = resolve_icon_path() {
        if let Ok(pixbuf) = Pixbuf::from_file_at_scale(&path, 64, 64, true) {
            let texture = gdk::Texture::for_pixbuf(&pixbuf);
            gtk::Image::from_paintable(Some(&texture))
        } else {
            gtk::Image::from_icon_name("accessories-text-editor-symbolic")
        }
    } else {
        gtk::Image::from_icon_name("accessories-text-editor-symbolic")
    };
    icon_img.set_pixel_size(64);
    hero_box.append(&icon_img);

    let title_col = gtk::Box::new(gtk::Orientation::Vertical, 4);
    title_col.set_valign(gtk::Align::Center);

    let title_row = gtk::Box::new(gtk::Orientation::Horizontal, 10);
    let app_title = gtk::Label::builder()
        .label("Ensemble")
        .css_classes(["title-1"])
        .xalign(0.0)
        .build();
    title_row.append(&app_title);

    let ver_badge = gtk::Label::builder()
        .label("v1.3.0")
        .css_classes(["accent", "heading"])
        .valign(gtk::Align::Center)
        .build();
    title_row.append(&ver_badge);
    title_col.append(&title_row);

    let subtitle_lbl = gtk::Label::builder()
        .label("Next-Gen Fanfiction Editor & Work Skin Composer")
        .css_classes(["dim-label"])
        .xalign(0.0)
        .wrap(true)
        .build();
    title_col.append(&subtitle_lbl);

    hero_box.append(&title_col);
    about_col.append(&hero_box);

    // 2. Application Overview Card
    let info_group = adw::PreferencesGroup::builder()
        .title("Overview")
        .description("A specialized desktop writing suite for Archive of Our Own (AO3) fanfiction authors. Features a distraction-free WYSIWYG composer, live AO3 HTML source view, Work Skin CSS recognition, live dual-flavor preview, visual Chapter Manager, Git-like Snapshots with diffs, and Model Context Protocol (MCP) AI co-authoring.")
        .build();

    let dev_row = adw::ActionRow::builder()
        .use_markup(false)
        .title("Developer")
        .subtitle("Mizumo-prjkt & Ensemble Contributors")
        .build();
    dev_row.add_prefix(&gtk::Image::from_icon_name("avatar-default-symbolic"));
    info_group.add(&dev_row);

    let stack_row = adw::ActionRow::builder()
        .use_markup(false)
        .title("Built With")
        .subtitle("Rust, GTK 4, Libadwaita, WebKitGTK 6.0 & Zstandard")
        .build();
    stack_row.add_prefix(&gtk::Image::from_icon_name("applications-engineering-symbolic"));
    info_group.add(&stack_row);

    let license_row = adw::ActionRow::builder()
        .use_markup(false)
        .title("Software License")
        .subtitle("Released under the MIT License — Free & Open Source Software")
        .build();
    license_row.add_prefix(&gtk::Image::from_icon_name("security-high-symbolic"));

    let mit_badge = gtk::Label::builder()
        .label("MIT License")
        .css_classes(["success", "heading"])
        .valign(gtk::Align::Center)
        .build();
    license_row.add_suffix(&mit_badge);
    info_group.add(&license_row);

    about_col.append(&info_group);

    // 3. Links & Updates Group
    let links_group = adw::PreferencesGroup::builder()
        .title("Links & Releases")
        .build();

    let repo_row = adw::ActionRow::builder()
        .title("GitHub Repository")
        .subtitle("https://github.com/Mizumo-prjkt/ensemble")
        .activatable(true)
        .build();
    repo_row.add_prefix(&gtk::Image::from_icon_name("web-browser-symbolic"));
    let open_repo_btn = gtk::Button::builder()
        .label("Open Repo")
        .valign(gtk::Align::Center)
        .css_classes(["flat"])
        .build();
    {
        let w = win.clone();
        open_repo_btn.connect_clicked(move |_| {
            let launcher = gtk::UriLauncher::new("https://github.com/Mizumo-prjkt/ensemble");
            launcher.launch(Some(&w), gtk::gio::Cancellable::NONE, |_| {});
        });
    }
    repo_row.add_suffix(&open_repo_btn);
    links_group.add(&repo_row);

    let releases_row = adw::ActionRow::builder()
        .title("Software Updates")
        .subtitle("Check for the latest release on GitHub")
        .build();
    releases_row.add_prefix(&gtk::Image::from_icon_name("software-update-available-symbolic"));
    let open_releases_btn = gtk::Button::builder()
        .label("View Releases")
        .valign(gtk::Align::Center)
        .css_classes(["flat"])
        .build();
    {
        let w = win.clone();
        open_releases_btn.connect_clicked(move |_| {
            let launcher = gtk::UriLauncher::new("https://github.com/Mizumo-prjkt/ensemble/releases/latest");
            launcher.launch(Some(&w), gtk::gio::Cancellable::NONE, |_| {});
        });
    }
    releases_row.add_suffix(&open_releases_btn);
    links_group.add(&releases_row);

    about_col.append(&links_group);

    // 4. MIT License Full Text Section
    let mit_group = adw::PreferencesGroup::builder()
        .title("MIT License Notice")
        .description("Ensemble is distributed under the terms of the MIT License:")
        .build();

    let mit_frame = gtk::Frame::new(None);
    let mit_scroll = gtk::ScrolledWindow::builder()
        .hscrollbar_policy(gtk::PolicyType::Automatic)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .propagate_natural_height(false)
        .min_content_height(150)
        .build();
    let mit_tv = gtk::TextView::builder()
        .editable(false)
        .cursor_visible(false)
        .monospace(true)
        .wrap_mode(gtk::WrapMode::WordChar)
        .top_margin(10)
        .bottom_margin(10)
        .left_margin(12)
        .right_margin(12)
        .build();
    mit_tv.buffer().set_text(MIT_LICENSE_TEXT);
    mit_scroll.set_child(Some(&mit_tv));
    mit_frame.set_child(Some(&mit_scroll));
    mit_group.add(&mit_frame);

    about_col.append(&mit_group);

    clamp.set_child(Some(&about_col));
    about_scroll.set_child(Some(&clamp));

    view_stack.add_titled_with_icon(
        &about_scroll,
        Some("about"),
        "About",
        "help-about-symbolic",
    );

    // ==========================================
    // Tab 2: Open Source Credits & Licenses
    // ==========================================
    let credits_paned = gtk::Paned::new(gtk::Orientation::Horizontal);
    credits_paned.set_position(210);
    credits_paned.set_vexpand(true);
    credits_paned.set_hexpand(true);
    credits_paned.set_shrink_start_child(false);
    credits_paned.set_shrink_end_child(true);

    // Left: Component List
    let left_box = gtk::Box::new(gtk::Orientation::Vertical, 6);
    left_box.set_size_request(190, 100);
    left_box.set_margin_top(8);
    left_box.set_margin_bottom(8);
    left_box.set_margin_start(8);
    left_box.set_margin_end(8);

    let list_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .propagate_natural_height(false)
        .build();

    let comp_list = gtk::ListBox::builder()
        .selection_mode(gtk::SelectionMode::Single)
        .css_classes(["navigation-sidebar"])
        .build();

    for entry in CREDITS {
        let row_box = gtk::Box::new(gtk::Orientation::Vertical, 2);
        row_box.set_margin_top(6);
        row_box.set_margin_bottom(6);
        row_box.set_margin_start(8);
        row_box.set_margin_end(8);

        let name_lbl = gtk::Label::builder()
            .label(entry.name)
            .xalign(0.0)
            .ellipsize(gtk::pango::EllipsizeMode::End)
            .css_classes(["heading"])
            .build();
        let lic_lbl = gtk::Label::builder()
            .label(entry.license_short)
            .xalign(0.0)
            .ellipsize(gtk::pango::EllipsizeMode::End)
            .css_classes(["dim-label", "caption"])
            .build();

        row_box.append(&name_lbl);
        row_box.append(&lic_lbl);
        comp_list.append(&row_box);
    }

    list_scroll.set_child(Some(&comp_list));
    left_box.append(&list_scroll);
    credits_paned.set_start_child(Some(&left_box));

    // Right: Detail Pane
    let right_box = gtk::Box::new(gtk::Orientation::Vertical, 10);
    right_box.set_margin_top(12);
    right_box.set_margin_bottom(12);
    right_box.set_margin_start(14);
    right_box.set_margin_end(14);

    let comp_name_lbl = gtk::Label::builder()
        .xalign(0.0)
        .css_classes(["title-3"])
        .wrap(true)
        .build();
    right_box.append(&comp_name_lbl);

    let comp_meta_lbl = gtk::Label::builder()
        .xalign(0.0)
        .css_classes(["dim-label"])
        .wrap(true)
        .build();
    right_box.append(&comp_meta_lbl);

    let comp_desc_lbl = gtk::Label::builder()
        .xalign(0.0)
        .wrap(true)
        .build();
    right_box.append(&comp_desc_lbl);

    let lic_scroll = gtk::ScrolledWindow::builder()
        .vexpand(true)
        .hexpand(true)
        .propagate_natural_height(false)
        .min_content_height(120)
        .build();
    let lic_tv = gtk::TextView::builder()
        .editable(false)
        .cursor_visible(false)
        .monospace(true)
        .wrap_mode(gtk::WrapMode::WordChar)
        .top_margin(10)
        .bottom_margin(10)
        .left_margin(10)
        .right_margin(10)
        .build();
    lic_scroll.set_child(Some(&lic_tv));

    let lic_frame = gtk::Frame::new(None);
    lic_frame.set_vexpand(true);
    lic_frame.set_child(Some(&lic_scroll));
    right_box.append(&lic_frame);

    let action_bar = gtk::Box::new(gtk::Orientation::Horizontal, 8);
    let copy_lic_btn = gtk::Button::builder()
        .label("Copy License Text")
        .icon_name("edit-copy-symbolic")
        .build();
    let visit_web_btn = gtk::Button::builder()
        .label("Visit Website")
        .icon_name("web-browser-symbolic")
        .build();
    action_bar.append(&copy_lic_btn);
    action_bar.append(&visit_web_btn);
    right_box.append(&action_bar);

    credits_paned.set_end_child(Some(&right_box));

    let current_url = std::rc::Rc::new(std::cell::RefCell::new(CREDITS[0].url.to_string()));

    {
        let comp_name_lbl = comp_name_lbl.clone();
        let comp_meta_lbl = comp_meta_lbl.clone();
        let comp_desc_lbl = comp_desc_lbl.clone();
        let lic_tv = lic_tv.clone();
        let current_url = current_url.clone();

        comp_list.connect_row_selected(move |_, row| {
            if let Some(r) = row {
                let idx = r.index() as usize;
                if let Some(entry) = CREDITS.get(idx) {
                    comp_name_lbl.set_text(&format!("{} ({})", entry.name, entry.version));
                    comp_meta_lbl.set_text(&format!(
                        "Author: {}  •  License: {}",
                        entry.author, entry.license_short
                    ));
                    comp_desc_lbl.set_text(entry.description);
                    lic_tv.buffer().set_text(entry.license_text);
                    *current_url.borrow_mut() = entry.url.to_string();
                }
            }
        });
    }

    if let Some(first_row) = comp_list.row_at_index(0) {
        comp_list.select_row(Some(&first_row));
    }

    {
        let lic_tv = lic_tv.clone();
        copy_lic_btn.connect_clicked(move |btn| {
            let buf = lic_tv.buffer();
            let text = buf.text(&buf.start_iter(), &buf.end_iter(), false);
            btn.clipboard().set_text(text.as_str());
        });
    }

    {
        let current_url = current_url.clone();
        let w = win.clone();
        visit_web_btn.connect_clicked(move |_| {
            let url = current_url.borrow().clone();
            let launcher = gtk::UriLauncher::new(&url);
            launcher.launch(Some(&w), gtk::gio::Cancellable::NONE, |_| {});
        });
    }

    view_stack.add_titled_with_icon(
        &credits_paned,
        Some("credits"),
        "Credits & Licenses",
        "emblem-documents-symbolic",
    );

    root_box.append(&view_stack);

    // Bottom Close Bar
    let bottom_sep = gtk::Separator::new(gtk::Orientation::Horizontal);
    root_box.append(&bottom_sep);

    let bottom_bar = gtk::Box::new(gtk::Orientation::Horizontal, 8);
    bottom_bar.set_margin_top(8);
    bottom_bar.set_margin_bottom(8);
    bottom_bar.set_margin_start(16);
    bottom_bar.set_margin_end(16);

    let copyright_lbl = gtk::Label::builder()
        .label("© 2026 Mizumo-prjkt — Released under the MIT License")
        .css_classes(["dim-label", "caption"])
        .hexpand(true)
        .xalign(0.0)
        .build();
    bottom_bar.append(&copyright_lbl);

    let close_btn = gtk::Button::builder()
        .label("Close")
        .build();
    {
        let w = win.clone();
        close_btn.connect_clicked(move |_| {
            w.close();
        });
    }
    bottom_bar.append(&close_btn);
    root_box.append(&bottom_bar);

    win.set_content(Some(&root_box));
    win.present();
}
