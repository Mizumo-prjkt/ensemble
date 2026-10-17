#include "SettingsDialog.h"
#include "AppIcon.h"
#include "HelpGuideDialog.h"
#include "themes/ThemeManager.h"
#include "mcp/McpServer.h"
#include "mcp/McpAgentInstaller.h"
#include "debug/debug.hpp"
#include "ui/PluginManagerDialog.h"
#include "utils/AppPaths.h"

#include <QApplication>
#include <QDesktopServices>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

static QScrollArea *createScrollableContainer(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    return scroll;
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_mcpServer(nullptr)
{
    setWindowTitle(QStringLiteral("Preferences"));
    setWindowIcon(AppIcon::icon());
    setModal(true);
    resize(740, 640);
    setMinimumSize(640, 520);

    setupUi();
    updateTheme();
    updateMcpStatusDisplay();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateTheme();
    });
    connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this, [this](SkinFlavor) {
        updateTheme();
    });
}

void SettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    // Header banner
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto *headerIconLabel = new QLabel(this);
    headerIconLabel->setPixmap(AppIcon::pixmap(36, 36));
    headerLayout->addWidget(headerIconLabel);

    auto *headerTextLayout = new QVBoxLayout();
    headerTextLayout->setSpacing(2);

    auto *headerTitle = new QLabel(QStringLiteral("<b>Preferences &amp; Settings</b>"), this);
    headerTitle->setStyleSheet(QStringLiteral("font-size: 16px;"));

    auto *headerSubtitle = new QLabel(
        QStringLiteral("Configure appearance, rendering engine, AI assistance, and plugins."), this);
    headerSubtitle->setWordWrap(true);
    headerSubtitle->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));

    headerTextLayout->addWidget(headerTitle);
    headerTextLayout->addWidget(headerSubtitle);
    headerLayout->addLayout(headerTextLayout, 1);

    mainLayout->addLayout(headerLayout);

    // Tab Widget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createGeneralTab(), QStringLiteral("🎨 General"));
    m_tabWidget->addTab(createEditorTab(), QStringLiteral("✍️ Editor"));
    m_tabWidget->addTab(createWebEngineTab(), QStringLiteral("🌐 WebEngine"));
    m_tabWidget->addTab(createAiTab(), QStringLiteral("🤖 AI Settings"));
    m_tabWidget->addTab(createPluginsTab(), QStringLiteral("🧩 Plugins"));

    mainLayout->addWidget(m_tabWidget, 1);

    // Footer actions
    auto *footerLayout = new QHBoxLayout();
    footerLayout->addStretch(1);

    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    m_closeBtn->setObjectName(QStringLiteral("closeBtn"));
    m_closeBtn->setDefault(true);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footerLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(footerLayout);
}

QWidget *SettingsDialog::createGeneralTab()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(14);

    // --- Section 1: Theme ---
    auto *themeGroup = new QGroupBox(QStringLiteral("Theme"), container);
    auto *themeLayout = new QVBoxLayout(themeGroup);
    themeLayout->setSpacing(8);

    auto *themeDesc = new QLabel(
        QStringLiteral("Select the visual color scheme for the application:"), themeGroup);
    themeDesc->setWordWrap(true);
    themeDesc->setStyleSheet(QStringLiteral("font-size: 11px;"));
    themeLayout->addWidget(themeDesc);

    m_themeDarkRadio = new QRadioButton(
        QStringLiteral("🌙  Dark (Deep slate color scheme)"), themeGroup);
    m_themeDarkerRadio = new QRadioButton(
        QStringLiteral("🖤  Darker (OLED pure black #000000 background)"), themeGroup);
    m_themeLightRadio = new QRadioButton(
        QStringLiteral("☀️  Light (Clean high-contrast bright theme)"), themeGroup);
    m_themeReversiRadio = new QRadioButton(
        QStringLiteral("🔴  AO3 Reversi (Official AO3 dark crimson theme)"), themeGroup);

    m_themeGroup = new QButtonGroup(this);
    m_themeGroup->addButton(m_themeDarkRadio, static_cast<int>(AppTheme::Dark));
    m_themeGroup->addButton(m_themeDarkerRadio, static_cast<int>(AppTheme::Darker));
    m_themeGroup->addButton(m_themeLightRadio, static_cast<int>(AppTheme::Light));
    m_themeGroup->addButton(m_themeReversiRadio, static_cast<int>(AppTheme::Ao3Reversi));

    AppTheme curTheme = ThemeManager::instance().currentTheme();
    if (curTheme == AppTheme::Dark) {
        m_themeDarkRadio->setChecked(true);
    } else if (curTheme == AppTheme::Darker) {
        m_themeDarkerRadio->setChecked(true);
    } else if (curTheme == AppTheme::Ao3Reversi) {
        m_themeReversiRadio->setChecked(true);
    } else {
        m_themeLightRadio->setChecked(true);
    }

    connect(m_themeDarkRadio, &QRadioButton::toggled, this, &SettingsDialog::onThemeSelectionChanged);
    connect(m_themeDarkerRadio, &QRadioButton::toggled, this, &SettingsDialog::onThemeSelectionChanged);
    connect(m_themeLightRadio, &QRadioButton::toggled, this, &SettingsDialog::onThemeSelectionChanged);
    connect(m_themeReversiRadio, &QRadioButton::toggled, this, &SettingsDialog::onThemeSelectionChanged);

    themeLayout->addWidget(m_themeLightRadio);
    themeLayout->addWidget(m_themeDarkRadio);
    themeLayout->addWidget(m_themeReversiRadio);
    themeLayout->addWidget(m_themeDarkerRadio);
    layout->addWidget(themeGroup);

    // --- Section 2: Skin Flavor ---
    auto *flavorGroup = new QGroupBox(QStringLiteral("Skin Flavor && Design System"), container);
    auto *flavorLayout = new QVBoxLayout(flavorGroup);
    flavorLayout->setSpacing(10);

    auto *flavorDesc = new QLabel(
        QStringLiteral("Select UI component flavor and styling accents:"), flavorGroup);
    flavorDesc->setWordWrap(true);
    flavorDesc->setStyleSheet(QStringLiteral("font-size: 11px;"));
    flavorLayout->addWidget(flavorDesc);

    m_skinFlavorCombo = new QComboBox(flavorGroup);
    m_skinFlavorCombo->addItem(
        QStringLiteral("🤖  Android Material You (Dynamic Wallpaper Extraction)"),
        static_cast<int>(SkinFlavor::MaterialYou));
    m_skinFlavorCombo->addItem(
        QStringLiteral("✨  Standard Ensemble Skin (Curated Violet)"),
        static_cast<int>(SkinFlavor::StandardEnsemble));
    m_skinFlavorCombo->addItem(
        QStringLiteral("🖥️  Standard Qt (Native Desktop Widgets)"),
        static_cast<int>(SkinFlavor::StandardQt));
    m_skinFlavorCombo->addItem(
        QStringLiteral("🐧  KDE UI (Breeze Design Integration)"),
        static_cast<int>(SkinFlavor::KdeUi));
    m_skinFlavorCombo->addItem(
        QStringLiteral("🎨  Custom [TODO: Coming soon]"),
        static_cast<int>(SkinFlavor::Custom));

    // Select current flavor
    SkinFlavor curFlavor = ThemeManager::instance().currentSkinFlavor();
    for (int i = 0; i < m_skinFlavorCombo->count(); ++i) {
        if (m_skinFlavorCombo->itemData(i).toInt() == static_cast<int>(curFlavor)) {
            m_skinFlavorCombo->setCurrentIndex(i);
            break;
        }
    }

    connect(m_skinFlavorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onSkinFlavorSelectionChanged);
    flavorLayout->addWidget(m_skinFlavorCombo);

    // Material You dynamic details card
    m_materialYouCard = new QFrame(flavorGroup);
    m_materialYouCard->setObjectName(QStringLiteral("materialYouCard"));
    auto *myLayout = new QVBoxLayout(m_materialYouCard);
    myLayout->setContentsMargins(14, 14, 14, 14);
    myLayout->setSpacing(10);

    auto *myInfoLabel = new QLabel(
        QStringLiteral("Extracts the dominant accent hue directly from your active desktop wallpaper "
                       "(GNOME / KDE / Freedesktop accent) to synthesize dynamic Material 3 colors:"),
        m_materialYouCard);
    myInfoLabel->setWordWrap(true);
    myInfoLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    myLayout->addWidget(myInfoLabel);

    auto *swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(10);

    m_wallpaperColorPreview = new QLabel(m_materialYouCard);
    m_wallpaperColorPreview->setFixedSize(28, 28);
    swatchRow->addWidget(m_wallpaperColorPreview);

    auto *colorInfoLayout = new QVBoxLayout();
    colorInfoLayout->setSpacing(2);

    m_wallpaperHexLabel = new QLabel(m_materialYouCard);
    m_wallpaperHexLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-family: monospace; font-size: 12px;"));
    colorInfoLayout->addWidget(m_wallpaperHexLabel);

    m_wallpaperStatusLabel = new QLabel(m_materialYouCard);
    m_wallpaperStatusLabel->setWordWrap(true);
    m_wallpaperStatusLabel->setStyleSheet(QStringLiteral("font-size: 10px; opacity: 0.85;"));
    colorInfoLayout->addWidget(m_wallpaperStatusLabel);

    swatchRow->addLayout(colorInfoLayout);
    swatchRow->addStretch(1);

    m_rescanWallpaperBtn = new QPushButton(QStringLiteral("🔄 Re-scan Wallpaper Color"), m_materialYouCard);
    m_rescanWallpaperBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rescanWallpaperBtn, &QPushButton::clicked, this, &SettingsDialog::onRescanWallpaperClicked);
    swatchRow->addWidget(m_rescanWallpaperBtn);

    myLayout->addLayout(swatchRow);
    flavorLayout->addWidget(m_materialYouCard);

    m_materialYouCard->setVisible(curFlavor == SkinFlavor::MaterialYou);
    updateWallpaperColorPreview();

    layout->addWidget(flavorGroup);
    layout->addStretch(1);

    return createScrollableContainer(container, this);
}

