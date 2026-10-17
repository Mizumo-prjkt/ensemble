#include "ScriptEvaluator.h"
#include <iostream>

namespace EnsemblePlugin {

struct ReturnSignal {
    ScriptValue value;
};

struct BreakSignal {};
struct ContinueSignal {};

// Environment implementation
Environment::Environment(std::shared_ptr<Environment> parent)
    : m_parent(std::move(parent))
{
}

bool Environment::has(const std::string &name) const {
    if (m_values.find(name) != m_values.end()) return true;
    if (m_parent) return m_parent->has(name);
    return false;
}

ScriptValue Environment::get(const std::string &name, int line, int col) const {
    auto it = m_values.find(name);
    if (it != m_values.end()) {
        return it->second;
    }
    if (m_parent) {
        return m_parent->get(name, line, col);
    }
    throw ScriptTrap("NameError: name '" + name + "' is not defined", line, col);
}

void Environment::set(const std::string &name, const ScriptValue &val, ResourceGuard &guard, int line, int col) {
    if (m_immutableNames.find(name) != m_immutableNames.end()) {
        throw ScriptTrap("SecurityError: cannot reassign built-in namespace '" + name + "'", line, col);
    }
    auto it = m_values.find(name);
    if (it != m_values.end()) {
        guard.alloc(val.estimateBytes(), line, col);
        it->second = val;
        return;
    }
    if (m_parent && m_parent->has(name)) {
        m_parent->set(name, val, guard, line, col);
        return;
    }
    define(name, val, guard, line, col);
}

void Environment::define(const std::string &name, const ScriptValue &val, ResourceGuard &guard, int line, int col) {
    if (m_immutableNames.find(name) != m_immutableNames.end()) {
        throw ScriptTrap("SecurityError: cannot reassign built-in namespace '" + name + "'", line, col);
    }
    guard.alloc(name.capacity() + val.estimateBytes() + 32, line, col);
    m_values[name] = val;
}

void Environment::defineImmutable(const std::string &name, const ScriptValue &val) {
    m_values[name] = val;
    m_immutableNames.insert(name);
}

// ScriptEvaluator implementation
ScriptEvaluator::ScriptEvaluator(PluginSandbox &sandbox, ResourceLimits limits)
    : m_sandbox(sandbox)
    , m_guard(limits)
    , m_globals(std::make_shared<Environment>())
{
    initGlobals();
}

void ScriptEvaluator::initGlobals() {
    // 4 Built-in Immutable Namespaces
    m_globals->defineImmutable("editor", m_sandbox.createEditorNamespace(m_guard));
    m_globals->defineImmutable("storage", m_sandbox.createStorageNamespace(m_guard));
    m_globals->defineImmutable("status", m_sandbox.createStatusNamespace(m_guard));
    m_globals->defineImmutable("plugins", m_sandbox.createPluginsNamespace(m_guard));

    // Built-in standard functions
    m_globals->defineImmutable("len", ScriptValue([](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("len() takes exactly one argument", l, c);
        const auto &val = args.at(0);
        if (val.isString()) return ScriptValue(static_cast<int64_t>(val.asString().size()));
        if (val.isList()) return ScriptValue(static_cast<int64_t>(val.asList()->size()));
        if (val.isDict()) return ScriptValue(static_cast<int64_t>(val.asDict()->size()));
        throw ScriptTrap("TypeError: object of type '" + val.typeName() + "' has no len()", l, c);
    }, "len"));

    m_globals->defineImmutable("str", ScriptValue([](const std::vector<ScriptValue> &args, ResourceGuard &g, int, int) -> ScriptValue {
        if (args.empty()) return ScriptValue("");
        std::string s = args.at(0).toString();
        g.alloc(s.capacity() + sizeof(std::string));
        return ScriptValue(std::move(s));
    }, "str"));

    m_globals->defineImmutable("int", ScriptValue([](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) return ScriptValue(0);
        return ScriptValue(args.at(0).toInteger(l, c));
    }, "int"));

