use ensemble_mcp::McpAgentInstaller;
use ensemble_theme::ThemeManager;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;

pub fn show_preferences_dialog(parent: &impl IsA<gtk4::Window>) {
    let window = adw::PreferencesWindow::builder()
        .title("Preferences")
        .transient_for(parent)
        .modal(true)
        .default_width(640)
        .default_height(500)
        .build();

    // --- Page 1: General & Appearance ---
    let general_page = adw::PreferencesPage::builder()
        .title("General")
        .icon_name("preferences-system-symbolic")
        .build();

    let appearance_group = adw::PreferencesGroup::builder()
        .title("Appearance")
        .description("Native GTK theme and color scheme")
        .build();

    let style_manager = adw::StyleManager::default();
    let theme_row = adw::ActionRow::builder()
        .title("Color Scheme")
        .subtitle("Adwaita light or dark desktop mode")
        .build();

    let theme_box = gtk4::Box::new(gtk4::Orientation::Horizontal, 0);
    theme_box.add_css_class("linked");
    theme_box.set_valign(gtk4::Align::Center);

    let btn_system = create_theme_btn("preferences-desktop-display-symbolic", "System");
    let btn_light = create_theme_btn("weather-clear-symbolic", "Light");
    let btn_dark = create_theme_btn("weather-clear-night-symbolic", "Dark");

    {
        let sm = style_manager.clone();
        btn_system.connect_clicked(move |_| {
            sm.set_color_scheme(adw::ColorScheme::Default);
        });
    }
    {
        let sm = style_manager.clone();
        btn_light.connect_clicked(move |_| {
            sm.set_color_scheme(adw::ColorScheme::ForceLight);
        });
    }
    {
        let sm = style_manager.clone();
        btn_dark.connect_clicked(move |_| {
            sm.set_color_scheme(adw::ColorScheme::ForceDark);
        });
    }

    theme_box.append(&btn_system);
    theme_box.append(&btn_light);
    theme_box.append(&btn_dark);
    theme_row.add_suffix(&theme_box);
    appearance_group.add(&theme_row);

    // Auto-format HTML switch
    let auto_format_row = adw::ActionRow::builder()
        .title("Auto-Format Raw HTML")
        .subtitle("Split paragraphs and clean whitespace on save")
        .build();
    let auto_format_switch = gtk4::Switch::builder()
        .active(ThemeManager::instance().settings().auto_format_html)
        .valign(gtk4::Align::Center)
        .build();
    auto_format_switch.connect_active_notify(|sw| {
        let active = sw.is_active();
        ThemeManager::instance().update_settings(|s| s.auto_format_html = active);
    });
    auto_format_row.add_suffix(&auto_format_switch);
    auto_format_row.set_activatable_widget(Some(&auto_format_switch));
    appearance_group.add(&auto_format_row);

    general_page.add(&appearance_group);

    // --- Page 2: Editor & Diagnostics ---
    let editor_page = adw::PreferencesPage::builder()
        .title("Editor")
        .icon_name("accessories-text-editor-symbolic")
        .build();

    let linter_group = adw::PreferencesGroup::builder()
        .title("Diagnostics & Linting")
        .description("AO3 HTML validation and syntax assistance")
        .build();

    let highlight_row = adw::ActionRow::builder()
        .title("Highlight Tag Errors")
        .subtitle("Underline unclosed or disallowed tags in the editor")
        .build();
    let highlight_switch = gtk4::Switch::builder()
        .active(ThemeManager::instance().settings().highlight_errors)
        .valign(gtk4::Align::Center)
        .build();
    highlight_switch.connect_active_notify(|sw| {
        let active = sw.is_active();
        ThemeManager::instance().update_settings(|s| s.highlight_errors = active);
    });
    highlight_row.add_suffix(&highlight_switch);
    highlight_row.set_activatable_widget(Some(&highlight_switch));
    linter_group.add(&highlight_row);

    let hover_row = adw::ActionRow::builder()
        .title("Detailed Problem Explanations")
        .subtitle("Display suggestions and fixes when inspecting issues")
        .build();
    let hover_switch = gtk4::Switch::builder()
        .active(ThemeManager::instance().settings().hover_error_details)
        .valign(gtk4::Align::Center)
        .build();
    hover_switch.connect_active_notify(|sw| {
        let active = sw.is_active();
        ThemeManager::instance().update_settings(|s| s.hover_error_details = active);
    });
    hover_row.add_suffix(&hover_switch);
    hover_row.set_activatable_widget(Some(&hover_switch));
    linter_group.add(&hover_row);

    editor_page.add(&linter_group);

    // --- Page 3: Model Context Protocol (MCP) & AI ---
    let ai_page = adw::PreferencesPage::builder()
        .title("AI & MCP")
        .icon_name("network-server-symbolic")
        .build();

    let mcp_group = adw::PreferencesGroup::builder()
        .title("Model Context Protocol (MCP)")
        .description("WebSocket server for external coding & writing AI agents")
        .build();

    let mcp_row = adw::ActionRow::builder()
        .title("Embedded MCP Server")
        .subtitle("Allow AI agents to query state and edit chapters on port 26303")
        .build();
    let mcp_switch = gtk4::Switch::builder()
        .active(ThemeManager::instance().settings().allow_mcp_access)
        .valign(gtk4::Align::Center)
        .build();
    mcp_switch.connect_active_notify(|sw| {
        let active = sw.is_active();
        ThemeManager::instance().update_settings(|s| s.allow_mcp_access = active);
    });
    mcp_row.add_suffix(&mcp_switch);
    mcp_row.set_activatable_widget(Some(&mcp_switch));
    mcp_group.add(&mcp_row);

    let local_llm_row = adw::ActionRow::builder()
        .title("Local LLMs Connector [Beta]")
        .subtitle("Allow Ollama, llama.cpp, and vLLM local agents to collaborate")
        .build();
    let local_llm_switch = gtk4::Switch::builder()
        .active(ThemeManager::instance().settings().local_llms_mcp_enabled)
        .valign(gtk4::Align::Center)
        .build();
    local_llm_switch.connect_active_notify(|sw| {
        let active = sw.is_active();
        ThemeManager::instance().update_settings(|s| s.local_llms_mcp_enabled = active);
    });
    local_llm_row.add_suffix(&local_llm_switch);
    local_llm_row.set_activatable_widget(Some(&local_llm_switch));
    mcp_group.add(&local_llm_row);

    let install_row = adw::ActionRow::builder()
        .title("Agent Configurations")
        .subtitle("Install MCP tool definitions to Claude Desktop, Cursor, and Gemini")
        .build();
    let install_btn = gtk4::Button::with_label("Install Tools");
    install_btn.connect_clicked(|btn| {
        let exe = std::env::current_exe()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|_| "ensemble".to_string());
        let _ = McpAgentInstaller::install_all(&exe, 26303);
        btn.set_label("Installed ✓");
        btn.set_sensitive(false);
    });
    install_row.add_suffix(&install_btn);
    mcp_group.add(&install_row);

    ai_page.add(&mcp_group);

    window.add(&general_page);
    window.add(&editor_page);
    window.add(&ai_page);

    window.present();
}

fn create_theme_btn(icon_name: &str, label: &str) -> gtk4::Button {
    let btn = gtk4::Button::new();
    btn.set_valign(gtk4::Align::Center);
    let box_ = gtk4::Box::new(gtk4::Orientation::Horizontal, 6);
    box_.set_margin_start(10);
    box_.set_margin_end(10);
    box_.set_margin_top(4);
    box_.set_margin_bottom(4);
    let icon = gtk4::Image::from_icon_name(icon_name);
    icon.set_pixel_size(16);
    let lbl = gtk4::Label::new(Some(label));
    box_.append(&icon);
    box_.append(&lbl);
    btn.set_child(Some(&box_));
    btn
}