QWidget *SettingsDialog::createEditorTab()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(14);

    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));

    // --- Section 1: Hover Inspection & Embedded Elements ---
    auto *hoverGroup = new QGroupBox(QStringLiteral("Element && Tag Inspection on Hover"), container);
    auto *hoverLayout = new QVBoxLayout(hoverGroup);
    hoverLayout->setSpacing(8);

    m_hoverElementInfoCheck = new QCheckBox(
        QStringLiteral("Show embedded element, tag, and CSS class info on hover"), hoverGroup);
    m_hoverElementInfoCheck->setChecked(settings.value(QStringLiteral("editor/hoverElementInfo"), true).toBool());
    connect(m_hoverElementInfoCheck, &QCheckBox::toggled, this, &SettingsDialog::onEditorSettingToggled);
    hoverLayout->addWidget(m_hoverElementInfoCheck);

    auto *hoverDesc = new QLabel(
        QStringLiteral("When hovering over formatted text, displays a tooltip showing enclosing HTML tags "
                       "and CSS classes (e.g., <p> → <span class=\"ichika\">, links, headings, or inline styles)."),
        hoverGroup);
    hoverDesc->setWordWrap(true);
    hoverDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    hoverLayout->addWidget(hoverDesc);

    layout->addWidget(hoverGroup);

    // --- Section 2: Error & Problem Diagnostics ---
    auto *diagGroup = new QGroupBox(QStringLiteral("Issue Highlighting && Error Diagnostics"), container);
    auto *diagLayout = new QVBoxLayout(diagGroup);
    diagLayout->setSpacing(8);

    m_highlightErrorsCheck = new QCheckBox(
        QStringLiteral("Highlight errors, undefined classes, and warnings directly in editor"), diagGroup);
    m_highlightErrorsCheck->setChecked(settings.value(QStringLiteral("editor/highlightErrors"), true).toBool());
    connect(m_highlightErrorsCheck, &QCheckBox::toggled, this, &SettingsDialog::onEditorSettingToggled);
    diagLayout->addWidget(m_highlightErrorsCheck);

    auto *diagDesc = new QLabel(
        QStringLiteral("Draws wavy colored underlays beneath undefined CSS classes, duplicate selectors in Work Skin CSS, "
                       "and disallowed HTML tags directly in the code."),
        diagGroup);
    diagDesc->setWordWrap(true);
    diagDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    diagLayout->addWidget(diagDesc);

    m_hoverErrorDetailsCheck = new QCheckBox(
        QStringLiteral("Show detailed problem explanations when hovering over issues"), diagGroup);
    m_hoverErrorDetailsCheck->setChecked(settings.value(QStringLiteral("editor/hoverErrorDetails"), true).toBool());
    connect(m_hoverErrorDetailsCheck, &QCheckBox::toggled, this, &SettingsDialog::onEditorSettingToggled);
    diagLayout->addWidget(m_hoverErrorDetailsCheck);

    auto *detailsDesc = new QLabel(
        QStringLiteral("Hovering over any highlighted warning or error displays a tooltip explaining why the issue occurred "
                       "and how to fix it."),
        diagGroup);
    detailsDesc->setWordWrap(true);
    detailsDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    diagLayout->addWidget(detailsDesc);

    layout->addWidget(diagGroup);

    // --- Section 3: Raw HTML Formatting ---
    auto *formatGroup = new QGroupBox(QStringLiteral("Raw HTML Source Formatting"), container);
    auto *formatLayout = new QVBoxLayout(formatGroup);
    formatLayout->setSpacing(8);

    m_autoFormatHtmlCheck = new QCheckBox(
        QStringLiteral("Auto-format Raw HTML (split paragraphs across lines)"), formatGroup);
    m_autoFormatHtmlCheck->setChecked(settings.value(QStringLiteral("editor/autoFormatHtml"), true).toBool());
    connect(m_autoFormatHtmlCheck, &QCheckBox::toggled, this, &SettingsDialog::onEditorSettingToggled);
    formatLayout->addWidget(m_autoFormatHtmlCheck);

    auto *formatDesc = new QLabel(
        QStringLiteral("Automatically formats adjacent paragraph blocks (<p>, <div>, <blockquote>) onto separate lines "
                       "to prevent chapters from displaying as giant single-line blocks in Raw HTML mode."),
        formatGroup);
    formatDesc->setWordWrap(true);
    formatDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    formatLayout->addWidget(formatDesc);

    layout->addWidget(formatGroup);
    layout->addStretch(1);

    return createScrollableContainer(container, this);
}

