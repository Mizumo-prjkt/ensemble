use crate::guard::{ResourceGuard, ScriptTrap};
use crate::lexer::TokenType;
use crate::parser::{AstNode, BlockNode, FuncDefNode};
use crate::value::ScriptValue;
use parking_lot::Mutex;
use std::collections::BTreeMap;
use std::sync::Arc;

pub struct Environment {
    parent: Option<Arc<Mutex<Environment>>>,
    values: BTreeMap<String, ScriptValue>,
}

impl Environment {
    pub fn new(parent: Option<Arc<Mutex<Environment>>>) -> Self {
        Self {
            parent,
            values: BTreeMap::new(),
        }
    }

    pub fn define(&mut self, name: impl Into<String>, val: ScriptValue) {
        self.values.insert(name.into(), val);
    }

    pub fn assign(&mut self, name: &str, val: ScriptValue) -> bool {
        if self.values.contains_key(name) {
            self.values.insert(name.to_string(), val);
            return true;
        }
        if let Some(ref parent) = self.parent {
            if parent.lock().assign(name, val.clone()) {
                return true;
            }
        }
        self.values.insert(name.to_string(), val);
        true
    }

    pub fn get(&self, name: &str, line: usize) -> Result<ScriptValue, ScriptTrap> {
        if let Some(v) = self.values.get(name) {
            return Ok(v.clone());
        }
        if let Some(ref parent) = self.parent {
            return parent.lock().get(name, line);
        }
        Err(ScriptTrap::new(
            format!("NameError: name '{}' is not defined", name),
            line,
        ))
    }

    pub fn has(&self, name: &str) -> bool {
        if self.values.contains_key(name) {
            return true;
        }
        if let Some(ref parent) = self.parent {
            return parent.lock().has(name);
        }
        false
    }
}

enum ControlFlow {
    None,
    Return(ScriptValue),
    Break,
    Continue,
}

pub struct ScriptEvaluator {
    global_env: Arc<Mutex<Environment>>,
    on_chapter_change_funcs: Vec<Arc<FuncDefNode>>,
    on_toolbar_click_funcs: Vec<(String, Arc<FuncDefNode>)>,
    on_message_funcs: Vec<Arc<FuncDefNode>>,
}

impl ScriptEvaluator {
    pub fn new(global_env: Arc<Mutex<Environment>>) -> Self {
        Self {
            global_env,
            on_chapter_change_funcs: Vec::new(),
            on_toolbar_click_funcs: Vec::new(),
            on_message_funcs: Vec::new(),
        }
    }

    pub fn global_env(&self) -> &Arc<Mutex<Environment>> {
        &self.global_env
    }

    pub fn on_chapter_change_funcs(&self) -> &[Arc<FuncDefNode>] {
        &self.on_chapter_change_funcs
    }

    pub fn on_toolbar_click_funcs(&self) -> &[(String, Arc<FuncDefNode>)] {
        &self.on_toolbar_click_funcs
    }

    pub fn on_message_funcs(&self) -> &[Arc<FuncDefNode>] {
        &self.on_message_funcs
    }

    pub fn execute_program(
        &mut self,
        program: &AstNode,
        guard: &mut ResourceGuard,
    ) -> Result<(), ScriptTrap> {
        if let AstNode::Program { statements, .. } = program {
            let env = Arc::clone(&self.global_env);
            for stmt in statements {
                self.exec_stmt(stmt, &env, guard)?;
            }
        }
        Ok(())
    }

