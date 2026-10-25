#![forbid(unsafe_code)]

pub mod evaluator;
pub mod guard;
pub mod lexer;
pub mod logger;
pub mod manager;
pub mod parser;
pub mod permissions;
pub mod sandbox;
pub mod value;

pub use evaluator::{Environment, ScriptEvaluator};
pub use guard::{ResourceGuard, ResourceLimits, ScriptTrap};
pub use lexer::{ScriptLexer, Token, TokenType};
pub use logger::PluginLogger;
pub use manager::{
    PluginManager, PluginMetadata, PluginToolbarAction, SecurityQuarantineEvent,
};
pub use parser::{AstNode, BlockNode, DecoratorNode, FuncDefNode, ScriptParser};
pub use permissions::{
    analyze_url_security, format_link_as_character_ids, PluginPermissions, PluginScanReport,
    PluginScanner, SuspiciousCharInfo, UrlSecurityAnalysis,
};
pub use sandbox::{
    ChapterSummary, PluginActionType, PluginHostContext, PluginPendingAction, PluginSandbox,
};
pub use value::ScriptValue;