void SettingsDialog::onEditorSettingToggled()
{
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    if (m_hoverElementInfoCheck)
        settings.setValue(QStringLiteral("editor/hoverElementInfo"), m_hoverElementInfoCheck->isChecked());
    if (m_highlightErrorsCheck)
        settings.setValue(QStringLiteral("editor/highlightErrors"), m_highlightErrorsCheck->isChecked());
    if (m_hoverErrorDetailsCheck)
        settings.setValue(QStringLiteral("editor/hoverErrorDetails"), m_hoverErrorDetailsCheck->isChecked());
    if (m_autoFormatHtmlCheck)
        settings.setValue(QStringLiteral("editor/autoFormatHtml"), m_autoFormatHtmlCheck->isChecked());

    emit editorSettingsChanged();
}

QWidget *SettingsDialog::createWebEngineTab()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(14);

    // --- Section 1: AO3 Simulation ---
    auto *ao3Group = new QGroupBox(QStringLiteral("AO3 Page Simulation"), container);
    auto *ao3Layout = new QVBoxLayout(ao3Group);
    ao3Layout->setSpacing(6);

    m_mimicAo3Check = new QCheckBox(
        QStringLiteral("[TODO: Coming soon] Allow Mimicking The whole AO3 Body page"), ao3Group);
    m_mimicAo3Check->setEnabled(false);
    ao3Layout->addWidget(m_mimicAo3Check);

    auto *ao3Desc = new QLabel(
        QStringLiteral("Renders the full Archive of Our Own page chrome (header banner, author summary, "
                       "work tags, kudos, and comment container) inside the live preview pane."),
        ao3Group);
    ao3Desc->setWordWrap(true);
    ao3Desc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    ao3Layout->addWidget(ao3Desc);

    layout->addWidget(ao3Group);

    // --- Section 2: Hardware GPU Acceleration ---
    auto *gpuGroup = new QGroupBox(QStringLiteral("Hardware Acceleration"), container);
    auto *gpuLayout = new QVBoxLayout(gpuGroup);
    gpuLayout->setSpacing(6);

    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    const bool gpuEnabled = settings.value(QStringLiteral("webengine/gpuAcceleration"), true).toBool();

    m_gpuAccelCheck = new QCheckBox(
        QStringLiteral("Hardware GPU Acceleration (Shared OpenGL Contexts)"), gpuGroup);
    m_gpuAccelCheck->setChecked(gpuEnabled);
    connect(m_gpuAccelCheck, &QCheckBox::toggled, this, &SettingsDialog::onGpuAccelerationToggled);
    gpuLayout->addWidget(m_gpuAccelCheck);

    auto *gpuDesc = new QLabel(
        QStringLiteral("Accelerates WebEngine rendering, canvas, WebGL, and compositing with your GPU. "
                       "Disabling forces software rasterization (useful for debugging display issues or VMs)."),
        gpuGroup);
    gpuDesc->setWordWrap(true);
    gpuDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    gpuLayout->addWidget(gpuDesc);

    m_gpuRestartNotice = new QLabel(
        QStringLiteral("⚠️ Note: Changing hardware GPU acceleration requires restarting Ensemble to take effect."),
        gpuGroup);
    m_gpuRestartNotice->setWordWrap(true);
    m_gpuRestartNotice->setStyleSheet(QStringLiteral("font-size: 11px; color: #ffb86c; font-weight: bold;"));
    gpuLayout->addWidget(m_gpuRestartNotice);

    layout->addWidget(gpuGroup);

    // --- Section 3: Custom Chromium Flags ---
    auto *flagsGroup = new QGroupBox(QStringLiteral("Chromium Engine Switches"), container);
    auto *flagsLayout = new QVBoxLayout(flagsGroup);
    flagsLayout->setSpacing(6);

    m_customWebEngineCommandsCheck = new QCheckBox(
        QStringLiteral("[TODO: Coming soon] Custom WebEngine Chromium Flags"), flagsGroup);
    m_customWebEngineCommandsCheck->setEnabled(false);
    flagsLayout->addWidget(m_customWebEngineCommandsCheck);

    auto *flagsDesc = new QLabel(
        QStringLiteral("Pass specialized command-line switches to QtWebEngine / Chromium to tweak rendering "
                       "pipelines, sandbox profiles, and experimental web platform features."),
        flagsGroup);
    flagsDesc->setWordWrap(true);
    flagsDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));
    flagsLayout->addWidget(flagsDesc);

    m_chromiumFlagsLink = new QLabel(flagsGroup);
    m_chromiumFlagsLink->setWordWrap(true);
    m_chromiumFlagsLink->setTextFormat(Qt::RichText);
    m_chromiumFlagsLink->setOpenExternalLinks(true);
    m_chromiumFlagsLink->setText(QStringLiteral(
        "Reference: <a href=\"https://peter.sh/experiments/chromium-command-line-switches/\">"
        "peter.sh Chromium Command Line Switches Reference ↗</a>"));
    flagsLayout->addWidget(m_chromiumFlagsLink);

    layout->addWidget(flagsGroup);
    layout->addStretch(1);

    return createScrollableContainer(container, this);
}

