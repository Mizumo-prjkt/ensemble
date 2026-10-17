#include "plugins/PluginLogger.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginPermissions.h"
#include "plugins/PluginSandbox.h"
#include "plugins/ResourceGuard.h"
#include "plugins/ScriptEvaluator.h"
#include "plugins/ScriptLexer.h"
#include "plugins/ScriptParser.h"
#include "plugins/ScriptValue.h"
#include "utils/AppPaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cassert>
#include <iostream>
#include <string>

using namespace EnsemblePlugin;

void testLexerBasics() {
    std::cout << "Running testLexerBasics..." << std::endl;

    std::string src =
        "def greet(name):\n"
        "    msg = 'Hello, ' + name\n"
        "    return msg\n";

    ScriptLexer lexer(src);
    auto tokens = lexer.tokenize();

    assert(!tokens.empty());
    assert(tokens.at(0).type == TokenType::KwDef);
    assert(tokens.at(1).type == TokenType::Identifier && tokens.at(1).text == "greet");
    assert(tokens.at(2).type == TokenType::LParen);
    assert(tokens.at(3).type == TokenType::Identifier && tokens.at(3).text == "name");
    assert(tokens.at(4).type == TokenType::RParen);
    assert(tokens.at(5).type == TokenType::Colon);
    assert(tokens.at(6).type == TokenType::Newline);
    assert(tokens.at(7).type == TokenType::Indent);

    std::cout << "✓ testLexerBasics passed." << std::endl;
}

void testLexerSecurityRejections() {
    std::cout << "Running testLexerSecurityRejections..." << std::endl;

    // Rule: Double underscore prohibited
    try {
        ScriptLexer lexer("x = obj.__class__\n");
        lexer.tokenize();
        assert(false && "Should have rejected double underscore");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected trap: " << e.what() << std::endl;
    }

    // Rule: import keyword prohibited
    try {
        ScriptLexer lexer("import os\n");
        lexer.tokenize();
        assert(false && "Should have rejected import");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected trap: " << e.what() << std::endl;
    }

    // Rule: eval prohibited
    try {
        ScriptLexer lexer("eval('1+1')\n");
        lexer.tokenize();
        assert(false && "Should have rejected eval");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected trap: " << e.what() << std::endl;
    }

    // Rule: open prohibited
    try {
        ScriptLexer lexer("f = open('/etc/passwd')\n");
        lexer.tokenize();
        assert(false && "Should have rejected open");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected trap: " << e.what() << std::endl;
    }

    std::cout << "✓ testLexerSecurityRejections passed." << std::endl;
}

void testParserAndEvaluatorBasics() {
    std::cout << "Running testParserAndEvaluatorBasics..." << std::endl;

    PluginSandbox sandbox("test.basics", "Basics Test", "1.0");
    ScriptEvaluator evaluator(sandbox);

    std::string script =
        "x = 10\n"
        "y = 20\n"
        "z = x + y * 2\n"
        "s = 'Ensemble ' + str(z)\n"
        "items = [1, 2, 3]\n"
        "items.append(4)\n"
        "d = {'key': 'value'}\n"
        "d['count'] = len(items)\n";

    evaluator.executeScript(script);

    // Verify functions and control flow
    std::string funcScript =
        "def compute(a, b):\n"
        "    if a > b:\n"
        "        return a - b\n"
        "    else:\n"
        "        return b - a\n"
        "diff1 = compute(50, 20)\n"
        "diff2 = compute(10, 40)\n";

    evaluator.executeScript(funcScript);

    std::cout << "✓ testParserAndEvaluatorBasics passed." << std::endl;
}

void testResourceGuardMemoryCap() {
    std::cout << "Running testResourceGuardMemoryCap..." << std::endl;

    ResourceLimits limits;
    limits.maxMemoryBytes = 1024 * 1024; // 1 MB limit for this test
    PluginSandbox sandbox("test.mem", "Memory Test", "1.0");
    ScriptEvaluator evaluator(sandbox, limits);

    // Attempting to allocate a giant string or list should trigger ScriptTrap
    std::string script =
        "s = 'A' * 2000000\n"; // 2 MB string attempt

    bool trapped = false;
    try {
        evaluator.executeScript(script);
    } catch (const ScriptTrap &e) {
        trapped = true;
        std::cout << "  Caught expected memory cap trap: " << e.what() << std::endl;
    }
    assert(trapped);

    std::cout << "✓ testResourceGuardMemoryCap passed." << std::endl;
}

