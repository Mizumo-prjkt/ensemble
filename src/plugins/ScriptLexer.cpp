#include "ScriptLexer.h"
#include <cctype>

namespace EnsemblePlugin {

ScriptLexer::ScriptLexer(std::string source)
    : m_source(std::move(source))
{
}

char ScriptLexer::peek(size_t offset) const {
    if (m_current + offset >= m_source.size()) return '\0';
    return m_source.at(m_current + offset);
}

char ScriptLexer::advance() {
    if (isAtEnd()) return '\0';
    char c = m_source.at(m_current++);
    m_column++;
    return c;
}

bool ScriptLexer::isAtEnd() const {
    return m_current >= m_source.size();
}

bool ScriptLexer::match(char expected) {
    if (isAtEnd()) return false;
    if (m_source.at(m_current) != expected) return false;
    m_current++;
    m_column++;
    return true;
}

std::string ScriptLexer::tokenTypeName(TokenType type) {
    switch (type) {
    case TokenType::EndOfFile: return "EOF";
    case TokenType::Newline: return "NEWLINE";
    case TokenType::Indent: return "INDENT";
    case TokenType::Dedent: return "DEDENT";
    case TokenType::Identifier: return "IDENTIFIER";
    case TokenType::Number: return "NUMBER";
    case TokenType::String: return "STRING";
    case TokenType::KwDef: return "def";
    case TokenType::KwReturn: return "return";
    case TokenType::KwIf: return "if";
    case TokenType::KwElif: return "elif";
    case TokenType::KwElse: return "else";
    case TokenType::KwFor: return "for";
    case TokenType::KwIn: return "in";
    case TokenType::KwWhile: return "while";
    case TokenType::KwBreak: return "break";
    case TokenType::KwContinue: return "continue";
    case TokenType::KwPass: return "pass";
    case TokenType::KwAnd: return "and";
    case TokenType::KwOr: return "or";
    case TokenType::KwNot: return "not";
    case TokenType::KwTrue: return "True";
    case TokenType::KwFalse: return "False";
    case TokenType::KwNone: return "None";
    case TokenType::At: return "@";
    case TokenType::Colon: return ":";
    case TokenType::Comma: return ",";
    case TokenType::Dot: return ".";
    case TokenType::LParen: return "(";
    case TokenType::RParen: return ")";
    case TokenType::LBracket: return "[";
    case TokenType::RBracket: return "]";
    case TokenType::LBrace: return "{";
    case TokenType::RBrace: return "}";
    case TokenType::Assign: return "=";
    case TokenType::PlusAssign: return "+=";
    case TokenType::MinusAssign: return "-=";
    case TokenType::Equal: return "==";
    case TokenType::NotEqual: return "!=";
    case TokenType::Less: return "<";
    case TokenType::Greater: return ">";
    case TokenType::LessEqual: return "<=";
    case TokenType::GreaterEqual: return ">=";
    case TokenType::Plus: return "+";
    case TokenType::Minus: return "-";
    case TokenType::Star: return "*";
    case TokenType::Slash: return "/";
    case TokenType::Percent: return "%";
    }
    return "UNKNOWN";
}

void ScriptLexer::scanLineStart(std::vector<Token> &tokens) {
    while (!isAtEnd() && m_atLineStart) {
        int indentSpaces = 0;
        size_t lookahead = m_current;
        int col = 1;

        while (lookahead < m_source.size()) {
            char c = m_source.at(lookahead);
            if (c == ' ') {
                indentSpaces++;
                lookahead++;
                col++;
            } else if (c == '\t') {
                indentSpaces += 4;
                lookahead++;
                col += 4;
            } else {
                break;
            }
        }

        // Empty line or full line comment
        if (lookahead < m_source.size() && (m_source.at(lookahead) == '\r' || m_source.at(lookahead) == '\n' || m_source.at(lookahead) == '#')) {
            m_current = lookahead;
            m_column = col;
            if (!isAtEnd() && m_source.at(m_current) == '#') {
                while (!isAtEnd() && peek() != '\n') {
                    advance();
                }
            }
            if (!isAtEnd() && peek() == '\r') advance();
            if (!isAtEnd() && peek() == '\n') {
                advance();
                m_line++;
                m_column = 1;
            }
            continue;
        }

        if (lookahead >= m_source.size()) {
            m_current = lookahead;
            m_atLineStart = false;
            break;
        }

        // We have code on this line
        m_current = lookahead;
        m_column = col;
        m_atLineStart = false;

        // Only calculate indentation if not inside brackets
        if (m_bracketDepth == 0) {
            int currentIndent = indentSpaces;
            int topIndent = m_indentStack.back();

            if (currentIndent > topIndent) {
                m_indentStack.push_back(currentIndent);
                tokens.push_back(Token{TokenType::Indent, "INDENT", 0.0, "", m_line, m_column});
            } else if (currentIndent < topIndent) {
                while (currentIndent < m_indentStack.back()) {
                    m_indentStack.pop_back();
                    tokens.push_back(Token{TokenType::Dedent, "DEDENT", 0.0, "", m_line, m_column});
                }
                if (currentIndent != m_indentStack.back()) {
                    throw ScriptTrap("IndentationError: unindent does not match any outer indentation level", m_line, m_column);
                }
            }
        }
        break;
    }
}

void ScriptLexer::scanString(char quoteChar, bool isTriple, std::vector<Token> &tokens) {
    int startLine = m_line;
    int startCol = m_column - (isTriple ? 3 : 1);
    std::string val;

    while (!isAtEnd()) {
        if (isTriple) {
            if (peek() == quoteChar && peek(1) == quoteChar && peek(2) == quoteChar) {
                advance();
                advance();
                advance();
                tokens.push_back(Token{TokenType::String, val, 0.0, val, startLine, startCol});
                return;
            }
        } else {
            if (peek() == quoteChar) {
                advance();
                tokens.push_back(Token{TokenType::String, val, 0.0, val, startLine, startCol});
                return;
            }
            if (peek() == '\n') {
                throw ScriptTrap("SyntaxError: EOL while scanning single-quoted string literal", startLine, startCol);
            }
        }

        char c = advance();
        if (c == '\\') {
            if (isAtEnd()) break;
            char esc = advance();
            switch (esc) {
            case 'n': val.push_back('\n'); break;
            case 't': val.push_back('\t'); break;
            case 'r': val.push_back('\r'); break;
            case '\\': val.push_back('\\'); break;
            case '\'': val.push_back('\''); break;
            case '"': val.push_back('"'); break;
            default:
                val.push_back('\\');
                val.push_back(esc);
                break;
            }
        } else {
            if (c == '\n') {
                m_line++;
                m_column = 1;
            }
            val.push_back(c);
        }
    }

    throw ScriptTrap("SyntaxError: EOF while scanning string literal", startLine, startCol);
}

void ScriptLexer::scanNumber(std::vector<Token> &tokens) {
    int startCol = m_column - 1;
    size_t startPos = m_current - 1;
    bool hasDot = (m_source.at(startPos) == '.');

    while (!isAtEnd()) {
        char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            advance();
        } else if (c == '.' && !hasDot && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            hasDot = true;
            advance();
        } else {
            break;
        }
    }

