use crate::controllers::WebViewPreviewController;
use crate::dialogs::{
    show_about_dialog, show_chapter_manager_dialog, show_help_guide_dialog,
    show_image_embed_dialog, show_plugin_manager_with_host, show_plugin_marketplace_dialog,
    show_preferences_dialog, show_problems_dialog, show_project_properties_dialog,
    show_task_manager_dialog, PluginUiHost, UiComponentTelemetry,
};
use crate::main_menu::MainMenuView;
use ensemble_core::project::Ao3Project;
use ensemble_core::serializer::ProjectSerializer;
use ensemble_html::formatter::HtmlFormatter;
use ensemble_html::HtmlLinter;
use ensemble_mcp::McpHandler;
use ensemble_theme::ThemeManager;
use gtk4 as gtk;
use gtk4::gdk;
use gtk4::gio;
use gtk4::glib;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::cell::{Cell, RefCell};
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;
use webkit6::prelude::*;

#[derive(serde::Deserialize, Default, Debug, Clone)]
struct FormatState {
    #[serde(default)]
    bold: bool,
    #[serde(default)]
    italic: bool,
    #[serde(default)]
    underline: bool,
    #[serde(default)]
    strike: bool,
    #[serde(default)]
    h1: bool,
    #[serde(default)]
    h2: bool,
    #[serde(default)]
    h3: bool,
    #[serde(default)]
    p: bool,
    #[serde(default)]
    quote: bool,
    #[serde(default)]
    ul: bool,
    #[serde(default)]
    ol: bool,
}

pub struct EnsembleWindow {
    pub window: adw::ApplicationWindow,
    handler: Arc<McpHandler>,
    plugin_host: PluginUiHost,
    toast_overlay: adw::ToastOverlay,
    window_title: adw::WindowTitle,
    problems_btn: gtk::Button,
    status_label: gtk::Label,
    dirty_label: gtk::Label,
    plugin_badge_label: gtk::Label,
    mcp_label: gtk::Label,
    open_btn: gtk::Button,
    save_btn: gtk::Button,
    copy_html_btn: gtk::Button,
    home_btn: gtk::Button,
    view_stack: gtk::Stack,
    notebook: gtk::Notebook,
    main_menu_view: Rc<MainMenuView>,
    chapter_list_box: gtk::ListBox,
    rich_web_view: webkit6::WebView,
    rich_editor_loaded: Rc<RefCell<bool>>,
    html_text_view: gtk::TextView,
    css_text_view: gtk::TextView,
    line_gutter_area: gtk::DrawingArea,
    loading_box: gtk::Box,
    loading_spinner: gtk::Spinner,
    loading_label: gtk::Label,
    current_file_path: Rc<RefCell<Option<PathBuf>>>,
    is_syncing: Rc<RefCell<bool>>,
    sideview_btn: gtk::ToggleButton,
    sideview_box: gtk::Box,
    sideview_web_view: webkit6::WebView,
    sideview_reversi: Rc<RefCell<bool>>,
    minimap_area: gtk::DrawingArea,
    line_col_label: gtk::Label,
    tag_breadcrumb_label: gtk::Label,
    issue_summary_label: gtk::Label,
    issue_icon: gtk::Image,
    toggle_details_btn: gtk::Button,
    issue_details_box: gtk::Box,
}

impl EnsembleWindow {
    pub fn new(app: &adw::Application, handler: Arc<McpHandler>) -> Rc<Self> {
        // Load custom application styles (tips box, action button cards, code font, loading card)
        let css_provider = gtk::CssProvider::new();
        css_provider.load_from_string(
            "
            .tip-box {
                background-color: alpha(@success_color, 0.16);
                border: 1px solid alpha(@success_color, 0.35);
                border-radius: 14px;
                padding: 14px 16px;
            }
            .main-action-btn {
                border-radius: 12px;
                padding: 4px;
            }
            .code-editor-font {
                font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', 'Source Code Pro', 'DejaVu Sans Mono', monospace;
                font-size: 13px;
                line-height: 1.5;
            }
            .loading-overlay-card {
                background-color: alpha(@window_bg_color, 0.94);
                border: 1px solid alpha(@borders, 0.65);
                border-radius: 14px;
                padding: 14px 22px;
                box-shadow: 0 4px 18px rgba(0, 0, 0, 0.32);
            }
            .toolbar-btn {
                padding: 2px 6px !important;
                min-width: 26px !important;
                min-height: 26px !important;
                font-size: 12px;
                font-weight: 500;
            }
            .format-toggle:checked {
                background-color: alpha(@accent_color, 0.32) !important;
                color: @accent_color !important;
                font-weight: bold !important;
                border-color: alpha(@accent_color, 0.7) !important;
            }
            ",
        );
        if let Some(display) = gdk::Display::default() {
            gtk::style_context_add_provider_for_display(
                &display,
                &css_provider,
                gtk::STYLE_PROVIDER_PRIORITY_APPLICATION,
            );
        }

        let window = adw::ApplicationWindow::builder()
            .application(app)
            .title("Ensemble")
            .default_width(1024)
            .default_height(640)
            .build();

        let window_title = adw::WindowTitle::new("Ensemble", "AO3 Fanfiction Typewriter");
        let header = adw::HeaderBar::new();
        header.set_title_widget(Some(&window_title));

        // --- Left Header: App Menu & Quick Actions ---
        let menu_model = gio::Menu::new();
        let nav_section = gio::Menu::new();
        nav_section.append(Some("Welcome Screen / Main Menu"), Some("app.home"));
        menu_model.append_section(None, &nav_section);

        let proj_section = gio::Menu::new();
        proj_section.append(Some("New Project"), Some("app.new"));
        proj_section.append(Some("Open Project…"), Some("app.open"));
        proj_section.append(Some("Save Project"), Some("app.save"));
        proj_section.append(Some("Save Project As…"), Some("app.save_as"));
        menu_model.append_section(None, &proj_section);

        let tools_section = gio::Menu::new();
        tools_section.append(Some("Project Properties & Tags…"), Some("app.properties"));
        tools_section.append(Some("Chapter Manager…"), Some("app.chapter_manager"));
        tools_section.append(Some("Toggle Live Preview"), Some("app.preview"));
        tools_section.append(Some("Task Manager & Resource Monitor…"), Some("app.task_manager"));
        tools_section.append(Some("Plugins & Extensions…"), Some("app.plugins"));
        tools_section.append(Some("Plugin Marketplace (WIP)…"), Some("app.marketplace"));
        menu_model.append_section(None, &tools_section);

        let help_section = gio::Menu::new();
        help_section.append(Some("Preferences…"), Some("app.preferences"));
        help_section.append(Some("Help Guide…"), Some("app.help"));
        help_section.append(Some("About Ensemble"), Some("app.about"));
        menu_model.append_section(None, &help_section);

        let menu_btn = gtk::MenuButton::builder()
            .icon_name("open-menu-symbolic")
            .menu_model(&menu_model)
            .tooltip_text("Main Menu")
            .build();
        header.pack_start(&menu_btn);

        let home_btn = gtk::Button::builder()
            .icon_name("go-home-symbolic")
            .tooltip_text("Return to Welcome Screen")
            .build();
        header.pack_start(&home_btn);

        let open_btn = gtk::Button::builder()
            .icon_name("document-open-symbolic")
            .tooltip_text("Open Project (Ctrl+O)")
            .build();
        header.pack_start(&open_btn);

        let save_btn = gtk::Button::builder()
            .icon_name("document-save-symbolic")
            .tooltip_text("Save Project (Ctrl+S)")
            .build();
        header.pack_start(&save_btn);

        // --- Right Header: Problems, Copy HTML, Task Manager, Theme, Preferences ---
        let problems_btn = gtk::Button::builder()
            .icon_name("dialog-information-symbolic")
            .label("✓ 0 Problems")
            .tooltip_text("AO3 HTML validation and diagnostics")
            .build();
        header.pack_end(&problems_btn);

        let copy_html_btn = gtk::Button::builder()
            .icon_name("edit-copy-symbolic")
            .tooltip_text("Copy active chapter AO3 HTML to clipboard")
            .build();
        header.pack_end(&copy_html_btn);

        let sideview_btn = gtk::ToggleButton::builder()
            .icon_name("view-dual-symbolic")
            .tooltip_text("Toggle Live Preview Sideview (Ctrl+P)")
            .active(true)
            .build();
        header.pack_end(&sideview_btn);

        let task_mgr_btn = gtk::Button::builder()
            .icon_name("utilities-system-monitor-symbolic")
            .tooltip_text("Task Manager & Resource Monitor (Ctrl+Shift+M)")
            .build();
        header.pack_end(&task_mgr_btn);

        let plugins_btn = gtk::Button::builder()
            .icon_name("application-x-addon-symbolic")
            .tooltip_text("Plugin Manager, Actions & Custom Plugin UIs")
            .build();
        header.pack_end(&plugins_btn);

        // Theme Switcher Menu with icons
        let theme_menu = gio::Menu::new();
        let item_sys = gio::MenuItem::new(Some("Follow Desktop Theme"), Some("app.theme_system"));
        item_sys.set_icon(&gio::ThemedIcon::new("preferences-desktop-display-symbolic"));
        theme_menu.append_item(&item_sys);

        let item_light = gio::MenuItem::new(Some("Light Mode"), Some("app.theme_light"));
        item_light.set_icon(&gio::ThemedIcon::new("weather-clear-symbolic"));
        theme_menu.append_item(&item_light);

        let item_dark = gio::MenuItem::new(Some("Dark Mode"), Some("app.theme_dark"));
        item_dark.set_icon(&gio::ThemedIcon::new("weather-clear-night-symbolic"));
        theme_menu.append_item(&item_dark);

        let theme_btn = gtk::MenuButton::builder()
            .icon_name("weather-clear-night-symbolic")
            .menu_model(&theme_menu)
            .tooltip_text("Switch Theme")
            .build();
        header.pack_end(&theme_btn);

        let prefs_btn = gtk::Button::builder()
            .icon_name("preferences-system-symbolic")
            .tooltip_text("Preferences")
            .build();
        header.pack_end(&prefs_btn);

        // --- Root Layout ---
        let root_box = gtk::Box::new(gtk::Orientation::Vertical, 0);
        root_box.append(&header);

        let toast_overlay = adw::ToastOverlay::new();

        // Stack to switch between Main Menu and Editor View
        let view_stack = gtk::Stack::new();
        view_stack.set_transition_type(gtk::StackTransitionType::Crossfade);
        view_stack.set_vhomogeneous(false);
        view_stack.set_hhomogeneous(false);
        view_stack.set_vexpand(true);
        view_stack.set_hexpand(true);

        // ==========================================
        // 1. Editor Page
        // ==========================================
        let editor_page_box = gtk::Box::new(gtk::Orientation::Vertical, 0);

        // Main Paned (Chapters Sidebar | Editors + Preview)
        let main_paned = gtk::Paned::new(gtk::Orientation::Horizontal);
        main_paned.set_position(240);
        main_paned.set_vexpand(true);
        main_paned.set_hexpand(true);
        main_paned.set_resize_start_child(false);
        main_paned.set_shrink_start_child(false);
        main_paned.set_resize_end_child(true);
        main_paned.set_shrink_end_child(true);

        // --- Left Sidebar (Chapters) ---
        let sidebar_box = gtk::Box::new(gtk::Orientation::Vertical, 6);
        sidebar_box.set_size_request(200, 100);
        sidebar_box.set_margin_top(8);
        sidebar_box.set_margin_bottom(8);
        sidebar_box.set_margin_start(8);
        sidebar_box.set_margin_end(8);

        let sidebar_header = gtk::Box::new(gtk::Orientation::Horizontal, 4);
        let chapters_title = gtk::Label::builder()
            .label("Chapters")
            .xalign(0.0)
            .css_classes(["title-4"])
            .hexpand(true)
            .build();
        sidebar_header.append(&chapters_title);

        let add_chapter_btn = gtk::Button::builder()
            .icon_name("list-add-symbolic")
            .tooltip_text("Add Chapter")
            .css_classes(["flat", "circular"])
            .build();
        sidebar_header.append(&add_chapter_btn);

        let del_chapter_btn = gtk::Button::builder()
            .icon_name("list-remove-symbolic")
            .tooltip_text("Delete Selected Chapter")
            .css_classes(["flat", "circular"])
            .build();
        sidebar_header.append(&del_chapter_btn);

        let manage_chapter_btn = gtk::Button::builder()
            .icon_name("view-list-symbolic")
            .tooltip_text("Manage & Reorder Chapters")
            .css_classes(["flat", "circular"])
            .build();
        sidebar_header.append(&manage_chapter_btn);
        sidebar_box.append(&sidebar_header);

        let chapter_scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Never)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .propagate_natural_width(false)
            .min_content_height(100)
            .build();
        let chapter_list_box = gtk::ListBox::builder()
            .selection_mode(gtk::SelectionMode::Single)
            .css_classes(["navigation-sidebar"])
            .build();
        chapter_scroll.set_child(Some(&chapter_list_box));
        sidebar_box.append(&chapter_scroll);

        main_paned.set_start_child(Some(&sidebar_box));

        // --- Center / Right Split Paned (Notebook | Sideview Preview) ---
        let editor_split_paned = gtk::Paned::new(gtk::Orientation::Horizontal);
        editor_split_paned.set_position(650);
        editor_split_paned.set_vexpand(true);
        editor_split_paned.set_hexpand(true);
        editor_split_paned.set_resize_start_child(true);
        editor_split_paned.set_shrink_start_child(true);
        editor_split_paned.set_resize_end_child(false);
        editor_split_paned.set_shrink_end_child(true);

        let notebook = gtk::Notebook::new();
        notebook.set_scrollable(true);
        notebook.set_vexpand(true);
        notebook.set_hexpand(true);
        notebook.set_size_request(260, 120);

        // Tab 1: WYSIWYG Rich Text Editor
        let rich_box = gtk::Box::new(gtk::Orientation::Vertical, 0);

        let is_updating_toolbar_state = Rc::new(Cell::new(false));

        let rich_ucm = webkit6::UserContentManager::new();
        let rich_web_view = webkit6::WebView::builder()
            .user_content_manager(&rich_ucm)
            .vexpand(true)
            .hexpand(true)
            .build();
        rich_web_view.set_size_request(200, 100);

        let make_tb_toggle_btn = |label: &str, tooltip: &str| -> gtk::ToggleButton {
            gtk::ToggleButton::builder()
                .label(label)
                .tooltip_text(tooltip)
                .css_classes(["flat", "toolbar-btn", "format-toggle"])
                .build()
        };

        let make_tb_btn = |label: &str, tooltip: &str| -> gtk::Button {
            gtk::Button::builder()
                .label(label)
                .tooltip_text(tooltip)
                .css_classes(["flat", "toolbar-btn"])
                .build()
        };

        // 1. Inline styles cluster
        let bold_btn = make_tb_toggle_btn("<b>", "Bold (Ctrl+B)");
        let italic_btn = make_tb_toggle_btn("<i>", "Italic (Ctrl+I)");
        let underline_btn = make_tb_toggle_btn("<u>", "Underline (Ctrl+U)");
        let strike_btn = make_tb_toggle_btn("<s>", "Strikethrough");

        let inline_cluster = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        inline_cluster.add_css_class("linked");
        inline_cluster.append(&bold_btn);
        inline_cluster.append(&italic_btn);
        inline_cluster.append(&underline_btn);
        inline_cluster.append(&strike_btn);

        // 2. Heading & Paragraph block cluster
        let h1_btn = make_tb_toggle_btn("H1", "Heading 1 (<h1>)");
        let h2_btn = make_tb_toggle_btn("H2", "Heading 2 (<h2>)");
        let h3_btn = make_tb_toggle_btn("H3", "Heading 3 (<h3>)");
        let p_btn = make_tb_toggle_btn("¶", "Paragraph (<p>)");

        let block_cluster = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        block_cluster.add_css_class("linked");
        block_cluster.append(&h1_btn);
        block_cluster.append(&h2_btn);
        block_cluster.append(&h3_btn);
        block_cluster.append(&p_btn);

        // 3. Quotes & Lists cluster
        let quote_btn = make_tb_toggle_btn("❝", "Toggle Blockquote (<blockquote>)");
        let ul_btn = make_tb_toggle_btn("•", "Bullet List (<ul>)");
        let ol_btn = make_tb_toggle_btn("1.", "Numbered List (<ol>)");

        let list_cluster = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        list_cluster.add_css_class("linked");
        list_cluster.append(&quote_btn);
        list_cluster.append(&ul_btn);
        list_cluster.append(&ol_btn);

        // 4. Insertions cluster
        let link_btn = make_tb_btn("🔗", "Insert Link (<a>)");
        let rich_image_btn = make_tb_btn("🖼️", "Insert Linked Image Embed (AO3 URL Attachment)");
        let hr_btn = make_tb_btn("―", "Horizontal Rule (<hr>)");

        let insert_cluster = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        insert_cluster.add_css_class("linked");
        insert_cluster.append(&link_btn);
        insert_cluster.append(&rich_image_btn);
        insert_cluster.append(&hr_btn);

        // 5. CSS Styles & Clear formatting cluster
        let rich_css_style_btn = gtk::MenuButton::builder()
            .label("🎨")
            .tooltip_text("Apply Work Skin CSS Class as <span> or <div>")
            .css_classes(["flat", "toolbar-btn"])
            .build();
        let clear_btn = make_tb_btn("🧹", "Clear Formatting & Work Skin Styles");

        let style_cluster = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        style_cluster.add_css_class("linked");
        style_cluster.append(&rich_css_style_btn);
        style_cluster.append(&clear_btn);

        let rich_toolbar = gtk::Box::new(gtk::Orientation::Horizontal, 4);
        rich_toolbar.set_margin_top(2);
        rich_toolbar.set_margin_bottom(2);
        rich_toolbar.set_margin_start(2);
        rich_toolbar.set_margin_end(2);

