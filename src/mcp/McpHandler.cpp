#include "McpHandler.h"
#include "McpServer.h"
#include "McpToolDefinitions.h"
#include "debug/debug.hpp"
#include "export/Ao3HtmlSanitizer.h"
#include "model/Ao3Project.h"
#include "model/Chapter.h"
#include "model/ProjectSerializer.h"
#include "ui/CssEditorPane.h"
#include "ui/MainWindow.h"
#include "utils/DiffUtil.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocument>
#include <QtWebSockets/QWebSocket>

McpHandler::McpHandler(McpServer *server, MainWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_mainWindow(mainWindow)
{
    if (m_server) {
        connect(m_server, &McpServer::messageReceived,
                this, &McpHandler::onMessageReceived);
    }
}

void McpHandler::onMessageReceived(QWebSocket *client, const QString &message)
{
    if (!client || !m_server) return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(logMcp) << "Invalid JSON-RPC format:" << parseError.errorString();
        QJsonObject err = makeErrorResponse(QJsonValue(), -32700, QStringLiteral("Parse error"));
        m_server->sendTextMessage(client, QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact)));
        return;
    }

    processRequest(client, doc.object());
}

void McpHandler::processRequest(QWebSocket *client, const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    qCDebug(logMcp) << "[MCP-DIAGNOSTIC] Dispatching RPC request: method =" << method << "id =" << id;

    // Handle notifications (no id)
    if (id.isUndefined() || id.isNull()) {
        if (method == QStringLiteral("notifications/initialized")) {
            qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 5 (Client Initialized): Received notifications/initialized - Handshake complete, AI agent ready.";
        }
        return;
    }

    QJsonObject response;

    if (method == QStringLiteral("initialize")) {
        response = handleInitialize(client, id, params);
    } else if (method == QStringLiteral("ping")) {
        response = handlePing(id);
    } else if (method == QStringLiteral("tools/list")) {
        response = handleToolsList(id);
    } else if (method == QStringLiteral("tools/call")) {
        response = handleToolsCall(client, id, params);
    } else if (method == QStringLiteral("resources/list")) {
        response = handleResourcesList(id);
    } else if (method == QStringLiteral("resources/read")) {
        response = handleResourcesRead(id, params);
    } else {
        qCWarning(logMcp) << "Unknown or unsupported MCP method:" << method;
        response = makeErrorResponse(id, -32601, QStringLiteral("Method not found"));
    }

    m_server->sendTextMessage(client, QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
}

namespace {
QString resolveCallerModel(const McpAgentSession &caller, const QJsonObject &args = QJsonObject())
{
    if (args.contains(QStringLiteral("model")) && !args.value(QStringLiteral("model")).toString().trimmed().isEmpty()) {
        return args.value(QStringLiteral("model")).toString().trimmed();
    }
    if (!caller.model.isEmpty() && caller.model != QStringLiteral("0.1.0") && caller.model != caller.name) {
        return caller.model;
    }
    return QStringLiteral("unspecified_model");
}
} // namespace

QJsonObject McpHandler::handleInitialize(QWebSocket *client, const QJsonValue &id, const QJsonObject &params)
{
    const QString clientProto = params.value(QStringLiteral("protocolVersion")).toString();
    const QJsonObject clientInfoObj = params.value(QStringLiteral("clientInfo")).toObject();
    const QString clientName = clientInfoObj.value(QStringLiteral("name")).toString();
    const QString clientVersion = clientInfoObj.value(QStringLiteral("version")).toString();
    const QString clientModel = clientInfoObj.contains(QStringLiteral("model"))
                                    ? clientInfoObj.value(QStringLiteral("model")).toString().trimmed()
                                    : QString();

    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Checkpoint 4 (Protocol Handshake): Client initialize requested by"
                   << (clientName.isEmpty() ? QStringLiteral("AI agent") : clientName)
                   << "model:" << (clientModel.isEmpty() ? QStringLiteral("(unspecified)") : clientModel)
                   << "client protocolVersion:" << (clientProto.isEmpty() ? QStringLiteral("(unspecified)") : clientProto)
                   << "- Responding with server protocolVersion 2024-11-05";

    if (m_server && client) {
        m_server->updateAgentSession(client,
                                     clientName.isEmpty() ? QStringLiteral("ai_agent") : clientName,
                                     clientVersion,
                                     QStringLiteral("co-author"),
                                     QStringLiteral("Connected session"),
                                     clientModel);
    }

    QJsonObject capabilities;
    QJsonObject toolsCap;
    toolsCap[QStringLiteral("listChanged")] = true;
    capabilities[QStringLiteral("tools")] = toolsCap;

    QJsonObject resourcesCap;
    resourcesCap[QStringLiteral("listChanged")] = true;
    resourcesCap[QStringLiteral("subscribe")] = false;
    capabilities[QStringLiteral("resources")] = resourcesCap;

    QJsonObject serverInfo;
    serverInfo[QStringLiteral("name")] = QStringLiteral("ensemble");
    serverInfo[QStringLiteral("version")] = QStringLiteral(ENSEMBLE_APP_VERSION);

    QJsonObject result;
    result[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
    result[QStringLiteral("capabilities")] = capabilities;
    result[QStringLiteral("serverInfo")] = serverInfo;

    return makeSuccessResponse(id, result);
}

QJsonObject McpHandler::handlePing(const QJsonValue &id)
{
    return makeSuccessResponse(id, QJsonObject());
}

QJsonObject McpHandler::handleToolsList(const QJsonValue &id)
{
    QJsonObject result;
    result[QStringLiteral("tools")] = getMcpAllToolDefinitions();
    return makeSuccessResponse(id, result);
}

QJsonObject McpHandler::handleToolsCall(QWebSocket *client, const QJsonValue &id, const QJsonObject &params)
{
    const QString toolName = params.value(QStringLiteral("name")).toString();
    const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();

    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Tool Call Begin:" << toolName
                   << "- Arguments count:" << args.keys().size();

    QElapsedTimer toolTimer;
    toolTimer.start();

    QJsonObject toolResult;

    // If caller provided a model in arguments, update the session model cache
    if (client && m_server && args.contains(QStringLiteral("model"))) {
        const QString argModel = args.value(QStringLiteral("model")).toString().trimmed();
        if (!argModel.isEmpty()) {
            m_server->updateAgentSession(client, QString(), QString(), QString(), QString(), argModel);
        }
    }

    if (toolName == QStringLiteral("get_project_summary")) {
        toolResult = toolGetProjectSummary(args);
    } else if (toolName == QStringLiteral("list_chapters")) {
        toolResult = toolListChapters(args);
    } else if (toolName == QStringLiteral("get_chapter")) {
        toolResult = toolGetChapter(args);
    } else if (toolName == QStringLiteral("update_chapter_html")) {
        toolResult = toolUpdateChapterHtml(client, args);
    } else if (toolName == QStringLiteral("add_chapter")) {
        toolResult = toolAddChapter(client, args);
    } else if (toolName == QStringLiteral("delete_chapter")) {
        toolResult = toolDeleteChapter(client, args);
    } else if (toolName == QStringLiteral("reorder_chapters")) {
        toolResult = toolReorderChapters(client, args);
    } else if (toolName == QStringLiteral("get_workskin_css")) {
        toolResult = toolGetWorkSkinCss(args);
    } else if (toolName == QStringLiteral("set_workskin_css")) {
        toolResult = toolSetWorkSkinCss(client, args);
    } else if (toolName == QStringLiteral("get_problems")) {
        toolResult = toolGetProblems(args);
    } else if (toolName == QStringLiteral("load_project") || toolName == QStringLiteral("open_project")) {
        toolResult = toolLoadProject(args);
    } else if (toolName == QStringLiteral("save_project")) {
        toolResult = toolSaveProject(client, args);
    } else if (toolName == QStringLiteral("reload_project") || toolName == QStringLiteral("discard_changes")) {
        toolResult = toolReloadProject(args);
    } else if (toolName == QStringLiteral("get_project_history") || toolName == QStringLiteral("get_history_commits") || toolName == QStringLiteral("get_snapshots")) {
        toolResult = toolGetProjectHistory(args);
    } else if (toolName == QStringLiteral("add_history_commit") || toolName == QStringLiteral("commit_history") || toolName == QStringLiteral("add_snapshot") || toolName == QStringLiteral("create_snapshot")) {
        toolResult = toolAddHistoryCommit(client, args);
    } else if (toolName == QStringLiteral("get_snapshot_diff") || toolName == QStringLiteral("get_commit_diff")) {
        toolResult = toolGetSnapshotDiff(args);
    } else if (toolName == QStringLiteral("get_preview_screenshot")) {
        toolResult = toolGetPreviewScreenshot(args);
    } else if (toolName == QStringLiteral("get_agent_presence") || toolName == QStringLiteral("get_connected_agents")) {
        toolResult = toolGetAgentPresence(client, args);
    } else if (toolName == QStringLiteral("register_agent_presence") || toolName == QStringLiteral("announce_agent_presence")) {
        toolResult = toolRegisterAgentPresence(client, args);
    } else if (toolName == QStringLiteral("get_agentic_summary") || toolName == QStringLiteral("get_agentic_index")) {
        toolResult = toolGetAgenticSummary(args);
    } else if (toolName == QStringLiteral("read_agentic_file")) {
        toolResult = toolReadAgenticFile(args);
    } else if (toolName == QStringLiteral("write_agentic_file")) {
        toolResult = toolWriteAgenticFile(client, args);
    } else if (toolName == QStringLiteral("append_agent_history")) {
        toolResult = toolAppendAgentHistory(client, args);
    } else if (toolName == QStringLiteral("get_agent_history") || toolName == QStringLiteral("get_model_history")) {
        toolResult = toolGetAgentHistory(args);
    } else if (toolName == QStringLiteral("post_agent_thought") || toolName == QStringLiteral("share_agent_thought")) {
        toolResult = toolPostAgentThought(client, args);
    } else if (toolName == QStringLiteral("get_lore_graph")) {
        toolResult = toolGetLoreGraph(args);
    } else if (toolName == QStringLiteral("update_lore_graph")) {
        toolResult = toolUpdateLoreGraph(client, args);
    } else if (toolName == QStringLiteral("add_lore_node")) {
        toolResult = toolAddLoreNode(client, args);
    } else if (toolName == QStringLiteral("connect_lore_nodes")) {
        toolResult = toolConnectLoreNodes(client, args);
    } else if (toolName == QStringLiteral("move_lore_node")) {
        toolResult = toolMoveLoreNode(client, args);
    } else if (toolName == QStringLiteral("get_lore_data")) {
        toolResult = toolGetLoreData(args);
    } else if (toolName == QStringLiteral("manage_lore_data")) {
        toolResult = toolManageLoreData(client, args);
    } else if (toolName == QStringLiteral("update_project_metadata")) {
        toolResult = toolUpdateProjectMetadata(client, args);
    } else if (toolName == QStringLiteral("list_image_embeds")) {
        toolResult = toolListImageEmbeds(args);
    } else if (toolName == QStringLiteral("insert_image_embed")) {
        toolResult = toolInsertImageEmbed(client, args);
    } else if (toolName == QStringLiteral("test_firewall_connection")) {
        toolResult = toolTestFirewallConnection(args);
    } else {
        qCWarning(logMcp) << "[MCP-DIAGNOSTIC] Tool Call Unknown:" << toolName;
        return makeErrorResponse(id, -32601, QStringLiteral("Tool '%1' not found").arg(toolName));
    }

    const qint64 elapsed = toolTimer.elapsed();
    const bool isErr = toolResult.value(QStringLiteral("isError")).toBool();
    qCInfo(logMcp) << "[MCP-DIAGNOSTIC] Tool Call Finished:" << toolName
                   << "in" << elapsed << "ms"
                   << (isErr ? QStringLiteral("(Result: ERROR)") : QStringLiteral("(Result: SUCCESS)"));

    // Log failed tool executions to persistent agentic audit log
    if (isErr && m_mainWindow && m_mainWindow->hasActiveProject()) {
        const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
        const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
        const QString callerModel = resolveCallerModel(caller, args);
        QString errText;
        QJsonArray contentArr = toolResult.value(QStringLiteral("content")).toArray();
        if (!contentArr.isEmpty()) {
            errText = contentArr.first().toObject().value(QStringLiteral("text")).toString();
        }
        if (errText.isEmpty()) {
            errText = toolResult.value(QStringLiteral("error")).toString();
        }
        if (errText.isEmpty()) {
            errText = QStringLiteral("Unknown tool execution error.");
        }
        m_mainWindow->project().agenticData().appendHistoryEntry(
            modifier, callerModel, caller.role,
            QStringLiteral("%1 (failed)").arg(toolName),
            QStringLiteral("Tool execution failed: %1").arg(errText),
            -1, 0,
            QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))
        );
    }

    // Log live activity and model thoughts for UI inspector
    if (m_server) {
        const auto session = client ? m_server->agentSessionForClient(client) : McpAgentSession();
        AgentActivityEvent event;
        event.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        event.timestamp = QDateTime::currentDateTimeUtc();
        event.agentName = session.name.isEmpty() ? QStringLiteral("AI Agent") : session.name;
        event.role = session.role.isEmpty() ? QStringLiteral("Co-author") : session.role;
        event.durationMs = static_cast<int>(elapsed);
        event.inputData = args;
        event.resultData = toolResult;
        event.isError = isErr;

        if (toolName == QStringLiteral("post_agent_thought") || toolName == QStringLiteral("share_agent_thought")) {
            event.type = AgentActivityEvent::Type::Thought;
            const QString category = args.value(QStringLiteral("category")).toString();
            const QString target = args.value(QStringLiteral("target")).toString();
            event.title = category.isEmpty() ? QStringLiteral("Critique & Reasoning") : QStringLiteral("Thought: %1").arg(category);
            if (!target.isEmpty()) {
                event.title += QStringLiteral(" [%1]").arg(target);
            }
            event.thought = args.value(QStringLiteral("thought")).toString();
        } else if (toolName.startsWith(QStringLiteral("get_")) || toolName.startsWith(QStringLiteral("read_")) || toolName.startsWith(QStringLiteral("list_"))) {
            event.type = AgentActivityEvent::Type::DataInspection;
            event.title = QStringLiteral("Read Project Data: %1").arg(toolName);
            if (args.contains(QStringLiteral("thought"))) {
                event.thought = args.value(QStringLiteral("thought")).toString();
            } else if (args.contains(QStringLiteral("reasoning"))) {
                event.thought = args.value(QStringLiteral("reasoning")).toString();
            } else {
                event.thought = QStringLiteral("Reading %1 to evaluate story context.").arg(toolName);
            }
        } else if (toolName.startsWith(QStringLiteral("update_")) || toolName.startsWith(QStringLiteral("write_")) ||
                   toolName.startsWith(QStringLiteral("set_")) || toolName.startsWith(QStringLiteral("add_")) ||
                   toolName.startsWith(QStringLiteral("delete_")) || toolName.startsWith(QStringLiteral("reorder_"))) {
            event.type = AgentActivityEvent::Type::DataModification;
            event.title = QStringLiteral("Modify Project Data: %1").arg(toolName);
            if (args.contains(QStringLiteral("thought"))) {
                event.thought = args.value(QStringLiteral("thought")).toString();
            } else if (args.contains(QStringLiteral("reasoning"))) {
                event.thought = args.value(QStringLiteral("reasoning")).toString();
            } else {
                event.thought = QStringLiteral("Executing %1 on active project.").arg(toolName);
            }
        } else if (toolName == QStringLiteral("register_agent_presence") || toolName == QStringLiteral("announce_agent_presence")) {
            event.type = AgentActivityEvent::Type::PresenceChange;
            event.title = QStringLiteral("Agent Presence Announced: %1").arg(event.agentName);
            event.thought = session.workingNote.isEmpty() ? QStringLiteral("Ready for collaborative writing.") : session.workingNote;
        } else {
            event.type = AgentActivityEvent::Type::ToolExecution;
            event.title = QStringLiteral("Tool: %1").arg(toolName);
            event.thought = args.value(QStringLiteral("thought")).toString();
        }

        m_server->logAgentActivity(event);
    }

    return makeSuccessResponse(id, toolResult);
}

