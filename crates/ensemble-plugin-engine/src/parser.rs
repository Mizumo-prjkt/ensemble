use crate::guard::ScriptTrap;
use crate::lexer::{Token, TokenType};
use std::sync::Arc;

#[derive(Debug, Clone)]
pub struct DecoratorNode {
    pub name: String,
    pub arguments: Vec<AstNode>,
    pub line: usize,
}

#[derive(Debug, Clone)]
pub struct BlockNode {
    pub statements: Vec<AstNode>,
    pub line: usize,
}

#[derive(Debug, Clone)]
pub struct FuncDefNode {
    pub name: String,
    pub parameters: Vec<String>,
    pub decorators: Vec<DecoratorNode>,
    pub body: BlockNode,
    pub line: usize,
}

#[derive(Debug, Clone)]
pub enum AstNode {
    Program {
        statements: Vec<AstNode>,
        line: usize,
    },
    Block(BlockNode),
    FuncDef(Arc<FuncDefNode>),
    IfStmt {
        branches: Vec<(AstNode, BlockNode)>,
        else_branch: Option<BlockNode>,
        line: usize,
    },
    ForStmt {
        iterator_var: String,
        iterable: Box<AstNode>,
        body: BlockNode,
        line: usize,
    },
    WhileStmt {
        condition: Box<AstNode>,
        body: BlockNode,
        line: usize,
    },
    ReturnStmt {
        value: Option<Box<AstNode>>,
        line: usize,
    },
    BreakStmt {
        line: usize,
    },
    ContinueStmt {
        line: usize,
    },
    PassStmt {
        line: usize,
    },
    AssignStmt {
        name: String,
        op: TokenType,
        value: Box<AstNode>,
        line: usize,
    },
    SubscriptAssignStmt {
        target: Box<AstNode>,
        index: Box<AstNode>,
        op: TokenType,
        value: Box<AstNode>,
        line: usize,
    },
    MemberAssignStmt {
        target: Box<AstNode>,
        member: String,
        op: TokenType,
        value: Box<AstNode>,
        line: usize,
    },
    ExprStmt {
        expr: Box<AstNode>,
        line: usize,
    },
    BinaryExpr {
        left: Box<AstNode>,
        op: TokenType,
        right: Box<AstNode>,
        line: usize,
    },
    UnaryExpr {
        op: TokenType,
        expr: Box<AstNode>,
        line: usize,
    },
    CallExpr {
        callee: Box<AstNode>,
        arguments: Vec<AstNode>,
        line: usize,
    },
    SubscriptExpr {
        target: Box<AstNode>,
        index: Box<AstNode>,
        line: usize,
    },
    MemberExpr {
        target: Box<AstNode>,
        member: String,
        line: usize,
    },
    IdentifierExpr {
        name: String,
        line: usize,
    },
    NumberExpr {
        value: f64,
        line: usize,
    },
    StringExpr {
        value: String,
        line: usize,
    },
    BooleanExpr {
        value: bool,
        line: usize,
    },
    NoneExpr {
        line: usize,
    },
    ListExpr {
        elements: Vec<AstNode>,
        line: usize,
    },
    DictExpr {
        entries: Vec<(AstNode, AstNode)>,
        line: usize,
    },
}

impl AstNode {
    pub fn line(&self) -> usize {
        match self {
            AstNode::Program { line, .. }
            | AstNode::IfStmt { line, .. }
            | AstNode::ForStmt { line, .. }
            | AstNode::WhileStmt { line, .. }
            | AstNode::ReturnStmt { line, .. }
            | AstNode::BreakStmt { line }
            | AstNode::ContinueStmt { line }
            | AstNode::PassStmt { line }
            | AstNode::AssignStmt { line, .. }
            | AstNode::SubscriptAssignStmt { line, .. }
            | AstNode::MemberAssignStmt { line, .. }
            | AstNode::ExprStmt { line, .. }
            | AstNode::BinaryExpr { line, .. }
            | AstNode::UnaryExpr { line, .. }
            | AstNode::CallExpr { line, .. }
            | AstNode::SubscriptExpr { line, .. }
            | AstNode::MemberExpr { line, .. }
            | AstNode::IdentifierExpr { line, .. }
            | AstNode::NumberExpr { line, .. }
            | AstNode::StringExpr { line, .. }
            | AstNode::BooleanExpr { line, .. }
            | AstNode::NoneExpr { line }
            | AstNode::ListExpr { line, .. }
            | AstNode::DictExpr { line, .. } => *line,
            AstNode::Block(b) => b.line,
            AstNode::FuncDef(f) => f.line,
        }
    }
}