        rich_toolbar.append(&inline_cluster);
        rich_toolbar.append(&block_cluster);
        rich_toolbar.append(&list_cluster);
        rich_toolbar.append(&insert_cluster);
        rich_toolbar.append(&style_cluster);

        let rich_tb_scroll = gtk::ScrolledWindow::builder()
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .vscrollbar_policy(gtk::PolicyType::Never)
            .propagate_natural_width(false)
            .hexpand(true)
            .vexpand(false)
            .build();
        rich_tb_scroll.set_child(Some(&rich_toolbar));

        // Horizontal mouse wheel scroll support for rich_tb_scroll
        {
            let scroll_ctrl = gtk::EventControllerScroll::new(
                gtk::EventControllerScrollFlags::VERTICAL | gtk::EventControllerScrollFlags::HORIZONTAL,
            );
            let hadj = rich_tb_scroll.hadjustment();
            scroll_ctrl.connect_scroll(move |_, dx, dy| {
                let delta = if dy.abs() > 0.0 { dy } else { dx };
                hadj.set_value(hadj.value() + delta * 24.0);
                glib::Propagation::Stop
            });
            rich_tb_scroll.add_controller(scroll_ctrl);
        }

        // Qt-Style Collapsed Options Extension Menu Button
        let rich_extension_btn = gtk::MenuButton::builder()
            .icon_name("view-more-symbolic")
            .tooltip_text("More Formatting Options (Collapsed Options)")
            .css_classes(["flat", "toolbar-btn"])
            .valign(gtk::Align::Center)
            .build();

        let rich_popover = gtk::Popover::new();
        let rich_pop_box = gtk::Box::new(gtk::Orientation::Vertical, 4);
        rich_pop_box.set_margin_top(8);
        rich_pop_box.set_margin_bottom(8);
        rich_pop_box.set_margin_start(8);
        rich_pop_box.set_margin_end(8);
        rich_pop_box.set_size_request(200, -1);

        let pop_lbl = gtk::Label::builder()
            .label("More Formatting Options")
            .xalign(0.0)
            .css_classes(["caption", "dim-label"])
            .build();
        rich_pop_box.append(&pop_lbl);

        let make_pop_action = |lbl: &str, pop: &gtk::Popover| -> gtk::Button {
            let btn = gtk::Button::builder()
                .label(lbl)
                .css_classes(["flat"])
                .build();
            if let Some(child) = btn.child() {
                child.set_halign(gtk::Align::Start);
            }
            let p = pop.clone();
            btn.connect_clicked(move |_| {
                p.popdown();
            });
            btn
        };

        let pop_h1 = make_pop_action("H1  Heading 1", &rich_popover);
        let pop_h2 = make_pop_action("H2  Heading 2", &rich_popover);
        let pop_h3 = make_pop_action("H3  Heading 3", &rich_popover);
        let pop_p = make_pop_action("¶    Normal Paragraph", &rich_popover);
        let pop_quote = make_pop_action("❝    Blockquote", &rich_popover);
        let pop_ul = make_pop_action("•    Bullet List", &rich_popover);
        let pop_ol = make_pop_action("1.   Numbered List", &rich_popover);
        let pop_link = make_pop_action("🔗  Insert Link…", &rich_popover);
        let pop_image = make_pop_action("🖼️  Insert Image Embed…", &rich_popover);
        let pop_hr = make_pop_action("―    Horizontal Rule", &rich_popover);
        let pop_clear = make_pop_action("🧹  Clear Formatting", &rich_popover);

        rich_pop_box.append(&pop_h1);
        rich_pop_box.append(&pop_h2);
        rich_pop_box.append(&pop_h3);
        rich_pop_box.append(&pop_p);
        rich_pop_box.append(&pop_quote);
        rich_pop_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        rich_pop_box.append(&pop_ul);
        rich_pop_box.append(&pop_ol);
        rich_pop_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        rich_pop_box.append(&pop_link);
        rich_pop_box.append(&pop_image);
        rich_pop_box.append(&pop_hr);
        rich_pop_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        rich_pop_box.append(&pop_clear);

        rich_popover.set_child(Some(&rich_pop_box));
        rich_extension_btn.set_popover(Some(&rich_popover));

        let rich_toolbar_row = gtk::Box::new(gtk::Orientation::Horizontal, 2);
        rich_toolbar_row.set_margin_top(2);
        rich_toolbar_row.set_margin_bottom(2);
        rich_toolbar_row.set_margin_start(4);
        rich_toolbar_row.set_margin_end(4);

        rich_toolbar_row.append(&rich_tb_scroll);
        rich_toolbar_row.append(&rich_extension_btn);