QJsonObject McpHandler::handleResourcesList(const QJsonValue &id)
{
    QJsonArray resources;

    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://project");
        r[QStringLiteral("name")] = QStringLiteral("Active Project Metadata");
        r[QStringLiteral("description")] = QStringLiteral("Structured metadata of the current AO3 work");
        r[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://workskin/css");
        r[QStringLiteral("name")] = QStringLiteral("Active Work Skin CSS");
        r[QStringLiteral("description")] = QStringLiteral("Custom AO3 CSS styles for this work");
        r[QStringLiteral("mimeType")] = QStringLiteral("text/css");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://problems");
        r[QStringLiteral("name")] = QStringLiteral("Current Lint Diagnostics");
        r[QStringLiteral("description")] = QStringLiteral("Real-time list of AO3 HTML & CSS warnings");
        r[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://agents");
        r[QStringLiteral("name")] = QStringLiteral("Active AI Agents Registry");
        r[QStringLiteral("description")] = QStringLiteral("Real-time list of AI models and agents connected to Ensemble for co-working");
        r[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://agentic/index");
        r[QStringLiteral("name")] = QStringLiteral("Agentic Data Storage Index & Quotas");
        r[QStringLiteral("description")] = QStringLiteral("JSON index of active project agentic storage: quotas (5MB shared, 20MB localized), usage, and file listings");
        r[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://agentic/history");
        r[QStringLiteral("name")] = QStringLiteral("Agent Model History Log");
        r[QStringLiteral("description")] = QStringLiteral("XML log of model interactions and co-writing actions stored in agent_model_history.xml");
        r[QStringLiteral("mimeType")] = QStringLiteral("application/xml");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://lore/graph");
        r[QStringLiteral("name")] = QStringLiteral("Story Lore Graph (.grph)");
        r[QStringLiteral("description")] = QStringLiteral("Active DSL script and narrative mapping from lore/graphs/data.grph");
        r[QStringLiteral("mimeType")] = QStringLiteral("text/plain");
        resources.append(r);
    }
    {
        QJsonObject r;
        r[QStringLiteral("uri")] = QStringLiteral("ensemble://lore/data");
        r[QStringLiteral("name")] = QStringLiteral("Story Lore Data (JSON Summary)");
        r[QStringLiteral("description")] = QStringLiteral("Entities, history, conflicts, plans, and references");
        r[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        resources.append(r);
    }

    QJsonObject result;
    result[QStringLiteral("resources")] = resources;
    return makeSuccessResponse(id, result);
}

QJsonObject McpHandler::handleResourcesRead(const QJsonValue &id, const QJsonObject &params)
{
    const QString uri = params.value(QStringLiteral("uri")).toString();
    QJsonObject contentItem;
    contentItem[QStringLiteral("uri")] = uri;

    if (uri == QStringLiteral("ensemble://project")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        if (m_mainWindow) {
            contentItem[QStringLiteral("text")] = QString::fromUtf8(
                QJsonDocument(toolGetProjectSummary(QJsonObject())).toJson(QJsonDocument::Indented));
        } else {
            contentItem[QStringLiteral("text")] = QStringLiteral("{}");
        }
    } else if (uri == QStringLiteral("ensemble://workskin/css")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("text/css");
        if (m_mainWindow && m_mainWindow->hasActiveProject()) {
            contentItem[QStringLiteral("text")] = m_mainWindow->project().workSkinCss();
        } else {
            contentItem[QStringLiteral("text")] = QString();
        }
    } else if (uri == QStringLiteral("ensemble://problems")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        contentItem[QStringLiteral("text")] = QString::fromUtf8(
            QJsonDocument(toolGetProblems(QJsonObject())).toJson(QJsonDocument::Indented));
    } else if (uri == QStringLiteral("ensemble://agents")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        contentItem[QStringLiteral("text")] = QString::fromUtf8(
            QJsonDocument(toolGetAgentPresence(nullptr, QJsonObject())).toJson(QJsonDocument::Indented));
    } else if (uri == QStringLiteral("ensemble://agentic/index")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        if (m_mainWindow && m_mainWindow->hasActiveProject()) {
            contentItem[QStringLiteral("text")] = QString::fromUtf8(
                QJsonDocument(m_mainWindow->project().agenticData().generateIndexJson()).toJson(QJsonDocument::Indented));
        } else {
            contentItem[QStringLiteral("text")] = QStringLiteral("{}");
        }
    } else if (uri == QStringLiteral("ensemble://agentic/history")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("application/xml");
        if (m_mainWindow && m_mainWindow->hasActiveProject()) {
            contentItem[QStringLiteral("text")] = m_mainWindow->project().agenticData().rawModelHistoryXml();
        } else {
            contentItem[QStringLiteral("text")] = QStringLiteral("<agent-model-history version=\"1.0\"/>");
        }
    } else if (uri == QStringLiteral("ensemble://lore/graph")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("text/plain");
        if (m_mainWindow && m_mainWindow->hasActiveProject()) {
            contentItem[QStringLiteral("text")] = m_mainWindow->project().lore().defaultGraph().toDsl();
        } else {
            contentItem[QStringLiteral("text")] = QString();
        }
    } else if (uri == QStringLiteral("ensemble://lore/data")) {
        contentItem[QStringLiteral("mimeType")] = QStringLiteral("application/json");
        if (m_mainWindow && m_mainWindow->hasActiveProject()) {
            contentItem[QStringLiteral("text")] = QString::fromUtf8(
                QJsonDocument(m_mainWindow->project().lore().toJsonSummary()).toJson(QJsonDocument::Indented));
        } else {
            contentItem[QStringLiteral("text")] = QStringLiteral("{}");
        }
    } else {
        return makeErrorResponse(id, -32602, QStringLiteral("Resource URI '%1' not recognized").arg(uri));
    }

    QJsonArray contents;
    contents.append(contentItem);
    QJsonObject result;
    result[QStringLiteral("contents")] = contents;
    return makeSuccessResponse(id, result);
}

// -----------------------------------------------------------------------------
// Tool Implementations
// -----------------------------------------------------------------------------

QJsonObject McpHandler::toolGetProjectSummary(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    int totalWords = 0;
    for (const Chapter &ch : proj.chapters()) {
        QTextDocument doc;
        doc.setHtml(ch.html());
        const QString plain = doc.toPlainText();
        totalWords += plain.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    }

    QJsonObject summary;
    summary[QStringLiteral("title")] = proj.title();
    summary[QStringLiteral("author")] = proj.author();
    summary[QStringLiteral("chapterCount")] = proj.chapters().size();
    summary[QStringLiteral("activeChapterIndex")] = proj.activeChapterIndex();
    summary[QStringLiteral("totalWordCount")] = totalWords;
    summary[QStringLiteral("isDirty")] = proj.isDirty();
    summary[QStringLiteral("lastModifiedBy")] = proj.lastModifiedBy().isEmpty() ? QStringLiteral("user") : proj.lastModifiedBy();
    summary[QStringLiteral("lastModifiedAt")] = proj.lastModifiedAt().isValid() ? proj.lastModifiedAt().toString(Qt::ISODate) : QString();
    summary[QStringLiteral("lastChangesByModel")] = proj.agenticData().lastChangesByModelJson();
    summary[QStringLiteral("filePath")] = proj.filePath();
    summary[QStringLiteral("hasCustomWorkSkin")] = !proj.workSkinCss().trimmed().isEmpty();

    // Standard AO3 work metadata
    summary[QStringLiteral("rating")] = proj.rating().isEmpty() ? QStringLiteral("Not Rated") : proj.rating();
    summary[QStringLiteral("archiveWarnings")] = QJsonArray::fromStringList(proj.archiveWarnings());
    summary[QStringLiteral("categories")] = QJsonArray::fromStringList(proj.categories());
    summary[QStringLiteral("fandoms")] = QJsonArray::fromStringList(proj.fandoms());
    summary[QStringLiteral("relationships")] = QJsonArray::fromStringList(proj.relationships());
    summary[QStringLiteral("characters")] = QJsonArray::fromStringList(proj.characters());
    summary[QStringLiteral("tags")] = QJsonArray::fromStringList(proj.tags());

    // Lore repository metrics
    QJsonObject loreSummary;
    loreSummary[QStringLiteral("entityCount")] = proj.lore().entities().size();
    loreSummary[QStringLiteral("historyCount")] = proj.lore().historyEvents().size();
    loreSummary[QStringLiteral("conflictCount")] = proj.lore().conflicts().size();
    loreSummary[QStringLiteral("resolvedConflictCount")] = proj.lore().resolvedConflicts().size();
    loreSummary[QStringLiteral("planCount")] = proj.lore().plans().size();
    loreSummary[QStringLiteral("referenceCount")] = proj.lore().references().size();
    loreSummary[QStringLiteral("defaultGraphNodes")] = proj.lore().defaultGraph().nodes().size();
    loreSummary[QStringLiteral("defaultGraphConnectors")] = proj.lore().defaultGraph().connectors().size();
    summary[QStringLiteral("loreSummary")] = loreSummary;

    // Git-like commit history (history.cmt)
    const QString history = proj.historyLog();
    const QStringList historyBlocks = history.split(QStringLiteral("\ncommit "), Qt::SkipEmptyParts);
    summary[QStringLiteral("commitCount")] = history.trimmed().isEmpty() ? 0 : historyBlocks.size();
    QJsonArray recentCommits;
    for (int i = 0; i < historyBlocks.size() && i < 3; ++i) {
        QString block = historyBlocks.at(i);
        if (i == 0 && block.startsWith(QStringLiteral("commit "))) block = block.mid(7);
        const QStringList lines = block.split(QLatin1Char('\n'));
        const QString hash = lines.value(0).trimmed();
        QString author;
        QString date;
        QString msg;
        for (int j = 1; j < lines.size(); ++j) {
            const QString &line = lines.at(j);
            if (line.startsWith(QStringLiteral("Author: "))) author = line.mid(8).trimmed();
            else if (line.startsWith(QStringLiteral("Date:   "))) date = line.mid(8).trimmed();
            else if (line.startsWith(QStringLiteral("    "))) {
                if (!msg.isEmpty()) msg += QLatin1Char(' ');
                msg += line.mid(4).trimmed();
            }
        }
        QJsonObject c;
        c[QStringLiteral("hash")] = hash;
        c[QStringLiteral("author")] = author;
        c[QStringLiteral("date")] = date;
        c[QStringLiteral("message")] = msg;
        recentCommits.append(c);
    }
    summary[QStringLiteral("recentCommits")] = recentCommits;

    return makeToolJsonResult(summary);
}

QJsonObject McpHandler::toolListChapters(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    QJsonArray chaptersArray;

    int idx = 0;
    for (const Chapter &ch : proj.chapters()) {
        QTextDocument doc;
        doc.setHtml(ch.html());
        const QString plain = doc.toPlainText();
        const int words = plain.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();

        QJsonObject item;
        item[QStringLiteral("index")] = idx;
        item[QStringLiteral("id")] = ch.id().toString();
        item[QStringLiteral("title")] = ch.title();
        item[QStringLiteral("order")] = ch.order();
        item[QStringLiteral("wordCount")] = words;
        item[QStringLiteral("isActive")] = (idx == proj.activeChapterIndex());
        chaptersArray.append(item);
        ++idx;
    }

    QJsonObject res;
    res[QStringLiteral("chapters")] = chaptersArray;
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolGetChapter(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const int index = args.value(QStringLiteral("chapterIndex")).toInt(-1);
    const QString format = args.value(QStringLiteral("format")).toString(QStringLiteral("all")).toLower();
    const Ao3Project &proj = m_mainWindow->project();

    if (index < 0 || index >= proj.chapters().size()) {
        return makeToolResult(QStringLiteral("Invalid chapterIndex %1. Valid range is 0 to %2.")
                                  .arg(index).arg(proj.chapters().size() - 1), true);
    }

    const Chapter &ch = proj.chapters().at(index);
    QJsonObject res;
    res[QStringLiteral("chapterIndex")] = index;
    res[QStringLiteral("title")] = ch.title();
    res[QStringLiteral("order")] = ch.order();

    QTextDocument doc;
    doc.setHtml(ch.html());
    const QString plain = doc.toPlainText();
    const int words = plain.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    res[QStringLiteral("wordCount")] = words;

    if (format == QStringLiteral("html")) {
        res[QStringLiteral("html")] = ch.html();
    } else if (format == QStringLiteral("text")) {
        res[QStringLiteral("plainText")] = plain;
    } else {
        res[QStringLiteral("html")] = ch.html();
        res[QStringLiteral("plainText")] = plain;
    }

    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolUpdateChapterHtml(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const int index = args.value(QStringLiteral("chapterIndex")).toInt(-1);
    const QString html = args.value(QStringLiteral("html")).toString();
    const Ao3Project &proj = m_mainWindow->project();

    if (index < 0 || index >= proj.chapters().size()) {
        return makeToolResult(QStringLiteral("Invalid chapterIndex %1. Valid range is 0 to %2.")
                                  .arg(index).arg(proj.chapters().size() - 1), true);
    }

    QTextDocument oldDoc;
    oldDoc.setHtml(proj.chapters().at(index).html());
    const int oldWords = oldDoc.toPlainText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();

    const SanitizeResult sanRes = m_mainWindow->applyMcpChapterHtml(index, html);
    const Chapter &updatedCh = m_mainWindow->project().chapters().at(index);

    QTextDocument doc;
    doc.setHtml(sanRes.cleanHtml);
    const int words = doc.toPlainText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    const int wordDelta = words - oldWords;

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString callerName = caller.name.isEmpty() ? QStringLiteral("ai-assistant") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    const QString callerRole = caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role;

    m_mainWindow->project().markDirtyBy(callerName);

    const QString summary = QStringLiteral("Updated chapter %1 ('%2'): %3 words (%4%5 words)")
                                .arg(index + 1)
                                .arg(updatedCh.title())
                                .arg(words)
                                .arg(wordDelta >= 0 ? QStringLiteral("+") : QString())
                                .arg(wordDelta);
    QStringList detailsList;
    if (!sanRes.strippedTags.isEmpty()) {
        detailsList << QStringLiteral("Stripped tags: %1").arg(sanRes.strippedTags.join(QStringLiteral(", ")));
    }
    if (!sanRes.strippedAttributes.isEmpty()) {
        detailsList << QStringLiteral("Stripped attrs: %1").arg(sanRes.strippedAttributes.join(QStringLiteral(", ")));
    }
    m_mainWindow->project().agenticData().appendHistoryEntry(
        callerName, callerModel, callerRole,
        QStringLiteral("update_chapter_html"),
        summary,
        index,
        wordDelta,
        detailsList.join(QStringLiteral("; "))
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("chapterIndex")] = index;
    res[QStringLiteral("title")] = updatedCh.title();
    res[QStringLiteral("wordCount")] = words;
    res[QStringLiteral("wordCountDelta")] = wordDelta;
    res[QStringLiteral("modifiedBy")] = callerName;
    res[QStringLiteral("wasSanitized")] = sanRes.wasModified;

    QJsonArray strippedTagsArr;
    for (const QString &t : sanRes.strippedTags) {
        strippedTagsArr.append(t);
    }
    res[QStringLiteral("strippedTags")] = strippedTagsArr;

    QJsonArray strippedAttrsArr;
    for (const QString &a : sanRes.strippedAttributes) {
        strippedAttrsArr.append(a);
    }
    res[QStringLiteral("strippedAttributes")] = strippedAttrsArr;
    res[QStringLiteral("sanitizedHtml")] = sanRes.cleanHtml;

    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolAddChapter(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString title = args.value(QStringLiteral("title")).toString();
    m_mainWindow->addChapterFromMcp(title);

    const int newIndex = m_mainWindow->project().chapters().size() - 1;
    const Chapter &newCh = m_mainWindow->project().chapters().at(newIndex);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString callerName = caller.name.isEmpty() ? QStringLiteral("ai-assistant") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    const QString callerRole = caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role;

    m_mainWindow->project().markDirtyBy(callerName);

    const QString summary = QStringLiteral("Added chapter %1 ('%2')").arg(newIndex + 1).arg(newCh.title());
    m_mainWindow->project().agenticData().appendHistoryEntry(
        callerName, callerModel, callerRole,
        QStringLiteral("add_chapter"),
        summary,
        newIndex,
        0
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("chapterIndex")] = newIndex;
    res[QStringLiteral("title")] = newCh.title();
    res[QStringLiteral("modifiedBy")] = callerName;
    res[QStringLiteral("message")] = QStringLiteral("Added new chapter at index %1: '%2'")
                                        .arg(newIndex).arg(newCh.title());
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolDeleteChapter(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const int index = args.value(QStringLiteral("chapterIndex")).toInt(-1);
    const Ao3Project &proj = m_mainWindow->project();
    if (index < 0 || index >= proj.chapters().size()) {
        return makeToolResult(QStringLiteral("Invalid chapterIndex %1.").arg(index), true);
    }

    if (proj.chapters().size() <= 1) {
        return makeToolResult(QStringLiteral("Cannot delete the only chapter in the project."), true);
    }

    QTextDocument doc;
    doc.setHtml(proj.chapters().at(index).html());
    const int delWords = doc.toPlainText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    const QString deletedTitle = proj.chapters().at(index).title();

    m_mainWindow->deleteChapterFromMcp(index);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString callerName = caller.name.isEmpty() ? QStringLiteral("ai-assistant") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    const QString callerRole = caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role;

    m_mainWindow->project().markDirtyBy(callerName);

    const QString summary = QStringLiteral("Deleted chapter %1 ('%2', %3 words)").arg(index + 1).arg(deletedTitle).arg(delWords);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        callerName, callerModel, callerRole,
        QStringLiteral("delete_chapter"),
        summary,
        -1,
        -delWords
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("deletedIndex")] = index;
    res[QStringLiteral("deletedTitle")] = deletedTitle;
    res[QStringLiteral("wordCountDelta")] = -delWords;
    res[QStringLiteral("modifiedBy")] = callerName;
    res[QStringLiteral("chapterCount")] = m_mainWindow->project().chapters().size();

    QJsonArray chaptersArr;
    int idx = 0;
    for (const Chapter &ch : m_mainWindow->project().chapters()) {
        QJsonObject chObj;
        chObj[QStringLiteral("index")] = idx++;
        chObj[QStringLiteral("title")] = ch.title();
        chObj[QStringLiteral("order")] = ch.order();
        chaptersArr.append(chObj);
    }
    res[QStringLiteral("chapters")] = chaptersArr;

    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolReorderChapters(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const int from = args.value(QStringLiteral("fromIndex")).toInt(-1);
    const int to = args.value(QStringLiteral("toIndex")).toInt(-1);
    const int count = m_mainWindow->project().chapters().size();

    if (from < 0 || from >= count || to < 0 || to >= count) {
        return makeToolResult(QStringLiteral("Invalid indices: from=%1, to=%2 (count=%3).").arg(from).arg(to).arg(count), true);
    }

    const QString movedTitle = m_mainWindow->project().chapters().at(from).title();
    m_mainWindow->reorderChaptersFromMcp(from, to);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString callerName = caller.name.isEmpty() ? QStringLiteral("ai-assistant") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    const QString callerRole = caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role;

    m_mainWindow->project().markDirtyBy(callerName);

    const QString summary = QStringLiteral("Reordered chapter '%1' from position %2 to %3").arg(movedTitle).arg(from + 1).arg(to + 1);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        callerName, callerModel, callerRole,
        QStringLiteral("reorder_chapters"),
        summary,
        to,
        0
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("fromIndex")] = from;
    res[QStringLiteral("toIndex")] = to;
    res[QStringLiteral("chapterTitle")] = movedTitle;
    res[QStringLiteral("modifiedBy")] = callerName;
    res[QStringLiteral("chapterCount")] = m_mainWindow->project().chapters().size();

    QJsonArray chaptersArr;
    int idx = 0;
    for (const Chapter &ch : m_mainWindow->project().chapters()) {
        QJsonObject chObj;
        chObj[QStringLiteral("index")] = idx++;
        chObj[QStringLiteral("title")] = ch.title();
        chObj[QStringLiteral("order")] = ch.order();
        chaptersArr.append(chObj);
    }
    res[QStringLiteral("chapters")] = chaptersArr;

    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolGetWorkSkinCss(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString css = m_mainWindow->project().workSkinCss();
    QJsonObject res;
    res[QStringLiteral("css")] = css;
    res[QStringLiteral("length")] = css.length();
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolSetWorkSkinCss(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString css = args.value(QStringLiteral("css")).toString();
    const bool allowMalformed = args.value(QStringLiteral("allowMalformed")).toBool(false);

    QStringList cssErrors;
    if (!allowMalformed && !CssEditorPane::validateCss(css, &cssErrors)) {
        return makeToolResult(
            QStringLiteral("CSS syntax validation failed with %1 error(s):\n• %2\nPass 'allowMalformed: true' to force update anyway.")
                .arg(cssErrors.size())
                .arg(cssErrors.join(QStringLiteral("\n• "))),
            true
        );
    }

    const int oldLen = m_mainWindow->project().workSkinCss().length();
    m_mainWindow->setWorkSkinCssFromMcp(css);
    const int byteDelta = css.length() - oldLen;

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString callerName = caller.name.isEmpty() ? QStringLiteral("ai-assistant") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    const QString callerRole = caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role;

    m_mainWindow->project().markDirtyBy(callerName);

    const QString summary = QStringLiteral("Updated Work Skin CSS: %1 bytes (%2%3 bytes)")
                                .arg(css.length())
                                .arg(byteDelta >= 0 ? QStringLiteral("+") : QString())
                                .arg(byteDelta);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        callerName, callerModel, callerRole,
        QStringLiteral("set_workskin_css"),
        summary,
        -1,
        0,
        QStringLiteral("Work Skin CSS update")
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("bytes")] = css.length();
    res[QStringLiteral("byteDelta")] = byteDelta;
    res[QStringLiteral("modifiedBy")] = callerName;
    res[QStringLiteral("message")] = QStringLiteral("Work Skin CSS updated successfully (%1 bytes). Preview re-rendered.").arg(css.length());
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolGetProblems(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_mainWindow) {
        return makeToolResult(QStringLiteral("Ensemble main window unavailable."), true);
    }

    m_mainWindow->updateProblemsDiagnostics();

    const QList<ProblemItem> problems = m_mainWindow->currentProblems();
    QJsonArray problemsArray;

    for (const ProblemItem &prob : problems) {
        QJsonObject p;
        p[QStringLiteral("severity")] = (prob.severity == ProblemItem::Error) ? QStringLiteral("Error") : QStringLiteral("Warning");
        p[QStringLiteral("category")] = prob.category;
        p[QStringLiteral("description")] = prob.description;
        p[QStringLiteral("chapterIndex")] = prob.chapterIndex;

        QJsonArray aff;
        for (int chIdx : prob.affectedChapters) {
            aff.append(chIdx);
        }
        p[QStringLiteral("affectedChapters")] = aff;
        problemsArray.append(p);
    }

    QJsonObject res;
    res[QStringLiteral("problems")] = problemsArray;
    res[QStringLiteral("count")] = problemsArray.size();
    res[QStringLiteral("lastUpdated")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolLoadProject(const QJsonObject &args)
{
    if (!m_mainWindow) {
        return makeToolResult(QStringLiteral("Ensemble application window is unavailable."), true);
    }

    const QString filePath = args.value(QStringLiteral("filePath")).toString().trimmed();
    if (filePath.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'filePath' parameter."), true);
    }
    const bool saveCurrent = args.value(QStringLiteral("saveCurrent")).toBool(false);

    QString err;
    const bool ok = m_mainWindow->loadProjectFromMcp(filePath, saveCurrent, &err);
    if (!ok) {
        return makeToolResult(QStringLiteral("Failed to load project from %1: %2").arg(filePath, err), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("filePath")] = proj.filePath();
    res[QStringLiteral("title")] = proj.title();
    res[QStringLiteral("author")] = proj.author();
    res[QStringLiteral("rating")] = proj.rating();
    res[QStringLiteral("chapterCount")] = proj.chapters().size();
    res[QStringLiteral("isDirty")] = proj.isDirty();

    const QString history = proj.historyLog();
    const QStringList historyBlocks = history.split(QStringLiteral("\ncommit "), Qt::SkipEmptyParts);
    res[QStringLiteral("commitCount")] = history.trimmed().isEmpty() ? 0 : historyBlocks.size();

    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolSaveProject(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project to save."), true);
    }

    Ao3Project &proj = m_mainWindow->project();
    const QString currentPath = proj.filePath();
    QString targetPath = args.value(QStringLiteral("filePath")).toString().trimmed();
    if (targetPath.isEmpty()) {
        targetPath = currentPath;
    }
    if (targetPath.isEmpty()) {
        return makeToolResult(QStringLiteral("Cannot save project: no file path specified and project has no current file path."), true);
    }

    const bool saveCopy = args.value(QStringLiteral("saveCopy")).toBool(
        args.value(QStringLiteral("exportCopy")).toBool(false)
    );

    // Check for git commit message
    QString commitMsg = args.value(QStringLiteral("commitMessage")).toString().trimmed();
    if (commitMsg.isEmpty()) {
        commitMsg = args.value(QStringLiteral("message")).toString().trimmed();
    }

    QString author = args.value(QStringLiteral("author")).toString().trimmed();
    if (author.isEmpty() && client && m_server) {
        const auto sess = m_server->agentSessionForClient(client);
        if (!sess.name.isEmpty()) {
            author = sess.name;
        }
    }
    if (author.isEmpty()) {
        author = proj.author().isEmpty() ? QStringLiteral("AI Agent") : proj.author();
    }

    if (!commitMsg.isEmpty()) {
        proj.appendHistoryCommit(author, commitMsg);
    }

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? author : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);

    if (saveCopy) {
        QString saveErr;
        const bool saved = ProjectSerializer::save(targetPath, proj, &saveErr);
        if (saved) {
            proj.agenticData().appendHistoryEntry(
                modifier, callerModel, caller.role, QStringLiteral("save_project_copy"),
                QStringLiteral("Exported project copy to '%1'%2")
                    .arg(targetPath, commitMsg.isEmpty() ? QString() : QStringLiteral(" (commit: %1)").arg(commitMsg))
            );

            QJsonObject res;
            res[QStringLiteral("success")] = true;
            res[QStringLiteral("filePath")] = targetPath;
            res[QStringLiteral("savedPath")] = targetPath;
            res[QStringLiteral("isCopy")] = true;
            res[QStringLiteral("switchedActiveProject")] = false;
            res[QStringLiteral("activeProjectFilePath")] = currentPath;
            res[QStringLiteral("committed")] = !commitMsg.isEmpty();
            if (!commitMsg.isEmpty()) {
                res[QStringLiteral("commitAuthor")] = author;
                res[QStringLiteral("commitMessage")] = commitMsg;
            }
            res[QStringLiteral("isDirty")] = proj.isDirty();
            res[QStringLiteral("message")] = QStringLiteral("Project copy exported successfully to %1 without switching active project.").arg(targetPath);
            return makeToolJsonResult(res);
        } else {
            return makeToolResult(QStringLiteral("Failed to export project copy to %1: %2").arg(targetPath, saveErr), true);
        }
    }

    proj.setFilePath(targetPath);
    QString saveErr;
    const bool saved = ProjectSerializer::save(targetPath, proj, &saveErr);
    if (saved) {
        proj.setDirty(false);
        m_mainWindow->updateWindowTitle();
        m_mainWindow->updateStatusBar();

        proj.agenticData().appendHistoryEntry(
            modifier, callerModel, caller.role, QStringLiteral("save_project"),
            QStringLiteral("Saved active project to '%1'%2")
                .arg(targetPath, commitMsg.isEmpty() ? QString() : QStringLiteral(" (commit: %1)").arg(commitMsg))
        );

        QJsonObject res;
        res[QStringLiteral("success")] = true;
        res[QStringLiteral("filePath")] = targetPath;
        res[QStringLiteral("savedPath")] = targetPath;
        res[QStringLiteral("isCopy")] = false;
        res[QStringLiteral("switchedActiveProject")] = true;
        res[QStringLiteral("activeProjectFilePath")] = targetPath;
        res[QStringLiteral("committed")] = !commitMsg.isEmpty();
        if (!commitMsg.isEmpty()) {
            res[QStringLiteral("commitAuthor")] = author;
            res[QStringLiteral("commitMessage")] = commitMsg;
        }
        res[QStringLiteral("isDirty")] = false;
        res[QStringLiteral("message")] = QStringLiteral("Project saved successfully to %1").arg(targetPath);
        return makeToolJsonResult(res);
    } else {
        return makeToolResult(QStringLiteral("Failed to save project to %1: %2").arg(targetPath, saveErr), true);
    }
}

QJsonObject McpHandler::toolGetProjectHistory(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project open in Ensemble."), true);
    }

    const int limit = args.value(QStringLiteral("limit")).toInt(50);
    const QString history = m_mainWindow->project().historyLog();
    const Ao3Project &proj = m_mainWindow->project();

    QJsonArray commitsArray;
    const QStringList blocks = history.split(QStringLiteral("\ncommit "), Qt::SkipEmptyParts);

    // Pre-extract hashes to associate previousCommitHash (parent)
    QStringList allHashes;
    for (int i = 0; i < blocks.size(); ++i) {
        QString block = blocks.at(i);
        if (i == 0 && block.startsWith(QStringLiteral("commit "))) {
            block = block.mid(7);
        }
        allHashes.append(block.split(QLatin1Char('\n')).value(0).trimmed());
    }

    for (int i = 0; i < blocks.size() && commitsArray.size() < limit; ++i) {
        QString block = blocks.at(i);
        if (i == 0 && block.startsWith(QStringLiteral("commit "))) {
            block = block.mid(7);
        }

        const QStringList lines = block.split(QLatin1Char('\n'));
        const QString hash = lines.value(0).trimmed();
        const QString previousHash = (i + 1 < allHashes.size()) ? allHashes.at(i + 1) : QString();
        QString author;
        QString date;
        QString msg;

        for (int j = 1; j < lines.size(); ++j) {
            const QString &line = lines.at(j);
            if (line.startsWith(QStringLiteral("Author: "))) {
                author = line.mid(8).trimmed();
            } else if (line.startsWith(QStringLiteral("Date:   "))) {
                date = line.mid(8).trimmed();
            } else if (line.startsWith(QStringLiteral("    "))) {
                if (!msg.isEmpty()) msg += QLatin1Char(' ');
                msg += line.mid(4).trimmed();
            }
        }

        bool hasDiff = !proj.snapshotDiff(hash).isEmpty();
        if (!hasDiff) {
            for (auto it = proj.snapshotDiffs().constBegin(); it != proj.snapshotDiffs().constEnd(); ++it) {
                if (it.key().startsWith(hash) || hash.startsWith(it.key())) {
                    hasDiff = true;
                    break;
                }
            }
        }

        QJsonObject c;
        c[QStringLiteral("hash")] = hash;
        c[QStringLiteral("previousCommitHash")] = previousHash;
        c[QStringLiteral("author")] = author;
        c[QStringLiteral("date")] = date;
        c[QStringLiteral("localDate")] = DiffUtil::formatIsoDateWithLocal(date);
        c[QStringLiteral("message")] = msg;
        c[QStringLiteral("diffAvailable")] = hasDiff;
        commitsArray.append(c);
    }

    QJsonObject res;
    res[QStringLiteral("totalCommits")] = commitsArray.size();
    res[QStringLiteral("latestCommitHash")] = commitsArray.isEmpty() ? QString() : commitsArray.first().toObject().value(QStringLiteral("hash")).toString();
    res[QStringLiteral("commits")] = commitsArray;
    res[QStringLiteral("rawHistory")] = history;
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolAddHistoryCommit(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project open in Ensemble."), true);
    }

    const QString message = args.value(QStringLiteral("message")).toString().trimmed();
    if (message.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'message' parameter."), true);
    }

    QString author = args.value(QStringLiteral("author")).toString().trimmed();
    if (author.isEmpty()) {
        author = args.value(QStringLiteral("agentId")).toString().trimmed();
    }
    if (author.isEmpty() && client && m_server) {
        const auto sess = m_server->agentSessionForClient(client);
        if (!sess.name.isEmpty() && sess.name != QStringLiteral("Anonymous")) {
            author = sess.name;
        }
    }

    if (author.isEmpty()) {
        return makeToolResult(QStringLiteral("Agent identification required: Every MCP snapshot/commit must specify the agent's ID or tag via the 'author' or 'agentId' parameter (e.g. 'claude-desktop', 'antigravity-worker', or '<agentName>@<host>')."), true);
    }

    const QString previousCommitHash = m_mainWindow->project().latestCommitHash();
    const QString newHash = m_mainWindow->project().appendHistoryCommit(author, message);
    m_mainWindow->project().markDirtyBy(author);
    m_mainWindow->updateWindowTitle();

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("hash")] = newHash;
    res[QStringLiteral("previousCommitHash")] = previousCommitHash;
    res[QStringLiteral("author")] = author;
    res[QStringLiteral("message")] = message;
    res[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    res[QStringLiteral("localTimestamp")] = DiffUtil::formatIsoDateWithLocal(res[QStringLiteral("timestamp")].toString());
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolGetSnapshotDiff(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString hash = args.value(QStringLiteral("hash")).toString().trimmed();
    if (hash.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'hash' parameter."), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    QString diffText = proj.snapshotDiff(hash);
    if (diffText.isEmpty()) {
        for (auto it = proj.snapshotDiffs().constBegin(); it != proj.snapshotDiffs().constEnd(); ++it) {
            if (it.key().startsWith(hash) || hash.startsWith(it.key())) {
                diffText = it.value();
                break;
            }
        }
    }

    if (diffText.isEmpty()) {
        return makeToolResult(
            QStringLiteral("No diff snapshot available for this commit. It may have been deleted by an external software, or this file was created before snapshots were introduced to this feature."),
            true);
    }

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("hash")] = hash;
    res[QStringLiteral("diff")] = diffText;
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolReloadProject(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    if (proj.filePath().isEmpty() || !QFile::exists(proj.filePath())) {
        return makeToolResult(QStringLiteral("Cannot reload: project has never been saved to disk or file does not exist."), true);
    }

    const QString savedPath = proj.filePath();
    const bool ok = m_mainWindow->reloadProjectFromDisk();
    if (!ok) {
        return makeToolResult(QStringLiteral("Failed to reload project from %1.").arg(savedPath), true);
    }

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("filePath")] = m_mainWindow->project().filePath();
    res[QStringLiteral("title")] = m_mainWindow->project().title();
    res[QStringLiteral("chapterCount")] = m_mainWindow->project().chapters().size();
    res[QStringLiteral("isDirty")] = m_mainWindow->project().isDirty();
    res[QStringLiteral("message")] = QStringLiteral("Project reloaded from disk. Unsaved changes discarded.");
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolGetPreviewScreenshot(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    int chapterIndex = proj.activeChapterIndex();
    if (args.contains(QStringLiteral("chapterIndex"))) {
        chapterIndex = args.value(QStringLiteral("chapterIndex")).toInt(-1);
    }

    if (chapterIndex < 0 || chapterIndex >= proj.chapters().size()) {
        return makeToolResult(QStringLiteral("Invalid chapterIndex %1. Valid range is 0 to %2.")
                                  .arg(chapterIndex).arg(proj.chapters().size() - 1), true);
    }

    const QByteArray pngBytes = m_mainWindow->capturePreviewScreenshot(chapterIndex);
    if (pngBytes.isEmpty()) {
        return makeToolResult(QStringLiteral("Failed to capture preview screenshot for chapter %1.").arg(chapterIndex), true);
    }

    const QString chapterTitle = proj.chapters().at(chapterIndex).title();
    return makeToolImageResult(pngBytes, QStringLiteral("Preview screenshot of chapter %1 ('%2') with Work Skin").arg(chapterIndex).arg(chapterTitle));
}

QJsonObject McpHandler::toolTestFirewallConnection(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_server) {
        return makeToolResult(QStringLiteral("MCP Server instance is not available."), true);
    }

    McpTestResult res = m_server->testLoopback();
    QJsonObject obj;
    obj[QStringLiteral("success")] = res.success;
    obj[QStringLiteral("latencyMs")] = res.latencyMs;
    obj[QStringLiteral("message")] = res.message;
    obj[QStringLiteral("details")] = res.details;
    obj[QStringLiteral("serverUrl")] = m_server->serverUrl();

    return makeToolJsonResult(obj, !res.success);
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

QJsonObject McpHandler::makeSuccessResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject resp;
    resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resp[QStringLiteral("id")] = id;
    resp[QStringLiteral("result")] = result;
    return resp;
}

QJsonObject McpHandler::makeErrorResponse(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject resp;
    resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resp[QStringLiteral("id")] = id;
    QJsonObject err;
    err[QStringLiteral("code")] = code;
    err[QStringLiteral("message")] = message;
    resp[QStringLiteral("error")] = err;
    return resp;
}

QJsonObject McpHandler::makeToolResult(const QString &text, bool isError)
{
    QJsonObject res;
    QJsonArray content;
    QJsonObject item;
    item[QStringLiteral("type")] = QStringLiteral("text");
    item[QStringLiteral("text")] = text;
    content.append(item);
    res[QStringLiteral("content")] = content;
    res[QStringLiteral("isError")] = isError;
    return res;
}

QJsonObject McpHandler::makeToolJsonResult(const QJsonObject &jsonObj, bool isError)
{
    return makeToolResult(QString::fromUtf8(QJsonDocument(jsonObj).toJson(QJsonDocument::Indented)), isError);
}

QJsonObject McpHandler::makeToolImageResult(const QByteArray &pngBytes, const QString &captionText, bool isError)
{
    QJsonObject res;
    QJsonArray content;

    QJsonObject imgItem;
    imgItem[QStringLiteral("type")] = QStringLiteral("image");
    imgItem[QStringLiteral("data")] = QString::fromLatin1(pngBytes.toBase64());
    imgItem[QStringLiteral("mimeType")] = QStringLiteral("image/png");
    content.append(imgItem);

    if (!captionText.isEmpty()) {
        QJsonObject textItem;
        textItem[QStringLiteral("type")] = QStringLiteral("text");
        textItem[QStringLiteral("text")] = captionText;
        content.append(textItem);
    }

    res[QStringLiteral("content")] = content;
    res[QStringLiteral("isError")] = isError;
    return res;
}

QJsonObject McpHandler::toolGetAgentPresence(QWebSocket *client, const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_server) {
        return makeToolResult(QStringLiteral("MCP server is unavailable."), true);
    }

    const auto sessions = m_server->activeAgentSessions();
    const auto callerSession = client ? m_server->agentSessionForClient(client) : McpAgentSession();

    const AgenticData &agentic = m_mainWindow && m_mainWindow->hasActiveProject()
                                     ? m_mainWindow->project().agenticData()
                                     : AgenticData();
    const auto lastChanges = agentic.lastChangesByModel();

    QJsonArray agentArray;
    QStringList namesWithRoles;

    for (const auto &s : sessions) {
        QJsonObject a;
        a[QStringLiteral("agentId")] = s.agentId;
        a[QStringLiteral("name")] = s.name;
        if (!s.model.isEmpty())
            a[QStringLiteral("model")] = s.model;
        if (!s.version.isEmpty())
            a[QStringLiteral("clientVersion")] = s.version;
        a[QStringLiteral("role")] = s.role;
        a[QStringLiteral("workingNote")] = s.workingNote;
        a[QStringLiteral("peerAddress")] = s.peerAddress;
        a[QStringLiteral("connectedAt")] = s.connectedAt.toString(Qt::ISODate);
        a[QStringLiteral("lastSeen")] = s.lastSeen.toString(Qt::ISODate);
        a[QStringLiteral("isCallingAgent")] = (client && s.agentId == callerSession.agentId);

        const QString lowerName = s.name.toLower();
        if (lastChanges.contains(lowerName)) {
            a[QStringLiteral("lastChange")] = lastChanges.value(lowerName).toJson();
        }

        agentArray.append(a);
        namesWithRoles.append(!s.model.isEmpty() ? QStringLiteral("%1 [%2] (%3)").arg(s.name, s.model, s.role)
                                                : QStringLiteral("%1 (%2)").arg(s.name, s.role));
    }

    QJsonObject data;
    data[QStringLiteral("connectedCount")] = sessions.size();
    if (client && !callerSession.agentId.isEmpty()) {
        data[QStringLiteral("callingAgentId")] = callerSession.agentId;
        data[QStringLiteral("callingAgentName")] = callerSession.name;
        if (!callerSession.model.isEmpty()) {
            data[QStringLiteral("callingAgentModel")] = callerSession.model;
        }
    }
    data[QStringLiteral("agents")] = agentArray;
    data[QStringLiteral("lastChangesByModel")] = agentic.lastChangesByModelJson();

    QString summary;
    if (sessions.isEmpty()) {
        summary = QStringLiteral("No active AI agents connected.");
    } else if (sessions.size() == 1) {
        summary = QStringLiteral("1 AI agent currently active: %1.").arg(namesWithRoles.first());
    } else {
        summary = QStringLiteral("%1 AI agents currently active and aware of each other: %2.")
                      .arg(QString::number(sessions.size()), namesWithRoles.join(QStringLiteral(", ")));
    }
    data[QStringLiteral("summary")] = summary;

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolRegisterAgentPresence(QWebSocket *client, const QJsonObject &args)
{
    if (!m_server) {
        return makeToolResult(QStringLiteral("MCP server is unavailable."), true);
    }

    const QString name = args.value(QStringLiteral("agentName")).toString().trimmed();
    const QString model = args.value(QStringLiteral("model")).toString().trimmed();
    const QString role = args.value(QStringLiteral("role")).toString().trimmed();
    const QString note = args.value(QStringLiteral("workingNote")).toString().trimmed();

    if (client) {
        m_server->updateAgentSession(client, name, QString(), role, note, model);
    }

    const auto callerSession = client ? m_server->agentSessionForClient(client) : McpAgentSession();
    const auto sessions = m_server->activeAgentSessions();

    QJsonArray otherAgents;
    for (const auto &s : sessions) {
        if (client && s.agentId == callerSession.agentId) continue;
        QJsonObject o;
        o[QStringLiteral("agentId")] = s.agentId;
        o[QStringLiteral("name")] = s.name;
        if (!s.model.isEmpty()) {
            o[QStringLiteral("model")] = s.model;
        }
        o[QStringLiteral("role")] = s.role;
        o[QStringLiteral("workingNote")] = s.workingNote;
        o[QStringLiteral("lastSeen")] = s.lastSeen.toString(Qt::ISODate);
        otherAgents.append(o);
    }

    QJsonObject data;
    data[QStringLiteral("success")] = true;
    if (client && !callerSession.agentId.isEmpty()) {
        data[QStringLiteral("agentId")] = callerSession.agentId;
        data[QStringLiteral("name")] = callerSession.name;
        if (!callerSession.model.isEmpty()) {
            data[QStringLiteral("model")] = callerSession.model;
        }
        data[QStringLiteral("role")] = callerSession.role;
        data[QStringLiteral("workingNote")] = callerSession.workingNote;
    }
    data[QStringLiteral("connectedCount")] = sessions.size();
    data[QStringLiteral("otherActiveAgents")] = otherAgents;
    data[QStringLiteral("message")] = QStringLiteral("Presence registered successfully. %1 active agent(s) in session.")
                                          .arg(sessions.size());

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolGetAgenticSummary(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project is currently loaded in Ensemble."), true);
    }

    const AgenticData &agentic = m_mainWindow->project().agenticData();
    const QJsonObject indexObj = agentic.generateIndexJson();
    QJsonObject data;
    data[QStringLiteral("index")] = indexObj;
    data[QStringLiteral("sharedBytes")] = agentic.totalSharedBytes();
    data[QStringLiteral("localizedBytes")] = agentic.totalLocalizedBytes();
    data[QStringLiteral("agents")] = indexObj.value(QStringLiteral("agents")).toObject();
    data[QStringLiteral("agentUsage")] = indexObj.value(QStringLiteral("agents")).toObject();
    data[QStringLiteral("lastChangesByModel")] = agentic.lastChangesByModelJson();
    data[QStringLiteral("summary")] = QStringLiteral("Agentic data repository active: %1 bytes shared (cap 5 MB), %2 bytes localized (cap 20 MB).")
                                          .arg(agentic.totalSharedBytes())
                                          .arg(agentic.totalLocalizedBytes());
    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolReadAgenticFile(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project is currently loaded in Ensemble."), true);
    }

    const QString path = args.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'path' parameter."), true);
    }

    const AgenticData &agentic = m_mainWindow->project().agenticData();
    const QString encoding = args.value(QStringLiteral("encoding")).toString().trimmed().toLower();

    // Check special root agentic files
    if (path == QStringLiteral("agent_model_history.xml") || path.endsWith(QStringLiteral("/agent_model_history.xml"))) {
        const QByteArray xmlBytes = agentic.rawModelHistoryXml().toUtf8();
        QJsonObject data;
        data[QStringLiteral("scope")] = QStringLiteral("root");
        data[QStringLiteral("path")] = QStringLiteral("agent_model_history.xml");
        data[QStringLiteral("bytes")] = xmlBytes.size();
        data[QStringLiteral("content")] = (encoding == QStringLiteral("base64"))
            ? QString::fromLatin1(xmlBytes.toBase64()) : QString::fromUtf8(xmlBytes);
        data[QStringLiteral("encoding")] = (encoding == QStringLiteral("base64"))
            ? QStringLiteral("base64") : QStringLiteral("utf-8");
        return makeToolJsonResult(data);
    }

    if (path == QStringLiteral("index.json") || path.endsWith(QStringLiteral("/index.json"))) {
        const QByteArray jsonBytes = QJsonDocument(agentic.generateIndexJson()).toJson(QJsonDocument::Indented);
        QJsonObject data;
        data[QStringLiteral("scope")] = QStringLiteral("root");
        data[QStringLiteral("path")] = QStringLiteral("index.json");
        data[QStringLiteral("bytes")] = jsonBytes.size();
        data[QStringLiteral("content")] = (encoding == QStringLiteral("base64"))
            ? QString::fromLatin1(jsonBytes.toBase64()) : QString::fromUtf8(jsonBytes);
        data[QStringLiteral("encoding")] = (encoding == QStringLiteral("base64"))
            ? QStringLiteral("base64") : QStringLiteral("utf-8");
        return makeToolJsonResult(data);
    }

    QString scope = args.value(QStringLiteral("scope")).toString().trimmed().toLower();
    QString agentName = args.value(QStringLiteral("agent")).toString().trimmed().toLower();
    if (agentName.isEmpty()) {
        agentName = args.value(QStringLiteral("agentName")).toString().trimmed().toLower();
    }
    if (!agentName.isEmpty() && (scope.isEmpty() || scope == QStringLiteral("private"))) {
        scope = agentName;
    }
    if (scope.isEmpty()) {
        scope = QStringLiteral("shared");
    }

    QByteArray fileData;
    if (scope == QStringLiteral("shared")) {
        if (!agentic.hasSharedFile(path)) {
            return makeToolResult(QStringLiteral("File '%1' not found in shared storage.").arg(path), true);
        }
        fileData = agentic.sharedFile(path);
    } else {
        if (!agentic.hasAgentFile(scope, path)) {
            return makeToolResult(QStringLiteral("File '%1' not found in agent '%2' private storage.").arg(path, scope), true);
        }
        fileData = agentic.agentFile(scope, path);
    }

    QJsonObject data;
    data[QStringLiteral("scope")] = scope;
    data[QStringLiteral("path")] = path;
    data[QStringLiteral("bytes")] = fileData.size();
    if (encoding == QStringLiteral("base64")) {
        data[QStringLiteral("content")] = QString::fromLatin1(fileData.toBase64());
        data[QStringLiteral("encoding")] = QStringLiteral("base64");
    } else {
        data[QStringLiteral("content")] = QString::fromUtf8(fileData);
        data[QStringLiteral("encoding")] = QStringLiteral("utf-8");
    }

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolWriteAgenticFile(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project is currently loaded in Ensemble."), true);
    }

    const QString path = args.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'path' parameter."), true);
    }

    if (!args.contains(QStringLiteral("content"))) {
        return makeToolResult(QStringLiteral("Missing required 'content' parameter."), true);
    }

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString callerName = caller.name.isEmpty() ? QStringLiteral("ai-assistant") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    const QString callerRole = caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role;

    QString scope = args.value(QStringLiteral("scope")).toString().trimmed().toLower();
    QString agentName = args.value(QStringLiteral("agent")).toString().trimmed().toLower();
    if (agentName.isEmpty()) {
        agentName = args.value(QStringLiteral("agentName")).toString().trimmed().toLower();
    }
    if (!agentName.isEmpty() && (scope.isEmpty() || scope == QStringLiteral("private"))) {
        scope = agentName;
    }

    // Both read and write strictly default to 'shared' if scope is omitted
    if (scope.isEmpty()) {
        scope = QStringLiteral("shared");
    } else if (scope == QStringLiteral("private")) {
        scope = callerName.toLower();
    }

    const QString contentStr = args.value(QStringLiteral("content")).toString();
    const QString encoding = args.value(QStringLiteral("encoding")).toString().trimmed().toLower();
    QByteArray fileData;
    if (encoding == QStringLiteral("base64")) {
        fileData = QByteArray::fromBase64(contentStr.toLatin1());
    } else {
        fileData = contentStr.toUtf8();
    }

    AgenticData &agentic = m_mainWindow->project().agenticData();
    QString writeErr;
    bool ok = false;
    if (scope == QStringLiteral("shared")) {
        ok = agentic.setSharedFile(path, fileData, &writeErr);
    } else {
        ok = agentic.setAgentFile(scope, path, fileData, &writeErr);
    }

    if (!ok) {
        return makeToolResult(QStringLiteral("Write failed: %1").arg(writeErr), true);
    }

    m_mainWindow->project().markDirtyBy(callerName);

    const QString summary = QStringLiteral("Wrote agentic file '%1' to %2 storage (%3 bytes)")
                                .arg(path, scope).arg(fileData.size());
    agentic.appendHistoryEntry(
        callerName, callerModel, callerRole,
        QStringLiteral("write_agentic_file"),
        summary,
        -1,
        0,
        QStringLiteral("Storage scope: %1").arg(scope)
    );

    QJsonObject data;
    data[QStringLiteral("success")] = true;
    data[QStringLiteral("scope")] = scope;
    data[QStringLiteral("path")] = path;
    data[QStringLiteral("bytesWritten")] = fileData.size();
    data[QStringLiteral("modifiedBy")] = callerName;
    if (scope == QStringLiteral("shared")) {
        data[QStringLiteral("sharedBytesUsed")] = agentic.totalSharedBytes();
        data[QStringLiteral("sharedBytesRemaining")] = AgenticData::kMaxSharedBytes - agentic.totalSharedBytes();
    } else {
        data[QStringLiteral("agentBytesUsed")] = agentic.agentBytes(scope);
        data[QStringLiteral("totalLocalizedBytesUsed")] = agentic.totalLocalizedBytes();
        data[QStringLiteral("localizedBytesRemaining")] = AgenticData::kMaxLocalizedBytes - agentic.totalLocalizedBytes();
    }
    data[QStringLiteral("message")] = QStringLiteral("Saved file '%1' to %2 storage (%3 bytes).")
                                          .arg(path, scope, QString::number(fileData.size()));

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolAppendAgentHistory(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project is currently loaded in Ensemble."), true);
    }

    const QString summary = args.value(QStringLiteral("summary")).toString().trimmed();
    if (summary.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'summary' parameter."), true);
    }

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();

    QString agentName = args.value(QStringLiteral("agentName")).toString().trimmed();
    if (agentName.isEmpty() && !caller.name.isEmpty()) {
        agentName = caller.name;
    }
    if (agentName.isEmpty()) {
        agentName = QStringLiteral("agent");
    }

    QString model = resolveCallerModel(caller, args);

    QString role = args.value(QStringLiteral("role")).toString().trimmed();
    if (role.isEmpty() && !caller.role.isEmpty()) {
        role = caller.role;
    }
    if (role.isEmpty()) {
        role = QStringLiteral("co-author");
    }

    QString action = args.value(QStringLiteral("action")).toString().trimmed();
    if (action.isEmpty()) {
        action = QStringLiteral("co-write");
    }

    const int chapterIndex = args.value(QStringLiteral("chapterIndex")).toInt(-1);
    const int wordCountDelta = args.value(QStringLiteral("wordCountDelta")).toInt(0);
    const QString details = args.value(QStringLiteral("details")).toString();

    AgenticData &agentic = m_mainWindow->project().agenticData();
    agentic.appendHistoryEntry(agentName, model, role, action, summary, chapterIndex, wordCountDelta, details);
    m_mainWindow->project().markDirtyBy(agentName);

    QJsonObject data;
    data[QStringLiteral("success")] = true;
    data[QStringLiteral("agentName")] = agentName;
    data[QStringLiteral("action")] = action;
    data[QStringLiteral("chapterIndex")] = chapterIndex;
    data[QStringLiteral("wordCountDelta")] = wordCountDelta;
    data[QStringLiteral("historyCount")] = agentic.historyEntries().size();
    data[QStringLiteral("message")] = QStringLiteral("Appended entry to agent_model_history.xml (%1: %2).")
                                          .arg(agentName, action);

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolGetAgentHistory(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No active project is currently loaded in Ensemble."), true);
    }

    const AgenticData &agentic = m_mainWindow->project().agenticData();
    const auto &allEntries = agentic.historyEntries();

    int limit = args.value(QStringLiteral("limit")).toInt(50);
    if (limit <= 0) limit = 50;
    if (limit > 500) limit = 500;

    const QString filterAgent = args.value(QStringLiteral("agentName")).toString().trimmed().toLower();
    const QString filterAction = args.value(QStringLiteral("action")).toString().trimmed().toLower();

    QList<AgentModelHistoryEntry> filtered;
    for (int i = allEntries.size() - 1; i >= 0; --i) {
        const auto &entry = allEntries.at(i);
        if (!filterAgent.isEmpty() && !entry.agentName.toLower().contains(filterAgent)) {
            continue;
        }
        if (!filterAction.isEmpty() && !entry.action.toLower().contains(filterAction)) {
            continue;
        }
        filtered.append(entry);
        if (filtered.size() >= limit) {
            break;
        }
    }

    QJsonArray historyArr;
    for (const auto &entry : filtered) {
        historyArr.append(entry.toJson());
    }

    QJsonObject data;
    data[QStringLiteral("entries")] = historyArr;
    data[QStringLiteral("returnedCount")] = historyArr.size();
    data[QStringLiteral("totalHistoryCount")] = allEntries.size();
    data[QStringLiteral("lastChangesByModel")] = agentic.lastChangesByModelJson();

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolPostAgentThought(QWebSocket *client, const QJsonObject &args)
{
    const QString thought = args.value(QStringLiteral("thought")).toString().trimmed();
    if (thought.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'thought' parameter."), true);
    }

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString agent = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString category = args.value(QStringLiteral("category")).toString(QStringLiteral("critique"));

    // Also persist significant thoughts/critiques into project agent history if an active project exists
    if (m_mainWindow && m_mainWindow->hasActiveProject()) {
        const QString callerModel = resolveCallerModel(caller, args);
        m_mainWindow->project().agenticData().appendHistoryEntry(
            agent,
            callerModel,
            caller.role.isEmpty() ? QStringLiteral("co-author") : caller.role,
            category,
            thought
        );
        m_mainWindow->project().markDirtyBy(agent);
    }

    QJsonObject data;
    data[QStringLiteral("success")] = true;
    data[QStringLiteral("recorded")] = true;
    data[QStringLiteral("agent")] = agent;
    data[QStringLiteral("category")] = category;
    data[QStringLiteral("message")] = QStringLiteral("Thought successfully broadcast to Ensemble AI Thoughts Monitor UI.");

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolGetLoreGraph(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString graphName = args.value(QStringLiteral("name")).toString(QStringLiteral("data")).trimmed();
    const bool includeGrammar = args.value(QStringLiteral("includeGrammar")).toBool(true);

    const LoreGraph *graph = m_mainWindow->project().lore().graph(graphName);
    if (!graph) {
        return makeToolResult(QStringLiteral("Lore graph '%1' not found in project.").arg(graphName), true);
    }

    QJsonObject data;
    data[QStringLiteral("name")] = graphName;
    data[QStringLiteral("title")] = graph->title();
    data[QStringLiteral("direction")] = graph->direction();
    data[QStringLiteral("nodeCount")] = graph->nodes().size();
    data[QStringLiteral("connectorCount")] = graph->connectors().size();
    data[QStringLiteral("boxCount")] = graph->boxes().size();
    data[QStringLiteral("followupCount")] = graph->followups().size();
    data[QStringLiteral("rawDsl")] = graph->toDsl();
    data[QStringLiteral("magicHeaderValid")] = true;
    data[QStringLiteral("magicHeaderHex")] = QStringLiteral("89 47 52 50 48 01 0D 0A");
    data[QStringLiteral("graphStructure")] = graph->toJson();

    if (includeGrammar) {
        data[QStringLiteral("grammarSpecification")] = LoreGraph::dslGrammarSpecification();
    }

    return makeToolJsonResult(data);
}

QJsonObject McpHandler::toolUpdateLoreGraph(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString dsl = args.value(QStringLiteral("dsl")).toString();
    if (dsl.trimmed().isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'dsl' parameter."), true);
    }

    const QString graphName = args.value(QStringLiteral("name")).toString(QStringLiteral("data")).trimmed();
    const bool autoLayout = args.value(QStringLiteral("autoLayout")).toBool(false);

    LoreGraph targetGraph;
    QString parseErr;
    if (!targetGraph.fromDsl(dsl, &parseErr)) {
        return makeToolResult(QStringLiteral(".grph DSL syntax error: %1").arg(parseErr), true);
    }

    if (autoLayout) {
        targetGraph.autoLayout();
    }

    m_mainWindow->project().lore().setNamedGraph(graphName, targetGraph);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    m_mainWindow->project().markDirtyBy(modifier);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("update_lore_graph"),
        QStringLiteral("Updated lore graph '%1' with %2 nodes and %3 connectors.")
            .arg(graphName).arg(targetGraph.nodes().size()).arg(targetGraph.connectors().size())
    );

    m_mainWindow->updateLoreGraphUi();

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("name")] = graphName;
    result[QStringLiteral("nodeCount")] = targetGraph.nodes().size();
    result[QStringLiteral("connectorCount")] = targetGraph.connectors().size();
    result[QStringLiteral("boxCount")] = targetGraph.boxes().size();
    result[QStringLiteral("message")] = QStringLiteral("Lore graph '%1' updated successfully.").arg(graphName);
    return makeToolJsonResult(result);
}

QJsonObject McpHandler::toolAddLoreNode(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'id' parameter for lore node."), true);
    }

    const QString label = args.value(QStringLiteral("label")).toString().trimmed();
    const QString graphName = args.value(QStringLiteral("graphName")).toString(QStringLiteral("data")).trimmed();

    LoreGraph *graph = m_mainWindow->project().lore().graph(graphName);
    if (!graph) {
        return makeToolResult(QStringLiteral("Lore graph '%1' not found.").arg(graphName), true);
    }

    LoreGraphNode node;
    if (LoreGraphNode *existing = graph->findNode(id)) {
        node = *existing;
    } else {
        node.id = id;
    }

    if (!label.isEmpty()) node.label = label;
    if (args.contains(QStringLiteral("type"))) {
        const QString t = args.value(QStringLiteral("type")).toString().trimmed();
        if (!LoreGraph::isValidNodeType(t)) {
            return makeToolResult(QStringLiteral("Invalid node type '%1'. Valid types are character, location, event, item, faction, concept, plot_point.").arg(t), true);
        }
        node.type = t.toLower();
    }
    if (args.contains(QStringLiteral("x"))) node.x = args.value(QStringLiteral("x")).toDouble();
    if (args.contains(QStringLiteral("y"))) node.y = args.value(QStringLiteral("y")).toDouble();
    if (args.contains(QStringLiteral("width"))) node.width = args.value(QStringLiteral("width")).toDouble();
    if (args.contains(QStringLiteral("height"))) node.height = args.value(QStringLiteral("height")).toDouble();
    if (args.contains(QStringLiteral("shape"))) {
        const QString s = args.value(QStringLiteral("shape")).toString().trimmed();
        if (!LoreGraph::isValidNodeShape(s)) {
            return makeToolResult(QStringLiteral("Invalid node shape '%1'. Valid shapes are box, rounded, diamond, ellipse, cylinder, hexagon.").arg(s), true);
        }
        node.shape = s.toLower();
    }
    if (args.contains(QStringLiteral("color"))) {
        const QString c = args.value(QStringLiteral("color")).toString().trimmed();
        if (!LoreGraph::isValidHexOrNamedColor(c)) {
            return makeToolResult(QStringLiteral("Invalid node color '%1'. Must be a valid hex color or CSS color name.").arg(c), true);
        }
        node.color = c;
    }
    if (args.contains(QStringLiteral("loreId"))) node.loreId = args.value(QStringLiteral("loreId")).toString();
    if (args.contains(QStringLiteral("notes"))) node.notes = args.value(QStringLiteral("notes")).toString();

    graph->addOrUpdateNode(node);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    m_mainWindow->project().markDirtyBy(modifier);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("add_lore_node"),
        QStringLiteral("Added or updated lore node '%1' ('%2') in graph '%3'.")
            .arg(node.id, node.label, graphName)
    );
    m_mainWindow->updateLoreGraphUi();

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("nodeId")] = node.id;
    result[QStringLiteral("label")] = node.label;
    result[QStringLiteral("type")] = node.type;
    result[QStringLiteral("pos")] = QStringLiteral("(%1, %2)").arg(node.x).arg(node.y);
    return makeToolJsonResult(result);
}

QJsonObject McpHandler::toolConnectLoreNodes(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString fromId = args.value(QStringLiteral("fromNodeId")).toString().trimmed();
    const QString toId = args.value(QStringLiteral("toNodeId")).toString().trimmed();
    if (fromId.isEmpty() || toId.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'fromNodeId' or 'toNodeId'."), true);
    }

    const QString graphName = args.value(QStringLiteral("graphName")).toString(QStringLiteral("data")).trimmed();
    LoreGraph *graph = m_mainWindow->project().lore().graph(graphName);
    if (!graph) {
        return makeToolResult(QStringLiteral("Lore graph '%1' not found.").arg(graphName), true);
    }

    if (!graph->findNode(fromId)) {
        return makeToolResult(QStringLiteral("Source node '%1' not found in graph.").arg(fromId), true);
    }
    if (!graph->findNode(toId)) {
        return makeToolResult(QStringLiteral("Target node '%1' not found in graph.").arg(toId), true);
    }

    const QString arrow = args.value(QStringLiteral("arrow")).toString(QStringLiteral("->")).trimmed();
    if (!LoreGraph::isValidArrowType(arrow)) {
        return makeToolResult(QStringLiteral("Invalid connector arrow '%1'. Valid arrows are ->, <->, --, -x.").arg(arrow), true);
    }
    const QString label = args.value(QStringLiteral("label")).toString();
    const QString style = args.value(QStringLiteral("style")).toString(QStringLiteral("solid")).trimmed();
    if (!LoreGraph::isValidConnectorStyle(style)) {
        return makeToolResult(QStringLiteral("Invalid connector style '%1'. Valid styles are solid, dashed, dotted, thick.").arg(style), true);
    }
    const QString color = args.value(QStringLiteral("color")).toString(QStringLiteral("#94a3b8")).trimmed();
    if (!LoreGraph::isValidHexOrNamedColor(color)) {
        return makeToolResult(QStringLiteral("Invalid connector color '%1'. Must be a valid hex color or CSS color name.").arg(color), true);
    }

    graph->connectNodes(fromId, toId, arrow, label, style, color);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    m_mainWindow->project().markDirtyBy(modifier);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("connect_lore_nodes"),
        QStringLiteral("Connected lore nodes '%1' %2 '%3' in graph '%4'.")
            .arg(fromId, arrow, toId, graphName)
    );
    m_mainWindow->updateLoreGraphUi();

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("fromNodeId")] = fromId;
    result[QStringLiteral("toNodeId")] = toId;
    result[QStringLiteral("arrow")] = arrow;
    result[QStringLiteral("label")] = label;
    return makeToolJsonResult(result);
}

QJsonObject McpHandler::toolMoveLoreNode(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString nodeId = args.value(QStringLiteral("nodeId")).toString().trimmed();
    if (nodeId.isEmpty()) {
        return makeToolResult(QStringLiteral("Missing required 'nodeId'."), true);
    }

    if (!args.contains(QStringLiteral("x")) || !args.contains(QStringLiteral("y"))) {
        return makeToolResult(QStringLiteral("Missing required 'x' or 'y' coordinates."), true);
    }

    const double x = args.value(QStringLiteral("x")).toDouble();
    const double y = args.value(QStringLiteral("y")).toDouble();
    const QString graphName = args.value(QStringLiteral("graphName")).toString(QStringLiteral("data")).trimmed();

    LoreGraph *graph = m_mainWindow->project().lore().graph(graphName);
    if (!graph) {
        return makeToolResult(QStringLiteral("Lore graph '%1' not found.").arg(graphName), true);
    }

    if (!graph->findNode(nodeId)) {
        return makeToolResult(QStringLiteral("Node '%1' not found in graph '%2'.").arg(nodeId, graphName), true);
    }

    graph->updateNodePosition(nodeId, x, y);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    m_mainWindow->project().markDirtyBy(modifier);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("move_lore_node"),
        QStringLiteral("Moved lore node '%1' to (%2, %3) in graph '%4'.")
            .arg(nodeId).arg(x).arg(y).arg(graphName)
    );
    m_mainWindow->updateLoreGraphUi();

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    result[QStringLiteral("nodeId")] = nodeId;
    result[QStringLiteral("newX")] = x;
    result[QStringLiteral("newY")] = y;
    return makeToolJsonResult(result);
}

QJsonObject McpHandler::toolGetLoreData(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString section = args.value(QStringLiteral("section")).toString(QStringLiteral("all")).toLower();
    const QString query = args.value(QStringLiteral("query")).toString().toLower();
    const LoreManager &lore = m_mainWindow->project().lore();

    QJsonObject result;

    if (section == QStringLiteral("all") || section == QStringLiteral("entities")) {
        QJsonArray arr;
        for (const auto &e : lore.entities()) {
            if (!query.isEmpty() && !e.name.toLower().contains(query) && !e.description.toLower().contains(query) && !e.id.toLower().contains(query)) {
                continue;
            }
            QJsonObject obj;
            obj[QStringLiteral("id")] = e.id;
            obj[QStringLiteral("name")] = e.name;
            obj[QStringLiteral("category")] = e.category;
            obj[QStringLiteral("description")] = e.description;
            obj[QStringLiteral("tags")] = QJsonArray::fromStringList(e.tags);
            obj[QStringLiteral("associatedCharacters")] = QJsonArray::fromStringList(e.associatedCharacters);
            QJsonArray chs;
            for (int ch : e.chaptersReferenced) chs.append(ch);
            obj[QStringLiteral("chaptersReferenced")] = chs;
            arr.append(obj);
        }
        result[QStringLiteral("entities")] = arr;
    }

    if (section == QStringLiteral("all") || section == QStringLiteral("history")) {
        QJsonArray arr;
        for (const auto &h : lore.historyEvents()) {
            if (!query.isEmpty() && !h.title.toLower().contains(query) && !h.summary.toLower().contains(query)) {
                continue;
            }
            QJsonObject obj;
            obj[QStringLiteral("id")] = h.id;
            obj[QStringLiteral("eraOrDate")] = h.eraOrDate;
            obj[QStringLiteral("title")] = h.title;
            obj[QStringLiteral("storyChapter")] = h.storyChapter;
            obj[QStringLiteral("summary")] = h.summary;
            obj[QStringLiteral("impact")] = h.impact;
            obj[QStringLiteral("involvedEntities")] = QJsonArray::fromStringList(h.involvedEntities);
            arr.append(obj);
        }
        result[QStringLiteral("historyEvents")] = arr;
    }

    if (section == QStringLiteral("all") || section == QStringLiteral("conflicts")) {
        QJsonArray arr;
        for (const auto &c : lore.conflicts()) {
            if (!query.isEmpty() && !c.title.toLower().contains(query) && !c.description.toLower().contains(query)) {
                continue;
            }
            QJsonObject obj;
            obj[QStringLiteral("id")] = c.id;
            obj[QStringLiteral("title")] = c.title;
            obj[QStringLiteral("severity")] = c.severity;
            obj[QStringLiteral("status")] = c.status;
            obj[QStringLiteral("chapterA")] = c.chapterA;
            obj[QStringLiteral("chapterB")] = c.chapterB;
            obj[QStringLiteral("description")] = c.description;
            obj[QStringLiteral("detectedBy")] = c.detectedBy;
            arr.append(obj);
        }
        result[QStringLiteral("conflicts")] = arr;
    }

    if (section == QStringLiteral("all") || section == QStringLiteral("resolved")) {
        QJsonArray arr;
        for (const auto &r : lore.resolvedConflicts()) {
            if (!query.isEmpty() && !r.title.toLower().contains(query) && !r.resolution.toLower().contains(query)) {
                continue;
            }
            QJsonObject obj;
            obj[QStringLiteral("id")] = r.id;
            obj[QStringLiteral("title")] = r.title;
            obj[QStringLiteral("resolution")] = r.resolution;
            obj[QStringLiteral("canonRuling")] = r.canonRuling;
            obj[QStringLiteral("resolvedBy")] = r.resolvedBy;
            arr.append(obj);
        }
        result[QStringLiteral("resolvedConflicts")] = arr;
    }

    if (section == QStringLiteral("all") || section == QStringLiteral("plans")) {
        QJsonArray arr;
        for (const auto &p : lore.plans()) {
            if (!query.isEmpty() && !p.title.toLower().contains(query) && !p.description.toLower().contains(query)) {
                continue;
            }
            QJsonObject obj;
            obj[QStringLiteral("id")] = p.id;
            obj[QStringLiteral("title")] = p.title;
            obj[QStringLiteral("targetChapter")] = p.targetChapter;
            obj[QStringLiteral("status")] = p.status;
            obj[QStringLiteral("description")] = p.description;
            obj[QStringLiteral("loreThread")] = p.loreThread;
            obj[QStringLiteral("neededFollowUps")] = QJsonArray::fromStringList(p.neededFollowUps);
            arr.append(obj);
        }
        result[QStringLiteral("plans")] = arr;
    }

    if (section == QStringLiteral("all") || section == QStringLiteral("references")) {
        QJsonArray arr;
        for (const auto &rf : lore.references()) {
            if (!query.isEmpty() && !rf.title.toLower().contains(query) && !rf.source.toLower().contains(query)) {
                continue;
            }
            QJsonObject obj;
            obj[QStringLiteral("id")] = rf.id;
            obj[QStringLiteral("title")] = rf.title;
            obj[QStringLiteral("source")] = rf.source;
            obj[QStringLiteral("citation")] = rf.citation;
            obj[QStringLiteral("applicability")] = rf.applicability;
            obj[QStringLiteral("notes")] = rf.notes;
            arr.append(obj);
        }
        result[QStringLiteral("references")] = arr;
    }

    result[QStringLiteral("archiveDirectory")] = QStringLiteral("lore/");
    result[QStringLiteral("manifestFiles")] = QJsonArray{
        QStringLiteral("lore/lore_data.json"),
        QStringLiteral("lore/history.json"),
        QStringLiteral("lore/conflicts.json"),
        QStringLiteral("lore/resolved.json"),
        QStringLiteral("lore/plans.json"),
        QStringLiteral("lore/references.json"),
        QStringLiteral("lore/metadata.xml"),
        QStringLiteral("lore/graphs/data.grph")
    };

    return makeToolJsonResult(result);
}

QJsonObject McpHandler::toolManageLoreData(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const QString action = args.value(QStringLiteral("action")).toString().trimmed();
    LoreManager &lore = m_mainWindow->project().lore();

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;

    static const QSet<QString> commonKeys = {
        QStringLiteral("action"), QStringLiteral("model"),
        QStringLiteral("thought"), QStringLiteral("reasoning")
    };

    QSet<QString> allowedKeys = commonKeys;
    if (action == QStringLiteral("add_entity") || action == QStringLiteral("update_entity")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("category"),
                            QStringLiteral("description"), QStringLiteral("tags"),
                            QStringLiteral("associatedCharacters"), QStringLiteral("chaptersReferenced")});
    } else if (action == QStringLiteral("remove_entity")) {
        allowedKeys.insert(QStringLiteral("id"));
    } else if (action == QStringLiteral("add_history") || action == QStringLiteral("update_history")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("eraOrDate"),
                            QStringLiteral("storyChapter"), QStringLiteral("chapterIndex"), QStringLiteral("chapter"),
                            QStringLiteral("summary"), QStringLiteral("description"),
                            QStringLiteral("impact"), QStringLiteral("involvedEntities")});
    } else if (action == QStringLiteral("remove_history")) {
        allowedKeys.insert(QStringLiteral("id"));
    } else if (action == QStringLiteral("add_conflict") || action == QStringLiteral("update_conflict")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("severity"),
                            QStringLiteral("status"), QStringLiteral("chapterA"), QStringLiteral("chapterB"),
                            QStringLiteral("description"), QStringLiteral("detectedBy"), QStringLiteral("detectedAt")});
    } else if (action == QStringLiteral("resolve_conflict")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("conflictId"), QStringLiteral("id"),
                            QStringLiteral("resolution"), QStringLiteral("canonRuling")});
    } else if (action == QStringLiteral("add_plan") || action == QStringLiteral("update_plan")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("targetChapter"),
                            QStringLiteral("chapterIndex"), QStringLiteral("chapter"),
                            QStringLiteral("status"), QStringLiteral("description"), QStringLiteral("loreThread"),
                            QStringLiteral("thread"), QStringLiteral("category"), QStringLiteral("notes"),
                            QStringLiteral("neededFollowUps")});
    } else if (action == QStringLiteral("remove_plan")) {
        allowedKeys.insert(QStringLiteral("id"));
    } else if (action == QStringLiteral("add_reference") || action == QStringLiteral("update_reference")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("source"),
                            QStringLiteral("citation"), QStringLiteral("applicability"),
                            QStringLiteral("notes"), QStringLiteral("description")});
    } else if (action == QStringLiteral("remove_reference")) {
        allowedKeys.insert(QStringLiteral("id"));
    } else if (action == QStringLiteral("remove_graph")) {
        allowedKeys.unite(QSet<QString>{QStringLiteral("name"), QStringLiteral("graphName"), QStringLiteral("id")});
    } else {
        return makeToolResult(QStringLiteral("Unknown lore management action '%1'.").arg(action), true);
    }

    QStringList unknownKeys;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            unknownKeys.append(it.key());
        }
    }
    if (!unknownKeys.isEmpty()) {
        QStringList allowedList(allowedKeys.begin(), allowedKeys.end());
        allowedList.sort();
        return makeToolResult(
            QStringLiteral("Unrecognized field(s) '%1' for lore action '%2'. Supported fields: %3.")
                .arg(unknownKeys.join(QStringLiteral(", ")), action, allowedList.join(QStringLiteral(", "))),
            true
        );
    }

    QString historySummary;
    QString historyDetails;
    int targetChapter = -1;
    QJsonObject res;

    if (action == QStringLiteral("add_entity") || action == QStringLiteral("update_entity")) {
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) return makeToolResult(QStringLiteral("Missing required 'id' parameter for entity."), true);

        LoreEntity ent;
        if (const LoreEntity *ex = lore.findEntity(id)) {
            ent = *ex;
        } else {
            ent.id = id;
            ent.created = QDateTime::currentDateTimeUtc();
        }
        if (args.contains(QStringLiteral("name"))) ent.name = args.value(QStringLiteral("name")).toString();
        if (args.contains(QStringLiteral("category"))) ent.category = args.value(QStringLiteral("category")).toString();
        if (args.contains(QStringLiteral("description"))) ent.description = args.value(QStringLiteral("description")).toString();
        if (args.contains(QStringLiteral("tags"))) {
            ent.tags.clear();
            for (const auto &v : args.value(QStringLiteral("tags")).toArray()) ent.tags.append(v.toString());
        }
        ent.modified = QDateTime::currentDateTimeUtc();
        lore.addOrUpdateEntity(ent);

        historySummary = QStringLiteral("Added or updated lore entity '%1' ('%2', category: '%3')")
                             .arg(ent.id, ent.name.isEmpty() ? ent.id : ent.name, ent.category.isEmpty() ? QStringLiteral("general") : ent.category);
        historyDetails = ent.description;
        res[QStringLiteral("entityId")] = ent.id;
        res[QStringLiteral("name")] = ent.name;
    } else if (action == QStringLiteral("remove_entity")) {
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) return makeToolResult(QStringLiteral("Missing 'id' parameter for remove_entity."), true);
        if (!lore.removeEntity(id)) {
            return makeToolResult(QStringLiteral("Entity '%1' not found in lore database.").arg(id), true);
        }
        historySummary = QStringLiteral("Removed lore entity '%1'").arg(id);
        res[QStringLiteral("entityId")] = id;
    } else if (action == QStringLiteral("add_history") || action == QStringLiteral("update_history")) {
        LoreHistoryEvent ev;
        ev.id = args.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        ev.eraOrDate = args.value(QStringLiteral("eraOrDate")).toString();
        ev.title = args.value(QStringLiteral("title")).toString();
        ev.storyChapter = args.value(QStringLiteral("storyChapter")).toInt(
            args.value(QStringLiteral("chapterIndex")).toInt(
                args.value(QStringLiteral("chapter")).toInt(-1)
            )
        );
        if (args.contains(QStringLiteral("summary"))) {
            ev.summary = args.value(QStringLiteral("summary")).toString();
        } else if (args.contains(QStringLiteral("description"))) {
            ev.summary = args.value(QStringLiteral("description")).toString();
        }
        if (ev.summary.trimmed().isEmpty()) {
            return makeToolResult(QStringLiteral("Missing required 'summary' (or 'description') parameter for history event."), true);
        }
        if (ev.title.trimmed().isEmpty()) {
            ev.title = ev.summary.left(50);
        }
        ev.impact = args.value(QStringLiteral("impact")).toString();
        if (args.contains(QStringLiteral("involvedEntities"))) {
            for (const auto &v : args.value(QStringLiteral("involvedEntities")).toArray()) {
                ev.involvedEntities.append(v.toString());
            }
        }
        lore.addOrUpdateHistoryEvent(ev);

        historySummary = QStringLiteral("Added lore history event '%1' ('%2'): %3")
                             .arg(ev.id, ev.title, ev.summary);
        historyDetails = ev.impact;
        targetChapter = ev.storyChapter;
        res[QStringLiteral("historyId")] = ev.id;
        res[QStringLiteral("title")] = ev.title;
        res[QStringLiteral("summary")] = ev.summary;
    } else if (action == QStringLiteral("remove_history")) {
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) return makeToolResult(QStringLiteral("Missing 'id' parameter for remove_history."), true);
        if (!lore.removeHistoryEvent(id)) {
            return makeToolResult(QStringLiteral("History event '%1' not found in lore database.").arg(id), true);
        }
        historySummary = QStringLiteral("Removed lore history event '%1'").arg(id);
        res[QStringLiteral("historyId")] = id;
    } else if (action == QStringLiteral("add_conflict") || action == QStringLiteral("update_conflict")) {
        LoreConflict conf;
        conf.id = args.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        conf.title = args.value(QStringLiteral("title")).toString();
        conf.severity = args.value(QStringLiteral("severity")).toString(QStringLiteral("medium"));
        conf.status = QStringLiteral("open");
        conf.chapterA = args.value(QStringLiteral("chapterA")).toInt(0);
        conf.chapterB = args.value(QStringLiteral("chapterB")).toInt(0);
        conf.description = args.value(QStringLiteral("description")).toString();
        conf.detectedBy = modifier;
        conf.detectedAt = QDateTime::currentDateTimeUtc();
        lore.addOrUpdateConflict(conf);

        historySummary = QStringLiteral("Logged lore conflict '%1' ('%2', severity: %3)")
                             .arg(conf.id, conf.title, conf.severity);
        historyDetails = conf.description;
        targetChapter = conf.chapterA > 0 ? conf.chapterA - 1 : -1;
        res[QStringLiteral("conflictId")] = conf.id;
        res[QStringLiteral("title")] = conf.title;
    } else if (action == QStringLiteral("resolve_conflict")) {
        const QString conflictId = args.value(QStringLiteral("conflictId")).toString(args.value(QStringLiteral("id")).toString()).trimmed();
        const QString resolution = args.value(QStringLiteral("resolution")).toString().trimmed();
        const QString canonRuling = args.value(QStringLiteral("canonRuling")).toString();
        if (conflictId.isEmpty() || resolution.isEmpty()) {
            return makeToolResult(QStringLiteral("Missing 'conflictId' or 'resolution' parameter."), true);
        }
        if (!lore.resolveConflict(conflictId, resolution, canonRuling, modifier)) {
            return makeToolResult(QStringLiteral("Conflict '%1' not found.").arg(conflictId), true);
        }
        historySummary = QStringLiteral("Resolved lore conflict '%1': %2").arg(conflictId, resolution);
        historyDetails = canonRuling;
        res[QStringLiteral("conflictId")] = conflictId;
        res[QStringLiteral("resolution")] = resolution;
    } else if (action == QStringLiteral("add_plan") || action == QStringLiteral("update_plan")) {
        LorePlan plan;
        plan.id = args.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        plan.title = args.value(QStringLiteral("title")).toString();
        plan.targetChapter = args.value(QStringLiteral("targetChapter")).toInt(
            args.value(QStringLiteral("chapterIndex")).toInt(
                args.value(QStringLiteral("chapter")).toInt(0)
            )
        );
        plan.status = args.value(QStringLiteral("status")).toString(QStringLiteral("planned"));
        plan.description = args.value(QStringLiteral("description")).toString();
        if (args.contains(QStringLiteral("loreThread"))) {
            plan.loreThread = args.value(QStringLiteral("loreThread")).toString();
        } else if (args.contains(QStringLiteral("thread"))) {
            plan.loreThread = args.value(QStringLiteral("thread")).toString();
        } else if (args.contains(QStringLiteral("category"))) {
            plan.loreThread = args.value(QStringLiteral("category")).toString();
        }
        if (args.contains(QStringLiteral("notes"))) {
            plan.notes = args.value(QStringLiteral("notes")).toString();
        }
        if (args.contains(QStringLiteral("neededFollowUps"))) {
            for (const auto &v : args.value(QStringLiteral("neededFollowUps")).toArray()) plan.neededFollowUps.append(v.toString());
        }
        lore.addOrUpdatePlan(plan);

        historySummary = QStringLiteral("Added lore plan '%1' ('%2', targetChapter: %3)")
                             .arg(plan.id, plan.title).arg(plan.targetChapter);
        historyDetails = plan.description;
        targetChapter = plan.targetChapter > 0 ? plan.targetChapter - 1 : -1;
        res[QStringLiteral("planId")] = plan.id;
        res[QStringLiteral("title")] = plan.title;
    } else if (action == QStringLiteral("remove_plan")) {
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) return makeToolResult(QStringLiteral("Missing 'id' parameter for remove_plan."), true);
        if (!lore.removePlan(id)) {
            return makeToolResult(QStringLiteral("Plan '%1' not found in lore database.").arg(id), true);
        }
        historySummary = QStringLiteral("Removed lore plan '%1'").arg(id);
        res[QStringLiteral("planId")] = id;
    } else if (action == QStringLiteral("add_reference") || action == QStringLiteral("update_reference")) {
        LoreReference ref;
        ref.id = args.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        ref.title = args.value(QStringLiteral("title")).toString();
        ref.source = args.value(QStringLiteral("source")).toString();
        ref.citation = args.value(QStringLiteral("citation")).toString();
        ref.applicability = args.value(QStringLiteral("applicability")).toString();
        if (args.contains(QStringLiteral("notes"))) {
            ref.notes = args.value(QStringLiteral("notes")).toString();
        } else if (args.contains(QStringLiteral("description"))) {
            ref.notes = args.value(QStringLiteral("description")).toString();
        }
        lore.addOrUpdateReference(ref);

        historySummary = QStringLiteral("Added lore reference '%1' ('%2', source: '%3')")
                             .arg(ref.id, ref.title, ref.source);
        historyDetails = ref.citation;
        res[QStringLiteral("referenceId")] = ref.id;
        res[QStringLiteral("title")] = ref.title;
    } else if (action == QStringLiteral("remove_reference")) {
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) return makeToolResult(QStringLiteral("Missing 'id' parameter for remove_reference."), true);
        if (!lore.removeReference(id)) {
            return makeToolResult(QStringLiteral("Reference '%1' not found in lore database.").arg(id), true);
        }
        historySummary = QStringLiteral("Removed lore reference '%1'").arg(id);
        res[QStringLiteral("referenceId")] = id;
    } else if (action == QStringLiteral("remove_graph")) {
        const QString graphName = args.value(QStringLiteral("name")).toString(
            args.value(QStringLiteral("graphName")).toString(
                args.value(QStringLiteral("id")).toString()
            )
        ).trimmed();
        if (graphName.isEmpty()) return makeToolResult(QStringLiteral("Missing 'name', 'graphName', or 'id' parameter for remove_graph."), true);
        const bool isDefault = (graphName == QStringLiteral("data") || graphName.isEmpty());
        if (!lore.removeNamedGraph(graphName)) {
            return makeToolResult(QStringLiteral("Lore graph '%1' not found in lore database.").arg(graphName), true);
        }
        res[QStringLiteral("name")] = graphName;
        res[QStringLiteral("isDefaultGraph")] = isDefault;
        if (isDefault) {
            res[QStringLiteral("resetToEmpty")] = true;
            res[QStringLiteral("message")] = QStringLiteral("The primary default graph 'data' cannot be deleted from the archive; it has been reset to an empty graph (all nodes, connectors, and boxes removed).");
            res[QStringLiteral("note")] = QStringLiteral("Primary default graph 'data' was reset to an empty state rather than deleted.");
            historySummary = QStringLiteral("Reset primary lore graph '%1' to empty").arg(graphName);
        } else {
            res[QStringLiteral("message")] = QStringLiteral("Lore graph '%1' removed successfully.").arg(graphName);
            historySummary = QStringLiteral("Removed lore graph '%1'").arg(graphName);
        }
    }

    const QString callerModel = resolveCallerModel(caller, args);
    m_mainWindow->project().markDirtyBy(modifier);
    m_mainWindow->project().agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("manage_lore_data"),
        historySummary,
        targetChapter,
        0,
        historyDetails
    );
    m_mainWindow->updateLoreGraphUi();

    res[QStringLiteral("success")] = true;
    res[QStringLiteral("action")] = action;
    res[QStringLiteral("entityCount")] = lore.entities().size();
    res[QStringLiteral("conflictCount")] = lore.conflicts().size();
    res[QStringLiteral("planCount")] = lore.plans().size();
    res[QStringLiteral("referenceCount")] = lore.references().size();
    res[QStringLiteral("historyCount")] = lore.historyEvents().size();
    res[QStringLiteral("graphCount")] = lore.graphNames().size();
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolUpdateProjectMetadata(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    Ao3Project &proj = m_mainWindow->project();

    if (args.contains(QStringLiteral("title"))) {
        proj.setTitle(args.value(QStringLiteral("title")).toString().trimmed());
    }
    if (args.contains(QStringLiteral("author"))) {
        proj.setAuthor(args.value(QStringLiteral("author")).toString().trimmed());
    }
    if (args.contains(QStringLiteral("rating"))) {
        proj.setRating(args.value(QStringLiteral("rating")).toString().trimmed());
    }
    if (args.contains(QStringLiteral("archiveWarnings"))) {
        QStringList list;
        for (const auto &v : args.value(QStringLiteral("archiveWarnings")).toArray()) list.append(v.toString());
        proj.archiveWarnings() = list;
    }
    if (args.contains(QStringLiteral("categories"))) {
        QStringList list;
        for (const auto &v : args.value(QStringLiteral("categories")).toArray()) list.append(v.toString());
        proj.categories() = list;
    }
    if (args.contains(QStringLiteral("fandoms"))) {
        proj.fandoms().clear();
        for (const auto &v : args.value(QStringLiteral("fandoms")).toArray()) proj.addFandom(v.toString());
    }
    if (args.contains(QStringLiteral("relationships"))) {
        proj.relationships().clear();
        for (const auto &v : args.value(QStringLiteral("relationships")).toArray()) proj.addRelationship(v.toString());
    }
    if (args.contains(QStringLiteral("characters"))) {
        proj.characters().clear();
        for (const auto &v : args.value(QStringLiteral("characters")).toArray()) proj.addCharacter(v.toString());
    }
    if (args.contains(QStringLiteral("tags"))) {
        proj.tags().clear();
        for (const auto &v : args.value(QStringLiteral("tags")).toArray()) proj.addTag(v.toString());
    }

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    proj.markDirtyBy(modifier);
    proj.agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("update_project_metadata"),
        QStringLiteral("Updated project metadata (rating, tags, fandoms, characters).")
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("title")] = proj.title();
    res[QStringLiteral("author")] = proj.author();
    res[QStringLiteral("rating")] = proj.rating();
    res[QStringLiteral("archiveWarnings")] = QJsonArray::fromStringList(proj.archiveWarnings());
    res[QStringLiteral("categories")] = QJsonArray::fromStringList(proj.categories());
    res[QStringLiteral("fandoms")] = QJsonArray::fromStringList(proj.fandoms());
    res[QStringLiteral("relationships")] = QJsonArray::fromStringList(proj.relationships());
    res[QStringLiteral("characters")] = QJsonArray::fromStringList(proj.characters());
    res[QStringLiteral("tags")] = QJsonArray::fromStringList(proj.tags());
    return makeToolJsonResult(res);
}