    std::string text = m_source.substr(startPos, m_current - startPos);
    double val = 0.0;
    try {
        val = std::stod(text);
    } catch (...) {
        throw ScriptTrap("SyntaxError: invalid number literal '" + text + "'", m_line, startCol);
    }
    tokens.push_back(Token{TokenType::Number, text, val, "", m_line, startCol});
}

void ScriptLexer::scanIdentifier(std::vector<Token> &tokens) {
    int startCol = m_column - 1;
    size_t startPos = m_current - 1;

    while (!isAtEnd()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            advance();
        } else {
            break;
        }
    }

    std::string id = m_source.substr(startPos, m_current - startPos);

    // Rule: Double underscore prohibited
    if (id.rfind("__", 0) == 0) {
        throw ScriptTrap("SecurityError: access to double-underscore ('" + id + "') is forbidden", m_line, startCol);
    }

    // Rule: import, eval, exec, open prohibited
    if (id == "import" || id == "eval" || id == "exec" || id == "open") {
        throw ScriptTrap("SecurityError: '" + id + "' is not permitted in sandboxed scripts", m_line, startCol);
    }

    // Check keywords
    TokenType type = TokenType::Identifier;
    if (id == "def") type = TokenType::KwDef;
    else if (id == "return") type = TokenType::KwReturn;
    else if (id == "if") type = TokenType::KwIf;
    else if (id == "elif") type = TokenType::KwElif;
    else if (id == "else") type = TokenType::KwElse;
    else if (id == "for") type = TokenType::KwFor;
    else if (id == "in") type = TokenType::KwIn;
    else if (id == "while") type = TokenType::KwWhile;
    else if (id == "break") type = TokenType::KwBreak;
    else if (id == "continue") type = TokenType::KwContinue;
    else if (id == "pass") type = TokenType::KwPass;
    else if (id == "and") type = TokenType::KwAnd;
    else if (id == "or") type = TokenType::KwOr;
    else if (id == "not") type = TokenType::KwNot;
    else if (id == "True") type = TokenType::KwTrue;
    else if (id == "False") type = TokenType::KwFalse;
    else if (id == "None") type = TokenType::KwNone;

    tokens.push_back(Token{type, id, 0.0, id, m_line, startCol});
}

