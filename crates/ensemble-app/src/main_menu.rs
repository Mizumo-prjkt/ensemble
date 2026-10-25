use chrono::{Local, Timelike};
use gtk4::gdk::Texture;
use gtk4::gdk_pixbuf::Pixbuf;
use gtk4::prelude::*;
use gtk4 as gtk;
use libadwaita as adw;
use std::cell::RefCell;
use std::path::PathBuf;
use std::rc::Rc;

pub struct MainMenuView {
    pub root: gtk::ScrolledWindow,
    greeting_label: gtk::Label,
    #[allow(dead_code)]
    tip_label: gtk::Label,
    #[allow(dead_code)]
    tip_index: Rc<RefCell<usize>>,
}

impl MainMenuView {
    pub fn new<FN, FO, FS, FP, FM>(
        on_new: FN,
        on_open: FO,
        on_settings: FS,
        on_plugins: FP,
        on_marketplace: FM,
    ) -> Rc<Self>
    where
        FN: Fn() + 'static,
        FO: Fn() + 'static,
        FS: Fn() + 'static,
        FP: Fn() + 'static,
        FM: Fn() + 'static,
    {
        let root = gtk::Box::new(gtk::Orientation::Vertical, 0);
        root.set_valign(gtk::Align::Center);
        root.set_halign(gtk::Align::Center);
        root.set_vexpand(true);
        root.set_hexpand(true);
        root.set_margin_top(24);
        root.set_margin_bottom(24);
        root.set_margin_start(24);
        root.set_margin_end(24);

        // Clamp to maintain clean proportions on wide displays
        let clamp = adw::Clamp::builder()
            .maximum_size(720)
            .tightening_threshold(580)
            .build();

        // Main Card
        let card = gtk::Box::new(gtk::Orientation::Vertical, 0);
        card.add_css_class("card");
        card.set_margin_top(16);
        card.set_margin_bottom(16);
        card.set_margin_start(16);
        card.set_margin_end(16);

        // Two-column layout: Left (Brand, Quote, Tips), Right (Actions)
        let columns_box = gtk::Box::new(gtk::Orientation::Horizontal, 28);
        columns_box.set_margin_top(22);
        columns_box.set_margin_bottom(22);
        columns_box.set_margin_start(26);
        columns_box.set_margin_end(26);

        // === Left Column: Brand, Greeting, Tips ===
        let left_col = gtk::Box::new(gtk::Orientation::Vertical, 14);
        left_col.set_hexpand(true);
        left_col.set_size_request(290, -1);

        // Brand header box with circular logo badge
        let brand_header = gtk::Box::new(gtk::Orientation::Horizontal, 12);
        brand_header.set_valign(gtk::Align::Center);

        if let Some(icon_path) = resolve_icon_path() {
            if let Ok(pixbuf) = Pixbuf::from_file_at_scale(&icon_path, 40, 40, true) {
                let texture = Texture::for_pixbuf(&pixbuf);
                let img = gtk::Image::from_paintable(Some(&texture));
                img.set_pixel_size(40);
                img.set_valign(gtk::Align::Center);
                brand_header.append(&img);
            } else {
                let fallback = gtk::Image::from_icon_name("accessories-text-editor-symbolic");
                fallback.set_pixel_size(36);
                fallback.set_valign(gtk::Align::Center);
                brand_header.append(&fallback);
            }
        } else {
            let fallback = gtk::Image::from_icon_name("accessories-text-editor-symbolic");
            fallback.set_pixel_size(36);
            fallback.set_valign(gtk::Align::Center);
            brand_header.append(&fallback);
        }

        let brand_label = gtk::Label::builder()
            .label("Ensemble")
            .xalign(0.0)
            .valign(gtk::Align::Center)
            .css_classes(["title-1", "bold"])
            .build();
        brand_header.append(&brand_label);
        left_col.append(&brand_header);

        let username = get_username();
        let greeting_text = get_greeting(&username);
        let greeting_label = gtk::Label::builder()
            .label(&format!("“ {} ”", greeting_text))
            .wrap(true)
            .xalign(0.0)
            .css_classes(["body", "dim-label"])
            .build();
        left_col.append(&greeting_label);

        let spacer = gtk::Box::new(gtk::Orientation::Vertical, 0);
        spacer.set_vexpand(true);
        left_col.append(&spacer);

        // Tips card (light green / accent tinted)
        let tip_box = gtk::Box::new(gtk::Orientation::Vertical, 6);
        tip_box.add_css_class("tip-box");

        let tip_header = gtk::Box::new(gtk::Orientation::Horizontal, 8);
        let tip_title = gtk::Label::builder()
            .label("Tips")
            .xalign(0.0)
            .css_classes(["heading"])
            .hexpand(true)
            .build();
        tip_header.append(&tip_title);

        let tip_dismiss_btn = gtk::Button::builder()
            .icon_name("window-close-symbolic")
            .tooltip_text("Next Tip")
            .css_classes(["flat", "circular"])
            .valign(gtk::Align::Center)
            .build();
        tip_header.append(&tip_dismiss_btn);
        tip_box.append(&tip_header);

        let tips = get_tips_list();
        let tip_index = Rc::new(RefCell::new(0));
        let tip_label = gtk::Label::builder()
            .label(tips[0])
            .wrap(true)
            .xalign(0.0)
            .css_classes(["body"])
            .build();
        tip_box.append(&tip_label);

        // Connect dismiss/next tip
        {
            let tip_label_clone = tip_label.clone();
            let tip_idx_clone = tip_index.clone();
            tip_dismiss_btn.connect_clicked(move |_| {
                let mut idx = tip_idx_clone.borrow_mut();
                *idx = (*idx + 1) % tips.len();
                tip_label_clone.set_text(tips[*idx]);
            });
        }

        left_col.append(&tip_box);
        columns_box.append(&left_col);

        // === Right Column: Actions ===
        let right_col = gtk::Box::new(gtk::Orientation::Vertical, 10);
        right_col.set_hexpand(true);
        right_col.set_valign(gtk::Align::Center);
        right_col.set_size_request(290, -1);

        // 1. New Project Button
        let btn_new = create_action_button(
            "document-new-symbolic",
            "New Project",
            "Start a fresh AO3 story or chapter",
        );
        btn_new.connect_clicked(move |_| {
            on_new();
        });
        right_col.append(&btn_new);

        // 2. Open Project Button
        let btn_open = create_action_button(
            "document-open-symbolic",
            "Open",
            "Open an existing .ao3proj file (Ctrl+O)",
        );
        btn_open.connect_clicked(move |_| {
            on_open();
        });
        right_col.append(&btn_open);

        // 3. Settings Button
        let btn_settings = create_action_button(
            "preferences-system-symbolic",
            "Settings",
            "Preferences, theme, and MCP configuration",
        );
        btn_settings.connect_clicked(move |_| {
            on_settings();
        });
        right_col.append(&btn_settings);

        // 4. Plugins Button
        let btn_plugins = create_action_button(
            "application-x-addon-symbolic",
            "Plugins",
            "Manage sandboxed .er extensions & tools",
        );
        btn_plugins.connect_clicked(move |_| {
            on_plugins();
        });
        right_col.append(&btn_plugins);

        // 5. Standalone Marketplace (WIP) Button
        let btn_marketplace = create_action_button(
            "system-software-install-symbolic",
            "Marketplace (WIP)",
            "Browse community plugin repository (Work in Progress)",
        );
        btn_marketplace.connect_clicked(move |_| {
            on_marketplace();
        });
        right_col.append(&btn_marketplace);

        columns_box.append(&right_col);
        card.append(&columns_box);

        clamp.set_child(Some(&card));
        root.append(&clamp);

        let scroll = gtk::ScrolledWindow::builder()
            .hscrollbar_policy(gtk::PolicyType::Never)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .vexpand(true)
            .hexpand(true)
            .build();
        scroll.set_child(Some(&root));

        Rc::new(Self {
            root: scroll,
            greeting_label,
            tip_label,
            tip_index,
        })
    }

