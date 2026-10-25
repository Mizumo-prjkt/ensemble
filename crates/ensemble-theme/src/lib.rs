use ensemble_core::{AppPaths, RgbColor};
use image::GenericImageView;
use once_cell::sync::Lazy;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum AppTheme {
    Dark,
    Darker, // OLED deep black
    Light,
    Ao3Reversi,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum SkinFlavor {
    MaterialYou,
    StandardEnsemble,
    StandardQt,
    KdeUi,
    Custom,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppSettings {
    pub theme: String,
    pub skin_flavor: String,
    pub gpu_acceleration: bool,
    pub auto_format_html: bool,
    pub hover_element_info: bool,
    pub highlight_errors: bool,
    pub hover_error_details: bool,
    pub mimic_ao3_body: bool,
    pub allow_mcp_access: bool,
    pub allow_api_access: bool,
    pub local_llms_mcp_enabled: bool,
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            theme: "dark".to_string(),
            skin_flavor: "ensemble".to_string(),
            gpu_acceleration: true,
            auto_format_html: true,
            hover_element_info: true,
            highlight_errors: true,
            hover_error_details: true,
            mimic_ao3_body: true,
            allow_mcp_access: true,
            allow_api_access: false,
            local_llms_mcp_enabled: false,
        }
    }
}

impl AppSettings {
    pub fn settings_file_path() -> PathBuf {
        AppPaths::app_config_dir().join("settings.json")
    }

    pub fn load() -> Self {
        let path = Self::settings_file_path();
        if let Ok(bytes) = fs::read(&path) {
            if let Ok(s) = serde_json::from_slice::<Self>(&bytes) {
                return s;
            }
        }
        Self::default()
    }

    pub fn save(&self) {
        let path = Self::settings_file_path();
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        if let Ok(json) = serde_json::to_vec_pretty(self) {
            let _ = fs::write(path, json);
        }
    }
}

fn rgb_to_hsv(r: u8, g: u8, b: u8) -> (i32, i32, i32) {
    let rf = r as f64 / 255.0;
    let gf = g as f64 / 255.0;
    let bf = b as f64 / 255.0;
    let max = rf.max(gf).max(bf);
    let min = rf.min(gf).min(bf);
    let delta = max - min;

    let h = if delta < 1e-6 {
        0.0
    } else if (max - rf).abs() < 1e-6 {
        60.0 * (((gf - bf) / delta).rem_euclid(6.0))
    } else if (max - gf).abs() < 1e-6 {
        60.0 * (((bf - rf) / delta) + 2.0)
    } else {
        60.0 * (((rf - gf) / delta) + 4.0)
    };

    let s = if max < 1e-6 { 0.0 } else { (delta / max) * 255.0 };
    let v = max * 255.0;
    (h.round() as i32, s.round() as i32, v.round() as i32)
}

fn hsv_to_rgb(h: i32, s: i32, v: i32) -> RgbColor {
    let hf = (h.rem_euclid(360)) as f64;
    let sf = (s.clamp(0, 255) as f64) / 255.0;
    let vf = (v.clamp(0, 255) as f64) / 255.0;

    let c = vf * sf;
    let x = c * (1.0 - ((hf / 60.0).rem_euclid(2.0) - 1.0).abs());
    let m = vf - c;

    let (r1, g1, b1) = if hf < 60.0 {
        (c, x, 0.0)
    } else if hf < 120.0 {
        (x, c, 0.0)
    } else if hf < 180.0 {
        (0.0, c, x)
    } else if hf < 240.0 {
        (0.0, x, c)
    } else if hf < 300.0 {
        (x, 0.0, c)
    } else {
        (c, 0.0, x)
    };

    RgbColor::new(
        ((r1 + m) * 255.0).round() as u8,
        ((g1 + m) * 255.0).round() as u8,
        ((b1 + m) * 255.0).round() as u8,
    )
}

fn color_lighter(c: RgbColor, factor: i32) -> RgbColor {
    let (h, s, v) = rgb_to_hsv(c.r, c.g, c.b);
    let new_v = ((v * factor) / 100).clamp(0, 255);
    hsv_to_rgb(h, s, new_v)
}