    m_globals->defineImmutable("float", ScriptValue([](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) return ScriptValue(0.0);
        return ScriptValue(args.at(0).toNumber(l, c));
    }, "float"));

    m_globals->defineImmutable("range", ScriptValue([](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("range expected at least 1 argument, got 0", l, c);
        int64_t start = 0;
        int64_t stop = 0;
        int64_t step = 1;

        if (args.size() == 1) {
            stop = args.at(0).toInteger(l, c);
        } else if (args.size() == 2) {
            start = args.at(0).toInteger(l, c);
            stop = args.at(1).toInteger(l, c);
        } else {
            start = args.at(0).toInteger(l, c);
            stop = args.at(1).toInteger(l, c);
            step = args.at(2).toInteger(l, c);
            if (step == 0) throw ScriptTrap("ValueError: range() arg 3 must not be zero", l, c);
        }

        auto list = std::make_shared<ScriptList>();
        g.alloc(sizeof(ScriptList), l, c);
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) {
                g.alloc(sizeof(ScriptValue), l, c);
                list->push_back(ScriptValue(i));
            }
        } else {
            for (int64_t i = start; i > stop; i += step) {
                g.alloc(sizeof(ScriptValue), l, c);
                list->push_back(ScriptValue(i));
            }
        }
        return ScriptValue(list);
    }, "range"));

    m_globals->defineImmutable("print", ScriptValue([](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
        std::string output;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) output += " ";
            output += args.at(i).toString();
        }
        std::cout << "[Plugin DSL] " << output << std::endl;
        return ScriptValue();
    }, "print"));
}

void ScriptEvaluator::executeScript(const std::string &source) {
    ScriptLexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    ScriptParser parser(std::move(tokens));
    auto program = parser.parseProgram();
    executeProgram(program);
}

void ScriptEvaluator::executeProgram(const std::shared_ptr<ProgramNode> &program) {
    if (!program) return;
    m_guard.resetForHook();

    for (const auto &stmt : program->statements) {
        evaluate(stmt, m_globals);
    }
}