QWidget *SettingsDialog::createAiTab()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(14);

    auto *aiGroup = new QGroupBox(QStringLiteral("AI Assistance && Agent Interop"), container);
    auto *aiLayout = new QVBoxLayout(aiGroup);
    aiLayout->setSpacing(12);

    auto *aiHeaderDesc = new QLabel(
        QStringLiteral("Integrate generative AI models and autonomous pair-programming agents into Ensemble:"),
        aiGroup);
    aiHeaderDesc->setWordWrap(true);
    aiHeaderDesc->setStyleSheet(QStringLiteral("font-size: 11px;"));
    aiLayout->addWidget(aiHeaderDesc);

    // Item 1: Cloud API (Gemini / Claude)
    auto *apiBox = new QFrame(aiGroup);
    auto *apiBoxLayout = new QVBoxLayout(apiBox);
    apiBoxLayout->setContentsMargins(0, 0, 0, 0);
    apiBoxLayout->setSpacing(3);

    m_aiApiCheck = new QCheckBox(
        QStringLiteral("[TODO: Coming soon] Allow Gemini or Claude access this via API"), apiBox);
    m_aiApiCheck->setEnabled(false);
    apiBoxLayout->addWidget(m_aiApiCheck);

    auto *apiDesc = new QLabel(
        QStringLiteral("Directly connect Google Gemini API or Anthropic Claude API for drafting prose, "
                       "generating work summaries, finding synonyms, and analyzing story structure."),
        apiBox);
    apiDesc->setWordWrap(true);
    apiDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8; padding-left: 20px;"));
    apiBoxLayout->addWidget(apiDesc);
    aiLayout->addWidget(apiBox);

    // Item 2: MCP Server (Antigravity / Claude Code)
    auto *mcpBox = new QFrame(aiGroup);
    auto *mcpBoxLayout = new QVBoxLayout(mcpBox);
    mcpBoxLayout->setContentsMargins(0, 0, 0, 0);
    mcpBoxLayout->setSpacing(6);

    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    const bool mcpEnabled = settings.value(QStringLiteral("ai/mcpEnabled"), false).toBool();

    m_aiMcpCheck = new QCheckBox(
        QStringLiteral("Allow Gemini Antigravity, Claude Code && AI agents access this via MCP"), mcpBox);
    m_aiMcpCheck->setChecked(mcpEnabled);
    connect(m_aiMcpCheck, &QCheckBox::toggled, this, &SettingsDialog::onMcpToggled);
    mcpBoxLayout->addWidget(m_aiMcpCheck);

    auto *mcpDesc = new QLabel(
        QStringLiteral("Expose an embedded Model Context Protocol (MCP) server so developer agents like "
                       "Gemini Antigravity and Claude Code can inspect your project, run automated tests, "
                       "read/write chapters with live preview synchronization, and author custom work skins."),
        mcpBox);
    mcpDesc->setWordWrap(true);
    mcpDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8; padding-left: 20px;"));
    mcpBoxLayout->addWidget(mcpDesc);

    // MCP Server Details Card
    m_mcpDetailsCard = new QFrame(mcpBox);
    m_mcpDetailsCard->setObjectName(QStringLiteral("mcpDetailsCard"));
    auto *cardLayout = new QVBoxLayout(m_mcpDetailsCard);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(10);

    // Disclaimer banner
    auto *disclaimerFrame = new QFrame(m_mcpDetailsCard);
    disclaimerFrame->setObjectName(QStringLiteral("mcpDisclaimerFrame"));
    auto *disclaimerLayout = new QHBoxLayout(disclaimerFrame);
    disclaimerLayout->setContentsMargins(10, 8, 10, 8);
    disclaimerLayout->setSpacing(8);

    auto *disclaimerIcon = new QLabel(QStringLiteral("⏱️"), disclaimerFrame);
    disclaimerIcon->setStyleSheet(QStringLiteral("font-size: 16px;"));
    disclaimerLayout->addWidget(disclaimerIcon, 0, Qt::AlignTop);

    m_mcpDisclaimerLabel = new QLabel(
        QStringLiteral("<b>Notice:</b> Initializing the embedded MCP server and establishing the connection handshake "
                       "with AI agents may take a short moment on startup while negotiating the protocol handshake. "
                       "If an agent is slow to connect or fails, use the diagnostic tools below to check loopback reachability."),
        disclaimerFrame);
    m_mcpDisclaimerLabel->setWordWrap(true);
    m_mcpDisclaimerLabel->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.9; line-height: 1.3;"));
    disclaimerLayout->addWidget(m_mcpDisclaimerLabel, 1);
    cardLayout->addWidget(disclaimerFrame);

    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(10);

    m_mcpStatusBadge = new QLabel(m_mcpDetailsCard);
    m_mcpStatusBadge->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 12px;"));
    statusRow->addWidget(m_mcpStatusBadge);

    m_mcpEndpointLabel = new QLabel(m_mcpDetailsCard);
    m_mcpEndpointLabel->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.85; font-family: monospace;"));
    statusRow->addWidget(m_mcpEndpointLabel);
    statusRow->addStretch(1);
    cardLayout->addLayout(statusRow);

    // Button Grid: 2 rows x 2 columns for clean responsive fit without horizontal scrolling
    auto *btnGrid = new QGridLayout();
    btnGrid->setHorizontalSpacing(10);
    btnGrid->setVerticalSpacing(8);

    m_mcpTestBtn = new QPushButton(QStringLiteral("🧪 Test Loopback Connection"), m_mcpDetailsCard);
    m_mcpTestBtn->setCursor(Qt::PointingHandCursor);
    m_mcpTestBtn->setToolTip(QStringLiteral("Probe loopback socket to verify localhost reachability and run diagnostic checkpoints."));
    connect(m_mcpTestBtn, &QPushButton::clicked, this, &SettingsDialog::onMcpTestClicked);
    btnGrid->addWidget(m_mcpTestBtn, 0, 0);

    m_mcpCopyConfigBtn = new QPushButton(QStringLiteral("📋 Copy MCP Config"), m_mcpDetailsCard);
    m_mcpCopyConfigBtn->setCursor(Qt::PointingHandCursor);
    m_mcpCopyConfigBtn->setToolTip(QStringLiteral("Copy JSON configuration snippet for Claude Desktop or Antigravity to clipboard."));
    connect(m_mcpCopyConfigBtn, &QPushButton::clicked, this, &SettingsDialog::onMcpCopyConfigClicked);
    btnGrid->addWidget(m_mcpCopyConfigBtn, 0, 1);

    m_mcpInstallAgentsBtn = new QPushButton(QStringLiteral("📤 Send MCP Manual && Setup to Agents"), m_mcpDetailsCard);
    m_mcpInstallAgentsBtn->setCursor(Qt::PointingHandCursor);
    m_mcpInstallAgentsBtn->setToolTip(QStringLiteral("Automatically register MCP schemas in Antigravity and Claude settings, and export ENSEMBLE_MCP_MANUAL.md"));
    connect(m_mcpInstallAgentsBtn, &QPushButton::clicked, this, &SettingsDialog::onMcpInstallAgentsClicked);
    btnGrid->addWidget(m_mcpInstallAgentsBtn, 1, 0);

    m_mcpDiagnosticsBtn = new QPushButton(QStringLiteral("🐞 View MCP Diagnostics"), m_mcpDetailsCard);
    m_mcpDiagnosticsBtn->setCursor(Qt::PointingHandCursor);
    m_mcpDiagnosticsBtn->setToolTip(QStringLiteral("View live connection status, socket metrics, diagnostic checkpoints, and troubleshooting advice."));
    connect(m_mcpDiagnosticsBtn, &QPushButton::clicked, this, &SettingsDialog::onMcpDiagnosticsClicked);
    btnGrid->addWidget(m_mcpDiagnosticsBtn, 1, 1);

    cardLayout->addLayout(btnGrid);

    m_mcpTestResultLabel = new QLabel(m_mcpDetailsCard);
    m_mcpTestResultLabel->setWordWrap(true);
    m_mcpTestResultLabel->setTextFormat(Qt::RichText);
    m_mcpTestResultLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    cardLayout->addWidget(m_mcpTestResultLabel);

    mcpBoxLayout->addWidget(m_mcpDetailsCard);
    m_mcpDetailsCard->setVisible(mcpEnabled);
    aiLayout->addWidget(mcpBox);

    // Item 3: Local LLMs via MCP Connector (Beta)
    auto *localBox = new QFrame(aiGroup);
    auto *localBoxLayout = new QVBoxLayout(localBox);
    localBoxLayout->setContentsMargins(0, 0, 0, 0);
    localBoxLayout->setSpacing(6);

    const bool localMcpEnabled = settings.value(QStringLiteral("ai/localLlmsMcpEnabled"), false).toBool();

    m_aiLocalAgentsCheck = new QCheckBox(
        QStringLiteral("[Beta] Allow local LLMs to access Ensemble via MCP connector"), localBox);
    m_aiLocalAgentsCheck->setObjectName(QStringLiteral("aiLocalAgentsCheck"));
    m_aiLocalAgentsCheck->setEnabled(true);
    m_aiLocalAgentsCheck->setChecked(localMcpEnabled);
    connect(m_aiLocalAgentsCheck, &QCheckBox::toggled, this, &SettingsDialog::onLocalLlmsMcpToggled);
    localBoxLayout->addWidget(m_aiLocalAgentsCheck);

    auto *localDesc = new QLabel(
        QStringLiteral("Enable privacy-preserving local LLMs (Ollama, llama.cpp, LM Studio, LocalAI, Jan, "
                       "Open WebUI) to interact with your story strictly through the Model Context "
                       "Protocol (MCP connector only). Beta feature."),
        localBox);
    localDesc->setWordWrap(true);
    localDesc->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8; padding-left: 20px;"));
    localBoxLayout->addWidget(localDesc);

    // Local LLMs Details Card
    m_localLlmsDetailsCard = new QFrame(localBox);
    m_localLlmsDetailsCard->setObjectName(QStringLiteral("localLlmsDetailsCard"));
    auto *localCardLayout = new QVBoxLayout(m_localLlmsDetailsCard);
    localCardLayout->setContentsMargins(14, 12, 14, 12);
    localCardLayout->setSpacing(10);

    // Notice banner
    auto *localBannerFrame = new QFrame(m_localLlmsDetailsCard);
    localBannerFrame->setObjectName(QStringLiteral("localBannerFrame"));
    auto *localBannerLayout = new QHBoxLayout(localBannerFrame);
    localBannerLayout->setContentsMargins(10, 8, 10, 8);
    localBannerLayout->setSpacing(8);

    auto *localBannerIcon = new QLabel(QStringLiteral("🧪"), localBannerFrame);
    localBannerIcon->setStyleSheet(QStringLiteral("font-size: 16px;"));
    localBannerLayout->addWidget(localBannerIcon, 0, Qt::AlignTop);

    auto *localBannerText = new QLabel(
        QStringLiteral("<b>Beta MCP Connector:</b> Local models connect exclusively via the Model Context Protocol (MCP). "
                       "All tool invocations (chapter edits, work skins, lore graph) are strictly validated and sanitized "
                       "against Ensemble's schema and AO3 HTML standards. No direct OS shell access or raw filesystem execution is permitted."),
        localBannerFrame);
    localBannerText->setWordWrap(true);
    localBannerText->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.9; line-height: 1.3;"));
    localBannerLayout->addWidget(localBannerText, 1);
    localCardLayout->addWidget(localBannerFrame);

    auto *localInfoLabel = new QLabel(
        QStringLiteral("<b>Supported Frameworks:</b> Ollama, llama.cpp, LM Studio, LocalAI, Jan, Open WebUI, and local Python scripts.<br>"
                       "<b>Connection Methods:</b> Stdio Bridge (<code>ensemble --mcp-bridge</code>) or Localhost WebSocket (<code>ws://127.0.0.1:26303</code>)."),
        m_localLlmsDetailsCard);
    localInfoLabel->setWordWrap(true);
    localInfoLabel->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.85; line-height: 1.3;"));
    localCardLayout->addWidget(localInfoLabel);

    // Buttons
    auto *localBtnLayout = new QHBoxLayout();
    localBtnLayout->setSpacing(10);

    m_localLlmsCopyConfigBtn = new QPushButton(QStringLiteral("📋 Copy Local LLM Config"), m_localLlmsDetailsCard);
    m_localLlmsCopyConfigBtn->setCursor(Qt::PointingHandCursor);
    m_localLlmsCopyConfigBtn->setToolTip(QStringLiteral("Copy JSON configuration snippet for local LLM clients (Ollama, llama.cpp, LM Studio, Open WebUI) to clipboard."));
    connect(m_localLlmsCopyConfigBtn, &QPushButton::clicked, this, &SettingsDialog::onLocalLlmsCopyConfigClicked);
    localBtnLayout->addWidget(m_localLlmsCopyConfigBtn);

    m_localLlmsExportConfigsBtn = new QPushButton(QStringLiteral("📤 Export to Local Clients"), m_localLlmsDetailsCard);
    m_localLlmsExportConfigsBtn->setCursor(Qt::PointingHandCursor);
    m_localLlmsExportConfigsBtn->setToolTip(QStringLiteral("Generate local LLM config and Python connector script (for Ollama, llama.cpp, LM Studio)."));
    connect(m_localLlmsExportConfigsBtn, &QPushButton::clicked, this, &SettingsDialog::onLocalLlmsExportConfigsClicked);
    localBtnLayout->addWidget(m_localLlmsExportConfigsBtn);

    m_localLlmsGuideBtn = new QPushButton(QStringLiteral("📖 Setup Guide"), m_localLlmsDetailsCard);
    m_localLlmsGuideBtn->setCursor(Qt::PointingHandCursor);
    m_localLlmsGuideBtn->setToolTip(QStringLiteral("Open Help Guide for step-by-step instructions on connecting local LLMs."));
    connect(m_localLlmsGuideBtn, &QPushButton::clicked, this, &SettingsDialog::onLocalLlmsGuideClicked);
    localBtnLayout->addWidget(m_localLlmsGuideBtn);

    localBtnLayout->addStretch(1);
    localCardLayout->addLayout(localBtnLayout);

    m_localLlmsStatusLabel = new QLabel(m_localLlmsDetailsCard);
    m_localLlmsStatusLabel->setWordWrap(true);
    m_localLlmsStatusLabel->setTextFormat(Qt::RichText);
    m_localLlmsStatusLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    localCardLayout->addWidget(m_localLlmsStatusLabel);

    localBoxLayout->addWidget(m_localLlmsDetailsCard);
    m_localLlmsDetailsCard->setVisible(localMcpEnabled);
    aiLayout->addWidget(localBox);

    layout->addWidget(aiGroup);
    layout->addStretch(1);

    return createScrollableContainer(container, this);
}

