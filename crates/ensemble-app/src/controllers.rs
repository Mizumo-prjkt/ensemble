use ensemble_core::diff::RgbColor;
use ensemble_theme::{AppTheme, SkinFlavor, ThemeManager};

pub struct ToolbarIcons;

impl ToolbarIcons {
    fn make_svg(glyph: &str, normal: RgbColor, active: RgbColor) -> String {
        format!(
            r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" data-normal="{}" data-active="{}"><text x="12" y="16" text-anchor="middle" fill="{}">{}</text></svg>"#,
            normal.to_hex(),
            active.to_hex(),
            normal.to_hex(),
            glyph
        )
    }

    pub fn bold(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("B", normal, active)
    }
    pub fn italic(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("I", normal, active)
    }
    pub fn underline(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("U", normal, active)
    }
    pub fn strikethrough(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("S", normal, active)
    }
    pub fn heading1(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("H1", normal, active)
    }
    pub fn heading2(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("H2", normal, active)
    }
    pub fn heading3(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("H3", normal, active)
    }
    pub fn paragraph(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("P", normal, active)
    }
    pub fn link(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("🔗", normal, active)
    }
    pub fn horizontal_rule(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("―", normal, active)
    }
    pub fn blockquote(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("❝", normal, active)
    }
    pub fn bullet_list(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("•", normal, active)
    }
    pub fn numbered_list(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("1.", normal, active)
    }
    pub fn image(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("🖼", normal, active)
    }
    pub fn code(normal: RgbColor, active: RgbColor) -> String {
        Self::make_svg("<>", normal, active)
    }
}

#[derive(Debug, Clone)]
pub struct CheckboxControl {
    pub id: String,
    pub text: String,
    pub enabled: bool,
    pub checked: bool,
}

pub struct SettingsDialogController {
    tabs: Vec<String>,
    checkboxes: Vec<CheckboxControl>,
    reversi_radio_checked: bool,
    reversi_radio_enabled: bool,
    selected_flavor: SkinFlavor,
    material_you_combo_enabled: bool,
    local_details_card_hidden: bool,
}

impl Default for SettingsDialogController {
    fn default() -> Self {
        Self::new()
    }
}

impl SettingsDialogController {
    pub fn new() -> Self {
        let tm = ThemeManager::instance();
        let settings = tm.settings();
        let current_theme = tm.current_theme();
        let current_flavor = tm.current_skin_flavor();

        let checkboxes = vec![
            CheckboxControl {
                id: "mimicAo3BodyCheck".to_string(),
                text: "Allow Mimicking The whole AO3 Body page (Coming Soon)".to_string(),
                enabled: false,
                checked: false,
            },
            CheckboxControl {
                id: "aiMcpAccessCheck".to_string(),
                text: "Allow AI Agents to access this via MCP".to_string(),
                enabled: true,
                checked: true,
            },
            CheckboxControl {
                id: "aiApiAccessCheck".to_string(),
                text: "Allow AI Agents to access this via API (Coming Soon)".to_string(),
                enabled: false,
                checked: false,
            },
            CheckboxControl {
                id: "aiLocalAgentsCheck".to_string(),
                text: "[Beta] Allow Other local agents / local LLMs to access this software (MCP connector)".to_string(),
                enabled: true,
                checked: settings.local_llms_mcp_enabled,
            },
            CheckboxControl {
                id: "hoverElementInfoCheck".to_string(),
                text: "Show embedded element, tag, and CSS class info on hover".to_string(),
                enabled: true,
                checked: settings.hover_element_info,
            },
            CheckboxControl {
                id: "highlightErrorsCheck".to_string(),
                text: "Highlight errors, undefined classes, and warnings directly in editor".to_string(),
                enabled: true,
                checked: settings.highlight_errors,
            },
            CheckboxControl {
                id: "hoverErrorDetailsCheck".to_string(),
                text: "Show detailed problem explanations when hovering over issues".to_string(),
                enabled: true,
                checked: settings.hover_error_details,
            },
            CheckboxControl {
                id: "autoFormatRawHtmlCheck".to_string(),
                text: "Auto-format Raw HTML (split paragraphs across lines)".to_string(),
                enabled: true,
                checked: settings.auto_format_html,
            },
        ];

        Self {
            tabs: vec![
                "General".to_string(),
                "Editor".to_string(),
                "WebEngine".to_string(),
                "AI".to_string(),
                "Plugins".to_string(),
            ],
            checkboxes,
            reversi_radio_checked: current_theme == AppTheme::Ao3Reversi,
            reversi_radio_enabled: current_flavor != SkinFlavor::MaterialYou,
            selected_flavor: current_flavor,
            material_you_combo_enabled: current_theme != AppTheme::Ao3Reversi,
            local_details_card_hidden: !settings.local_llms_mcp_enabled,
        }
    }