void testResourceGuardStepLimit() {
    std::cout << "Running testResourceGuardStepLimit..." << std::endl;

    ResourceLimits limits;
    limits.maxAstSteps = 500; // Low step limit for fast test
    PluginSandbox sandbox("test.steps", "Step Limit Test", "1.0");
    ScriptEvaluator evaluator(sandbox, limits);

    std::string script =
        "while True:\n"
        "    pass\n";

    bool trapped = false;
    try {
        evaluator.executeScript(script);
    } catch (const ScriptTrap &e) {
        trapped = true;
        std::cout << "  Caught expected step limit trap: " << e.what() << std::endl;
    }
    assert(trapped);

    std::cout << "✓ testResourceGuardStepLimit passed." << std::endl;
}

void testResourceGuardRecursionDepth() {
    std::cout << "Running testResourceGuardRecursionDepth..." << std::endl;

    ResourceLimits limits;
    limits.maxCallDepth = 10;
    PluginSandbox sandbox("test.recursion", "Recursion Test", "1.0");
    ScriptEvaluator evaluator(sandbox, limits);

    std::string script =
        "def recurse(n):\n"
        "    return recurse(n + 1)\n"
        "recurse(1)\n";

    bool trapped = false;
    try {
        evaluator.executeScript(script);
    } catch (const ScriptTrap &e) {
        trapped = true;
        std::cout << "  Caught expected recursion trap: " << e.what() << std::endl;
    }
    assert(trapped);

    std::cout << "✓ testResourceGuardRecursionDepth passed." << std::endl;
}

void testStorageSandboxingAndPathTraversal() {
    std::cout << "Running testStorageSandboxingAndPathTraversal..." << std::endl;

    PluginSandbox sandbox("test.storage", "Storage Test", "1.0");
    ScriptEvaluator evaluator(sandbox);

    // Normal storage operations
    std::string script =
        "storage.write('setting1', 'alpha')\n"
        "storage.write('setting2', 'beta')\n"
        "val = storage.read('setting1')\n"
        "keys = storage.list_keys()\n"
        "storage.delete('setting2')\n";

    evaluator.executeScript(script);

    // Path traversal attack: ".."
    bool trappedDotDot = false;
    try {
        evaluator.executeScript("storage.read('../hacked')\n");
    } catch (const ScriptTrap &e) {
        trappedDotDot = true;
        std::cout << "  Caught expected path traversal trap (..): " << e.what() << std::endl;
    }
    assert(trappedDotDot);

    // Path traversal attack: "/"
    bool trappedSlash = false;
    try {
        evaluator.executeScript("storage.write('/etc/passwd', 'malicious')\n");
    } catch (const ScriptTrap &e) {
        trappedSlash = true;
        std::cout << "  Caught expected path traversal trap (/): " << e.what() << std::endl;
    }
    assert(trappedSlash);

    // Path traversal attack: "\"
    bool trappedBackslash = false;
    try {
        evaluator.executeScript("storage.write('sub\\\\secret', 'malicious')\n");
    } catch (const ScriptTrap &e) {
        trappedBackslash = true;
        std::cout << "  Caught expected path traversal trap (\\): " << e.what() << std::endl;
    }
    assert(trappedBackslash);

    std::cout << "✓ testStorageSandboxingAndPathTraversal passed." << std::endl;
}