        {
            let rwv = rich_web_view.clone();
            pop_h1.connect_clicked(move |_| exec_js(&rwv, "toggleBlock('h1');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_h2.connect_clicked(move |_| exec_js(&rwv, "toggleBlock('h2');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_h3.connect_clicked(move |_| exec_js(&rwv, "toggleBlock('h3');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_p.connect_clicked(move |_| exec_js(&rwv, "toggleBlock('p');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_quote.connect_clicked(move |_| exec_js(&rwv, "toggleBlock('blockquote');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_ul.connect_clicked(move |_| exec_js(&rwv, "execCmd('insertUnorderedList');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_ol.connect_clicked(move |_| exec_js(&rwv, "execCmd('insertOrderedList');"));
        }
        {
            let rwv = rich_web_view.clone();
            let win = window.clone();
            pop_link.connect_clicked(move |_| show_insert_link_dialog(&win, &rwv));
        }
        {
            let rwv = rich_web_view.clone();
            let win = window.clone();
            pop_image.connect_clicked(move |_| {
                let rwv_in = rwv.clone();
                show_image_embed_dialog(&win, move |snippet| {
                    if let Ok(json_html) = serde_json::to_string(&snippet) {
                        exec_js(&rwv_in, &format!("insertHtmlSnippet({});", json_html));
                    }
                });
            });
        }
        {
            let rwv = rich_web_view.clone();
            pop_hr.connect_clicked(move |_| exec_js(&rwv, "execCmd('insertHorizontalRule');"));
        }
        {
            let rwv = rich_web_view.clone();
            pop_clear.connect_clicked(move |_| exec_js(&rwv, "clearCssStyle(); execCmd('removeFormat');"));
        }

        // Actions for rich toolbar toggle buttons (with is_updating_toolbar_state guard)
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            bold_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "execCmd('bold');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            italic_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "execCmd('italic');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            underline_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "execCmd('underline');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            strike_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "execCmd('strikeThrough');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            h1_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "toggleBlock('h1');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            h2_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "toggleBlock('h2');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            h3_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "toggleBlock('h3');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            p_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "toggleBlock('p');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            quote_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "toggleBlock('blockquote');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            ul_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "execCmd('insertUnorderedList');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            let updating = is_updating_toolbar_state.clone();
            ol_btn.connect_toggled(move |_| {
                if !updating.get() {
                    exec_js(&rwv, "execCmd('insertOrderedList');");
                }
            });
        }
        {
            let rwv = rich_web_view.clone();
            hr_btn.connect_clicked(move |_| exec_js(&rwv, "execCmd('insertHorizontalRule');"));
        }
        {
            let rwv = rich_web_view.clone();
            clear_btn.connect_clicked(move |_| exec_js(&rwv, "clearCssStyle(); execCmd('removeFormat');"));
        }
        {
            let rwv = rich_web_view.clone();
            let win = window.clone();
            link_btn.connect_clicked(move |_| {
                show_insert_link_dialog(&win, &rwv);
            });
        }
        {
            let rwv = rich_web_view.clone();
            let win = window.clone();
            rich_image_btn.connect_clicked(move |_| {
                let rwv_in = rwv.clone();
                show_image_embed_dialog(&win, move |snippet| {
                    if let Ok(json_html) = serde_json::to_string(&snippet) {
                        exec_js(&rwv_in, &format!("insertHtmlSnippet({});", json_html));
                    }
                });
            });
        }

        rich_box.append(&rich_toolbar_row);
        rich_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));

        let rich_web_scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .hexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .propagate_natural_width(false)
            .min_content_height(100)
            .min_content_width(200)
            .build();
        rich_web_scroll.set_child(Some(&rich_web_view));
        rich_box.append(&rich_web_scroll);

        let tab1_label = gtk::Label::new(Some("✍️ Rich Text"));
        notebook.append_page(&rich_box, Some(&tab1_label));

        // Tab 2: Raw HTML Editor with IDE Columns, Line Numbers, Diagnostics & Minimap
        let html_box = gtk::Box::new(gtk::Orientation::Vertical, 0);

        // A. Collapsible Diagnostics Banner
        let issue_banner = gtk::Box::new(gtk::Orientation::Vertical, 4);
        issue_banner.set_margin_start(8);
        issue_banner.set_margin_end(8);
        issue_banner.set_margin_top(4);
        issue_banner.set_margin_bottom(2);

        let issue_header_row = gtk::Box::new(gtk::Orientation::Horizontal, 8);
        let issue_icon = gtk::Image::from_icon_name("dialog-information-symbolic");
        issue_header_row.append(&issue_icon);

        let issue_summary_label = gtk::Label::builder()
            .label("✓ 0 Problems in HTML")
            .xalign(0.0)
            .hexpand(true)
            .ellipsize(gtk4::pango::EllipsizeMode::End)
            .css_classes(["dim-label"])
            .build();
        issue_header_row.append(&issue_summary_label);

        let toggle_details_btn = gtk::Button::builder()
            .label("Show Details ▾")
            .visible(false)
            .build();
        issue_header_row.append(&toggle_details_btn);

        let view_problems_win_btn = gtk::Button::builder()
            .label("🔍 View in Problems Window")
            .visible(false)
            .build();
        issue_header_row.append(&view_problems_win_btn);

        issue_banner.append(&issue_header_row);

        let issue_details_box = gtk::Box::new(gtk::Orientation::Vertical, 2);
        issue_details_box.set_visible(false);
        issue_details_box.set_margin_start(24);
        issue_details_box.set_margin_top(4);
        issue_details_box.set_margin_bottom(4);
        issue_banner.append(&issue_details_box);

        html_box.append(&issue_banner);

        // B. Quick Tag Toolbar (single-row linked clusters in ScrolledWindow with Revealer)
        let tag_cluster_inline = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        tag_cluster_inline.add_css_class("linked");

        let tag_cluster_block = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        tag_cluster_block.add_css_class("linked");

        let tag_cluster_insert = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        tag_cluster_insert.add_css_class("linked");

        let tag_cluster_tools = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        tag_cluster_tools.add_css_class("linked");

        let html_scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .hexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .propagate_natural_width(false)
            .min_content_height(100)
            .min_content_width(150)
            .build();
        let html_text_view = gtk::TextView::builder()
            .monospace(true)
            .wrap_mode(gtk::WrapMode::WordChar)
            .left_margin(8)
            .right_margin(12)
            .top_margin(16)
            .bottom_margin(16)
            .css_classes(["code-editor-font"])
            .build();
        html_text_view.set_pixels_above_lines(0);
        html_text_view.set_pixels_below_lines(0);
        html_text_view.set_pixels_inside_wrap(0);
        html_scroll.set_child(Some(&html_text_view));

        let make_tag_btn = |lbl: &str, open: &str, close: &str, htv: &gtk::TextView| -> gtk::Button {
            let btn = gtk::Button::builder()
                .label(lbl)
                .css_classes(["flat", "toolbar-btn"])
                .build();
            let view = htv.clone();
            let o = open.to_string();
            let c = close.to_string();
            btn.connect_clicked(move |_| {
                insert_or_wrap_tag(&view, &o, &c);
            });
            btn
        };

        tag_cluster_inline.append(&make_tag_btn("<b>", "<b>", "</b>", &html_text_view));
        tag_cluster_inline.append(&make_tag_btn("<i>", "<i>", "</i>", &html_text_view));
        tag_cluster_inline.append(&make_tag_btn("<s>", "<s>", "</s>", &html_text_view));
        tag_cluster_inline.append(&make_tag_btn("<em>", "<em>", "</em>", &html_text_view));
        tag_cluster_inline.append(&make_tag_btn("<strong>", "<strong>", "</strong>", &html_text_view));

        tag_cluster_block.append(&make_tag_btn("<p>", "<p>", "</p>", &html_text_view));
        tag_cluster_block.append(&make_tag_btn("<blockquote>", "<blockquote>", "</blockquote>", &html_text_view));
        tag_cluster_block.append(&make_tag_btn("<span>", "<span class=\"\">", "</span>", &html_text_view));
        tag_cluster_block.append(&make_tag_btn("<div>", "<div class=\"\">\n", "\n</div>", &html_text_view));

        let html_image_btn = gtk::Button::builder()
            .label("🖼️")
            .tooltip_text("Insert Linked Image Embed (AO3 URL Attachment)")
            .css_classes(["flat", "toolbar-btn"])
            .build();
        {
            let htv = html_text_view.clone();
            let win = window.clone();
            html_image_btn.connect_clicked(move |_| {
                let htv_in = htv.clone();
                show_image_embed_dialog(&win, move |snippet| {
                    insert_or_wrap_tag(&htv_in, &snippet, "");
                });
            });
        }

        tag_cluster_insert.append(&make_tag_btn("<a>", "<a href=\"\">", "</a>", &html_text_view));
        tag_cluster_insert.append(&html_image_btn);
        tag_cluster_insert.append(&make_tag_btn("<hr>", "<hr/>\n", "", &html_text_view));
        tag_cluster_insert.append(&make_tag_btn("<details>", "<details>\n<summary>Spoiler / Note</summary>\n<p>", "</p>\n</details>", &html_text_view));

        let html_css_style_btn = gtk::MenuButton::builder()
            .label("🎨")
            .tooltip_text("Wrap selection in Work Skin CSS Class as <span> or <div>")
            .css_classes(["flat", "toolbar-btn"])
            .build();
        tag_cluster_tools.append(&html_css_style_btn);

        let format_btn = gtk::Button::builder()
            .label("Format HTML")
            .tooltip_text("Format HTML indentation and paragraph separation")
            .css_classes(["flat", "toolbar-btn"])
            .build();
        tag_cluster_tools.append(&format_btn);

        let tag_toolbar = gtk::Box::new(gtk::Orientation::Horizontal, 4);
        tag_toolbar.set_margin_top(2);
        tag_toolbar.set_margin_bottom(2);
        tag_toolbar.set_margin_start(2);
        tag_toolbar.set_margin_end(2);

        tag_toolbar.append(&tag_cluster_inline);
        tag_toolbar.append(&tag_cluster_block);
        tag_toolbar.append(&tag_cluster_insert);
        tag_toolbar.append(&tag_cluster_tools);

        let tag_scroll = gtk::ScrolledWindow::builder()
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .vscrollbar_policy(gtk::PolicyType::Never)
            .propagate_natural_width(false)
            .hexpand(true)
            .vexpand(false)
            .build();
        tag_scroll.set_child(Some(&tag_toolbar));

        // Horizontal mouse wheel scroll support for tag_scroll
        {
            let scroll_ctrl = gtk::EventControllerScroll::new(
                gtk::EventControllerScrollFlags::VERTICAL | gtk::EventControllerScrollFlags::HORIZONTAL,
            );
            let hadj = tag_scroll.hadjustment();
            scroll_ctrl.connect_scroll(move |_, dx, dy| {
                let delta = if dy.abs() > 0.0 { dy } else { dx };
                hadj.set_value(hadj.value() + delta * 24.0);
                glib::Propagation::Stop
            });
            tag_scroll.add_controller(scroll_ctrl);
        }

        // HTML Tag Extension Menu Button (Qt-Style Collapsed Options)
        let html_extension_btn = gtk::MenuButton::builder()
            .icon_name("view-more-symbolic")
            .tooltip_text("More HTML Tag Tools (Collapsed Options)")
            .css_classes(["flat", "toolbar-btn"])
            .valign(gtk::Align::Center)
            .build();

        let html_popover = gtk::Popover::new();
        let html_pop_box = gtk::Box::new(gtk::Orientation::Vertical, 4);
        html_pop_box.set_margin_top(8);
        html_pop_box.set_margin_bottom(8);
        html_pop_box.set_margin_start(8);
        html_pop_box.set_margin_end(8);
        html_pop_box.set_size_request(200, -1);

        let html_pop_lbl = gtk::Label::builder()
            .label("HTML Tag Tools")
            .xalign(0.0)
            .css_classes(["caption", "dim-label"])
            .build();
        html_pop_box.append(&html_pop_lbl);

        let make_html_pop_tag = |lbl: &str, open: &str, close: &str, htv: &gtk::TextView, pop: &gtk::Popover| -> gtk::Button {
            let btn = gtk::Button::builder()
                .label(lbl)
                .css_classes(["flat"])
                .build();
            if let Some(child) = btn.child() {
                child.set_halign(gtk::Align::Start);
            }
            let view = htv.clone();
            let o = open.to_string();
            let c = close.to_string();
            let p = pop.clone();
            btn.connect_clicked(move |_| {
                p.popdown();
                insert_or_wrap_tag(&view, &o, &c);
            });
            btn
        };

        html_pop_box.append(&make_html_pop_tag("<b> Bold", "<b>", "</b>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<i> Italic", "<i>", "</i>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<s> Strikethrough", "<s>", "</s>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<em> Emphasis", "<em>", "</em>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<strong> Strong", "<strong>", "</strong>", &html_text_view, &html_popover));
        html_pop_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        html_pop_box.append(&make_html_pop_tag("<p> Paragraph", "<p>", "</p>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<blockquote> Blockquote", "<blockquote>", "</blockquote>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<span> CSS Class Span", "<span class=\"\">", "</span>", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<div> CSS Class Div", "<div class=\"\">\n", "\n</div>", &html_text_view, &html_popover));
        html_pop_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        html_pop_box.append(&make_html_pop_tag("<a> Hyperlink", "<a href=\"\">", "</a>", &html_text_view, &html_popover));

        let pop_html_img = gtk::Button::builder()
            .label("🖼️ Image Embed…")
            .css_classes(["flat"])
            .build();
        if let Some(child) = pop_html_img.child() {
            child.set_halign(gtk::Align::Start);
        }
        {
            let htv = html_text_view.clone();
            let win = window.clone();
            let p = html_popover.clone();
            pop_html_img.connect_clicked(move |_| {
                p.popdown();
                let htv_in = htv.clone();
                show_image_embed_dialog(&win, move |snippet| {
                    insert_or_wrap_tag(&htv_in, &snippet, "");
                });
            });
        }
        html_pop_box.append(&pop_html_img);
        html_pop_box.append(&make_html_pop_tag("<hr> Horizontal Rule", "<hr/>\n", "", &html_text_view, &html_popover));
        html_pop_box.append(&make_html_pop_tag("<details> Spoiler", "<details>\n<summary>Spoiler / Note</summary>\n<p>", "</p>\n</details>", &html_text_view, &html_popover));

        html_popover.set_child(Some(&html_pop_box));
        html_extension_btn.set_popover(Some(&html_popover));

        let tag_toolbar_row = gtk::Box::new(gtk::Orientation::Horizontal, 2);
        tag_toolbar_row.set_margin_top(2);
        tag_toolbar_row.set_margin_bottom(2);
        tag_toolbar_row.set_margin_start(4);
        tag_toolbar_row.set_margin_end(4);

        tag_toolbar_row.append(&tag_scroll);
        tag_toolbar_row.append(&html_extension_btn);

        html_box.append(&tag_toolbar_row);

        let tag_breadcrumb_label = gtk::Label::builder()
            .label("🏷️ Tags: (root document)")
            .xalign(0.0)
            .margin_start(10)
            .margin_end(10)
            .margin_bottom(3)
            .css_classes(["dim-label", "caption"])
            .ellipsize(gtk4::pango::EllipsizeMode::End)
            .build();
        html_box.append(&tag_breadcrumb_label);
        html_box.append(&gtk::Separator::new(gtk::Orientation::Horizontal));

        // C. IDE Editor Grid: [ Line Numbers Gutter | Code Editor | Minimap ]
        let editor_grid = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        editor_grid.set_vexpand(true);
        editor_grid.set_hexpand(true);

        // Column 1: Line Numbers Gutter (Cairo DrawingArea — zero adjustment ping-pong, <0.05ms render)
        let line_gutter_area = gtk::DrawingArea::builder()
            .content_width(48)
            .content_height(100)
            .vexpand(true)
            .can_focus(false)
            .build();
        editor_grid.append(&line_gutter_area);

        let gutter_sep = gtk::Separator::new(gtk::Orientation::Vertical);
        editor_grid.append(&gutter_sep);

        // Column 2: Code Editor (html_scroll)
        editor_grid.append(&html_scroll);

        // Column 3: Minimap Overview Column
        let minimap_sep = gtk::Separator::new(gtk::Orientation::Vertical);
        editor_grid.append(&minimap_sep);

        let minimap_area = gtk::DrawingArea::builder()
            .content_width(55)
            .content_height(100)
            .vexpand(true)
            .build();
        editor_grid.append(&minimap_area);

        html_box.append(&editor_grid);
        let tab2_label = gtk::Label::new(Some("🏷️ Raw HTML"));
        notebook.append_page(&html_box, Some(&tab2_label));

        // Tab 3: Work Skin CSS Editor
        let css_box = gtk::Box::new(gtk::Orientation::Vertical, 0);
        let css_info = gtk::Label::builder()
            .label("AO3 Work Skin Stylesheet (e.g. .miku { color: #39c5bb; })")
            .xalign(0.0)
            .ellipsize(gtk4::pango::EllipsizeMode::End)
            .css_classes(["dim-label"])
            .margin_top(6)
            .margin_bottom(6)
            .margin_start(12)
            .margin_end(12)
            .build();
        css_box.append(&css_info);

        let css_scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .hexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .propagate_natural_width(false)
            .min_content_height(100)
            .min_content_width(150)
            .build();
        let css_text_view = gtk::TextView::builder()
            .monospace(true)
            .wrap_mode(gtk::WrapMode::WordChar)
            .left_margin(16)
            .right_margin(16)
            .top_margin(16)
            .bottom_margin(16)
            .css_classes(["code-editor-font"])
            .build();
        css_scroll.set_child(Some(&css_text_view));
        css_box.append(&css_scroll);
        let tab3_label = gtk::Label::new(Some("🎨 Work Skin CSS"));
        notebook.append_page(&css_box, Some(&tab3_label));

        // Wrap notebook in an Overlay with a Progress Loading Spinner Card
        let editor_overlay = gtk::Overlay::new();
        editor_overlay.set_vexpand(true);
        editor_overlay.set_hexpand(true);
        editor_overlay.set_child(Some(&notebook));

        let loading_box = gtk::Box::new(gtk::Orientation::Horizontal, 12);
        loading_box.set_halign(gtk::Align::Center);
        loading_box.set_valign(gtk::Align::Center);
        loading_box.add_css_class("loading-overlay-card");
        loading_box.set_visible(false);

        let loading_spinner = gtk::Spinner::new();
        loading_spinner.set_size_request(22, 22);
        loading_box.append(&loading_spinner);

        let loading_label = gtk::Label::builder()
            .label("Loading chapter & offloading inactive memory…")
            .css_classes(["heading"])
            .build();
        loading_box.append(&loading_label);

        editor_overlay.add_overlay(&loading_box);

        editor_split_paned.set_start_child(Some(&editor_overlay));

        // --- Sideview Box (IDE Live Preview side-by-side) ---
        let sideview_box = gtk::Box::new(gtk::Orientation::Vertical, 0);
        sideview_box.set_size_request(240, 100);
        sideview_box.set_vexpand(true);
        sideview_box.set_hexpand(true);
        sideview_box.add_css_class("background");

        let sideview_header = gtk::Box::new(gtk::Orientation::Horizontal, 6);
        sideview_header.set_margin_top(6);
        sideview_header.set_margin_bottom(6);
        sideview_header.set_margin_start(10);
        sideview_header.set_margin_end(10);

        let sv_icon = gtk::Image::from_icon_name("view-dual-symbolic");
        sideview_header.append(&sv_icon);

        let sv_title = gtk::Label::builder()
            .label("Live Preview")
            .css_classes(["heading", "bold"])
            .build();
        sideview_header.append(&sv_title);

        let skin_box = gtk::Box::new(gtk::Orientation::Horizontal, 2);
        skin_box.add_css_class("linked");
        skin_box.set_margin_start(10);

        let skin_default_btn = gtk::ToggleButton::builder()
            .label("Default")
            .tooltip_text("AO3 Default Skin")
            .active(true)
            .build();
        let skin_reversi_btn = gtk::ToggleButton::builder()
            .label("Reversi")
            .tooltip_text("AO3 Reversi Dark Skin")
            .group(&skin_default_btn)
            .build();
        skin_box.append(&skin_default_btn);
        skin_box.append(&skin_reversi_btn);
        sideview_header.append(&skin_box);

        let sv_spacer = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        sv_spacer.set_hexpand(true);
        sideview_header.append(&sv_spacer);

        let sv_refresh_btn = gtk::Button::builder()
            .icon_name("view-refresh-symbolic")
            .tooltip_text("Refresh Live Preview")
            .build();
        sideview_header.append(&sv_refresh_btn);

        let sv_close_btn = gtk::Button::builder()
            .icon_name("window-close-symbolic")
            .tooltip_text("Hide Sideview (Ctrl+P)")
            .build();
        sideview_header.append(&sv_close_btn);

        sideview_box.append(&sideview_header);
        let sv_sep = gtk::Separator::new(gtk::Orientation::Horizontal);
        sideview_box.append(&sv_sep);

        let sideview_scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .hexpand(true)
            .hscrollbar_policy(gtk::PolicyType::Automatic)
            .vscrollbar_policy(gtk::PolicyType::Automatic)
            .propagate_natural_height(false)
            .propagate_natural_width(false)
            .min_content_height(100)
            .min_content_width(180)
            .build();
        let sideview_web_view = webkit6::WebView::new();
        sideview_web_view.set_vexpand(true);
        sideview_web_view.set_hexpand(true);
        sideview_web_view.set_size_request(180, 100);
        sideview_scroll.set_child(Some(&sideview_web_view));
        sideview_box.append(&sideview_scroll);

        editor_split_paned.set_end_child(Some(&sideview_box));
        main_paned.set_end_child(Some(&editor_split_paned));
        editor_page_box.append(&main_paned);

        // --- Bottom Status Bar ---
        let status_box = gtk::Box::new(gtk::Orientation::Horizontal, 12);
        status_box.set_margin_top(4);
        status_box.set_margin_bottom(4);
        status_box.set_margin_start(12);
        status_box.set_margin_end(12);

        let status_label = gtk::Label::builder()
            .label("Words: 0 (Total: 0)")
            .xalign(0.0)
            .build();
        status_box.append(&status_label);

        let sep = gtk::Separator::new(gtk::Orientation::Vertical);
        status_box.append(&sep);

        let dirty_label = gtk::Label::builder().label("Saved").build();
        status_box.append(&dirty_label);

        let sep2 = gtk::Separator::new(gtk::Orientation::Vertical);
        status_box.append(&sep2);

        let line_col_label = gtk::Label::builder()
            .label("Ln 1, Col 1")
            .css_classes(["dim-label"])
            .build();
        status_box.append(&line_col_label);

        let plugin_badge_label = gtk::Label::builder()
            .label("")
            .css_classes(["accent", "caption"])
            .visible(false)
            .build();
        status_box.append(&plugin_badge_label);

        let spacer = gtk::Box::new(gtk::Orientation::Horizontal, 0);
        spacer.set_hexpand(true);
        status_box.append(&spacer);

        let mcp_label = gtk::Label::builder()
            .label("🟢 MCP: ws://127.0.0.1:26303")
            .css_classes(["dim-label"])
            .build();
        status_box.append(&mcp_label);

        editor_page_box.append(&status_box);
        view_stack.add_named(&editor_page_box, Some("editor"));

        // Line Numbers Gutter Drawing Function (renders only visible lines in <0.05ms)
        {
            let html_tv_gutter = html_text_view.clone();
            let scroll_gutter = html_scroll.clone();
            line_gutter_area.set_draw_func(move |_area, cr, width, height| {
                let is_dark = adw::StyleManager::default().is_dark();
                if is_dark {
                    cr.set_source_rgb(0.11, 0.11, 0.13);
                } else {
                    cr.set_source_rgb(0.96, 0.96, 0.97);
                }
                let _ = cr.paint();

                if is_dark {
                    cr.set_source_rgba(0.65, 0.68, 0.75, 0.78);
                } else {
                    cr.set_source_rgba(0.35, 0.38, 0.45, 0.78);
                }
                cr.select_font_face(
                    "monospace",
                    gtk::cairo::FontSlant::Normal,
                    gtk::cairo::FontWeight::Normal,
                );
                cr.set_font_size(12.0);

                let vadj = scroll_gutter.vadjustment();
                let top_y = vadj.value() as i32;
                let (mut iter, _) = html_tv_gutter.line_at_y(top_y);
                let top_margin = html_tv_gutter.top_margin();

                let mut drawn = 0usize;
                loop {
                    let (line_y, _line_h) = html_tv_gutter.line_yrange(&iter);
                    let screen_y = (line_y + top_margin - top_y) as f64;
                    if screen_y > height as f64 {
                        break;
                    }
                    if screen_y + 18.0 >= 0.0 {
                        let line_num = iter.line() + 1;
                        let s = format!("{}", line_num);
                        let x = (width as f64 - 8.0 - (s.len() as f64 * 7.2)).max(4.0);
                        cr.move_to(x, screen_y + 13.0);
                        let _ = cr.show_text(&s);
                    }
                    drawn += 1;
                    if drawn > 200 || !iter.forward_line() {
                        break;
                    }
                }
            });
        }

        // Minimap Drawing and Gestures
        let html_tv_mm = html_text_view.clone();
        let scroll_w_mm = html_scroll.clone();
        minimap_area.set_draw_func(move |_area, cr, width, height| {
            let is_dark = adw::StyleManager::default().is_dark();
            if is_dark {
                cr.set_source_rgb(0.09, 0.10, 0.12);
            } else {
                cr.set_source_rgb(0.96, 0.97, 0.98);
            }
            let _ = cr.paint();

            if is_dark {
                cr.set_source_rgb(0.18, 0.20, 0.24);
            } else {
                cr.set_source_rgb(0.85, 0.88, 0.92);
            }
            cr.set_line_width(1.0);
            cr.move_to(0.5, 0.0);
            cr.line_to(0.5, height as f64);
            let _ = cr.stroke();

            let buf = html_tv_mm.buffer();
            let line_count = buf.line_count().max(1) as usize;
            let line_spacing = ((height as f64 - 8.0) / line_count as f64).clamp(1.5, 6.0);
            let max_lines = ((height as f64 - 8.0) / line_spacing) as usize;

            if is_dark {
                cr.set_source_rgba(0.75, 0.80, 0.90, 0.35);
            } else {
                cr.set_source_rgba(0.25, 0.30, 0.40, 0.35);
            }

            let step = (line_count / max_lines.max(1)).max(1);
            for (drawn_idx, i) in (0..line_count).step_by(step).enumerate() {
                let y = 4.0 + (drawn_idx as f64 * line_spacing);
                if y >= height as f64 - 4.0 {
                    break;
                }
                if let Some(iter) = buf.iter_at_line(i as i32) {
                    let chars = iter.chars_in_line().min(80) as f64;
                    let bar_w = ((chars / 80.0) * (width as f64 - 16.0)).max(3.0);
                    let _ = cr.rectangle(6.0, y, bar_w, 1.2);
                }
            }
            let _ = cr.fill();

            // Highlight visible scroll viewport
            let adj = scroll_w_mm.vadjustment();
            let val = adj.value();
            let upper = adj.upper().max(1.0);
            let page_size = adj.page_size().max(1.0);

            let vp_y = (val / upper) * height as f64;
            let vp_h = ((page_size / upper) * height as f64).clamp(14.0, height as f64);

            cr.set_source_rgba(0.22, 0.77, 0.73, 0.22);
            let _ = cr.rectangle(2.0, vp_y, width as f64 - 4.0, vp_h);
            let _ = cr.fill();

            cr.set_source_rgba(0.22, 0.77, 0.73, 0.85);
            cr.set_line_width(1.5);
            let _ = cr.rectangle(2.0, vp_y, width as f64 - 4.0, vp_h);
            let _ = cr.stroke();
        });

        // Click gesture on minimap
        let click_gesture = gtk::GestureClick::new();
        {
            let html_scroll = html_scroll.clone();
            let minimap_area = minimap_area.clone();
            click_gesture.connect_pressed(move |_, _, _, y| {
                let adj = html_scroll.vadjustment();
                let height = minimap_area.height() as f64;
                if height > 0.0 {
                    let ratio = y / height;
                    let target = ratio * adj.upper() - (adj.page_size() / 2.0);
                    adj.set_value(target.clamp(0.0, (adj.upper() - adj.page_size()).max(0.0)));
                    minimap_area.queue_draw();
                }
            });
        }
        minimap_area.add_controller(click_gesture);

        let drag_gesture = gtk::GestureDrag::new();
        {
            let html_scroll = html_scroll.clone();
            let minimap_area = minimap_area.clone();
            drag_gesture.connect_drag_update(move |gesture, _x, y| {
                if let Some((_start_x, start_y)) = gesture.start_point() {
                    let current_y = start_y + y;
                    let adj = html_scroll.vadjustment();
                    let height = minimap_area.height() as f64;
                    if height > 0.0 {
                        let ratio = current_y / height;
                        let target = ratio * adj.upper() - (adj.page_size() / 2.0);
                        adj.set_value(target.clamp(0.0, (adj.upper() - adj.page_size()).max(0.0)));
                        minimap_area.queue_draw();
                    }
                }
            });
        }
        minimap_area.add_controller(drag_gesture);

        {
            let mm = minimap_area.clone();
            let lg = line_gutter_area.clone();
            html_scroll.vadjustment().connect_value_changed(move |_| {
                lg.queue_draw();
                mm.queue_draw();
            });
        }

        // ==========================================
        // 2. Main Menu Page
        // ==========================================
        let current_file_path = Rc::new(RefCell::new(None));
        let is_syncing = Rc::new(RefCell::new(false));
        let rich_editor_loaded = Rc::new(RefCell::new(false));

        let handles_ref: Rc<RefCell<Option<EnsembleWindowHandles>>> = Rc::new(RefCell::new(None));

        let sideview_reversi = Rc::new(RefCell::new(false));
        {
            let rev = sideview_reversi.clone();
            let h_ref = handles_ref.clone();
            skin_reversi_btn.connect_toggled(move |btn| {
                *rev.borrow_mut() = btn.is_active();
                if let Some(ref h) = *h_ref.borrow() {
                    h.refresh_preview();
                }
            });
        }

        {
            let h_ref = handles_ref.clone();
            sideview_btn.connect_toggled(move |btn| {
                if let Some(ref h) = *h_ref.borrow() {
                    let active = btn.is_active();
                    h.sideview_box.set_visible(active);
                    if active {
                        h.refresh_preview();
                    }
                }
            });
        }

        {
            let sv_btn = sideview_btn.clone();
            sv_close_btn.connect_clicked(move |_| {
                sv_btn.set_active(false);
            });
        }

        {
            let h_ref = handles_ref.clone();
            sv_refresh_btn.connect_clicked(move |_| {
                if let Some(ref h) = *h_ref.borrow() {
                    h.refresh_preview();
                }
            });
        }

        {
            let id_box = issue_details_box.clone();
            let td_btn = toggle_details_btn.clone();
            toggle_details_btn.connect_clicked(move |_| {
                let vis = id_box.is_visible();
                id_box.set_visible(!vis);
                if vis {
                    td_btn.set_label("Show Details ▾");
                } else {
                    td_btn.set_label("Hide Details ▴");
                }
            });
        }

        {
            let win = window.clone();
            let h_ref = handles_ref.clone();
            view_problems_win_btn.connect_clicked(move |_| {
                if let Some(ref h) = *h_ref.borrow() {
                    let ws = h.handler.workspace();
                    let g = ws.lock();
                    if let Some(ref proj) = g.project {
                        let active_idx = g.active_chapter_index.min(proj.chapters.len().saturating_sub(1));
                        if let Some(ch) = proj.chapters.get(active_idx) {
                            let report = HtmlLinter::lint_html(&ch.html, 1);
                            let h_clone = h.clone();
                            show_problems_dialog(&win, report.issues, move |line| {
                                h_clone.jump_to_html_line(line);
                            });
                        }
                    }
                }
            });
        }

        {
            let lcol = line_col_label.clone();
            let h_ref = handles_ref.clone();
            html_text_view.buffer().connect_notify_local(Some("cursor-position"), move |buf, _| {
                if let Some(ref h) = *h_ref.borrow() {
                    if *h.is_syncing.borrow() {
                        return;
                    }
                    let mark = buf.get_insert();
                    let iter = buf.iter_at_mark(&mark);
                    let ln = iter.line() + 1;
                    let col = iter.line_offset() + 1;
                    lcol.set_text(&format!("Ln {}, Col {}", ln, col));
                    h.update_tag_breadcrumbs();
                }
            });
        }

        {
            let h_ref = handles_ref.clone();
            rich_ucm.connect_script_message_received(Some("ensembleRichChanged"), move |_, val| {
                let html = val.to_str().to_string();
                if let Some(ref h) = *h_ref.borrow() {
                    h.on_rich_text_changed(html);
                }
            });
            rich_ucm.register_script_message_handler("ensembleRichChanged", None);
        }

        {
            let is_updating = is_updating_toolbar_state.clone();
            let b = bold_btn.clone();
            let it = italic_btn.clone();
            let u = underline_btn.clone();
            let s = strike_btn.clone();
            let h1 = h1_btn.clone();
            let h2 = h2_btn.clone();
            let h3 = h3_btn.clone();
            let p = p_btn.clone();
            let q = quote_btn.clone();
            let ul = ul_btn.clone();
            let ol = ol_btn.clone();

            rich_ucm.connect_script_message_received(Some("ensembleFormatChanged"), move |_, val| {
                let json_str = val.to_str().to_string();
                if let Ok(state) = serde_json::from_str::<FormatState>(&json_str) {
                    is_updating.set(true);
                    b.set_active(state.bold);
                    it.set_active(state.italic);
                    u.set_active(state.underline);
                    s.set_active(state.strike);
                    h1.set_active(state.h1);
                    h2.set_active(state.h2);
                    h3.set_active(state.h3);
                    p.set_active(state.p);
                    q.set_active(state.quote);
                    ul.set_active(state.ul);
                    ol.set_active(state.ol);
                    is_updating.set(false);
                }
            });
            rich_ucm.register_script_message_handler("ensembleFormatChanged", None);
        }

        // Redundant fallback channel via document.title notify so edits never get dropped
        {
            let h_ref = handles_ref.clone();
            rich_web_view.connect_title_notify(move |wv| {
                let Some(title) = wv.title() else {
                    return;
                };
                if !title.as_str().starts_with("ensemble-sync:") {
                    return;
                }
                let h_in = h_ref.clone();
                wv.evaluate_javascript(
                    "document.getElementById('editor') ? document.getElementById('editor').innerHTML : ''",
                    None,
                    None,
                    None::<&gio::Cancellable>,
                    move |res| {
                        if let Ok(val) = res {
                            let html = val.to_str().to_string();
                            if let Some(ref h) = *h_in.borrow() {
                                h.on_rich_text_changed(html);
                            }
                        }
                    },
                );
            });
        }

        // Right-click Context Menu in Rich Text Editor: Apply Work Skin CSS Style as <span> or <div>, Clear Styles, Insert Image
        {
            let h_ref = handles_ref.clone();
            let win_ctx = window.clone();
            rich_web_view.connect_context_menu(move |wv, context_menu, _hit_test| {
                let classes = {
                    if let Some(ref h) = *h_ref.borrow() {
                        let ws = h.handler.workspace();
                        let g = ws.lock();
                        if let Some(ref proj) = g.project {
                            let idx = g
                                .active_chapter_index
                                .min(proj.chapters.len().saturating_sub(1));
                            let ch_html = proj
                                .chapters
                                .get(idx)
                                .map(|c| c.html.as_str())
                                .unwrap_or("");
                            extract_work_skin_classes(&proj.work_skin_css, ch_html)
                        } else {
                            Vec::new()
                        }
                    } else {
                        Vec::new()
                    }
                };

                context_menu.append(&webkit6::ContextMenuItem::new_separator());

                // 1. Submenu: Apply CSS Style as <span> (Inline)
                let span_menu = webkit6::ContextMenu::new();
                for cls in &classes {
                    let act = gio::SimpleAction::new(&format!("span-{}", sanitize_action_id(cls)), None);
                    let wv_c = wv.clone();
                    let cls_c = cls.clone();
                    act.connect_activate(move |_, _| {
                        if let Ok(json_cls) = serde_json::to_string(&cls_c) {
                            exec_js(&wv_c, &format!("applyCssStyle({}, 'span');", json_cls));
                        }
                    });
                    let item = webkit6::ContextMenuItem::from_gaction(
                        &act,
                        &format!(".{}  (<span class=\"{}\">)", cls, cls),
                        None,
                    );
                    span_menu.append(&item);
                }
                if !classes.is_empty() {
                    span_menu.append(&webkit6::ContextMenuItem::new_separator());
                }
                {
                    let custom_span_act = gio::SimpleAction::new("custom-span-class", None);
                    let wv_c = wv.clone();
                    let win_c = win_ctx.clone();
                    custom_span_act.connect_activate(move |_, _| {
                        show_custom_css_style_dialog(&win_c, &wv_c, "span");
                    });
                    span_menu.append(&webkit6::ContextMenuItem::from_gaction(
                        &custom_span_act,
                        "Custom Class Name as <span>…",
                        None,
                    ));
                }
                context_menu.append(&webkit6::ContextMenuItem::with_submenu(
                    "🏷️ Apply CSS Style as <span> (Inline)",
                    &span_menu,
                ));

                // 2. Submenu: Apply CSS Style as <div> (Block / Card)
                let div_menu = webkit6::ContextMenu::new();
                for cls in &classes {
                    let act = gio::SimpleAction::new(&format!("div-{}", sanitize_action_id(cls)), None);
                    let wv_c = wv.clone();
                    let cls_c = cls.clone();
                    act.connect_activate(move |_, _| {
                        if let Ok(json_cls) = serde_json::to_string(&cls_c) {
                            exec_js(&wv_c, &format!("applyCssStyle({}, 'div');", json_cls));
                        }
                    });
                    let item = webkit6::ContextMenuItem::from_gaction(
                        &act,
                        &format!(".{}  (<div class=\"{}\">)", cls, cls),
                        None,
                    );
                    div_menu.append(&item);
                }
                if !classes.is_empty() {
                    div_menu.append(&webkit6::ContextMenuItem::new_separator());
                }
                {
                    let custom_div_act = gio::SimpleAction::new("custom-div-class", None);
                    let wv_c = wv.clone();
                    let win_c = win_ctx.clone();
                    custom_div_act.connect_activate(move |_, _| {
                        show_custom_css_style_dialog(&win_c, &wv_c, "div");
                    });
                    div_menu.append(&webkit6::ContextMenuItem::from_gaction(
                        &custom_div_act,
                        "Custom Class Name as <div>…",
                        None,
                    ));
                }
                context_menu.append(&webkit6::ContextMenuItem::with_submenu(
                    "📦 Apply CSS Style as <div> (Block / Card)",
                    &div_menu,
                ));

                // 3. Clear Work Skin CSS Style
                {
                    let clear_act = gio::SimpleAction::new("clear-workskin-css", None);
                    let wv_c = wv.clone();
                    clear_act.connect_activate(move |_, _| {
                        exec_js(&wv_c, "clearCssStyle();");
                    });
                    context_menu.append(&webkit6::ContextMenuItem::from_gaction(
                        &clear_act,
                        "🧹 Clear Work Skin CSS Style (Unwrap div/span)",
                        None,
                    ));
                }

                // 4. Insert Linked Image (<img>)
                {
                    let img_act = gio::SimpleAction::new("insert-linked-img", None);
                    let wv_c = wv.clone();
                    let win_c = win_ctx.clone();
                    img_act.connect_activate(move |_, _| {
                        let wv_in = wv_c.clone();
                        show_image_embed_dialog(&win_c, move |snippet| {
                            if let Ok(json_html) = serde_json::to_string(&snippet) {
                                exec_js(&wv_in, &format!("insertHtmlSnippet({});", json_html));
                            }
                        });
                    });
                    context_menu.append(&webkit6::ContextMenuItem::from_gaction(
                        &img_act,
                        "🖼️ Insert Linked Image (<img>)…",
                        None,
                    ));
                }

                false
            });
        }

        // Toolbar CSS Style Popover for Rich Text & Raw HTML
        attach_rich_css_style_popover(&rich_css_style_btn, &rich_web_view, handles_ref.clone());
        attach_html_css_style_popover(&html_css_style_btn, &html_text_view, handles_ref.clone());

        {
            let lcol = line_col_label.clone();
            css_text_view.buffer().connect_notify_local(Some("cursor-position"), move |buf, _| {
                let mark = buf.get_insert();
                let iter = buf.iter_at_mark(&mark);
                let ln = iter.line() + 1;
                let col = iter.line_offset() + 1;
                lcol.set_text(&format!("Ln {}, Col {}", ln, col));
            });
        }

        let plugin_host = PluginUiHost::new(
            &window,
            handler.clone(),
            {
                let h_ref = handles_ref.clone();
                move || {
                    if let Some(ref h) = *h_ref.borrow() {
                        h.sync_from_workspace();
                    }
                }
            },
            {
                let toast_ov = toast_overlay.clone();
                let badge_lbl = plugin_badge_label.clone();
                move |badge_opt, toast_opt| {
                    if let Some(badge) = badge_opt {
                        if badge.trim().is_empty() {
                            badge_lbl.set_visible(false);
                            badge_lbl.set_text("");
                        } else {
                            badge_lbl.set_text(&format!("🧩 {}", badge));
                            badge_lbl.set_visible(true);
                        }
                    }
                    if let Some(toast_msg) = toast_opt {
                        toast_ov.add_toast(adw::Toast::new(&toast_msg));
                    }
                }
            },
        );

        let main_menu_view = MainMenuView::new(
            {
                let h_ref = handles_ref.clone();
                move || {
                    if let Some(ref h) = *h_ref.borrow() {
                        let h: &EnsembleWindowHandles = h;
                        h.new_project();
                    }
                }
            },
            {
                let h_ref = handles_ref.clone();
                move || {
                    if let Some(ref h) = *h_ref.borrow() {
                        let h: &EnsembleWindowHandles = h;
                        h.open_project_dialog();
                    }
                }
            },
            {
                let win = window.clone();
                move || {
                    show_preferences_dialog(&win);
                }
            },
            {
                let h_ref = handles_ref.clone();
                move || {
                    if let Some(ref h) = *h_ref.borrow() {
                        h.open_plugin_manager();
                    }
                }
            },
            {
                let win = window.clone();
                move || {
                    show_plugin_marketplace_dialog(&win);
                }
            },
        );

        view_stack.add_named(&main_menu_view.root, Some("main_menu"));
        view_stack.set_visible_child_name("main_menu");

        toast_overlay.set_child(Some(&view_stack));
        root_box.append(&toast_overlay);
        window.set_content(Some(&root_box));

        let app_win = Rc::new(Self {
            window,
            handler,
            plugin_host,
            toast_overlay,
            window_title,
            problems_btn,
            status_label,
            dirty_label,
            plugin_badge_label,
            mcp_label,
            open_btn,
            save_btn,
            copy_html_btn,
            home_btn,
            view_stack,
            notebook,
            main_menu_view,
            chapter_list_box,
            rich_web_view,
            rich_editor_loaded,
            html_text_view,
            css_text_view,
            line_gutter_area,
            loading_box,
            loading_spinner,
            loading_label,
            current_file_path,
            is_syncing,
            sideview_btn,
            sideview_box,
            sideview_web_view,
            sideview_reversi,
            minimap_area,
            line_col_label,
            tag_breadcrumb_label,
            issue_summary_label,
            issue_icon,
            toggle_details_btn,
            issue_details_box,
        });

        *handles_ref.borrow_mut() = Some(app_win.clone_handles());

        // Intercept close requests to warn about unsaved changes
        let is_closing = Rc::new(RefCell::new(false));
        {
            let handles_ref_close = handles_ref.clone();
            let is_closing_clone = is_closing.clone();
            let win_weak = app_win.window.downgrade();
            app_win.window.connect_close_request(move |_| {
                if *is_closing_clone.borrow() {
                    return gtk4::glib::Propagation::Proceed;
                }

                if let Some(ref h) = *handles_ref_close.borrow() {
                    let is_dirty = {
                        let ws = h.handler.workspace();
                        let g = ws.lock();
                        g.project.as_ref().map(|p| p.dirty).unwrap_or(false)
                    };

                    if is_dirty {
                        let is_closing_action = is_closing_clone.clone();
                        let win_w = win_weak.clone();
                        h.confirm_unsaved_changes("closing Ensemble", move || {
                            *is_closing_action.borrow_mut() = true;
                            if let Some(win) = win_w.upgrade() {
                                win.close();
                            }
                        });
                        return gtk4::glib::Propagation::Stop;
                    }
                }

                gtk4::glib::Propagation::Proceed
            });
        }

        // Initialize actions & connections
        app_win.setup_actions(app);
        app_win.setup_signals(
            &prefs_btn,
            &task_mgr_btn,
            &plugins_btn,
            &add_chapter_btn,
            &del_chapter_btn,
            &manage_chapter_btn,
            &format_btn,
        );

        app_win
    }

    fn setup_actions(&self, app: &adw::Application) {
        let win = self.window.clone();

        // Welcome / Home
        let action_home = gio::ActionEntry::builder("home")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.show_main_menu();
                }
            })
            .build();