    pub fn invoke_function(
        &mut self,
        func: &Arc<FuncDefNode>,
        args: &[ScriptValue],
        guard: &mut ResourceGuard,
    ) -> Result<ScriptValue, ScriptTrap> {
        if args.len() != func.parameters.len() {
            return Err(ScriptTrap::new(
                format!(
                    "TypeError: {}() takes {} positional arguments but {} were given",
                    func.name,
                    func.parameters.len(),
                    args.len()
                ),
                func.line,
            ));
        }

        guard.enter_frame(func.line)?;
        let local_env = Arc::new(Mutex::new(Environment::new(Some(Arc::clone(
            &self.global_env,
        )))));
        {
            let mut env_lock = local_env.lock();
            for (param, arg) in func.parameters.iter().zip(args.iter()) {
                guard.alloc_bytes(arg.estimated_bytes(), func.line)?;
                env_lock.define(param.clone(), arg.clone());
            }
        }

        let res = self.exec_block(&func.body, &local_env, guard);
        guard.leave_frame();

        match res? {
            ControlFlow::Return(val) => Ok(val),
            _ => Ok(ScriptValue::None),
        }
    }

    fn exec_block(
        &mut self,
        block: &BlockNode,
        env: &Arc<Mutex<Environment>>,
        guard: &mut ResourceGuard,
    ) -> Result<ControlFlow, ScriptTrap> {
        for stmt in &block.statements {
            let flow = self.exec_stmt(stmt, env, guard)?;
            if !matches!(flow, ControlFlow::None) {
                return Ok(flow);
            }
        }
        Ok(ControlFlow::None)
    }