    pub fn tabs(&self) -> &[String] {
        &self.tabs
    }

    pub fn checkboxes(&self) -> &[CheckboxControl] {
        &self.checkboxes
    }

    pub fn find_checkbox(&self, id: &str) -> Option<&CheckboxControl> {
        self.checkboxes.iter().find(|c| c.id == id)
    }

    pub fn set_local_agents_checked(&mut self, checked: bool) {
        if let Some(cb) = self
            .checkboxes
            .iter_mut()
            .find(|c| c.id == "aiLocalAgentsCheck")
        {
            cb.checked = checked;
        }
        self.local_details_card_hidden = !checked;
        ThemeManager::instance().update_settings(|s| {
            s.local_llms_mcp_enabled = checked;
        });
    }

    pub fn is_local_details_card_hidden(&self) -> bool {
        self.local_details_card_hidden
    }

    pub fn set_reversi_checked(&mut self, checked: bool) {
        if checked {
            ThemeManager::instance().set_theme(AppTheme::Ao3Reversi);
            self.reversi_radio_checked = true;
            self.material_you_combo_enabled = false;
            if self.selected_flavor == SkinFlavor::MaterialYou {
                self.selected_flavor = SkinFlavor::StandardEnsemble;
            }
        } else {
            self.reversi_radio_checked = false;
            self.material_you_combo_enabled = true;
        }
    }

    pub fn is_reversi_checked(&self) -> bool {
        self.reversi_radio_checked
    }

    pub fn is_reversi_enabled(&self) -> bool {
        self.reversi_radio_enabled
    }

    pub fn is_material_you_combo_enabled(&self) -> bool {
        self.material_you_combo_enabled
    }

    pub fn selected_flavor(&self) -> SkinFlavor {
        self.selected_flavor
    }

    pub fn set_selected_flavor(&mut self, flavor: SkinFlavor) {
        let updated_flavor = {
            let mut tm = ThemeManager::instance();
            tm.set_skin_flavor(flavor);
            tm.current_skin_flavor()
        };
        self.selected_flavor = updated_flavor;
        if self.selected_flavor == SkinFlavor::MaterialYou {
            self.reversi_radio_checked = false;
            self.reversi_radio_enabled = false;
        } else {
            self.reversi_radio_enabled = true;
        }
    }
}

pub struct MainMenuController {
    pub save_btn_title: String,
    pub save_btn_enabled: bool,
    pub continue_btn_hidden: bool,
    pub continue_subtext: String,
    pub close_btn_hidden: bool,
}

impl Default for MainMenuController {
    fn default() -> Self {
        Self::new()
    }
}

impl MainMenuController {
    pub fn new() -> Self {
        Self {
            save_btn_title: "Save Project".to_string(),
            save_btn_enabled: false,
            continue_btn_hidden: true,
            continue_subtext: String::new(),
            close_btn_hidden: true,
        }
    }

    pub fn set_project_state(&mut self, active: bool, project_title: &str) {
        if active {
            self.save_btn_enabled = true;
            self.continue_btn_hidden = false;
            self.continue_subtext = format!("Resume editing '{}'", project_title);
            self.close_btn_hidden = false;
        } else {
            self.save_btn_enabled = false;
            self.continue_btn_hidden = true;
            self.continue_subtext.clear();
            self.close_btn_hidden = true;
        }
    }
}

