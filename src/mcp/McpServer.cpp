#include "McpServer.h"
#include "debug/debug.hpp"
#include "utils/AppPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

McpServer::McpServer(QObject *parent)
    : QObject(parent)
{
}

McpServer::~McpServer()
{
    disconnect();
    stop();
}

QString McpServer::serverUrl() const
{
    if (!m_isRunning || m_port == 0) {
        return QString();
    }
    return QStringLiteral("ws://127.0.0.1:%1").arg(m_port);
}

QString McpServer::discoveryFilePath()
{
    return QDir(AppPaths::appConfigDir()).filePath(QStringLiteral("mcp.json"));
}

QJsonObject McpServer::readDiscoveryFile(bool *ok)
{
    if (ok) *ok = false;
    QFile file(discoveryFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }
    if (ok) *ok = true;
    return doc.object();
}

bool McpServer::start(quint16 preferredPort)
{
    if (m_isRunning) {
        qCDebug(logMcp) << "[MCP-DIAGNOSTIC] MCP Server is already running on port" << m_port;
        return true;
    }

    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 1 (Socket Init): Attempting to start MCP WebSocket server on preferred port"
                   << preferredPort;

    if (!m_server) {
        m_server = new QWebSocketServer(
            QStringLiteral("Ensemble MCP Server"),
            QWebSocketServer::NonSecureMode,
            this);

        connect(m_server, &QWebSocketServer::newConnection,
                this, &McpServer::onNewConnection);
        connect(m_server, &QWebSocketServer::serverError,
                this, &McpServer::onServerError);
    }

    // Bind strictly to localhost (127.0.0.1) for security & to minimize firewall friction
    const QHostAddress host = QHostAddress::LocalHost;

    bool listened = false;
    quint16 chosenPort = preferredPort;

    // 1. Try preferred port
    if (preferredPort > 0) {
        listened = m_server->listen(host, preferredPort);
        if (listened) {
            chosenPort = preferredPort;
        }
    }

    // 2. If preferred port is occupied, try adjacent scan range (preferredPort+1 to +20)
    if (!listened && preferredPort > 0) {
        for (quint16 tryPort = preferredPort + 1; tryPort <= preferredPort + 20; ++tryPort) {
            if (m_server->listen(host, tryPort)) {
                listened = true;
                chosenPort = tryPort;
                qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 1 (Port Fallback): Preferred port" << preferredPort
                              << "in use. Auto-acquired fallback port:" << chosenPort;
                break;
            }
        }
    }

    // 3. If range is exhausted, bind to port 0 (OS dynamic ephemeral allocation)
    if (!listened) {
        if (m_server->listen(host, 0)) {
            listened = true;
            chosenPort = m_server->serverPort();
            qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 1 (Ephemeral Fallback): Auto-acquired dynamic ephemeral port from OS:" << chosenPort;
        }
    }

    if (!listened) {
        qCCritical(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 1 [FAIL]: Failed to start MCP WebSocket server:"
                           << m_server->errorString();
        m_isRunning = false;
        m_port = 0;
        emit statusChanged();
        return false;
    }

    m_port = m_server->serverPort();
    m_isRunning = true;
    m_authToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_messagesReceived = 0;
    m_messagesSent = 0;

    writeDiscoveryFile();

    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 1 [PASS]: MCP WebSocket server listening at" << serverUrl()
                   << "PID:" << QCoreApplication::applicationPid();

    emit serverStarted(m_port);
    emit statusChanged();
    return true;
}

void McpServer::stop()
{
    if (!m_isRunning) {
        if (m_server && m_server->isListening()) {
            m_server->close();
        }
        return;
    }

    qCInfo(logMcp) << "Stopping MCP WebSocket server on port" << m_port;

    for (QWebSocket *client : m_clients) {
        if (client) {
            client->close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("Server shutting down"));
            client->deleteLater();
        }
    }
    m_clients.clear();
    m_agentSessions.clear();

    if (m_server) {
        m_server->close();
    }

    m_isRunning = false;
    m_port = 0;

    removeDiscoveryFile();

    emit serverStopped();
    emit statusChanged();
}