    fn exec_stmt(
        &mut self,
        node: &AstNode,
        env: &Arc<Mutex<Environment>>,
        guard: &mut ResourceGuard,
    ) -> Result<ControlFlow, ScriptTrap> {
        guard.step(node.line())?;

        match node {
            AstNode::PassStmt { .. } => Ok(ControlFlow::None),
            AstNode::BreakStmt { .. } => Ok(ControlFlow::Break),
            AstNode::ContinueStmt { .. } => Ok(ControlFlow::Continue),
            AstNode::ReturnStmt { value, .. } => {
                let val = if let Some(expr) = value {
                    self.eval_expr(expr, env, guard)?
                } else {
                    ScriptValue::None
                };
                Ok(ControlFlow::Return(val))
            }
            AstNode::FuncDef(fn_node) => {
                let fn_val = ScriptValue::from_user_func(Arc::clone(fn_node));
                env.lock().define(fn_node.name.clone(), fn_val);

                for dec in &fn_node.decorators {
                    if dec.name == "on_chapter_change" {
                        self.on_chapter_change_funcs.push(Arc::clone(fn_node));
                    } else if dec.name == "on_toolbar_click" {
                        let mut label = fn_node.name.clone();
                        if let Some(first_arg) = dec.arguments.first() {
                            let v = self.eval_expr(first_arg, env, guard)?;
                            label = v.as_string();
                        }
                        self.on_toolbar_click_funcs
                            .push((label, Arc::clone(fn_node)));
                    } else if dec.name == "on_message" {
                        self.on_message_funcs.push(Arc::clone(fn_node));
                    } else {
                        return Err(ScriptTrap::new(
                            format!("SyntaxError: unknown decorator '@{}'", dec.name),
                            dec.line,
                        ));
                    }
                }
                Ok(ControlFlow::None)
            }
            AstNode::AssignStmt {
                name,
                op,
                value,
                line,
            } => {
                let mut val = self.eval_expr(value, env, guard)?;
                if *op != TokenType::Assign {
                    let cur = env.lock().get(name, *line)?;
                    val = self.apply_compound_op(*op, &cur, &val, *line, guard)?;
                }
                guard.alloc_bytes(val.estimated_bytes(), *line)?;
                env.lock().assign(name, val);
                Ok(ControlFlow::None)
            }
            AstNode::SubscriptAssignStmt {
                target,
                index,
                op,
                value,
                line,
            } => {
                let target_val = self.eval_expr(target, env, guard)?;
                let idx_val = self.eval_expr(index, env, guard)?;
                let mut val = self.eval_expr(value, env, guard)?;

                match target_val {
                    ScriptValue::List(list_arc) => {
                        let mut list = list_arc.lock();
                        let mut i = idx_val.as_number() as isize;
                        if i < 0 {
                            i += list.len() as isize;
                        }
                        if i < 0 || (i as usize) >= list.len() {
                            return Err(ScriptTrap::new(
                                "IndexError: list assignment index out of range",
                                *line,
                            ));
                        }
                        let idx = i as usize;
                        if *op != TokenType::Assign {
                            val = self.apply_compound_op(*op, &list[idx], &val, *line, guard)?;
                        }
                        list[idx] = val;
                        Ok(ControlFlow::None)
                    }
                    ScriptValue::Dict(dict_arc) => {
                        let key = idx_val.as_string();
                        let mut dict = dict_arc.lock();
                        if *op != TokenType::Assign {
                            let cur = dict.get(&key).cloned().ok_or_else(|| {
                                ScriptTrap::new(format!("KeyError: '{}'", key), *line)
                            })?;
                            val = self.apply_compound_op(*op, &cur, &val, *line, guard)?;
                        }
                        guard.alloc_bytes(key.len() + val.estimated_bytes(), *line)?;
                        dict.insert(key, val);
                        Ok(ControlFlow::None)
                    }
                    _ => Err(ScriptTrap::new(
                        "TypeError: object does not support item assignment",
                        *line,
                    )),
                }
            }
            AstNode::MemberAssignStmt {
                target,
                member,
                op,
                value,
                line,
            } => {
                let target_val = self.eval_expr(target, env, guard)?;
                let mut val = self.eval_expr(value, env, guard)?;
                if let ScriptValue::Dict(dict_arc) = target_val {
                    let mut dict = dict_arc.lock();
                    if *op != TokenType::Assign {
                        let cur = dict.get(member).cloned().ok_or_else(|| {
                            ScriptTrap::new(
                                format!("AttributeError: '{}' not found", member),
                                *line,
                            )
                        })?;
                        val = self.apply_compound_op(*op, &cur, &val, *line, guard)?;
                    }
                    dict.insert(member.clone(), val);
                    Ok(ControlFlow::None)
                } else {
                    Err(ScriptTrap::new(
                        "TypeError: object does not support attribute assignment",
                        *line,
                    ))
                }
            }
            AstNode::ExprStmt { expr, .. } => {
                let _ = self.eval_expr(expr, env, guard)?;
                Ok(ControlFlow::None)
            }
            AstNode::IfStmt {
                branches,
                else_branch,
                ..
            } => {
                for (cond_expr, body) in branches {
                    let cond = self.eval_expr(cond_expr, env, guard)?;
                    if cond.is_truthy() {
                        return self.exec_block(body, env, guard);
                    }
                }
                if let Some(else_body) = else_branch {
                    return self.exec_block(else_body, env, guard);
                }
                Ok(ControlFlow::None)
            }
            AstNode::ForStmt {
                iterator_var,
                iterable,
                body,
                line,
            } => {
                let iter_val = self.eval_expr(iterable, env, guard)?;
                let items: Vec<ScriptValue> = match iter_val {
                    ScriptValue::List(l) => l.lock().clone(),
                    ScriptValue::String(s) => s
                        .chars()
                        .map(|c| ScriptValue::from_string(c.to_string()))
                        .collect(),
                    ScriptValue::Dict(d) => d
                        .lock()
                        .keys()
                        .map(|k| ScriptValue::from_string(k.clone()))
                        .collect(),
                    _ => {
                        return Err(ScriptTrap::new(
                            "TypeError: object is not iterable in 'for' loop",
                            *line,
                        ));
                    }
                };

                for item in items {
                    guard.step(*line)?;
                    env.lock().assign(iterator_var, item);
                    match self.exec_block(body, env, guard)? {
                        ControlFlow::Break => break,
                        ControlFlow::Continue => continue,
                        ControlFlow::Return(v) => return Ok(ControlFlow::Return(v)),
                        ControlFlow::None => {}
                    }
                }
                Ok(ControlFlow::None)
            }
            AstNode::WhileStmt {
                condition,
                body,
                line,
            } => {
                loop {
                    guard.step(*line)?;
                    let cond = self.eval_expr(condition, env, guard)?;
                    if !cond.is_truthy() {
                        break;
                    }
                    match self.exec_block(body, env, guard)? {
                        ControlFlow::Break => break,
                        ControlFlow::Continue => continue,
                        ControlFlow::Return(v) => return Ok(ControlFlow::Return(v)),
                        ControlFlow::None => {}
                    }
                }
                Ok(ControlFlow::None)
            }
            AstNode::Block(b) => self.exec_block(b, env, guard),
            _ => Ok(ControlFlow::None),
        }
    }