        // New Project
        let action_new = gio::ActionEntry::builder("new")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.new_project();
                }
            })
            .build();

        // Open Project
        let action_open = gio::ActionEntry::builder("open")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.open_project_dialog();
                }
            })
            .build();

        // Save Project
        let action_save = gio::ActionEntry::builder("save")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.save_project();
                }
            })
            .build();

        // Save Project As
        let action_save_as = gio::ActionEntry::builder("save_as")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.save_project_as();
                }
            })
            .build();

        // Toggle Live Preview (Ctrl+P)
        let action_preview = gio::ActionEntry::builder("preview")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.toggle_sideview();
                }
            })
            .build();

        // Task Manager (Ctrl+Shift+M)
        let action_task_mgr = gio::ActionEntry::builder("task_manager")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.open_task_manager();
                }
            })
            .build();

        // Preferences
        let action_prefs = gio::ActionEntry::builder("preferences")
            .activate({
                let win_clone = win.clone();
                move |_, _, _| {
                    show_preferences_dialog(&win_clone);
                }
            })
            .build();

        // Plugins
        let action_plugins = gio::ActionEntry::builder("plugins")
            .activate({
                let h = self.clone_handles();
                move |_, _, _| {
                    h.open_plugin_manager();
                }
            })
            .build();

        // Standalone Plugin Marketplace (WIP)
        let action_marketplace = gio::ActionEntry::builder("marketplace")
            .activate({
                let win_clone = win.clone();
                move |_, _, _| {
                    show_plugin_marketplace_dialog(&win_clone);
                }
            })
            .build();

        // Properties
        let action_props = gio::ActionEntry::builder("properties")
            .activate({
                let win_clone = win.clone();
                let h = self.clone_handles();
                move |_, _, _| {
                    let handler = h.handler.clone();
                    let h_clone = h.clone();
                    show_project_properties_dialog(&win_clone, handler, move || {
                        h_clone.sync_from_workspace();
                    });
                }
            })
            .build();

        // Chapter Manager
        let action_chap = gio::ActionEntry::builder("chapter_manager")
            .activate({
                let win_clone = win.clone();
                let h = self.clone_handles();
                move |_, _, _| {
                    let handler = h.handler.clone();
                    let h_clone = h.clone();
                    show_chapter_manager_dialog(&win_clone, handler, move || {
                        h_clone.sync_from_workspace();
                    });
                }
            })
            .build();

        // Help
        let action_help = gio::ActionEntry::builder("help")
            .activate({
                let win_clone = win.clone();
                move |_, _, _| {
                    show_help_guide_dialog(&win_clone);
                }
            })
            .build();

        // About
        let action_about = gio::ActionEntry::builder("about")
            .activate({
                let win_clone = win.clone();
                move |_, _, _| {
                    show_about_dialog(&win_clone);
                }
            })
            .build();

        // Theme switches
        let action_sys = gio::ActionEntry::builder("theme_system")
            .activate(|_, _, _| {
                adw::StyleManager::default().set_color_scheme(adw::ColorScheme::Default);
            })
            .build();

        let action_light = gio::ActionEntry::builder("theme_light")
            .activate(|_, _, _| {
                adw::StyleManager::default().set_color_scheme(adw::ColorScheme::ForceLight);
            })
            .build();

        let action_dark = gio::ActionEntry::builder("theme_dark")
            .activate(|_, _, _| {
                adw::StyleManager::default().set_color_scheme(adw::ColorScheme::ForceDark);
            })
            .build();

        app.add_action_entries([
            action_home,
            action_new,
            action_open,
            action_save,
            action_save_as,
            action_preview,
            action_task_mgr,
            action_prefs,
            action_plugins,
            action_marketplace,
            action_props,
            action_chap,
            action_help,
            action_about,
            action_sys,
            action_light,
            action_dark,
        ]);

        app.set_accels_for_action("app.new", &["<Ctrl>N"]);
        app.set_accels_for_action("app.open", &["<Ctrl>O"]);
        app.set_accels_for_action("app.save", &["<Ctrl>S"]);
        app.set_accels_for_action("app.save_as", &["<Ctrl><Shift>S"]);
        app.set_accels_for_action("app.preview", &["<Ctrl>P"]);
        app.set_accels_for_action("app.task_manager", &["<Ctrl><Shift>M"]);
        app.set_accels_for_action("app.preferences", &["<Ctrl>comma"]);
        app.set_accels_for_action("app.help", &["F1"]);
    }

    fn setup_signals(
        &self,
        prefs_btn: &gtk::Button,
        task_mgr_btn: &gtk::Button,
        plugins_btn: &gtk::Button,
        add_chapter_btn: &gtk::Button,
        del_chapter_btn: &gtk::Button,
        manage_chapter_btn: &gtk::Button,
        format_btn: &gtk::Button,
    ) {
        let handles = self.clone_handles();

        // Home Button
        {
            let h = handles.clone();
            self.home_btn.connect_clicked(move |_| {
                h.show_main_menu();
            });
        }

        // Open Button
        {
            let h = handles.clone();
            self.open_btn.connect_clicked(move |_| {
                h.open_project_dialog();
            });
        }

        // Save Button
        {
            let h = handles.clone();
            self.save_btn.connect_clicked(move |_| {
                h.save_project();
            });
        }

        // Prefs Button
        {
            let win = self.window.clone();
            prefs_btn.connect_clicked(move |_| {
                show_preferences_dialog(&win);
            });
        }

        // Task Manager Button
        {
            let h = handles.clone();
            task_mgr_btn.connect_clicked(move |_| {
                h.open_task_manager();
            });
        }

        // Plugins Button
        {
            let h = handles.clone();
            plugins_btn.connect_clicked(move |_| {
                h.open_plugin_manager();
            });
        }

        // Copy HTML Button
        {
            let h = handles.clone();
            self.copy_html_btn.connect_clicked(move |_| {
                h.copy_active_chapter_html();
            });
        }

        // Add Chapter Button
        {
            let h = handles.clone();
            add_chapter_btn.connect_clicked(move |_| {
                h.add_new_chapter();
            });
        }

        // Delete Chapter Button
        {
            let h = handles.clone();
            del_chapter_btn.connect_clicked(move |_| {
                h.delete_active_chapter();
            });
        }

        // Manage Chapters Button
        {
            let h = handles.clone();
            let win = self.window.clone();
            manage_chapter_btn.connect_clicked(move |_| {
                let handler = h.handler.clone();
                let h_clone = h.clone();
                show_chapter_manager_dialog(&win, handler, move || {
                    h_clone.sync_from_workspace();
                });
            });
        }

        // Format HTML Button
        {
            let h = handles.clone();
            format_btn.connect_clicked(move |_| {
                h.format_active_chapter_html();
            });
        }

        // Problems Button
        {
            let h = handles.clone();
            let win = self.window.clone();
            self.problems_btn.connect_clicked(move |_| {
                let ws = h.handler.workspace();
                let g = ws.lock();
                if let Some(ref proj) = g.project {
                    let active_idx = g.active_chapter_index.min(proj.chapters.len().saturating_sub(1));
                    if let Some(ch) = proj.chapters.get(active_idx) {
                        let report = HtmlLinter::lint_html(&ch.html, 1);
                        let h_clone = h.clone();
                        show_problems_dialog(&win, report.issues, move |line| {
                            h_clone.jump_to_html_line(line);
                        });
                    }
                }
            });
        }

        // Chapter List Selection
        {
            let h = handles.clone();
            self.chapter_list_box.connect_row_selected(move |_, row| {
                if *h.is_syncing.borrow() {
                    return;
                }
                if let Some(r) = row {
                    let idx = r.index() as usize;
                    h.switch_active_chapter(idx);
                }
            });
        }

        {
            let h = handles.clone();
            self.html_text_view
                .buffer()
                .connect_changed(move |buffer| {
                    if *h.is_syncing.borrow() {
                        return;
                    }
                    let (start, end) = buffer.bounds();
                    let text = buffer.text(&start, &end, false).to_string();
                    h.on_html_text_changed(text);
                });
        }

        {
            let h = handles.clone();
            self.css_text_view
                .buffer()
                .connect_changed(move |buffer| {
                    if *h.is_syncing.borrow() {
                        return;
                    }
                    let (start, end) = buffer.bounds();
                    let text = buffer.text(&start, &end, false).to_string();
                    h.on_css_text_changed(text);
                });
        }

        // Notebook switch page: unload inactive tab buffer and load only the selected tab with spinner
        {
            let h = handles.clone();
            self.notebook.connect_switch_page(move |_, _, page_num| {
                if *h.is_syncing.borrow() {
                    return;
                }
                h.switch_editor_tab_with_offload(page_num);
            });
        }
    }

    fn clone_handles(&self) -> EnsembleWindowHandles {
        EnsembleWindowHandles {
            window: self.window.clone(),
            handler: self.handler.clone(),
            plugin_host: self.plugin_host.clone(),
            toast_overlay: self.toast_overlay.clone(),
            window_title: self.window_title.clone(),
            problems_btn: self.problems_btn.clone(),
            status_label: self.status_label.clone(),
            dirty_label: self.dirty_label.clone(),
            plugin_badge_label: self.plugin_badge_label.clone(),
            mcp_label: self.mcp_label.clone(),
            open_btn: self.open_btn.clone(),
            save_btn: self.save_btn.clone(),
            copy_html_btn: self.copy_html_btn.clone(),
            home_btn: self.home_btn.clone(),
            view_stack: self.view_stack.clone(),
            notebook: self.notebook.clone(),
            main_menu_view: self.main_menu_view.clone(),
            chapter_list_box: self.chapter_list_box.clone(),
            rich_web_view: self.rich_web_view.clone(),
            rich_editor_loaded: self.rich_editor_loaded.clone(),
            html_text_view: self.html_text_view.clone(),
            css_text_view: self.css_text_view.clone(),
            line_gutter_area: self.line_gutter_area.clone(),
            loading_box: self.loading_box.clone(),
            loading_spinner: self.loading_spinner.clone(),
            loading_label: self.loading_label.clone(),
            current_file_path: self.current_file_path.clone(),
            is_syncing: self.is_syncing.clone(),
            sideview_btn: self.sideview_btn.clone(),
            sideview_box: self.sideview_box.clone(),
            sideview_web_view: self.sideview_web_view.clone(),
            sideview_reversi: self.sideview_reversi.clone(),
            minimap_area: self.minimap_area.clone(),
            line_col_label: self.line_col_label.clone(),
            tag_breadcrumb_label: self.tag_breadcrumb_label.clone(),
            issue_summary_label: self.issue_summary_label.clone(),
            issue_icon: self.issue_icon.clone(),
            toggle_details_btn: self.toggle_details_btn.clone(),
            issue_details_box: self.issue_details_box.clone(),
        }
    }

    pub fn open_file(&self, path: PathBuf) {
        let handles = self.clone_handles();
        let h = handles.clone();
        handles.confirm_unsaved_changes("opening another project", move || {
            h.load_project_file(path);
        });
    }

    pub fn sync_from_workspace(&self) {
        let handles = self.clone_handles();
        handles.sync_from_workspace();
    }
}

