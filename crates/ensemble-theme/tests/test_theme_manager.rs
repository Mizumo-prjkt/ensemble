use ensemble_core::RgbColor;
use ensemble_theme::{AppTheme, SkinFlavor, ThemeManager};

#[test]
fn test_theme_manager_and_mutual_exclusivity() {
    let mut tm = ThemeManager::new();

    // 1. Test OLED Darker mode
    tm.set_theme(AppTheme::Darker);
    assert_eq!(tm.current_theme(), AppTheme::Darker);
    assert!(tm.is_dark_mode());
    assert!(tm.is_oled_mode());
    assert_eq!(tm.window_bg_color(), RgbColor::new(0x00, 0x00, 0x00));

    // 2. Test SkinFlavor and Material You wallpaper scanning
    tm.set_skin_flavor(SkinFlavor::MaterialYou);
    assert_eq!(tm.current_skin_flavor(), SkinFlavor::MaterialYou);
    tm.rescan_wallpaper_color();
    assert!(!tm.wallpaper_status().is_empty());

    // 3. Test Light mode high-contrast text tokens
    tm.set_theme(AppTheme::Light);
    assert_eq!(
        tm.primary_text_color(),
        RgbColor::from_css_str("#0f172a").unwrap()
    );
    assert_eq!(
        tm.muted_text_color(),
        RgbColor::from_css_str("#334155").unwrap()
    );

    // 4. Test AO3 Reversi theme and Material You conflict resolution
    tm.set_theme(AppTheme::Ao3Reversi);
    assert_eq!(tm.current_theme(), AppTheme::Ao3Reversi);
    assert!(tm.is_dark_mode());
    assert!(tm.is_ao3_reversi());
    assert_eq!(
        tm.accent_color(),
        RgbColor::from_css_str("#990000").unwrap()
    );
    assert_eq!(
        tm.window_bg_color(),
        RgbColor::from_css_str("#111111").unwrap()
    );
    // Switching to Reversi must disable/switch away from Material You
    assert_eq!(tm.current_skin_flavor(), SkinFlavor::StandardEnsemble);
    assert!(!ThemeManager::is_material_you_allowed_for_theme(
        AppTheme::Ao3Reversi
    ));

    // 5. Selecting Material You must disallow Reversi and switch away from Reversi to Dark
    tm.set_skin_flavor(SkinFlavor::MaterialYou);
    assert_eq!(tm.current_skin_flavor(), SkinFlavor::MaterialYou);
    assert_eq!(tm.current_theme(), AppTheme::Dark);
    assert!(!ThemeManager::is_reversi_allowed_for_flavor(
        SkinFlavor::MaterialYou
    ));

    // 6. Test toggle_theme() with Material You active (skips Reversi)
    tm.set_theme(AppTheme::Light);
    tm.toggle_theme();
    assert_eq!(tm.current_theme(), AppTheme::Dark);
    tm.toggle_theme();
    assert_eq!(tm.current_theme(), AppTheme::Light); // Skipped Ao3Reversi!

    // 7. Test global theme toggle cycle with StandardEnsemble (Light -> Dark -> Ao3Reversi -> Light)
    tm.set_skin_flavor(SkinFlavor::StandardEnsemble);
    tm.set_theme(AppTheme::Light);
    tm.toggle_theme();
    assert_eq!(tm.current_theme(), AppTheme::Dark);
    tm.toggle_theme();
    assert_eq!(tm.current_theme(), AppTheme::Ao3Reversi);
    tm.toggle_theme();
    assert_eq!(tm.current_theme(), AppTheme::Light);

    // 8. Test SkinFlavor geometry metrics and styling
    tm.set_skin_flavor(SkinFlavor::MaterialYou);
    assert_eq!(tm.button_radius(), 20);
    assert_eq!(tm.tab_radius(), 18);
    assert_eq!(tm.card_radius(), 22);
    assert!(!tm.selection_bg_css().is_empty());
    assert!(tm
        .get_editor_pane_style_sheet()
        .contains("selection-background-color"));
    assert!(tm
        .get_code_editor_style_sheet()
        .contains("selection-background-color"));

    tm.set_skin_flavor(SkinFlavor::KdeUi);
    assert_eq!(tm.button_radius(), 3);
    assert_eq!(tm.tab_radius(), 3);
    assert_eq!(tm.card_radius(), 4);
    assert_eq!(
        tm.accent_color(),
        RgbColor::from_css_str("#3daee9").unwrap()
    );

    tm.set_skin_flavor(SkinFlavor::StandardQt);
    assert_eq!(tm.button_radius(), 4);
    assert_eq!(tm.tab_radius(), 4);
    assert_eq!(tm.card_radius(), 6);
}