    fn apply_compound_op(
        &self,
        op: TokenType,
        cur: &ScriptValue,
        val: &ScriptValue,
        line: usize,
        guard: &mut ResourceGuard,
    ) -> Result<ScriptValue, ScriptTrap> {
        match op {
            TokenType::PlusAssign => {
                if cur.is_string() || val.is_string() {
                    let combined = format!("{}{}", cur.as_string(), val.as_string());
                    guard.alloc_bytes(combined.len(), line)?;
                    Ok(ScriptValue::from_string(combined))
                } else {
                    Ok(ScriptValue::from_number(cur.as_number() + val.as_number()))
                }
            }
            TokenType::MinusAssign => {
                Ok(ScriptValue::from_number(cur.as_number() - val.as_number()))
            }
            _ => Ok(val.clone()),
        }
    }

    fn eval_expr(
        &mut self,
        node: &AstNode,
        env: &Arc<Mutex<Environment>>,
        guard: &mut ResourceGuard,
    ) -> Result<ScriptValue, ScriptTrap> {
        guard.step(node.line())?;

        match node {
            AstNode::NoneExpr { .. } => Ok(ScriptValue::None),
            AstNode::BooleanExpr { value, .. } => Ok(ScriptValue::from_bool(*value)),
            AstNode::NumberExpr { value, .. } => Ok(ScriptValue::from_number(*value)),
            AstNode::StringExpr { value, line } => {
                guard.alloc_bytes(value.len(), *line)?;
                Ok(ScriptValue::from_string(value.clone()))
            }
            AstNode::IdentifierExpr { name, line } => env.lock().get(name, *line),
            AstNode::ListExpr { elements, line } => {
                let mut list = Vec::with_capacity(elements.len());
                for el in elements {
                    let v = self.eval_expr(el, env, guard)?;
                    guard.alloc_bytes(v.estimated_bytes(), *line)?;
                    list.push(v);
                }
                Ok(ScriptValue::from_list(list))
            }
            AstNode::DictExpr { entries, line } => {
                let mut dict = BTreeMap::new();
                for (k_expr, v_expr) in entries {
                    let k = self.eval_expr(k_expr, env, guard)?.as_string();
                    let v = self.eval_expr(v_expr, env, guard)?;
                    guard.alloc_bytes(k.len() + v.estimated_bytes(), *line)?;
                    dict.insert(k, v);
                }
                Ok(ScriptValue::from_dict(dict))
            }
            AstNode::UnaryExpr { op, expr, .. } => {
                let right = self.eval_expr(expr, env, guard)?;
                match op {
                    TokenType::KwNot => Ok(ScriptValue::from_bool(!right.is_truthy())),
                    TokenType::Minus => Ok(ScriptValue::from_number(-right.as_number())),
                    _ => Ok(right),
                }
            }
            AstNode::BinaryExpr {
                left,
                op,
                right,
                line,
            } => {
                if *op == TokenType::KwAnd {
                    let l = self.eval_expr(left, env, guard)?;
                    if !l.is_truthy() {
                        return Ok(l);
                    }
                    return self.eval_expr(right, env, guard);
                }
                if *op == TokenType::KwOr {
                    let l = self.eval_expr(left, env, guard)?;
                    if l.is_truthy() {
                        return Ok(l);
                    }
                    return self.eval_expr(right, env, guard);
                }

                let l = self.eval_expr(left, env, guard)?;
                let r = self.eval_expr(right, env, guard)?;

                match op {
                    TokenType::Plus => {
                        if l.is_string() || r.is_string() {
                            let s = format!("{}{}", l.as_string(), r.as_string());
                            guard.alloc_bytes(s.len(), *line)?;
                            return Ok(ScriptValue::from_string(s));
                        }
                        if let (ScriptValue::List(la), ScriptValue::List(lb)) = (&l, &r) {
                            let mut combined = la.lock().clone();
                            combined.extend(lb.lock().iter().cloned());
                            let val = ScriptValue::from_list(combined);
                            guard.alloc_bytes(val.estimated_bytes(), *line)?;
                            return Ok(val);
                        }
                        Ok(ScriptValue::from_number(l.as_number() + r.as_number()))
                    }
                    TokenType::Minus => Ok(ScriptValue::from_number(l.as_number() - r.as_number())),
                    TokenType::Star => {
                        if l.is_string() && r.is_number() {
                            let count = (r.as_number() as isize).max(0) as usize;
                            let base = l.as_string();
                            guard.alloc_bytes(base.len().saturating_mul(count), *line)?;
                            return Ok(ScriptValue::from_string(base.repeat(count)));
                        }
                        Ok(ScriptValue::from_number(l.as_number() * r.as_number()))
                    }
                    TokenType::Slash => {
                        let denom = r.as_number();
                        if denom == 0.0 {
                            return Err(ScriptTrap::new(
                                "ZeroDivisionError: division by zero",
                                *line,
                            ));
                        }
                        Ok(ScriptValue::from_number(l.as_number() / denom))
                    }
                    TokenType::Percent => {
                        let denom = r.as_number() as i64;
                        if denom == 0 {
                            return Err(ScriptTrap::new(
                                "ZeroDivisionError: modulo by zero",
                                *line,
                            ));
                        }
                        Ok(ScriptValue::from_number(
                            ((l.as_number() as i64) % denom) as f64,
                        ))
                    }
                    TokenType::Equal => Ok(ScriptValue::from_bool(l.equals(&r))),
                    TokenType::NotEqual => Ok(ScriptValue::from_bool(!l.equals(&r))),
                    TokenType::Less => {
                        if l.is_string() && r.is_string() {
                            Ok(ScriptValue::from_bool(l.as_string() < r.as_string()))
                        } else {
                            Ok(ScriptValue::from_bool(l.as_number() < r.as_number()))
                        }
                    }
                    TokenType::Greater => {
                        if l.is_string() && r.is_string() {
                            Ok(ScriptValue::from_bool(l.as_string() > r.as_string()))
                        } else {
                            Ok(ScriptValue::from_bool(l.as_number() > r.as_number()))
                        }
                    }
                    TokenType::LessEqual => {
                        if l.is_string() && r.is_string() {
                            Ok(ScriptValue::from_bool(l.as_string() <= r.as_string()))
                        } else {
                            Ok(ScriptValue::from_bool(l.as_number() <= r.as_number()))
                        }
                    }
                    TokenType::GreaterEqual => {
                        if l.is_string() && r.is_string() {
                            Ok(ScriptValue::from_bool(l.as_string() >= r.as_string()))
                        } else {
                            Ok(ScriptValue::from_bool(l.as_number() >= r.as_number()))
                        }
                    }
                    TokenType::KwIn => match &r {
                        ScriptValue::String(s) => {
                            Ok(ScriptValue::from_bool(s.contains(&l.as_string())))
                        }
                        ScriptValue::List(list_arc) => {
                            let found = list_arc.lock().iter().any(|item| item.equals(&l));
                            Ok(ScriptValue::from_bool(found))
                        }
                        ScriptValue::Dict(dict_arc) => Ok(ScriptValue::from_bool(
                            dict_arc.lock().contains_key(&l.as_string()),
                        )),
                        _ => Err(ScriptTrap::new(
                            "TypeError: right side of 'in' is not iterable",
                            *line,
                        )),
                    },
                    _ => Err(ScriptTrap::new("SyntaxError: unknown binary operator", *line)),
                }
            }
            AstNode::SubscriptExpr {
                target,
                index,
                line,
            } => {
                let target_val = self.eval_expr(target, env, guard)?;
                let idx_val = self.eval_expr(index, env, guard)?;

                match target_val {
                    ScriptValue::List(list_arc) => {
                        let list = list_arc.lock();
                        let mut i = idx_val.as_number() as isize;
                        if i < 0 {
                            i += list.len() as isize;
                        }
                        if i < 0 || (i as usize) >= list.len() {
                            return Err(ScriptTrap::new(
                                "IndexError: list index out of range",
                                *line,
                            ));
                        }
                        Ok(list[i as usize].clone())
                    }
                    ScriptValue::String(s) => {
                        let chars: Vec<char> = s.chars().collect();
                        let mut i = idx_val.as_number() as isize;
                        if i < 0 {
                            i += chars.len() as isize;
                        }
                        if i < 0 || (i as usize) >= chars.len() {
                            return Err(ScriptTrap::new(
                                "IndexError: string index out of range",
                                *line,
                            ));
                        }
                        Ok(ScriptValue::from_string(chars[i as usize].to_string()))
                    }
                    ScriptValue::Dict(dict_arc) => {
                        let key = idx_val.as_string();
                        let dict = dict_arc.lock();
                        dict.get(&key).cloned().ok_or_else(|| {
                            ScriptTrap::new(format!("KeyError: '{}'", key), *line)
                        })
                    }
                    _ => Err(ScriptTrap::new(
                        "TypeError: object is not subscriptable",
                        *line,
                    )),
                }
            }
            AstNode::MemberExpr {
                target,
                member,
                line,
            } => {
                let target_val = self.eval_expr(target, env, guard)?;
                if let ScriptValue::Dict(dict_arc) = &target_val {
                    if let Some(v) = dict_arc.lock().get(member).cloned() {
                        return Ok(v);
                    }
                }
                self.resolve_builtin_method(&target_val, member, *line)
            }
            AstNode::CallExpr {
                callee,
                arguments,
                line,
            } => {
                let callee_val = self.eval_expr(callee, env, guard)?;
                let mut args = Vec::with_capacity(arguments.len());
                for arg_expr in arguments {
                    args.push(self.eval_expr(arg_expr, env, guard)?);
                }

                match callee_val {
                    ScriptValue::NativeFunc(_, func) => func(&args, guard),
                    ScriptValue::UserFunc(func_node) => {
                        self.invoke_function(&func_node, &args, guard)
                    }
                    _ => Err(ScriptTrap::new(
                        "TypeError: object is not callable",
                        *line,
                    )),
                }
            }
            _ => Ok(ScriptValue::None),
        }
    }