// Helpers for tag breadcrumbs and insertion
fn extract_tag_path(text: &str) -> Vec<String> {
    let mut stack = Vec::new();
    let void_tags = ["hr", "br", "img", "input", "col", "meta", "link"];
    let mut tag_buf = String::new();
    let mut in_tag = false;

    for ch in text.chars() {
        if ch == '<' {
            in_tag = true;
            tag_buf.clear();
        } else if ch == '>' && in_tag {
            in_tag = false;
            let trimmed = tag_buf.trim();
            if trimmed.starts_with('/') {
                let name = trimmed[1..].split_whitespace().next().unwrap_or("").to_lowercase();
                let clean_name = format!("<{}>", name);
                if let Some(pos) = stack.iter().rposition(|x: &String| x == &clean_name) {
                    stack.truncate(pos);
                }
            } else if !trimmed.ends_with('/') {
                let name = trimmed.split_whitespace().next().unwrap_or("").to_lowercase();
                let clean = name.trim_end_matches('/');
                if !clean.is_empty() && !void_tags.contains(&clean) {
                    stack.push(format!("<{}>", clean));
                }
            }
            tag_buf.clear();
        } else if in_tag {
            tag_buf.push(ch);
        }
    }
    stack
}

fn ensure_readable_html_lines(html: &str) -> String {
    if !html.contains("><") {
        return html.to_string();
    }
    html.replace("</p><p", "</p>\n<p")
        .replace("</p><blockquote", "</p>\n<blockquote")
        .replace("</blockquote><p", "</blockquote>\n<p")
        .replace("</p><hr", "</p>\n<hr")
        .replace("<hr><p", "<hr>\n<p")
        .replace("<hr/><p", "<hr/>\n<p")
        .replace("<hr /><p", "<hr />\n<p")
        .replace("</h1><p", "</h1>\n<p")
        .replace("</h2><p", "</h2>\n<p")
        .replace("</h3><p", "</h3>\n<p")
}

fn insert_or_wrap_tag(view: &gtk::TextView, open: &str, close: &str) {
    let buffer = view.buffer();
    if let Some((start, end)) = buffer.selection_bounds() {
        let text = buffer.text(&start, &end, false);
        buffer.delete(&mut start.clone(), &mut end.clone());
        let mut insert_iter = buffer.iter_at_offset(start.offset());
        let replacement = format!("{}{}{}", open, text, close);
        buffer.insert(&mut insert_iter, &replacement);
    } else {
        buffer.insert_at_cursor(&format!("{}{}", open, close));
    }
}

#[derive(Clone)]
#[allow(dead_code)]
struct EnsembleWindowHandles {
    window: adw::ApplicationWindow,
    handler: Arc<McpHandler>,
    plugin_host: PluginUiHost,
    toast_overlay: adw::ToastOverlay,
    window_title: adw::WindowTitle,
    problems_btn: gtk::Button,
    status_label: gtk::Label,
    dirty_label: gtk::Label,
    plugin_badge_label: gtk::Label,
    mcp_label: gtk::Label,
    open_btn: gtk::Button,
    save_btn: gtk::Button,
    copy_html_btn: gtk::Button,
    home_btn: gtk::Button,
    view_stack: gtk::Stack,
    notebook: gtk::Notebook,
    main_menu_view: Rc<MainMenuView>,
    chapter_list_box: gtk::ListBox,
    rich_web_view: webkit6::WebView,
    rich_editor_loaded: Rc<RefCell<bool>>,
    html_text_view: gtk::TextView,
    css_text_view: gtk::TextView,
    line_gutter_area: gtk::DrawingArea,
    loading_box: gtk::Box,
    loading_spinner: gtk::Spinner,
    loading_label: gtk::Label,
    current_file_path: Rc<RefCell<Option<PathBuf>>>,
    is_syncing: Rc<RefCell<bool>>,
    sideview_btn: gtk::ToggleButton,
    sideview_box: gtk::Box,
    sideview_web_view: webkit6::WebView,
    sideview_reversi: Rc<RefCell<bool>>,
    minimap_area: gtk::DrawingArea,
    line_col_label: gtk::Label,
    tag_breadcrumb_label: gtk::Label,
    issue_summary_label: gtk::Label,
    issue_icon: gtk::Image,
    toggle_details_btn: gtk::Button,
    issue_details_box: gtk::Box,
}

impl EnsembleWindowHandles {
    fn open_plugin_manager(&self) {
        show_plugin_manager_with_host(&self.window, self.plugin_host.clone());
    }

    fn show_loading_spinner(&self, message: &str) {
        self.loading_label.set_text(message);
        self.loading_box.set_visible(true);
        self.loading_spinner.start();
    }

    fn hide_loading_spinner(&self) {
        self.loading_spinner.stop();
        self.loading_box.set_visible(false);
    }

    fn unload_inactive_tabs(&self, active_page: u32) {
        *self.is_syncing.borrow_mut() = true;
        if active_page != 0 && *self.rich_editor_loaded.borrow() {
            let is_dark = adw::StyleManager::default().is_dark();
            let bg = if is_dark { "#1e1e1e" } else { "#ffffff" };
            let blank = format!("<!DOCTYPE html><html><body style='background:{};margin:0;'></body></html>", bg);
            self.rich_web_view.load_html(&blank, None);
            *self.rich_editor_loaded.borrow_mut() = false;
        }
        if active_page != 1 {
            self.html_text_view.buffer().set_text("");
        }
        if active_page != 2 {
            self.css_text_view.buffer().set_text("");
        }
        *self.is_syncing.borrow_mut() = false;
    }

    fn open_task_manager(&self) {
        let h_tel = self.clone();
        let h_offload = self.clone();
        show_task_manager_dialog(
            &self.window,
            self.handler.clone(),
            move || {
                let active_tab = h_tel.notebook.current_page().unwrap_or(0);
                let rich_loaded = *h_tel.rich_editor_loaded.borrow();
                let html_buf = h_tel.html_text_view.buffer();
                let html_bytes = html_buf.char_count().max(0) as usize;
                let html_lines = html_buf.line_count().max(1);
                let css_bytes = h_tel.css_text_view.buffer().char_count().max(0) as usize;
                let sideview_visible = h_tel.sideview_box.is_visible();
                UiComponentTelemetry {
                    active_tab,
                    rich_editor_loaded: rich_loaded,
                    html_editor_bytes: html_bytes,
                    html_editor_lines: html_lines,
                    css_editor_bytes: css_bytes,
                    sideview_visible,
                }
            },
            move || {
                let ws = h_offload.handler.workspace();
                let mut g = ws.lock();
                let active_idx = g.active_chapter_index;
                if let Some(ref mut proj) = g.project {
                    proj.active_chapter_index = active_idx;
                    proj.offload_inactive_chapters();
                }
                drop(g);
                let cur_page = h_offload.notebook.current_page().unwrap_or(0);
                h_offload.unload_inactive_tabs(cur_page);
                h_offload.toast_overlay.add_toast(adw::Toast::new(
                    "Offloaded inactive chapters & unused editor buffers to disk cache",
                ));
            },
        );
    }

    fn show_main_menu(&self) {
        let h = self.clone();
        self.confirm_unsaved_changes("returning to the main menu", move || {
            h.show_main_menu_internal();
        });
    }

    fn show_main_menu_internal(&self) {
        self.unload_inactive_tabs(99);
        self.main_menu_view.refresh_greeting();
        self.view_stack.set_visible_child_name("main_menu");
        self.save_btn.set_sensitive(false);
        self.copy_html_btn.set_sensitive(false);
        self.problems_btn.set_sensitive(false);
        self.sideview_btn.set_sensitive(false);
    }

    fn toggle_sideview(&self) {
        if self.view_stack.visible_child_name().as_deref() != Some("editor") {
            self.view_stack.set_visible_child_name("editor");
        }
        let is_vis = self.sideview_box.is_visible();
        self.sideview_box.set_visible(!is_vis);
        self.sideview_btn.set_active(!is_vis);
        if !is_vis {
            self.refresh_preview();
        } else {
            self.sideview_web_view
                .load_html("<!DOCTYPE html><html><body></body></html>", None);
        }
    }

    fn new_project(&self) {
        let h = self.clone();
        self.confirm_unsaved_changes("creating a new project", move || {
            h.new_project_internal();
        });
    }

    fn new_project_internal(&self) {
        let mut proj = Ao3Project::new();
        proj.title = "Untitled Project".to_string();
        if let Some(ch) = proj.chapters.first_mut() {
            ch.title = "Chapter 1".to_string();
            ch.set_html_modified("<p>Start writing your story here…</p>".to_string());
            ch.dirty_chapter = false;
        } else {
            proj.add_chapter("Chapter 1");
            if let Some(ch) = proj.chapters.first_mut() {
                ch.set_html_modified("<p>Start writing your story here…</p>".to_string());
                ch.dirty_chapter = false;
            }
        }
        proj.dirty = false;
        proj.offload_inactive_chapters();

        let ws = self.handler.workspace();
        let mut g = ws.lock();
        g.project = Some(proj);
        g.active_chapter_index = 0;
        drop(g);

        *self.current_file_path.borrow_mut() = None;
        self.view_stack.set_visible_child_name("editor");
        self.save_btn.set_sensitive(true);
        self.copy_html_btn.set_sensitive(true);
        self.problems_btn.set_sensitive(true);
        self.sideview_btn.set_sensitive(true);
        self.sync_from_workspace();
        self.toast_overlay
            .add_toast(adw::Toast::new("Created new project"));
    }

    fn open_project_dialog(&self) {
        let h = self.clone();
        self.confirm_unsaved_changes("opening another project", move || {
            h.open_project_dialog_internal();
        });
    }

    fn open_project_dialog_internal(&self) {
        let dialog = gtk::FileDialog::builder()
            .title("Open AO3 Project")
            .modal(true)
            .build();

        let filter_ao3 = gtk::FileFilter::new();
        filter_ao3.set_name(Some("AO3 Project Archive (*.ao3proj)"));
        filter_ao3.add_pattern("*.ao3proj");

        let filter_all = gtk::FileFilter::new();
        filter_all.set_name(Some("All Files"));
        filter_all.add_pattern("*");

        let filters = gio::ListStore::new::<gtk::FileFilter>();
        filters.append(&filter_ao3);
        filters.append(&filter_all);
        dialog.set_filters(Some(&filters));
        dialog.set_default_filter(Some(&filter_ao3));

        let parent = self.window.clone();
        let self_clone = self.clone();

        dialog.open(Some(&parent), gio::Cancellable::NONE, move |result| {
            if let Ok(file) = result {
                if let Some(path) = file.path() {
                    self_clone.load_project_file(path);
                }
            }
        });
    }