QWidget *SettingsDialog::createPluginsTab()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(14);

    auto *pluginGroup = new QGroupBox(QStringLiteral("[Beta] Sandboxed Scripting Plugins"), container);
    auto *pluginLayout = new QVBoxLayout(pluginGroup);
    pluginLayout->setSpacing(12);

    auto *betaTitle = new QLabel(
        QStringLiteral("<b>🧩 Third-Party Scripting Engine (Beta)</b>"), pluginGroup);
    betaTitle->setWordWrap(true);
    betaTitle->setStyleSheet(QStringLiteral("font-size: 14px;"));
    pluginLayout->addWidget(betaTitle);

    m_pluginsDesc = new QLabel(
        QStringLiteral("Ensemble features a custom, memory-bounded, Python-like DSL interpreter for third-party scripts.<br><br>"
                       "<b>Non-Negotiable Sandbox Protection:</b><br>"
                       "• <b>Zero Native Risk:</b> No CPython, Lua, or dynamic libraries (<code>.so</code> / <code>.dll</code>).<br>"
                       "• <b>Thread Isolation:</b> Runs in dedicated <code>QThread</code> workers without GUI pointers.<br>"
                       "• <b>Memory &amp; Time Guards:</b> 8 MB RAM cap, 150,000 steps / 250ms timeout, 64-frame stack depth.<br>"
                       "• <b>Opaque Storage:</b> Isolated key-value storage (5 MB cap) with strict path-traversal traps.<br>"
                       "• <b>Host Connectors:</b> <code>editor</code>, <code>storage</code>, <code>status</code>, and <code>plugins</code>.<br><br>"
                       "Use the Plugin Manager to view active plugins, test hooks, or load new Python-like scripts."),
        pluginGroup);
    m_pluginsDesc->setWordWrap(true);
    m_pluginsDesc->setTextFormat(Qt::RichText);
    m_pluginsDesc->setStyleSheet(QStringLiteral("line-height: 1.4;"));
    pluginLayout->addWidget(m_pluginsDesc);

    auto *btnRow = new QHBoxLayout();
    auto *openManagerBtn = new QPushButton(QStringLiteral("🧩 Open Plugin Manager…"), pluginGroup);
    openManagerBtn->setCursor(Qt::PointingHandCursor);
    connect(openManagerBtn, &QPushButton::clicked, this, [this]() {
        auto *dialog = new EnsemblePlugin::PluginManagerDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    btnRow->addWidget(openManagerBtn);

    auto *openFolderBtn = new QPushButton(QStringLiteral("📂 Open Plugins Folder"), pluginGroup);
    openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(openFolderBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::pluginsDir()));
    });
    btnRow->addWidget(openFolderBtn);

    btnRow->addStretch(1);
    pluginLayout->addLayout(btnRow);

    layout->addWidget(pluginGroup);
    layout->addStretch(1);

    return createScrollableContainer(container, this);
}