    fn resolve_builtin_method(
        &self,
        target: &ScriptValue,
        member: &str,
        line: usize,
    ) -> Result<ScriptValue, ScriptTrap> {
        match target {
            ScriptValue::String(s) => {
                let s_val = s.clone();
                match member {
                    "upper" => Ok(ScriptValue::from_native(
                        "str.upper",
                        Arc::new(move |_args, _guard| Ok(ScriptValue::from_string(s_val.to_uppercase()))),
                    )),
                    "lower" => Ok(ScriptValue::from_native(
                        "str.lower",
                        Arc::new(move |_args, _guard| Ok(ScriptValue::from_string(s_val.to_lowercase()))),
                    )),
                    "strip" => Ok(ScriptValue::from_native(
                        "str.strip",
                        Arc::new(move |_args, _guard| {
                            Ok(ScriptValue::from_string(s_val.trim().to_string()))
                        }),
                    )),
                    "split" => Ok(ScriptValue::from_native(
                        "str.split",
                        Arc::new(move |args, guard| {
                            let parts: Vec<ScriptValue> = if args.is_empty() {
                                s_val
                                    .split_whitespace()
                                    .map(|p| ScriptValue::from_string(p.to_string()))
                                    .collect()
                            } else {
                                let sep = args[0].as_string();
                                if sep.is_empty() {
                                    vec![ScriptValue::from_string(s_val.clone())]
                                } else {
                                    s_val
                                        .split(&sep)
                                        .map(|p| ScriptValue::from_string(p.to_string()))
                                        .collect()
                                }
                            };
                            let val = ScriptValue::from_list(parts);
                            guard.alloc_bytes(val.estimated_bytes(), line)?;
                            Ok(val)
                        }),
                    )),
                    "replace" => Ok(ScriptValue::from_native(
                        "str.replace",
                        Arc::new(move |args, guard| {
                            if args.len() < 2 {
                                return Err(ScriptTrap::new(
                                    "TypeError: str.replace(old, new) requires 2 arguments",
                                    line,
                                ));
                            }
                            let from = args[0].as_string();
                            let to = args[1].as_string();
                            let out = if from.is_empty() {
                                s_val.clone()
                            } else {
                                s_val.replace(&from, &to)
                            };
                            guard.alloc_bytes(out.len(), line)?;
                            Ok(ScriptValue::from_string(out))
                        }),
                    )),
                    "startswith" => Ok(ScriptValue::from_native(
                        "str.startswith",
                        Arc::new(move |args, _guard| {
                            if args.is_empty() {
                                return Ok(ScriptValue::from_bool(false));
                            }
                            let prefix = args[0].as_string();
                            Ok(ScriptValue::from_bool(s_val.starts_with(&prefix)))
                        }),
                    )),
                    "endswith" => Ok(ScriptValue::from_native(
                        "str.endswith",
                        Arc::new(move |args, _guard| {
                            if args.is_empty() {
                                return Ok(ScriptValue::from_bool(false));
                            }
                            let suffix = args[0].as_string();
                            Ok(ScriptValue::from_bool(s_val.ends_with(&suffix)))
                        }),
                    )),
                    _ => Err(ScriptTrap::new(
                        format!("AttributeError: 'str' object has no attribute '{}'", member),
                        line,
                    )),
                }
            }
            ScriptValue::List(list_arc) => {
                let list_clone = Arc::clone(list_arc);
                match member {
                    "append" => Ok(ScriptValue::from_native(
                        "list.append",
                        Arc::new(move |args, guard| {
                            if args.len() != 1 {
                                return Err(ScriptTrap::new(
                                    "TypeError: list.append() takes exactly one argument",
                                    line,
                                ));
                            }
                            guard.alloc_bytes(args[0].estimated_bytes(), line)?;
                            list_clone.lock().push(args[0].clone());
                            Ok(ScriptValue::None)
                        }),
                    )),
                    _ => Err(ScriptTrap::new(
                        format!(
                            "AttributeError: 'list' object has no attribute '{}'",
                            member
                        ),
                        line,
                    )),
                }
            }
            ScriptValue::Dict(dict_arc) => {
                let dict_clone = Arc::clone(dict_arc);
                match member {
                    "keys" => Ok(ScriptValue::from_native(
                        "dict.keys",
                        Arc::new(move |_args, _guard| {
                            let keys: Vec<ScriptValue> = dict_clone
                                .lock()
                                .keys()
                                .map(|k| ScriptValue::from_string(k.clone()))
                                .collect();
                            Ok(ScriptValue::from_list(keys))
                        }),
                    )),
                    "get" => Ok(ScriptValue::from_native(
                        "dict.get",
                        Arc::new(move |args, _guard| {
                            if args.is_empty() {
                                return Ok(ScriptValue::None);
                            }
                            let key = args[0].as_string();
                            let dict = dict_clone.lock();
                            if let Some(v) = dict.get(&key) {
                                return Ok(v.clone());
                            }
                            if args.len() >= 2 {
                                return Ok(args[1].clone());
                            }
                            Ok(ScriptValue::None)
                        }),
                    )),
                    _ => Err(ScriptTrap::new(
                        format!(
                            "AttributeError: 'dict' object has no attribute '{}'",
                            member
                        ),
                        line,
                    )),
                }
            }
            _ => Err(ScriptTrap::new(
                format!("AttributeError: object has no attribute '{}'", member),
                line,
            )),
        }
    }
}