    fn load_project_file(&self, path: PathBuf) {
        self.view_stack.set_visible_child_name("editor");
        self.show_loading_spinner("Loading project & offloading inactive chapters…");

        let h = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(20), move || {
            let mut proj = Ao3Project::new();
            let report = ProjectSerializer::load_with_validation(&path, &mut proj, None);
            if report.success {
                proj.dirty = false;
                proj.active_chapter_index = 0;
                proj.mark_all_chapters_saved();
                proj.offload_inactive_chapters();

                let ws = h.handler.workspace();
                let mut g = ws.lock();
                g.project = Some(proj);
                g.active_chapter_index = 0;
                drop(g);

                *h.current_file_path.borrow_mut() = Some(path.clone());
                h.save_btn.set_sensitive(true);
                h.copy_html_btn.set_sensitive(true);
                h.problems_btn.set_sensitive(true);
                h.sideview_btn.set_sensitive(true);
                h.sync_from_workspace();
                h.hide_loading_spinner();
                let msg = format!(
                    "Opened project: {}",
                    path.file_name().unwrap_or_default().to_string_lossy()
                );
                h.toast_overlay.add_toast(adw::Toast::new(&msg));
            } else {
                h.hide_loading_spinner();
                let err_msg = format!("Failed to open project: {}", report.summary_error);
                h.toast_overlay.add_toast(adw::Toast::new(&err_msg));
            }
        });
    }

    fn save_project(&self) {
        let path_opt = self.current_file_path.borrow().clone();
        if let Some(path) = path_opt {
            self.save_to_path(path);
        } else {
            self.save_project_as();
        }
    }

    fn save_project_as(&self) {
        self.save_project_as_with_callback(|_| {});
    }

    fn save_project_as_with_callback<F: FnOnce(bool) + 'static>(&self, callback: F) {
        let dialog = gtk::FileDialog::builder()
            .title("Save AO3 Project")
            .modal(true)
            .initial_name("project.ao3proj")
            .build();

        let filter_ao3 = gtk::FileFilter::new();
        filter_ao3.set_name(Some("AO3 Project Archive (*.ao3proj)"));
        filter_ao3.add_pattern("*.ao3proj");

        let filters = gio::ListStore::new::<gtk::FileFilter>();
        filters.append(&filter_ao3);
        dialog.set_filters(Some(&filters));

        let parent = self.window.clone();
        let self_clone = self.clone();

        dialog.save(Some(&parent), gio::Cancellable::NONE, move |result| {
            if let Ok(file) = result {
                if let Some(path) = file.path() {
                    let ok = self_clone.save_to_path(path);
                    callback(ok);
                    return;
                }
            }
            callback(false);
        });
    }

    fn save_to_path(&self, path: PathBuf) -> bool {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        if let Some(ref mut proj) = g.project {
            proj.active_chapter_index = active_idx;
            if ThemeManager::instance().settings().auto_format_html {
                if let Some(ch) = proj.chapters.get_mut(active_idx) {
                    ch.ensure_loaded();
                    let formatted = HtmlFormatter::format_html_source(&ch.html);
                    ch.set_html_modified(formatted);
                }
            }
            let res = ProjectSerializer::save(&path, proj);
            if res.is_ok() {
                proj.mark_all_chapters_saved();
                proj.offload_inactive_chapters();
                drop(g);
                *self.current_file_path.borrow_mut() = Some(path.clone());
                self.sync_from_workspace();
                let msg = format!(
                    "Project saved to {}",
                    path.file_name().unwrap_or_default().to_string_lossy()
                );
                self.toast_overlay.add_toast(adw::Toast::new(&msg));
                true
            } else {
                drop(g);
                let err_msg = format!("Failed to save project: {:?}", res.err());
                self.toast_overlay.add_toast(adw::Toast::new(&err_msg));
                false
            }
        } else {
            false
        }
    }

    fn confirm_unsaved_changes<F: FnOnce() + 'static>(&self, action_name: &str, on_proceed: F) {
        let is_dirty = {
            let ws = self.handler.workspace();
            let g = ws.lock();
            g.project.as_ref().map(|p| p.dirty).unwrap_or(false)
        };

        if !is_dirty {
            on_proceed();
            return;
        }

        let dialog = adw::MessageDialog::builder()
            .heading("Save Changes?")
            .body(&format!(
                "The current project has unsaved changes. Do you want to save them before {}?",
                action_name
            ))
            .transient_for(&self.window)
            .modal(true)
            .build();

        dialog.add_response("cancel", "Cancel");
        dialog.add_response("discard", "Discard");
        dialog.set_response_appearance("discard", adw::ResponseAppearance::Destructive);
        dialog.add_response("save", "Save");
        dialog.set_response_appearance("save", adw::ResponseAppearance::Suggested);
        dialog.set_default_response(Some("save"));
        dialog.set_close_response("cancel");

        let h = self.clone();
        let on_proceed_cell = Rc::new(RefCell::new(Some(on_proceed)));

        dialog.connect_response(None, move |_, response| {
            if response == "discard" {
                let ws = h.handler.workspace();
                let mut g = ws.lock();
                if let Some(ref mut proj) = g.project {
                    proj.dirty = false;
                }
                drop(g);
                if let Some(cb) = on_proceed_cell.borrow_mut().take() {
                    cb();
                }
            } else if response == "save" {
                let has_path = h.current_file_path.borrow().is_some();
                if has_path {
                    let path = h.current_file_path.borrow().clone().unwrap();
                    if h.save_to_path(path) {
                        if let Some(cb) = on_proceed_cell.borrow_mut().take() {
                            cb();
                        }
                    }
                } else {
                    let cb_clone = on_proceed_cell.clone();
                    h.save_project_as_with_callback(move |saved| {
                        if saved {
                            if let Some(cb) = cb_clone.borrow_mut().take() {
                                cb();
                            }
                        }
                    });
                }
            }
        });

        dialog.present();
    }

    fn add_new_chapter(&self) {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let next_num_opt = if let Some(ref mut proj) = g.project {
            let next_num = proj.chapters.len() + 1;
            proj.add_chapter(&format!("Chapter {}", next_num));
            if let Some(ch) = proj.chapters.last_mut() {
                ch.set_html_modified("<p></p>".to_string());
            }
            proj.dirty = true;
            proj.active_chapter_index = next_num - 1;
            proj.offload_inactive_chapters();
            Some(next_num)
        } else {
            None
        };
        if let Some(next_num) = next_num_opt {
            g.active_chapter_index = next_num - 1;
            drop(g);
            self.sync_from_workspace();
            self.toast_overlay
                .add_toast(adw::Toast::new(&format!("Added Chapter {}", next_num)));
        }
    }

    fn delete_active_chapter(&self) {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        let mut new_active = active_idx;
        let removed = if let Some(ref mut proj) = g.project {
            if proj.chapters.is_empty() {
                None
            } else {
                let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                let ch = proj.remove_chapter(idx);
                proj.dirty = true;
                if active_idx > 0 && active_idx >= proj.chapters.len() {
                    new_active = proj.chapters.len().saturating_sub(1);
                }
                proj.active_chapter_index = new_active;
                proj.offload_inactive_chapters();
                ch
            }
        } else {
            None
        };
        g.active_chapter_index = new_active;
        drop(g);
        self.sync_from_workspace();
        if let Some(ch) = removed {
            self.toast_overlay
                .add_toast(adw::Toast::new(&format!("Deleted '{}'", ch.title)));
        }
    }

    fn copy_active_chapter_html(&self) {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        if let Some(ref mut proj) = g.project {
            let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
            if let Some(ch) = proj.chapters.get_mut(idx) {
                ch.ensure_loaded();
                if let Some(display) = gdk::Display::default() {
                    display.clipboard().set_text(&ch.html);
                }
                let count = ch.word_count();
                self.toast_overlay.add_toast(adw::Toast::new(&format!(
                    "Chapter HTML copied to clipboard ({} words)",
                    count
                )));
            }
        }
    }

    fn format_active_chapter_html(&self) {
        self.show_loading_spinner("Formatting chapter HTML…");
        let h = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(15), move || {
            let ws = h.handler.workspace();
            let mut g = ws.lock();
            let active_idx = g.active_chapter_index;
            if let Some(ref mut proj) = g.project {
                let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                if let Some(ch) = proj.chapters.get_mut(idx) {
                    ch.ensure_loaded();
                    let formatted = HtmlFormatter::format_html_source(&ch.html);
                    ch.set_html_modified(formatted);
                    proj.dirty = true;
                    drop(g);
                    h.sync_from_workspace();
                    h.hide_loading_spinner();
                    h.toast_overlay
                        .add_toast(adw::Toast::new("HTML formatted"));
                    return;
                }
            }
            drop(g);
            h.hide_loading_spinner();
        });
    }

    fn jump_to_html_line(&self, line: usize) {
        if self.notebook.current_page() != Some(1) {
            self.notebook.set_current_page(Some(1));
        }
        let buffer = self.html_text_view.buffer();
        let target_line = (line.saturating_sub(1)) as i32;
        if let Some(mut iter) = buffer.iter_at_line(target_line) {
            buffer.place_cursor(&iter);
            self.html_text_view.scroll_to_iter(&mut iter, 0.1, false, 0.0, 0.0);
        }
    }

    fn switch_active_chapter(&self, idx: usize) {
        let should_switch = {
            let ws = self.handler.workspace();
            let g = ws.lock();
            if let Some(ref proj) = g.project {
                idx < proj.chapters.len() && idx != g.active_chapter_index
            } else {
                false
            }
        };
        if !should_switch {
            return;
        }

        self.show_loading_spinner(&format!(
            "Offloading previous chapter & loading Chapter {}…",
            idx + 1
        ));

        // Immediately clear active editor buffers before loading the next chapter
        let cur_page = self.notebook.current_page().unwrap_or(0);
        self.unload_inactive_tabs(99);

        let h = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(20), move || {
            {
                let ws = h.handler.workspace();
                let mut g = ws.lock();
                let prev_idx = g.active_chapter_index;
                if let Some(ref mut proj) = g.project {
                    proj.active_chapter_index = prev_idx;
                    proj.switch_chapter_with_offload(idx);
                    g.active_chapter_index = idx;
                }
            }
            h.load_active_tab_content(cur_page);
            h.update_status_and_problems_only();
            h.refresh_preview();
            h.plugin_host.notify_chapter_changed();
            h.hide_loading_spinner();
        });
    }

    fn switch_editor_tab_with_offload(&self, page_num: u32) {
        let msg = match page_num {
            0 => "Unloading Raw HTML & loading Rich Text composer…",
            1 => "Unloading Rich Text & loading Raw HTML source…",
            _ => "Unloading chapter editor & loading Work Skin CSS…",
        };
        self.show_loading_spinner(msg);
        self.unload_inactive_tabs(page_num);

        let h = self.clone();
        glib::timeout_add_local_once(Duration::from_millis(20), move || {
            h.load_active_tab_content(page_num);
            h.hide_loading_spinner();
        });
    }

    fn load_active_tab_content(&self, page_num: u32) {
        match page_num {
            0 => self.sync_rich_editor_from_workspace(),
            1 => self.sync_html_editor_from_workspace(),
            2 => self.sync_css_editor_from_workspace(),
            _ => {}
        }
    }

    fn update_line_numbers(&self) {
        let count = self.html_text_view.buffer().line_count().max(1);
        let digits = format!("{}", count).len().max(2);
        let gutter_w = (digits as i32 * 8) + 22;
        self.line_gutter_area.set_content_width(gutter_w);
        self.line_gutter_area.queue_draw();
    }

    fn update_tag_breadcrumbs(&self) {
        let buf = self.html_text_view.buffer();
        let mark = buf.get_insert();
        let iter = buf.iter_at_mark(&mark);
        let end_offset = iter.offset();
        // Only scan at most the last 4096 characters before cursor to stay O(1) on giant chapters
        let start_offset = (end_offset - 4096).max(0);
        let start = buf.iter_at_offset(start_offset);
        let text_before = buf.text(&start, &iter, false);
        let path = extract_tag_path(&text_before);
        if path.is_empty() {
            self.tag_breadcrumb_label.set_text("🏷️ Tags: (root document)");
        } else {
            self.tag_breadcrumb_label
                .set_text(&format!("🏷️ Tags: {}", path.join(" > ")));
        }
    }

    fn on_rich_text_changed(&self, text: String) {
        if *self.is_syncing.borrow() || !*self.rich_editor_loaded.borrow() {
            return;
        }
        let mut changed = false;
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        if let Some(ref mut proj) = g.project {
            if proj.chapters.is_empty() {
                drop(g);
                return;
            }
            let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
            if let Some(ch) = proj.chapters.get_mut(idx) {
                ch.ensure_loaded();
                if ch.html != text {
                    ch.set_html_modified(text);
                    proj.dirty = true;
                    changed = true;
                }
            }
        }
        drop(g);
        if changed {
            self.update_status_and_problems_only();
            if self.sideview_box.is_visible() {
                self.refresh_preview();
            }
        }
    }

    fn on_html_text_changed(&self, text: String) {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        if let Some(ref mut proj) = g.project {
            if proj.chapters.is_empty() {
                drop(g);
                return;
            }
            let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
            if let Some(ch) = proj.chapters.get_mut(idx) {
                ch.set_html_modified(text);
                proj.dirty = true;
            }
        }
        drop(g);
        self.update_status_and_problems_only();
        self.update_line_numbers();
        self.update_tag_breadcrumbs();
        self.minimap_area.queue_draw();
        if self.sideview_box.is_visible() {
            self.refresh_preview();
        }
    }

    fn on_css_text_changed(&self, text: String) {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        if let Some(ref mut proj) = g.project {
            proj.work_skin_css = text;
            proj.dirty = true;
        }
        drop(g);
        self.update_status_and_problems_only();
        if self.sideview_box.is_visible() {
            self.refresh_preview();
        }
    }

    fn sync_rich_editor_from_workspace(&self) {
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        if let Some(ref mut proj) = g.project {
            let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
            let css = proj.work_skin_css.clone();
            if let Some(ch) = proj.chapters.get_mut(idx) {
                ch.ensure_loaded();
                let is_dark = adw::StyleManager::default().is_dark();
                let doc = compose_rich_editor_document(&ch.html, &css, is_dark);
                self.rich_web_view.load_html(&doc, None);
                *self.rich_editor_loaded.borrow_mut() = true;
            }
        }
    }

    fn sync_html_editor_from_workspace(&self) {
        let html_to_load = {
            let ws = self.handler.workspace();
            let mut g = ws.lock();
            let active_idx = g.active_chapter_index;
            if let Some(ref mut proj) = g.project {
                let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                if let Some(ch) = proj.chapters.get_mut(idx) {
                    ch.ensure_loaded();
                    Some(ensure_readable_html_lines(&ch.html))
                } else {
                    None
                }
            } else {
                None
            }
        };

        if let Some(readable_html) = html_to_load {
            *self.is_syncing.borrow_mut() = true;
            self.html_text_view.buffer().set_text(&readable_html);
            *self.is_syncing.borrow_mut() = false;
            self.update_line_numbers();
            self.update_tag_breadcrumbs();
            self.minimap_area.queue_draw();
            self.update_status_and_problems_only();
        }
    }

    fn sync_css_editor_from_workspace(&self) {
        let css = {
            let ws = self.handler.workspace();
            let g = ws.lock();
            g.project.as_ref().map(|p| p.work_skin_css.clone())
        };
        if let Some(css_text) = css {
            *self.is_syncing.borrow_mut() = true;
            self.css_text_view.buffer().set_text(&css_text);
            *self.is_syncing.borrow_mut() = false;
        }
    }

    fn refresh_preview(&self) {
        if !self.sideview_box.is_visible() {
            return;
        }
        let ws = self.handler.workspace();
        let mut g = ws.lock();
        let active_idx = g.active_chapter_index;
        if let Some(ref mut proj) = g.project {
            if proj.chapters.is_empty() {
                let empty_html = "<!DOCTYPE html><html><body style=\"font-family: sans-serif; display: flex; align-items: center; justify-content: center; height: 80vh; color: #888;\"><div style=\"text-align: center;\"><h3>No chapters yet</h3><p>Click '+' in the sidebar to add a new chapter to get started</p></div></body></html>";
                self.sideview_web_view.load_html(empty_html, None);
                return;
            }
            let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
            let work_skin_css = proj.work_skin_css.clone();
            if let Some(ch) = proj.chapters.get_mut(idx) {
                ch.ensure_loaded();
                let preview = WebViewPreviewController::new();
                let side_css = if *self.sideview_reversi.borrow() {
                    format!(
                        "{}\n/* AO3 Reversi Skin */\nbody, html {{ background-color: #1e1e1e !important; color: #e0e0e0 !important; }}\n#outer, #inner, .userstuff, .preface {{ background-color: #1e1e1e !important; color: #e0e0e0 !important; }}\na, a:link {{ color: #729fcf !important; }}\na:visited {{ color: #ad7fa8 !important; }}\nblockquote {{ border-left: 3px solid #555753 !important; background: rgba(255, 255, 255, 0.04) !important; color: #d3d7cf !important; }}\nhr {{ border-color: #444 !important; }}",
                        work_skin_css
                    )
                } else {
                    work_skin_css
                };
                let doc_side = preview.compose_ao3_preview_document(&ch.title, &ch.html, &side_css);
                self.sideview_web_view.load_html(&doc_side, None);
            }
        }
    }

    fn update_status_and_problems_only(&self) {
        let status_data = {
            let ws = self.handler.workspace();
            let mut g = ws.lock();
            let active_idx = g.active_chapter_index;
            if let Some(ref mut proj) = g.project {
                let idx = active_idx.min(proj.chapters.len().saturating_sub(1));
                let total_words: usize = proj.chapters.iter().map(|c| c.word_count()).sum();
                let dirty = proj.dirty;
                if let Some(ch) = proj.chapters.get_mut(idx) {
                    ch.ensure_loaded();
                    let wc = ch.word_count();
                    Some((dirty, idx, wc, total_words, ch.html.clone()))
                } else {
                    None
                }
            } else {
                None
            }
        };

        if let Some((dirty, active_idx, word_count, total_words, html)) = status_data {
            self.dirty_label.set_text(if dirty { "● Unsaved" } else { "Saved" });
            self.status_label.set_text(&format!(
                "Chapter {}: {} words | Total: {} words",
                active_idx + 1,
                word_count,
                total_words
            ));

            // Update active row subtitle in sidebar without rebuilding all rows
            if let Some(row) = self.chapter_list_box.row_at_index(active_idx as i32) {
                if let Ok(action_row) = row.downcast::<adw::ActionRow>() {
                    action_row.set_subtitle(&format!("{} words", word_count));
                }
            }

            let report = HtmlLinter::lint_html(&html, 1);
            let count = report.issues.len();
            if count == 0 {
                self.problems_btn.set_icon_name("dialog-information-symbolic");
                self.problems_btn.set_label("✓ 0 Problems");
                self.issue_icon.set_icon_name(Some("dialog-information-symbolic"));
                self.issue_summary_label.set_text("✓ 0 Problems in HTML");
                self.toggle_details_btn.set_visible(false);
                self.issue_details_box.set_visible(false);
            } else {
                self.problems_btn.set_icon_name("dialog-warning-symbolic");
                let label_text = if count == 1 {
                    "⚠️ 1 Problem".to_string()
                } else {
                    format!("⚠️ {} Problems", count)
                };
                self.problems_btn.set_label(&label_text);
                self.issue_icon.set_icon_name(Some("dialog-warning-symbolic"));
                self.issue_summary_label.set_text(&format!(
                    "⚠️ {} formatting issue(s) detected in HTML",
                    count
                ));
                self.toggle_details_btn.set_visible(true);

                while let Some(child) = self.issue_details_box.first_child() {
                    self.issue_details_box.remove(&child);
                }
                for issue in report.issues.iter().take(6) {
                    let row = gtk::Box::new(gtk::Orientation::Horizontal, 8);
                    let line_lbl = gtk::Label::builder()
                        .label(&format!("Line {}:", issue.start_pos))
                        .css_classes(["bold"])
                        .build();
                    row.append(&line_lbl);

                    let msg_lbl = gtk::Label::builder()
                        .label(&issue.message)
                        .xalign(0.0)
                        .hexpand(true)
                        .ellipsize(gtk4::pango::EllipsizeMode::End)
                        .build();
                    row.append(&msg_lbl);

                    let jump_btn = gtk::Button::builder()
                        .label("Jump")
                        .css_classes(["small-button", "flat"])
                        .build();
                    let line_no = issue.start_pos;
                    let self_clone = self.clone();
                    jump_btn.connect_clicked(move |_| {
                        self_clone.jump_to_html_line(line_no);
                    });
                    row.append(&jump_btn);

                    self.issue_details_box.append(&row);
                }
            }
        }
    }

    fn sync_from_workspace(&self) {
        *self.is_syncing.borrow_mut() = true;

        let proj_data = {
            let ws = self.handler.workspace();
            let mut g = ws.lock();
            let active_idx_raw = g.active_chapter_index;
            if let Some(ref mut proj) = g.project {
                let active_idx = active_idx_raw.min(proj.chapters.len().saturating_sub(1));
                proj.active_chapter_index = active_idx;
                proj.offload_inactive_chapters();

                let ch_list = proj
                    .chapters
                    .iter()
                    .map(|c| (c.title.clone(), c.word_count()))
                    .collect::<Vec<_>>();
                let active_ch = proj.chapters.get(active_idx).cloned();
                let total_words: usize = proj.chapters.iter().map(|c| c.word_count()).sum();
                Some((
                    proj.title.clone(),
                    proj.dirty,
                    ch_list,
                    active_idx,
                    active_ch,
                    total_words,
                ))
            } else {
                None
            }
        };

        if let Some((proj_title, dirty, ch_list, active_idx, active_ch, total_words)) = proj_data {
            let file_sub = if let Some(ref p) = *self.current_file_path.borrow() {
                p.file_name().unwrap_or_default().to_string_lossy().to_string()
            } else {
                "Untitled".to_string()
            };
            self.window_title.set_title(&proj_title);
            self.window_title.set_subtitle(&file_sub);
            self.dirty_label.set_text(if dirty { "● Unsaved" } else { "Saved" });

            // Refresh chapters list
            while let Some(child) = self.chapter_list_box.first_child() {
                self.chapter_list_box.remove(&child);
            }

            if ch_list.is_empty() {
                let empty_row = adw::ActionRow::builder()
                    .title("No chapters")
                    .subtitle("Click '+' to add a chapter")
                    .activatable(false)
                    .selectable(false)
                    .build();
                self.chapter_list_box.append(&empty_row);

                self.status_label.set_text("0 chapters | Add a new chapter to get started");
                let is_dark = adw::StyleManager::default().is_dark();
                let empty_doc = compose_rich_editor_document(
                    "<p style='color: #888; text-align: center; margin-top: 30vh;'>No chapters yet. Click '+' in the sidebar to add a new chapter to get started.</p>",
                    "",
                    is_dark,
                );
                self.rich_web_view.load_html(&empty_doc, None);
                self.html_text_view.buffer().set_text("");
                self.html_text_view.set_sensitive(false);
                self.copy_html_btn.set_sensitive(false);
                self.problems_btn.set_sensitive(false);
                self.problems_btn.set_icon_name("dialog-information-symbolic");
                self.problems_btn.set_label("✓ 0 Problems");
                self.issue_icon.set_icon_name(Some("dialog-information-symbolic"));
                self.issue_summary_label.set_text("✓ 0 Problems in HTML");
                self.toggle_details_btn.set_visible(false);
                self.issue_details_box.set_visible(false);
                self.line_gutter_area.queue_draw();
                self.tag_breadcrumb_label.set_text("🏷️ Tags: (root document)");
            } else {
                self.html_text_view.set_sensitive(true);
                self.copy_html_btn.set_sensitive(true);
                self.problems_btn.set_sensitive(true);

                for (idx, (title, wc)) in ch_list.iter().enumerate() {
                    let row = adw::ActionRow::builder()
                        .use_markup(false)
                        .title(&format!("{}. {}", idx + 1, title))
                        .subtitle(&format!("{} words", wc))
                        .build();
                    self.chapter_list_box.append(&row);
                }

                if let Some(row) = self.chapter_list_box.row_at_index(active_idx as i32) {
                    self.chapter_list_box.select_row(Some(&row));
                }

                if let Some(ref ch) = active_ch {
                    self.status_label.set_text(&format!(
                        "Chapter {}: {} words | Total: {} words",
                        active_idx + 1,
                        ch.word_count(),
                        total_words
                    ));
                }
            }
        } else {
            self.window_title.set_title("Ensemble");
            self.window_title.set_subtitle("AO3 Fanfiction Typewriter");
            self.status_label.set_text("Words: 0 (Total: 0)");
            self.dirty_label.set_text("Saved");
            self.problems_btn.set_label("✓ 0 Problems");
        }

        *self.is_syncing.borrow_mut() = false;

        // Only populate the currently viewed editor tab and unload the inactive tabs!
        let cur_page = self.notebook.current_page().unwrap_or(0);
        self.unload_inactive_tabs(cur_page);
        self.load_active_tab_content(cur_page);
        self.update_status_and_problems_only();
        self.refresh_preview();
    }
}