ScriptValue ScriptEvaluator::evaluate(const std::shared_ptr<AstNode> &node, std::shared_ptr<Environment> env) {
    if (!node) return ScriptValue();

    m_guard.tick(node->line, node->column);

    switch (node->type()) {
    case AstNodeType::NumberExpr: {
        auto n = std::static_pointer_cast<NumberExprNode>(node);
        return ScriptValue(n->value);
    }
    case AstNodeType::StringExpr: {
        auto s = std::static_pointer_cast<StringExprNode>(node);
        m_guard.alloc(s->value.capacity() + sizeof(std::string), s->line, s->column);
        return ScriptValue(s->value);
    }
    case AstNodeType::BooleanExpr: {
        auto b = std::static_pointer_cast<BooleanExprNode>(node);
        return ScriptValue(b->value);
    }
    case AstNodeType::NoneExpr: {
        return ScriptValue();
    }
    case AstNodeType::IdentifierExpr: {
        auto id = std::static_pointer_cast<IdentifierExprNode>(node);
        return env->get(id->name, id->line, id->column);
    }
    case AstNodeType::ListExpr: {
        auto l = std::static_pointer_cast<ListExprNode>(node);
        auto list = std::make_shared<ScriptList>();
        m_guard.alloc(sizeof(ScriptList), l->line, l->column);
        for (const auto &elem : l->elements) {
            auto val = evaluate(elem, env);
            m_guard.alloc(val.estimateBytes(), elem->line, elem->column);
            list->push_back(val);
        }
        return ScriptValue(list);
    }
    case AstNodeType::DictExpr: {
        auto d = std::static_pointer_cast<DictExprNode>(node);
        auto dict = std::make_shared<ScriptDict>();
        m_guard.alloc(sizeof(ScriptDict), d->line, d->column);
        for (const auto &entry : d->entries) {
            auto k = evaluate(entry.first, env);
            auto v = evaluate(entry.second, env);
            std::string keyStr = k.toString();
            m_guard.alloc(keyStr.capacity() + v.estimateBytes() + 32, entry.first->line, entry.first->column);
            (*dict)[keyStr] = v;
        }
        return ScriptValue(dict);
    }
    case AstNodeType::UnaryExpr: {
        auto u = std::static_pointer_cast<UnaryExprNode>(node);
        auto val = evaluate(u->expr, env);
        if (u->op == TokenType::KwNot) {
            return ScriptValue(!val.toBool());
        }
        if (u->op == TokenType::Minus) {
            return val.unaryMinus(m_guard, u->line, u->column);
        }
        throw ScriptTrap("SyntaxError: unsupported unary operator", u->line, u->column);
    }
    case AstNodeType::BinaryExpr: {
        auto b = std::static_pointer_cast<BinaryExprNode>(node);
        // Short-circuiting logical operators
        if (b->op == TokenType::KwOr) {
            auto left = evaluate(b->left, env);
            if (left.toBool()) return left;
            return evaluate(b->right, env);
        }
        if (b->op == TokenType::KwAnd) {
            auto left = evaluate(b->left, env);
            if (!left.toBool()) return left;
            return evaluate(b->right, env);
        }

        auto left = evaluate(b->left, env);
        auto right = evaluate(b->right, env);

        switch (b->op) {
        case TokenType::Plus:
            return left.add(right, m_guard, b->line, b->column);
        case TokenType::Minus:
            return left.subtract(right, m_guard, b->line, b->column);
        case TokenType::Star:
            return left.multiply(right, m_guard, b->line, b->column);
        case TokenType::Slash:
            return left.divide(right, m_guard, b->line, b->column);
        case TokenType::Percent:
            return left.modulo(right, m_guard, b->line, b->column);
        case TokenType::Equal:
            return ScriptValue(left.equals(right));
        case TokenType::NotEqual:
            return ScriptValue(!left.equals(right));
        case TokenType::Less:
            return ScriptValue(left.lessThan(right, b->line, b->column));
        case TokenType::LessEqual:
            return ScriptValue(left.lessThan(right, b->line, b->column) || left.equals(right));
        case TokenType::Greater:
            return ScriptValue(!left.lessThan(right, b->line, b->column) && !left.equals(right));
        case TokenType::GreaterEqual:
            return ScriptValue(!left.lessThan(right, b->line, b->column));
        case TokenType::KwIn:
            return ScriptValue(right.contains(left, m_guard, b->line, b->column));
        default:
            throw ScriptTrap("SyntaxError: unknown binary operator", b->line, b->column);
        }
    }
    case AstNodeType::SubscriptExpr: {
        auto sub = std::static_pointer_cast<SubscriptExprNode>(node);
        auto target = evaluate(sub->target, env);
        auto index = evaluate(sub->index, env);
        return target.getItem(index, m_guard, sub->line, sub->column);
    }
    case AstNodeType::MemberExpr: {
        auto mem = std::static_pointer_cast<MemberExprNode>(node);
        auto target = evaluate(mem->target, env);
        if (target.isDict()) {
            return target.getMember(mem->member, m_guard, mem->line, mem->column);
        }
        // Return bound method closure for string, list, dict methods
        return ScriptValue([target, method = mem->member](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
            return target.callMember(method, args, g, l, c);
        }, mem->member);
    }
    case AstNodeType::CallExpr: {
        auto call = std::static_pointer_cast<CallExprNode>(node);
        std::vector<ScriptValue> args;
        for (const auto &argNode : call->arguments) {
            args.push_back(evaluate(argNode, env));
        }

        // Optimization: if callee is MemberExpr, we can call directly
        if (call->callee->type() == AstNodeType::MemberExpr) {
            auto mem = std::static_pointer_cast<MemberExprNode>(call->callee);
            auto target = evaluate(mem->target, env);
            return target.callMember(mem->member, args, m_guard, call->line, call->column);
        }

        auto calleeVal = evaluate(call->callee, env);
        return callFunction(calleeVal, args, call->line, call->column);
    }
    case AstNodeType::AssignStmt: {
        auto assign = std::static_pointer_cast<AssignStmtNode>(node);
        auto val = evaluate(assign->value, env);
        if (assign->op == TokenType::Assign) {
            env->set(assign->name, val, m_guard, assign->line, assign->column);
        } else if (assign->op == TokenType::PlusAssign) {
            auto oldVal = env->get(assign->name, assign->line, assign->column);
            auto newVal = oldVal.add(val, m_guard, assign->line, assign->column);
            env->set(assign->name, newVal, m_guard, assign->line, assign->column);
        } else if (assign->op == TokenType::MinusAssign) {
            auto oldVal = env->get(assign->name, assign->line, assign->column);
            auto newVal = oldVal.subtract(val, m_guard, assign->line, assign->column);
            env->set(assign->name, newVal, m_guard, assign->line, assign->column);
        }
        return ScriptValue();
    }
    case AstNodeType::SubscriptAssignStmt: {
        auto subAssign = std::static_pointer_cast<SubscriptAssignStmtNode>(node);
        auto target = evaluate(subAssign->target, env);
        auto index = evaluate(subAssign->index, env);
        auto val = evaluate(subAssign->value, env);
        if (subAssign->op == TokenType::Assign) {
            target.setItem(index, val, m_guard, subAssign->line, subAssign->column);
        } else if (subAssign->op == TokenType::PlusAssign) {
            auto oldVal = target.getItem(index, m_guard, subAssign->line, subAssign->column);
            auto newVal = oldVal.add(val, m_guard, subAssign->line, subAssign->column);
            target.setItem(index, newVal, m_guard, subAssign->line, subAssign->column);
        } else if (subAssign->op == TokenType::MinusAssign) {
            auto oldVal = target.getItem(index, m_guard, subAssign->line, subAssign->column);
            auto newVal = oldVal.subtract(val, m_guard, subAssign->line, subAssign->column);
            target.setItem(index, newVal, m_guard, subAssign->line, subAssign->column);
        }
        return ScriptValue();
    }
    case AstNodeType::MemberAssignStmt: {
        auto memAssign = std::static_pointer_cast<MemberAssignStmtNode>(node);
        auto target = evaluate(memAssign->target, env);
        auto val = evaluate(memAssign->value, env);
        if (memAssign->op == TokenType::Assign) {
            target.setMember(memAssign->member, val, m_guard, memAssign->line, memAssign->column);
        } else if (memAssign->op == TokenType::PlusAssign) {
            auto oldVal = target.getMember(memAssign->member, m_guard, memAssign->line, memAssign->column);
            auto newVal = oldVal.add(val, m_guard, memAssign->line, memAssign->column);
            target.setMember(memAssign->member, newVal, m_guard, memAssign->line, memAssign->column);
        } else if (memAssign->op == TokenType::MinusAssign) {
            auto oldVal = target.getMember(memAssign->member, m_guard, memAssign->line, memAssign->column);
            auto newVal = oldVal.subtract(val, m_guard, memAssign->line, memAssign->column);
            target.setMember(memAssign->member, newVal, m_guard, memAssign->line, memAssign->column);
        }
        return ScriptValue();
    }
    case AstNodeType::ExprStmt: {
        auto exprStmt = std::static_pointer_cast<ExprStmtNode>(node);
        evaluate(exprStmt->expr, env);
        return ScriptValue();
    }
    case AstNodeType::ReturnStmt: {
        auto ret = std::static_pointer_cast<ReturnStmtNode>(node);
        ScriptValue val;
        if (ret->value) {
            val = evaluate(ret->value, env);
        }
        throw ReturnSignal{val};
    }
    case AstNodeType::BreakStmt: {
        throw BreakSignal{};
    }
    case AstNodeType::ContinueStmt: {
        throw ContinueSignal{};
    }
    case AstNodeType::PassStmt: {
        return ScriptValue();
    }
    case AstNodeType::IfStmt: {
        auto ifStmt = std::static_pointer_cast<IfStmtNode>(node);
        for (const auto &branch : ifStmt->branches) {
            auto condVal = evaluate(branch.condition, env);
            if (condVal.toBool()) {
                executeBlock(branch.body, env);
                return ScriptValue();
            }
        }
        if (ifStmt->elseBranch) {
            executeBlock(ifStmt->elseBranch, env);
        }
        return ScriptValue();
    }
    case AstNodeType::ForStmt: {
        auto forStmt = std::static_pointer_cast<ForStmtNode>(node);
        auto iterable = evaluate(forStmt->iterable, env);

        std::vector<ScriptValue> items;
        if (iterable.isList()) {
            auto l = iterable.asList();
            if (l) items = *l;
        } else if (iterable.isString()) {
            const auto &str = iterable.asString();
            for (char ch : str) {
                items.push_back(ScriptValue(std::string(1, ch)));
            }
        } else if (iterable.isDict()) {
            auto d = iterable.asDict();
            if (d) {
                for (const auto &pair : *d) {
                    items.push_back(ScriptValue(pair.first));
                }
            }
        } else {
            throw ScriptTrap("TypeError: '" + iterable.typeName() + "' object is not iterable", forStmt->line, forStmt->column);
        }

        for (const auto &item : items) {
            m_guard.tick(forStmt->line, forStmt->column);
            env->set(forStmt->iteratorVar, item, m_guard, forStmt->line, forStmt->column);
            try {
                executeBlock(forStmt->body, env);
            } catch (const BreakSignal &) {
                break;
            } catch (const ContinueSignal &) {
                continue;
            }
        }
        return ScriptValue();
    }
    case AstNodeType::WhileStmt: {
        auto whileStmt = std::static_pointer_cast<WhileStmtNode>(node);
        while (true) {
            m_guard.tick(whileStmt->line, whileStmt->column);
            auto cond = evaluate(whileStmt->condition, env);
            if (!cond.toBool()) break;

            try {
                executeBlock(whileStmt->body, env);
            } catch (const BreakSignal &) {
                break;
            } catch (const ContinueSignal &) {
                continue;
            }
        }
        return ScriptValue();
    }
    case AstNodeType::FuncDef: {
        auto funcDef = std::static_pointer_cast<FuncDefNode>(node);
        ScriptFunction fn;
        fn.name = funcDef->name;
        fn.parameters = funcDef->parameters;
        fn.body = funcDef->body;
        fn.closure = env;

        std::vector<std::string> decs;
        for (const auto &decNode : funcDef->decorators) {
            decs.push_back(decNode->name);
            RegisteredHook hook;
            hook.functionName = funcDef->name;
            hook.function = fn;
            if (!decNode->arguments.empty()) {
                auto argVal = evaluate(decNode->arguments.front(), env);
                hook.argument = argVal.toString();
            }

            if (decNode->name == "on_chapter_change") {
                m_chapterChangeHooks.push_back(hook);
            } else if (decNode->name == "on_toolbar_click") {
                m_toolbarClickHooks.push_back(hook);
            } else if (decNode->name == "on_message") {
                m_messageHooks.push_back(hook);
            }
        }
        fn.decorators = decs;

        ScriptValue funcVal(fn);
        env->define(funcDef->name, funcVal, m_guard, funcDef->line, funcDef->column);
        return ScriptValue();
    }
    default:
        return ScriptValue();
    }
}

