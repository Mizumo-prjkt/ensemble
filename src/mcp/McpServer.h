#pragma once

#include <QDateTime>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>
#include "AgentActivity.h"

struct McpTestResult {
    bool success = false;
    int latencyMs = -1;
    QString message;
    QString details;
    QStringList checkpoints;
};

struct McpAgentSession {
    QString agentId;
    QString name;        // e.g. "antigravity", "claude"
    QString version;     // e.g. "2.0.0"
    QString role;        // e.g. "co-author", "reviewer"
    QString workingNote; // e.g. "Drafting Chapter 2"
    QString peerAddress; // "127.0.0.1:56720"
    QDateTime connectedAt;
    QDateTime lastSeen;
};

class McpServer : public QObject
{
    Q_OBJECT

public:
    explicit McpServer(QObject *parent = nullptr);
    ~McpServer() override;

    bool start(quint16 preferredPort = 26303);
    void stop();

    bool isRunning() const { return m_isRunning; }
    quint16 port() const { return m_port; }
    QString serverUrl() const;
    QString authToken() const { return m_authToken; }
    int clientCount() const { return m_clients.size(); }
    qint64 messagesReceived() const { return m_messagesReceived; }
    qint64 messagesSent() const { return m_messagesSent; }

    // Multi-Agent Presence Registry
    QList<McpAgentSession> activeAgentSessions() const;
    McpAgentSession agentSessionForClient(QWebSocket *client) const;
    void updateAgentSession(QWebSocket *client,
                            const QString &name = QString(),
                            const QString &version = QString(),
                            const QString &role = QString(),
                            const QString &workingNote = QString());
    void touchAgentActivity(QWebSocket *client);

    // Agent Activities & Thoughts Log
    void logAgentActivity(const AgentActivityEvent &event);
    QList<AgentActivityEvent> recentActivities() const;

    // Path to the local discovery file: ~/.config/Ensemble/mcp.json
    static QString discoveryFilePath();
    static QJsonObject readDiscoveryFile(bool *ok = nullptr);

    // Self-test routine for loopback connectivity and firewall diagnostics
    McpTestResult testLoopback(int timeoutMs = 2500);

    // Send a message to a specific client
    void sendTextMessage(QWebSocket *client, const QString &message);
    // Broadcast a message to all connected clients
    void broadcastTextMessage(const QString &message);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void clientConnected(const QString &peerAddress);
    void clientDisconnected(const QString &peerAddress);
    void messageReceived(QWebSocket *client, const QString &message);
    void agentPresenceChanged(int activeCount, const QStringList &activeAgentNames);
    void agentActivityLogged(const AgentActivityEvent &event);
    void statusChanged();

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onTextMessageReceived(const QString &message);
    void onServerError(QWebSocketProtocol::CloseCode closeCode);

private:
    void writeDiscoveryFile();
    void removeDiscoveryFile();

    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_clients;
    QMap<QWebSocket *, McpAgentSession> m_agentSessions;
    QList<AgentActivityEvent> m_recentActivities;
    int m_agentCounter = 0;
    quint16 m_port = 0;
    QString m_authToken;
    bool m_isRunning = false;
    qint64 m_messagesReceived = 0;
    qint64 m_messagesSent = 0;
};
