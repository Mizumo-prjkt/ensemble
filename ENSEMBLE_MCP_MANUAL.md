# Ensemble Model Context Protocol (MCP) Manual

Welcome to the **Ensemble** AI Agent integration guide. This manual describes how autonomous AI agents (e.g. Gemini Antigravity, Claude Code, Claude Desktop, Cursor) connect to and interact with Ensemble.

---

## 1. Overview

Ensemble is a desktop writing suite and AO3 Work Skin composer. By exposing an embedded Model Context Protocol (MCP) server, AI agents can inspect the current project, read and write chapters in real-time with automated AO3 HTML sanitization, tweak custom CSS work skins, and run linting diagnostics.

---

## 2. Connection Methods

### A. Direct WebSocket Connection
- **Address**: `ws://127.0.0.1:26303` (or auto-assigned port recorded in `mcp.json`)
- **Discovery File**: `~/.config/Ensemble/mcp.json`
- **Protocol**: JSON-RPC 2.0 (MCP 2024-11-05 specification)

### B. Stdio CLI Bridge (Claude Desktop / Claude Code / Antigravity)
Add Ensemble to your agent's MCP configuration:
```json
{
  "mcpServers": {
    "ensemble": {
      "command": "/home/miku/Documents/ao3-typewriter/build/ensemble",
      "args": ["--mcp-bridge"]
    }
  }
}
```

Or for Claude Code CLI:
```bash
claude mcp add ensemble "/home/miku/Documents/ao3-typewriter/build/ensemble" --mcp-bridge
```

---

## 3. Available Tools