void SettingsDialog::updateTheme()
{
    setStyleSheet(ThemeManager::instance().getDialogStyleSheet());
    updateWallpaperColorPreview();
}

void SettingsDialog::updateWallpaperColorPreview()
{
    if (!m_wallpaperColorPreview || !m_wallpaperHexLabel) return;

    const QColor accent = ThemeManager::instance().materialYouAccentColor();
    const QString hex = accent.name();

    m_wallpaperColorPreview->setStyleSheet(QStringLiteral(
        "background-color: %1; border: 2px solid %2; border-radius: 14px;"
    ).arg(hex, ThemeManager::instance().isDarkMode() ? QStringLiteral("#ffffff") : QStringLiteral("#0f172a")));
    m_wallpaperHexLabel->setText(hex);

    if (m_wallpaperStatusLabel) {
        m_wallpaperStatusLabel->setText(ThemeManager::instance().wallpaperStatus());
    }

    if (m_materialYouCard) {
        const QString border = ThemeManager::instance().borderColor().name();
        const QString cardBg = ThemeManager::instance().cardBgColor().name();
        m_materialYouCard->setStyleSheet(QStringLiteral(
            "QFrame#materialYouCard {"
            "  border: 1px solid %1;"
            "  border-radius: 16px;"
            "  background-color: %2;"
            "  padding: 12px 14px;"
            "}"
        ).arg(border, cardBg));
    }
}

void SettingsDialog::onThemeSelectionChanged()
{
    if (m_themeDarkRadio && m_themeDarkRadio->isChecked()) {
        ThemeManager::instance().setTheme(AppTheme::Dark);
    } else if (m_themeDarkerRadio && m_themeDarkerRadio->isChecked()) {
        ThemeManager::instance().setTheme(AppTheme::Darker);
    } else if (m_themeLightRadio && m_themeLightRadio->isChecked()) {
        ThemeManager::instance().setTheme(AppTheme::Light);
    } else if (m_themeReversiRadio && m_themeReversiRadio->isChecked()) {
        ThemeManager::instance().setTheme(AppTheme::Ao3Reversi);
    }
}

void SettingsDialog::onSkinFlavorSelectionChanged(int index)
{
    if (!m_skinFlavorCombo) return;

    const auto flavor = static_cast<SkinFlavor>(m_skinFlavorCombo->itemData(index).toInt());
    ThemeManager::instance().setSkinFlavor(flavor);

    if (m_materialYouCard) {
        m_materialYouCard->setVisible(flavor == SkinFlavor::MaterialYou);
    }
    updateWallpaperColorPreview();
    updateTheme();
}

void SettingsDialog::onRescanWallpaperClicked()
{
    ThemeManager::instance().rescanWallpaperColor();
    updateWallpaperColorPreview();
    updateTheme();
}

void SettingsDialog::onGpuAccelerationToggled(bool checked)
{
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    settings.setValue(QStringLiteral("webengine/gpuAcceleration"), checked);
}

void SettingsDialog::setMcpServer(McpServer *server)
{
    m_mcpServer = server;
    if (m_mcpServer) {
        connect(m_mcpServer, &McpServer::statusChanged, this, &SettingsDialog::updateMcpStatusDisplay);
    }
    updateMcpStatusDisplay();
}

void SettingsDialog::onMcpToggled(bool enabled)
{
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    settings.setValue(QStringLiteral("ai/mcpEnabled"), enabled);

    if (m_mcpServer) {
        if (enabled && !m_mcpServer->isRunning()) {
            m_mcpServer->start();
        } else if (!enabled && m_mcpServer->isRunning()) {
            m_mcpServer->stop();
        }
    }
    emit mcpServerToggled(enabled);

    if (m_mcpDetailsCard) {
        m_mcpDetailsCard->setVisible(enabled);
    }
    updateMcpStatusDisplay();
}