pub struct ScriptParser {
    tokens: Vec<Token>,
    current: usize,
}

impl ScriptParser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, current: 0 }
    }

    fn peek(&self, offset: usize) -> &Token {
        let idx = (self.current + offset).min(self.tokens.len().saturating_sub(1));
        &self.tokens[idx]
    }

    fn previous(&self) -> &Token {
        if self.current == 0 {
            &self.tokens[0]
        } else {
            &self.tokens[self.current - 1]
        }
    }

    fn is_at_end(&self) -> bool {
        self.peek(0).token_type == TokenType::EndOfFile
    }

    fn advance(&mut self) -> Token {
        if !self.is_at_end() {
            self.current += 1;
        }
        self.previous().clone()
    }

    fn check(&self, token_type: TokenType) -> bool {
        if self.is_at_end() {
            return false;
        }
        self.peek(0).token_type == token_type
    }

    fn match_token(&mut self, token_type: TokenType) -> bool {
        if self.check(token_type) {
            self.advance();
            true
        } else {
            false
        }
    }

    fn consume(&mut self, token_type: TokenType, error_msg: &str) -> Result<Token, ScriptTrap> {
        if self.check(token_type) {
            return Ok(self.advance());
        }
        let tok = self.peek(0);
        Err(ScriptTrap::new(
            format!("{} (got '{}')", error_msg, tok.text),
            tok.line,
        ))
    }

    fn skip_newlines(&mut self) {
        while self.check(TokenType::Newline) {
            self.advance();
        }
    }

    pub fn parse_program(&mut self) -> Result<AstNode, ScriptTrap> {
        let line = self.tokens.first().map(|t| t.line).unwrap_or(1);
        let mut statements = Vec::new();

        while !self.is_at_end() {
            self.skip_newlines();
            if self.is_at_end() {
                break;
            }
            statements.push(self.parse_declaration_or_statement()?);
        }

        Ok(AstNode::Program { statements, line })
    }

    fn parse_declaration_or_statement(&mut self) -> Result<AstNode, ScriptTrap> {
        if self.check(TokenType::At) {
            let mut decorators = Vec::new();
            while self.check(TokenType::At) {
                decorators.push(self.parse_decorator()?);
                self.skip_newlines();
            }
            if !self.check(TokenType::KwDef) {
                let tok = self.peek(0);
                return Err(ScriptTrap::new(
                    "SyntaxError: expected 'def' after decorator",
                    tok.line,
                ));
            }
            return self.parse_func_def(decorators);
        }

        if self.check(TokenType::KwDef) {
            return self.parse_func_def(Vec::new());
        }

        self.parse_statement()
    }

    fn parse_decorator(&mut self) -> Result<DecoratorNode, ScriptTrap> {
        let at_tok = self.consume(TokenType::At, "Expected '@'")?;
        let id_tok = self.consume(TokenType::Identifier, "Expected identifier after '@'")?;
        let mut arguments = Vec::new();

        if self.match_token(TokenType::LParen) {
            if !self.check(TokenType::RParen) {
                loop {
                    arguments.push(self.parse_expression()?);
                    if !self.match_token(TokenType::Comma) {
                        break;
                    }
                }
            }
            self.consume(TokenType::RParen, "Expected ')' after decorator arguments")?;
        }

        self.consume(TokenType::Newline, "Expected newline after decorator")?;
        Ok(DecoratorNode {
            name: id_tok.text,
            arguments,
            line: at_tok.line,
        })
    }

    fn parse_func_def(&mut self, decorators: Vec<DecoratorNode>) -> Result<AstNode, ScriptTrap> {
        let def_tok = self.consume(TokenType::KwDef, "Expected 'def'")?;
        let name_tok = self.consume(TokenType::Identifier, "Expected function name")?;
        self.consume(TokenType::LParen, "Expected '(' after function name")?;

        let mut parameters = Vec::new();
        if !self.check(TokenType::RParen) {
            loop {
                let param = self.consume(TokenType::Identifier, "Expected parameter name")?;
                parameters.push(param.text);
                if !self.match_token(TokenType::Comma) {
                    break;
                }
            }
        }
        self.consume(TokenType::RParen, "Expected ')' after parameter list")?;
        self.consume(TokenType::Colon, "Expected ':' after function signature")?;
        self.consume(TokenType::Newline, "Expected newline after ':'")?;

        let body = self.parse_block()?;
        Ok(AstNode::FuncDef(Arc::new(FuncDefNode {
            name: name_tok.text,
            parameters,
            decorators,
            body,
            line: def_tok.line,
        })))
    }

    fn parse_block(&mut self) -> Result<BlockNode, ScriptTrap> {
        let indent_tok = self.consume(TokenType::Indent, "Expected indented block")?;
        let mut statements = Vec::new();

        while !self.check(TokenType::Dedent) && !self.is_at_end() {
            self.skip_newlines();
            if self.check(TokenType::Dedent) || self.is_at_end() {
                break;
            }
            statements.push(self.parse_statement()?);
        }

        self.consume(TokenType::Dedent, "Expected dedent at end of block")?;
        Ok(BlockNode {
            statements,
            line: indent_tok.line,
        })
    }

    fn parse_statement(&mut self) -> Result<AstNode, ScriptTrap> {
        self.skip_newlines();

        if self.check(TokenType::KwIf) {
            return self.parse_if_statement();
        }
        if self.check(TokenType::KwFor) {
            return self.parse_for_statement();
        }
        if self.check(TokenType::KwWhile) {
            return self.parse_while_statement();
        }
        if self.check(TokenType::KwDef) {
            return self.parse_func_def(Vec::new());
        }

        let stmt = self.parse_simple_statement()?;
        self.consume(TokenType::Newline, "Expected newline after statement")?;
        Ok(stmt)
    }

    fn parse_simple_statement(&mut self) -> Result<AstNode, ScriptTrap> {
        if self.match_token(TokenType::KwReturn) {
            let line = self.previous().line;
            let value = if !self.check(TokenType::Newline) && !self.check(TokenType::EndOfFile) {
                Some(Box::new(self.parse_expression()?))
            } else {
                None
            };
            return Ok(AstNode::ReturnStmt { value, line });
        }

        if self.match_token(TokenType::KwBreak) {
            return Ok(AstNode::BreakStmt {
                line: self.previous().line,
            });
        }

        if self.match_token(TokenType::KwContinue) {
            return Ok(AstNode::ContinueStmt {
                line: self.previous().line,
            });
        }

        if self.match_token(TokenType::KwPass) {
            return Ok(AstNode::PassStmt {
                line: self.previous().line,
            });
        }

        let expr = self.parse_expression()?;
        if self.check(TokenType::Assign)
            || self.check(TokenType::PlusAssign)
            || self.check(TokenType::MinusAssign)
        {
            let op = self.advance().token_type;
            let value = Box::new(self.parse_expression()?);
            let line = expr.line();

            return match expr {
                AstNode::IdentifierExpr { name, .. } => Ok(AstNode::AssignStmt {
                    name,
                    op,
                    value,
                    line,
                }),
                AstNode::SubscriptExpr { target, index, .. } => Ok(AstNode::SubscriptAssignStmt {
                    target,
                    index,
                    op,
                    value,
                    line,
                }),
                AstNode::MemberExpr { target, member, .. } => Ok(AstNode::MemberAssignStmt {
                    target,
                    member,
                    op,
                    value,
                    line,
                }),
                _ => Err(ScriptTrap::new(
                    "SyntaxError: invalid target for assignment",
                    line,
                )),
            };
        }

        let line = expr.line();
        Ok(AstNode::ExprStmt {
            expr: Box::new(expr),
            line,
        })
    }

    fn parse_if_statement(&mut self) -> Result<AstNode, ScriptTrap> {
        let if_tok = self.consume(TokenType::KwIf, "Expected 'if'")?;
        let mut branches = Vec::new();

        let cond = self.parse_expression()?;
        self.consume(TokenType::Colon, "Expected ':' after if condition")?;
        self.consume(TokenType::Newline, "Expected newline after ':'")?;
        let body = self.parse_block()?;
        branches.push((cond, body));

        while self.match_token(TokenType::KwElif) {
            let elif_cond = self.parse_expression()?;
            self.consume(TokenType::Colon, "Expected ':' after elif condition")?;
            self.consume(TokenType::Newline, "Expected newline after ':'")?;
            let elif_body = self.parse_block()?;
            branches.push((elif_cond, elif_body));
        }

        let else_branch = if self.match_token(TokenType::KwElse) {
            self.consume(TokenType::Colon, "Expected ':' after else")?;
            self.consume(TokenType::Newline, "Expected newline after ':'")?;
            Some(self.parse_block()?)
        } else {
            None
        };

        Ok(AstNode::IfStmt {
            branches,
            else_branch,
            line: if_tok.line,
        })
    }

    fn parse_for_statement(&mut self) -> Result<AstNode, ScriptTrap> {
        let for_tok = self.consume(TokenType::KwFor, "Expected 'for'")?;
        let var_tok =
            self.consume(TokenType::Identifier, "Expected variable name after 'for'")?;
        self.consume(TokenType::KwIn, "Expected 'in' after for variable")?;
        let iterable = Box::new(self.parse_expression()?);
        self.consume(TokenType::Colon, "Expected ':' after for expression")?;
        self.consume(TokenType::Newline, "Expected newline after ':'")?;
        let body = self.parse_block()?;

        Ok(AstNode::ForStmt {
            iterator_var: var_tok.text,
            iterable,
            body,
            line: for_tok.line,
        })
    }

    fn parse_while_statement(&mut self) -> Result<AstNode, ScriptTrap> {
        let while_tok = self.consume(TokenType::KwWhile, "Expected 'while'")?;
        let condition = Box::new(self.parse_expression()?);
        self.consume(TokenType::Colon, "Expected ':' after while condition")?;
        self.consume(TokenType::Newline, "Expected newline after ':'")?;
        let body = self.parse_block()?;

        Ok(AstNode::WhileStmt {
            condition,
            body,
            line: while_tok.line,
        })
    }

    pub fn parse_expression(&mut self) -> Result<AstNode, ScriptTrap> {
        self.parse_logic_or()
    }

    fn parse_logic_or(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut left = self.parse_logic_and()?;
        while self.match_token(TokenType::KwOr) {
            let op_tok = self.previous().clone();
            let right = self.parse_logic_and()?;
            left = AstNode::BinaryExpr {
                left: Box::new(left),
                op: op_tok.token_type,
                right: Box::new(right),
                line: op_tok.line,
            };
        }
        Ok(left)
    }

    fn parse_logic_and(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut left = self.parse_equality()?;
        while self.match_token(TokenType::KwAnd) {
            let op_tok = self.previous().clone();
            let right = self.parse_equality()?;
            left = AstNode::BinaryExpr {
                left: Box::new(left),
                op: op_tok.token_type,
                right: Box::new(right),
                line: op_tok.line,
            };
        }
        Ok(left)
    }

    fn parse_equality(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut left = self.parse_comparison()?;
        while self.check(TokenType::Equal) || self.check(TokenType::NotEqual) {
            let op_tok = self.advance();
            let right = self.parse_comparison()?;
            left = AstNode::BinaryExpr {
                left: Box::new(left),
                op: op_tok.token_type,
                right: Box::new(right),
                line: op_tok.line,
            };
        }
        Ok(left)
    }

    fn parse_comparison(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut left = self.parse_term()?;
        while self.check(TokenType::Less)
            || self.check(TokenType::Greater)
            || self.check(TokenType::LessEqual)
            || self.check(TokenType::GreaterEqual)
            || self.check(TokenType::KwIn)
        {
            let op_tok = self.advance();
            let right = self.parse_term()?;
            left = AstNode::BinaryExpr {
                left: Box::new(left),
                op: op_tok.token_type,
                right: Box::new(right),
                line: op_tok.line,
            };
        }
        Ok(left)
    }

    fn parse_term(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut left = self.parse_factor()?;
        while self.check(TokenType::Plus) || self.check(TokenType::Minus) {
            let op_tok = self.advance();
            let right = self.parse_factor()?;
            left = AstNode::BinaryExpr {
                left: Box::new(left),
                op: op_tok.token_type,
                right: Box::new(right),
                line: op_tok.line,
            };
        }
        Ok(left)
    }

    fn parse_factor(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut left = self.parse_unary()?;
        while self.check(TokenType::Star)
            || self.check(TokenType::Slash)
            || self.check(TokenType::Percent)
        {
            let op_tok = self.advance();
            let right = self.parse_unary()?;
            left = AstNode::BinaryExpr {
                left: Box::new(left),
                op: op_tok.token_type,
                right: Box::new(right),
                line: op_tok.line,
            };
        }
        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<AstNode, ScriptTrap> {
        if self.check(TokenType::KwNot) || self.check(TokenType::Minus) {
            let op_tok = self.advance();
            let expr = self.parse_unary()?;
            return Ok(AstNode::UnaryExpr {
                op: op_tok.token_type,
                expr: Box::new(expr),
                line: op_tok.line,
            });
        }
        self.parse_postfix()
    }

    fn parse_postfix(&mut self) -> Result<AstNode, ScriptTrap> {
        let mut target = self.parse_primary()?;

        loop {
            if self.match_token(TokenType::LParen) {
                let lp = self.previous().clone();
                let mut arguments = Vec::new();
                if !self.check(TokenType::RParen) {
                    loop {
                        arguments.push(self.parse_expression()?);
                        if !self.match_token(TokenType::Comma) {
                            break;
                        }
                    }
                }
                self.consume(TokenType::RParen, "Expected ')' after call arguments")?;
                target = AstNode::CallExpr {
                    callee: Box::new(target),
                    arguments,
                    line: lp.line,
                };
            } else if self.match_token(TokenType::LBracket) {
                let lb = self.previous().clone();
                let index = Box::new(self.parse_expression()?);
                self.consume(TokenType::RBracket, "Expected ']' after index")?;
                target = AstNode::SubscriptExpr {
                    target: Box::new(target),
                    index,
                    line: lb.line,
                };
            } else if self.match_token(TokenType::Dot) {
                let dot = self.previous().clone();
                let id_tok =
                    self.consume(TokenType::Identifier, "Expected member name after '.'")?;
                if id_tok.text.starts_with("__") {
                    return Err(ScriptTrap::new(
                        format!(
                            "SecurityError: access to double-underscore ('{}') is forbidden",
                            id_tok.text
                        ),
                        id_tok.line,
                    ));
                }
                target = AstNode::MemberExpr {
                    target: Box::new(target),
                    member: id_tok.text,
                    line: dot.line,
                };
            } else {
                break;
            }
        }

        Ok(target)
    }

    fn parse_primary(&mut self) -> Result<AstNode, ScriptTrap> {
        let tok = self.peek(0).clone();

        if self.match_token(TokenType::Number) {
            return Ok(AstNode::NumberExpr {
                value: tok.number_value,
                line: tok.line,
            });
        }
        if self.match_token(TokenType::String) {
            return Ok(AstNode::StringExpr {
                value: tok.string_value,
                line: tok.line,
            });
        }
        if self.match_token(TokenType::KwTrue) {
            return Ok(AstNode::BooleanExpr {
                value: true,
                line: tok.line,
            });
        }
        if self.match_token(TokenType::KwFalse) {
            return Ok(AstNode::BooleanExpr {
                value: false,
                line: tok.line,
            });
        }
        if self.match_token(TokenType::KwNone) {
            return Ok(AstNode::NoneExpr { line: tok.line });
        }
        if self.match_token(TokenType::Identifier) {
            return Ok(AstNode::IdentifierExpr {
                name: tok.text,
                line: tok.line,
            });
        }
        if self.match_token(TokenType::LParen) {
            let expr = self.parse_expression()?;
            self.consume(TokenType::RParen, "Expected ')' after expression")?;
            return Ok(expr);
        }
        if self.match_token(TokenType::LBracket) {
            let mut elements = Vec::new();
            if !self.check(TokenType::RBracket) {
                loop {
                    if self.check(TokenType::RBracket) {
                        break;
                    }
                    elements.push(self.parse_expression()?);
                    if !self.match_token(TokenType::Comma) {
                        break;
                    }
                }
            }
            self.consume(TokenType::RBracket, "Expected ']' after list literal")?;
            return Ok(AstNode::ListExpr {
                elements,
                line: tok.line,
            });
        }
        if self.match_token(TokenType::LBrace) {
            let mut entries = Vec::new();
            if !self.check(TokenType::RBrace) {
                loop {
                    if self.check(TokenType::RBrace) {
                        break;
                    }
                    let key = self.parse_expression()?;
                    self.consume(
                        TokenType::Colon,
                        "Expected ':' between key and value in dict literal",
                    )?;
                    let val = self.parse_expression()?;
                    entries.push((key, val));
                    if !self.match_token(TokenType::Comma) {
                        break;
                    }
                }
            }
            self.consume(TokenType::RBrace, "Expected '}' after dict literal")?;
            return Ok(AstNode::DictExpr {
                entries,
                line: tok.line,
            });
        }

        Err(ScriptTrap::new(
            format!("SyntaxError: unexpected token '{}' in expression", tok.text),
            tok.line,
        ))
    }
}