fn color_darker(c: RgbColor, factor: i32) -> RgbColor {
    let (h, s, v) = rgb_to_hsv(c.r, c.g, c.b);
    let new_v = ((v * 100) / factor.max(1)).clamp(0, 255);
    hsv_to_rgb(h, s, new_v)
}

pub fn extract_dominant_color_from_image(path: &Path) -> Option<RgbColor> {
    let img = image::open(path).ok()?;
    let small = img.thumbnail_exact(64, 64);

    #[derive(Default, Clone, Copy)]
    struct Bucket {
        sum_r: u64,
        sum_g: u64,
        sum_b: u64,
        score: u64,
        count: u64,
    }

    let mut buckets = [Bucket::default(); 12];
    let mut total_r = 0u64;
    let mut total_g = 0u64;
    let mut total_b = 0u64;
    let mut total_count = 0u64;

    for (_x, _y, pixel) in small.pixels() {
        let [r, g, b, _a] = pixel.0;
        let (h, s, v) = rgb_to_hsv(r, g, b);
        if v > 35 && v < 245 {
            total_r += r as u64;
            total_g += g as u64;
            total_b += b as u64;
            total_count += 1;

            if s >= 30 {
                let bucket_idx = ((h.max(0) / 30) as usize) % 12;
                buckets[bucket_idx].sum_r += r as u64;
                buckets[bucket_idx].sum_g += g as u64;
                buckets[bucket_idx].sum_b += b as u64;
                buckets[bucket_idx].score += (s as u64) * (v as u64);
                buckets[bucket_idx].count += 1;
            }
        }
    }

    let mut best_idx = None;
    let mut max_score = 0u64;
    for (i, b) in buckets.iter().enumerate() {
        if b.count > 0 && b.score > max_score {
            max_score = b.score;
            best_idx = Some(i);
        }
    }

    if let Some(idx) = best_idx {
        let b = &buckets[idx];
        let r = (b.sum_r / b.count) as u8;
        let g = (b.sum_g / b.count) as u8;
        let bl = (b.sum_b / b.count) as u8;
        let (h, s, v) = rgb_to_hsv(r, g, bl);
        return Some(hsv_to_rgb(h, s.max(110), v.clamp(120, 215)));
    }

    if total_count > 0 {
        return Some(RgbColor::new(
            (total_r / total_count) as u8,
            (total_g / total_count) as u8,
            (total_b / total_count) as u8,
        ));
    }

    None
}

fn resolve_wallpaper_image_file(raw_path: &str) -> Option<PathBuf> {
    if raw_path.is_empty() {
        return None;
    }
    let path = Path::new(raw_path);
    if !path.exists() {
        return None;
    }
    if path.is_file() {
        return Some(path.to_path_buf());
    }
    if path.is_dir() {
        let screenshots = [
            "contents/screenshot.png",
            "contents/screenshot.jpg",
            "screenshot.png",
            "screenshot.jpg",
        ];
        for s in screenshots {
            let candidate = path.join(s);
            if candidate.exists() {
                return Some(candidate);
            }
        }
        let subdirs = ["contents/images_dark", "contents/images", ""];
        for sub in subdirs {
            let dir = if sub.is_empty() {
                path.to_path_buf()
            } else {
                path.join(sub)
            };
            if let Ok(entries) = fs::read_dir(&dir) {
                for entry in entries.flatten() {
                    let p = entry.path();
                    if p.is_file() {
                        if let Some(ext) = p.extension().and_then(|e| e.to_str()) {
                            if matches!(
                                ext.to_ascii_lowercase().as_str(),
                                "png" | "jpg" | "jpeg" | "webp"
                            ) {
                                return Some(p);
                            }
                        }
                    }
                }
            }
        }
    }
    None
}

static GLOBAL_THEME_MANAGER: Lazy<Mutex<ThemeManager>> =
    Lazy::new(|| Mutex::new(ThemeManager::new()));

#[derive(Debug, Clone)]
pub struct ThemeManager {
    current_theme: AppTheme,
    skin_flavor: SkinFlavor,
    material_you_accent_color: RgbColor,
    wallpaper_path: String,
    wallpaper_name: String,
    wallpaper_status: String,
    settings: AppSettings,
}

