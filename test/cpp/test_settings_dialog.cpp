#include "themes/ThemeManager.h"
#include "ui/SettingsDialog.h"
#include "ui/HelpGuideDialog.h"
#include "ui/AboutDialog.h"
#include "ui/ProjectLoadingDialog.h"
#include "ui/MainMenuWindow.h"
#include "ui/ToolbarIcons.h"
#include "net/UpdateChecker.h"
#include "utils/SingleInstanceManager.h"
#include "mcp/McpServer.h"
#include "mcp/McpAgentInstaller.h"
#include "ui/PluginHostDialog.h"
#include "ui/PluginManagerDialog.h"

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
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextBrowser>
#include <cassert>
#include <iostream>

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#endif

int main(int argc, char *argv[])
{
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif

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

    // Test AO3 Reversi theme and Material You conflict resolution
    tm.setTheme(AppTheme::Ao3Reversi);
    assert(tm.currentTheme() == AppTheme::Ao3Reversi);
    assert(tm.isDarkMode() == true);
    assert(tm.isAo3Reversi() == true);
    assert(tm.accentColor() == QColor("#990000"));
    assert(tm.windowBgColor() == QColor("#111111"));
    // Switching to Reversi must disable/switch away from Material You
    assert(tm.currentSkinFlavor() == SkinFlavor::StandardEnsemble);
    std::cout << "✓ AO3 Reversi theme verified (#111111 background, #990000 crimson accent) and Material You auto-disabled." << std::endl;

    // Selecting Material You must disallow Reversi and switch away from Reversi
    tm.setSkinFlavor(SkinFlavor::MaterialYou);
    assert(tm.currentSkinFlavor() == SkinFlavor::MaterialYou);
    assert(tm.currentTheme() == AppTheme::Dark);
    std::cout << "✓ Selecting Material You while in Reversi switches theme away from Reversi to Dark." << std::endl;

    // Test toggleTheme() with Material You active (skips Reversi)
    tm.setTheme(AppTheme::Light);
    tm.toggleTheme();
    assert(tm.currentTheme() == AppTheme::Dark);
    tm.toggleTheme();
    assert(tm.currentTheme() == AppTheme::Light); // Skipped Ao3Reversi!
    std::cout << "✓ toggleTheme with Material You active skips AO3 Reversi." << std::endl;

    // Test global theme toggle cycle with StandardEnsemble (Light -> Dark -> Ao3Reversi -> Light)
    tm.setSkinFlavor(SkinFlavor::StandardEnsemble);
    tm.setTheme(AppTheme::Light);
    tm.toggleTheme();
    assert(tm.currentTheme() == AppTheme::Dark);
    tm.toggleTheme();
    assert(tm.currentTheme() == AppTheme::Ao3Reversi);
    tm.toggleTheme();
    assert(tm.currentTheme() == AppTheme::Light);
    std::cout << "✓ Global theme toggle cycle verified (Light -> Dark -> AO3 Reversi -> Light)." << std::endl;

    // Test ToolbarIcons vector icon generation
    {
        const QColor normalColor("#334155");
        const QColor activeColor("#ffffff");
        assert(!ToolbarIcons::bold(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::italic(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::underline(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::strikethrough(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::heading1(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::heading2(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::heading3(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::paragraph(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::link(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::horizontalRule(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::blockquote(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::bulletList(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::numberedList(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::image(normalColor, activeColor).isNull());
        assert(!ToolbarIcons::code(normalColor, activeColor).isNull());
        std::cout << "✓ ToolbarIcons vector icon generation verified." << std::endl;
    }

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
        if (cb->text().contains("Allow Other local agents access this software") ||
            cb->text().contains("local LLMs")) {
            foundLocal = true;
            assert(cb->isEnabled());
            assert(cb->text().contains("[Beta]"));
        }
    }
    assert(foundMimic);
    assert(foundMcp);
    assert(foundApi);
    assert(foundLocal);

    // Verify Local LLMs Beta MCP Connector controls
    auto *localAgentsCheck = dialog.findChild<QCheckBox *>(QStringLiteral("aiLocalAgentsCheck"));
    assert(localAgentsCheck != nullptr);
    assert(localAgentsCheck->isEnabled());
    assert(localAgentsCheck->text().contains("[Beta]"));
    assert(localAgentsCheck->text().contains("MCP connector"));

    // Verify toggle persistence
    localAgentsCheck->setChecked(true);
    assert(settings.value("ai/localLlmsMcpEnabled").toBool() == true);
    auto *localDetailsCard = dialog.findChild<QFrame *>(QStringLiteral("localLlmsDetailsCard"));
    assert(localDetailsCard != nullptr);
    assert(!localDetailsCard->isHidden());

    localAgentsCheck->setChecked(false);
    assert(settings.value("ai/localLlmsMcpEnabled").toBool() == false);
    assert(localDetailsCard->isHidden());

    // Verify Local LLM MCP JSON snippet generation
    const QString localLlmConfig = McpAgentInstaller::generateLocalLlmMcpConfigJson();
    assert(localLlmConfig.contains("ensemble"));
    assert(localLlmConfig.contains("--mcp-bridge"));

    // Verify Local LLM Python connector script generation (Ollama & llama.cpp)
    const QString localLlmScript = McpAgentInstaller::generateLocalLlmAgentScript();
    assert(localLlmScript.contains("--ollama"));
    assert(localLlmScript.contains("--llama-cpp"));
    assert(localLlmScript.contains("--mcp-bridge"));

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
    std::cout << "✓ Local LLMs Beta MCP connector controls, settings persistence, and config snippet verified." << std::endl;
    std::cout << "✓ All requested SettingsDialog checkboxes and TODO items verified." << std::endl;

    // Test SettingsDialog mutual exclusivity between Reversi and Material You
    QRadioButton *reversiRadio = nullptr;
    for (auto *rb : dialog.findChildren<QRadioButton *>()) {
        if (rb->text().contains("AO3 Reversi")) {
            reversiRadio = rb;
            break;
        }
    }
    assert(reversiRadio != nullptr);
    QComboBox *flavorCombo = dialog.findChild<QComboBox *>();
    assert(flavorCombo != nullptr);

    // 1. Check Reversi in dialog
    reversiRadio->setChecked(true);
    assert(reversiRadio->isChecked());
    // Material You item in flavorCombo must be disabled
    int myIdx = -1;
    for (int i = 0; i < flavorCombo->count(); ++i) {
        if (flavorCombo->itemData(i).toInt() == static_cast<int>(SkinFlavor::MaterialYou)) {
            myIdx = i;
            break;
        }
    }
    assert(myIdx >= 0);
    auto *itemModel = qobject_cast<QStandardItemModel*>(flavorCombo->model());
    assert(itemModel != nullptr);
    assert(!itemModel->item(myIdx)->isEnabled());
    assert(flavorCombo->currentIndex() != myIdx);

    // 2. Select Material You in dialog
    flavorCombo->setCurrentIndex(myIdx);
    assert(flavorCombo->currentIndex() == myIdx);
    // Reversi radio must be disabled and not checked
    assert(!reversiRadio->isEnabled());
    assert(!reversiRadio->isChecked());
    std::cout << "✓ SettingsDialog UI mutual exclusivity between Reversi and Material You verified." << std::endl;

    // 4. Test SkinFlavor geometry metrics and styling
    tm.setSkinFlavor(SkinFlavor::MaterialYou);
    assert(tm.buttonRadius() == 8); // Balanced modern button radius to prevent clipping text
    assert(tm.tabRadius() == 8);
    assert(tm.cardRadius() == 12);
    assert(!tm.selectionBgCss().isEmpty());
    assert(tm.getEditorPaneStyleSheet().contains("selection-background-color"));
    assert(tm.getCodeEditorStyleSheet().contains("selection-background-color"));
    std::cout << "✓ Android Material You refined button radius (8px) and selection CSS verified." << std::endl;

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
        assert(topicList->count() == 18);

        // Verify plugins_dsl topic selection
        helpDialog.selectTopic("plugins_dsl");
        assert(contentBrowser->toHtml().contains("Sandboxed Scripting Engine"));

        // Verify topic selection
        helpDialog.selectTopic("work_skins");
        assert(contentBrowser->toHtml().contains("Work Skin"));

        // Verify chapter_manager topic selection
        helpDialog.selectTopic("chapter_manager");
        assert(contentBrowser->toHtml().contains("Chapter Manager"));
        assert(contentBrowser->toHtml().contains("Strict 1-Based"));

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
        assert(contentBrowser->toHtml().contains("Snapshots"));
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
        assert(restoredCount == 18);

        helpDialog.updateTheme();
        std::cout << "✓ HelpGuideDialog 18 topics (including Plugins DSL, Chapter Manager, MCP, Lore Graph, Properties, Image Embeds, Snapshots), search filtering, and documentation browser verified." << std::endl;
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
        assert(!SingleInstanceManager::socketPath().isEmpty());
        assert(!SingleInstanceManager::runtimeDir().isEmpty());
        assert(!SingleInstanceManager::pidFilePath().isEmpty());
        assert(SingleInstanceManager::isProcessRunning(QCoreApplication::applicationPid()) == true);
        assert(SingleInstanceManager::isProcessRunning(99999999) == false);

        // Verify process name verification: self is recognized as valid process
        assert(SingleInstanceManager::isEnsembleProcess(QCoreApplication::applicationPid()) == true);
        assert(SingleInstanceManager::isEnsembleProcess(99999999) == false);

#if defined(Q_OS_LINUX)
        // Linux init / systemd (PID 1) is running, but must NOT be recognized as an Ensemble process!
        assert(SingleInstanceManager::isEnsembleProcess(1) == false);
        assert(SingleInstanceManager::isProcessRunning(1) == false);

        // Boot ID verification
        const QString bootId = SingleInstanceManager::currentBootId();
        assert(!bootId.isEmpty());
#endif

        // Test stale boot ID PID file cleanup
        {
            const QString pidPath = SingleInstanceManager::pidFilePath();
            QJsonObject staleMeta;
            staleMeta[QStringLiteral("pid")] = 12345;
            staleMeta[QStringLiteral("bootId")] = QStringLiteral("stale-boot-id-simulated-99999");
            staleMeta[QStringLiteral("appPath")] = QStringLiteral("/fake/path/ensemble");

            QFile f(pidPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QJsonDocument(staleMeta).toJson(QJsonDocument::Compact));
                f.close();
            }
            assert(QFile::exists(pidPath));

            // checkAndHandle should detect stale boot ID, clean it up, and proceed as primary
            SingleInstanceAction staleAction = SingleInstanceManager::checkAndHandle(true);
            assert(staleAction == SingleInstanceAction::ProceedAsPrimary);
            assert(!QFile::exists(pidPath));
        }

        // Test headless checkAndHandle
        SingleInstanceAction action = SingleInstanceManager::checkAndHandle(true);
        assert(action == SingleInstanceAction::ProceedAsPrimary || action == SingleInstanceAction::ExitApplication);

        std::cout << "✓ SingleInstanceManager socket, boot ID validation, PID tracking, and process lifecycle verified." << std::endl;
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

    // 11. Test UpdateChecker semver comparison & GitHub releases JSON parsing
    {
        // A. Version parsing & semver comparison
        assert(UpdateChecker::isVersionNewer("v1.2.2", "1.1.1") == true);
        assert(UpdateChecker::isVersionNewer("1.2.2", "1.2.1") == true);
        assert(UpdateChecker::isVersionNewer("v2.0.0", "1.9.9") == true);
        assert(UpdateChecker::isVersionNewer("1.1.1", "1.1.1") == false);
        assert(UpdateChecker::isVersionNewer("v1.1.0", "1.1.1") == false);
        assert(UpdateChecker::isVersionNewer("1.0.5", "1.2.0") == false);
        assert(UpdateChecker::isVersionNewer("v1.2.2", "1.2.2 (Debug-dirty)") == false);

        // B. GitHub releases JSON parsing
        const QByteArray sampleGithubJson = R"json({
            "tag_name": "v1.2.2",
            "name": "Ensemble Release v1.2.2",
            "html_url": "https://github.com/Mizumo-prjkt/ensemble/releases/tag/v1.2.2",
            "published_at": "2026-10-17T12:00:00Z",
            "body": "### Changes\n- Enhanced rich text editor\n- Added snapshots diff\n- Fixed chapter indexing"
        })json";

        ReleaseInfo info = UpdateChecker::parseReleaseJson(sampleGithubJson, "1.1.1");
        assert(info.tagName == "v1.2.2");
        assert(info.version == "1.2.2");
        assert(info.name == "Ensemble Release v1.2.2");
        assert(info.htmlUrl == "https://github.com/Mizumo-prjkt/ensemble/releases/tag/v1.2.2");
        assert(info.isUpdateAvailable == true);
        assert(!info.body.isEmpty());

        // Same version -> update not available
        ReleaseInfo infoUpToDate = UpdateChecker::parseReleaseJson(sampleGithubJson, "1.2.2");
        assert(infoUpToDate.isUpdateAvailable == false);

        std::cout << "✓ UpdateChecker semver comparison and GitHub release JSON parsing verified." << std::endl;

        // C. AboutDialog UI with UpdateChecker integration
        AboutDialog aboutDlg;
        auto *updateCard = aboutDlg.findChild<QFrame *>("updateCard");
        assert(updateCard != nullptr);

        bool foundCheckBtn = false;
        bool foundGetBtn = false;
        for (auto *btn : aboutDlg.findChildren<QPushButton *>()) {
            if (btn->text().contains("Check for Updates") || btn->text().contains("Check")) {
                foundCheckBtn = true;
            }
            if (btn->text().contains("Get Update") || btn->text().contains("Get")) {
                foundGetBtn = true;
            }
        }
        assert(foundCheckBtn);
        assert(foundGetBtn);

        // Verify updateTheme works cleanly in both Light and Dark
        ThemeManager::instance().setTheme(AppTheme::Light);
        aboutDlg.updateTheme();
        ThemeManager::instance().setTheme(AppTheme::Dark);
        aboutDlg.updateTheme();

        std::cout << "✓ AboutDialog updater UI components and theme integration verified." << std::endl;
    }

    // 12. Test ProjectLoadingDialog progress bar, labels, clamping, theme styling, and finish lifecycle
    {
        ProjectLoadingDialog loadDlg(QStringLiteral("/home/user/stories/epic_fanfic.ao3proj"));

        // Initial state
        assert(loadDlg.progressValue() == 0);
        assert(!loadDlg.currentStatus().isEmpty());
        assert(loadDlg.windowTitle() == QStringLiteral("Opening Project…"));

        // Progress update
        loadDlg.setProgress(25, QStringLiteral("Decompressing project archive container…"));
        assert(loadDlg.progressValue() == 25);
        assert(loadDlg.currentStatus() == QStringLiteral("Decompressing project archive container…"));

        // Clamping bounds
        loadDlg.setProgress(150, QStringLiteral("Clamped max"));
        assert(loadDlg.progressValue() == 100);

        loadDlg.setProgress(-20, QStringLiteral("Clamped min"));
        assert(loadDlg.progressValue() == 0);

        // Verify updateTheme works across all themes
        ThemeManager::instance().setTheme(AppTheme::Light);
        loadDlg.updateTheme();
        ThemeManager::instance().setTheme(AppTheme::Dark);
        loadDlg.updateTheme();
        ThemeManager::instance().setTheme(AppTheme::Darker);
        loadDlg.updateTheme();
        ThemeManager::instance().setTheme(AppTheme::Ao3Reversi);
        loadDlg.updateTheme();

        // Finish lifecycle
        loadDlg.finish();
        assert(loadDlg.progressValue() == 100);
        assert(loadDlg.currentStatus() == QStringLiteral("Ready!"));

        std::cout << "✓ ProjectLoadingDialog progress, bounds, theme updates, and finish lifecycle verified." << std::endl;
    }

    // 13. Test PluginHostDialog & PluginManagerDialog Close / Dismiss Behavior
    {
        bool closedEmitted = false;
        QString closedWinId;
        auto *hostDlg = new EnsemblePlugin::PluginHostDialog(
            QStringLiteral("author.dialogue-checker"),
            QStringLiteral("win_test_close"),
            QStringLiteral("Dialogue Analysis"),
            450, 320
        );

        QObject::connect(hostDlg, &EnsemblePlugin::PluginHostDialog::dialogClosed,
                         [&](const QString &wid) {
                             closedEmitted = true;
                             closedWinId = wid;
                         });

        hostDlg->addLabel(QStringLiteral("Chapter Dialogue & Quotation Analysis:"));
        hostDlg->addText(QStringLiteral("Total quotes: 0\nLines with unmatched pairs: 0"));
        hostDlg->addButton(QStringLiteral("Close"), QStringLiteral("close_window"));
        hostDlg->show();

        // Find the "Close" button inside the host dialog
        QList<QPushButton *> buttons = hostDlg->findChildren<QPushButton *>();
        QPushButton *closeButton = nullptr;
        for (auto *b : buttons) {
            if (b->text() == QStringLiteral("Close")) {
                closeButton = b;
                break;
            }
        }
        assert(closeButton != nullptr);

        // Click the close button
        closeButton->click();
        qApp->processEvents();

        assert(closedEmitted);
        assert(closedWinId == QStringLiteral("win_test_close"));
        assert(!hostDlg->isVisible());

        // Test PluginManagerDialog close button
        auto *pmDlg = new EnsemblePlugin::PluginManagerDialog();
        pmDlg->show();
        QList<QPushButton *> pmButtons = pmDlg->findChildren<QPushButton *>();
        QPushButton *pmCloseBtn = nullptr;
        for (auto *b : pmButtons) {
            if (b->text() == QStringLiteral("Close")) {
                pmCloseBtn = b;
                break;
            }
        }
        assert(pmCloseBtn != nullptr);
        pmCloseBtn->click();
        qApp->processEvents();
        assert(!pmDlg->isVisible());
        delete pmDlg;

        std::cout << "✓ PluginHostDialog & PluginManagerDialog Close / Dismiss lifecycle verified." << std::endl;
    }

    std::cout << "All Settings, Preferences, Help Guide & Main Menu lifecycle tests passed successfully!" << std::endl;
    return 0;
}