void SettingsDialog::updateMcpStatusDisplay()
{
    if (!m_mcpStatusBadge || !m_mcpEndpointLabel) return;

    bool isRunning = false;
    quint16 port = 0;
    int clients = 0;

    if (m_mcpServer) {
        isRunning = m_mcpServer->isRunning();
        port = m_mcpServer->port();
        clients = m_mcpServer->clientCount();
    } else {
        bool ok = false;
        QJsonObject info = McpServer::readDiscoveryFile(&ok);
        if (ok && info.value(QStringLiteral("status")).toString() == QStringLiteral("running")) {
            isRunning = true;
            port = static_cast<quint16>(info.value(QStringLiteral("port")).toInt());
        }
    }

    if (isRunning && port > 0) {
        m_mcpStatusBadge->setText(QStringLiteral("🟢 Active"));
        m_mcpStatusBadge->setStyleSheet(QStringLiteral("color: #50fa7b; font-weight: bold; font-size: 12px;"));
        m_mcpEndpointLabel->setText(QStringLiteral("ws://127.0.0.1:%1  (%2 connected)").arg(port).arg(clients));
    } else {
        m_mcpStatusBadge->setText(QStringLiteral("🔴 Stopped"));
        m_mcpStatusBadge->setStyleSheet(QStringLiteral("color: #ff5555; font-weight: bold; font-size: 12px;"));
        m_mcpEndpointLabel->setText(QStringLiteral("Server is not currently listening."));
    }

    if (m_mcpDetailsCard) {
        const QString border = ThemeManager::instance().borderColor().name();
        const QString cardBg = ThemeManager::instance().cardBgColor().name();
        const bool isDark = ThemeManager::instance().isDarkMode();
        const QString noticeBg = isDark ? QStringLiteral("rgba(241, 250, 140, 0.08)") : QStringLiteral("rgba(245, 158, 11, 0.1)");
        const QString noticeBorder = isDark ? QStringLiteral("rgba(241, 250, 140, 0.25)") : QStringLiteral("rgba(245, 158, 11, 0.35)");

        m_mcpDetailsCard->setStyleSheet(QStringLiteral(
            "QFrame#mcpDetailsCard {"
            "  border: 1px solid %1;"
            "  border-radius: 12px;"
            "  background-color: %2;"
            "}"
            "QFrame#mcpDisclaimerFrame {"
            "  border: 1px solid %3;"
            "  border-radius: 8px;"
            "  background-color: %4;"
            "}"
        ).arg(border, cardBg, noticeBorder, noticeBg));
    }

    if (m_localLlmsDetailsCard) {
        const QString border = ThemeManager::instance().borderColor().name();
        const QString cardBg = ThemeManager::instance().cardBgColor().name();
        const bool isDark = ThemeManager::instance().isDarkMode();
        const QString noticeBg = isDark ? QStringLiteral("rgba(139, 92, 246, 0.08)") : QStringLiteral("rgba(139, 92, 246, 0.1)");
        const QString noticeBorder = isDark ? QStringLiteral("rgba(139, 92, 246, 0.25)") : QStringLiteral("rgba(139, 92, 246, 0.35)");

        m_localLlmsDetailsCard->setStyleSheet(QStringLiteral(
            "QFrame#localLlmsDetailsCard {"
            "  border: 1px solid %1;"
            "  border-radius: 12px;"
            "  background-color: %2;"
            "}"
            "QFrame#localBannerFrame {"
            "  border: 1px solid %3;"
            "  border-radius: 8px;"
            "  background-color: %4;"
            "}"
        ).arg(border, cardBg, noticeBorder, noticeBg));
    }
}

