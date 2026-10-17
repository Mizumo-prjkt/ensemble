#include "plugins/PluginManager.h"
#include "plugins/PluginSandbox.h"
#include "plugins/ResourceGuard.h"
#include "plugins/ScriptEvaluator.h"
#include "plugins/ScriptLexer.h"
#include "plugins/ScriptParser.h"
#include "plugins/ScriptValue.h"
#include <QCoreApplication>
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

    std::cout << "========================================" << std::endl;
    std::cout << "All Sandboxed Plugin Engine tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