fn exec_js(view: &webkit6::WebView, script: &str) {
    view.evaluate_javascript(script, None, None, None::<&gio::Cancellable>, |_| {});
}

fn show_insert_link_dialog(parent: &adw::ApplicationWindow, view: &webkit6::WebView) {
    let dialog = adw::MessageDialog::builder()
        .heading("Insert Link")
        .body("Enter the web URL for the link:")
        .transient_for(parent)
        .modal(true)
        .build();

    let entry = gtk::Entry::builder()
        .placeholder_text("https://archiveofourown.org")
        .text("https://")
        .activates_default(true)
        .margin_top(8)
        .margin_bottom(8)
        .build();

    dialog.set_extra_child(Some(&entry));
    dialog.add_response("cancel", "Cancel");
    dialog.add_response("insert", "Insert");
    dialog.set_response_appearance("insert", adw::ResponseAppearance::Suggested);
    dialog.set_default_response(Some("insert"));

    let view = view.clone();
    dialog.connect_response(None, move |_, resp| {
        if resp == "insert" {
            let url = entry.text().to_string();
            if !url.trim().is_empty() {
                let json_url = serde_json::to_string(&url).unwrap_or_else(|_| "\"\"".to_string());
                let script = format!("execCmd('createLink', {});", json_url);
                exec_js(&view, &script);
            }
        }
    });
    dialog.present();
}

fn compose_rich_editor_document(html_content: &str, work_skin_css: &str, is_dark: bool) -> String {
    let (bg, fg, accent) = if is_dark {
        ("#1e1e1e", "#e0e0e0", "#729fcf")
    } else {
        ("#ffffff", "#2a2a2a", "#990000")
    };

    format!(
        r#"<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<style>
  html, body {{
    min-height: 100%;
    margin: 0;
    padding: 0;
    background-color: {bg};
    color: {fg};
    font-family: 'Lucida Grande', 'Lucida Sans Unicode', 'GNU Unifont', Verdana, Helvetica, sans-serif;
    font-size: 15px;
    line-height: 1.6;
  }}
  body {{
    box-sizing: border-box;
    padding: 20px 28px;
  }}
  #workskin {{
    max-width: 62em;
    margin: 0 auto;
  }}
  #editor {{
    min-height: 75vh;
    outline: none;
    cursor: text;
  }}
  #editor:empty:before {{
    content: "Start writing your story here…";
    color: #888;
    pointer-events: none;
  }}
  #editor img {{
    max-width: 100%;
    height: auto;
  }}
  a {{ color: {accent}; }}
  blockquote {{
    border-left: 3px solid #888;
    margin-left: 0;
    padding-left: 16px;
    color: {fg};
    opacity: 0.85;
  }}
  hr {{
    border: none;
    border-top: 1px solid #888;
    margin: 1.5em 0;
  }}
  /* Custom Work Skin CSS */
  {work_skin_css}
</style>
<script>
  var isInternalUpdate = false;
  var syncSeq = 0;
  function notifyChange() {{
    if (isInternalUpdate) return;
    var editor = document.getElementById('editor');
    if (!editor) return;
    var html = editor.innerHTML;
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ensembleRichChanged) {{
      try {{
        window.webkit.messageHandlers.ensembleRichChanged.postMessage(html);
      }} catch (e) {{}}
    }}
    syncSeq += 1;
    document.title = 'ensemble-sync:' + syncSeq;
  }}
  function findAncestorTag(node, tagName, editor) {{
    var cur = node && node.nodeType === 3 ? node.parentNode : node;
    var target = (tagName || '').toLowerCase();
    while (cur && cur !== editor && cur !== document.body) {{
      if ((cur.tagName || '').toLowerCase() === target) {{
        return cur;
      }}
      cur = cur.parentNode;
    }}
    return null;
  }}
  function unwrapBlockquote(bq, editor) {{
    var parent = bq.parentNode;
    if (!parent) return;
    var sel = window.getSelection();
    var lastNode = null;
    while (bq.firstChild) {{
      var child = bq.firstChild;
      if (child.nodeType === 3) {{
        var txt = child.textContent;
        if (txt.trim().length > 0) {{
          var p = document.createElement('p');
          p.textContent = txt;
          parent.insertBefore(p, bq);
          lastNode = p;
        }}
        bq.removeChild(child);
      }} else {{
        parent.insertBefore(child, bq);
        lastNode = child;
      }}
    }}
    parent.removeChild(bq);
    if (lastNode && sel) {{
      try {{
        var range = document.createRange();
        range.selectNodeContents(lastNode);
        range.collapse(false);
        sel.removeAllRanges();
        sel.addRange(range);
      }} catch (e) {{}}
    }}
  }}
  function toggleBlock(tag) {{
    var editor = document.getElementById('editor');
    if (!editor) return;
    editor.focus();
    var sel = window.getSelection();
    var lower = (tag || '').toLowerCase();

    if (lower === 'blockquote') {{
      var bq = sel && sel.anchorNode ? findAncestorTag(sel.anchorNode, 'blockquote', editor) : null;
      if (bq) {{
        unwrapBlockquote(bq, editor);
      }} else {{
        document.execCommand('formatBlock', false, '<blockquote>');
      }}
    }} else if (lower === 'p') {{
      var bq = sel && sel.anchorNode ? findAncestorTag(sel.anchorNode, 'blockquote', editor) : null;
      if (bq) {{
        unwrapBlockquote(bq, editor);
      }}
      document.execCommand('formatBlock', false, '<p>');
    }} else {{
      var h = sel && sel.anchorNode ? findAncestorTag(sel.anchorNode, lower, editor) : null;
      if (h) {{
        document.execCommand('formatBlock', false, '<p>');
      }} else {{
        document.execCommand('formatBlock', false, '<' + lower + '>');
      }}
    }}
    notifyChange();
    setTimeout(sendFormatState, 10);
  }}
  function sendFormatState() {{
    var editor = document.getElementById('editor');
    if (!editor) return;
    var sel = window.getSelection();
    var isBold = false, isItalic = false, isUnderline = false, isStrike = false;
    var isH1 = false, isH2 = false, isH3 = false, isP = false, isQuote = false;
    var isUl = false, isOl = false;

    try {{ isBold = document.queryCommandState('bold'); }} catch (e) {{}}
    try {{ isItalic = document.queryCommandState('italic'); }} catch (e) {{}}
    try {{ isUnderline = document.queryCommandState('underline'); }} catch (e) {{}}
    try {{ isStrike = document.queryCommandState('strikeThrough'); }} catch (e) {{}}
    try {{ isUl = document.queryCommandState('insertUnorderedList'); }} catch (e) {{}}
    try {{ isOl = document.queryCommandState('insertOrderedList'); }} catch (e) {{}}

    if (sel && sel.rangeCount > 0 && sel.anchorNode) {{
      var cur = sel.anchorNode;
      if (cur.nodeType === 3) cur = cur.parentNode;
      while (cur && cur !== editor && cur !== document.body) {{
        var t = (cur.tagName || '').toLowerCase();
        if (t === 'b' || t === 'strong') isBold = true;
        if (t === 'i' || t === 'em') isItalic = true;
        if (t === 'u') isUnderline = true;
        if (t === 's' || t === 'strike' || t === 'del') isStrike = true;
        if (t === 'h1') isH1 = true;
        if (t === 'h2') isH2 = true;
        if (t === 'h3') isH3 = true;
        if (t === 'p') isP = true;
        if (t === 'blockquote') isQuote = true;
        if (t === 'ul') isUl = true;
        if (t === 'ol') isOl = true;
        cur = cur.parentNode;
      }}
    }}

    var state = {{
      bold: !!isBold,
      italic: !!isItalic,
      underline: !!isUnderline,
      strike: !!isStrike,
      h1: !!isH1,
      h2: !!isH2,
      h3: !!isH3,
      p: !!isP,
      quote: !!isQuote,
      ul: !!isUl,
      ol: !!isOl
    }};

    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ensembleFormatChanged) {{
      try {{
        window.webkit.messageHandlers.ensembleFormatChanged.postMessage(JSON.stringify(state));
      }} catch (e) {{}}
    }}
  }}
  function execCmd(cmd, val) {{
    var editor = document.getElementById('editor');
    if (editor) editor.focus();
    document.execCommand(cmd, false, val || null);
    notifyChange();
    setTimeout(sendFormatState, 10);
  }}
  function insertHtmlSnippet(snippet) {{
    var editor = document.getElementById('editor');
    if (!editor) return;
    editor.focus();
    document.execCommand('insertHTML', false, snippet);
    notifyChange();
    setTimeout(sendFormatState, 10);
  }}
  function findAncestorBlock(node, editor) {{
    var cur = node && node.nodeType === 3 ? node.parentNode : node;
    while (cur && cur !== editor && cur !== document.body) {{
      var tag = (cur.tagName || '').toLowerCase();
      if (tag === 'p' || tag === 'div' || tag === 'blockquote' || tag === 'li' ||
          tag === 'h1' || tag === 'h2' || tag === 'h3' || tag === 'h4' || tag === 'h5' || tag === 'h6') {{
        return cur;
      }}
      cur = cur.parentNode;
    }}
    return null;
  }}
  function applyCssStyle(className, tagType) {{
    var editor = document.getElementById('editor');
    if (!editor || !className) return;
    editor.focus();
    var sel = window.getSelection();
    if (!sel || sel.rangeCount === 0) return;
    var range = sel.getRangeAt(0);

    if (tagType === 'span') {{
      if (!sel.isCollapsed) {{
        var pNode = range.commonAncestorContainer;
        if (pNode && pNode.nodeType === 3) pNode = pNode.parentNode;
        if (pNode && pNode !== editor && (pNode.tagName || '').toLowerCase() === 'span' && pNode.textContent === range.toString()) {{
          pNode.className = className;
        }} else {{
          var span = document.createElement('span');
          span.className = className;
          try {{
            span.appendChild(range.extractContents());
            range.insertNode(span);
            sel.removeAllRanges();
            var nr = document.createRange();
            nr.selectNodeContents(span);
            sel.addRange(nr);
          }} catch (e) {{}}
        }}
      }} else {{
        var cur = sel.anchorNode && sel.anchorNode.nodeType === 3 ? sel.anchorNode.parentNode : sel.anchorNode;
        if (cur && cur !== editor && (cur.tagName || '').toLowerCase() === 'span') {{
          cur.className = className;
        }} else {{
          var blk = findAncestorBlock(sel.anchorNode, editor);
          if (blk && blk !== editor) {{
            blk.innerHTML = '<span class="' + className + '">' + blk.innerHTML + '</span>';
          }}
        }}
      }}
    }} else {{
      // tagType === 'div'
      var blk = findAncestorBlock(sel.anchorNode, editor);
      if (blk && blk !== editor && (sel.isCollapsed || blk.textContent.trim() === range.toString().trim())) {{
        var tag = (blk.tagName || '').toLowerCase();
        if (tag === 'div') {{
          blk.className = className;
        }} else {{
          var div = document.createElement('div');
          div.className = className;
          div.innerHTML = blk.innerHTML;
          if (blk.parentNode) {{
            blk.parentNode.replaceChild(div, blk);
          }}
        }}
      }} else if (!sel.isCollapsed) {{
        var divWrap = document.createElement('div');
        divWrap.className = className;
        try {{
          divWrap.appendChild(range.extractContents());
          range.insertNode(divWrap);
        }} catch (e) {{}}
      }} else {{
        var newDiv = document.createElement('div');
        newDiv.className = className;
        newDiv.innerHTML = editor.innerHTML || '<br>';
        editor.innerHTML = '';
        editor.appendChild(newDiv);
      }}
    }}
    notifyChange();
    setTimeout(sendFormatState, 10);
  }}
  function clearCssStyle() {{
    var editor = document.getElementById('editor');
    if (!editor) return;
    var sel = window.getSelection();
    if (!sel || sel.rangeCount === 0) return;
    var cur = sel.anchorNode && sel.anchorNode.nodeType === 3 ? sel.anchorNode.parentNode : sel.anchorNode;
    while (cur && cur !== editor && cur !== document.body) {{
      var tag = (cur.tagName || '').toLowerCase();
      var next = cur.parentNode;
      if (tag === 'span' && cur.hasAttribute('class')) {{
        while (cur.firstChild) {{
          next.insertBefore(cur.firstChild, cur);
        }}
        next.removeChild(cur);
      }} else if (tag === 'div' && cur.hasAttribute('class')) {{
        var p = document.createElement('p');
        p.innerHTML = cur.innerHTML;
        next.replaceChild(p, cur);
      }} else if (cur.hasAttribute && cur.hasAttribute('class')) {{
        cur.removeAttribute('class');
      }}
      cur = next;
    }}
    notifyChange();
    setTimeout(sendFormatState, 10);
  }}
  function setEditorHtml(newHtml) {{
    var editor = document.getElementById('editor');
    if (editor) {{
      isInternalUpdate = true;
      editor.innerHTML = newHtml;
      isInternalUpdate = false;
      setTimeout(sendFormatState, 10);
    }}
  }}
  document.addEventListener('DOMContentLoaded', function() {{
    var editor = document.getElementById('editor');
    if (editor) {{
      document.addEventListener('input', function() {{
        notifyChange();
        sendFormatState();
      }}, true);
      document.addEventListener('keyup', function() {{
        notifyChange();
        sendFormatState();
      }}, true);
      document.addEventListener('mouseup', sendFormatState, true);
      document.addEventListener('selectionchange', sendFormatState, true);
      document.addEventListener('paste', function() {{
        setTimeout(function() {{
          notifyChange();
          sendFormatState();
        }}, 15);
      }}, true);
      document.addEventListener('cut', function() {{
        setTimeout(function() {{
          notifyChange();
          sendFormatState();
        }}, 15);
      }}, true);
      if (window.MutationObserver) {{
        var obs = new MutationObserver(function() {{
          notifyChange();
          sendFormatState();
        }});
        obs.observe(editor, {{
          childList: true,
          subtree: true,
          characterData: true,
          attributes: true
        }});
      }}
      editor.focus();
      setTimeout(sendFormatState, 50);
    }}
  }});