void testHostConnectorsAndDecorators() {
    std::cout << "Running testHostConnectorsAndDecorators..." << std::endl;

    PluginSandbox sandbox("test.connectors", "Connectors Test", "1.0");
    PluginPermissions perms;
    perms.allowEditorWrite = true;
    sandbox.setPermissions(perms);

    QString editorText = "Chapter 1: The Encounter\n\"Where are you going?\" asked Alice.";
    QString chapterTitle = "Chapter 1";
    QString selection = "Where are you going?";
    QString replacedText;
    QString lastWarning;
    int lastWarnLine = -1;

    EditorCallbacks edCb;
    edCb.getChapterText = [&]() { return editorText; };
    edCb.getSelection = [&]() { return selection; };
    edCb.getChapterTitle = [&]() { return chapterTitle; };
    edCb.replaceSelection = [&](const QString &s) { replacedText = s; };
    sandbox.setEditorCallbacks(edCb);

    StatusCallbacks stCb;
    stCb.onWarn = [&](const QString &, const QString &msg, int line) {
        lastWarning = msg;
        lastWarnLine = line;
    };
    stCb.onError = [](const QString &, const QString &, int) {};
    stCb.onNotify = [](const QString &, const QString &) {};
    sandbox.setStatusCallbacks(stCb);

    InterPluginCallbacks ipCb;
    ipCb.listPlugins = []() -> QList<QVariantMap> {
        QList<QVariantMap> list;
        QVariantMap m;
        m["id"] = "test.connectors";
        m["name"] = "Connectors Test";
        m["version"] = "1.0";
        // NO secret_uidp!
        list.append(m);
        return list;
    };
    ipCb.isActive = [](const QString &) { return true; };
    ipCb.sendMessage = [](const QString &, const QString &, const QString &) { return true; };
    sandbox.setInterPluginCallbacks(ipCb);

    ScriptEvaluator evaluator(sandbox);

    std::string script =
        "@on_toolbar_click(\"Check Dialogue\")\n"
        "def on_check():\n"
        "    t = editor.get_chapter_text()\n"
        "    sel = editor.get_selection()\n"
        "    if len(sel) > 0:\n"
        "        editor.replace_selection(sel.upper())\n"
        "    status.warn('Dialogue checked successfully', 42)\n"
        "\n"
        "@on_chapter_change\n"
        "def on_change(title, text, idx):\n"
        "    plist = plugins.list()\n"
        "    storage.write('last_chapter', title)\n";

    evaluator.executeScript(script);

    assert(evaluator.hasToolbarHook("Check Dialogue"));
    evaluator.dispatchToolbarClick("Check Dialogue");

    assert(replacedText == "WHERE ARE YOU GOING?");
    assert(lastWarning == "Dialogue checked successfully");
    assert(lastWarnLine == 42);

    evaluator.dispatchChapterChange(chapterTitle, editorText, 0);
    assert(sandbox.storageRead("last_chapter") == "Chapter 1");

    std::cout << "✓ testHostConnectorsAndDecorators passed." << std::endl;
}

void testPluginScannerAndPermissions() {
    std::cout << "Running testPluginScannerAndPermissions..." << std::endl;

    QString script =
        "# ID: scanner.test\n"
        "# Name: Scanner Test\n"
        "# Version: 1.0\n"
        "\n"
        "def run_check():\n"
        "    url1 = 'https://api.github.com/repos'\n"
        "    url2 = 'http://archiveofourown.org/works/123'\n"
        "    t = editor.get_chapter_text()\n"
        "    cnt = editor.get_chapter_count()\n"
        "    editor.replace_selection('hello')\n"
        "    storage.write('k', 'v')\n"
        "    win = ui.create_window('Win', 400, 300)\n"
        "    res = http.get('https://example.com/api')\n";

    PluginPermissions perms;
    PluginScanner::scan(script, perms);

    assert(perms.detectedEditorRead);
    assert(perms.detectedEditorWrite);
    assert(perms.detectedStorage);
    assert(perms.detectedUi);
    assert(perms.detectedNetwork);

    assert(perms.detectedDomains.contains("api.github.com"));
    assert(perms.detectedDomains.contains("archiveofourown.org"));
    assert(perms.detectedDomains.contains("example.com"));

    // Test permission JSON round-trip
    perms.allowNetwork = true;
    perms.allowedDomains = QStringList() << "api.github.com" << "example.com";
    QJsonObject json = perms.toJson();
    PluginPermissions restored = PluginPermissions::fromJson(json);

    assert(restored.allowNetwork);
    assert(restored.isDomainAllowed("api.github.com"));
    assert(restored.isDomainAllowed("API.GITHUB.COM"));
    assert(!restored.isDomainAllowed("evil.com"));

    std::cout << "✓ testPluginScannerAndPermissions passed." << std::endl;
}