impl Default for ThemeManager {
    fn default() -> Self {
        Self::new()
    }
}

impl ThemeManager {
    pub fn new() -> Self {
        let settings = AppSettings::load();
        let current_theme = match settings.theme.to_ascii_lowercase().as_str() {
            "darker" => AppTheme::Darker,
            "light" => AppTheme::Light,
            "reversi" | "ao3reversi" => AppTheme::Ao3Reversi,
            _ => AppTheme::Dark,
        };
        let mut skin_flavor = match settings.skin_flavor.to_ascii_lowercase().as_str() {
            "materialyou" => SkinFlavor::MaterialYou,
            "qt" => SkinFlavor::StandardQt,
            "kde" => SkinFlavor::KdeUi,
            "custom" => SkinFlavor::Custom,
            _ => SkinFlavor::StandardEnsemble,
        };
        if current_theme == AppTheme::Ao3Reversi && skin_flavor == SkinFlavor::MaterialYou {
            skin_flavor = SkinFlavor::StandardEnsemble;
        }

        let mut tm = Self {
            current_theme,
            skin_flavor,
            material_you_accent_color: RgbColor::new(0x79, 0x86, 0xcb),
            wallpaper_path: String::new(),
            wallpaper_name: "Default Palette".to_string(),
            wallpaper_status: "Default Material 3 fallback (#7986cb)".to_string(),
            settings,
        };
        tm.rescan_wallpaper_color();
        tm
    }