</script>
</head>
<body>
  <div id="workskin">
    <div id="editor" class="userstuff module" contenteditable="true">{html}</div>
  </div>
</body>
</html>"#,
        bg = bg,
        fg = fg,
        accent = accent,
        work_skin_css = work_skin_css,
        html = html_content
    )
}

fn sanitize_action_id(name: &str) -> String {
    name.chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' { c } else { '-' })
        .collect()
}

pub fn extract_work_skin_classes(work_skin_css: &str, chapter_html: &str) -> Vec<String> {
    let mut classes = Vec::new();

    // 1. Extract `.classname` selectors from Work Skin CSS (outside `{ ... }` declaration blocks)
    let mut in_block = 0usize;
    let mut in_comment = false;
    let bytes = work_skin_css.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if !in_comment && i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            in_comment = true;
            i += 2;
            continue;
        }
        if in_comment {
            if i + 1 < bytes.len() && bytes[i] == b'*' && bytes[i + 1] == b'/' {
                in_comment = false;
                i += 2;
            } else {
                i += 1;
            }
            continue;
        }
        if bytes[i] == b'{' {
            in_block += 1;
            i += 1;
            continue;
        }
        if bytes[i] == b'}' {
            in_block = in_block.saturating_sub(1);
            i += 1;
            continue;
        }
        if in_block == 0 && bytes[i] == b'.' {
            let start = i + 1;
            let mut end = start;
            while end < bytes.len() {
                let c = bytes[end] as char;
                if c.is_ascii_alphanumeric() || c == '-' || c == '_' {
                    end += 1;
                } else {
                    break;
                }
            }
            if end > start {
                let cls = &work_skin_css[start..end];
                if !cls.chars().next().unwrap_or('0').is_ascii_digit()
                    && cls != "userstuff"
                    && cls != "module"
                    && !classes.iter().any(|x| x == cls)
                {
                    classes.push(cls.to_string());
                }
            }
            i = end;
            continue;
        }
        i += 1;
    }

    // 2. Also extract any `class="..."` attributes already present in chapter_html
    let mut search = chapter_html;
    while let Some(pos) = search.find("class=\"") {
        let after = &search[pos + 7..];
        if let Some(end_q) = after.find('"') {
            for tok in after[..end_q].split_whitespace() {
                let clean = tok.trim();
                if !clean.is_empty()
                    && clean != "userstuff"
                    && clean != "module"
                    && !classes.iter().any(|x| x == clean)
                {
                    classes.push(clean.to_string());
                }
            }
            search = &after[end_q + 1..];
        } else {
            break;
        }
    }

    classes
}

fn show_custom_css_style_dialog(
    parent: &adw::ApplicationWindow,
    view: &webkit6::WebView,
    tag_type: &str,
) {
    let dialog = adw::MessageDialog::builder()
        .heading(&format!("Apply Custom CSS Class as <{}>", tag_type))
        .body("Enter the Work Skin CSS class name (without a leading dot):")
        .transient_for(parent)
        .modal(true)
        .build();

    let entry = gtk::Entry::builder()
        .placeholder_text("e.g. chat-in, text-bubble, note-box")
        .activates_default(true)
        .margin_top(8)
        .margin_bottom(8)
        .build();

    dialog.set_extra_child(Some(&entry));
    dialog.add_response("cancel", "Cancel");
    dialog.add_response("apply", "Apply Style");
    dialog.set_response_appearance("apply", adw::ResponseAppearance::Suggested);
    dialog.set_default_response(Some("apply"));

    let view = view.clone();
    let tag_t = tag_type.to_string();
    dialog.connect_response(None, move |_, resp| {
        if resp == "apply" {
            let raw = entry.text();
            let cls = raw.trim().trim_start_matches('.');
            if !cls.is_empty() {
                if let Ok(json_cls) = serde_json::to_string(cls) {
                    exec_js(&view, &format!("applyCssStyle({}, '{}');", json_cls, tag_t));
                }
            }
        }
    });
    dialog.present();
}

fn attach_rich_css_style_popover(
    menu_btn: &gtk::MenuButton,
    rich_web_view: &webkit6::WebView,
    handles_ref: Rc<RefCell<Option<EnsembleWindowHandles>>>,
) {
    let popover = gtk::Popover::new();
    let root = gtk::Box::new(gtk::Orientation::Vertical, 8);
    root.set_margin_top(10);
    root.set_margin_bottom(10);
    root.set_margin_start(10);
    root.set_margin_end(10);
    root.set_size_request(280, -1);

    let title_lbl = gtk::Label::builder()
        .label("Apply Work Skin CSS Class")
        .xalign(0.0)
        .css_classes(["heading"])
        .build();
    root.append(&title_lbl);

    let mode_row = gtk::Box::new(gtk::Orientation::Horizontal, 4);
    mode_row.add_css_class("linked");
    let span_mode_btn = gtk::ToggleButton::builder()
        .label("🏷️ Span (Inline)")
        .active(true)
        .hexpand(true)
        .build();
    let div_mode_btn = gtk::ToggleButton::builder()
        .label("📦 Div (Block / Card)")
        .group(&span_mode_btn)
        .hexpand(true)
        .build();
    mode_row.append(&span_mode_btn);
    mode_row.append(&div_mode_btn);
    root.append(&mode_row);

    let classes_scroll = gtk::ScrolledWindow::builder()
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .min_content_height(140)
        .max_content_height(220)
        .propagate_natural_height(true)
        .build();
    let classes_box = gtk::Box::new(gtk::Orientation::Vertical, 4);
    classes_scroll.set_child(Some(&classes_box));
    root.append(&classes_scroll);

    let custom_row = gtk::Box::new(gtk::Orientation::Horizontal, 6);
    let custom_entry = gtk::Entry::builder()
        .placeholder_text("Custom class (e.g. chat-in)")
        .hexpand(true)
        .build();
    let custom_apply_btn = gtk::Button::builder()
        .label("Apply")
        .css_classes(["suggested-action"])
        .build();
    custom_row.append(&custom_entry);
    custom_row.append(&custom_apply_btn);
    root.append(&custom_row);

    let clear_style_btn = gtk::Button::builder()
        .label("🧹 Clear Work Skin CSS Style")
        .css_classes(["flat"])
        .build();
    root.append(&clear_style_btn);

    popover.set_child(Some(&root));
    menu_btn.set_popover(Some(&popover));

    {
        let wv = rich_web_view.clone();
        let span_b = span_mode_btn.clone();
        let entry = custom_entry.clone();
        let pop = popover.clone();
        custom_apply_btn.connect_clicked(move |_| {
            let raw = entry.text();
            let cls = raw.trim().trim_start_matches('.');
            if !cls.is_empty() {
                let tag = if span_b.is_active() { "span" } else { "div" };
                if let Ok(json_cls) = serde_json::to_string(cls) {
                    exec_js(&wv, &format!("applyCssStyle({}, '{}');", json_cls, tag));
                }
                pop.popdown();
            }
        });
    }
    {
        let wv = rich_web_view.clone();
        let pop = popover.clone();
        clear_style_btn.connect_clicked(move |_| {
            exec_js(&wv, "clearCssStyle();");
            pop.popdown();
        });
    }

    let c_box = classes_box.clone();
    let wv = rich_web_view.clone();
    let span_b = span_mode_btn.clone();
    popover.connect_show(move |pop| {
        while let Some(child) = c_box.first_child() {
            c_box.remove(&child);
        }
        let classes = if let Some(ref h) = *handles_ref.borrow() {
            let ws = h.handler.workspace();
            let g = ws.lock();
            if let Some(ref proj) = g.project {
                let idx = g
                    .active_chapter_index
                    .min(proj.chapters.len().saturating_sub(1));
                let ch_html = proj
                    .chapters
                    .get(idx)
                    .map(|c| c.html.as_str())
                    .unwrap_or("");
                extract_work_skin_classes(&proj.work_skin_css, ch_html)
            } else {
                Vec::new()
            }
        } else {
            Vec::new()
        };

        if classes.is_empty() {
            let empty_lbl = gtk::Label::builder()
                .label("No classes declared in Work Skin CSS yet.\nUse the custom class field below or add rules in the Work Skin CSS tab.")
                .wrap(true)
                .xalign(0.0)
                .css_classes(["dim-label", "caption"])
                .build();
            c_box.append(&empty_lbl);
        } else {
            for cls in classes {
                let btn = gtk::Button::builder()
                    .label(&format!(".{}", cls))
                    .css_classes(["flat"])
                    .halign(gtk::Align::Fill)
                    .build();
                let wv_c = wv.clone();
                let span_c = span_b.clone();
                let pop_c = pop.clone();
                btn.connect_clicked(move |_| {
                    let tag = if span_c.is_active() { "span" } else { "div" };
                    if let Ok(json_cls) = serde_json::to_string(&cls) {
                        exec_js(&wv_c, &format!("applyCssStyle({}, '{}');", json_cls, tag));
                    }
                    pop_c.popdown();
                });
                c_box.append(&btn);
            }
        }
    });
}

fn attach_html_css_style_popover(
    menu_btn: &gtk::MenuButton,
    html_text_view: &gtk::TextView,
    handles_ref: Rc<RefCell<Option<EnsembleWindowHandles>>>,
) {
    let popover = gtk::Popover::new();
    let root = gtk::Box::new(gtk::Orientation::Vertical, 8);
    root.set_margin_top(10);
    root.set_margin_bottom(10);
    root.set_margin_start(10);
    root.set_margin_end(10);
    root.set_size_request(280, -1);

    let title_lbl = gtk::Label::builder()
        .label("Wrap Selection in Work Skin Class")
        .xalign(0.0)
        .css_classes(["heading"])
        .build();
    root.append(&title_lbl);

    let mode_row = gtk::Box::new(gtk::Orientation::Horizontal, 4);
    mode_row.add_css_class("linked");
    let span_mode_btn = gtk::ToggleButton::builder()
        .label("🏷️ <span class=\"…\">")
        .active(true)
        .hexpand(true)
        .build();
    let div_mode_btn = gtk::ToggleButton::builder()
        .label("📦 <div class=\"…\">")
        .group(&span_mode_btn)
        .hexpand(true)
        .build();
    mode_row.append(&span_mode_btn);
    mode_row.append(&div_mode_btn);
    root.append(&mode_row);

    let classes_scroll = gtk::ScrolledWindow::builder()
        .hscrollbar_policy(gtk::PolicyType::Never)
        .vscrollbar_policy(gtk::PolicyType::Automatic)
        .min_content_height(140)
        .max_content_height(220)
        .propagate_natural_height(true)
        .build();
    let classes_box = gtk::Box::new(gtk::Orientation::Vertical, 4);
    classes_scroll.set_child(Some(&classes_box));
    root.append(&classes_scroll);

    let custom_row = gtk::Box::new(gtk::Orientation::Horizontal, 6);
    let custom_entry = gtk::Entry::builder()
        .placeholder_text("Custom class (e.g. chat-in)")
        .hexpand(true)
        .build();
    let custom_apply_btn = gtk::Button::builder()
        .label("Wrap")
        .css_classes(["suggested-action"])
        .build();
    custom_row.append(&custom_entry);
    custom_row.append(&custom_apply_btn);
    root.append(&custom_row);

    popover.set_child(Some(&root));
    menu_btn.set_popover(Some(&popover));

    {
        let htv = html_text_view.clone();
        let span_b = span_mode_btn.clone();
        let entry = custom_entry.clone();
        let pop = popover.clone();
        custom_apply_btn.connect_clicked(move |_| {
            let raw = entry.text();
            let cls = raw.trim().trim_start_matches('.');
            if !cls.is_empty() {
                if span_b.is_active() {
                    insert_or_wrap_tag(&htv, &format!("<span class=\"{}\">", cls), "</span>");
                } else {
                    insert_or_wrap_tag(&htv, &format!("<div class=\"{}\">", cls), "</div>");
                }
                pop.popdown();
            }
        });
    }

    let c_box = classes_box.clone();
    let htv = html_text_view.clone();
    let span_b = span_mode_btn.clone();
    popover.connect_show(move |pop| {
        while let Some(child) = c_box.first_child() {
            c_box.remove(&child);
        }
        let classes = if let Some(ref h) = *handles_ref.borrow() {
            let ws = h.handler.workspace();
            let g = ws.lock();
            if let Some(ref proj) = g.project {
                let idx = g
                    .active_chapter_index
                    .min(proj.chapters.len().saturating_sub(1));
                let ch_html = proj
                    .chapters
                    .get(idx)
                    .map(|c| c.html.as_str())
                    .unwrap_or("");
                extract_work_skin_classes(&proj.work_skin_css, ch_html)
            } else {
                Vec::new()
            }
        } else {
            Vec::new()
        };

        if classes.is_empty() {
            let empty_lbl = gtk::Label::builder()
                .label("No classes declared in Work Skin CSS yet.\nUse the custom class field below or add rules in the Work Skin CSS tab.")
                .wrap(true)
                .xalign(0.0)
                .css_classes(["dim-label", "caption"])
                .build();
            c_box.append(&empty_lbl);
        } else {
            for cls in classes {
                let btn = gtk::Button::builder()
                    .label(&format!(".{}", cls))
                    .css_classes(["flat"])
                    .halign(gtk::Align::Fill)
                    .build();
                let htv_c = htv.clone();
                let span_c = span_b.clone();
                let pop_c = pop.clone();
                btn.connect_clicked(move |_| {
                    if span_c.is_active() {
                        insert_or_wrap_tag(&htv_c, &format!("<span class=\"{}\">", cls), "</span>");
                    } else {
                        insert_or_wrap_tag(&htv_c, &format!("<div class=\"{}\">", cls), "</div>");
                    }
                    pop_c.popdown();
                });
                c_box.append(&btn);
            }
        }
    });
}

