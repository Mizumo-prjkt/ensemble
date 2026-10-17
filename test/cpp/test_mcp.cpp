#include "mcp/McpServer.h"
#include "mcp/McpHandler.h"
#include "mcp/McpAgentInstaller.h"
#include "ui/MainWindow.h"
#include "ui/AgentThoughtsDialog.h"
#include "ui/PreviewPane.h"
#include "ui/EditorPane.h"
#include "ui/ChapterManagerDialog.h"
#include "ui/ChapterSidebar.h"
#include "model/Ao3Project.h"
#include "export/Ao3HtmlSanitizer.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTextEdit>
#include <QTextCursor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QtWebSockets/QWebSocket>
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
    QStandardPaths::setTestModeEnabled(true);

    std::cout << "Testing Ensemble MCP Protocol, Tools, Resources & Loopback..." << std::endl;

    // 1. Launch MainWindow with embedded MCP server
    MainWindow mainWindow;
    QMetaObject::invokeMethod(&mainWindow, "onNewProject");
    mainWindow.startMcpServer(26303);

    McpServer *server = mainWindow.mcpServer();
    assert(server != nullptr);
    assert(server->isRunning());
    assert(server->port() > 0);
    std::cout << "✓ MCP server running on port " << server->port() << std::endl;

    // 1b. Test Word Count Breakdown on status bar (exact counts, no tilde, selection count)
    {
        mainWindow.updateStatusBar();
        const auto labels = mainWindow.statusBar()->findChildren<QLabel *>();
        bool foundWordCount = false;
        for (QLabel *lbl : labels) {
            if (lbl->text().startsWith(QStringLiteral("Words:"))) {
                foundWordCount = true;
                assert(!lbl->text().contains(QStringLiteral("~")));
                assert(lbl->text().contains(QStringLiteral("Total:")));
            }
        }
        assert(foundWordCount);

        // Test with editor text and selection
        auto *editorPane = mainWindow.findChild<EditorPane *>();
        assert(editorPane != nullptr);
        auto *editor = editorPane->editor();
        assert(editor != nullptr);

        editor->setPlainText(QStringLiteral("The quick brown fox jumps over the lazy dog."));
        mainWindow.updateStatusBar();

        bool verifiedWords = false;
        for (QLabel *lbl : labels) {
            if (lbl->text().startsWith(QStringLiteral("Words:"))) {
                assert(lbl->text().contains(QStringLiteral("9")));
                assert(!lbl->text().contains(QStringLiteral("~")));
                verifiedWords = true;
            }
        }
        assert(verifiedWords);

        // Highlight 3 words ("The quick brown")
        QTextCursor cur = editor->textCursor();
        cur.setPosition(0);
        cur.setPosition(15, QTextCursor::KeepAnchor);
        editor->setTextCursor(cur);
        mainWindow.updateStatusBar();

        bool verifiedSelection = false;
        for (QLabel *lbl : labels) {
            if (lbl->text().startsWith(QStringLiteral("Words:"))) {
                assert(lbl->text().contains(QStringLiteral("3 selected of 9")));
                assert(!lbl->text().contains(QStringLiteral("~")));
                verifiedSelection = true;
            }
        }
        assert(verifiedSelection);
        std::cout << "✓ Bottom-left status bar exact word count & selection breakdown verified." << std::endl;
    }

    // 1c. Test PreviewPane AO3 Default and AO3 Reversi background toggles
    {
        auto *preview = mainWindow.findChild<PreviewPane *>();
        assert(preview != nullptr);
        assert(preview->skinMode() == PreviewSkinMode::Ao3Default || preview->skinMode() == PreviewSkinMode::Ao3Reversi);

        preview->setSkinMode(PreviewSkinMode::Ao3Reversi);
        assert(preview->skinMode() == PreviewSkinMode::Ao3Reversi);

        preview->setSkinMode(PreviewSkinMode::Ao3Default);
        assert(preview->skinMode() == PreviewSkinMode::Ao3Default);
        std::cout << "✓ PreviewPane AO3 Default (Light) and Reversi (Dark) skin mode toggles verified." << std::endl;
    }

    // 1d. Test EditorPane toolbar active states when cursor moves into styled text
    {
        auto *editorPane = mainWindow.findChild<EditorPane *>();
        assert(editorPane != nullptr);
        auto *editor = editorPane->editor();
        assert(editor != nullptr);

        editor->setHtml(QStringLiteral("<p>Plain text <b>bold phrase</b> <i>italic phrase</i></p>"));

        // Position 16 is inside "bold phrase"
        QTextCursor cur = editor->textCursor();
        cur.setPosition(16);
        editor->setTextCursor(cur);
        editorPane->updateToolbarActiveStates();

        auto *tb = editorPane->findChild<QToolBar *>();
        assert(tb != nullptr);
        bool foundCheckedBold = false;
        for (QAction *act : tb->actions()) {
            if (act->isCheckable() && act->isChecked() && act->toolTip().contains(QStringLiteral("Bold"))) {
                foundCheckedBold = true;
            }
        }
        assert(foundCheckedBold);
        std::cout << "✓ EditorPane toolbar active visual state verified for bold text." << std::endl;
    }

    // 2. Connect client WebSocket to test live JSON-RPC roundtrip
    QWebSocket client;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(4000);

    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&client, &QWebSocket::connected, &loop, &QEventLoop::quit);

    timer.start();
    client.open(QUrl(server->serverUrl()));
    loop.exec();
    assert(client.isValid());
    std::cout << "✓ Client successfully connected to " << server->serverUrl().toStdString() << std::endl;

    auto sendAndReceive = [&](const QJsonObject &request) -> QJsonObject {
        QJsonObject response;
        QEventLoop reqLoop;
        QTimer reqTimer;
        reqTimer.setSingleShot(true);
        reqTimer.setInterval(3000);

        auto conn = QObject::connect(&client, &QWebSocket::textMessageReceived, [&](const QString &msg) {
            QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
            if (doc.isObject()) {
                response = doc.object();
                reqLoop.quit();
            }
        });
        QObject::connect(&reqTimer, &QTimer::timeout, &reqLoop, &QEventLoop::quit);

        reqTimer.start();
        client.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
        reqLoop.exec();

        QObject::disconnect(conn);
        return response;
    };

    // 3. Test "initialize"
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "initialize";
        req["params"] = QJsonObject();

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 1);
        QJsonObject result = resp.value("result").toObject();
        assert(result.value("protocolVersion").toString() == "2024-11-05");
        assert(result.value("serverInfo").toObject().value("name").toString() == "ensemble");
        std::cout << "✓ MCP 'initialize' verified." << std::endl;
    }

    // 4. Test "ping"
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 2;
        req["method"] = "ping";

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 2);
        assert(resp.contains("result"));
        std::cout << "✓ MCP 'ping' verified." << std::endl;
    }

    // 5. Test "tools/list"
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 3;
        req["method"] = "tools/list";

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 3);
        QJsonArray tools = resp.value("result").toObject().value("tools").toArray();
        assert(tools.size() >= 30);

        QStringList toolNames;
        for (const auto &t : tools) {
            toolNames << t.toObject().value("name").toString();
        }
        assert(toolNames.contains("get_project_summary"));
        assert(toolNames.contains("list_chapters"));
        assert(toolNames.contains("get_chapter"));
        assert(toolNames.contains("update_chapter_html"));
        assert(toolNames.contains("add_chapter"));
        assert(toolNames.contains("delete_chapter"));
        assert(toolNames.contains("reorder_chapters"));
        assert(toolNames.contains("get_workskin_css"));
        assert(toolNames.contains("set_workskin_css"));
        assert(toolNames.contains("get_problems"));
        assert(toolNames.contains("save_project"));
        assert(toolNames.contains("reload_project"));
        assert(toolNames.contains("get_preview_screenshot"));
        assert(toolNames.contains("get_agent_presence"));
        assert(toolNames.contains("register_agent_presence"));
        assert(toolNames.contains("test_firewall_connection"));
        assert(toolNames.contains("get_agentic_summary"));
        assert(toolNames.contains("read_agentic_file"));
        assert(toolNames.contains("write_agentic_file"));
        assert(toolNames.contains("append_agent_history"));
        assert(toolNames.contains("get_agent_history"));
        assert(toolNames.contains("post_agent_thought"));
        assert(toolNames.contains("get_lore_graph"));
        assert(toolNames.contains("update_lore_graph"));
        assert(toolNames.contains("add_lore_node"));
        assert(toolNames.contains("connect_lore_nodes"));
        assert(toolNames.contains("move_lore_node"));
        assert(toolNames.contains("get_lore_data"));
        assert(toolNames.contains("manage_lore_data"));
        assert(toolNames.contains("update_project_metadata"));
        std::cout << "✓ MCP 'tools/list' verified (" << tools.size() << " tools registered, including all 8 lore & metadata tools)." << std::endl;
    }

    // 6. Test "tools/call" -> test_firewall_connection
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 4;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "test_firewall_connection";
        params["arguments"] = QJsonObject();
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 4);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        std::cout << "✓ MCP 'test_firewall_connection' tool verified." << std::endl;
    }

    // 7. Test "tools/call" -> register_agent_presence (Agent 1: Antigravity)
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 5;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "register_agent_presence";
        QJsonObject args;
        args["agentName"] = "antigravity";
        args["role"] = "co-author";
        args["workingNote"] = "Reviewing story structure";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 5);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        QJsonArray content = resp.value("result").toObject().value("content").toArray();
        assert(!content.isEmpty());
        QJsonObject data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("success").toBool() == true);
        assert(data.value("name").toString() == "antigravity");
        assert(data.value("role").toString() == "co-author");
        std::cout << "✓ MCP 'register_agent_presence' (Agent 1: Antigravity) verified." << std::endl;
    }

    // 8. Connect Agent 2 (Claude) and test multi-agent mutual awareness!
    {
        QWebSocket client2;
        QEventLoop loop2;
        QTimer timer2;
        timer2.setSingleShot(true);
        timer2.setInterval(3000);

        QObject::connect(&timer2, &QTimer::timeout, &loop2, &QEventLoop::quit);
        QObject::connect(&client2, &QWebSocket::connected, &loop2, &QEventLoop::quit);

        timer2.start();
        client2.open(QUrl(server->serverUrl()));
        loop2.exec();
        assert(client2.isValid());

        auto sendAndReceive2 = [&](const QJsonObject &request) -> QJsonObject {
            QJsonObject response;
            QEventLoop reqLoop;
            QTimer reqTimer;
            reqTimer.setSingleShot(true);
            reqTimer.setInterval(3000);

            auto conn = QObject::connect(&client2, &QWebSocket::textMessageReceived, [&](const QString &msg) {
                QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
                if (doc.isObject()) {
                    response = doc.object();
                    reqLoop.quit();
                }
            });
            QObject::connect(&reqTimer, &QTimer::timeout, &reqLoop, &QEventLoop::quit);

            reqTimer.start();
            client2.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
            reqLoop.exec();
            QObject::disconnect(conn);
            return response;
        };

        // Initialize Agent 2 as "claude"
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 10;
            req["method"] = "initialize";
            QJsonObject clientInfo;
            clientInfo["name"] = "claude";
            clientInfo["version"] = "1.0.0";
            QJsonObject p;
            p["clientInfo"] = clientInfo;
            p["protocolVersion"] = "2024-11-05";
            req["params"] = p;
            QJsonObject resp = sendAndReceive2(req);
            assert(resp.value("id").toInt() == 10);
        }

        // Register Agent 2 presence as "reviewer"
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 11;
            req["method"] = "tools/call";
            QJsonObject params;
            params["name"] = "register_agent_presence";
            QJsonObject args;
            args["agentName"] = "claude";
            args["role"] = "reviewer";
            args["workingNote"] = "Analyzing Work Skin CSS";
            params["arguments"] = args;
            req["params"] = params;

            QJsonObject resp = sendAndReceive2(req);
            assert(resp.value("id").toInt() == 11);
            QJsonArray content = resp.value("result").toObject().value("content").toArray();
            QJsonObject data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
            assert(data.value("success").toBool() == true);
            assert(data.value("connectedCount").toInt() == 2);
            QJsonArray others = data.value("otherActiveAgents").toArray();
            assert(!others.isEmpty());
            assert(others.first().toObject().value("name").toString() == "antigravity");
            std::cout << "✓ Multi-Agent Awareness: Claude discovered Antigravity in session." << std::endl;
        }

        // Query get_agent_presence from Agent 1 (Antigravity) and verify it sees Claude
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 6;
            req["method"] = "tools/call";
            QJsonObject params;
            params["name"] = "get_agent_presence";
            params["arguments"] = QJsonObject();
            req["params"] = params;

            QJsonObject resp = sendAndReceive(req);
            assert(resp.value("id").toInt() == 6);
            QJsonArray content = resp.value("result").toObject().value("content").toArray();
            QJsonObject data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
            assert(data.value("connectedCount").toInt() == 2);
            assert(data.value("callingAgentName").toString() == "antigravity");
            QJsonArray agents = data.value("agents").toArray();
            assert(agents.size() == 2);
            std::cout << "✓ Multi-Agent Awareness: Antigravity discovered Claude in session." << std::endl;
        }

        client2.close();
    }

    // 9. Test "resources/list" and "resources/read" for ensemble://agents
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 7;
        req["method"] = "resources/list";

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 7);
        QJsonArray resources = resp.value("result").toObject().value("resources").toArray();
        assert(resources.size() >= 4);

        QStringList uris;
        for (const auto &r : resources) {
            uris << r.toObject().value("uri").toString();
        }
        assert(uris.contains("ensemble://project"));
        assert(uris.contains("ensemble://workskin/css"));
        assert(uris.contains("ensemble://problems"));
        assert(uris.contains("ensemble://agents"));
        assert(uris.contains("ensemble://agentic/index"));
        assert(uris.contains("ensemble://agentic/history"));
        std::cout << "✓ MCP 'resources/list' verified (including ensemble://agentic/index & history)." << std::endl;

        // Read ensemble://agents
        QJsonObject readReq;
        readReq["jsonrpc"] = "2.0";
        readReq["id"] = 8;
        readReq["method"] = "resources/read";
        QJsonObject readParams;
        readParams["uri"] = "ensemble://agents";
        readReq["params"] = readParams;

        QJsonObject readResp = sendAndReceive(readReq);
        assert(readResp.value("id").toInt() == 8);
        QJsonArray contents = readResp.value("result").toObject().value("contents").toArray();
        assert(!contents.isEmpty());
        assert(contents.first().toObject().value("mimeType").toString() == "application/json");
        std::cout << "✓ MCP 'resources/read' ensemble://agents verified." << std::endl;

        // Read ensemble://agentic/index
        readReq["id"] = 9;
        readParams["uri"] = "ensemble://agentic/index";
        readReq["params"] = readParams;
        readResp = sendAndReceive(readReq);
        assert(readResp.value("id").toInt() == 9);
        contents = readResp.value("result").toObject().value("contents").toArray();
        assert(!contents.isEmpty());
        assert(contents.first().toObject().value("mimeType").toString() == "application/json");
        std::cout << "✓ MCP 'resources/read' ensemble://agentic/index verified." << std::endl;
    }

    // 10. Test write_agentic_file (shared scope and private localized agent scope)
    {
        // 10a. Write shared memory file
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 20;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "write_agentic_file";
        QJsonObject args;
        args["scope"] = "shared";
        args["path"] = "shared_notes.txt";
        args["content"] = "This is collaborative memory shared between AI models.";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 20);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        std::cout << "✓ MCP 'write_agentic_file' (shared) verified." << std::endl;

        // 10b. Write private localized agent file
        req["id"] = 21;
        args["scope"] = "private";
        args["agent"] = "antigravity";
        args["path"] = "scratchpad.json";
        args["content"] = "{\"plan\": [\"inspect chapter 1\", \"check tags\"]}";
        params["arguments"] = args;
        req["params"] = params;

        resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 21);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        std::cout << "✓ MCP 'write_agentic_file' (private antigravity) verified." << std::endl;

        // 10c. Write agentic file without specifying scope (MUST default to 'shared'!)
        req["id"] = 210;
        QJsonObject noScopeArgs;
        noScopeArgs["path"] = "default_scope_test.txt";
        noScopeArgs["content"] = "Omitting scope must default to shared storage across all agents.";
        params["arguments"] = noScopeArgs;
        req["params"] = params;

        resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 210);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        QJsonArray wrContent = resp.value("result").toObject().value("content").toArray();
        QJsonObject wrData = QJsonDocument::fromJson(wrContent.first().toObject().value("text").toString().toUtf8()).object();
        assert(wrData.value("scope").toString() == "shared");
        std::cout << "✓ MCP 'write_agentic_file' omitted scope defaults to 'shared' verified." << std::endl;
    }

    // 11. Test read_agentic_file
    {
        // Read shared file
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 22;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "read_agentic_file";
        QJsonObject args;
        args["scope"] = "shared";
        args["path"] = "shared_notes.txt";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 22);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        QJsonArray content = resp.value("result").toObject().value("content").toArray();
        QJsonObject data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("content").toString().contains("collaborative memory"));
        std::cout << "✓ MCP 'read_agentic_file' (shared) verified." << std::endl;

        // Read file written without scope (defaults to shared)
        req["id"] = 220;
        args["path"] = "default_scope_test.txt";
        params["arguments"] = args;
        req["params"] = params;
        resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 220);
        assert(resp.value("result").toObject().value("isError").toBool() == false);

        // Read private file
        req["id"] = 23;
        args["scope"] = "private";
        args["agent"] = "antigravity";
        args["path"] = "scratchpad.json";
        params["arguments"] = args;
        req["params"] = params;

        resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 23);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        content = resp.value("result").toObject().value("content").toArray();
        data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("content").toString().contains("inspect chapter 1"));
        std::cout << "✓ MCP 'read_agentic_file' (private antigravity) verified." << std::endl;

        // Read special root agentic files (agent_model_history.xml & index.json)
        req["id"] = 231;
        QJsonObject readHistArgs;
        readHistArgs["path"] = "agent_model_history.xml";
        params["arguments"] = readHistArgs;
        req["params"] = params;
        resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 231);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        content = resp.value("result").toObject().value("content").toArray();
        data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("content").toString().contains("agent-model-history"));

        req["id"] = 232;
        readHistArgs["path"] = "index.json";
        params["arguments"] = readHistArgs;
        req["params"] = params;
        resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 232);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        std::cout << "✓ MCP 'read_agentic_file' special root files (history XML & index JSON) verified." << std::endl;
    }

    // 12. Test append_agent_history
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 24;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "append_agent_history";
        QJsonObject args;
        args["agentName"] = "antigravity";
        args["model"] = "gemini-2.5-pro";
        args["action"] = "StylingAudit";
        args["summary"] = "Validated workskin responsive grid classes";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 24);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        std::cout << "✓ MCP 'append_agent_history' verified." << std::endl;
    }

    // 13. Test post_agent_thought (AI reasoning and input reflection monitor)
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 25;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "post_agent_thought";
        QJsonObject args;
        args["thought"] = "Analyzing potential margin collapse between chapter title and work skin CSS banner.";
        args["agentName"] = "antigravity";
        QJsonArray tags;
        tags.append("css");
        tags.append("layout");
        args["tags"] = tags;
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 25);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        std::cout << "✓ MCP 'post_agent_thought' verified." << std::endl;
    }

    // 14. Test get_agentic_summary
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 26;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "get_agentic_summary";
        params["arguments"] = QJsonObject();
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 26);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        QJsonArray content = resp.value("result").toObject().value("content").toArray();
        QJsonObject data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("sharedBytes").toInt() > 0);
        assert(data.value("agentUsage").toObject().contains("antigravity"));
        assert(data.contains("lastChangesByModel"));
        std::cout << "✓ MCP 'get_agentic_summary' verified." << std::endl;
    }

    // 15. Test get_agent_history (Retrieving audit trail and model changes)
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 27;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "get_agent_history";
        QJsonObject args;
        args["limit"] = 50;
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 27);
        assert(resp.value("result").toObject().value("isError").toBool() == false);
        QJsonArray content = resp.value("result").toObject().value("content").toArray();
        QJsonObject data = QJsonDocument::fromJson(content.first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("returnedCount").toInt() > 0);
        assert(data.contains("lastChangesByModel"));
        QJsonObject lastChanges = data.value("lastChangesByModel").toObject();
        assert(lastChanges.contains("antigravity"));
        assert(!lastChanges.value("antigravity").toObject().value("action").toString().isEmpty());
        std::cout << "✓ MCP 'get_agent_history' verified with " << data.value("returnedCount").toInt()
                  << " entries and model changes tracking." << std::endl;
    }

    // 16. Verify McpServer activity logging and AgentThoughtsDialog integration
    {
        QList<AgentActivityEvent> acts = server->recentActivities();
        assert(!acts.isEmpty());

        bool foundThought = false;
        bool foundToolCall = false;
        for (const auto &act : acts) {
            if (act.type == AgentActivityEvent::Type::Thought) {
                foundThought = true;
                assert(act.agentName == "antigravity");
                assert(act.thought.contains("margin collapse"));
            }
            if (act.title.contains("write_agentic_file")) {
                foundToolCall = true;
                assert(act.type == AgentActivityEvent::Type::DataModification);
                assert(!act.inputData.isEmpty()); // Verified input arguments captured!
            }
        }
        assert(foundThought);
        assert(foundToolCall);
        std::cout << "✓ McpServer recorded agent activities with input parameters and model thoughts." << std::endl;

        // Instantiate AgentThoughtsDialog with server and verify UI initialization & model history integration
        AgentThoughtsDialog thoughtsDialog(server);
        thoughtsDialog.setProject(&mainWindow.project());
        thoughtsDialog.updateTheme();
        thoughtsDialog.refreshModelHistory();
        thoughtsDialog.show();
        assert(thoughtsDialog.windowTitle().contains("Monitor"));

        // Verify tree widget columns and Date & Time formatting
        QList<QTreeWidget *> trees = thoughtsDialog.findChildren<QTreeWidget *>();
        assert(trees.size() >= 2);
        QTreeWidget *eventTree = nullptr;
        for (auto *tree : trees) {
            if (tree->headerItem()->text(1) == "Agent") {
                eventTree = tree;
                break;
            }
        }
        assert(eventTree != nullptr);
        assert(eventTree->headerItem()->text(0) == "Date & Time");
        assert(eventTree->topLevelItemCount() > 0);

        // Verify that topLevelItem(0) date & time contains yyyy-MM-dd HH:mm:ss format
        QString dateSample = eventTree->topLevelItem(0)->text(0);
        assert(dateSample.length() == 19);
        assert(dateSample.at(4) == QLatin1Char('-') && dateSample.at(7) == QLatin1Char('-'));
        assert(dateSample.at(10) == QLatin1Char(' ') && dateSample.at(13) == QLatin1Char(':'));

        const int initialCount = eventTree->topLevelItemCount();

        // Verify deduplication: calling setMcpServer again must NOT duplicate events
        thoughtsDialog.setMcpServer(server);
        assert(eventTree->topLevelItemCount() == initialCount);

        // Verify out-of-order timestamp insertion: older events (e.g. 04:28) must never appear after newer events (04:33)
        AgentActivityEvent evNew;
        evNew.id = "test-event-new-433";
        evNew.timestamp = QDateTime::fromString("2026-10-12T04:33:00Z", Qt::ISODate);
        evNew.agentName = "claude";
        evNew.role = "reviewer";
        evNew.type = AgentActivityEvent::Type::DataInspection;
        evNew.title = "Read Project Data: get_project_summary";
        evNew.thought = "Analyzing latest state";
        thoughtsDialog.addActivityEvent(evNew);

        AgentActivityEvent evOld;
        evOld.id = "test-event-old-428";
        evOld.timestamp = QDateTime::fromString("2026-10-12T04:28:00Z", Qt::ISODate);
        evOld.agentName = "claude-ai";
        evOld.role = "co-author";
        evOld.type = AgentActivityEvent::Type::DataInspection;
        evOld.title = "Read Project Data: get_agent_presence";
        evOld.thought = "Initial connection check";
        thoughtsDialog.addActivityEvent(evOld);

        // Verify evOld (04:28) is placed before evNew (04:33) in the tree
        int idxOld = -1;
        int idxNew = -1;
        for (int i = 0; i < eventTree->topLevelItemCount(); ++i) {
            QString id = eventTree->topLevelItem(i)->data(0, Qt::UserRole).toString();
            if (id == "test-event-old-428") {
                idxOld = i;
            }
            if (id == "test-event-new-433") {
                idxNew = i;
            }
        }
        assert(idxOld >= 0 && idxNew >= 0);
        assert(idxOld < idxNew); // Chronological sorting verified!

        // Verify deduplication when adding same event again
        const int countBeforeDupe = eventTree->topLevelItemCount();
        thoughtsDialog.addActivityEvent(evOld);
        assert(eventTree->topLevelItemCount() == countBeforeDupe);

        thoughtsDialog.close();
        std::cout << "✓ AgentThoughtsDialog verified: Date & Time formatting, chronological sorting, and deduplication." << std::endl;
    }

    // 21. Test "tools/call" -> get_lore_graph
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 40;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "get_lore_graph";
        params["arguments"] = QJsonObject{{"name", "data"}, {"includeGrammar", true}};
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 40);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject content = result.value("content").toArray().first().toObject();
        QJsonObject parsed = QJsonDocument::fromJson(content.value("text").toString().toUtf8()).object();
        assert(parsed.value("magicHeaderValid").toBool() == true);
        assert(parsed.value("magicHeaderHex").toString() == "89 47 52 50 48 01 0D 0A");
        assert(!parsed.value("rawDsl").toString().isEmpty());
        assert(!parsed.value("grammarSpecification").toString().isEmpty());
        std::cout << "✓ MCP 'get_lore_graph' verified (.grph DSL, unique magic bytes, and grammar guide returned)." << std::endl;
    }

    // 22. Test "tools/call" -> add_lore_node
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 41;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "add_lore_node";
        QJsonObject args;
        args["id"] = "mcp_climax";
        args["label"] = "Climax Battle";
        args["type"] = "plot_point";
        args["x"] = 800.0;
        args["y"] = 350.0;
        args["shape"] = "diamond";
        args["color"] = "#ef4444";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 41);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        std::cout << "✓ MCP 'add_lore_node' verified (node 'mcp_climax' added)." << std::endl;
    }

    // 23. Test "tools/call" -> connect_lore_nodes
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 42;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "connect_lore_nodes";
        QJsonObject args;
        args["fromNodeId"] = "inciting_incident";
        args["toNodeId"] = "mcp_climax";
        args["arrow"] = "->";
        args["label"] = "leads directly to";
        args["style"] = "solid";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 42);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        std::cout << "✓ MCP 'connect_lore_nodes' verified ('inciting_incident' -> 'mcp_climax')." << std::endl;
    }

    // 24. Test "tools/call" -> move_lore_node
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 43;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "move_lore_node";
        QJsonObject args;
        args["nodeId"] = "mcp_climax";
        args["x"] = 850.0;
        args["y"] = 400.0;
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 43);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        std::cout << "✓ MCP 'move_lore_node' verified (moved to 850, 400)." << std::endl;
    }

    // 25. Test "tools/call" -> manage_lore_data & get_lore_data
    {
        // Add entity
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 44;
            req["method"] = "tools/call";
            QJsonObject params;
            params["name"] = "manage_lore_data";
            QJsonObject args;
            args["action"] = "add_entity";
            args["id"] = "shadow_emperor";
            args["name"] = "The Shadow Emperor";
            args["category"] = "Character";
            args["description"] = "Ruler of the veiled dynasty.";
            args["tags"] = QJsonArray{QStringLiteral("villain"), QStringLiteral("magic")};
            params["arguments"] = args;
            req["params"] = params;

            QJsonObject resp = sendAndReceive(req);
            assert(resp.value("id").toInt() == 44);
            assert(!resp.value("result").toObject().value("isError").toBool());
        }

        // Add conflict
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 45;
            req["method"] = "tools/call";
            QJsonObject params;
            params["name"] = "manage_lore_data";
            QJsonObject args;
            args["action"] = "add_conflict";
            args["id"] = "throne_succession";
            args["title"] = "Succession Timeline Discrepancy";
            args["chapterA"] = 1;
            args["chapterB"] = 3;
            args["description"] = "Chapter 1 claims Emperor died in Year 50, Chapter 3 mentions him active in Year 55.";
            params["arguments"] = args;
            req["params"] = params;

            QJsonObject resp = sendAndReceive(req);
            assert(resp.value("id").toInt() == 45);
            assert(!resp.value("result").toObject().value("isError").toBool());
        }

        // Resolve conflict
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 46;
            req["method"] = "tools/call";
            QJsonObject params;
            params["name"] = "manage_lore_data";
            QJsonObject args;
            args["action"] = "resolve_conflict";
            args["conflictId"] = "throne_succession";
            args["resolution"] = "The Emperor feigned his death in Year 50 and operated covertly until Year 55.";
            args["canonRuling"] = "Covert survival canon confirmed";
            params["arguments"] = args;
            req["params"] = params;

            QJsonObject resp = sendAndReceive(req);
            assert(resp.value("id").toInt() == 46);
            assert(!resp.value("result").toObject().value("isError").toBool());
        }

        // Verify get_lore_data
        {
            QJsonObject req;
            req["jsonrpc"] = "2.0";
            req["id"] = 47;
            req["method"] = "tools/call";
            QJsonObject params;
            params["name"] = "get_lore_data";
            params["arguments"] = QJsonObject{{"section", "all"}};
            req["params"] = params;

            QJsonObject resp = sendAndReceive(req);
            assert(resp.value("id").toInt() == 47);
            QJsonObject result = resp.value("result").toObject();
            assert(!result.value("isError").toBool());
            QJsonObject content = result.value("content").toArray().first().toObject();
            QJsonObject loreObj = QJsonDocument::fromJson(content.value("text").toString().toUtf8()).object();
            assert(loreObj.value("entities").toArray().size() >= 1);
            assert(loreObj.value("resolvedConflicts").toArray().size() >= 1);
            assert(loreObj.value("manifestFiles").toArray().size() == 8);
            std::cout << "✓ MCP 'manage_lore_data' & 'get_lore_data' verified (entities, conflicts, resolutions, all 7 files + .grph)." << std::endl;
        }
    }

    // 26. Test "tools/call" -> update_project_metadata
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 48;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "update_project_metadata";
        QJsonObject args;
        args["rating"] = "Mature";
        args["archiveWarnings"] = QJsonArray{QStringLiteral("No Archive Warnings Apply")};
        args["categories"] = QJsonArray{QStringLiteral("M/M"), QStringLiteral("Multi")};
        args["fandoms"] = QJsonArray{QStringLiteral("Original Lore Fandom")};
        args["relationships"] = QJsonArray{QStringLiteral("Hero/Mentor")};
        args["characters"] = QJsonArray{QStringLiteral("Hero"), QStringLiteral("Mentor")};
        args["tags"] = QJsonArray{QStringLiteral("Slow Burn"), QStringLiteral("Angst with a Happy Ending")};
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 48);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());

        // Verify with get_project_summary
        QJsonObject sumReq;
        sumReq["jsonrpc"] = "2.0";
        sumReq["id"] = 49;
        sumReq["method"] = "tools/call";
        QJsonObject sumParams;
        sumParams["name"] = "get_project_summary";
        sumParams["arguments"] = QJsonObject();
        sumReq["params"] = sumParams;

        QJsonObject sumResp = sendAndReceive(sumReq);
        assert(sumResp.value("id").toInt() == 49);
        QJsonObject sumContent = sumResp.value("result").toObject().value("content").toArray().first().toObject();
        QJsonObject sumData = QJsonDocument::fromJson(sumContent.value("text").toString().toUtf8()).object();
        assert(sumData.value("rating").toString() == "Mature");
        assert(sumData.value("tags").toArray().contains(QJsonValue("Slow Burn")));
        assert(sumData.value("fandoms").toArray().contains(QJsonValue("Original Lore Fandom")));
        assert(sumData.contains("loreSummary"));
        assert(sumData.value("loreSummary").toObject().value("entityCount").toInt() >= 1);
        std::cout << "✓ MCP 'update_project_metadata' verified with AO3 tags and enriched 'get_project_summary'." << std::endl;
    }

    // 31. Test add_history_commit (git-like history.cmt)
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 50;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "add_history_commit";
        QJsonObject args;
        args["author"] = "Miku Agent";
        args["message"] = "feat: added new lore nodes and updated relationship tags";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 50);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject cData = QJsonDocument::fromJson(result.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(cData.value("success").toBool() == true);
        assert(!cData.value("hash").toString().isEmpty());
        assert(cData.value("author").toString() == "Miku Agent");
        assert(cData.value("message").toString() == "feat: added new lore nodes and updated relationship tags");
        assert(cData.contains("previousCommitHash"));
        assert(cData.contains("localTimestamp"));
        std::cout << "✓ MCP 'add_history_commit' verified with snapshot hash, previousCommitHash, and author." << std::endl;
    }

    // 31b. Test get_snapshot_diff and safe failure on missing diff
    {
        // First get latest commit hash
        const QString latestHash = mainWindow.project().latestCommitHash();
        assert(!latestHash.isEmpty());

        QJsonObject diffReq;
        diffReq["jsonrpc"] = "2.0";
        diffReq["id"] = 150;
        diffReq["method"] = "tools/call";
        diffReq["params"] = QJsonObject{
            {"name", "get_snapshot_diff"},
            {"arguments", QJsonObject{{"hash", latestHash}}}
        };
        QJsonObject diffResp = sendAndReceive(diffReq);
        assert(diffResp.value("id").toInt() == 150);
        QJsonObject diffRes = diffResp.value("result").toObject();
        assert(!diffRes.value("isError").toBool());
        QJsonObject diffData = QJsonDocument::fromJson(diffRes.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(diffData.value("success").toBool() == true);
        assert(diffData.value("hash").toString() == latestHash);
        assert(!diffData.value("diff").toString().isEmpty());
        std::cout << "✓ MCP 'get_snapshot_diff' successfully returned line-by-line diff." << std::endl;

        // Test safe failure on nonexistent / legacy commit hash
        QJsonObject failReq;
        failReq["jsonrpc"] = "2.0";
        failReq["id"] = 151;
        failReq["method"] = "tools/call";
        failReq["params"] = QJsonObject{
            {"name", "get_snapshot_diff"},
            {"arguments", QJsonObject{{"hash", "legacy_hash_00"}}}
        };
        QJsonObject failResp = sendAndReceive(failReq);
        assert(failResp.value("id").toInt() == 151);
        QJsonObject failRes = failResp.value("result").toObject();
        assert(failRes.value("isError").toBool());
        const QString failMsg = failRes.value("content").toArray().first().toObject().value("text").toString();
        assert(failMsg.contains("deleted by an external software") || failMsg.contains("created before snapshots were introduced"));
        std::cout << "✓ MCP 'get_snapshot_diff' safe failure fallback verified for legacy/missing diffs." << std::endl;
    }

    // 32. Test get_project_history
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 51;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "get_project_history";
        QJsonObject args;
        args["limit"] = 10;
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 51);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject hData = QJsonDocument::fromJson(result.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(hData.value("totalCommits").toInt() >= 1);
        assert(!hData.value("latestCommitHash").toString().isEmpty());
        QJsonArray commits = hData.value("commits").toArray();
        assert(commits.size() >= 1);
        QJsonObject firstCommit = commits.first().toObject();
        assert(!firstCommit.value("hash").toString().isEmpty());
        assert(firstCommit.contains("previousCommitHash"));
        assert(firstCommit.contains("localDate"));
        assert(firstCommit.value("diffAvailable").toBool() == true);
        assert(firstCommit.value("message").toString().contains("feat: added new lore nodes"));
        assert(hData.value("rawHistory").toString().contains("commit "));
        std::cout << "✓ MCP 'get_project_history' parsed commits array, parent hashes, localDate, and raw history.cmt verified." << std::endl;
    }

    // 33. Test save_project with filePath and commitMessage
    const QString mcpSavePath = QDir::tempPath() + QStringLiteral("/mcp_test_story_%1.ao3proj").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 52;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "save_project";
        QJsonObject args;
        args["filePath"] = mcpSavePath;
        args["commitMessage"] = "chore: saved project archive via MCP loopback";
        args["author"] = "Miku Agent";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 52);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        assert(QFile::exists(mcpSavePath));
        std::cout << "✓ MCP 'save_project' with commitMessage and target path verified." << std::endl;
    }

    // 34. Test load_project
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 53;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "load_project";
        QJsonObject args;
        args["filePath"] = mcpSavePath;
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 53);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject lData = QJsonDocument::fromJson(result.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(lData.value("success").toBool() == true);
        assert(lData.value("filePath").toString() == mcpSavePath);
        assert(lData.value("rating").toString() == "Mature");
        assert(lData.value("commitCount").toInt() >= 2);
        std::cout << "✓ MCP 'load_project' successfully loaded saved project with metadata and commit history." << std::endl;

        QFile::remove(mcpSavePath);
    }

    // 35. Test insert_image_embed
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 54;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "insert_image_embed";
        QJsonObject args;
        args["chapterIndex"] = 0;
        args["url"] = "https://images.example.com/illustration_scene.png";
        args["alt"] = "Hero confronting dragon";
        args["title"] = "Chapter 1 Climax";
        args["width"] = 750;
        args["height"] = 500;
        args["alignment"] = "center";
        args["wrapInLink"] = true;
        args["position"] = "append";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 54);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject data = QJsonDocument::fromJson(result.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("success").toBool() == true);
        assert(data.value("url").toString() == "https://images.example.com/illustration_scene.png");
        assert(data.value("embeddedHtml").toString().contains("<img src=\"https://images.example.com/illustration_scene.png\""));
        assert(data.value("embeddedHtml").toString().contains("alt=\"Hero confronting dragon\""));
        assert(data.value("embeddedHtml").toString().contains("<p align=\"center\">"));
        std::cout << "✓ MCP 'insert_image_embed' inserted image URL attachment successfully." << std::endl;
    }

    // 36. Test list_image_embeds
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 55;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "list_image_embeds";
        QJsonObject args;
        args["chapterIndex"] = 0;
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 55);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject data = QJsonDocument::fromJson(result.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("totalImageEmbeds").toInt() >= 1);
        QJsonArray embeds = data.value("imageEmbeds").toArray();
        assert(embeds.size() >= 1);
        assert(embeds.first().toObject().value("url").toString() == "https://images.example.com/illustration_scene.png");
        assert(embeds.first().toObject().value("alt").toString() == "Hero confronting dragon");
        std::cout << "✓ MCP 'list_image_embeds' returned active project image embeds." << std::endl;
    }

    // 37. Test update_lore_graph rejects malformed DSL and preserves existing graph
    {
        // First get current lore graph to know baseline state
        QJsonObject getReq;
        getReq["jsonrpc"] = "2.0";
        getReq["id"] = 56;
        getReq["method"] = "tools/call";
        getReq["params"] = QJsonObject{{"name", "get_lore_graph"}, {"arguments", QJsonObject{{"name", "data"}}}};
        QJsonObject getResp = sendAndReceive(getReq);
        assert(getResp.value("id").toInt() == 56);
        QJsonObject getResult = getResp.value("result").toObject();
        assert(!getResult.value("isError").toBool());
        QJsonObject prevData = QJsonDocument::fromJson(getResult.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        const QString prevTitle = prevData.value("title").toString();
        const int prevNodeCount = prevData.value("nodeCount").toInt();

        // Send deliberately malformed DSL
        QJsonObject badReq;
        badReq["jsonrpc"] = "2.0";
        badReq["id"] = 57;
        badReq["method"] = "tools/call";
        QJsonObject badParams;
        badParams["name"] = "update_lore_graph";
        QJsonObject badArgs;
        badArgs["name"] = "data";
        badArgs["dsl"] = "graph \"Broken\" [direction=SIDEWAYS]\nTHIS_IS_NOT_VALID_DSL_SYNTAX\n";
        badParams["arguments"] = badArgs;
        badReq["params"] = badParams;

        QJsonObject badResp = sendAndReceive(badReq);
        assert(badResp.value("id").toInt() == 57);
        QJsonObject badResult = badResp.value("result").toObject();
        assert(badResult.value("isError").toBool() == true);

        // Verify the existing graph was preserved and NOT wiped out
        QJsonObject checkReq;
        checkReq["jsonrpc"] = "2.0";
        checkReq["id"] = 58;
        checkReq["method"] = "tools/call";
        checkReq["params"] = QJsonObject{{"name", "get_lore_graph"}, {"arguments", QJsonObject{{"name", "data"}}}};
        QJsonObject checkResp = sendAndReceive(checkReq);
        assert(checkResp.value("id").toInt() == 58);
        QJsonObject checkData = QJsonDocument::fromJson(checkResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(checkData.value("title").toString() == prevTitle);
        assert(checkData.value("nodeCount").toInt() == prevNodeCount);
        std::cout << "✓ MCP 'update_lore_graph' rejects malformed DSL and preserves previous graph without data loss." << std::endl;
    }

    // 38. Test update_lore_graph with valid DSL containing notes and connectors
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 59;
        req["method"] = "tools/call";
        QJsonObject params;
        params["name"] = "update_lore_graph";
        QJsonObject args;
        args["name"] = "data";
        args["dsl"] =
            "graph \"Valid Map\" [direction=LR]\n"
            "node hero [label=\"Hero\", type=\"character\", pos(100, 100), size(120, 60), color=\"#3b82f6\", shape=box, notes=\"Protagonist note\"]\n"
            "node rival [label=\"Rival\", type=\"character\", pos(400, 100), size(120, 60), color=\"#ef4444\", shape=box, notes=\"Antagonist note\"]\n"
            "hero -> rival [id=\"conn_hero_rival\", style=dashed, color=\"#94a3b8\", label=\"rivalry\"]\n";
        params["arguments"] = args;
        req["params"] = params;

        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 59);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());

        // Verify with get_lore_graph
        QJsonObject getReq;
        getReq["jsonrpc"] = "2.0";
        getReq["id"] = 60;
        getReq["method"] = "tools/call";
        getReq["params"] = QJsonObject{{"name", "get_lore_graph"}, {"arguments", QJsonObject{{"name", "data"}}}};
        QJsonObject getResp = sendAndReceive(getReq);
        QJsonObject data = QJsonDocument::fromJson(getResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("nodeCount").toInt() == 2);
        assert(data.value("connectorCount").toInt() == 1);
        assert(data.value("rawDsl").toString().contains("notes=\"Protagonist note\""));
        assert(data.value("rawDsl").toString().contains("id=\"conn_hero_rival\""));
        std::cout << "✓ MCP 'update_lore_graph' verified with valid DSL, notes=, and stable connector IDs." << std::endl;
    }

    // 39. Test manage_lore_data: nonexistent entity error, field aliases (description->summary, etc.), and deletions
    {
        // 1. remove_entity on nonexistent ID fails
        QJsonObject remBadReq;
        remBadReq["jsonrpc"] = "2.0";
        remBadReq["id"] = 61;
        remBadReq["method"] = "tools/call";
        QJsonObject remBadParams;
        remBadParams["name"] = "manage_lore_data";
        remBadParams["arguments"] = QJsonObject{{"action", "remove_entity"}, {"id", "absolutely_nonexistent_entity_id"}};
        remBadReq["params"] = remBadParams;
        QJsonObject remBadResp = sendAndReceive(remBadReq);
        assert(remBadResp.value("id").toInt() == 61);
        assert(remBadResp.value("result").toObject().value("isError").toBool() == true);

        // 2. add_history with 'description' fallback into 'summary'
        QJsonObject histReq;
        histReq["jsonrpc"] = "2.0";
        histReq["id"] = 62;
        histReq["method"] = "tools/call";
        QJsonObject histParams;
        histParams["name"] = "manage_lore_data";
        histParams["arguments"] = QJsonObject{
            {"action", "add_history"},
            {"id", "coronation_event"},
            {"title", "Coronation Day"},
            {"description", "The prince was crowned king amidst festivities."},
            {"impact", "Shifted political power"}
        };
        histReq["params"] = histParams;
        QJsonObject histResp = sendAndReceive(histReq);
        assert(histResp.value("id").toInt() == 62);
        assert(!histResp.value("result").toObject().value("isError").toBool());

        // Verify history was saved with summary
        QJsonObject getLoreReq;
        getLoreReq["jsonrpc"] = "2.0";
        getLoreReq["id"] = 63;
        getLoreReq["method"] = "tools/call";
        getLoreReq["params"] = QJsonObject{{"name", "get_lore_data"}, {"arguments", QJsonObject{{"section", "history"}}}};
        QJsonObject getLoreResp = sendAndReceive(getLoreReq);
        QJsonObject loreData = QJsonDocument::fromJson(getLoreResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        QJsonArray hArr = loreData.value("historyEvents").toArray();
        bool foundHist = false;
        for (const auto &v : hArr) {
            QJsonObject hObj = v.toObject();
            if (hObj.value("id").toString() == "coronation_event") {
                assert(hObj.value("summary").toString() == "The prince was crowned king amidst festivities.");
                foundHist = true;
                break;
            }
        }
        assert(foundHist);

        // 3. remove_history
        QJsonObject remHistReq;
        remHistReq["jsonrpc"] = "2.0";
        remHistReq["id"] = 64;
        remHistReq["method"] = "tools/call";
        remHistReq["params"] = QJsonObject{{"name", "manage_lore_data"}, {"arguments", QJsonObject{{"action", "remove_history"}, {"id", "coronation_event"}}}};
        QJsonObject remHistResp = sendAndReceive(remHistReq);
        assert(remHistResp.value("id").toInt() == 64);
        assert(!remHistResp.value("result").toObject().value("isError").toBool());

        // 4. add_reference with description fallback into notes, then remove_reference
        QJsonObject refReq;
        refReq["jsonrpc"] = "2.0";
        refReq["id"] = 65;
        refReq["method"] = "tools/call";
        refReq["params"] = QJsonObject{
            {"name", "manage_lore_data"},
            {"arguments", QJsonObject{
                {"action", "add_reference"},
                {"id", "canon_ref_1"},
                {"title", "Canon Timeline Reference"},
                {"source", "Author Blog"},
                {"citation", "Post #12"},
                {"description", "Timeline clarification by the original author."}
            }}
        };
        QJsonObject refResp = sendAndReceive(refReq);
        assert(refResp.value("id").toInt() == 65);
        assert(!refResp.value("result").toObject().value("isError").toBool());

        QJsonObject remRefReq;
        remRefReq["jsonrpc"] = "2.0";
        remRefReq["id"] = 66;
        remRefReq["method"] = "tools/call";
        remRefReq["params"] = QJsonObject{{"name", "manage_lore_data"}, {"arguments", QJsonObject{{"action", "remove_reference"}, {"id", "canon_ref_1"}}}};
        QJsonObject remRefResp = sendAndReceive(remRefReq);
        assert(remRefResp.value("id").toInt() == 66);
        assert(!remRefResp.value("result").toObject().value("isError").toBool());

        std::cout << "✓ MCP 'manage_lore_data' error on nonexistent ID, description alias mapping, and remove actions verified." << std::endl;
    }

    // 40. Test register_agent_presence with model and audit logging verification in get_agent_history
    {
        QJsonObject regReq;
        regReq["jsonrpc"] = "2.0";
        regReq["id"] = 67;
        regReq["method"] = "tools/call";
        regReq["params"] = QJsonObject{
            {"name", "register_agent_presence"},
            {"arguments", QJsonObject{
                {"agentName", "Claude"},
                {"model", "claude-3-5-sonnet"},
                {"role", "co-author"},
                {"workingNote", "Conducting verification audits"}
            }}
        };
        QJsonObject regResp = sendAndReceive(regReq);
        assert(regResp.value("id").toInt() == 67);
        QJsonObject regResult = regResp.value("result").toObject();
        assert(!regResult.value("isError").toBool());

        // Verify get_agent_presence returns model
        QJsonObject presReq;
        presReq["jsonrpc"] = "2.0";
        presReq["id"] = 68;
        presReq["method"] = "tools/call";
        presReq["params"] = QJsonObject{{"name", "get_agent_presence"}, {"arguments", QJsonObject{}}};
        QJsonObject presResp = sendAndReceive(presReq);
        QJsonObject presData = QJsonDocument::fromJson(presResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(presData.value("callingAgentModel").toString() == "claude-3-5-sonnet");

        // Verify get_agent_history contains recorded lore operations, failed calls, and image embeds
        QJsonObject histReq;
        histReq["jsonrpc"] = "2.0";
        histReq["id"] = 69;
        histReq["method"] = "tools/call";
        histReq["params"] = QJsonObject{{"name", "get_agent_history"}, {"arguments", QJsonObject{{"limit", 100}}}};
        QJsonObject histResp = sendAndReceive(histReq);
        assert(histResp.value("id").toInt() == 69);
        QJsonObject histData = QJsonDocument::fromJson(histResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        QJsonArray entries = histData.value("entries").toArray();
        assert(!entries.isEmpty());

        bool foundLoreNode = false;
        bool foundFailedCall = false;
        bool foundImageEmbed = false;
        for (const auto &v : entries) {
            QJsonObject entry = v.toObject();
            const QString act = entry.value("action").toString();
            if (act == "add_lore_node" || act == "move_lore_node" || act == "connect_lore_nodes") {
                foundLoreNode = true;
            }
            if (act.contains("(failed)")) {
                foundFailedCall = true;
            }
            if (act == "insert_image_embed") {
                foundImageEmbed = true;
            }
        }
        assert(foundLoreNode);
        assert(foundFailedCall);
        assert(foundImageEmbed);
        std::cout << "✓ MCP 'get_agent_history' verified with lore operations, failed tool calls, and model registration." << std::endl;
    }

    // 41. Test Ao3HtmlSanitizer completely strips <script> and <style> tags and their inner text
    {
        const QString dirtyHtml = QStringLiteral(
            "<p>Normal paragraph</p>\n"
            "<script type=\"text/javascript\">alert('xss'); x();</script>\n"
            "<style>body { background: red; }</style>\n"
            "<p>Safe text continues</p>"
        );
        const QString cleanHtml = Ao3HtmlSanitizer::sanitize(dirtyHtml);
        assert(!cleanHtml.contains("<script"));
        assert(!cleanHtml.contains("</script>"));
        assert(!cleanHtml.contains("alert('xss')"));
        assert(!cleanHtml.contains("x()"));
        assert(!cleanHtml.contains("<style"));
        assert(!cleanHtml.contains("background: red"));
        assert(cleanHtml.contains("<p>Normal paragraph</p>"));
        assert(cleanHtml.contains("<p>Safe text continues</p>"));
        std::cout << "✓ Ao3HtmlSanitizer completely strips dangerous blocks (<script>, <style>) including all inner text." << std::endl;
    }

    // 42. Test set_workskin_css validation & get_problems diagnostic reporting
    {
        // A. Reject malformed CSS by default
        QJsonObject badCssReq;
        badCssReq["jsonrpc"] = "2.0";
        badCssReq["id"] = 70;
        badCssReq["method"] = "tools/call";
        badCssReq["params"] = QJsonObject{
            {"name", "set_workskin_css"},
            {"arguments", QJsonObject{{"css", "#workskin { color: red;"}}}
        };
        QJsonObject badCssResp = sendAndReceive(badCssReq);
        assert(badCssResp.value("id").toInt() == 70);
        QJsonObject badRes = badCssResp.value("result").toObject();
        assert(badRes.value("isError").toBool());
        QString errText = badRes.value("content").toArray().first().toObject().value("text").toString();
        assert(errText.contains("CSS syntax validation failed"));

        // B. Allow malformed CSS when allowMalformed is true
        QJsonObject allowBadReq;
        allowBadReq["jsonrpc"] = "2.0";
        allowBadReq["id"] = 71;
        allowBadReq["method"] = "tools/call";
        allowBadReq["params"] = QJsonObject{
            {"name", "set_workskin_css"},
            {"arguments", QJsonObject{
                {"css", "#workskin { color: red;"},
                {"allowMalformed", true}
            }}
        };
        QJsonObject allowBadResp = sendAndReceive(allowBadReq);
        assert(allowBadResp.value("id").toInt() == 71);
        QJsonObject allowRes = allowBadResp.value("result").toObject();
        assert(!allowRes.value("isError").toBool());

        // C. get_problems should now flag the CSS syntax error
        QJsonObject probReq;
        probReq["jsonrpc"] = "2.0";
        probReq["id"] = 72;
        probReq["method"] = "tools/call";
        probReq["params"] = QJsonObject{{"name", "get_problems"}, {"arguments", QJsonObject{}}};
        QJsonObject probResp = sendAndReceive(probReq);
        assert(probResp.value("id").toInt() == 72);
        QJsonObject probData = QJsonDocument::fromJson(probResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        QJsonArray problems = probData.value("problems").toArray();
        bool foundCssProblem = false;
        for (const auto &pVal : problems) {
            QJsonObject pObj = pVal.toObject();
            if (pObj.value("category").toString() == "Work Skin CSS" && pObj.value("severity").toString() == "Error") {
                foundCssProblem = true;
                break;
            }
        }
        assert(foundCssProblem);

        // D. Restore clean CSS and verify problems are cleared
        QJsonObject cleanCssReq;
        cleanCssReq["jsonrpc"] = "2.0";
        cleanCssReq["id"] = 73;
        cleanCssReq["method"] = "tools/call";
        cleanCssReq["params"] = QJsonObject{
            {"name", "set_workskin_css"},
            {"arguments", QJsonObject{{"css", "#workskin .system-alert { color: #d9534f; }"}}}
        };
        QJsonObject cleanResp = sendAndReceive(cleanCssReq);
        assert(!cleanResp.value("result").toObject().value("isError").toBool());

        std::cout << "✓ MCP 'set_workskin_css' validation & 'get_problems' diagnostic integration verified." << std::endl;
    }

    // 43. Test semantic validation for update_lore_graph, add_lore_node, and connect_lore_nodes
    {
        // A. Reject invalid node type in DSL
        QJsonObject badTypeReq;
        badTypeReq["jsonrpc"] = "2.0";
        badTypeReq["id"] = 74;
        badTypeReq["method"] = "tools/call";
        badTypeReq["params"] = QJsonObject{
            {"name", "update_lore_graph"},
            {"arguments", QJsonObject{
                {"name", "data"},
                {"dsl", "graph \"Test\"\ndirection LR\nnode c1 \"Hero\" type=spaceship\n"}
            }}
        };
        QJsonObject badTypeResp = sendAndReceive(badTypeReq);
        assert(badTypeResp.value("id").toInt() == 74);
        assert(badTypeResp.value("result").toObject().value("isError").toBool());

        // B. Reject invalid node shape in DSL
        QJsonObject badShapeReq;
        badShapeReq["jsonrpc"] = "2.0";
        badShapeReq["id"] = 75;
        badShapeReq["method"] = "tools/call";
        badShapeReq["params"] = QJsonObject{
            {"name", "update_lore_graph"},
            {"arguments", QJsonObject{
                {"name", "data"},
                {"dsl", "graph \"Test\"\ndirection LR\nnode c1 \"Hero\" shape=blob\n"}
            }}
        };
        QJsonObject badShapeResp = sendAndReceive(badShapeReq);
        assert(badShapeResp.value("id").toInt() == 75);
        assert(badShapeResp.value("result").toObject().value("isError").toBool());

        // C. Reject invalid color in DSL
        QJsonObject badColorReq;
        badColorReq["jsonrpc"] = "2.0";
        badColorReq["id"] = 76;
        badColorReq["method"] = "tools/call";
        badColorReq["params"] = QJsonObject{
            {"name", "update_lore_graph"},
            {"arguments", QJsonObject{
                {"name", "data"},
                {"dsl", "graph \"Test\"\ndirection LR\nnode c1 \"Hero\" color=\"notacolor\"\n"}
            }}
        };
        QJsonObject badColorResp = sendAndReceive(badColorReq);
        assert(badColorResp.value("id").toInt() == 76);
        assert(badColorResp.value("result").toObject().value("isError").toBool());

        // D. Reject connector to nonexistent node in DSL
        QJsonObject badConnReq;
        badConnReq["jsonrpc"] = "2.0";
        badConnReq["id"] = 77;
        badConnReq["method"] = "tools/call";
        badConnReq["params"] = QJsonObject{
            {"name", "update_lore_graph"},
            {"arguments", QJsonObject{
                {"name", "data"},
                {"dsl", "graph \"Test\"\ndirection LR\nnode c1 \"Hero\"\nc1 -> ghost [label=\"meets\"]\n"}
            }}
        };
        QJsonObject badConnResp = sendAndReceive(badConnReq);
        assert(badConnResp.value("id").toInt() == 77);
        assert(badConnResp.value("result").toObject().value("isError").toBool());

        // E. Reject add_lore_node with invalid shape or color
        QJsonObject badAddReq;
        badAddReq["jsonrpc"] = "2.0";
        badAddReq["id"] = 78;
        badAddReq["method"] = "tools/call";
        badAddReq["params"] = QJsonObject{
            {"name", "add_lore_node"},
            {"arguments", QJsonObject{
                {"id", "bad_node"},
                {"label", "Bad Node"},
                {"shape", "blob"}
            }}
        };
        QJsonObject badAddResp = sendAndReceive(badAddReq);
        assert(badAddResp.value("id").toInt() == 78);
        assert(badAddResp.value("result").toObject().value("isError").toBool());

        // F. Reject connect_lore_nodes with nonexistent node
        QJsonObject badConnectReq;
        badConnectReq["jsonrpc"] = "2.0";
        badConnectReq["id"] = 79;
        badConnectReq["method"] = "tools/call";
        badConnectReq["params"] = QJsonObject{
            {"name", "connect_lore_nodes"},
            {"arguments", QJsonObject{
                {"from", "nonexistent_a"},
                {"to", "nonexistent_b"}
            }}
        };
        QJsonObject badConnectResp = sendAndReceive(badConnectReq);
        assert(badConnectResp.value("id").toInt() == 79);
        assert(badConnectResp.value("result").toObject().value("isError").toBool());

        std::cout << "✓ MCP lore tools semantic validation (types, shapes, colors, connector targets) verified." << std::endl;
    }

    // 44. Test save_project with saveCopy: true (saves copy without switching active project)
    {
        const QString activeBefore = mainWindow.project().filePath();
        const QString copySavePath = QDir::tempPath() + QStringLiteral("/mcp_test_copy_%1.ao3proj").arg(QDateTime::currentMSecsSinceEpoch());

        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 80;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{
            {"name", "save_project"},
            {"arguments", QJsonObject{
                {"filePath", copySavePath},
                {"saveCopy", true},
                {"commitMessage", "Snapshot copy"}
            }}
        };
        QJsonObject resp = sendAndReceive(req);
        assert(resp.value("id").toInt() == 80);
        QJsonObject result = resp.value("result").toObject();
        assert(!result.value("isError").toBool());
        QJsonObject data = QJsonDocument::fromJson(result.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(data.value("isCopy").toBool() == true);
        assert(data.value("switchedActiveProject").toBool() == false);
        assert(data.value("savedPath").toString() == copySavePath);
        assert(QFile::exists(copySavePath));
        assert(mainWindow.project().filePath() == activeBefore);
        QFile::remove(copySavePath);
        std::cout << "✓ MCP 'save_project' with saveCopy: true preserves active project state." << std::endl;
    }

    // 45. Test manage_lore_data unrecognized field validation, remove_graph default reset, and audit enrichment
    {
        // A. Reject unrecognized field
        QJsonObject badKeyReq;
        badKeyReq["jsonrpc"] = "2.0";
        badKeyReq["id"] = 81;
        badKeyReq["method"] = "tools/call";
        badKeyReq["params"] = QJsonObject{
            {"name", "manage_lore_data"},
            {"arguments", QJsonObject{
                {"action", "add_history"},
                {"summary", "Valid summary"},
                {"unknownBogusKey", 123}
            }}
        };
        QJsonObject badKeyResp = sendAndReceive(badKeyReq);
        assert(badKeyResp.value("id").toInt() == 81);
        QJsonObject badKeyRes = badKeyResp.value("result").toObject();
        assert(badKeyRes.value("isError").toBool());
        QString badKeyText = badKeyRes.value("content").toArray().first().toObject().value("text").toString();
        assert(badKeyText.contains("Unrecognized field"));
        assert(badKeyText.contains("unknownBogusKey"));

        // B. Add history using description fallback
        QJsonObject histReq;
        histReq["jsonrpc"] = "2.0";
        histReq["id"] = 82;
        histReq["method"] = "tools/call";
        histReq["params"] = QJsonObject{
            {"name", "manage_lore_data"},
            {"arguments", QJsonObject{
                {"action", "add_history"},
                {"id", "audit_test_event_1"},
                {"description", "Crucial turning point in lore"},
                {"chapterIndex", 1}
            }}
        };
        QJsonObject histResp = sendAndReceive(histReq);
        assert(histResp.value("id").toInt() == 82);
        assert(!histResp.value("result").toObject().value("isError").toBool());

        // C. remove_graph on default graph reports resetToEmpty: true and isDefaultGraph: true
        QJsonObject remGraphReq;
        remGraphReq["jsonrpc"] = "2.0";
        remGraphReq["id"] = 83;
        remGraphReq["method"] = "tools/call";
        remGraphReq["params"] = QJsonObject{
            {"name", "manage_lore_data"},
            {"arguments", QJsonObject{
                {"action", "remove_graph"},
                {"graphName", "data"}
            }}
        };
        QJsonObject remGraphResp = sendAndReceive(remGraphReq);
        assert(remGraphResp.value("id").toInt() == 83);
        QJsonObject remGraphRes = remGraphResp.value("result").toObject();
        assert(!remGraphRes.value("isError").toBool());
        QJsonObject remGraphData = QJsonDocument::fromJson(remGraphRes.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(remGraphData.value("isDefaultGraph").toBool() == true);
        assert(remGraphData.value("resetToEmpty").toBool() == true);
        assert(remGraphData.value("message").toString().contains("reset to an empty graph"));

        // D. Verify enriched audit entry in get_agent_history
        QJsonObject auditReq;
        auditReq["jsonrpc"] = "2.0";
        auditReq["id"] = 84;
        auditReq["method"] = "tools/call";
        auditReq["params"] = QJsonObject{
            {"name", "get_agent_history"},
            {"arguments", QJsonObject{{"limit", 10}}}
        };
        QJsonObject auditResp = sendAndReceive(auditReq);
        assert(auditResp.value("id").toInt() == 84);
        QJsonObject auditData = QJsonDocument::fromJson(auditResp.value("result").toObject().value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        QJsonArray entries = auditData.value("entries").toArray();
        bool foundDetailedLoreAction = false;
        for (const auto &v : entries) {
            QJsonObject entry = v.toObject();
            const QString summary = entry.value("summary").toString();
            const QString act = entry.value("action").toString();
            if (act == "manage_lore_data" && summary.contains("audit_test_event_1")) {
                foundDetailedLoreAction = true;
                break;
            }
        }
        assert(foundDetailedLoreAction);

        std::cout << "✓ MCP 'manage_lore_data' field validation, default graph reset, and enriched audit logging verified." << std::endl;
    }

    // 25. Test ChapterManagerDialog UI and ChapterSidebar integration
    {
        auto *sidebar = mainWindow.findChild<ChapterSidebar *>();
        assert(sidebar != nullptr);

        // Sidebar must have m_managerBtn and no old cluttered buttons
        auto *managerBtn = sidebar->findChild<QPushButton *>();
        assert(managerBtn != nullptr);
        bool foundManagerBtn = false;
        for (auto *btn : sidebar->findChildren<QPushButton *>()) {
            if (btn->text().contains("Chapter Manager")) {
                foundManagerBtn = true;
                break;
            }
        }
        assert(foundManagerBtn);

        // Ensure the active project has at least 2 chapters for reordering tests
        Ao3Project &proj = mainWindow.project();
        while (proj.chapters().size() < 3) {
            proj.addChapter(QStringLiteral("Test Extra Chapter %1").arg(proj.chapters().size() + 1));
        }
        proj.chapters()[0].setTitle(QStringLiteral("First Chapter"));
        proj.chapters()[0].setHtml(QStringLiteral("<p><b>Bold sentence</b> <i>italic note</i> <div>div box</div></p>"));
        proj.chapters()[1].setTitle(QStringLiteral("Second Chapter"));
        proj.chapters()[1].setHtml(QStringLiteral("<p>Plain text paragraph in chapter 2.</p>"));

        ChapterManagerDialog dlg(&proj, &mainWindow);
        auto *tree = dlg.findChild<QTreeWidget *>();
        assert(tree != nullptr);
        assert(tree->topLevelItemCount() == proj.chapters().size());

        // Check columns: Chapter #, Title, Words, Lines, Syntaxes Used
        auto *item0 = tree->topLevelItem(0);
        assert(item0->text(0) == "Chapter 1");
        assert(item0->text(1) == "First Chapter");
        assert(item0->text(4).contains("<b>"));
        assert(item0->text(4).contains("<i>"));
        assert(item0->text(4).contains("<div>"));

        // Check spinbox minimum is 1 (never 0!)
        auto *spin = dlg.findChild<QSpinBox *>();
        assert(spin != nullptr);
        assert(spin->minimum() == 1);
        assert(spin->maximum() == proj.chapters().size());

        std::cout << "✓ ChapterManagerDialog metadata display, syntax extraction, and 1-based spinbox minimum verified." << std::endl;
    }

    // 26. Test MCP 'reorder_chapters': Chapter 0 rejection and 1-based indexing
    {
        // A. Attempting to use fromIndex: 0 must be rejected
        QJsonObject req0;
        req0["jsonrpc"] = "2.0";
        req0["id"] = 90;
        req0["method"] = "tools/call";
        req0["params"] = QJsonObject{
            {"name", "reorder_chapters"},
            {"arguments", QJsonObject{
                {"fromIndex", 0},
                {"toIndex", 2}
            }}
        };
        QJsonObject resp0 = sendAndReceive(req0);
        assert(resp0.value("id").toInt() == 90);
        QJsonObject res0 = resp0.value("result").toObject();
        assert(res0.value("isError").toBool() == true);
        QString errText0 = res0.value("content").toArray().first().toObject().value("text").toString();
        assert(errText0.contains("Chapter 0 is not permitted"));
        assert(errText0.contains("Chapter 1 must stay Chapter 1"));

        // B. Attempting to use toChapter: 0 must be rejected
        QJsonObject req0b;
        req0b["jsonrpc"] = "2.0";
        req0b["id"] = 91;
        req0b["method"] = "tools/call";
        req0b["params"] = QJsonObject{
            {"name", "reorder_chapters"},
            {"arguments", QJsonObject{
                {"fromChapter", 1},
                {"toChapter", 0}
            }}
        };
        QJsonObject resp0b = sendAndReceive(req0b);
        assert(resp0b.value("id").toInt() == 91);
        QJsonObject res0b = resp0b.value("result").toObject();
        assert(res0b.value("isError").toBool() == true);
        QString errText0b = res0b.value("content").toArray().first().toObject().value("text").toString();
        assert(errText0b.contains("Chapter 0 is not permitted"));

        // C. Claude test case: attempting out of bounds resort (e.g. position 25) must be rejected
        QJsonObject req25;
        req25["jsonrpc"] = "2.0";
        req25["id"] = 92;
        req25["method"] = "tools/call";
        req25["params"] = QJsonObject{
            {"name", "reorder_chapters"},
            {"arguments", QJsonObject{
                {"fromIndex", 1},
                {"toIndex", 25}
            }}
        };
        QJsonObject resp25 = sendAndReceive(req25);
        assert(resp25.value("id").toInt() == 92);
        QJsonObject res25 = resp25.value("result").toObject();
        assert(res25.value("isError").toBool() == true);
        QString errText25 = res25.value("content").toArray().first().toObject().value("text").toString();
        assert(errText25.contains("Valid chapter positions are 1 to"));

        // D. Valid 1-based reorder: Move Chapter 1 to position 2
        const QString origCh1Title = mainWindow.project().chapters().at(0).title();
        QJsonObject reqValid;
        reqValid["jsonrpc"] = "2.0";
        reqValid["id"] = 93;
        reqValid["method"] = "tools/call";
        reqValid["params"] = QJsonObject{
            {"name", "reorder_chapters"},
            {"arguments", QJsonObject{
                {"fromIndex", 1},
                {"toIndex", 2}
            }}
        };
        QJsonObject respValid = sendAndReceive(reqValid);
        assert(respValid.value("id").toInt() == 93);
        QJsonObject resValid = respValid.value("result").toObject();
        assert(!resValid.value("isError").toBool());
        QJsonObject dataValid = QJsonDocument::fromJson(resValid.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(dataValid.value("success").toBool() == true);
        assert(dataValid.value("fromChapter").toInt() == 1);
        assert(dataValid.value("toChapter").toInt() == 2);
        assert(mainWindow.project().chapters().at(1).title() == origCh1Title);

        std::cout << "✓ MCP 'reorder_chapters' Chapter 0 rejection, out-of-bounds rejection, and 1-based reordering verified." << std::endl;
    }

    // 27. Test MCP 'update_chapter_html': <span> support with layout warning tip, and Chapter 0 rejection
    {
        // A. Submitting HTML with <span> succeeds and returns spanTip warning against card layout
        QJsonObject spanReq;
        spanReq["jsonrpc"] = "2.0";
        spanReq["id"] = 94;
        spanReq["method"] = "tools/call";
        spanReq["params"] = QJsonObject{
            {"name", "update_chapter_html"},
            {"arguments", QJsonObject{
                {"chapterIndex", 0},
                {"html", "<p><span class=\"custom-highlight\">Valid inline text highlighter</span></p>"}
            }}
        };
        QJsonObject spanResp = sendAndReceive(spanReq);
        assert(spanResp.value("id").toInt() == 94);
        QJsonObject spanRes = spanResp.value("result").toObject();
        assert(spanRes.value("isError").toBool() == false);
        QJsonObject spanData = QJsonDocument::fromJson(spanRes.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(spanData.value("success").toBool() == true);
        assert(spanData.value("spanTip").toString().contains("inline text highlighters"));
        assert(spanData.value("spanTip").toString().contains("card layout"));

        // B. Submitting Chapter 0 must be rejected
        QJsonObject ch0Req;
        ch0Req["jsonrpc"] = "2.0";
        ch0Req["id"] = 95;
        ch0Req["method"] = "tools/call";
        ch0Req["params"] = QJsonObject{
            {"name", "update_chapter_html"},
            {"arguments", QJsonObject{
                {"chapterNumber", 0},
                {"html", "<p><b>Some valid text</b></p>"}
            }}
        };
        QJsonObject ch0Resp = sendAndReceive(ch0Req);
        assert(ch0Resp.value("id").toInt() == 95);
        assert(ch0Resp.value("result").toObject().value("isError").toBool() == true);

        // C. Submitting valid semantic HTML without <span> must succeed
        QJsonObject validReq;
        validReq["jsonrpc"] = "2.0";
        validReq["id"] = 96;
        validReq["method"] = "tools/call";
        validReq["params"] = QJsonObject{
            {"name", "update_chapter_html"},
            {"arguments", QJsonObject{
                {"chapterIndex", 0},
                {"html", "<p><strong>Valid bold</strong> and <em>italic prose</em>.</p>"}
            }}
        };
        QJsonObject validResp = sendAndReceive(validReq);
        assert(validResp.value("id").toInt() == 96);
        QJsonObject validRes = validResp.value("result").toObject();
        assert(!validRes.value("isError").toBool());
        QJsonObject validData = QJsonDocument::fromJson(validRes.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        assert(validData.value("success").toBool() == true);
        assert(validData.value("chapterNumber").toInt() == 1);

        std::cout << "✓ MCP 'update_chapter_html' <span> support with layout warning tip and Chapter 0 rejection verified." << std::endl;
    }

    client.close();
    mainWindow.stopMcpServer();
    assert(!server->isRunning());

    std::cout << "All MCP JSON-RPC protocol, tools, multi-agent presence, and loopback tests passed successfully!" << std::endl;
    return 0;
}
