use crate::guard::{ResourceGuard, ScriptTrap};
use crate::parser::FuncDefNode;
use parking_lot::Mutex;
use std::collections::BTreeMap;
use std::fmt;
use std::sync::Arc;

pub type NativeCallable =
    Arc<dyn Fn(&[ScriptValue], &mut ResourceGuard) -> Result<ScriptValue, ScriptTrap> + Send + Sync>;

#[derive(Clone)]
pub enum ScriptValue {
    None,
    Bool(bool),
    Number(f64),
    String(String),
    List(Arc<Mutex<Vec<ScriptValue>>>),
    Dict(Arc<Mutex<BTreeMap<String, ScriptValue>>>),
    NativeFunc(String, NativeCallable),
    UserFunc(Arc<FuncDefNode>),
}

impl Default for ScriptValue {
    fn default() -> Self {
        Self::None
    }
}

impl fmt::Debug for ScriptValue {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.to_display_string())
    }
}

impl ScriptValue {
    pub fn none() -> Self {
        Self::None
    }

    pub fn from_bool(b: bool) -> Self {
        Self::Bool(b)
    }

    pub fn from_number(n: f64) -> Self {
        Self::Number(n)
    }

    pub fn from_string(s: impl Into<String>) -> Self {
        Self::String(s.into())
    }

    pub fn from_list(items: Vec<ScriptValue>) -> Self {
        Self::List(Arc::new(Mutex::new(items)))
    }

    pub fn from_dict(entries: BTreeMap<String, ScriptValue>) -> Self {
        Self::Dict(Arc::new(Mutex::new(entries)))
    }

    pub fn from_native(name: impl Into<String>, func: NativeCallable) -> Self {
        Self::NativeFunc(name.into(), func)
    }

    pub fn from_user_func(node: Arc<FuncDefNode>) -> Self {
        Self::UserFunc(node)
    }

    pub fn is_none(&self) -> bool {
        matches!(self, Self::None)
    }

    pub fn is_bool(&self) -> bool {
        matches!(self, Self::Bool(_))
    }

    pub fn is_number(&self) -> bool {
        matches!(self, Self::Number(_))
    }

    pub fn is_string(&self) -> bool {
        matches!(self, Self::String(_))
    }

    pub fn is_list(&self) -> bool {
        matches!(self, Self::List(_))
    }

    pub fn is_dict(&self) -> bool {
        matches!(self, Self::Dict(_))
    }

    pub fn as_number(&self) -> f64 {
        match self {
            Self::Number(n) => *n,
            Self::Bool(b) => {
                if *b {
                    1.0
                } else {
                    0.0
                }
            }
            Self::String(s) => s.trim().parse::<f64>().unwrap_or(0.0),
            _ => 0.0,
        }
    }

    pub fn as_string(&self) -> String {
        match self {
            Self::String(s) => s.clone(),
            _ => self.to_display_string(),
        }
    }

    pub fn is_truthy(&self) -> bool {
        match self {
            Self::None => false,
            Self::Bool(b) => *b,
            Self::Number(n) => *n != 0.0 && !n.is_nan(),
            Self::String(s) => !s.is_empty(),
            Self::List(l) => !l.lock().is_empty(),
            Self::Dict(d) => !d.lock().is_empty(),
            Self::NativeFunc(_, _) | Self::UserFunc(_) => true,
        }
    }

    pub fn equals(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::None, Self::None) => true,
            (Self::Bool(a), Self::Bool(b)) => a == b,
            (Self::Number(a), Self::Number(b)) => (a - b).abs() < 1e-12,
            (Self::String(a), Self::String(b)) => a == b,
            (Self::List(a), Self::List(b)) => {
                let la = a.lock();
                let lb = b.lock();
                la.len() == lb.len() && la.iter().zip(lb.iter()).all(|(x, y)| x.equals(y))
            }
            (Self::Dict(a), Self::Dict(b)) => {
                let da = a.lock();
                let db = b.lock();
                da.len() == db.len()
                    && da
                        .iter()
                        .all(|(k, v)| db.get(k).map(|ov| v.equals(ov)).unwrap_or(false))
            }
            _ => false,
        }
    }

    pub fn to_display_string(&self) -> String {
        match self {
            Self::None => "None".to_string(),
            Self::Bool(b) => if *b { "True" } else { "False" }.to_string(),
            Self::Number(n) => {
                if (n.fract()).abs() < 1e-12 && n.abs() < 1e15 {
                    format!("{}", *n as i64)
                } else {
                    format!("{}", n)
                }
            }
            Self::String(s) => s.clone(),
            Self::List(l) => {
                let list = l.lock();
                let parts: Vec<String> = list
                    .iter()
                    .map(|item| {
                        if item.is_string() {
                            format!("\"{}\"", item.as_string())
                        } else {
                            item.to_display_string()
                        }
                    })
                    .collect();
                format!("[{}]", parts.join(", "))
            }
            Self::Dict(d) => {
                let dict = d.lock();
                let parts: Vec<String> = dict
                    .iter()
                    .map(|(k, v)| {
                        let vs = if v.is_string() {
                            format!("\"{}\"", v.as_string())
                        } else {
                            v.to_display_string()
                        };
                        format!("\"{}\": {}", k, vs)
                    })
                    .collect();
                format!("{{{}}}", parts.join(", "))
            }
            Self::NativeFunc(name, _) => format!("<native function {}>", name),
            Self::UserFunc(f) => format!("<function {}>", f.name),
        }
    }

    pub fn estimated_bytes(&self) -> usize {
        let mut base = 32usize;
        match self {
            Self::String(s) => base += s.len(),
            Self::List(l) => {
                for item in l.lock().iter() {
                    base += item.estimated_bytes();
                }
            }
            Self::Dict(d) => {
                for (k, v) in d.lock().iter() {
                    base += k.len() + v.estimated_bytes();
                }
            }
            _ => {}
        }
        base
    }
}
