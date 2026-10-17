#pragma once

#include "ScriptLexer.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace EnsemblePlugin {

enum class AstNodeType {
    Program,
    Decorator,
    FuncDef,
    Block,
    AssignStmt,
    SubscriptAssignStmt,
    MemberAssignStmt,
    ReturnStmt,
    ExprStmt,
    BreakStmt,
    ContinueStmt,
    PassStmt,
    IfStmt,
    ForStmt,
    WhileStmt,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    SubscriptExpr,
    MemberExpr,
    IdentifierExpr,
    NumberExpr,
    StringExpr,
    BooleanExpr,
    NoneExpr,
    ListExpr,
    DictExpr
};

struct AstNode {
    virtual ~AstNode() = default;
    int line = 1;
    int column = 1;
    virtual AstNodeType type() const = 0;
};

struct DecoratorNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::Decorator; }
    std::string name;
    std::vector<std::shared_ptr<AstNode>> arguments;
};

struct BlockNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::Block; }
    std::vector<std::shared_ptr<AstNode>> statements;
};

struct FuncDefNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::FuncDef; }
    std::string name;
    std::vector<std::string> parameters;
    std::shared_ptr<BlockNode> body;
    std::vector<std::shared_ptr<DecoratorNode>> decorators;
};

struct AssignStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::AssignStmt; }
    std::string name;
    TokenType op = TokenType::Assign; // =, +=, -=
    std::shared_ptr<AstNode> value;
};

struct SubscriptAssignStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::SubscriptAssignStmt; }
    std::shared_ptr<AstNode> target;
    std::shared_ptr<AstNode> index;
    TokenType op = TokenType::Assign;
    std::shared_ptr<AstNode> value;
};

struct MemberAssignStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::MemberAssignStmt; }
    std::shared_ptr<AstNode> target;
    std::string member;
    TokenType op = TokenType::Assign;
    std::shared_ptr<AstNode> value;
};

struct ReturnStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::ReturnStmt; }
    std::shared_ptr<AstNode> value; // nullptr if return;
};

struct ExprStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::ExprStmt; }
    std::shared_ptr<AstNode> expr;
};

struct BreakStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::BreakStmt; }
};

struct ContinueStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::ContinueStmt; }
};

struct PassStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::PassStmt; }
};

struct IfBranch {
    std::shared_ptr<AstNode> condition;
    std::shared_ptr<BlockNode> body;
};

struct IfStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::IfStmt; }
    std::vector<IfBranch> branches;
    std::shared_ptr<BlockNode> elseBranch;
};

struct ForStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::ForStmt; }
    std::string iteratorVar;
    std::shared_ptr<AstNode> iterable;
    std::shared_ptr<BlockNode> body;
};

struct WhileStmtNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::WhileStmt; }
    std::shared_ptr<AstNode> condition;
    std::shared_ptr<BlockNode> body;
};

struct BinaryExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::BinaryExpr; }
    std::shared_ptr<AstNode> left;
    TokenType op = TokenType::Plus;
    std::shared_ptr<AstNode> right;
};

struct UnaryExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::UnaryExpr; }
    TokenType op = TokenType::Minus;
    std::shared_ptr<AstNode> expr;
};

struct CallExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::CallExpr; }
    std::shared_ptr<AstNode> callee;
    std::vector<std::shared_ptr<AstNode>> arguments;
};

struct SubscriptExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::SubscriptExpr; }
    std::shared_ptr<AstNode> target;
    std::shared_ptr<AstNode> index;
};

struct MemberExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::MemberExpr; }
    std::shared_ptr<AstNode> target;
    std::string member;
};

struct IdentifierExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::IdentifierExpr; }
    std::string name;
};

struct NumberExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::NumberExpr; }
    double value = 0.0;
};

struct StringExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::StringExpr; }
    std::string value;
};

struct BooleanExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::BooleanExpr; }
    bool value = false;
};

struct NoneExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::NoneExpr; }
};

struct ListExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::ListExpr; }
    std::vector<std::shared_ptr<AstNode>> elements;
};

struct DictExprNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::DictExpr; }
    std::vector<std::pair<std::shared_ptr<AstNode>, std::shared_ptr<AstNode>>> entries;
};

struct ProgramNode : public AstNode {
    AstNodeType type() const override { return AstNodeType::Program; }
    std::vector<std::shared_ptr<AstNode>> statements;
};

class ScriptParser {
public:
    explicit ScriptParser(std::vector<Token> tokens);

    std::shared_ptr<ProgramNode> parseProgram();

private:
    const Token &peek(size_t offset = 0) const;
    const Token &previous() const;
    bool isAtEnd() const;
    const Token &advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token &consume(TokenType type, const std::string &errorMessage);

    void skipNewlines();

    std::shared_ptr<AstNode> parseDeclarationOrStatement();
    std::shared_ptr<FuncDefNode> parseFuncDef(std::vector<std::shared_ptr<DecoratorNode>> decorators);
    std::shared_ptr<DecoratorNode> parseDecorator();
    std::shared_ptr<BlockNode> parseBlock();

    std::shared_ptr<AstNode> parseStatement();
    std::shared_ptr<AstNode> parseSimpleStatement();
    std::shared_ptr<AstNode> parseIfStatement();
    std::shared_ptr<AstNode> parseForStatement();
    std::shared_ptr<AstNode> parseWhileStatement();

    std::shared_ptr<AstNode> parseExpression();
    std::shared_ptr<AstNode> parseLogicOr();
    std::shared_ptr<AstNode> parseLogicAnd();
    std::shared_ptr<AstNode> parseEquality();
    std::shared_ptr<AstNode> parseComparison();
    std::shared_ptr<AstNode> parseTerm();
    std::shared_ptr<AstNode> parseFactor();
    std::shared_ptr<AstNode> parseUnary();
    std::shared_ptr<AstNode> parsePostfix();
    std::shared_ptr<AstNode> parsePrimary();

    std::vector<Token> m_tokens;
    size_t m_current = 0;
};

} // namespace EnsemblePlugin