void testNetworkPermissionsAndAuditLog() {
    std::cout << "Running testNetworkPermissionsAndAuditLog..." << std::endl;

    PluginSandbox sandbox("test.net", "Net Test", "1.0");

    // Case 1: Network disabled by default
    ScriptEvaluator eval1(sandbox);
    try {
        eval1.executeScript("res = http.get('https://api.example.com/data')\n");
        assert(false && "Should have blocked network access when disabled");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected network disabled trap: " << e.what() << std::endl;
    }

    // Case 2: Network enabled, but domain not whitelisted
    PluginPermissions perms;
    perms.allowNetwork = true;
    perms.allowedDomains = QStringList() << "trusted.com";
    sandbox.setPermissions(perms);

    ScriptEvaluator eval2(sandbox);
    try {
        eval2.executeScript("res = http.get('https://untrusted.com/data')\n");
        assert(false && "Should have blocked untrusted domain");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected untrusted domain trap: " << e.what() << std::endl;
    }

    // Case 3: Insecure non-HTTPS rejected and quarantined
    try {
        eval2.executeScript("res = http.get('http://trusted.com/data')\n");
        assert(false && "Should have blocked insecure non-HTTPS scheme");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected non-HTTPS security violation: " << e.what() << std::endl;
        assert(sandbox.isQuarantined());
    }

    // Check audit log contains blocked records and security violations
    QStringList logs = PluginLogger::readRecentLogs(20);
    assert(!logs.isEmpty());
    bool foundBlocked = false;
    bool foundViolation = false;
    for (const QString &line : logs) {
        if (line.contains("BLOCKED") && line.contains("test.net")) {
            foundBlocked = true;
        }
        if (line.contains("SECURITY_VIOLATION") && line.contains("test.net")) {
            foundViolation = true;
        }
    }
    assert(foundBlocked);
    assert(foundViolation);

    std::cout << "✓ testNetworkPermissionsAndAuditLog passed." << std::endl;
}

void testDeclarativeUiAndWindowActions() {
    std::cout << "Running testDeclarativeUiAndWindowActions..." << std::endl;

    PluginSandbox sandbox("test.ui", "UI Test", "1.0");
    PluginPermissions perms;
    perms.allowUi = true;
    sandbox.setPermissions(perms);

    QString createdWinId;
    QString createdTitle;
    int createdW = 0, createdH = 0;
    QStringList addedWidgetTypes;

    UiCallbacks uiCb;
    uiCb.createWindow = [&](const QString &winId, const QString &title, int w, int h) {
        createdWinId = winId;
        createdTitle = title;
        createdW = w;
        createdH = h;
    };
    uiCb.addWidget = [&](const QString &, const QString &type, const QVariantMap &) {
        addedWidgetTypes.append(type);
    };
    uiCb.showWindow = [&](const QString &) {};
    uiCb.closeWindow = [&](const QString &) {};
    sandbox.setUiCallbacks(uiCb);

    ScriptEvaluator eval(sandbox);
    std::string script =
        "win = ui.create_window('Dialogue Analysis', 500, 350)\n"
        "win.add_label('Checking dialog...')\n"
        "win.add_text('All quotes ok')\n"
        "win.add_input('type here')\n"
        "win.add_button('Apply', 'on_apply')\n"
        "\n"
        "result_var = ''\n"
        "def on_apply(data):\n"
        "    result_var = data['input_0']\n";

    eval.executeScript(script);

    assert(!createdWinId.isEmpty());
    assert(createdTitle == "Dialogue Analysis");
    assert(createdW == 500);
    assert(createdH == 350);
    assert(addedWidgetTypes.size() == 4);
    assert(addedWidgetTypes[0] == "label");
    assert(addedWidgetTypes[1] == "text");
    assert(addedWidgetTypes[2] == "input");
    assert(addedWidgetTypes[3] == "button");

    // Case 2: UI disabled
    perms.allowUi = false;
    sandbox.setPermissions(perms);
    ScriptEvaluator evalBlocked(sandbox);
    try {
        evalBlocked.executeScript("win = ui.create_window('Bad', 400, 300)\n");
        assert(false && "Should have blocked UI creation");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected UI disabled trap: " << e.what() << std::endl;
    }

    std::cout << "✓ testDeclarativeUiAndWindowActions passed." << std::endl;
}