    pub fn instance() -> parking_lot::MutexGuard<'static, ThemeManager> {
        GLOBAL_THEME_MANAGER.lock()
    }

    pub fn with_global<F, R>(f: F) -> R
    where
        F: FnOnce(&mut ThemeManager) -> R,
    {
        let mut guard = GLOBAL_THEME_MANAGER.lock();
        f(&mut guard)
    }

    pub fn current_theme(&self) -> AppTheme {
        self.current_theme
    }

    pub fn is_dark_mode(&self) -> bool {
        matches!(
            self.current_theme,
            AppTheme::Dark | AppTheme::Darker | AppTheme::Ao3Reversi
        )
    }

    pub fn is_oled_mode(&self) -> bool {
        self.current_theme == AppTheme::Darker
    }

    pub fn is_ao3_reversi(&self) -> bool {
        self.current_theme == AppTheme::Ao3Reversi
    }

    pub fn is_material_you(&self) -> bool {
        self.skin_flavor == SkinFlavor::MaterialYou
    }

    pub fn current_skin_flavor(&self) -> SkinFlavor {
        self.skin_flavor
    }

    pub fn is_material_you_allowed_for_theme(theme: AppTheme) -> bool {
        theme != AppTheme::Ao3Reversi
    }

    pub fn is_reversi_allowed_for_flavor(flavor: SkinFlavor) -> bool {
        flavor != SkinFlavor::MaterialYou
    }

    pub fn set_theme(&mut self, theme: AppTheme) {
        if theme == AppTheme::Ao3Reversi && self.skin_flavor == SkinFlavor::MaterialYou {
            self.skin_flavor = SkinFlavor::StandardEnsemble;
        }
        self.current_theme = theme;
        self.sync_and_save_settings();
    }

    pub fn set_skin_flavor(&mut self, flavor: SkinFlavor) {
        if flavor == SkinFlavor::MaterialYou && self.current_theme == AppTheme::Ao3Reversi {
            self.current_theme = AppTheme::Dark;
        }
        self.skin_flavor = flavor;
        if flavor == SkinFlavor::MaterialYou && self.wallpaper_path.is_empty() {
            self.rescan_wallpaper_color();
        }
        self.sync_and_save_settings();
    }

    pub fn toggle_theme(&mut self) {
        let is_material_you = self.skin_flavor == SkinFlavor::MaterialYou;
        match self.current_theme {
            AppTheme::Light => self.set_theme(AppTheme::Dark),
            AppTheme::Dark => {
                if is_material_you {
                    self.set_theme(AppTheme::Light);
                } else {
                    self.set_theme(AppTheme::Ao3Reversi);
                }
            }
            AppTheme::Ao3Reversi | AppTheme::Darker => self.set_theme(AppTheme::Light),
        }
    }

    pub fn settings(&self) -> &AppSettings {
        &self.settings
    }

    pub fn settings_mut(&mut self) -> &mut AppSettings {
        &mut self.settings
    }

    pub fn update_settings<F>(&mut self, f: F)
    where
        F: FnOnce(&mut AppSettings),
    {
        f(&mut self.settings);
        self.settings.save();
    }

    pub fn save_settings(&self) {
        self.settings.save();
    }

    fn sync_and_save_settings(&mut self) {
        self.settings.theme = match self.current_theme {
            AppTheme::Darker => "darker".to_string(),
            AppTheme::Light => "light".to_string(),
            AppTheme::Ao3Reversi => "reversi".to_string(),
            AppTheme::Dark => "dark".to_string(),
        };
        self.settings.skin_flavor = match self.skin_flavor {
            SkinFlavor::MaterialYou => "materialyou".to_string(),
            SkinFlavor::StandardQt => "qt".to_string(),
            SkinFlavor::KdeUi => "kde".to_string(),
            SkinFlavor::Custom => "custom".to_string(),
            SkinFlavor::StandardEnsemble => "ensemble".to_string(),
        };
        self.settings.save();
    }

    pub fn material_you_accent_color(&self) -> RgbColor {
        self.material_you_accent_color
    }

    pub fn wallpaper_path(&self) -> &str {
        &self.wallpaper_path
    }

    pub fn wallpaper_name(&self) -> &str {
        &self.wallpaper_name
    }

    pub fn wallpaper_status(&self) -> &str {
        &self.wallpaper_status
    }

    pub fn rescan_wallpaper_color(&mut self) {
        let mut raw_path = String::new();
        let mut source = String::new();

        #[cfg(target_os = "linux")]
        {
            if let Ok(output) = Command::new("qdbus6")
                .args([
                    "org.kde.plasmashell",
                    "/PlasmaShell",
                    "org.kde.PlasmaShell.evaluateScript",
                    "var d = desktops(); for (var i = 0; i < d.length; i++) { d[i].currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General']; var img = d[i].readConfig('Image'); if (img) { print(img); break; } }",
                ])
                .output()
            {
                if output.status.success() {
                    let out = String::from_utf8_lossy(&output.stdout).trim().to_string();
                    if !out.is_empty() && out != "undefined" {
                        raw_path = out;
                        source = "KDE Plasma DBus".to_string();
                    }
                }
            }

            if raw_path.is_empty() {
                if let Some(home) = std::env::var_os("HOME") {
                    let plasma_cfg =
                        Path::new(&home).join(".config/plasma-org.kde.plasma.desktop-appletsrc");
                    if let Ok(text) = fs::read_to_string(&plasma_cfg) {
                        for line in text.lines() {
                            let trimmed = line.trim();
                            if let Some(val) = trimmed
                                .strip_prefix("Image=")
                                .or_else(|| trimmed.strip_prefix("wallpaper="))
                            {
                                if !val.trim().is_empty() {
                                    raw_path = val.trim().to_string();
                                    source = "KDE Plasma config file".to_string();
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if raw_path.is_empty() {
                let key = if self.is_dark_mode() {
                    "picture-uri-dark"
                } else {
                    "picture-uri"
                };
                if let Ok(output) = Command::new("gsettings")
                    .args(["get", "org.gnome.desktop.background", key])
                    .output()
                {
                    if output.status.success() {
                        let out = String::from_utf8_lossy(&output.stdout).trim().to_string();
                        if !out.is_empty() && out != "''" && out != "\"\"" {
                            raw_path = out;
                            source = "GNOME gsettings".to_string();
                        }
                    }
                }
            }
        }

        let cleaned = raw_path
            .trim_matches(|c| c == '\'' || c == '"' || c == ' ')
            .strip_prefix("file://")
            .unwrap_or_else(|| raw_path.trim_matches(|c| c == '\'' || c == '"' || c == ' '))
            .trim_end_matches('/');

        if let Some(img_file) = resolve_wallpaper_image_file(cleaned) {
            if let Some(extracted) = extract_dominant_color_from_image(&img_file) {
                self.material_you_accent_color = extracted;
                self.wallpaper_path = img_file.to_string_lossy().to_string();
                self.wallpaper_name = img_file
                    .file_stem()
                    .and_then(|s| s.to_str())
                    .unwrap_or("Wallpaper")
                    .to_string();
                self.wallpaper_status = format!("Extracted from {}: {}", source, self.wallpaper_name);
                return;
            }
        }

        self.material_you_accent_color = RgbColor::new(0x79, 0x86, 0xcb);
        self.wallpaper_path.clear();
        self.wallpaper_name = "Default Palette".to_string();
        self.wallpaper_status = "Default Material 3 fallback (#7986cb)".to_string();
    }

    pub fn button_radius(&self) -> i32 {
        match self.skin_flavor {
            SkinFlavor::MaterialYou => 20,
            SkinFlavor::KdeUi => 3,
            SkinFlavor::StandardQt => 4,
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => 6,
        }
    }

    pub fn card_radius(&self) -> i32 {
        match self.skin_flavor {
            SkinFlavor::MaterialYou => 22,
            SkinFlavor::KdeUi => 4,
            SkinFlavor::StandardQt => 6,
            SkinFlavor::StandardEnsemble => 12,
            SkinFlavor::Custom => 8,
        }
    }

    pub fn input_radius(&self) -> i32 {
        match self.skin_flavor {
            SkinFlavor::MaterialYou => 14,
            SkinFlavor::KdeUi => 3,
            SkinFlavor::StandardQt => 4,
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => 6,
        }
    }

    pub fn tab_radius(&self) -> i32 {
        match self.skin_flavor {
            SkinFlavor::MaterialYou => 18,
            SkinFlavor::KdeUi => 3,
            SkinFlavor::StandardQt => 4,
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => 6,
        }
    }

    pub fn button_padding(&self) -> &'static str {
        match self.skin_flavor {
            SkinFlavor::MaterialYou => "8px 22px",
            SkinFlavor::KdeUi => "6px 14px",
            SkinFlavor::StandardQt => "6px 16px",
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => "7px 16px",
        }
    }

    pub fn window_bg_color(&self) -> RgbColor {
        if self.is_oled_mode() {
            return RgbColor::new(0x00, 0x00, 0x00);
        }
        if self.is_ao3_reversi() {
            return RgbColor::new(0x11, 0x11, 0x11);
        }
        match self.skin_flavor {
            SkinFlavor::KdeUi => {
                if self.is_dark_mode() {
                    RgbColor::new(0x23, 0x26, 0x29)
                } else {
                    RgbColor::new(0xef, 0xf0, 0xf1)
                }
            }
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x2b, 0x2b, 0x2b)
                } else {
                    RgbColor::new(0xf0, 0xf0, 0xf0)
                }
            }
            SkinFlavor::MaterialYou => {
                if self.is_dark_mode() {
                    RgbColor::new(0x13, 0x15, 0x1b)
                } else {
                    RgbColor::new(0xf7, 0xf8, 0xfc)
                }
            }
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => {
                if self.is_dark_mode() {
                    RgbColor::new(0x14, 0x15, 0x1a)
                } else {
                    RgbColor::new(0xf8, 0xfa, 0xfc)
                }
            }
        }
    }

    pub fn surface_bg_color(&self) -> RgbColor {
        if self.is_oled_mode() {
            return RgbColor::new(0x08, 0x08, 0x0a);
        }
        if self.is_ao3_reversi() {
            return RgbColor::new(0x1e, 0x1e, 0x1e);
        }
        match self.skin_flavor {
            SkinFlavor::KdeUi => {
                if self.is_dark_mode() {
                    RgbColor::new(0x2a, 0x2e, 0x32)
                } else {
                    RgbColor::new(0xf7, 0xf7, 0xf7)
                }
            }
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x32, 0x32, 0x32)
                } else {
                    RgbColor::new(0xe6, 0xe6, 0xe6)
                }
            }
            SkinFlavor::MaterialYou => {
                if self.is_dark_mode() {
                    RgbColor::new(0x1c, 0x1e, 0x27)
                } else {
                    RgbColor::new(0xee, 0xf1, 0xf7)
                }
            }
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => {
                if self.is_dark_mode() {
                    RgbColor::new(0x1a, 0x1b, 0x23)
                } else {
                    RgbColor::new(0xf1, 0xf5, 0xf9)
                }
            }
        }
    }

    pub fn card_bg_color(&self) -> RgbColor {
        if self.is_oled_mode() {
            return RgbColor::new(0x10, 0x10, 0x14);
        }
        if self.is_ao3_reversi() {
            return RgbColor::new(0x26, 0x26, 0x26);
        }
        match self.skin_flavor {
            SkinFlavor::KdeUi => {
                if self.is_dark_mode() {
                    RgbColor::new(0x31, 0x36, 0x3b)
                } else {
                    RgbColor::new(0xff, 0xff, 0xff)
                }
            }
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x3c, 0x3c, 0x3c)
                } else {
                    RgbColor::new(0xff, 0xff, 0xff)
                }
            }
            SkinFlavor::MaterialYou => {
                if self.is_dark_mode() {
                    RgbColor::new(0x24, 0x27, 0x33)
                } else {
                    RgbColor::new(0xff, 0xff, 0xff)
                }
            }
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => {
                if self.is_dark_mode() {
                    RgbColor::new(0x21, 0x22, 0x2c)
                } else {
                    RgbColor::new(0xff, 0xff, 0xff)
                }
            }
        }
    }

    pub fn primary_text_color(&self) -> RgbColor {
        if self.is_oled_mode() {
            return RgbColor::new(0xff, 0xff, 0xff);
        }
        if self.is_ao3_reversi() {
            return RgbColor::new(0xee, 0xee, 0xee);
        }
        if self.is_dark_mode() {
            RgbColor::new(0xf8, 0xf8, 0xf2)
        } else {
            RgbColor::new(0x0f, 0x17, 0x2a)
        }
    }

    pub fn muted_text_color(&self) -> RgbColor {
        if self.is_oled_mode() {
            return RgbColor::new(0xa1, 0xa1, 0xaa);
        }
        if self.is_ao3_reversi() {
            return RgbColor::new(0xaa, 0xaa, 0xaa);
        }
        if self.is_dark_mode() {
            RgbColor::new(0x94, 0xa3, 0xb8)
        } else {
            RgbColor::new(0x33, 0x41, 0x55)
        }
    }

    pub fn accent_color(&self) -> RgbColor {
        if self.is_ao3_reversi() {
            return RgbColor::new(0x99, 0x00, 0x00);
        }
        match self.skin_flavor {
            SkinFlavor::MaterialYou => self.material_you_accent_color,
            SkinFlavor::KdeUi => RgbColor::new(0x3d, 0xae, 0xe9),
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x4a, 0x90, 0xe2)
                } else {
                    RgbColor::new(0x29, 0x80, 0xb9)
                }
            }
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => {
                if self.is_dark_mode() {
                    RgbColor::new(0x7f, 0x5a, 0xf0)
                } else {
                    RgbColor::new(0x63, 0x66, 0xf1)
                }
            }
        }
    }

    pub fn accent_hover_color(&self) -> RgbColor {
        if self.is_ao3_reversi() {
            return RgbColor::new(0xb3, 0x00, 0x00);
        }
        match self.skin_flavor {
            SkinFlavor::MaterialYou => {
                if self.is_dark_mode() {
                    color_lighter(self.material_you_accent_color, 118)
                } else {
                    color_darker(self.material_you_accent_color, 115)
                }
            }
            SkinFlavor::KdeUi => RgbColor::new(0x29, 0x80, 0xb9),
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x6b, 0xa4, 0xe8)
                } else {
                    RgbColor::new(0x34, 0x98, 0xdb)
                }
            }
            SkinFlavor::StandardEnsemble | SkinFlavor::Custom => {
                if self.is_dark_mode() {
                    RgbColor::new(0x6c, 0x47, 0xd9)
                } else {
                    RgbColor::new(0x4f, 0x46, 0xe5)
                }
            }
        }
    }

    pub fn border_color(&self) -> RgbColor {
        if self.is_oled_mode() {
            return RgbColor::new(0x20, 0x20, 0x26);
        }
        if self.is_ao3_reversi() {
            return RgbColor::new(0x38, 0x38, 0x38);
        }
        match self.skin_flavor {
            SkinFlavor::KdeUi => {
                if self.is_dark_mode() {
                    RgbColor::new(0x31, 0x36, 0x3b)
                } else {
                    RgbColor::new(0xbd, 0xc3, 0xc7)
                }
            }
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x44, 0x44, 0x44)
                } else {
                    RgbColor::new(0xcc, 0xcc, 0xcc)
                }
            }
            _ => {
                if self.is_dark_mode() {
                    RgbColor::new(0x2e, 0x2f, 0x3e)
                } else {
                    RgbColor::new(0xcb, 0xd5, 0xe1)
                }
            }
        }
    }

    pub fn selection_bg_color(&self) -> RgbColor {
        if self.is_ao3_reversi() {
            return RgbColor::new(0x59, 0x1a, 0x1a);
        }
        match self.skin_flavor {
            SkinFlavor::MaterialYou => {
                let sel = self.material_you_accent_color;
                let (_, _, v) = rgb_to_hsv(sel.r, sel.g, sel.b);
                if self.is_dark_mode() {
                    sel
                } else if v > 180 {
                    color_darker(sel, 120)
                } else {
                    sel
                }
            }
            SkinFlavor::KdeUi => {
                if self.is_dark_mode() {
                    RgbColor::new(0x2d, 0x5c, 0x7f)
                } else {
                    RgbColor::new(0x29, 0x80, 0xb9)
                }
            }
            SkinFlavor::StandardQt => {
                if self.is_dark_mode() {
                    RgbColor::new(0x26, 0x4f, 0x78)
                } else {
                    RgbColor::new(0x29, 0x80, 0xb9)
                }
            }
            _ => {
                if self.is_dark_mode() {
                    RgbColor::new(0x44, 0x47, 0x5a)
                } else {
                    RgbColor::new(0x43, 0x38, 0xca)
                }
            }
        }
    }

    pub fn selection_text_color(&self) -> RgbColor {
        let bg = self.selection_bg_color();
        let lum = 0.2126 * (bg.r as f64 / 255.0)
            + 0.7152 * (bg.g as f64 / 255.0)
            + 0.0722 * (bg.b as f64 / 255.0);
        if lum > 0.45 {
            RgbColor::new(0x0f, 0x17, 0x2a)
        } else {
            RgbColor::new(0xff, 0xff, 0xff)
        }
    }

    pub fn selection_bg_css(&self) -> String {
        self.selection_bg_color().to_hex()
    }

    pub fn get_editor_pane_style_sheet(&self) -> String {
        let bg = if self.is_oled_mode() {
            "#050505"
        } else if self.is_ao3_reversi() {
            "#181818"
        } else if self.is_dark_mode() {
            "#181920"
        } else {
            "#ffffff"
        };
        format!(
            "QTextEdit {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; selection-background-color: {}; selection-color: {}; padding: 14px; }}",
            bg,
            self.primary_text_color().to_hex(),
            self.border_color().to_hex(),
            self.card_radius(),
            self.selection_bg_css(),
            self.selection_text_color().to_hex()
        )
    }

    pub fn get_code_editor_style_sheet(&self) -> String {
        let bg = if self.is_oled_mode() {
            "#000000"
        } else if self.is_ao3_reversi() {
            "#141414"
        } else if self.is_dark_mode() {
            "#14151a"
        } else {
            "#ffffff"
        };
        format!(
            "QPlainTextEdit {{ background-color: {}; color: {}; border: 1px solid {}; border-radius: {}px; selection-background-color: {}; selection-color: {}; font-family: 'Consolas', 'Monospace', monospace; }}",
            bg,
            self.primary_text_color().to_hex(),
            self.border_color().to_hex(),
            self.input_radius(),
            self.selection_bg_css(),
            self.selection_text_color().to_hex()
        )
    }
}