    pub fn refresh_greeting(&self) {
        let username = get_username();
        let greeting = get_greeting(&username);
        self.greeting_label.set_text(&format!("“ {} ”", greeting));
    }
}

fn create_action_button(icon_name: &str, title: &str, description: &str) -> gtk::Button {
    let btn = gtk::Button::new();
    btn.add_css_class("main-action-btn");

    let hbox = gtk::Box::new(gtk::Orientation::Horizontal, 12);
    hbox.set_margin_start(14);
    hbox.set_margin_end(14);
    hbox.set_margin_top(10);
    hbox.set_margin_bottom(10);

    let icon = gtk::Image::from_icon_name(icon_name);
    icon.set_pixel_size(24);
    icon.set_valign(gtk::Align::Center);
    hbox.append(&icon);

    let vbox = gtk::Box::new(gtk::Orientation::Vertical, 2);
    vbox.set_hexpand(true);
    vbox.set_valign(gtk::Align::Center);

    let title_label = gtk::Label::builder()
        .label(title)
        .xalign(0.0)
        .css_classes(["heading"])
        .build();
    vbox.append(&title_label);

    let desc_label = gtk::Label::builder()
        .label(description)
        .xalign(0.0)
        .css_classes(["caption", "dim-label"])
        .build();
    vbox.append(&desc_label);

    hbox.append(&vbox);
    btn.set_child(Some(&hbox));
    btn
}

