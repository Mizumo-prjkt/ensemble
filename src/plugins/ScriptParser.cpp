#include "ScriptParser.h"
#include <sstream>

namespace EnsemblePlugin {

ScriptParser::ScriptParser(std::vector<Token> tokens)
    : m_tokens(std::move(tokens))
{
}

const Token &ScriptParser::peek(size_t offset) const {
    if (m_current + offset >= m_tokens.size()) {
        return m_tokens.back(); // EOF
    }
    return m_tokens.at(m_current + offset);
}

const Token &ScriptParser::previous() const {
    if (m_current == 0) return m_tokens.front();
    return m_tokens.at(m_current - 1);
}

bool ScriptParser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile;
}

const Token &ScriptParser::advance() {
    if (!isAtEnd()) {
        m_current++;
    }
    return previous();
}

bool ScriptParser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool ScriptParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

const Token &ScriptParser::consume(TokenType type, const std::string &errorMessage) {
    if (check(type)) return advance();
    const Token &tok = peek();
    throw ScriptTrap(errorMessage + " (got '" + tok.text + "')", tok.line, tok.column);
}

void ScriptParser::skipNewlines() {
    while (check(TokenType::Newline)) {
        advance();
    }
}

std::shared_ptr<ProgramNode> ScriptParser::parseProgram() {
    auto program = std::make_shared<ProgramNode>();
    if (!m_tokens.empty()) {
        program->line = m_tokens.front().line;
        program->column = m_tokens.front().column;
    }

    while (!isAtEnd()) {
        skipNewlines();
        if (isAtEnd()) break;
        auto stmt = parseDeclarationOrStatement();
        if (stmt) {
            program->statements.push_back(stmt);
        }
    }

    return program;
}

std::shared_ptr<AstNode> ScriptParser::parseDeclarationOrStatement() {
    if (check(TokenType::At)) {
        std::vector<std::shared_ptr<DecoratorNode>> decorators;
        while (check(TokenType::At)) {
            decorators.push_back(parseDecorator());
            skipNewlines();
        }
        if (!check(TokenType::KwDef)) {
            const Token &tok = peek();
            throw ScriptTrap("SyntaxError: expected 'def' after decorator", tok.line, tok.column);
        }
        return parseFuncDef(std::move(decorators));
    }

    if (check(TokenType::KwDef)) {
        return parseFuncDef({});
    }

    return parseStatement();
}