QList<McpAgentSession> McpServer::activeAgentSessions() const
{
    return m_agentSessions.values();
}

McpAgentSession McpServer::agentSessionForClient(QWebSocket *client) const
{
    return m_agentSessions.value(client);
}

void McpServer::updateAgentSession(QWebSocket *client,
                                   const QString &name,
                                   const QString &version,
                                   const QString &role,
                                   const QString &workingNote,
                                   const QString &model)
{
    if (!m_agentSessions.contains(client)) {
        return;
    }
    auto &session = m_agentSessions[client];
    if (!name.trimmed().isEmpty())
        session.name = name.trimmed();
    if (!version.trimmed().isEmpty())
        session.version = version.trimmed();
    if (!role.trimmed().isEmpty())
        session.role = role.trimmed();
    if (!workingNote.isEmpty())
        session.workingNote = workingNote;
    if (!model.trimmed().isEmpty())
        session.model = model.trimmed();
    session.lastSeen = QDateTime::currentDateTimeUtc();

    QStringList names;
    for (const auto &s : m_agentSessions.values()) {
        const QString displayName = !s.model.isEmpty() ? QStringLiteral("%1 [%2] (%3)").arg(s.name, s.model, s.role)
                                                       : QStringLiteral("%1 (%2)").arg(s.name, s.role);
        names.append(displayName);
    }
    emit agentPresenceChanged(m_agentSessions.size(), names);
    emit statusChanged();
}

void McpServer::touchAgentActivity(QWebSocket *client)
{
    if (m_agentSessions.contains(client)) {
        m_agentSessions[client].lastSeen = QDateTime::currentDateTimeUtc();
    }
}

void McpServer::logAgentActivity(const AgentActivityEvent &event)
{
    m_recentActivities.append(event);
    if (m_recentActivities.size() > 500) {
        m_recentActivities.removeFirst();
    }
    emit agentActivityLogged(event);
}

QList<AgentActivityEvent> McpServer::recentActivities() const
{
    return m_recentActivities;
}

void McpServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *client = m_server->nextPendingConnection();
        if (!client) continue;

        // Security check: only allow connections from loopback / localhost
        const QHostAddress peer = client->peerAddress();
        if (peer != QHostAddress::LocalHost && peer != QHostAddress::LocalHostIPv6 &&
            peer.toString() != QStringLiteral("127.0.0.1") && peer.toString() != QStringLiteral("::1")) {
            qCWarning(logMcp) << "Rejected non-localhost connection attempt from" << peer.toString();
            client->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("Localhost only"));
            client->deleteLater();
            continue;
        }

        m_clients.append(client);

        const QString peerStr = QStringLiteral("%1:%2").arg(peer.toString()).arg(client->peerPort());
        qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 3 (Client Connected): MCP Client connected from" << peerStr
                       << "- Total active clients:" << m_clients.size();

        McpAgentSession session;
        session.agentId = QStringLiteral("agent_%1").arg(++m_agentCounter);
        session.name = QStringLiteral("unspecified_agent");
        session.role = QStringLiteral("co-author");
        session.peerAddress = peerStr;
        session.connectedAt = QDateTime::currentDateTimeUtc();
        session.lastSeen = session.connectedAt;
        m_agentSessions.insert(client, session);

        connect(client, &QWebSocket::textMessageReceived,
                this, &McpServer::onTextMessageReceived);
        connect(client, &QWebSocket::disconnected,
                this, &McpServer::onClientDisconnected);

        QStringList names;
        for (const auto &s : m_agentSessions.values()) {
            names.append(QStringLiteral("%1 (%2)").arg(s.name, s.role));
        }

        emit clientConnected(peerStr);
        emit agentPresenceChanged(m_agentSessions.size(), names);
        emit statusChanged();
    }
}

