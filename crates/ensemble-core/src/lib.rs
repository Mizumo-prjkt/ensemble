pub mod agentic;
pub mod archive;
pub mod chapter;
pub mod crypto;
pub mod debug;
pub mod diff;
pub mod lore;
pub mod lore_graph;
pub mod paths;
pub mod project;
pub mod serializer;

pub use agentic::{AgentModelHistoryEntry, AgenticData};
pub use archive::ZstdArchive;
pub use chapter::Chapter;
pub use crypto::AesCipher;
pub use debug::{
    init_debug_system, log_message, query_hardware_info, set_aggressive_checks_alert_callback,
    HardwareInfo,
};
pub use diff::{DiffUtil, RgbColor};
pub use lore::{
    LoreConflict, LoreEntity, LoreHistoryEvent, LoreManager, LorePlan, LoreReference,
    LoreResolvedConflict,
};
pub use lore_graph::{
    LoreGraph, LoreGraphBox, LoreGraphConnector, LoreGraphFollowup, LoreGraphNode,
    GRPH_MAGIC_BYTES,
};
pub use paths::AppPaths;
pub use project::{Ao3Project, ProjectMemoryStats, DEFAULT_WORK_SKIN_CSS};
pub use serializer::{
    ProjectFormatType, ProjectLoadReport, ProjectSerializer, ValidationStage,
};
