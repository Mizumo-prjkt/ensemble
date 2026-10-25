use crate::guard::ScriptTrap;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TokenType {
    // Structure
    Indent,
    Dedent,
    Newline,
    EndOfFile,

    // Literals & Identifiers
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

    // Operators & Delimiters
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Assign,
    PlusAssign,
    MinusAssign,
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Dot,
    At,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub token_type: TokenType,
    pub text: String,
    pub number_value: f64,
    pub string_value: String,
    pub line: usize,
    pub column: usize,
}

impl Token {
    pub fn new(token_type: TokenType, text: impl Into<String>, line: usize, column: usize) -> Self {
        Self {
            token_type,
            text: text.into(),
            number_value: 0.0,
            string_value: String::new(),
            line,
            column,
        }
    }
}

pub struct ScriptLexer {
    source: Vec<char>,
    pos: usize,
    line: usize,
    column: usize,
}

impl ScriptLexer {
    pub fn new(source: &str) -> Self {
        Self {
            source: source.chars().collect(),
            pos: 0,
            line: 1,
            column: 1,
        }
    }

    fn peek(&self, offset: usize) -> char {
        self.source.get(self.pos + offset).copied().unwrap_or('\0')
    }

    fn advance(&mut self) -> char {
        if self.pos >= self.source.len() {
            return '\0';
        }
        let c = self.source[self.pos];
        self.pos += 1;
        if c == '\n' {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
        c
    }

    fn is_at_end(&self) -> bool {
        self.pos >= self.source.len()
    }

    pub fn tokenize(&mut self) -> Result<Vec<Token>, ScriptTrap> {
        let mut tokens = Vec::new();
        let mut indent_stack: Vec<usize> = vec![0];
        let mut at_line_start = true;
        let mut paren_depth: i32 = 0;

        while !self.is_at_end() {
            if at_line_start && paren_depth == 0 {
                let mut indent = 0usize;
                while !self.is_at_end() {
                    let c = self.peek(0);
                    if c == ' ' {
                        indent += 1;
                        self.advance();
                    } else if c == '\t' {
                        indent += 4;
                        self.advance();
                    } else if c == '\r' {
                        self.advance();
                    } else {
                        break;
                    }
                }

                // Skip blank lines and comment-only lines
                if self.peek(0) == '\n' {
                    self.advance();
                    continue;
                }
                if self.peek(0) == '#' {
                    while !self.is_at_end() && self.peek(0) != '\n' {
                        self.advance();
                    }
                    if self.peek(0) == '\n' {
                        self.advance();
                    }
                    continue;
                }
                if self.is_at_end() {
                    break;
                }

                at_line_start = false;
                let current_indent = *indent_stack.last().unwrap_or(&0);
                if indent > current_indent {
                    indent_stack.push(indent);
                    tokens.push(Token::new(TokenType::Indent, "<INDENT>", self.line, 1));
                } else if indent < current_indent {
                    while indent_stack.len() > 1 && *indent_stack.last().unwrap() > indent {
                        indent_stack.pop();
                        tokens.push(Token::new(TokenType::Dedent, "<DEDENT>", self.line, 1));
                    }
                    if *indent_stack.last().unwrap() != indent {
                        return Err(ScriptTrap::new(
                            "IndentationError: unindent does not match any outer indentation level",
                            self.line,
                        ));
                    }
                }
            }

            let c = self.peek(0);
            if c == ' ' || c == '\t' || c == '\r' {
                self.advance();
                continue;
            }

            if c == '#' {
                while !self.is_at_end() && self.peek(0) != '\n' {
                    self.advance();
                }
                continue;
            }

            if c == '\n' {
                let line = self.line;
                let col = self.column;
                self.advance();
                if paren_depth == 0 {
                    if tokens
                        .last()
                        .map(|t| {
                            t.token_type != TokenType::Newline
                                && t.token_type != TokenType::Indent
                                && t.token_type != TokenType::Dedent
                        })
                        .unwrap_or(false)
                    {
                        tokens.push(Token::new(TokenType::Newline, "\\n", line, col));
                    }
                    at_line_start = true;
                }
                continue;
            }

            if c.is_ascii_digit() {
                tokens.push(self.read_number()?);
                continue;
            }

            if c == '"' || c == '\'' {
                tokens.push(self.read_string(c)?);
                continue;
            }

            if c.is_ascii_alphabetic() || c == '_' {
                tokens.push(self.read_identifier_or_keyword()?);
                continue;
            }

            let tok_line = self.line;
            let tok_col = self.column;
            self.advance();

            let mut emit = |tt: TokenType, txt: &str| {
                tokens.push(Token::new(tt, txt, tok_line, tok_col));
            };

            match c {
                '+' => {
                    if self.peek(0) == '=' {
                        self.advance();
                        emit(TokenType::PlusAssign, "+=");
                    } else {
                        emit(TokenType::Plus, "+");
                    }
                }
                '-' => {
                    if self.peek(0) == '=' {
                        self.advance();
                        emit(TokenType::MinusAssign, "-=");
                    } else {
                        emit(TokenType::Minus, "-");
                    }
                }
                '*' => emit(TokenType::Star, "*"),
                '/' => emit(TokenType::Slash, "/"),
                '%' => emit(TokenType::Percent, "%"),
                '=' => {
                    if self.peek(0) == '=' {
                        self.advance();
                        emit(TokenType::Equal, "==");
                    } else {
                        emit(TokenType::Assign, "=");
                    }
                }
                '!' => {
                    if self.peek(0) == '=' {
                        self.advance();
                        emit(TokenType::NotEqual, "!=");
                    } else {
                        return Err(ScriptTrap::new(
                            "SyntaxError: unexpected character '!' (use 'not' instead)",
                            tok_line,
                        ));
                    }
                }
                '<' => {
                    if self.peek(0) == '=' {
                        self.advance();
                        emit(TokenType::LessEqual, "<=");
                    } else {
                        emit(TokenType::Less, "<");
                    }
                }
                '>' => {
                    if self.peek(0) == '=' {
                        self.advance();
                        emit(TokenType::GreaterEqual, ">=");
                    } else {
                        emit(TokenType::Greater, ">");
                    }
                }
                '(' => {
                    paren_depth += 1;
                    emit(TokenType::LParen, "(");
                }
                ')' => {
                    if paren_depth > 0 {
                        paren_depth -= 1;
                    }
                    emit(TokenType::RParen, ")");
                }
                '[' => {
                    paren_depth += 1;
                    emit(TokenType::LBracket, "[");
                }
                ']' => {
                    if paren_depth > 0 {
                        paren_depth -= 1;
                    }
                    emit(TokenType::RBracket, "]");
                }
                '{' => {
                    paren_depth += 1;
                    emit(TokenType::LBrace, "{");
                }
                '}' => {
                    if paren_depth > 0 {
                        paren_depth -= 1;
                    }
                    emit(TokenType::RBrace, "}");
                }
                ',' => emit(TokenType::Comma, ","),
                ':' => emit(TokenType::Colon, ":"),
                '.' => emit(TokenType::Dot, "."),
                '@' => emit(TokenType::At, "@"),
                _ => {
                    return Err(ScriptTrap::new(
                        format!("SyntaxError: unexpected character '{}'", c),
                        tok_line,
                    ));
                }
            }
        }

        if tokens
            .last()
            .map(|t| {
                t.token_type != TokenType::Newline
                    && t.token_type != TokenType::Indent
                    && t.token_type != TokenType::Dedent
            })
            .unwrap_or(false)
        {
            tokens.push(Token::new(TokenType::Newline, "\\n", self.line, self.column));
        }

        while indent_stack.len() > 1 {
            indent_stack.pop();
            tokens.push(Token::new(
                TokenType::Dedent,
                "<DEDENT>",
                self.line,
                self.column,
            ));
        }

        tokens.push(Token::new(
            TokenType::EndOfFile,
            "<EOF>",
            self.line,
            self.column,
        ));
        Ok(tokens)
    }

    fn read_number(&mut self) -> Result<Token, ScriptTrap> {
        let start_line = self.line;
        let start_col = self.column;
        let mut text = String::new();
        let mut has_dot = false;

        while !self.is_at_end() {
            let c = self.peek(0);
            if c.is_ascii_digit() {
                text.push(self.advance());
            } else if c == '.' && !has_dot && self.peek(1).is_ascii_digit() {
                has_dot = true;
                text.push(self.advance());
            } else {
                break;
            }
        }

        let val: f64 = text.parse().map_err(|_| {
            ScriptTrap::new(format!("SyntaxError: invalid number '{}'", text), start_line)
        })?;
        let mut tok = Token::new(TokenType::Number, text, start_line, start_col);
        tok.number_value = val;
        Ok(tok)
    }

    fn read_string(&mut self, quote: char) -> Result<Token, ScriptTrap> {
        let start_line = self.line;
        let start_col = self.column;
        self.advance(); // opening quote
        let mut val = String::new();

        while !self.is_at_end() {
            let c = self.peek(0);
            if c == quote {
                self.advance();
                let mut tok = Token::new(TokenType::String, val.clone(), start_line, start_col);
                tok.string_value = val;
                return Ok(tok);
            }
            if c == '\n' {
                return Err(ScriptTrap::new(
                    "SyntaxError: unterminated string literal",
                    start_line,
                ));
            }
            if c == '\\' {
                self.advance();
                if self.is_at_end() {
                    return Err(ScriptTrap::new(
                        "SyntaxError: unterminated escape sequence",
                        start_line,
                    ));
                }
                let esc = self.advance();
                match esc {
                    'n' => val.push('\n'),
                    't' => val.push('\t'),
                    'r' => val.push('\r'),
                    '\\' => val.push('\\'),
                    '"' => val.push('"'),
                    '\'' => val.push('\''),
                    _ => val.push(esc),
                }
            } else {
                val.push(self.advance());
            }
        }

        Err(ScriptTrap::new(
            "SyntaxError: unterminated string literal at EOF",
            start_line,
        ))
    }

    fn read_identifier_or_keyword(&mut self) -> Result<Token, ScriptTrap> {
        let start_line = self.line;
        let start_col = self.column;
        let mut text = String::new();

        while !self.is_at_end() {
            let c = self.peek(0);
            if c.is_ascii_alphanumeric() || c == '_' {
                text.push(self.advance());
            } else {
                break;
            }
        }

        // Security check: forbid double-underscore identifiers
        if text.starts_with("__") {
            return Err(ScriptTrap::new(
                format!(
                    "SecurityError: double-underscore identifier '{}' is forbidden in sandboxed scripts",
                    text
                ),
                start_line,
            ));
        }

        // Security check: forbid dangerous Python keywords/builtins
        if matches!(
            text.as_str(),
            "import"
                | "from"
                | "exec"
                | "eval"
                | "compile"
                | "open"
                | "globals"
                | "locals"
                | "getattr"
                | "setattr"
                | "delattr"
        ) {
            return Err(ScriptTrap::new(
                format!(
                    "SecurityError: '{}' is forbidden in sandboxed Ensemble plugins",
                    text
                ),
                start_line,
            ));
        }

        let token_type = match text.as_str() {
            "def" => TokenType::KwDef,
            "return" => TokenType::KwReturn,
            "if" => TokenType::KwIf,
            "elif" => TokenType::KwElif,
            "else" => TokenType::KwElse,
            "for" => TokenType::KwFor,
            "in" => TokenType::KwIn,
            "while" => TokenType::KwWhile,
            "break" => TokenType::KwBreak,
            "continue" => TokenType::KwContinue,
            "pass" => TokenType::KwPass,
            "and" => TokenType::KwAnd,
            "or" => TokenType::KwOr,
            "not" => TokenType::KwNot,
            "True" => TokenType::KwTrue,
            "False" => TokenType::KwFalse,
            "None" => TokenType::KwNone,
            _ => TokenType::Identifier,
        };

        Ok(Token::new(token_type, text, start_line, start_col))
    }
}