void testExpandedEditorAndPermissions() {
    std::cout << "Running testExpandedEditorAndPermissions..." << std::endl;

    PluginSandbox sandbox("test.editor", "Editor Test", "1.0");
    PluginPermissions perms;
    perms.allowEditorRead = true;
    perms.allowEditorWrite = false; // Write disabled!
    sandbox.setPermissions(perms);

    EditorCallbacks edCb;
    edCb.getChapterText = []() { return QStringLiteral("Once upon a time in a faraway land"); };
    edCb.getSelection = []() { return QStringLiteral("time"); };
    edCb.getChapterTitle = []() { return QStringLiteral("Prologue"); };
    edCb.getChapterCount = []() { return 3; };
    edCb.getAllChapters = []() {
        QList<QVariantMap> list;
        QVariantMap c1, c2;
        c1["index"] = 0; c1["title"] = "Chapter 1"; c1["text"] = "Text 1";
        c2["index"] = 1; c2["title"] = "Chapter 2"; c2["text"] = "Text 2";
        list.append(c1); list.append(c2);
        return list;
    };
    edCb.getWordCount = []() { return 7; };
    sandbox.setEditorCallbacks(edCb);

    ScriptEvaluator eval(sandbox);
    eval.executeScript(
        "cnt = editor.get_chapter_count()\n"
        "wc = editor.get_word_count()\n"
        "all_c = editor.get_all_chapters()\n"
        "first_title = all_c[0]['title']\n"
    );

    // Try editor write while disabled
    try {
        eval.executeScript("editor.replace_selection('test')\n");
        assert(false && "Should have blocked editor write");
    } catch (const ScriptTrap &e) {
        std::cout << "  Caught expected editor write disabled trap: " << e.what() << std::endl;
    }

    // Enable write
    perms.allowEditorWrite = true;
    sandbox.setPermissions(perms);
    QString written;
    edCb.replaceSelection = [&](const QString &s) { written = s; };
    sandbox.setEditorCallbacks(edCb);

    eval.executeScript("editor.replace_selection('new text')\n");
    assert(written == "new text");

    std::cout << "✓ testExpandedEditorAndPermissions passed." << std::endl;
}

void testPluginManagerErLoadingAndUninstall() {
    std::cout << "Running testPluginManagerErLoadingAndUninstall..." << std::endl;

    QString pluginDir = AppPaths::pluginsDir();
    QDir().mkpath(pluginDir);

    QString erPath = QDir(pluginDir).filePath(QStringLiteral("test_sample_runner.er"));
    {
        QFile file(erPath);
        assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const char *code =
            "# ID: test.sample-runner\n"
            "# Name: Sample Runner\n"
            "# Version: 2.0\n"
            "\n"
            "@on_toolbar_click(\"Action Runner\")\n"
            "def do_run():\n"
            "    status.notify('Runner executed')\n";
        file.write(code);
    }

    // Load .er file
    QString err;
    bool loaded = PluginManager::instance().loadPluginFromFile(erPath, &err);
    assert(loaded);
    assert(PluginManager::instance().hasPlugin("test.sample-runner"));

    PluginMetadata meta = PluginManager::instance().plugin("test.sample-runner");
    assert(meta.name == "Sample Runner");
    assert(meta.version == "2.0");

    // Uninstall
    bool uninstalled = PluginManager::instance().uninstallPlugin("test.sample-runner", true, &err);
    assert(uninstalled);
    assert(!PluginManager::instance().hasPlugin("test.sample-runner"));
    assert(!QFile::exists(erPath)); // File deleted on disk!

    std::cout << "✓ testPluginManagerErLoadingAndUninstall passed." << std::endl;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "========================================" << std::endl;
    std::cout << "Running Sandboxed Plugin Engine Tests..." << std::endl;
    std::cout << "========================================" << std::endl;

    testLexerBasics();
    testLexerSecurityRejections();
    testParserAndEvaluatorBasics();
    testResourceGuardMemoryCap();
    testResourceGuardStepLimit();
    testResourceGuardRecursionDepth();
    testStorageSandboxingAndPathTraversal();
    testHostConnectorsAndDecorators();
    testPluginScannerAndPermissions();
    testNetworkPermissionsAndAuditLog();
    testDeclarativeUiAndWindowActions();
    testExpandedEditorAndPermissions();
    testPluginManagerErLoadingAndUninstall();

    std::cout << "========================================" << std::endl;
    std::cout << "All Sandboxed Plugin Engine tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