void McpServer::onClientDisconnected()
{
    auto *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    const QString peerStr = QStringLiteral("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
    m_clients.removeAll(client);
    m_agentSessions.remove(client);
    client->deleteLater();

    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint (Client Disconnected):" << peerStr
                   << "- Remaining active clients:" << m_clients.size();

    QStringList names;
    for (const auto &s : m_agentSessions.values()) {
        names.append(QStringLiteral("%1 (%2)").arg(s.name, s.role));
    }

    emit clientDisconnected(peerStr);
    emit agentPresenceChanged(m_agentSessions.size(), names);
    emit statusChanged();
}

void McpServer::onTextMessageReceived(const QString &message)
{
    auto *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    ++m_messagesReceived;
    touchAgentActivity(client);
    qCDebug(logMcp) << "[MCP-DIAGNOSTIC] Received MCP message (" << message.length() << "bytes):" << message;

    emit messageReceived(client, message);
}

void McpServer::onServerError(QWebSocketProtocol::CloseCode closeCode)
{
    Q_UNUSED(closeCode);
    if (m_server) {
        qCWarning(logMcp) << "[MCP-DIAGNOSTIC] Server error:" << m_server->errorString();
    }
}

void McpServer::sendTextMessage(QWebSocket *client, const QString &message)
{
    if (!client || !client->isValid()) {
        return;
    }
    ++m_messagesSent;
    qCDebug(logMcp) << "[MCP-DIAGNOSTIC] Sending MCP message to" << client->peerAddress().toString()
                    << "(" << message.length() << "bytes)";
    client->sendTextMessage(message);
}

void McpServer::broadcastTextMessage(const QString &message)
{
    for (QWebSocket *client : m_clients) {
        sendTextMessage(client, message);
    }
}

void McpServer::writeDiscoveryFile()
{
    AppPaths::ensureDirectoriesExist();
    const QString path = discoveryFilePath();

    QJsonObject obj;
    obj[QStringLiteral("status")] = QStringLiteral("running");
    obj[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    obj[QStringLiteral("port")] = static_cast<int>(m_port);
    obj[QStringLiteral("url")] = serverUrl();
    obj[QStringLiteral("authToken")] = m_authToken;
    obj[QStringLiteral("pid")] = static_cast<qint64>(QCoreApplication::applicationPid());
    obj[QStringLiteral("appVersion")] = QCoreApplication::applicationVersion();
    obj[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        file.close();
        qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 2 (Discovery Manifest): Wrote discovery manifest to" << path;
    } else {
        qCWarning(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 2 [FAIL]: Could not write MCP discovery file to" << path;
    }
}

void McpServer::removeDiscoveryFile()
{
    const QString path = discoveryFilePath();
    if (QFile::exists(path)) {
        // Mark as stopped or remove
        QJsonObject obj;
        obj[QStringLiteral("status")] = QStringLiteral("stopped");
        obj[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
            file.close();
        }
    }
}

McpTestResult McpServer::testLoopback(int timeoutMs)
{
    McpTestResult result;
    QStringList checkpoints;

    // Checkpoint 1: Server Socket State
    if (!m_isRunning || m_port == 0) {
        checkpoints << QStringLiteral("❌ [FAIL] Checkpoint 1 (Server Socket): Server is offline or port is unassigned.");
        result.success = false;
        result.message = QStringLiteral("MCP Server is not currently running.");
        result.details = QStringLiteral("Enable the server in Preferences > AI Settings first.");
        result.checkpoints = checkpoints;
        qCWarning(logMcp) << "[MCP-DIAGNOSTIC] Loopback test failed at Checkpoint 1: server not running";
        return result;
    }

    checkpoints << QStringLiteral("✅ [PASS] Checkpoint 1 (Server Socket): Bound to 127.0.0.1:%1 (PID %2)")
                       .arg(m_port).arg(QCoreApplication::applicationPid());
    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 1 [PASS]: Server socket listening on 127.0.0.1:" << m_port;

    // Checkpoint 2: Discovery Manifest verification
    bool discOk = false;
    QJsonObject discObj = readDiscoveryFile(&discOk);
    if (discOk && discObj.value(QStringLiteral("status")).toString() == QStringLiteral("running")) {
        checkpoints << QStringLiteral("✅ [PASS] Checkpoint 2 (Discovery Manifest): Manifest verified at %1")
                           .arg(discoveryFilePath());
        qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 2 [PASS]: Discovery manifest verified at" << discoveryFilePath();
    } else {
        checkpoints << QStringLiteral("⚠️ [WARN] Checkpoint 2 (Discovery Manifest): Manifest missing or outdated at %1")
                           .arg(discoveryFilePath());
        qCWarning(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 2 [WARN]: Discovery manifest missing or outdated at" << discoveryFilePath();
    }

    // Checkpoints 3 & 4: Loopback connection probe & Ping/Pong frame echo
    QElapsedTimer timer;
    timer.start();

    QWebSocket testSocket;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(timeoutMs);

    bool connected = false;
    bool pingReplied = false;
    QString errorStr;
    qint64 connectElapsed = -1;

    connect(&testSocket, &QWebSocket::connected, &loop, [&]() {
        connected = true;
        connectElapsed = timer.elapsed();
        qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 3 [PASS]: Loopback probe connected in" << connectElapsed << "ms";
        // Send a ping to measure complete loopback roundtrip
        testSocket.ping(QByteArrayLiteral("loopback_probe"));
    });

    connect(&testSocket, &QWebSocket::pong, &loop, [&](quint64, const QByteArray &payload) {
        if (payload == QByteArrayLiteral("loopback_probe")) {
            pingReplied = true;
            loop.quit();
        }
    });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(&testSocket, &QWebSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError) {
#else
    connect(&testSocket, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), &loop, [&](QAbstractSocket::SocketError) {
#endif
        errorStr = testSocket.errorString();
        loop.quit();
    });

    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        if (!connected) {
            errorStr = QStringLiteral("Connection attempt timed out after %1 ms.").arg(timeoutMs);
        } else if (!pingReplied) {
            errorStr = QStringLiteral("WebSocket connected, but ping reply timed out after %1 ms.").arg(timeoutMs);
        }
        loop.quit();
    });

    timeoutTimer.start();
    testSocket.open(QUrl(serverUrl()));
    loop.exec();

    testSocket.close();

    const qint64 elapsed = timer.elapsed();

    if (connected) {
        checkpoints << QStringLiteral("✅ [PASS] Checkpoint 3 (Loopback Handshake): Connected to %1 in %2 ms")
                           .arg(serverUrl()).arg(connectElapsed >= 0 ? connectElapsed : elapsed);
    } else {
        checkpoints << QStringLiteral("❌ [FAIL] Checkpoint 3 (Loopback Handshake): Failed to connect within %1 ms (%2)")
                           .arg(timeoutMs).arg(errorStr);
    }

    if (pingReplied) {
        checkpoints << QStringLiteral("✅ [PASS] Checkpoint 4 (Frame Echo & Latency): Roundtrip completed in %1 ms")
                           .arg(elapsed);
        checkpoints << QStringLiteral("✅ [PASS] Checkpoint 5 (Security Policy): Localhost loopback connection accepted.");
        qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 4 & 5 [PASS]: Ping/Pong roundtrip" << elapsed
                       << "ms, loopback policy accepted";

        result.success = true;
        result.latencyMs = static_cast<int>(elapsed);
        result.message = QStringLiteral("Loopback connection verified successfully.");
        result.details = QStringLiteral("Localhost socket is open and responsive. Roundtrip latency: %1 ms.").arg(elapsed);
    } else {
        if (connected) {
            checkpoints << QStringLiteral("❌ [FAIL] Checkpoint 4 (Frame Echo & Latency): Ping/Pong frame echo failed (%1)")
                               .arg(errorStr);
        }
        qCWarning(logMcp) << "[MCP-DIAGNOSTIC] Loopback diagnostic test failed:" << errorStr;

        result.success = false;
        result.latencyMs = static_cast<int>(elapsed);
        result.message = QStringLiteral("Loopback test failed: %1").arg(errorStr);
        result.details = QStringLiteral(
            "Host: 127.0.0.1, Port: %1.\n"
            "Diagnostic Hint: Your local firewall, antivirus, or loopback proxy may be blocking "
            "incoming connections to localhost sockets on port %1. "
            "Verify that loopback communication is permitted for Ensemble.").arg(m_port);
    }

    result.checkpoints = checkpoints;
    return result;
}
