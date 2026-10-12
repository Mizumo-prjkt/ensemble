#include "debug/debug.hpp"
#include "themes/ThemeManager.h"
#include "ui/AppIcon.h"
#include "ui/MainWindow.h"
#include "utils/SingleInstanceManager.h"
#include "mcp/McpBridge.h"
#include "mcp/McpAgentInstaller.h"

#include <iostream>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSettings>
#include <QSurfaceFormat>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#if defined(ENSEMBLE_HAS_WEBENGINE_QUICK)
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif

int main(int argc, char *argv[]) {
  // 1. Early argument scan for CLI / headless execution BEFORE any Qt or GPU initialization
  bool isMcpBridge = false;
  bool isHeadlessCli = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == QStringLiteral("--mcp-bridge")) {
      isMcpBridge = true;
      isHeadlessCli = true;
      break;
    }
    if (arg == QStringLiteral("--mcp-status") ||
        arg == QStringLiteral("--send-mcp-manual-to-agents") ||
        arg == QStringLiteral("-h") || arg == QStringLiteral("--help") ||
        arg == QStringLiteral("-v") || arg == QStringLiteral("--version")) {
      isHeadlessCli = true;
      break;
    }
  }

  if (isMcpBridge) {
    EnsembleDebug::setMcpBridgeMode(true);
  }

  if (isHeadlessCli) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  EnsembleDebug::initDebugSystem();

  // If in headless CLI mode, use QCoreApplication:
  // This guarantees NO Qt platform plugin (xcb, wayland, etc.) is initialized,
  // preventing crashes when spawned by MCP clients (Claude Desktop, Antigravity) in headless environments.
  if (isHeadlessCli) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Ensemble"));
    app.setOrganizationName(QStringLiteral("Ensemble"));
    app.setApplicationVersion(QStringLiteral(ENSEMBLE_APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Ensemble - Next-Generation AO3 Writing Suite & Work Skin Composer"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption mcpBridgeOption(
        QStringLiteral("mcp-bridge"),
        QStringLiteral("Run Ensemble in Model Context Protocol (MCP) Stdio Bridge mode for AI agents."));
    parser.addOption(mcpBridgeOption);

    QCommandLineOption mcpStatusOption(
        QStringLiteral("mcp-status"),
        QStringLiteral("Query active Ensemble MCP Server status, active port, and verify loopback reachability."));
    parser.addOption(mcpStatusOption);

    QCommandLineOption sendManualOption(
        QStringLiteral("send-mcp-manual-to-agents"),
        QStringLiteral("Export ENSEMBLE_MCP_MANUAL.md and automatically configure MCP tool schemas in Antigravity and Claude settings."));
    parser.addOption(sendManualOption);

    parser.process(app);

    if (parser.isSet(sendManualOption)) {
      McpInstallReport report = McpAgentInstaller::installAll(app.applicationFilePath());
      std::cout << report.summary.toStdString() << std::endl;
      return report.success ? 0 : 1;
    }

    if (parser.isSet(mcpStatusOption)) {
      return McpBridge::runStatusCheck();
    }

    if (parser.isSet(mcpBridgeOption)) {
      return McpBridge::runBridge();
    }

    return 0;
  }

  // 2. GUI Mode Startup: GPU Acceleration & WebEngine Initialization
  bool disableGpu = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == QStringLiteral("--disable-gpu-acceleration") ||
        arg == QStringLiteral("--disable-gpu")) {
      disableGpu = true;
      break;
    }
  }

  bool disableGpuViaSettings = false;
  if (!disableGpu) {
    QSettings userSettings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    if (!userSettings.value(QStringLiteral("webengine/gpuAcceleration"), true).toBool()) {
      disableGpu = true;
      disableGpuViaSettings = true;
    }
  }

  if (disableGpu) {
    if (disableGpuViaSettings) {
      qCInfo(logApp) << "Hardware GPU acceleration disabled via Preferences setting (webengine/gpuAcceleration).";
    } else {
      qCInfo(logApp) << "Hardware GPU acceleration disabled via command-line flag (--disable-gpu-acceleration).";
    }
    qputenv("QT_QUICK_BACKEND", "software");

    const QByteArray existingFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (existingFlags.isEmpty()) {
      qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --disable-gpu-compositing");
    } else if (!existingFlags.contains("--disable-gpu")) {
      qputenv("QTWEBENGINE_CHROMIUM_FLAGS", existingFlags + " --disable-gpu --disable-gpu-compositing");
    }
  } else {
    qCInfo(logApp) << "Hardware GPU acceleration enabled on first window try (Shared OpenGL Contexts).";
    // Required to prevent CPU raster fallback and window recreation when WebEngine initializes
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#if defined(ENSEMBLE_HAS_WEBENGINE_QUICK)
    QtWebEngineQuick::initialize();
#endif

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);

    const QByteArray existingFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    const QByteArray gpuFlags = "--ignore-gpu-blocklist --enable-gpu-rasterization";
    if (existingFlags.isEmpty()) {
      qputenv("QTWEBENGINE_CHROMIUM_FLAGS", gpuFlags);
    } else if (!existingFlags.contains("--ignore-gpu-blocklist")) {
      qputenv("QTWEBENGINE_CHROMIUM_FLAGS", existingFlags + " " + gpuFlags);
    }
  }

  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Ensemble"));
  app.setOrganizationName(QStringLiteral("Ensemble"));
  app.setApplicationVersion(QStringLiteral(ENSEMBLE_APP_VERSION));
  app.setWindowIcon(AppIcon::icon());

  // CLI option parsing & help/version handling for GUI application
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Ensemble - Next-Generation AO3 Writing Suite & Work Skin Composer"));
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption disableGpuOption(
      QStringList() << QStringLiteral("disable-gpu-acceleration")
                    << QStringLiteral("disable-gpu"),
      QStringLiteral("Disable hardware GPU acceleration and force software rasterization."));
  parser.addOption(disableGpuOption);
  QCommandLineOption allowMultipleOption(
      QStringList() << QStringLiteral("allow-multiple-instances")
                    << QStringLiteral("new-instance"),
      QStringLiteral("Allow multiple instances of Ensemble to run concurrently."));
  parser.addOption(allowMultipleOption);

  QCommandLineOption enableMcpOption(
      QStringList() << QStringLiteral("enable-mcp"),
      QStringLiteral("Enable and start the embedded MCP WebSocket server at application launch."));
  parser.addOption(enableMcpOption);

  QCommandLineOption mcpPortOption(
      QStringList() << QStringLiteral("mcp-port"),
      QStringLiteral("Specify preferred port for the MCP WebSocket server (default: 26303)."),
      QStringLiteral("port"));
  parser.addOption(mcpPortOption);

  parser.addPositionalArgument(QStringLiteral("project"),
                               QStringLiteral("Optional path to a .ao3proj file to open on startup."));

  parser.process(app);

  if (parser.isSet(enableMcpOption)) {
    QSettings userSettings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    userSettings.setValue(QStringLiteral("ai/mcpEnabled"), true);
  }

  if (parser.isSet(mcpPortOption)) {
    bool ok = false;
    quint16 customPort = parser.value(mcpPortOption).toUShort(&ok);
    if (ok && customPort > 0) {
      QSettings userSettings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
      userSettings.setValue(QStringLiteral("ai/mcpPort"), customPort);
    }
  }

  QString startupProjectPath;
  const QStringList positionalArgs = parser.positionalArguments();
  if (!positionalArgs.isEmpty()) {
    QString raw = positionalArgs.first().trimmed();
    if ((raw.startsWith(QLatin1Char('"')) && raw.endsWith(QLatin1Char('"'))) ||
        (raw.startsWith(QLatin1Char('\'')) && raw.endsWith(QLatin1Char('\'')))) {
      raw = raw.mid(1, raw.length() - 2).trimmed();
    }
    if (raw.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
      raw = QUrl(raw).toLocalFile();
    }
    const QFileInfo fi(raw);
    startupProjectPath = fi.absoluteFilePath();
  }

  // Single-instance check: detect, warn, bring existing window to front, or terminate if unresponsive
  if (!parser.isSet(allowMultipleOption)) {
    const auto action = SingleInstanceManager::checkAndHandle(false, startupProjectPath);
    if (action == SingleInstanceAction::ExitApplication) {
      return 0;
    }
  }

  // Configure WebEngine settings according to GPU acceleration mode
  auto *profileSettings = QWebEngineProfile::defaultProfile()->settings();
  if (disableGpu) {
    profileSettings->setAttribute(
        QWebEngineSettings::Accelerated2dCanvasEnabled, false);
    profileSettings->setAttribute(
        QWebEngineSettings::WebGLEnabled, false);
  } else {
    profileSettings->setAttribute(
        QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    profileSettings->setAttribute(
        QWebEngineSettings::WebGLEnabled, true);

    // Warm up WebEngine rendering pipeline and GPU compositor FIRST before creating UI
    qCInfo(logApp) << "Pre-initializing WebEngine rendering pipeline before UI creation...";
    {
      QWebEngineView warmUpView;
      warmUpView.setAttribute(Qt::WA_DontShowOnScreen, true);
      warmUpView.setHtml(QStringLiteral("<html><body style='background:#121318;'></body></html>"));
      QCoreApplication::processEvents();
    }
  }

  // Hardware and system diagnostics check (CPU, RAM, installed GPUs, active GPU)
  EnsembleDebug::logHardwareDiagnostics();

  // Initialize and apply theme and skin flavor (Dark, OLED, Light / MaterialYou, KDE, Qt)
  ThemeManager::instance().applyCurrentThemeAndSkin();

  ENSEMBLE_PROFILE_SCOPE("Application Window Initialization");

  MainWindow window;

  // Verify and synchronize AI agent MCP configurations (Antigravity & Claude)
  McpAgentInstaller::checkAndSyncAgentConfigs(app.applicationFilePath());

  // Start single-instance listener to bring this window to front if a second instance launches
  SingleInstanceManager instanceManager(&window);
  QObject::connect(&instanceManager, &SingleInstanceManager::openFileRequested,
                   &window, &MainWindow::openProjectFile);
  instanceManager.startListening();

  // If a project file was provided at startup, open and render it immediately BEFORE window.show()
  // to avoid flashing the Main Menu / Welcome screen.
  if (!startupProjectPath.isEmpty()) {
    window.openProjectFile(startupProjectPath);
  }

  window.show();

  return app.exec();
}

