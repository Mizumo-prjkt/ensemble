#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class McpServer;
class MainWindow;
class QWebSocket;

class McpHandler : public QObject
{
    Q_OBJECT

public:
    explicit McpHandler(McpServer *server, MainWindow *mainWindow, QObject *parent = nullptr);
    ~McpHandler() override = default;

private slots:
    void onMessageReceived(QWebSocket *client, const QString &message);

private:
    void processRequest(QWebSocket *client, const QJsonObject &request);

    // Protocol handlers
    QJsonObject handleInitialize(QWebSocket *client, const QJsonValue &id, const QJsonObject &params);
    QJsonObject handleToolsList(const QJsonValue &id);
    QJsonObject handleToolsCall(QWebSocket *client, const QJsonValue &id, const QJsonObject &params);
    QJsonObject handleResourcesList(const QJsonValue &id);
    QJsonObject handleResourcesRead(const QJsonValue &id, const QJsonObject &params);
    QJsonObject handlePing(const QJsonValue &id);

    // Tool execution methods
    QJsonObject toolGetProjectSummary(const QJsonObject &args);
    QJsonObject toolListChapters(const QJsonObject &args);
    QJsonObject toolGetChapter(const QJsonObject &args);
    QJsonObject toolUpdateChapterHtml(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolAddChapter(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolDeleteChapter(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolReorderChapters(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetWorkSkinCss(const QJsonObject &args);
    QJsonObject toolSetWorkSkinCss(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetProblems(const QJsonObject &args);
    QJsonObject toolLoadProject(const QJsonObject &args);
    QJsonObject toolSaveProject(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolReloadProject(const QJsonObject &args);
    QJsonObject toolGetProjectHistory(const QJsonObject &args);
    QJsonObject toolAddHistoryCommit(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetSnapshotDiff(const QJsonObject &args);
    QJsonObject toolGetPreviewScreenshot(const QJsonObject &args);
    QJsonObject toolTestFirewallConnection(const QJsonObject &args);
    QJsonObject toolGetAgentPresence(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolRegisterAgentPresence(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetAgenticSummary(const QJsonObject &args);
    QJsonObject toolReadAgenticFile(const QJsonObject &args);
    QJsonObject toolWriteAgenticFile(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolAppendAgentHistory(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetAgentHistory(const QJsonObject &args);
    QJsonObject toolPostAgentThought(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetLoreGraph(const QJsonObject &args);
    QJsonObject toolUpdateLoreGraph(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolAddLoreNode(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolConnectLoreNodes(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolMoveLoreNode(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolGetLoreData(const QJsonObject &args);
    QJsonObject toolManageLoreData(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolUpdateProjectMetadata(QWebSocket *client, const QJsonObject &args);
    QJsonObject toolListImageEmbeds(const QJsonObject &args);
    QJsonObject toolInsertImageEmbed(QWebSocket *client, const QJsonObject &args);

    // Helpers
    static QJsonObject makeSuccessResponse(const QJsonValue &id, const QJsonObject &result);
    static QJsonObject makeErrorResponse(const QJsonValue &id, int code, const QString &message);
    static QJsonObject makeToolResult(const QString &text, bool isError = false);
    static QJsonObject makeToolJsonResult(const QJsonObject &jsonObj, bool isError = false);
    static QJsonObject makeToolImageResult(const QByteArray &pngBytes, const QString &captionText = QString(), bool isError = false);

    McpServer *m_server = nullptr;
    MainWindow *m_mainWindow = nullptr;
};