pub struct ProjectLoadingController {
    window_title: String,
    progress_value: i32,
    current_status: String,
    file_path: String,
    theme_bg: String,
}

impl ProjectLoadingController {
    pub fn new(file_path: &str) -> Self {
        let mut dlg = Self {
            window_title: "Opening Project…".to_string(),
            progress_value: 0,
            current_status: format!("Preparing to load {}…", file_path),
            file_path: file_path.to_string(),
            theme_bg: String::new(),
        };
        dlg.update_theme();
        dlg
    }

    pub fn window_title(&self) -> &str {
        &self.window_title
    }

    pub fn progress_value(&self) -> i32 {
        self.progress_value
    }

    pub fn current_status(&self) -> &str {
        &self.current_status
    }

    pub fn file_path(&self) -> &str {
        &self.file_path
    }

    pub fn set_progress(&mut self, percent: i32, status_message: &str) {
        self.progress_value = percent.clamp(0, 100);
        if !status_message.is_empty() {
            self.current_status = status_message.to_string();
        }
    }

    pub fn finish(&mut self) {
        self.progress_value = 100;
        self.current_status = "Ready!".to_string();
    }

    pub fn update_theme(&mut self) {
        self.theme_bg = ThemeManager::instance().window_bg_color().to_hex();
    }
}

pub struct AboutDialogController {
    pub has_update_card: bool,
    pub check_updates_btn_text: String,
    pub get_update_btn_text: String,
    pub theme_bg: String,
}

impl Default for AboutDialogController {
    fn default() -> Self {
        Self::new()
    }
}

impl AboutDialogController {
    pub fn new() -> Self {
        let mut ctrl = Self {
            has_update_card: true,
            check_updates_btn_text: "Check for Updates".to_string(),
            get_update_btn_text: "Get Update on GitHub".to_string(),
            theme_bg: String::new(),
        };
        ctrl.update_theme();
        ctrl
    }

    pub fn update_theme(&mut self) {
        self.theme_bg = ThemeManager::instance().window_bg_color().to_hex();
    }
}

pub struct WebViewPreviewController {
    reversi_skin: bool,
}

impl Default for WebViewPreviewController {
    fn default() -> Self {
        Self::new()
    }
}

impl WebViewPreviewController {
    pub fn new() -> Self {
        Self {
            reversi_skin: false,
        }
    }

    pub fn set_reversi_skin(&mut self, reversi: bool) {
        self.reversi_skin = reversi;
    }

    pub fn is_reversi_skin(&self) -> bool {
        self.reversi_skin
    }

    pub fn compose_ao3_preview_document(
        &self,
        chapter_title: &str,
        chapter_html: &str,
        work_skin_css: &str,
    ) -> String {
        let (bg, fg, accent, border) = if self.reversi_skin {
            ("#111111", "#eeeeee", "#5998d6", "#333333")
        } else {
            ("#ffffff", "#2a2a2a", "#990000", "#dddddd")
        };

        format!(
            r#"<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{title}</title>
<style>
  body {{
    background-color: {bg};
    color: {fg};
    font-family: 'Lucida Grande', 'Lucida Sans Unicode', 'GNU Unifont', Verdana, Helvetica, sans-serif;
    line-height: 1.5;
    margin: 0;
    padding: 24px;
  }}
  a {{ color: {accent}; }}
  hr {{ border: none; border-top: 1px solid {border}; margin: 1.5em 0; }}
  #workskin {{
    max-width: 62em;
    margin: 0 auto;
  }}
  {css}
</style>
</head>
<body>
  <div id="workskin">
    <h2 class="title heading">{title}</h2>
    <div class="userstuff module">
      {html}
    </div>
  </div>
</body>
</html>"#,
            title = chapter_title,
            bg = bg,
            fg = fg,
            accent = accent,
            border = border,
            css = work_skin_css,
            html = chapter_html
        )
    }
}