void ScriptEvaluator::executeBlock(const std::shared_ptr<BlockNode> &block, std::shared_ptr<Environment> env) {
    if (!block) return;
    for (const auto &stmt : block->statements) {
        evaluate(stmt, env);
    }
}

ScriptValue ScriptEvaluator::callFunction(const ScriptValue &callee, const std::vector<ScriptValue> &args, int line, int col) {
    if (!callee.isCallable()) {
        throw ScriptTrap("TypeError: '" + callee.typeName() + "' object is not callable", line, col);
    }

    if (callee.type() == ScriptValue::Type::NativeFunc) {
        return callee.asNativeFunc()(args, m_guard, line, col);
    }

    if (callee.type() == ScriptValue::Type::UserFunc) {
        CallFrameGuard frameGuard(m_guard, line, col);
        const auto &fn = callee.asUserFunc();

        if (args.size() != fn.parameters.size()) {
            throw ScriptTrap("TypeError: " + fn.name + "() takes " + std::to_string(fn.parameters.size()) +
                             " arguments (" + std::to_string(args.size()) + " given)", line, col);
        }

        auto localEnv = std::make_shared<Environment>(fn.closure);
        for (size_t i = 0; i < fn.parameters.size(); ++i) {
            localEnv->define(fn.parameters.at(i), args.at(i), m_guard, line, col);
        }

        try {
            auto blockNode = std::static_pointer_cast<BlockNode>(fn.body);
            executeBlock(blockNode, localEnv);
        } catch (const ReturnSignal &ret) {
            return ret.value;
        }

        return ScriptValue();
    }

    return ScriptValue();
}

