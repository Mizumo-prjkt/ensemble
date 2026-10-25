use crate::diff::RgbColor;
use once_cell::sync::Lazy;
use regex::Regex;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;

pub const GRPH_MAGIC_BYTES: [u8; 8] = [0x89, b'G', b'R', b'P', b'H', 0x01, b'\r', b'\n'];

static POS_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"pos\s*\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)").unwrap());
static SIZE_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"size\s*\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)").unwrap());
static ATTR_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r#"([a-zA-Z0-9_]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s,\]]+))"#).unwrap()
});
static GRAPH_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r#"(?i)^graph\s*(?:"([^"]*)"|'([^']*)'|(\S+))(?:\s*\[(.*)\])?\s*$"#).unwrap()
});
static NODE_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)^node\s+([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*$").unwrap());
static BOX_START_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(?i)^box\s+([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*\{\s*$").unwrap()
});
static FOLLOWUP_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"(?i)^followup\s+([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*$").unwrap());
static CONN_RE: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"^([a-zA-Z0-9_\-\.]+)\s*(->|<->|--|-x)\s*([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*$")
        .unwrap()
});
static VALID_ID_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"^[a-zA-Z0-9_\-\.]+$").unwrap());
static HEX_COLOR_RE: Lazy<Regex> =
    Lazy::new(|| Regex::new(r"^#([0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$").unwrap());

