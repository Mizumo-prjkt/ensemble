#pragma once

#include "ResourceGuard.h"
#include <string>
#include <vector>

namespace EnsemblePlugin {

enum class TokenType {
    EndOfFile,
    Newline,
    Indent,
    Dedent,

    Identifier,
    Number,
    String,

    // Keywords
    KwDef,
    KwReturn,
    KwIf,
    KwElif,
    KwElse,
    KwFor,
    KwIn,
    KwWhile,
    KwBreak,
    KwContinue,
    KwPass,
    KwAnd,
    KwOr,
    KwNot,
    KwTrue,
    KwFalse,
    KwNone,

    // Punctuation & Operators
    At,             // @
    Colon,          // :
    Comma,          // ,
    Dot,            // .
    LParen,         // (
    RParen,         // )
    LBracket,       // [
    RBracket,       // ]
    LBrace,         // {
    RBrace,         // }

    Assign,         // =
    PlusAssign,     // +=
    MinusAssign,    // -=

    Equal,          // ==
    NotEqual,       // !=
    Less,           // <
    Greater,        // >
    LessEqual,      // <=
    GreaterEqual,   // >=

    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Percent         // %
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string text;
    double numberValue = 0.0;
    std::string stringValue;
    int line = 1;
    int column = 1;
};

class ScriptLexer {
public:
    explicit ScriptLexer(std::string source);

    std::vector<Token> tokenize();

    static std::string tokenTypeName(TokenType type);

private:
    char peek(size_t offset = 0) const;
    char advance();
    bool isAtEnd() const;
    bool match(char expected);

    void scanLineStart(std::vector<Token> &tokens);
    void scanToken(std::vector<Token> &tokens);
    void scanString(char quoteChar, bool isTriple, std::vector<Token> &tokens);
    void scanNumber(std::vector<Token> &tokens);
    void scanIdentifier(std::vector<Token> &tokens);

    std::string m_source;
    size_t m_current = 0;
    int m_line = 1;
    int m_column = 1;
    int m_bracketDepth = 0;
    bool m_atLineStart = true;
    std::vector<int> m_indentStack = {0};
};

} // namespace EnsemblePlugin
