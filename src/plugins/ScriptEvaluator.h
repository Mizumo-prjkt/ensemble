#pragma once

#include "PluginSandbox.h"
#include "ResourceGuard.h"
#include "ScriptParser.h"
#include "ScriptValue.h"
#include <QString>
#include <QStringList>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace EnsemblePlugin {

class Environment : public std::enable_shared_from_this<Environment> {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr);

    bool has(const std::string &name) const;
    ScriptValue get(const std::string &name, int line = -1, int col = -1) const;
    void set(const std::string &name, const ScriptValue &val, ResourceGuard &guard, int line = -1, int col = -1);
    void define(const std::string &name, const ScriptValue &val, ResourceGuard &guard, int line = -1, int col = -1);
    void defineImmutable(const std::string &name, const ScriptValue &val);

    std::shared_ptr<Environment> parent() const noexcept { return m_parent; }

private:
    std::shared_ptr<Environment> m_parent;
    std::map<std::string, ScriptValue> m_values;
    std::unordered_set<std::string> m_immutableNames;
};

struct RegisteredHook {
    std::string functionName;
    ScriptFunction function;
    std::string argument; // e.g. button label for @on_toolbar_click("Check")
};

/**
 * @brief Tree-walking AST evaluator with ResourceGuard enforcement on every step.
 */
class ScriptEvaluator {
public:
    explicit ScriptEvaluator(PluginSandbox &sandbox, ResourceLimits limits = ResourceLimits());

    ResourceGuard &guard() noexcept { return m_guard; }
    PluginSandbox &sandbox() noexcept { return m_sandbox; }

    void executeProgram(const std::shared_ptr<ProgramNode> &program);
    void executeScript(const std::string &source);

    // Hook dispatchers
    void dispatchChapterChange(const QString &title, const QString &text, int chapterIndex);
    void dispatchToolbarClick(const QString &actionName);
    void dispatchMessage(const QString &senderId, const QString &payload);

    bool hasToolbarHook(const QString &actionName) const;
    QStringList registeredToolbarActions() const;

private:
    void initGlobals();

    ScriptValue evaluate(const std::shared_ptr<AstNode> &node, std::shared_ptr<Environment> env);
    void executeBlock(const std::shared_ptr<BlockNode> &block, std::shared_ptr<Environment> env);
    ScriptValue callFunction(const ScriptValue &callee, const std::vector<ScriptValue> &args, int line, int col);

    PluginSandbox &m_sandbox;
    ResourceGuard m_guard;
    std::shared_ptr<Environment> m_globals;

    std::vector<RegisteredHook> m_chapterChangeHooks;
    std::vector<RegisteredHook> m_toolbarClickHooks;
    std::vector<RegisteredHook> m_messageHooks;
};

} // namespace EnsemblePlugin