std::shared_ptr<DecoratorNode> ScriptParser::parseDecorator() {
    const Token &atTok = consume(TokenType::At, "Expected '@'");
    auto decorator = std::make_shared<DecoratorNode>();
    decorator->line = atTok.line;
    decorator->column = atTok.column;

    const Token &idTok = consume(TokenType::Identifier, "Expected identifier after '@'");
    decorator->name = idTok.text;

    if (match(TokenType::LParen)) {
        if (!check(TokenType::RParen)) {
            do {
                decorator->arguments.push_back(parseExpression());
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RParen, "Expected ')' after decorator arguments");
    }

    consume(TokenType::Newline, "Expected newline after decorator");
    return decorator;
}

std::shared_ptr<FuncDefNode> ScriptParser::parseFuncDef(std::vector<std::shared_ptr<DecoratorNode>> decorators) {
    const Token &defTok = consume(TokenType::KwDef, "Expected 'def'");
    auto func = std::make_shared<FuncDefNode>();
    func->line = defTok.line;
    func->column = defTok.column;
    func->decorators = std::move(decorators);

    const Token &nameTok = consume(TokenType::Identifier, "Expected function name");
    func->name = nameTok.text;

    consume(TokenType::LParen, "Expected '(' after function name");
    if (!check(TokenType::RParen)) {
        do {
            const Token &param = consume(TokenType::Identifier, "Expected parameter name");
            func->parameters.push_back(param.text);
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RParen, "Expected ')' after parameter list");
    consume(TokenType::Colon, "Expected ':' after function signature");
    consume(TokenType::Newline, "Expected newline after ':'");

    func->body = parseBlock();
    return func;
}

std::shared_ptr<BlockNode> ScriptParser::parseBlock() {
    const Token &indentTok = consume(TokenType::Indent, "Expected indented block");
    auto block = std::make_shared<BlockNode>();
    block->line = indentTok.line;
    block->column = indentTok.column;

    while (!check(TokenType::Dedent) && !isAtEnd()) {
        skipNewlines();
        if (check(TokenType::Dedent) || isAtEnd()) break;
        block->statements.push_back(parseStatement());
    }

    consume(TokenType::Dedent, "Expected dedent at end of block");
    return block;
}

std::shared_ptr<AstNode> ScriptParser::parseStatement() {
    skipNewlines();

    if (check(TokenType::KwIf)) {
        return parseIfStatement();
    }
    if (check(TokenType::KwFor)) {
        return parseForStatement();
    }
    if (check(TokenType::KwWhile)) {
        return parseWhileStatement();
    }
    if (check(TokenType::KwDef)) {
        return parseFuncDef({});
    }

    auto stmt = parseSimpleStatement();
    consume(TokenType::Newline, "Expected newline after statement");
    return stmt;
}

std::shared_ptr<AstNode> ScriptParser::parseSimpleStatement() {
    if (match(TokenType::KwReturn)) {
        auto ret = std::make_shared<ReturnStmtNode>();
        ret->line = previous().line;
        ret->column = previous().column;
        if (!check(TokenType::Newline) && !check(TokenType::EndOfFile)) {
            ret->value = parseExpression();
        }
        return ret;
    }

    if (match(TokenType::KwBreak)) {
        auto b = std::make_shared<BreakStmtNode>();
        b->line = previous().line;
        b->column = previous().column;
        return b;
    }

    if (match(TokenType::KwContinue)) {
        auto c = std::make_shared<ContinueStmtNode>();
        c->line = previous().line;
        c->column = previous().column;
        return c;
    }

    if (match(TokenType::KwPass)) {
        auto p = std::make_shared<PassStmtNode>();
        p->line = previous().line;
        p->column = previous().column;
        return p;
    }

    // Expression or Assignment
    auto expr = parseExpression();

    // Check for assignment operators: =, +=, -=
    if (check(TokenType::Assign) || check(TokenType::PlusAssign) || check(TokenType::MinusAssign)) {
        TokenType op = advance().type;
        auto val = parseExpression();

        if (expr->type() == AstNodeType::IdentifierExpr) {
            auto idExpr = std::static_pointer_cast<IdentifierExprNode>(expr);
            auto assign = std::make_shared<AssignStmtNode>();
            assign->line = idExpr->line;
            assign->column = idExpr->column;
            assign->name = idExpr->name;
            assign->op = op;
            assign->value = val;
            return assign;
        }

        if (expr->type() == AstNodeType::SubscriptExpr) {
            auto subExpr = std::static_pointer_cast<SubscriptExprNode>(expr);
            auto assign = std::make_shared<SubscriptAssignStmtNode>();
            assign->line = subExpr->line;
            assign->column = subExpr->column;
            assign->target = subExpr->target;
            assign->index = subExpr->index;
            assign->op = op;
            assign->value = val;
            return assign;
        }

        if (expr->type() == AstNodeType::MemberExpr) {
            auto memExpr = std::static_pointer_cast<MemberExprNode>(expr);
            auto assign = std::make_shared<MemberAssignStmtNode>();
            assign->line = memExpr->line;
            assign->column = memExpr->column;
            assign->target = memExpr->target;
            assign->member = memExpr->member;
            assign->op = op;
            assign->value = val;
            return assign;
        }

        throw ScriptTrap("SyntaxError: invalid target for assignment", expr->line, expr->column);
    }

    auto exprStmt = std::make_shared<ExprStmtNode>();
    exprStmt->line = expr->line;
    exprStmt->column = expr->column;
    exprStmt->expr = expr;
    return exprStmt;
}

std::shared_ptr<AstNode> ScriptParser::parseIfStatement() {
    const Token &ifTok = consume(TokenType::KwIf, "Expected 'if'");
    auto ifNode = std::make_shared<IfStmtNode>();
    ifNode->line = ifTok.line;
    ifNode->column = ifTok.column;

    auto cond = parseExpression();
    consume(TokenType::Colon, "Expected ':' after if condition");
    consume(TokenType::Newline, "Expected newline after ':'");
    auto body = parseBlock();
    ifNode->branches.push_back({cond, body});

    while (match(TokenType::KwElif)) {
        auto elifCond = parseExpression();
        consume(TokenType::Colon, "Expected ':' after elif condition");
        consume(TokenType::Newline, "Expected newline after ':'");
        auto elifBody = parseBlock();
        ifNode->branches.push_back({elifCond, elifBody});
    }

    if (match(TokenType::KwElse)) {
        consume(TokenType::Colon, "Expected ':' after else");
        consume(TokenType::Newline, "Expected newline after ':'");
        ifNode->elseBranch = parseBlock();
    }

    return ifNode;
}

std::shared_ptr<AstNode> ScriptParser::parseForStatement() {
    const Token &forTok = consume(TokenType::KwFor, "Expected 'for'");
    auto forNode = std::make_shared<ForStmtNode>();
    forNode->line = forTok.line;
    forNode->column = forTok.column;

    const Token &varTok = consume(TokenType::Identifier, "Expected variable name after 'for'");
    forNode->iteratorVar = varTok.text;

    consume(TokenType::KwIn, "Expected 'in' after for variable");
    forNode->iterable = parseExpression();
    consume(TokenType::Colon, "Expected ':' after for expression");
    consume(TokenType::Newline, "Expected newline after ':'");

    forNode->body = parseBlock();
    return forNode;
}

std::shared_ptr<AstNode> ScriptParser::parseWhileStatement() {
    const Token &whileTok = consume(TokenType::KwWhile, "Expected 'while'");
    auto whileNode = std::make_shared<WhileStmtNode>();
    whileNode->line = whileTok.line;
    whileNode->column = whileTok.column;

    whileNode->condition = parseExpression();
    consume(TokenType::Colon, "Expected ':' after while condition");
    consume(TokenType::Newline, "Expected newline after ':'");

    whileNode->body = parseBlock();
    return whileNode;
}

std::shared_ptr<AstNode> ScriptParser::parseExpression() {
    return parseLogicOr();
}

std::shared_ptr<AstNode> ScriptParser::parseLogicOr() {
    auto left = parseLogicAnd();

    while (match(TokenType::KwOr)) {
        const Token &opTok = previous();
        auto right = parseLogicAnd();
        auto bin = std::make_shared<BinaryExprNode>();
        bin->line = opTok.line;
        bin->column = opTok.column;
        bin->left = left;
        bin->op = opTok.type;
        bin->right = right;
        left = bin;
    }

    return left;
}

std::shared_ptr<AstNode> ScriptParser::parseLogicAnd() {
    auto left = parseEquality();

    while (match(TokenType::KwAnd)) {
        const Token &opTok = previous();
        auto right = parseEquality();
        auto bin = std::make_shared<BinaryExprNode>();
        bin->line = opTok.line;
        bin->column = opTok.column;
        bin->left = left;
        bin->op = opTok.type;
        bin->right = right;
        left = bin;
    }

    return left;
}

std::shared_ptr<AstNode> ScriptParser::parseEquality() {
    auto left = parseComparison();

    while (check(TokenType::Equal) || check(TokenType::NotEqual)) {
        const Token &opTok = advance();
        auto right = parseComparison();
        auto bin = std::make_shared<BinaryExprNode>();
        bin->line = opTok.line;
        bin->column = opTok.column;
        bin->left = left;
        bin->op = opTok.type;
        bin->right = right;
        left = bin;
    }

    return left;
}

std::shared_ptr<AstNode> ScriptParser::parseComparison() {
    auto left = parseTerm();

    while (check(TokenType::Less) || check(TokenType::Greater) ||
           check(TokenType::LessEqual) || check(TokenType::GreaterEqual) ||
           check(TokenType::KwIn)) {
        const Token &opTok = advance();
        auto right = parseTerm();
        auto bin = std::make_shared<BinaryExprNode>();
        bin->line = opTok.line;
        bin->column = opTok.column;
        bin->left = left;
        bin->op = opTok.type;
        bin->right = right;
        left = bin;
    }

    return left;
}

std::shared_ptr<AstNode> ScriptParser::parseTerm() {
    auto left = parseFactor();

    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        const Token &opTok = advance();
        auto right = parseFactor();
        auto bin = std::make_shared<BinaryExprNode>();
        bin->line = opTok.line;
        bin->column = opTok.column;
        bin->left = left;
        bin->op = opTok.type;
        bin->right = right;
        left = bin;
    }

    return left;
}

std::shared_ptr<AstNode> ScriptParser::parseFactor() {
    auto left = parseUnary();

    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        const Token &opTok = advance();
        auto right = parseUnary();
        auto bin = std::make_shared<BinaryExprNode>();
        bin->line = opTok.line;
        bin->column = opTok.column;
        bin->left = left;
        bin->op = opTok.type;
        bin->right = right;
        left = bin;
    }

    return left;
}