void ScriptEvaluator::dispatchChapterChange(const QString &title, const QString &text, int chapterIndex) {
    m_guard.resetForHook();
    std::vector<ScriptValue> args;
    args.push_back(ScriptValue(title.toStdString()));
    args.push_back(ScriptValue(text.toStdString()));
    args.push_back(ScriptValue(chapterIndex));

    for (const auto &hook : m_chapterChangeHooks) {
        try {
            std::vector<ScriptValue> hookArgs;
            for (size_t i = 0; i < hook.function.parameters.size() && i < args.size(); ++i) {
                hookArgs.push_back(args.at(i));
            }
            callFunction(ScriptValue(hook.function), hookArgs, -1, -1);
        } catch (const ReturnSignal &) {
        }
    }
}

void ScriptEvaluator::dispatchToolbarClick(const QString &actionName) {
    m_guard.resetForHook();
    std::string act = actionName.toStdString();

    for (const auto &hook : m_toolbarClickHooks) {
        if (hook.argument.empty() || hook.argument == act) {
            try {
                std::vector<ScriptValue> args;
                if (!hook.function.parameters.empty()) {
                    args.push_back(ScriptValue(act));
                }
                callFunction(ScriptValue(hook.function), args, -1, -1);
            } catch (const ReturnSignal &) {
            }
        }
    }
}