fn get_username() -> String {
    std::env::var("USER")
        .or_else(|_| std::env::var("USERNAME"))
        .unwrap_or_else(|_| "Writer".to_string())
}

fn get_greeting(username: &str) -> String {
    let hour = Local::now().hour();

    // 3 AM - 5 AM special rule:
    if hour >= 3 && hour < 5 {
        return format!("Early writer gets the fame, {}!", username);
    }

    let standard_quotes = [
        format!("Ready to write, {}", username),
        format!("Let's get creative, {}", username),
        format!("Got new ideas? {}", username),
        "For those who can write, make stories, and for those who cannot, reads".to_string(),
        "Put your heart and soul when writing a fanfiction".to_string(),
        "Be honest of your work, if you used AI in your fiction, please be honest to your readers about it, okay?!".to_string(),
    ];

    let time_specific_quotes: &[String] = if hour >= 5 && hour < 12 {
        // Morning (5AM - 12PM)
        &[
            format!("Good morning, {}. Are you ready to write?", username),
            format!("Good day, {}", username),
        ]
    } else if hour >= 12 && hour < 17 {
        // Afternoon (12PM - 5PM)
        &[
            format!("Afternoon drabble? {}", username),
            format!("Good day, {}", username),
        ]
    } else if hour >= 17 && hour < 22 {
        // Evening (5PM - 10PM)
        &[
            format!("Evening writing, {}?", username),
            format!("Good day, {}", username),
        ]
    } else {
        // Night / Midnight (10PM - 3AM)
        &[
            "Midnight writing time?".to_string(),
            format!("Midnight writing time, {}?", username),
        ]
    };

    // Cycle or pick pseudo-randomly based on current minute & second
    let sec = (Local::now().timestamp_subsec_millis() / 250) as usize;
    if sec % 2 == 0 && !time_specific_quotes.is_empty() {
        time_specific_quotes[sec % time_specific_quotes.len()].clone()
    } else {
        standard_quotes[sec % standard_quotes.len()].clone()
    }
}

fn get_tips_list() -> &'static [&'static str] {
    &[
        "Press Ctrl + P to see the preview of your work",
        "Press Ctrl + O to open an existing .ao3proj project archive",
        "Press Ctrl + S to save your progress with revision snapshots",
        "Press Ctrl + N to begin a new story with 1-based chapter ordering",
        "Press F1 at any time to open the complete Help Guide",
        "Work Skin CSS classes are rendered live with authentic AO3 styles",
        "Use Model Context Protocol (MCP) on port 26303 to co-author with AI agents",
    ]
}

fn resolve_icon_path() -> Option<PathBuf> {
    for p in &[
        PathBuf::from("resources/icon.png"),
        PathBuf::from("/usr/share/icons/hicolor/256x256/apps/ensemble.png"),
        std::env::current_exe()
            .ok()
            .and_then(|e| e.parent().map(|d| d.join("resources/icon.png")))
            .unwrap_or_default(),
        std::env::current_exe()
            .ok()
            .and_then(|e| e.parent().and_then(|d| d.parent()).map(|d| d.join("resources/icon.png")))
            .unwrap_or_default(),
    ] {
        if p.exists() {
            return Some(p.clone());
        }
    }
    None
}
