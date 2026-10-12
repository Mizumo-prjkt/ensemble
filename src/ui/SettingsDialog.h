#pragma once

#include <QDialog>
#include "themes/ThemeManager.h"

class QTabWidget;
class QRadioButton;
class QButtonGroup;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QFrame;
class QVBoxLayout;

class McpServer;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setMcpServer(McpServer *server);

public slots:
    void updateTheme();

private slots:
    void onThemeSelectionChanged();
    void onSkinFlavorSelectionChanged(int index);
    void onRescanWallpaperClicked();
    void onGpuAccelerationToggled(bool checked);
    void onEditorSettingToggled();
    void onMcpToggled(bool enabled);
    void onMcpTestClicked();
    void onMcpCopyConfigClicked();
    void onMcpInstallAgentsClicked();
    void onMcpDiagnosticsClicked();
    void updateMcpStatusDisplay();

signals:
    void editorSettingsChanged();
    void mcpServerToggled(bool enabled);

private:
    void setupUi();
    QWidget *createGeneralTab();
    QWidget *createEditorTab();
    QWidget *createWebEngineTab();
    QWidget *createAiTab();
    QWidget *createPluginsTab();

    void updateWallpaperColorPreview();

    QTabWidget *m_tabWidget = nullptr;

    // General Tab
    QRadioButton *m_themeDarkRadio = nullptr;
    QRadioButton *m_themeDarkerRadio = nullptr; // OLED mode
    QRadioButton *m_themeLightRadio = nullptr;
    QButtonGroup *m_themeGroup = nullptr;

    QComboBox *m_skinFlavorCombo = nullptr;
    QFrame *m_materialYouCard = nullptr;
    QLabel *m_wallpaperColorPreview = nullptr;
    QLabel *m_wallpaperHexLabel = nullptr;
    QLabel *m_wallpaperStatusLabel = nullptr;
    QPushButton *m_rescanWallpaperBtn = nullptr;

    // Editor Tab
    QCheckBox *m_hoverElementInfoCheck = nullptr;
    QCheckBox *m_highlightErrorsCheck = nullptr;
    QCheckBox *m_hoverErrorDetailsCheck = nullptr;
    QCheckBox *m_autoFormatHtmlCheck = nullptr;

    // WebEngine Tab
    QCheckBox *m_mimicAo3Check = nullptr;
    QCheckBox *m_gpuAccelCheck = nullptr;
    QLabel *m_gpuRestartNotice = nullptr;
    QCheckBox *m_customWebEngineCommandsCheck = nullptr;
    QLabel *m_chromiumFlagsLink = nullptr;

    // AI Tab
    QCheckBox *m_aiApiCheck = nullptr;
    QCheckBox *m_aiMcpCheck = nullptr;
    QFrame *m_mcpDetailsCard = nullptr;
    QLabel *m_mcpDisclaimerLabel = nullptr;
    QLabel *m_mcpStatusBadge = nullptr;
    QLabel *m_mcpEndpointLabel = nullptr;
    QPushButton *m_mcpTestBtn = nullptr;
    QLabel *m_mcpTestResultLabel = nullptr;
    QPushButton *m_mcpCopyConfigBtn = nullptr;
    QPushButton *m_mcpInstallAgentsBtn = nullptr;
    QPushButton *m_mcpDiagnosticsBtn = nullptr;
    QCheckBox *m_aiLocalAgentsCheck = nullptr;

    // Plugins Tab
    QLabel *m_pluginsDesc = nullptr;

    // Footer
    QPushButton *m_closeBtn = nullptr;

    McpServer *m_mcpServer = nullptr;
};