void ScriptEvaluator::dispatchMessage(const QString &senderId, const QString &payload) {
    m_guard.resetForHook();
    std::vector<ScriptValue> args;
    args.push_back(ScriptValue(senderId.toStdString()));
    args.push_back(ScriptValue(payload.toStdString()));

    for (const auto &hook : m_messageHooks) {
        try {
            std::vector<ScriptValue> hookArgs;
            for (size_t i = 0; i < hook.function.parameters.size() && i < args.size(); ++i) {
                hookArgs.push_back(args.at(i));
            }
            callFunction(ScriptValue(hook.function), hookArgs, -1, -1);
        } catch (const ReturnSignal &) {
        }
    }
}

bool ScriptEvaluator::hasToolbarHook(const QString &actionName) const {
    std::string act = actionName.toStdString();
    for (const auto &hook : m_toolbarClickHooks) {
        if (hook.argument.empty() || hook.argument == act) {
            return true;
        }
    }
    return false;
}

QStringList ScriptEvaluator::registeredToolbarActions() const {
    QStringList list;
    for (const auto &hook : m_toolbarClickHooks) {
        if (!hook.argument.empty()) {
            list.append(QString::fromStdString(hook.argument));
        } else {
            list.append(QString::fromStdString(hook.functionName));
        }
    }
    list.removeDuplicates();
    return list;
}

} // namespace EnsemblePlugin
