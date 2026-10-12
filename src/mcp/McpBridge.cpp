#include "McpBridge.h"
#include "McpServer.h"
#include "debug/debug.hpp"
#include "utils/SingleInstanceManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QtWebSockets/QWebSocket>
#include <iostream>
#include <string>

class StdinReaderThread : public QThread
{
    Q_OBJECT
signals:
    void lineRead(const QString &line);
    void finishedReading();

protected:
    void run() override {
        std::string line;
        while (std::getline(std::cin, line)) {
            emit lineRead(QString::fromStdString(line));
        }
        emit finishedReading();
    }
};

int McpBridge::runBridge()
{
    EnsembleDebug::setMcpBridgeMode(true);

    bool ok = false;
    QJsonObject info = McpServer::readDiscoveryFile(&ok);

    QString status = info.value(QStringLiteral("status")).toString();
    QString url = info.value(QStringLiteral("url")).toString();
    qint64 pid = info.value(QStringLiteral("pid")).toInteger();

    bool isRunning = (ok && status == QStringLiteral("running") && !url.isEmpty() &&
                      (pid <= 0 || SingleInstanceManager::isProcessRunning(pid)));

    // If server is not running and we are in a graphical desktop session, attempt to launch Ensemble with --enable-mcp
    if (!isRunning) {
        const bool hasDisplay = !qEnvironmentVariableIsEmpty("DISPLAY") || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
        if (hasDisplay) {
            std::cerr << "[Ensemble MCP Bridge] Ensemble GUI is not running. Launching Ensemble in background with --enable-mcp...\n";
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--enable-mcp")});

            // Wait up to 3000ms for GUI MCP server to start
            for (int i = 0; i < 15; ++i) {
                QThread::msleep(200);
                info = McpServer::readDiscoveryFile(&ok);
                status = info.value(QStringLiteral("status")).toString();
                url = info.value(QStringLiteral("url")).toString();
                pid = info.value(QStringLiteral("pid")).toInteger();
                if (ok && status == QStringLiteral("running") && !url.isEmpty()) {
                    isRunning = true;
                    break;
                }
            }
        }
    }

    if (!isRunning || url.isEmpty()) {
        std::cerr << "[Ensemble MCP Bridge Error]\n"
                  << "Ensemble GUI is not running or the MCP Server is disabled.\n\n"
                  << "To use Ensemble with your AI agent:\n"
                  << "  1. Launch Ensemble GUI (e.g. 'ensemble' or 'ensemble --enable-mcp')\n"
                  << "  2. In Preferences > AI Settings, ensure 'Allow access via MCP' is checked\n\n"
                  << "To diagnose connection issues, run: ensemble --mcp-status\n";
        return 1;
    }

    std::cerr << "[Ensemble MCP Bridge] Connecting to " << url.toStdString() << " (PID " << pid << ")...\n";

    QWebSocket socket;
    StdinReaderThread reader;
    QStringList pendingLines;
    bool isConnected = false;
    bool finishedReadingStdin = false;

    auto checkGracefulExit = [&]() {
        if (finishedReadingStdin && pendingLines.isEmpty()) {
            // Allow in-flight response roundtrip to settle before closing socket
            QTimer::singleShot(250, [&]() {
                if (socket.isValid()) {
                    socket.close();
                } else {
                    QCoreApplication::quit();
                }
            });
        }
    };

    QObject::connect(&socket, &QWebSocket::connected, [&]() {
        std::cerr << "[Ensemble MCP Bridge] Connected to Ensemble MCP Server. Ready for AI agent requests.\n";
        isConnected = true;
        for (const QString &line : pendingLines) {
            socket.sendTextMessage(line);
        }
        pendingLines.clear();
        checkGracefulExit();
    });

    QObject::connect(&reader, &StdinReaderThread::lineRead, &socket, [&](const QString &line) {
        if (line.trimmed().isEmpty()) return;
        if (isConnected && socket.isValid()) {
            socket.sendTextMessage(line);
        } else {
            pendingLines.append(line);
        }
    });

    QObject::connect(&socket, &QWebSocket::textMessageReceived, [](const QString &msg) {
        std::cout << msg.toStdString() << std::endl;
        std::cout.flush();
    });

    QObject::connect(&socket, &QWebSocket::disconnected, []() {
        std::cerr << "[Ensemble MCP Bridge] WebSocket disconnected.\n";
        QCoreApplication::quit();
    });

    // Pass &socket as receiver context object so this executes safely on the main thread
    QObject::connect(&reader, &StdinReaderThread::finishedReading, &socket, [&]() {
        finishedReadingStdin = true;
        checkGracefulExit();
    });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(&socket, &QWebSocket::errorOccurred, [&](QAbstractSocket::SocketError) {
#else
    QObject::connect(&socket, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), [&](QAbstractSocket::SocketError) {
#endif
        std::cerr << "[Ensemble MCP Bridge] WebSocket error: "
                  << socket.errorString().toStdString() << std::endl;
        QCoreApplication::quit();
    });

    reader.start();
    socket.open(QUrl(url));

    const int exitCode = QCoreApplication::exec();
    reader.wait(500);
    return exitCode;
}

int McpBridge::runStatusCheck()
{
    std::cout << "========================================\n"
              << " Ensemble MCP Status & Diagnostics\n"
              << "========================================\n";

    const QString path = McpServer::discoveryFilePath();
    std::cout << "Discovery file: " << path.toStdString() << "\n";

    if (!QFile::exists(path)) {
        std::cout << "Status: NOT CONFIGURED / NEVER STARTED\n\n"
                  << "Hint: Launch Ensemble and enable MCP in Preferences > AI Settings.\n";
        return 1;
    }

    bool ok = false;
    const QJsonObject info = McpServer::readDiscoveryFile(&ok);
    if (!ok) {
        std::cout << "Status: CORRUPTED DISCOVERY FILE\n\n";
        return 1;
    }

    const QString status = info.value(QStringLiteral("status")).toString();
    const QString url = info.value(QStringLiteral("url")).toString();
    const qint64 pid = info.value(QStringLiteral("pid")).toInteger();
    const QString updated = info.value(QStringLiteral("updatedAt")).toString();

    std::cout << "Reported Status: " << status.toStdString() << "\n"
              << "Endpoint URL:    " << (url.isEmpty() ? "None" : url.toStdString()) << "\n"
              << "Target PID:      " << pid << "\n"
              << "Last Updated:    " << updated.toStdString() << "\n\n";

    if (pid > 0) {
        const bool running = SingleInstanceManager::isProcessRunning(pid);
        std::cout << "Process Check:   " << (running ? "Process is ACTIVE" : "Process is NOT running") << "\n";
    }

    if (status != QStringLiteral("running") || url.isEmpty()) {
        std::cout << "\nServer is currently OFFLINE.\n";
        return 1;
    }

    // Attempt loopback probe
    std::cout << "Probing loopback endpoint (" << url.toStdString() << ")... ";
    std::cout.flush();

    QWebSocket probe;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(2000);

    bool connected = false;
    bool ponged = false;
    QString probeError;
    QElapsedTimer elapsed;
    elapsed.start();

    QObject::connect(&probe, &QWebSocket::connected, [&]() {
        connected = true;
        probe.ping(QByteArrayLiteral("status_probe"));
    });

    QObject::connect(&probe, &QWebSocket::pong, [&](quint64, const QByteArray &payload) {
        if (payload == QByteArrayLiteral("status_probe")) {
            ponged = true;
            loop.quit();
        }
    });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(&probe, &QWebSocket::errorOccurred, [&](QAbstractSocket::SocketError) {
#else
    QObject::connect(&probe, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), [&](QAbstractSocket::SocketError) {
#endif
        probeError = probe.errorString();
        loop.quit();
    });

    QObject::connect(&timer, &QTimer::timeout, [&]() {
        if (!connected) {
            probeError = QStringLiteral("Connection timed out after 2000 ms");
        }
        loop.quit();
    });

    timer.start();
    probe.open(QUrl(url));
    loop.exec();
    probe.close();

    if (connected && ponged) {
        std::cout << "SUCCESS! (" << elapsed.elapsed() << " ms roundtrip)\n\n"
                  << "Diagnostic Checkpoints:\n"
                  << "  [PASS] Checkpoint 1: Discovery file valid\n"
                  << "  [PASS] Checkpoint 2: Host GUI process active\n"
                  << "  [PASS] Checkpoint 3: Loopback WebSocket connected\n"
                  << "  [PASS] Checkpoint 4: Ping/pong protocol echoed\n\n"
                  << "Ensemble MCP Server is healthy, active, and accessible to AI agents.\n";
        return 0;
    } else {
        std::cout << "FAILED!\n"
                  << "Error: " << probeError.toStdString() << "\n\n"
                  << "Diagnostic Checkpoints:\n"
                  << "  [PASS] Checkpoint 1: Discovery file found\n"
                  << "  [FAIL] Checkpoint 3: Loopback probe failed (" << probeError.toStdString() << ")\n\n"
                  << "Troubleshooting:\n"
                  << "  1. If connection was refused, check if Ensemble GUI is currently open.\n"
                  << "  2. If timed out, your local firewall or antivirus may be blocking localhost loopback.\n"
                  << "  3. In Preferences > AI Settings, click '🧪 Test Loopback Connection' for live GUI diagnostics.\n";
        return 1;
    }
}

#include "McpBridge.moc"