| Tool | Parameters | Description |
| :--- | :--- | :--- |
| `get_project_summary` | *(none)* | Returns project title, author, chapter count, word counts, and unsaved changes state. |
| `list_chapters` | *(none)* | Lists all chapters with their index, UUID, title, order, and word count. |
| `get_chapter` | `chapterIndex` (int), `format` (str, opt) | Retrieves HTML and/or plain text content of a chapter ('all', 'html', or 'text'). |
| `update_chapter_html` | `chapterIndex` (int), `html` (str) | Updates chapter body. Automatically sanitized; returns sanitized HTML, word count, and stripped tags/attributes. |
| `add_chapter` | `title` (str, opt) | Appends a new chapter to the active project. |
| `delete_chapter` | `chapterIndex` (int) | Deletes a chapter by index and returns full updated chapter order. |
| `reorder_chapters` | `fromIndex` (int), `toIndex` (int) | Reorders chapters and returns full updated chapter order. |
| `get_workskin_css` | *(none)* | Retrieves the custom AO3 Work Skin stylesheet. |
| `set_workskin_css` | `css` (str) | Updates the Work Skin CSS and triggers an instant Chromium live re-render. |
| `get_problems` | *(none)* | Returns current HTML & CSS lint diagnostics with affectedChapters list and lastUpdated timestamp. |
| `load_project` | `filePath` (str), `saveCurrent` (bool, opt) | Loads an `.ao3proj` story archive from disk into Ensemble, updating chapters, stylesheet, tags, lore graph, and git history. |
| `save_project` | `filePath` (str, opt), `commitMessage` (str, opt), `author` (str, opt) | Saves active project to `.ao3proj` on disk. Supports recording a git-like commit into `history.cmt`. |
| `get_project_history` | `limit` (int, opt) | Retrieves the git-like version history from `history.cmt`, parsed into structured commit objects (hash, author, date, message) and raw log text. |
| `add_history_commit` | `message` (str), `author` (str, opt) | Records a new git-like commit entry into `history.cmt` with a 12-char commit hash, author, ISO timestamp, and message. |
| `reload_project` | *(none)* | Discards unsaved changes and reloads active project from disk. |
| `get_preview_screenshot` | `chapterIndex` (int, opt) | Captures rendered Chromium PreviewPane as a base64 PNG MCP image block for visual inspection. |
| `get_agent_presence` | *(none)* | Inspects all active AI agents currently connected to Ensemble (Antigravity, Claude, etc.) for multi-agent presence and co-working. |
| `register_agent_presence` | `agentName` (str, opt), `role` (str, opt), `workingNote` (str, opt) | Announces or updates your agent identity, role (co-author, reviewer, etc.), and current work note for multi-model co-working. |
| `get_agentic_summary` | *(none)* | Retrieves storage quotas (5 MB shared, 20 MB localized), current byte usage, index metadata, list of shared files, and registered localized agent directories in .ao3proj agentic-datas. |
| `read_agentic_file` | `path` (str), `scope` (str, opt), `encoding` (str, opt) | Reads a file from .ao3proj agentic-datas ('shared/' by default, or an agent's private '<agent>/' memory directory). Can also read root files `agent_model_history.xml` and `index.json`. |
| `write_agentic_file` | `path` (str), `content` (str), `scope` (str, opt), `encoding` (str, opt) | Writes or updates a file in .ao3proj agentic-datas ('shared/' by default for multi-model collaboration or '<agent>/' for private memory). Strictly enforces the 5 MB shared cap and 20 MB localized cap. |
| `append_agent_history` | `summary` (str), `agentName` (str, opt), `model` (str, opt), `role` (str, opt), `action` (str, opt), `chapterIndex` (int, opt), `wordCountDelta` (int, opt), `details` (str, opt) | Appends a structured interaction entry to agent_model_history.xml in .ao3proj. |
| `get_agent_history` | `limit` (int, opt), `agentName` (str, opt), `action` (str, opt) | Retrieves audit trail and recent changes by each model from agent_model_history.xml, including timestamps, actions, deltas, and summaries. |
| `post_agent_thought` | `thought` (str), `category` (str, opt), `target` (str, opt) | Broadcasts an AI model's internal reasoning, critique, pacing feedback, or plan to be displayed live in Ensemble's Agent Thoughts Monitor UI. |
| `get_lore_graph` | `name` (str, opt), `includeGrammar` (bool, opt) | Retrieves the story lore and narrative graph in custom `.grph` DSL syntax, JSON structure, and complete grammar specification. |
| `update_lore_graph` | `dsl` (str), `name` (str, opt), `autoLayout` (bool, opt) | Replaces or updates the `.grph` narrative graph DSL. Strictly adheres to unique 8-byte magic header `\x89GRPH\x01\r\n` and updates live desktop canvas. |
| `add_lore_node` | `id` (str), `label` (str), `type` (str, opt), `x` (num, opt), `y` (num, opt), `shape` (str, opt), `color` (str, opt) | Adds or updates an individual node block in the story's lore graph with position, shape, color, and optional link to lore/lore_data.json. |
| `connect_lore_nodes` | `fromNodeId` (str), `toNodeId` (str), `arrow` (str, opt), `label` (str, opt), `style` (str, opt), `color` (str, opt) | Creates or updates a relationship connector between two lore nodes (`->`, `<->`, `--`, `-x` conflict). |
| `move_lore_node` | `nodeId` (str), `x` (num), `y` (num), `graphName` (str, opt) | Updates the 2D canvas coordinates `pos(x, y)` of a movable block in the lore graph. |
| `get_lore_data` | `section` (str, opt), `query` (str, opt) | Retrieves narrative lore data from the `.ao3proj` lore/ directory files (`lore_data.json`, `history.json`, `conflicts.json`, `resolved.json`, `plans.json`, `references.json`, `metadata.xml`). |
| `manage_lore_data` | `action` (str), *payload* | Performs CRUD actions on story lore entities, timeline history, plot conflicts & resolutions, and narrative follow-up plans. |
| `update_project_metadata` | `rating` (str), `archiveWarnings` (arr), `categories` (arr), `fandoms` (arr), `relationships` (arr), `characters` (arr), `tags` (arr) | Updates standard AO3 work metadata tags stored in `metadata.xml`. |
| `list_image_embeds` | `chapterIndex` (int, opt) | Lists all external image embeds across chapters in the project (or for a specific chapter), including image URLs, alt texts, titles, dimensions, and chapter indices. |
| `insert_image_embed` | `chapterIndex` (int), `url` (str), `alt` (str, opt), `title` (str, opt), `width` (int, opt), `height` (int, opt), `alignment` (str, opt), `wrapInLink` (bool, opt), `position` (str, opt) | Inserts an AO3-compliant external image URL attachment into a chapter with clean `<img>` tags and optional link/alignment wrapping. |
| `test_firewall_connection` | *(none)* | Probes the local WebSocket loopback socket and reports latency or firewall blocks. |

---

## 4. Lore Graph & Narrative Worldbuilding (`.grph`)

Ensemble provides an independent narrative graphing engine and DSL format (`.grph`) designed specifically for storyboards, relationship tracking, lore mapping, and narrative follow-ups.

### Unique File Signature (Magic Bytes)
`.grph` files begin with an 8-byte binary signature:
`\x89GRPH\x01\r\n` (`0x89, 0x47, 0x52, 0x50, 0x48, 0x01, 0x0D, 0x0A`).

### Language Grammar & Syntax
```text
graph "Story Lore & Narrative Arc" [direction=LR]

# Nodes (Story Blocks with 2D positions)
node protagonist [label="Hero", type="character", pos(100, 150), shape="rounded", color="#3b82f6"]
node inciting_incident [label="Call to Adventure", type="event", pos(400, 150), shape="diamond", color="#f59e0b"]

# Connectors (Relationships & Narrative Flows)
protagonist -> inciting_incident [label="triggers", relation="causes", style="solid", color="#94a3b8"]
hero -x antagonist [label="sworn enemies", relation="conflict", style="dashed", color="#ef4444"]

# Boxes (Grouping Containers)
box academy [label="Royal Knight Academy", pos(80, 100), size(600, 300), color="#8b5cf6"] {
    protagonist
    inciting_incident
}

# Follow-up Hooks for Story Chapters
followup inciting_incident [targetChapter=2, status="pending", note="Establish character stakes and motivation."]
```

---

## 5. Lore Directory Structure in `.ao3proj`

Inside every `.ao3proj` archive, the `lore/` directory manages narrative continuity:
```text
lore/
├── lore_data.json       (Entities: Characters, Locations, Factions, Artifacts, Concepts)
├── history.json         (Backstory events & timeline chronology)
├── conflicts.json       (Detected canon inconsistencies & plot discrepancies)
├── resolved.json        (Resolved conflicts and definitive author canon rulings)
├── plans.json           (Story follow-up threads and future chapter plans)
├── references.json      (Canon sources, world codex citations, external references)
├── metadata.xml         (Lore manifest and chapter integration descriptors)
└── graphs/
    └── data.grph        (Primary narrative lore graph with \x89GRPH\x01\r\n magic bytes)
```

---

## 6. Multi-Agent Co-Working & Presence Awareness

Ensemble supports multiple concurrently connected AI agents (for example, **Gemini Antigravity** and **Claude** running simultaneously). Agents can:
1. Discover each other using `get_agent_presence` (or the `ensemble://agents` resource).
2. Announce their role and current focus using `register_agent_presence` (e.g. `role: 'co-author'`, `workingNote: 'Drafting chapter 2 dialogue'`).
3. Check what other models are working on to divide tasks effectively and prevent conflicting edits.

---

## 7. Agentic Data & Coroutine Shared Storage (`agentic-datas`)

Every `.ao3proj` project container contains a dedicated `agentic-datas` attribute:
```text
.ao3proj
├── metadata.xml (contains <agentic-datas> manifest)
├── stylesheet/workskin.css
├── chapters/chapter_*.html
└── agentic-datas/
    ├── index.json                        (Storage catalog, limits, and usage metrics)
    ├── agent_model_history.xml           (Structured model interaction & contribution log)
    ├── agentifiers.json                  (AES-256-CBC-HMAC-SHA256 encrypted agent identifier registry)
    ├── shared/                           (Collaborative coroutine memory shared by all models; max 5 MB cap)
    └── <agent>/                          (Private agent memory, e.g. claude/, cursor/, antigravity/; max 20 MB cap)
```

### Storage Caps
- **Shared Data (`agentic-datas/shared/`)**: Hard cap of **5 MB** (5,242,880 bytes).
- **Localized Agent Data (`agentic-datas/<agent>/`)**: Hard cap of **20 MB** (20,971,520 bytes).
Attempts to write files exceeding these caps are automatically rejected with a descriptive error.

---

## 8. Available Resources

- `ensemble://project` — Current project metadata (JSON)
- `ensemble://workskin/css` — Current Work Skin stylesheet (CSS)
- `ensemble://problems` — Active problem diagnostics and lint errors (JSON)
- `ensemble://agents` — Real-time list of all connected AI agents and presence state (JSON)
- `ensemble://agentic/index` — Storage index, quotas (5MB shared / 20MB localized), and file listings (JSON)
- `ensemble://agentic/history` — Agent interaction and contribution log (XML)
- `ensemble://lore/graph` — Active narrative DSL script and mapping from lore/graphs/data.grph (Text)
- `ensemble://lore/data` — Story entities, history, conflicts, plans, and references (JSON)

---

## 9. Troubleshooting & Diagnostics

- **Diagnose Connection via CLI**:
  ```bash
  ensemble --mcp-status
  ```
- **Firewall Notice**: Ensemble binds strictly to `127.0.0.1` (localhost loopback). If your client cannot connect, ensure local security software is not blocking loopback TCP sockets.
- **Test in GUI**: Open **Preferences > AI Settings** and click **Test Loopback Connection**.