QJsonObject McpHandler::toolListImageEmbeds(const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    const Ao3Project &proj = m_mainWindow->project();
    const int filterChapter = args.contains(QStringLiteral("chapterIndex"))
        ? args.value(QStringLiteral("chapterIndex")).toInt(-1)
        : -1;

    static const QRegularExpression reImg(
        QStringLiteral(R"(<img\b([^>]*?)\/?>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSrc(
        QStringLiteral(R"(src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reAlt(
        QStringLiteral(R"(alt\s*=\s*["']([^"']*)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTitle(
        QStringLiteral(R"(title\s*=\s*["']([^"']*)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reWidth(
        QStringLiteral(R"(width\s*=\s*["']?(\d+)["']?)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reHeight(
        QStringLiteral(R"(height\s*=\s*["']?(\d+)["']?)"),
        QRegularExpression::CaseInsensitiveOption);

    QJsonArray embeds;
    const auto &chapters = proj.chapters();
    for (int i = 0; i < chapters.size(); ++i) {
        if (filterChapter >= 0 && i != filterChapter)
            continue;

        const auto &chap = chapters.at(i);
        auto it = reImg.globalMatch(chap.html());
        while (it.hasNext()) {
            const auto match = it.next();
            const QString tag = match.captured(0);
            const QString attrs = match.captured(1);
            const auto mSrc = reSrc.match(attrs);
            if (!mSrc.hasMatch()) continue;

            QJsonObject item;
            item[QStringLiteral("chapterIndex")] = i;
            item[QStringLiteral("chapterTitle")] = chap.title();
            item[QStringLiteral("url")] = mSrc.captured(1).trimmed();

            const auto mAlt = reAlt.match(attrs);
            if (mAlt.hasMatch()) item[QStringLiteral("alt")] = mAlt.captured(1);

            const auto mTit = reTitle.match(attrs);
            if (mTit.hasMatch()) item[QStringLiteral("title")] = mTit.captured(1);

            const auto mW = reWidth.match(attrs);
            if (mW.hasMatch()) item[QStringLiteral("width")] = mW.captured(1).toInt();

            const auto mH = reHeight.match(attrs);
            if (mH.hasMatch()) item[QStringLiteral("height")] = mH.captured(1).toInt();

            item[QStringLiteral("rawTag")] = tag;
            embeds.append(item);
        }
    }

    QJsonObject resp;
    resp[QStringLiteral("totalImageEmbeds")] = embeds.size();
    resp[QStringLiteral("imageEmbeds")] = embeds;
    return makeToolJsonResult(resp);
}

QJsonObject McpHandler::toolInsertImageEmbed(QWebSocket *client, const QJsonObject &args)
{
    if (!m_mainWindow || !m_mainWindow->hasActiveProject()) {
        return makeToolResult(QStringLiteral("No project is currently open in Ensemble."), true);
    }

    Ao3Project &proj = m_mainWindow->project();
    if (!args.contains(QStringLiteral("chapterIndex")) || !args.contains(QStringLiteral("url"))) {
        return makeToolResult(QStringLiteral("Error: 'chapterIndex' and 'url' are required arguments."), true);
    }

    const int chIdx = args.value(QStringLiteral("chapterIndex")).toInt(-1);
    if (chIdx < 0 || chIdx >= proj.chapters().size()) {
        return makeToolResult(QStringLiteral("Error: Invalid chapterIndex %1. Project has %2 chapters.")
                                  .arg(chIdx).arg(proj.chapters().size()), true);
    }

    const QString url = args.value(QStringLiteral("url")).toString().trimmed();
    if (url.isEmpty()) {
        return makeToolResult(QStringLiteral("Error: 'url' must not be empty."), true);
    }

    if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return makeToolResult(QStringLiteral("Error: 'url' must begin with http:// or https:// for AO3 compliance."), true);
    }

    const QString alt = args.value(QStringLiteral("alt")).toString().trimmed();
    const QString title = args.value(QStringLiteral("title")).toString().trimmed();
    const int width = args.value(QStringLiteral("width")).toInt(0);
    const int height = args.value(QStringLiteral("height")).toInt(0);
    const QString align = args.value(QStringLiteral("alignment")).toString(QStringLiteral("center")).toLower();
    const bool wrapLink = args.value(QStringLiteral("wrapInLink")).toBool(true);
    const QString pos = args.value(QStringLiteral("position")).toString(QStringLiteral("append")).toLower();

    // Generate pure AO3 image tag
    QString imgTag = QStringLiteral("<img src=\"%1\"").arg(url);
    if (!alt.isEmpty()) imgTag += QStringLiteral(" alt=\"%1\"").arg(alt);
    if (!title.isEmpty()) imgTag += QStringLiteral(" title=\"%1\"").arg(title);
    if (width > 0) imgTag += QStringLiteral(" width=\"%1\"").arg(width);
    if (height > 0) imgTag += QStringLiteral(" height=\"%1\"").arg(height);
    imgTag += QStringLiteral(" />");

    if (wrapLink) {
        imgTag = QStringLiteral("<a href=\"%1\">%2</a>").arg(url, imgTag);
    }

    QString blockTag = imgTag;
    if (align == QStringLiteral("center")) {
        blockTag = QStringLiteral("<p align=\"center\">%1</p>").arg(imgTag);
    } else if (align == QStringLiteral("left")) {
        blockTag = QStringLiteral("<p align=\"left\">%1</p>").arg(imgTag);
    } else if (align == QStringLiteral("right")) {
        blockTag = QStringLiteral("<p align=\"right\">%1</p>").arg(imgTag);
    }

    const Chapter &currentChap = proj.chapters().at(chIdx);
    QString currentHtml = currentChap.html().trimmed();
    QString updatedHtml;
    if (pos == QStringLiteral("prepend")) {
        updatedHtml = blockTag + QStringLiteral("\n\n") + currentHtml;
    } else {
        if (!currentHtml.isEmpty()) {
            updatedHtml = currentHtml + QStringLiteral("\n\n") + blockTag;
        } else {
            updatedHtml = blockTag;
        }
    }

    m_mainWindow->applyMcpChapterHtml(chIdx, updatedHtml);

    const auto caller = client && m_server ? m_server->agentSessionForClient(client) : McpAgentSession();
    const QString modifier = caller.name.isEmpty() ? QStringLiteral("AI Agent") : caller.name;
    const QString callerModel = resolveCallerModel(caller, args);
    proj.markDirtyBy(modifier);
    proj.appendHistoryCommit(modifier, QStringLiteral("Embedded image URL in Chapter %1 ('%2')")
                                           .arg(chIdx + 1).arg(currentChap.title()));
    proj.agenticData().appendHistoryEntry(
        modifier, callerModel, caller.role, QStringLiteral("insert_image_embed"),
        QStringLiteral("Embedded image in Chapter %1 ('%2'): %3")
            .arg(chIdx + 1).arg(currentChap.title(), url),
        chIdx, 0, blockTag
    );

    QJsonObject res;
    res[QStringLiteral("success")] = true;
    res[QStringLiteral("chapterIndex")] = chIdx;
    res[QStringLiteral("chapterTitle")] = currentChap.title();
    res[QStringLiteral("embeddedHtml")] = blockTag;
    res[QStringLiteral("url")] = url;
    return makeToolJsonResult(res);
}