void ScriptLexer::scanToken(std::vector<Token> &tokens) {
    char c = advance();
    int tokLine = m_line;
    int tokCol = m_column - 1;

    switch (c) {
    case ' ':
    case '\t':
    case '\r':
        // Ignored whitespace
        break;

    case '#':
        // Comment until newline
        while (!isAtEnd() && peek() != '\n') {
            advance();
        }
        break;

    case '\n':
        m_line++;
        m_column = 1;
        if (m_bracketDepth == 0) {
            if (!tokens.empty()) {
                TokenType last = tokens.back().type;
                if (last != TokenType::Newline && last != TokenType::Indent) {
                    tokens.push_back(Token{TokenType::Newline, "\\n", 0.0, "", tokLine, tokCol});
                }
            }
            m_atLineStart = true;
        }
        break;

    case '(':
        m_bracketDepth++;
        tokens.push_back(Token{TokenType::LParen, "(", 0.0, "", tokLine, tokCol});
        break;
    case ')':
        if (m_bracketDepth > 0) m_bracketDepth--;
        tokens.push_back(Token{TokenType::RParen, ")", 0.0, "", tokLine, tokCol});
        break;

    case '[':
        m_bracketDepth++;
        tokens.push_back(Token{TokenType::LBracket, "[", 0.0, "", tokLine, tokCol});
        break;
    case ']':
        if (m_bracketDepth > 0) m_bracketDepth--;
        tokens.push_back(Token{TokenType::RBracket, "]", 0.0, "", tokLine, tokCol});
        break;

    case '{':
        m_bracketDepth++;
        tokens.push_back(Token{TokenType::LBrace, "{", 0.0, "", tokLine, tokCol});
        break;
    case '}':
        if (m_bracketDepth > 0) m_bracketDepth--;
        tokens.push_back(Token{TokenType::RBrace, "}", 0.0, "", tokLine, tokCol});
        break;

    case '@':
        tokens.push_back(Token{TokenType::At, "@", 0.0, "", tokLine, tokCol});
        break;
    case ':':
        tokens.push_back(Token{TokenType::Colon, ":", 0.0, "", tokLine, tokCol});
        break;
    case ',':
        tokens.push_back(Token{TokenType::Comma, ",", 0.0, "", tokLine, tokCol});
        break;
    case '.':
        if (std::isdigit(static_cast<unsigned char>(peek()))) {
            scanNumber(tokens);
        } else {
            tokens.push_back(Token{TokenType::Dot, ".", 0.0, "", tokLine, tokCol});
        }
        break;

    case '+':
        if (match('=')) {
            tokens.push_back(Token{TokenType::PlusAssign, "+=", 0.0, "", tokLine, tokCol});
        } else {
            tokens.push_back(Token{TokenType::Plus, "+", 0.0, "", tokLine, tokCol});
        }
        break;

    case '-':
        if (match('=')) {
            tokens.push_back(Token{TokenType::MinusAssign, "-=", 0.0, "", tokLine, tokCol});
        } else {
            tokens.push_back(Token{TokenType::Minus, "-", 0.0, "", tokLine, tokCol});
        }
        break;

    case '*':
        tokens.push_back(Token{TokenType::Star, "*", 0.0, "", tokLine, tokCol});
        break;
    case '/':
        tokens.push_back(Token{TokenType::Slash, "/", 0.0, "", tokLine, tokCol});
        break;
    case '%':
        tokens.push_back(Token{TokenType::Percent, "%", 0.0, "", tokLine, tokCol});
        break;

    case '=':
        if (match('=')) {
            tokens.push_back(Token{TokenType::Equal, "==", 0.0, "", tokLine, tokCol});
        } else {
            tokens.push_back(Token{TokenType::Assign, "=", 0.0, "", tokLine, tokCol});
        }
        break;

    case '!':
        if (match('=')) {
            tokens.push_back(Token{TokenType::NotEqual, "!=", 0.0, "", tokLine, tokCol});
        } else {
            throw ScriptTrap("SyntaxError: unexpected character '!'", tokLine, tokCol);
        }
        break;

    case '<':
        if (match('=')) {
            tokens.push_back(Token{TokenType::LessEqual, "<=", 0.0, "", tokLine, tokCol});
        } else {
            tokens.push_back(Token{TokenType::Less, "<", 0.0, "", tokLine, tokCol});
        }
        break;

    case '>':
        if (match('=')) {
            tokens.push_back(Token{TokenType::GreaterEqual, ">=", 0.0, "", tokLine, tokCol});
        } else {
            tokens.push_back(Token{TokenType::Greater, ">", 0.0, "", tokLine, tokCol});
        }
        break;

    case '\'':
    case '"': {
        bool isTriple = false;
        if (peek() == c && peek(1) == c) {
            advance();
            advance();
            isTriple = true;
        }
        scanString(c, isTriple, tokens);
        break;
    }

    default:
        if (std::isdigit(static_cast<unsigned char>(c))) {
            scanNumber(tokens);
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            scanIdentifier(tokens);
        } else {
            throw ScriptTrap(std::string("SyntaxError: unexpected character '") + c + "'", tokLine, tokCol);
        }
        break;
    }
}

std::vector<Token> ScriptLexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        if (m_atLineStart) {
            scanLineStart(tokens);
            if (isAtEnd()) break;
        }
        scanToken(tokens);
    }

    // Emit final newline if last token was not newline
    if (!tokens.empty() && tokens.back().type != TokenType::Newline) {
        tokens.push_back(Token{TokenType::Newline, "\\n", 0.0, "", m_line, m_column});
    }

    // Dedent any remaining indentation levels
    while (m_indentStack.size() > 1) {
        m_indentStack.pop_back();
        tokens.push_back(Token{TokenType::Dedent, "DEDENT", 0.0, "", m_line, m_column});
    }

    tokens.push_back(Token{TokenType::EndOfFile, "EOF", 0.0, "", m_line, m_column});
    return tokens;
}

} // namespace EnsemblePlugin