void SettingsDialog::onMcpTestClicked()
{
    if (!m_mcpTestResultLabel) return;

    if (m_mcpServer && m_mcpServer->isRunning()) {
        m_mcpTestResultLabel->setText(QStringLiteral("<i>Testing loopback socket connectivity and diagnostic checkpoints...</i>"));
        QApplication::processEvents();

        McpTestResult res = m_mcpServer->testLoopback();
        QString html;
        if (res.success) {
            html += QStringLiteral("<div style=\"color: #50fa7b; font-weight: bold; font-size: 11px;\">✅ %1 (%2 ms latency)</div>")
                        .arg(res.message).arg(res.latencyMs);
        } else {
            html += QStringLiteral("<div style=\"color: #ff5555; font-weight: bold; font-size: 11px;\">❌ %1</div>")
                        .arg(res.message);
        }

        for (const QString &cp : res.checkpoints) {
            html += QStringLiteral("<div style=\"margin-left: 6px; font-size: 10px; opacity: 0.9;\">• %1</div>").arg(cp);
        }

        if (!res.success && !res.details.isEmpty()) {
            html += QStringLiteral("<div style=\"margin-top: 4px; font-size: 10px; color: #ffb86c;\">%1</div>")
                        .arg(res.details.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        }

        m_mcpTestResultLabel->setText(html);
    } else {
        m_mcpTestResultLabel->setText(QStringLiteral("<span style=\"color: #ffb86c;\">⚠️ MCP Server is currently offline. Enable the checkbox above first.</span>"));
    }
}

void SettingsDialog::onMcpCopyConfigClicked()
{
    const QString exePath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    const QString jsonEscaped = QString(exePath).replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    const QString configSnippet = QStringLiteral(
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"ensemble\": {\n"
        "      \"command\": \"%1\",\n"
        "      \"args\": [\"--mcp-bridge\"]\n"
        "    }\n"
        "  }\n"
        "}"
    ).arg(jsonEscaped);

    QApplication::clipboard()->setText(configSnippet);
    if (m_mcpTestResultLabel) {
        m_mcpTestResultLabel->setText(QStringLiteral("<span style=\"color: #8be9fd;\">📋 Configuration snippet copied to clipboard! Paste into claude_desktop_config.json or Antigravity settings.</span>"));
    }
}

void SettingsDialog::onMcpInstallAgentsClicked()
{
    const QString exePath = QApplication::applicationFilePath();
    McpInstallReport report = McpAgentInstaller::installAll(exePath);

    if (m_mcpTestResultLabel) {
        m_mcpTestResultLabel->setText(QStringLiteral("<span style=\"color: #50fa7b;\">📤 Successfully installed MCP manual to: %1</span>").arg(report.manualPath));
    }

    QMessageBox::information(this, QStringLiteral("MCP Agent Integration Complete"), report.summary);
}

void SettingsDialog::onMcpDiagnosticsClicked()
{
    auto *diagDialog = new QDialog(this);
    diagDialog->setWindowTitle(QStringLiteral("Ensemble MCP Server Diagnostics"));
    diagDialog->resize(680, 520);
    diagDialog->setMinimumSize(540, 380);

    auto *layout = new QVBoxLayout(diagDialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto *headerLabel = new QLabel(
        QStringLiteral("<b>MCP Diagnostic Status &amp; Health Check</b>"), diagDialog);
    headerLabel->setStyleSheet(QStringLiteral("font-size: 14px;"));
    layout->addWidget(headerLabel);

    auto *infoText = new QTextEdit(diagDialog);
    infoText->setReadOnly(true);

    QString report;
    report += QStringLiteral("=== Ensemble MCP Diagnostic Report ===\n\n");

    const bool isRunning = m_mcpServer ? m_mcpServer->isRunning() : false;
    const quint16 port = m_mcpServer ? m_mcpServer->port() : 0;
    const int clients = m_mcpServer ? m_mcpServer->clientCount() : 0;
    const qint64 msgsRecv = m_mcpServer ? m_mcpServer->messagesReceived() : 0;
    const qint64 msgsSent = m_mcpServer ? m_mcpServer->messagesSent() : 0;

    report += QStringLiteral("Server State:        %1\n").arg(isRunning ? QStringLiteral("ACTIVE") : QStringLiteral("STOPPED"));
    report += QStringLiteral("WebSocket URL:       %1\n").arg(m_mcpServer ? m_mcpServer->serverUrl() : QStringLiteral("None"));
    report += QStringLiteral("Bound Port:          %1\n").arg(port);
    report += QStringLiteral("Active AI Clients:   %1\n").arg(clients);
    report += QStringLiteral("Messages Received:   %1\n").arg(msgsRecv);
    report += QStringLiteral("Messages Sent:       %1\n").arg(msgsSent);
    report += QStringLiteral("Discovery Manifest:  %1\n").arg(McpServer::discoveryFilePath());

    bool discOk = false;
    QJsonObject disc = McpServer::readDiscoveryFile(&discOk);
    report += QStringLiteral("Manifest on Disk:    %1\n\n").arg(discOk ? QStringLiteral("Valid (status: ") + disc.value(QStringLiteral("status")).toString() + QStringLiteral(")") : QStringLiteral("Missing / Unreadable"));

    report += QStringLiteral("--- Self-Test Diagnostic Checkpoints ---\n");
    if (m_mcpServer && m_mcpServer->isRunning()) {
        McpTestResult res = m_mcpServer->testLoopback(1500);
        for (const QString &cp : res.checkpoints) {
            report += QStringLiteral("%1\n").arg(cp);
        }
    } else {
        report += QStringLiteral("Server is not currently running. Start the server to test loopback.\n");
    }

    report += QStringLiteral("\n--- Troubleshooting Guidance ---\n");
    report += QStringLiteral("1. Initialization Delay: AI agent handshakes (initialize -> capabilities) typically take 100-800 ms.\n");
    report += QStringLiteral("2. Windows Firewall: If loopback fails, allow ensemble.exe through Windows Defender Firewall.\n");
    report += QStringLiteral("3. Linux Firewall: Ensure localhost loopback interface (lo / 127.0.0.1) is not blocked by ufw/iptables.\n");
    report += QStringLiteral("4. Stdio Bridge Mode: For clients without native WebSocket MCP support, use:\n");
#if defined(Q_OS_WIN)
    report += QStringLiteral("   ensemble.exe --mcp-bridge\n");
    report += QStringLiteral("5. CLI Health Check: Run 'ensemble.exe --mcp-status' from PowerShell / Command Prompt.\n\n");
#else
    report += QStringLiteral("   ensemble --mcp-bridge\n");
    report += QStringLiteral("5. CLI Health Check: Run 'ensemble --mcp-status' from terminal to diagnose from command line.\n\n");
#endif

    report += QStringLiteral("--- Recent MCP Log Entries ---\n");
    const QString allLogs = EnsembleDebug::getLogBuffer();
    int mcpLineCount = 0;
    for (const QString &line : allLogs.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("[mcp]"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("MCP"), Qt::CaseInsensitive)) {
            report += line + QLatin1Char('\n');
            mcpLineCount++;
        }
    }
    if (mcpLineCount == 0) {
        report += QStringLiteral("(No MCP events logged yet.)\n");
    }

    infoText->setPlainText(report);
    infoText->setStyleSheet(QStringLiteral("font-family: monospace; font-size: 11px;"));
    layout->addWidget(infoText, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);

    auto *copyBtn = new QPushButton(QStringLiteral("📋 Copy Report"), diagDialog);
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QPushButton::clicked, [report]() {
        QApplication::clipboard()->setText(report);
    });
    btnRow->addWidget(copyBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), diagDialog);
    closeBtn->setDefault(true);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, diagDialog, &QDialog::accept);
    btnRow->addWidget(closeBtn);

    layout->addLayout(btnRow);
    diagDialog->setStyleSheet(ThemeManager::instance().getDialogStyleSheet());
    diagDialog->exec();
}

void SettingsDialog::onLocalLlmsMcpToggled(bool enabled)
{
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    settings.setValue(QStringLiteral("ai/localLlmsMcpEnabled"), enabled);

    if (m_localLlmsDetailsCard) {
        m_localLlmsDetailsCard->setVisible(enabled);
    }
    if (enabled && m_localLlmsStatusLabel) {
        m_localLlmsStatusLabel->setText(QStringLiteral("<span style=\"color: #50fa7b;\">🧪 Local LLM Beta MCP connector enabled. Use the config snippet or exported settings to connect your local model.</span>"));
    }
}

void SettingsDialog::onLocalLlmsCopyConfigClicked()
{
    const QString configSnippet = McpAgentInstaller::generateLocalLlmMcpConfigJson();
    QApplication::clipboard()->setText(configSnippet);
    if (m_localLlmsStatusLabel) {
        m_localLlmsStatusLabel->setText(QStringLiteral("<span style=\"color: #8be9fd;\">📋 Local LLM MCP configuration snippet copied to clipboard! Paste into Ollama/llama.cpp agent runner, LM Studio, or Open WebUI.</span>"));
    }
}

void SettingsDialog::onLocalLlmsExportConfigsClicked()
{
    const QString exePath = QApplication::applicationFilePath();
    McpInstallReport report = McpAgentInstaller::installAll(exePath);

    QString msg;
    if (!report.localLlmConfigPath.isEmpty() || !report.localLlmScriptPath.isEmpty()) {
        msg = QStringLiteral("<span style=\"color: #50fa7b;\">📤 Exported Local LLM MCP config (%1) and connector script (%2).</span>")
                  .arg(report.localLlmConfigPath, report.localLlmScriptPath);
    } else {
        msg = QStringLiteral("<span style=\"color: #f1fa8c;\">ℹ️ Manual generated at %1. Configure your local client (Ollama/llama.cpp/LM Studio) using this path.</span>")
                  .arg(report.manualPath);
    }

    if (m_localLlmsStatusLabel) {
        m_localLlmsStatusLabel->setText(msg);
    }
    QMessageBox::information(this, QStringLiteral("Local LLM MCP Connector (Beta)"),
                             QStringLiteral("Local LLM MCP integration files synchronized.\n\n"
                                            "• Executable: %1\n"
                                            "• Bridge command: %1 --mcp-bridge\n"
                                            "• Local MCP Config: %2\n"
                                            "• Agent Connector Script: %3\n"
                                            "• Agent Manual: %4\n\n"
                                            "Compatible with Ollama, llama.cpp, LM Studio, LocalAI, Jan, Open WebUI, and Python scripts.")
                                 .arg(exePath, report.localLlmConfigPath, report.localLlmScriptPath, report.manualPath));
}

void SettingsDialog::onLocalLlmsGuideClicked()
{
    HelpGuideDialog guideDlg(this);
    guideDlg.selectTopic(QStringLiteral("mcp_setup"));
    guideDlg.exec();
}