fn parse_attributes(bracket_content: &str) -> HashMap<String, String> {
    let mut attrs = HashMap::new();
    if let Some(m) = POS_RE.captures(bracket_content) {
        attrs.insert("x".to_string(), m[1].to_string());
        attrs.insert("y".to_string(), m[2].to_string());
    }
    if let Some(m) = SIZE_RE.captures(bracket_content) {
        attrs.insert("w".to_string(), m[1].to_string());
        attrs.insert("h".to_string(), m[2].to_string());
    }
    for cap in ATTR_RE.captures_iter(bracket_content) {
        let key = cap[1].to_string();
        let val = cap
            .get(2)
            .or_else(|| cap.get(3))
            .or_else(|| cap.get(4))
            .map(|m| m.as_str().to_string())
            .unwrap_or_default();
        if key != "pos" && key != "size" {
            attrs.insert(key, val);
        }
    }
    attrs
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreGraphNode {
    pub id: String,
    pub label: String,
    pub node_type: String,
    pub x: f64,
    pub y: f64,
    pub width: f64,
    pub height: f64,
    pub shape: String,
    pub color: String,
    pub lore_id: String,
    pub notes: String,
    pub custom_data: Value,
}

impl Default for LoreGraphNode {
    fn default() -> Self {
        Self {
            id: String::new(),
            label: String::new(),
            node_type: "concept".to_string(),
            x: 0.0,
            y: 0.0,
            width: 170.0,
            height: 75.0,
            shape: "rounded".to_string(),
            color: "#3b82f6".to_string(),
            lore_id: String::new(),
            notes: String::new(),
            custom_data: json!({}),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreGraphBox {
    pub id: String,
    pub label: String,
    pub x: f64,
    pub y: f64,
    pub width: f64,
    pub height: f64,
    pub color: String,
    pub member_node_ids: Vec<String>,
}

impl Default for LoreGraphBox {
    fn default() -> Self {
        Self {
            id: String::new(),
            label: String::new(),
            x: 0.0,
            y: 0.0,
            width: 360.0,
            height: 240.0,
            color: "#6366f1".to_string(),
            member_node_ids: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreGraphConnector {
    pub id: String,
    pub from_node_id: String,
    pub to_node_id: String,
    pub arrow_type: String,
    pub label: String,
    pub relation: String,
    pub style: String,
    pub color: String,
}

impl Default for LoreGraphConnector {
    fn default() -> Self {
        Self {
            id: String::new(),
            from_node_id: String::new(),
            to_node_id: String::new(),
            arrow_type: "->".to_string(),
            label: String::new(),
            relation: "related".to_string(),
            style: "solid".to_string(),
            color: "#94a3b8".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreGraphFollowup {
    pub node_id: String,
    pub target_chapter: i32,
    pub status: String,
    pub note: String,
}

impl Default for LoreGraphFollowup {
    fn default() -> Self {
        Self {
            node_id: String::new(),
            target_chapter: 0,
            status: "pending".to_string(),
            note: String::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoreGraph {
    pub title: String,
    pub direction: String,
    pub nodes: Vec<LoreGraphNode>,
    pub boxes: Vec<LoreGraphBox>,
    pub connectors: Vec<LoreGraphConnector>,
    pub followups: Vec<LoreGraphFollowup>,
}

impl Default for LoreGraph {
    fn default() -> Self {
        Self::new()
    }
}

impl LoreGraph {
    pub fn new() -> Self {
        Self {
            title: "Story Lore Map".to_string(),
            direction: "LR".to_string(),
            nodes: Vec::new(),
            boxes: Vec::new(),
            connectors: Vec::new(),
            followups: Vec::new(),
        }
    }

    pub const fn magic_bytes() -> &'static [u8; 8] {
        &GRPH_MAGIC_BYTES
    }

    pub fn has_valid_magic(bytes: &[u8]) -> bool {
        bytes.len() >= 8 && bytes[..8] == GRPH_MAGIC_BYTES
    }

    pub fn is_valid_hex_or_named_color(c: &str) -> bool {
        if c.is_empty() {
            return true;
        }
        if c.starts_with('#') {
            return HEX_COLOR_RE.is_match(c);
        }
        RgbColor::from_css_str(c).is_some()
    }

    pub fn is_valid_node_type(t: &str) -> bool {
        matches!(
            t.to_ascii_lowercase().as_str(),
            "character" | "location" | "event" | "item" | "faction" | "concept" | "plot_point"
        )
    }

    pub fn is_valid_node_shape(s: &str) -> bool {
        matches!(
            s.to_ascii_lowercase().as_str(),
            "box" | "rounded" | "diamond" | "ellipse" | "cylinder" | "hexagon"
        )
    }

    pub fn is_valid_connector_style(s: &str) -> bool {
        matches!(
            s.to_ascii_lowercase().as_str(),
            "solid" | "dashed" | "dotted" | "thick"
        )
    }

    pub fn is_valid_arrow_type(a: &str) -> bool {
        matches!(a, "->" | "<->" | "--" | "-x")
    }

    pub fn add_or_update_node(&mut self, node: LoreGraphNode) {
        if let Some(existing) = self.nodes.iter_mut().find(|n| n.id == node.id) {
            *existing = node;
        } else {
            self.nodes.push(node);
        }
    }

    pub fn remove_node(&mut self, id: &str) -> bool {
        let initial_len = self.nodes.len();
        self.nodes.retain(|n| n.id != id);
        let removed = self.nodes.len() < initial_len;
        self.connectors
            .retain(|c| c.from_node_id != id && c.to_node_id != id);
        for b in &mut self.boxes {
            b.member_node_ids.retain(|m| m != id);
        }
        self.remove_followup(id);
        removed
    }

    pub fn find_node(&self, id: &str) -> Option<&LoreGraphNode> {
        self.nodes.iter().find(|n| n.id == id)
    }

    pub fn find_node_mut(&mut self, id: &str) -> Option<&mut LoreGraphNode> {
        self.nodes.iter_mut().find(|n| n.id == id)
    }

    pub fn update_node_position(&mut self, id: &str, x: f64, y: f64) {
        if let Some(n) = self.find_node_mut(id) {
            n.x = x;
            n.y = y;
        }
    }

    pub fn add_or_update_box(&mut self, b: LoreGraphBox) {
        if let Some(existing) = self.boxes.iter_mut().find(|x| x.id == b.id) {
            *existing = b;
        } else {
            self.boxes.push(b);
        }
    }

    pub fn remove_box(&mut self, id: &str) -> bool {
        let len = self.boxes.len();
        self.boxes.retain(|b| b.id != id);
        self.boxes.len() < len
    }

    pub fn add_connector(&mut self, conn: LoreGraphConnector) {
        if !conn.id.is_empty() {
            if let Some(existing) = self.connectors.iter_mut().find(|c| c.id == conn.id) {
                *existing = conn;
                return;
            }
        }
        self.connectors.push(conn);
    }

    pub fn remove_connector(&mut self, id: &str) -> bool {
        let len = self.connectors.len();
        self.connectors.retain(|c| c.id != id);
        self.connectors.len() < len
    }

    pub fn connect_nodes(
        &mut self,
        from_id: &str,
        to_id: &str,
        arrow: &str,
        label: &str,
        style: &str,
        color: &str,
    ) {
        if self.connectors.iter().any(|c| {
            c.from_node_id == from_id
                && c.to_node_id == to_id
                && c.label == label
                && c.arrow_type == arrow
        }) {
            return;
        }

        let base_id = format!("conn_{}_{}", from_id, to_id);
        let mut cand_id = base_id.clone();
        let mut suffix = 2;
        while self.connectors.iter().any(|c| c.id == cand_id) {
            cand_id = format!("{}_{}", base_id, suffix);
            suffix += 1;
        }

        self.connectors.push(LoreGraphConnector {
            id: cand_id,
            from_node_id: from_id.to_string(),
            to_node_id: to_id.to_string(),
            arrow_type: if arrow.is_empty() {
                "->".to_string()
            } else {
                arrow.to_string()
            },
            label: label.to_string(),
            relation: "related".to_string(),
            style: if style.is_empty() {
                "solid".to_string()
            } else {
                style.to_string()
            },
            color: if color.is_empty() {
                "#94a3b8".to_string()
            } else {
                color.to_string()
            },
        });
    }

    pub fn add_or_update_followup(&mut self, f: LoreGraphFollowup) {
        if let Some(existing) = self.followups.iter_mut().find(|x| x.node_id == f.node_id) {
            *existing = f;
        } else {
            self.followups.push(f);
        }
    }

    pub fn remove_followup(&mut self, node_id: &str) -> bool {
        let len = self.followups.len();
        self.followups.retain(|f| f.node_id != node_id);
        self.followups.len() < len
    }

    pub fn clear(&mut self) {
        self.title = "Story Lore Map".to_string();
        self.direction = "LR".to_string();
        self.nodes.clear();
        self.boxes.clear();
        self.connectors.clear();
        self.followups.clear();
    }

    pub fn auto_layout(&mut self) {
        if self.nodes.is_empty() {
            return;
        }
        let start_x = 60.0;
        let start_y = 80.0;
        let col_spacing = 240.0;
        let row_spacing = 130.0;
        let cols = ((self.nodes.len() as f64).sqrt().ceil() as usize).max(1);
        for (i, node) in self.nodes.iter_mut().enumerate() {
            let row = i / cols;
            let col = i % cols;
            node.x = start_x + (col as f64) * col_spacing;
            node.y = start_y + (row as f64) * row_spacing;
        }
    }

    pub fn to_grph_bytes(&self) -> Vec<u8> {
        let mut out = Vec::from(GRPH_MAGIC_BYTES);
        out.extend_from_slice(self.to_dsl().as_bytes());
        out
    }

    pub fn from_grph_bytes(&mut self, bytes: &[u8]) -> Result<(), String> {
        if !Self::has_valid_magic(bytes) {
            return Err(
                "Invalid .grph magic header. Expected 8 magic bytes: 0x89 'G' 'R' 'P' 'H' 0x01 '\\r' '\\n'."
                    .to_string(),
            );
        }
        let dsl = String::from_utf8_lossy(&bytes[8..]);
        self.from_dsl(&dsl)
    }

    pub fn to_dsl(&self) -> String {
        let mut dsl = String::new();
        dsl.push_str("# Ensemble Lore Graph Definition (.grph)\n");
        dsl.push_str("# Format Specification: Ensemble Graph Language V1\n\n");
        dsl.push_str(&format!(
            "graph \"{}\" [direction={}]\n\n",
            self.title, self.direction
        ));

        if !self.nodes.is_empty() {
            dsl.push_str("# Nodes (Story Blocks)\n");
            for n in &self.nodes {
                let mut line = format!(
                    "node {} [label=\"{}\", type=\"{}\", pos({}, {}), size({}, {}), color=\"{}\", shape={}",
                    n.id, n.label, n.node_type, n.x, n.y, n.width, n.height, n.color, n.shape
                );
                if !n.lore_id.is_empty() {
                    line.push_str(&format!(", loreId=\"{}\"", n.lore_id));
                }
                if !n.notes.is_empty() {
                    line.push_str(&format!(", notes=\"{}\"", n.notes));
                }
                line.push_str("]\n");
                dsl.push_str(&line);
            }
            dsl.push('\n');
        }

        if !self.boxes.is_empty() {
            dsl.push_str("# Boxes (Grouping Containers)\n");
            for b in &self.boxes {
                dsl.push_str(&format!(
                    "box {} [label=\"{}\", pos({}, {}), size({}, {}), color=\"{}\"] {{\n",
                    b.id, b.label, b.x, b.y, b.width, b.height, b.color
                ));
                for mem in &b.member_node_ids {
                    dsl.push_str(&format!("    {}\n", mem));
                }
                dsl.push_str("}\n\n");
            }
        }

        if !self.connectors.is_empty() {
            dsl.push_str("# Connectors (Relationships & Narrative Flows)\n");
            for c in &self.connectors {
                let mut line = format!(
                    "{} {} {} [id=\"{}\", style={}, color=\"{}\"",
                    c.from_node_id, c.arrow_type, c.to_node_id, c.id, c.style, c.color
                );
                if !c.label.is_empty() {
                    line.push_str(&format!(", label=\"{}\"", c.label));
                }
                if !c.relation.is_empty() {
                    line.push_str(&format!(", relation=\"{}\"", c.relation));
                }
                line.push_str("]\n");
                dsl.push_str(&line);
            }
            dsl.push('\n');
        }

        if !self.followups.is_empty() {
            dsl.push_str("# Narrative Follow-ups & Foreshadowing Targets\n");
            for f in &self.followups {
                dsl.push_str(&format!(
                    "followup {} [targetChapter={}, status=\"{}\", note=\"{}\"]\n",
                    f.node_id, f.target_chapter, f.status, f.note
                ));
            }
            dsl.push('\n');
        }

        dsl
    }

    pub fn from_dsl(&mut self, dsl: &str) -> Result<(), String> {
        let mut parsed_title = "Untitled Graph".to_string();
        let mut parsed_direction = "LR".to_string();
        let mut parsed_nodes: Vec<LoreGraphNode> = Vec::new();
        let mut parsed_boxes: Vec<LoreGraphBox> = Vec::new();
        let mut parsed_connectors: Vec<LoreGraphConnector> = Vec::new();
        let mut parsed_followups: Vec<LoreGraphFollowup> = Vec::new();

        let mut in_box = false;
        let mut current_box = LoreGraphBox::default();

        for (idx, raw_line) in dsl.lines().enumerate() {
            let line_num = idx + 1;
            let line = raw_line.trim();
            if line.is_empty() || line.starts_with('#') || line.starts_with("//") {
                continue;
            }

            if in_box {
                if line == "}" || line.starts_with('}') {
                    parsed_boxes.push(current_box.clone());
                    in_box = false;
                    continue;
                } else {
                    let member = line.trim_end_matches(';').trim();
                    if !VALID_ID_RE.is_match(member) {
                        return Err(format!(
                            "Line {}: Invalid box member node ID '{}'.",
                            line_num, member
                        ));
                    }
                    current_box.member_node_ids.push(member.to_string());
                    continue;
                }
            }

            if line.len() >= 5 && line[..5].eq_ignore_ascii_case("graph") {
                let Some(m) = GRAPH_RE.captures(line) else {
                    return Err(format!(
                        "Line {}: Malformed graph declaration '{}'.",
                        line_num, line
                    ));
                };
                if let Some(t) = m.get(1).or_else(|| m.get(2)).or_else(|| m.get(3)) {
                    parsed_title = t.as_str().to_string();
                }
                if let Some(attr_str) = m.get(4) {
                    let attrs = parse_attributes(attr_str.as_str());
                    if let Some(dir) = attrs.get("direction") {
                        let up = dir.to_ascii_uppercase();
                        if !matches!(up.as_str(), "LR" | "RL" | "TD" | "TB" | "BT") {
                            return Err(format!(
                                "Line {}: Invalid graph direction '{}'. Allowed values are LR, RL, TD, TB, BT.",
                                line_num, up
                            ));
                        }
                        parsed_direction = up;
                    }
                }
                continue;
            }

            if line.len() >= 5 && line[..5].eq_ignore_ascii_case("node ") {
                let Some(m) = NODE_RE.captures(line) else {
                    return Err(format!(
                        "Line {}: Malformed node declaration '{}'.",
                        line_num, line
                    ));
                };
                let mut n = LoreGraphNode {
                    id: m[1].to_string(),
                    label: m[1].to_string(),
                    ..Default::default()
                };
                if let Some(attr_str) = m.get(2) {
                    let attrs = parse_attributes(attr_str.as_str());
                    if let Some(lbl) = attrs.get("label") {
                        n.label = lbl.clone();
                    }
                    if let Some(t) = attrs.get("type") {
                        let lower = t.to_ascii_lowercase();
                        if !Self::is_valid_node_type(&lower) {
                            return Err(format!("Line {}: Invalid node type '{}'.", line_num, lower));
                        }
                        n.node_type = lower;
                    }
                    if let Some(s) = attrs.get("shape") {
                        let lower = s.to_ascii_lowercase();
                        if !Self::is_valid_node_shape(&lower) {
                            return Err(format!(
                                "Line {}: Invalid node shape '{}'.",
                                line_num, lower
                            ));
                        }
                        n.shape = lower;
                    }
                    if let Some(col) = attrs.get("color") {
                        if !Self::is_valid_hex_or_named_color(col) {
                            return Err(format!("Line {}: Invalid node color '{}'.", line_num, col));
                        }
                        n.color = col.clone();
                    }
                    if let Some(lid) = attrs.get("loreId") {
                        n.lore_id = lid.clone();
                    }
                    if let Some(notes) = attrs.get("notes") {
                        n.notes = notes.clone();
                    }
                    if let Some(x) = attrs.get("x").and_then(|v| v.parse().ok()) {
                        n.x = x;
                    }
                    if let Some(y) = attrs.get("y").and_then(|v| v.parse().ok()) {
                        n.y = y;
                    }
                    if let Some(w) = attrs.get("w").and_then(|v| v.parse().ok()) {
                        n.width = w;
                    }
                    if let Some(h) = attrs.get("h").and_then(|v| v.parse().ok()) {
                        n.height = h;
                    }
                }
                if let Some(existing) = parsed_nodes.iter_mut().find(|x| x.id == n.id) {
                    *existing = n;
                } else {
                    parsed_nodes.push(n);
                }
                continue;
            }

            if line.len() >= 4 && line[..4].eq_ignore_ascii_case("box ") {
                let Some(m) = BOX_START_RE.captures(line) else {
                    return Err(format!(
                        "Line {}: Malformed box declaration '{}'.",
                        line_num, line
                    ));
                };
                current_box = LoreGraphBox {
                    id: m[1].to_string(),
                    label: m[1].to_string(),
                    ..Default::default()
                };
                if let Some(attr_str) = m.get(2) {
                    let attrs = parse_attributes(attr_str.as_str());
                    if let Some(lbl) = attrs.get("label") {
                        current_box.label = lbl.clone();
                    }
                    if let Some(col) = attrs.get("color") {
                        if !Self::is_valid_hex_or_named_color(col) {
                            return Err(format!("Line {}: Invalid box color '{}'.", line_num, col));
                        }
                        current_box.color = col.clone();
                    }
                    if let Some(x) = attrs.get("x").and_then(|v| v.parse().ok()) {
                        current_box.x = x;
                    }
                    if let Some(y) = attrs.get("y").and_then(|v| v.parse().ok()) {
                        current_box.y = y;
                    }
                    if let Some(w) = attrs.get("w").and_then(|v| v.parse().ok()) {
                        current_box.width = w;
                    }
                    if let Some(h) = attrs.get("h").and_then(|v| v.parse().ok()) {
                        current_box.height = h;
                    }
                }
                in_box = true;
                continue;
            }

            if line.len() >= 9 && line[..9].eq_ignore_ascii_case("followup ") {
                let Some(m) = FOLLOWUP_RE.captures(line) else {
                    return Err(format!(
                        "Line {}: Malformed followup declaration '{}'.",
                        line_num, line
                    ));
                };
                let mut f = LoreGraphFollowup {
                    node_id: m[1].to_string(),
                    ..Default::default()
                };
                if let Some(attr_str) = m.get(2) {
                    let attrs = parse_attributes(attr_str.as_str());
                    if let Some(tc) = attrs.get("targetChapter").and_then(|v| v.parse().ok()) {
                        f.target_chapter = tc;
                    }
                    if let Some(st) = attrs.get("status") {
                        f.status = st.clone();
                    }
                    if let Some(note) = attrs.get("note") {
                        f.note = note.clone();
                    }
                }
                parsed_followups.push(f);
                continue;
            }

            if let Some(m) = CONN_RE.captures(line) {
                let mut c = LoreGraphConnector {
                    from_node_id: m[1].to_string(),
                    arrow_type: m[2].to_string(),
                    to_node_id: m[3].to_string(),
                    ..Default::default()
                };
                if let Some(attr_str) = m.get(4) {
                    let attrs = parse_attributes(attr_str.as_str());
                    if let Some(id) = attrs.get("id") {
                        c.id = id.clone();
                    }
                    if let Some(lbl) = attrs.get("label") {
                        c.label = lbl.clone();
                    }
                    if let Some(rel) = attrs.get("relation") {
                        c.relation = rel.clone();
                    }
                    if let Some(st) = attrs.get("style") {
                        let lower = st.to_ascii_lowercase();
                        if !Self::is_valid_connector_style(&lower) {
                            return Err(format!(
                                "Line {}: Invalid connector style '{}'.",
                                line_num, lower
                            ));
                        }
                        c.style = lower;
                    }
                    if let Some(col) = attrs.get("color") {
                        if !Self::is_valid_hex_or_named_color(col) {
                            return Err(format!(
                                "Line {}: Invalid connector color '{}'.",
                                line_num, col
                            ));
                        }
                        c.color = col.clone();
                    }
                }
                if c.id.is_empty() {
                    c.id = format!("conn_{}_{}_{}", c.from_node_id, c.to_node_id, parsed_connectors.len() + 1);
                }
                parsed_connectors.push(c);
                continue;
            }

            return Err(format!(
                "Line {}: Unrecognized .grph syntax: '{}'",
                line_num, line
            ));
        }

        if in_box {
            return Err("Unclosed box declaration (missing '}').".to_string());
        }

        self.title = parsed_title;
        self.direction = parsed_direction;
        self.nodes = parsed_nodes;
        self.boxes = parsed_boxes;
        self.connectors = parsed_connectors;
        self.followups = parsed_followups;
        Ok(())
    }

    pub fn dsl_grammar_specification() -> &'static str {
        r##"Ensemble Graph Language (.grph) V1 Specification:
- Magic Header (Binary .grph): 8 bytes = 89 47 52 50 48 01 0D 0A (\x89 G R P H \x01 \r \n)
- Graph Header: graph "<Title>" [direction=LR|RL|TD|TB|BT]
- Node: node <id> [label="...", type="character|location|event|item|faction|concept|plot_point", pos(x, y), size(w, h), color="#hex", shape=box|rounded|diamond|ellipse|cylinder|hexagon, loreId="...", notes="..."]
- Box: box <id> [label="...", pos(x, y), size(w, h), color="#hex"] { <member_node_id> ... }
- Connector: <from_id> ->|<->|--|-x <to_id> [id="...", style=solid|dashed|dotted|thick, color="#hex", label="...", relation="..."]
- Followup: followup <node_id> [targetChapter=<int>, status="pending|in_progress|resolved", note="..."]"##
    }

    pub fn to_json(&self) -> Value {
        json!({
            "title": self.title,
            "direction": self.direction,
            "nodeCount": self.nodes.len(),
            "boxCount": self.boxes.len(),
            "connectorCount": self.connectors.len(),
            "followupCount": self.followups.len(),
            "nodes": self.nodes,
            "boxes": self.boxes,
            "connectors": self.connectors,
            "followups": self.followups,
        })
    }
}
