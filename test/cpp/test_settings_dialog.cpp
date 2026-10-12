#include "themes/ThemeManager.h"
#include "ui/SettingsDialog.h"
#include "ui/HelpGuideDialog.h"
#include "ui/MainMenuWindow.h"
#include "utils/SingleInstanceManager.h"
#include "mcp/McpServer.h"
#include "mcp/McpAgentInstaller.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTabWidget>
#include <QTextBrowser>
#include <cassert>
#include <iostream>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Ensemble"));
    app.setOrganizationName(QStringLiteral("Ensemble"));

    std::cout << "Testing ThemeManager and SettingsDialog..." << std::endl;

    // 1. Test OLED Darker mode
    auto &tm = ThemeManager::instance();
    tm.setTheme(AppTheme::Darker);
    assert(tm.currentTheme() == AppTheme::Darker);
    assert(tm.isDarkMode() == true);
    assert(tm.isOledMode() == true);
    assert(tm.windowBgColor() == QColor(0x00, 0x00, 0x00));
    std::cout << "✓ OLED Darker mode verified (pitch black #000000)." << std::endl;

    // 2. Test SkinFlavor and Material You wallpaper scanning
    tm.setSkinFlavor(SkinFlavor::MaterialYou);
    assert(tm.currentSkinFlavor() == SkinFlavor::MaterialYou);
    tm.rescanWallpaperColor();
    assert(tm.materialYouAccentColor().isValid());
    assert(!tm.wallpaperStatus().isEmpty());
    std::cout << "✓ Material You wallpaper accent color resolved: "
              << tm.materialYouAccentColor().name().toStdString()
              << " (" << tm.wallpaperStatus().toStdString() << ")" << std::endl;

    // Test Light mode high-contrast text tokens
    tm.setTheme(AppTheme::Light);
    assert(tm.primaryTextColor() == QColor("#0f172a"));
    assert(tm.mutedTextColor() == QColor("#334155"));
    std::cout << "✓ Light mode high-contrast primary (#0f172a) and muted (#334155) text verified." << std::endl;
    tm.setTheme(AppTheme::Dark);

    // 3. Test SettingsDialog tabs and controls
    SettingsDialog dialog;
    auto *tabWidget = dialog.findChild<QTabWidget *>();
    assert(tabWidget != nullptr);
    assert(tabWidget->count() == 5);
    assert(tabWidget->tabText(0).contains("General"));
    assert(tabWidget->tabText(1).contains("Editor"));
    assert(tabWidget->tabText(2).contains("WebEngine"));
    assert(tabWidget->tabText(3).contains("AI"));
    assert(tabWidget->tabText(4).contains("Plugins"));
    std::cout << "✓ SettingsDialog tabs verified (General, Editor, WebEngine, AI, Plugins)." << std::endl;

    // Verify GPU acceleration setting persistence
    QSettings settings("Ensemble", "Ensemble");
    settings.setValue("webengine/gpuAcceleration", true);
    assert(settings.value("webengine/gpuAcceleration").toBool() == true);
    settings.setValue("webengine/gpuAcceleration", false);
    assert(settings.value("webengine/gpuAcceleration").toBool() == false);
    settings.setValue("webengine/gpuAcceleration", true); // restore default
    std::cout << "✓ GPU acceleration QSettings toggle verified." << std::endl;

    // Check coming soon items
    auto checkBoxes = dialog.findChildren<QCheckBox *>();
    bool foundMimic = false;
    bool foundMcp = false;
    bool foundApi = false;
    bool foundLocal = false;
    for (auto *cb : checkBoxes) {
        if (cb->text().contains("Allow Mimicking The whole AO3 Body page")) foundMimic = true;
        if (cb->text().contains("access this via MCP")) foundMcp = true;
        if (cb->text().contains("access this via API")) foundApi = true;
        if (cb->text().contains("Allow Other local agents access this software")) foundLocal = true;
    }
    assert(foundMimic);
    assert(foundMcp);
    assert(foundApi);
    assert(foundLocal);

    // Verify Editor Tab checkboxes
    bool foundHoverElem = false;
    bool foundHighlightErr = false;
    bool foundHoverDetails = false;
    bool foundAutoFormat = false;
    for (auto *cb : checkBoxes) {
        if (cb->text().contains("Show embedded element, tag, and CSS class info on hover")) foundHoverElem = true;
        if (cb->text().contains("Highlight errors, undefined classes, and warnings directly in editor")) foundHighlightErr = true;
        if (cb->text().contains("Show detailed problem explanations when hovering over issues")) foundHoverDetails = true;
        if (cb->text().contains("Auto-format Raw HTML (split paragraphs across lines)")) foundAutoFormat = true;
    }
    assert(foundHoverElem);
    assert(foundHighlightErr);
    assert(foundHoverDetails);
    assert(foundAutoFormat);
    std::cout << "✓ Editor settings checkboxes verified (Element hover, error highlight, error details, auto-format)." << std::endl;
    std::cout << "✓ All requested SettingsDialog checkboxes and TODO items verified." << std::endl;

    // 4. Test SkinFlavor geometry metrics and styling
    tm.setSkinFlavor(SkinFlavor::MaterialYou);
    assert(tm.buttonRadius() == 20); // Pill button
    assert(tm.tabRadius() == 18);
    assert(tm.cardRadius() == 22);
    assert(!tm.selectionBgCss().isEmpty());
    assert(tm.getEditorPaneStyleSheet().contains("selection-background-color"));
    assert(tm.getCodeEditorStyleSheet().contains("selection-background-color"));
    std::cout << "✓ Android Material You pill button radius (20px) and selection CSS verified." << std::endl;

    tm.setSkinFlavor(SkinFlavor::KdeUi);
    assert(tm.buttonRadius() == 3); // Breeze 3px radius
    assert(tm.tabRadius() == 3);
    assert(tm.cardRadius() == 4);
    assert(tm.accentColor() == QColor("#3daee9"));
    std::cout << "✓ KDE UI Breeze geometry (3px radius, #3daee9 accent) verified." << std::endl;

    tm.setSkinFlavor(SkinFlavor::StandardQt);
    assert(tm.buttonRadius() == 4); // Qt 4px radius
    assert(tm.tabRadius() == 4);
    assert(tm.cardRadius() == 6);
    std::cout << "✓ Standard Qt geometry (4px radius) verified." << std::endl;

    tm.applyCurrentThemeAndSkin();
    std::cout << "✓ applyCurrentThemeAndSkin executed cleanly without restart." << std::endl;

    // 5. Test HelpGuideDialog topics, search, and content rendering
    {
        HelpGuideDialog helpDialog;
        auto *topicList = helpDialog.findChild<QListWidget *>();
        auto *contentBrowser = helpDialog.findChild<QTextBrowser *>();
        auto *searchEdit = helpDialog.findChild<QLineEdit *>();

        assert(topicList != nullptr);
        assert(contentBrowser != nullptr);
        assert(searchEdit != nullptr);
        assert(topicList->count() == 16);

        // Verify topic selection
        helpDialog.selectTopic("work_skins");
        assert(contentBrowser->toHtml().contains("Work Skin"));

        // Verify new topics selection
        helpDialog.selectTopic("mcp_setup");
        assert(contentBrowser->toHtml().contains("Model Context Protocol"));
        assert(contentBrowser->toHtml().contains("--mcp-bridge"));

        helpDialog.selectTopic("lore_graph");
        assert(contentBrowser->toHtml().contains("Lore Graph"));
        assert(contentBrowser->toHtml().contains(".grph"));

        helpDialog.selectTopic("project_properties");
        assert(contentBrowser->toHtml().contains("Project Properties"));
        assert(contentBrowser->toHtml().contains("metadata.xml"));

        helpDialog.selectTopic("image_embeds");
        assert(contentBrowser->toHtml().contains("Image Embed"));
        assert(contentBrowser->toHtml().contains("Copy AO3 HTML Snippet"));

        helpDialog.selectTopic("agentic_data");
        assert(contentBrowser->toHtml().contains("Multi-Agent Collaboration"));
        assert(contentBrowser->toHtml().contains("5 MB"));

        helpDialog.selectTopic("git_history");
        assert(contentBrowser->toHtml().contains("Git-Like Version History"));
        assert(contentBrowser->toHtml().contains("history.cmt"));

        // Verify search filtering
        searchEdit->setText("Shortcuts");
        int visibleCount = 0;
        for (int i = 0; i < topicList->count(); ++i) {
            if (!topicList->item(i)->isHidden()) visibleCount++;
        }
        assert(visibleCount >= 1);

        searchEdit->setText("mcp");
        int mcpCount = 0;
        for (int i = 0; i < topicList->count(); ++i) {
            if (!topicList->item(i)->isHidden()) mcpCount++;
        }
        assert(mcpCount >= 1);

        searchEdit->clear();
        int restoredCount = 0;
        for (int i = 0; i < topicList->count(); ++i) {
            if (!topicList->item(i)->isHidden()) restoredCount++;
        }
        assert(restoredCount == 16);

        helpDialog.updateTheme();
        std::cout << "✓ HelpGuideDialog 16 topics (including MCP, Lore Graph, Properties, Image Embeds, Agentic Data), search filtering, and documentation browser verified." << std::endl;
    }

    // 6. Test MainMenuWidget project open/close state tracking
    {
        MainMenuWidget menu;
        QPushButton *saveBtn = menu.findChild<QPushButton *>(QStringLiteral("saveBtn"));
        QPushButton *continueBtn = menu.findChild<QPushButton *>(QStringLiteral("continueBtn"));
        QPushButton *closeBtn = menu.findChild<QPushButton *>(QStringLiteral("closeBtn"));

        assert(saveBtn != nullptr);
        assert(continueBtn != nullptr);
        assert(closeBtn != nullptr);

        auto *saveTitle = saveBtn->findChild<QLabel *>(QStringLiteral("actionBtnTitle"));
        assert(saveTitle != nullptr && saveTitle->text().contains("Save Project"));

        // Initially no active project
        assert(!saveBtn->isEnabled());
        assert(continueBtn->isHidden());
        assert(closeBtn->isHidden());

        // Set active project
        menu.setProjectState(true, QStringLiteral("My Epic AO3 Fic"));
        assert(saveBtn->isEnabled());
        assert(!continueBtn->isHidden());
        auto *continueSub = continueBtn->findChild<QLabel *>(QStringLiteral("actionBtnSubtext"));
        assert(continueSub != nullptr && continueSub->text().contains("My Epic AO3 Fic"));
        assert(!closeBtn->isHidden());

        // Revert / close project back to welcome screen
        menu.setProjectState(false);
        assert(!saveBtn->isEnabled());
        assert(continueBtn->isHidden());
        assert(closeBtn->isHidden());

        std::cout << "✓ MainMenuWidget project lifecycle states (active, reverted, closed) verified." << std::endl;
    }

    // 7. Test SingleInstanceManager socket naming, PID tracking, and process lifecycle
    {
        assert(!SingleInstanceManager::socketName().isEmpty());
        assert(!SingleInstanceManager::pidFilePath().isEmpty());
        assert(SingleInstanceManager::isProcessRunning(QCoreApplication::applicationPid()) == true);
        assert(SingleInstanceManager::isProcessRunning(99999999) == false);

        // Test headless checkAndHandle
        SingleInstanceAction action = SingleInstanceManager::checkAndHandle(true);
        assert(action == SingleInstanceAction::ProceedAsPrimary || action == SingleInstanceAction::ExitApplication);

        std::cout << "✓ SingleInstanceManager socket, PID tracking, and process lifecycle verified." << std::endl;
    }

    // 8. Test McpServer lifecycle, port acquisition, loopback self-test, and McpAgentInstaller
    {
        McpServer server;
        assert(!server.isRunning());
        assert(server.port() == 0);

        const bool started = server.start(26303);
        assert(started);
        assert(server.isRunning());
        assert(server.port() > 0);
        assert(!server.authToken().isEmpty());
        assert(!server.serverUrl().isEmpty());

        // Verify discovery file
        bool ok = false;
        QJsonObject info = McpServer::readDiscoveryFile(&ok);
        assert(ok);
        assert(info.value(QStringLiteral("status")).toString() == QStringLiteral("running"));
        assert(info.value(QStringLiteral("port")).toInt() == server.port());

        // Test loopback self-test probe
        McpTestResult testRes = server.testLoopback(3000);
        assert(testRes.success == true);
        assert(testRes.latencyMs >= 0);
        std::cout << "✓ MCP loopback self-test verified (roundtrip: " << testRes.latencyMs << " ms)." << std::endl;

        // Test McpAgentInstaller
        const QString testManual = McpAgentInstaller::generateManualMarkdown(QStringLiteral("/usr/bin/ensemble"));
        assert(testManual.contains(QStringLiteral("# Ensemble Model Context Protocol (MCP) Manual")));
        assert(testManual.contains(QStringLiteral("get_project_summary")));
        assert(testManual.contains(QStringLiteral("update_chapter_html")));
        assert(testManual.contains(QStringLiteral("test_firewall_connection")));
        std::cout << "✓ McpAgentInstaller markdown manual generation verified." << std::endl;

        // Test McpAgentInstaller::syncServerConfig add and overwrite detection
        {
            const QString tempConfigDir = QDir::tempPath() + QStringLiteral("/ensemble_test_mcp_") + QString::number(QCoreApplication::applicationPid());
            QDir().mkpath(tempConfigDir);
            const QString tempConfigFile = tempConfigDir + QStringLiteral("/mcp_config.json");

            // Write initial config with an outdated path
            QJsonObject initialRoot;
            QJsonObject initialServers;
            QJsonObject oldServer;
            oldServer[QStringLiteral("command")] = QStringLiteral("/old/stale/ensemble");
            oldServer[QStringLiteral("args")] = QJsonArray{QStringLiteral("--old-flag")};
            initialServers[QStringLiteral("ensemble")] = oldServer;
            // Also add a third-party server to verify preservation
            QJsonObject otherServer;
            otherServer[QStringLiteral("command")] = QStringLiteral("dart");
            initialServers[QStringLiteral("dart-mcp")] = otherServer;
            initialRoot[QStringLiteral("mcpServers")] = initialServers;

            QFile f(tempConfigFile);
            f.open(QIODevice::WriteOnly);
            f.write(QJsonDocument(initialRoot).toJson());
            f.close();

            // Run syncServerConfig with the actual app binary
            bool wasOverwritten = false;
            bool wasAdded = false;
            QString oldPath;
            const QString realBinary = QCoreApplication::applicationFilePath();
            bool synced = McpAgentInstaller::syncServerConfig(tempConfigFile, realBinary, wasOverwritten, wasAdded, oldPath);
            assert(synced == true);
            assert(wasOverwritten == true);
            assert(wasAdded == false);
            assert(oldPath == QStringLiteral("/old/stale/ensemble"));

            // Verify content
            f.open(QIODevice::ReadOnly);
            QJsonObject updatedRoot = QJsonDocument::fromJson(f.readAll()).object();
            f.close();

            QJsonObject updatedServers = updatedRoot.value(QStringLiteral("mcpServers")).toObject();
            assert(updatedServers.contains(QStringLiteral("ensemble")));
            assert(updatedServers.contains(QStringLiteral("dart-mcp")));
            QJsonObject updatedEnsemble = updatedServers.value(QStringLiteral("ensemble")).toObject();
            assert(updatedEnsemble.value(QStringLiteral("command")).toString() == realBinary);
            QJsonArray updatedArgs = updatedEnsemble.value(QStringLiteral("args")).toArray();
            assert(updatedArgs.size() == 1 && updatedArgs.at(0).toString() == QStringLiteral("--mcp-bridge"));

            // Run sync again without changes: should detect it is already up to date
            wasOverwritten = false;
            wasAdded = false;
            synced = McpAgentInstaller::syncServerConfig(tempConfigFile, realBinary, wasOverwritten, wasAdded, oldPath);
            assert(synced == true);
            assert(wasOverwritten == false);
            assert(wasAdded == false);

            QFile::remove(tempConfigFile);
            QDir().rmdir(tempConfigDir);
            std::cout << "✓ McpAgentInstaller syncServerConfig overwrite detection and server preservation verified." << std::endl;
        }

        server.stop();
        assert(!server.isRunning());
        assert(server.port() == 0);

        QJsonObject stoppedInfo = McpServer::readDiscoveryFile(&ok);
        assert(ok);
        assert(stoppedInfo.value(QStringLiteral("status")).toString() == QStringLiteral("stopped"));
        std::cout << "✓ McpServer start/stop and discovery file lifecycle verified." << std::endl;
    }

    std::cout << "All Settings, Preferences, Help Guide & Main Menu lifecycle tests passed successfully!" << std::endl;
    return 0;
}
