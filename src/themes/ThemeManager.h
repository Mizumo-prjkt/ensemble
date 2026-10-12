#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

enum class AppTheme {
    Dark,
    Darker, // OLED deep black
    Light
};

enum class SkinFlavor {
    MaterialYou,
    StandardEnsemble,
    StandardQt,
    KdeUi,
    Custom
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager &instance();

    AppTheme currentTheme() const { return m_currentTheme; }
    bool isDarkMode() const { return m_currentTheme == AppTheme::Dark || m_currentTheme == AppTheme::Darker; }
    bool isOledMode() const { return m_currentTheme == AppTheme::Darker; }

    void setTheme(AppTheme theme);
    void toggleTheme();

    SkinFlavor currentSkinFlavor() const { return m_skinFlavor; }
    void setSkinFlavor(SkinFlavor flavor);

    QColor materialYouAccentColor() const { return m_materialYouAccentColor; }
    QString wallpaperPath() const { return m_wallpaperPath; }
    QString wallpaperName() const { return m_wallpaperName; }
    QString wallpaperStatus() const { return m_wallpaperStatus; }
    void rescanWallpaperColor();
    void applyCurrentThemeAndSkin();

    // Geometric metrics based on active SkinFlavor
    int buttonRadius() const;
    int cardRadius() const;
    int inputRadius() const;
    int tabRadius() const;
    QString buttonPadding() const;

    // Palette
    QPalette getPalette() const;

    // Style sheets for components
    QString getApplicationStyleSheet() const;
    QString getMainMenuStyleSheet() const;
    QString getEditorPaneStyleSheet() const;
    QString getSidebarStyleSheet() const;
    QString getCodeEditorStyleSheet() const;
    QString getStatusBarStyleSheet() const;
    QString getDialogStyleSheet() const;
    QString getProblemsDialogStyleSheet() const;

    // Palette color tokens
    QColor windowBgColor() const;
    QColor surfaceBgColor() const;
    QColor cardBgColor() const;
    QColor primaryTextColor() const;
    QColor mutedTextColor() const;
    QColor accentColor() const;
    QColor accentHoverColor() const;
    QColor borderColor() const;
    QColor selectionBgColor() const;
    QColor selectionTextColor() const;
    QString selectionBgCss() const;
    QColor primaryButtonBgColor() const;
    QColor primaryButtonTextColor() const;

    // Syntax highlighting colors
    QColor syntaxTagColor() const;
    QColor syntaxAttributeColor() const;
    QColor syntaxValueColor() const;
    QColor syntaxCommentColor() const;
    QColor syntaxEntityColor() const;
    QColor syntaxSelectorColor() const;
    QColor syntaxPropertyColor() const;

signals:
    void themeChanged(AppTheme newTheme);
    void skinFlavorChanged(SkinFlavor newFlavor);

private:
    explicit ThemeManager(QObject *parent = nullptr);
    void loadSettings();
    void saveSettings();

    AppTheme m_currentTheme = AppTheme::Dark;
    SkinFlavor m_skinFlavor = SkinFlavor::StandardEnsemble;
    QColor m_materialYouAccentColor = QColor(0x79, 0x86, 0xcb);
    QString m_wallpaperPath;
    QString m_wallpaperName;
    QString m_wallpaperStatus;
};