std::shared_ptr<AstNode> ScriptParser::parseUnary() {
    if (check(TokenType::KwNot) || check(TokenType::Minus)) {
        const Token &opTok = advance();
        auto expr = parseUnary();
        auto un = std::make_shared<UnaryExprNode>();
        un->line = opTok.line;
        un->column = opTok.column;
        un->op = opTok.type;
        un->expr = expr;
        return un;
    }

    return parsePostfix();
}

std::shared_ptr<AstNode> ScriptParser::parsePostfix() {
    auto target = parsePrimary();

    while (true) {
        if (match(TokenType::LParen)) {
            const Token &lp = previous();
            auto call = std::make_shared<CallExprNode>();
            call->line = lp.line;
            call->column = lp.column;
            call->callee = target;

            if (!check(TokenType::RParen)) {
                do {
                    call->arguments.push_back(parseExpression());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RParen, "Expected ')' after call arguments");
            target = call;
        } else if (match(TokenType::LBracket)) {
            const Token &lb = previous();
            auto sub = std::make_shared<SubscriptExprNode>();
            sub->line = lb.line;
            sub->column = lb.column;
            sub->target = target;
            sub->index = parseExpression();
            consume(TokenType::RBracket, "Expected ']' after index");
            target = sub;
        } else if (match(TokenType::Dot)) {
            const Token &dot = previous();
            const Token &idTok = consume(TokenType::Identifier, "Expected member name after '.'");
            if (idTok.text.rfind("__", 0) == 0) {
                throw ScriptTrap("SecurityError: access to double-underscore ('" + idTok.text + "') is forbidden", idTok.line, idTok.column);
            }
            auto mem = std::make_shared<MemberExprNode>();
            mem->line = dot.line;
            mem->column = dot.column;
            mem->target = target;
            mem->member = idTok.text;
            target = mem;
        } else {
            break;
        }
    }

    return target;
}

std::shared_ptr<AstNode> ScriptParser::parsePrimary() {
    const Token &tok = peek();

    if (match(TokenType::Number)) {
        auto num = std::make_shared<NumberExprNode>();
        num->line = tok.line;
        num->column = tok.column;
        num->value = tok.numberValue;
        return num;
    }

    if (match(TokenType::String)) {
        auto str = std::make_shared<StringExprNode>();
        str->line = tok.line;
        str->column = tok.column;
        str->value = tok.stringValue;
        return str;
    }

    if (match(TokenType::KwTrue)) {
        auto b = std::make_shared<BooleanExprNode>();
        b->line = tok.line;
        b->column = tok.column;
        b->value = true;
        return b;
    }

    if (match(TokenType::KwFalse)) {
        auto b = std::make_shared<BooleanExprNode>();
        b->line = tok.line;
        b->column = tok.column;
        b->value = false;
        return b;
    }

    if (match(TokenType::KwNone)) {
        auto n = std::make_shared<NoneExprNode>();
        n->line = tok.line;
        n->column = tok.column;
        return n;
    }

    if (match(TokenType::Identifier)) {
        auto id = std::make_shared<IdentifierExprNode>();
        id->line = tok.line;
        id->column = tok.column;
        id->name = tok.text;
        return id;
    }

    if (match(TokenType::LParen)) {
        auto expr = parseExpression();
        consume(TokenType::RParen, "Expected ')' after expression");
        return expr;
    }

    if (match(TokenType::LBracket)) {
        auto list = std::make_shared<ListExprNode>();
        list->line = tok.line;
        list->column = tok.column;

        if (!check(TokenType::RBracket)) {
            do {
                if (check(TokenType::RBracket)) break;
                list->elements.push_back(parseExpression());
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RBracket, "Expected ']' after list literal");
        return list;
    }

    if (match(TokenType::LBrace)) {
        auto dict = std::make_shared<DictExprNode>();
        dict->line = tok.line;
        dict->column = tok.column;

        if (!check(TokenType::RBrace)) {
            do {
                if (check(TokenType::RBrace)) break;
                auto key = parseExpression();
                consume(TokenType::Colon, "Expected ':' between key and value in dict literal");
                auto val = parseExpression();
                dict->entries.emplace_back(key, val);
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RBrace, "Expected '}' after dict literal");
        return dict;
    }

    throw ScriptTrap("SyntaxError: unexpected token '" + tok.text + "' in expression", tok.line, tok.column);
}

} // namespace EnsemblePlugin
