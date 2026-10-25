use serde_json::{json, Value};

/// Returns the JSON array of all MCP tool definitions exposed by Ensemble.
pub fn get_mcp_all_tool_definitions() -> Value {
    json!([
        {
            "name": "get_project_summary",
            "description": "Retrieves high-level metadata about the currently open AO3 project in Ensemble (title, author, total chapters, total word count, modified state, file path, and workskin status).",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": false
            }
        },
        {
            "name": "list_chapters",
            "description": "Lists all chapters in the open project with their 0-based index, title, word count, summary, notes, and last modified timestamp.",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": false
            }
        },
        {
            "name": "get_chapter",
            "description": "Reads the full HTML content, summary, beginning notes, and end notes of a specific chapter by its 0-based index.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "The 0-based index of the chapter to retrieve."
                    }
                },
                "required": ["chapterIndex"],
                "additionalProperties": false
            }
        },
        {
            "name": "update_chapter_html",
            "description": "Updates the HTML body, title, summary, or notes of a specific chapter. Automatically sanitizes HTML for AO3 compliance and runs linter diagnostics.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "The 0-based index of the chapter to update."
                    },
                    "html": {
                        "type": "string",
                        "description": "The new AO3-compliant HTML content for the chapter."
                    },
                    "title": {
                        "type": "string",
                        "description": "Optional new title for the chapter."
                    },
                    "summary": {
                        "type": "string",
                        "description": "Optional chapter summary."
                    },
                    "notesBegin": {
                        "type": "string",
                        "description": "Optional chapter beginning notes."
                    },
                    "notesEnd": {
                        "type": "string",
                        "description": "Optional chapter end notes."
                    },
                    "sanitize": {
                        "type": "boolean",
                        "description": "Whether to automatically sanitize the HTML for AO3 compliance (default: true)."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    }
                },
                "required": ["chapterIndex", "html"],
                "additionalProperties": false
            }
        },
        {
            "name": "add_chapter",
            "description": "Adds a new chapter to the project with a title and optional initial HTML content.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "title": {
                        "type": "string",
                        "description": "The title of the new chapter."
                    },
                    "html": {
                        "type": "string",
                        "description": "Optional initial HTML content for the chapter."
                    },
                    "summary": {
                        "type": "string",
                        "description": "Optional chapter summary."
                    },
                    "notesBegin": {
                        "type": "string",
                        "description": "Optional beginning notes."
                    },
                    "notesEnd": {
                        "type": "string",
                        "description": "Optional end notes."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier."
                    }
                },
                "required": ["title"],
                "additionalProperties": false
            }
        },
        {
            "name": "delete_chapter",
            "description": "Deletes a chapter at the specified 0-based index (at least 1 chapter must remain in the project).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "The 0-based index of the chapter to delete."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier."
                    }
                },
                "required": ["chapterIndex"],
                "additionalProperties": false
            }
        },
        {
            "name": "reorder_chapters",
            "description": "Moves a chapter from one 0-based index to another.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "fromIndex": {
                        "type": "integer",
                        "description": "Current 0-based index of the chapter."
                    },
                    "toIndex": {
                        "type": "integer",
                        "description": "Target 0-based index for the chapter."
                    }
                },
                "required": ["fromIndex", "toIndex"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_workskin_css",
            "description": "Retrieves the current AO3 Work Skin CSS stylesheet for the project.",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": false
            }
        },
        {
            "name": "set_workskin_css",
            "description": "Updates the AO3 Work Skin CSS stylesheet for the project and refreshes the live preview.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "css": {
                        "type": "string",
                        "description": "The new AO3 Work Skin CSS content."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier."
                    }
                },
                "required": ["css"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_problems",
            "description": "Runs the AO3 HTML and CSS linter across all chapters and workskin, returning all errors, warnings, and info diagnostics.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "Optional 0-based chapter index to check. If omitted, checks all chapters."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "load_project",
            "description": "Loads an .ao3proj project file from disk into Ensemble.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "filePath": {
                        "type": "string",
                        "description": "Absolute path to the .ao3proj file to open."
                    }
                },
                "required": ["filePath"],
                "additionalProperties": false
            }
        },
        {
            "name": "save_project",
            "description": "Saves the currently open project to disk (either to its current path or a specified filePath).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "filePath": {
                        "type": "string",
                        "description": "Optional file path to save the .ao3proj archive to."
                    },
                    "commitMessage": {
                        "type": "string",
                        "description": "Optional revision history commit message to record before saving."
                    },
                    "author": {
                        "type": "string",
                        "description": "Optional author/agent name for the commit."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "get_project_history",
            "description": "Retrieves the git-style revision history commits recorded inside the .ao3proj archive.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "limit": {
                        "type": "integer",
                        "description": "Maximum number of history commits to return (default: 50)."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "add_history_commit",
            "description": "Appends a new git-style revision commit entry to the project's internal history log.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "message": {
                        "type": "string",
                        "description": "Commit message describing the changes."
                    },
                    "author": {
                        "type": "string",
                        "description": "Name of the author or AI agent creating the commit."
                    }
                },
                "required": ["message"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_snapshot_diff",
            "description": "Computes unified diffs across chapters and workskin between two snapshot commits or between a snapshot commit and the live working state.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "commitId": {
                        "type": "string",
                        "description": "Target commit ID (or 0-based index as string) to compare."
                    },
                    "baseCommitId": {
                        "type": "string",
                        "description": "Optional base commit ID. If omitted, compares commitId against the parent commit or live working state."
                    }
                },
                "required": ["commitId"],
                "additionalProperties": false
            }
        },
        {
            "name": "test_firewall_connection",
            "description": "Tests local loopback WebSocket connectivity and returns diagnostic firewall and port status.",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": false
            }
        },
        {
            "name": "reload_project",
            "description": "Reloads the currently open project from disk or refreshes all UI editors and previews.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "fromDisk": {
                        "type": "boolean",
                        "description": "If true and the project has a file path on disk, reloads the project archive from disk (default: false)."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "get_preview_screenshot",
            "description": "Captures a PNG screenshot of the live AO3 WebView preview pane and returns it as base64-encoded image data.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "Optional 0-based chapter index to switch to before capturing."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "get_agent_presence",
            "description": "Lists all AI agents currently connected to the Ensemble MCP server along with their roles, models, and activity status.",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": false
            }
        },
        {
            "name": "register_agent_presence",
            "description": "Registers or updates the calling AI agent's identity (name, model, role, and current status) in Ensemble's live agent presence indicator.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Display name of the AI agent (e.g. 'Claude', 'Gemini', 'Antigravity')."
                    },
                    "model": {
                        "type": "string",
                        "description": "Underlying model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    },
                    "role": {
                        "type": "string",
                        "description": "Role of the agent (e.g. 'co-author', 'continuity-checker', 'workskin-designer', 'beta-reader')."
                    },
                    "status": {
                        "type": "string",
                        "description": "Current status message (e.g. 'Drafting Chapter 3', 'Idle')."
                    }
                },
                "required": ["name"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_agentic_summary",
            "description": "Retrieves a summary of the project's encrypted agentic workspace (instructions, style guide, outline, character notes, research, and audit log counts).",
            "inputSchema": {
                "type": "object",
                "properties": {},
                "additionalProperties": false
            }
        },
        {
            "name": "read_agentic_file",
            "description": "Reads a specific file from the project's agentic workspace (instructions, style_guide, outline, character_notes, research, or custom files).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": {
                        "type": "string",
                        "description": "Name of the agentic file to read ('instructions', 'style_guide', 'outline', 'character_notes', 'research', or custom filename)."
                    }
                },
                "required": ["file"],
                "additionalProperties": false
            }
        },
        {
            "name": "write_agentic_file",
            "description": "Writes or updates a file in the project's encrypted agentic workspace (instructions, style_guide, outline, character_notes, research, or custom files).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": {
                        "type": "string",
                        "description": "Name of the agentic file to write ('instructions', 'style_guide', 'outline', 'character_notes', 'research', or custom filename)."
                    },
                    "content": {
                        "type": "string",
                        "description": "Content to write into the agentic file."
                    },
                    "append": {
                        "type": "boolean",
                        "description": "If true, appends to existing content instead of overwriting (default: false)."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier."
                    }
                },
                "required": ["file", "content"],
                "additionalProperties": false
            }
        },
        {
            "name": "append_agent_history",
            "description": "Appends an audit entry to the project's agentic history log (agent_history.json).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "description": "Action category or tool name."
                    },
                    "summary": {
                        "type": "string",
                        "description": "Human-readable summary of what the agent did."
                    },
                    "chapterIndex": {
                        "type": "integer",
                        "description": "Optional 0-based chapter index (-1 for project-wide)."
                    },
                    "wordCountDelta": {
                        "type": "integer",
                        "description": "Net word count change."
                    },
                    "details": {
                        "type": "string",
                        "description": "Optional detailed notes or diff."
                    },
                    "agentName": {
                        "type": "string",
                        "description": "Optional override for agent name."
                    },
                    "agentModel": {
                        "type": "string",
                        "description": "Optional override for agent model."
                    },
                    "agentRole": {
                        "type": "string",
                        "description": "Optional override for agent role."
                    }
                },
                "required": ["action", "summary"],
                "additionalProperties": false
            }
        },
        {
            "name": "post_agent_thought",
            "description": "Broadcasts a real-time reasoning thought, critique, or suggestion to the Ensemble AI Thoughts Monitor UI and logs it in agent history.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "thought": {
                        "type": "string",
                        "description": "The thought, critique, or reasoning text to display."
                    },
                    "category": {
                        "type": "string",
                        "description": "Category of the thought (e.g. 'critique', 'continuity', 'pacing', 'style', 'brainstorm')."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier."
                    }
                },
                "required": ["thought"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_agent_history",
            "description": "Retrieves the chronological audit log of AI agent actions and thoughts recorded in the project.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "limit": {
                        "type": "integer",
                        "description": "Maximum number of entries to return (default: 50)."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "get_lore_graph",
            "description": "Retrieves the story lore and narrative graph (from lore/graphs/data.grph) in custom .grph DSL syntax and structured JSON. Includes complete language grammar specification guiding AI models on how to compose .grph lines.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Graph name (default: 'data')."
                    },
                    "includeGrammar": {
                        "type": "boolean",
                        "description": "Whether to include the full .grph language grammar guide (default: true)."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "update_lore_graph",
            "description": "Replaces or updates the .grph narrative graph DSL. Strictly adheres to unique 8-byte magic header \\x89GRPH\\x01\\r\\n and grammar rules. Validates syntax and semantic rules: valid node types, shapes, colors, connector styles, and ensures connector and box references point to defined nodes.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "dsl": {
                        "type": "string",
                        "description": "The complete .grph DSL source code to compile into the story's narrative graph."
                    },
                    "name": {
                        "type": "string",
                        "description": "Graph name (default: 'data')."
                    },
                    "autoLayout": {
                        "type": "boolean",
                        "description": "Whether to trigger automatic layout positioning (default: false)."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    }
                },
                "required": ["dsl"],
                "additionalProperties": false
            }
        },
        {
            "name": "add_lore_node",
            "description": "Adds or updates an individual node block in the story's lore graph with position pos(x, y), shape, color, and optional link to lore/lore_data.json.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "id": {
                        "type": "string",
                        "description": "Unique node identifier (e.g. 'hero_mc', 'inciting_incident', 'ancient_relic')."
                    },
                    "label": {
                        "type": "string",
                        "description": "Display label of the node."
                    },
                    "type": {
                        "type": "string",
                        "enum": ["character", "location", "event", "item", "faction", "concept", "plot_point"],
                        "description": "Narrative entity type."
                    },
                    "x": {
                        "type": "number",
                        "description": "X coordinate on the visual canvas."
                    },
                    "y": {
                        "type": "number",
                        "description": "Y coordinate on the visual canvas."
                    },
                    "shape": {
                        "type": "string",
                        "enum": ["rounded", "box", "diamond", "ellipse", "cylinder", "hexagon"]
                    },
                    "color": {
                        "type": "string",
                        "description": "Color hex (e.g. '#3b82f6', '#f59e0b', '#10b981')."
                    },
                    "loreId": {
                        "type": "string",
                        "description": "Optional entity ID link in lore/lore_data.json."
                    },
                    "notes": {
                        "type": "string",
                        "description": "Optional notes or narrative follow-up hooks."
                    },
                    "graphName": {
                        "type": "string",
                        "description": "Graph name (default: 'data')."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    }
                },
                "required": ["id", "label"],
                "additionalProperties": false
            }
        },
        {
            "name": "connect_lore_nodes",
            "description": "Creates or updates a relationship connector between two lore nodes with directional arrows (->, <->, --, -x) and relation labels.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "fromNodeId": {
                        "type": "string",
                        "description": "Source node ID."
                    },
                    "toNodeId": {
                        "type": "string",
                        "description": "Target node ID."
                    },
                    "arrow": {
                        "type": "string",
                        "enum": ["->", "<->", "--", "-x"],
                        "description": "Arrow style: '->' (directed), '<->' (bidirectional), '--' (undirected), '-x' (conflict/antagonism)."
                    },
                    "label": {
                        "type": "string",
                        "description": "Relationship label (e.g. 'triggers', 'mentors', 'opposes', 'located in')."
                    },
                    "style": {
                        "type": "string",
                        "enum": ["solid", "dashed", "dotted", "thick"]
                    },
                    "color": {
                        "type": "string"
                    },
                    "graphName": {
                        "type": "string"
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    }
                },
                "required": ["fromNodeId", "toNodeId"],
                "additionalProperties": false
            }
        },
        {
            "name": "move_lore_node",
            "description": "Moves a node block in the story's visual lore graph to new coordinates pos(x, y).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "nodeId": {
                        "type": "string",
                        "description": "The node ID to move."
                    },
                    "x": {
                        "type": "number",
                        "description": "New X coordinate on the canvas."
                    },
                    "y": {
                        "type": "number",
                        "description": "New Y coordinate on the canvas."
                    },
                    "graphName": {
                        "type": "string"
                    }
                },
                "required": ["nodeId", "x", "y"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_lore_data",
            "description": "Retrieves narrative lore data from the .ao3proj lore/ directory files: lore_data.json, history.json, conflicts.json, resolved.json, plans.json, references.json, and metadata.xml.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "section": {
                        "type": "string",
                        "enum": ["all", "entities", "history", "conflicts", "resolved", "plans", "references"],
                        "description": "Which lore section to return (default: 'all')."
                    },
                    "query": {
                        "type": "string",
                        "description": "Optional query to filter entities/events/conflicts by text."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "manage_lore_data",
            "description": "Performs CRUD actions on story lore entities, timeline history, plot conflicts & resolutions, and narrative follow-up plans in the .ao3proj archive.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "enum": [
                            "add_entity", "update_entity", "remove_entity",
                            "add_history", "remove_history",
                            "add_conflict", "resolve_conflict",
                            "add_plan", "update_plan", "remove_plan",
                            "add_reference", "remove_reference",
                            "remove_graph"
                        ],
                        "description": "The action to perform on the story lore database."
                    },
                    "id": {
                        "type": "string",
                        "description": "Identifier for entity, event, conflict, plan, reference, or graph."
                    },
                    "name": { "type": "string" },
                    "graphName": {
                        "type": "string",
                        "description": "Name of the graph to remove (alias for 'name')."
                    },
                    "category": { "type": "string" },
                    "description": { "type": "string" },
                    "summary": {
                        "type": "string",
                        "description": "Summary of historical event or lore entry."
                    },
                    "impact": {
                        "type": "string",
                        "description": "Impact or narrative significance of historical event."
                    },
                    "source": {
                        "type": "string",
                        "description": "Source title or canon origin for reference."
                    },
                    "citation": {
                        "type": "string",
                        "description": "Citation, episode, chapter, or URL for reference."
                    },
                    "applicability": {
                        "type": "string",
                        "description": "Applicability of reference to current work."
                    },
                    "notes": {
                        "type": "string",
                        "description": "Notes or additional details."
                    },
                    "loreThread": {
                        "type": "string",
                        "description": "Lore thread or plot arc for plan."
                    },
                    "tags": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "title": { "type": "string" },
                    "eraOrDate": { "type": "string" },
                    "storyChapter": { "type": "integer" },
                    "severity": { "type": "string" },
                    "chapterA": { "type": "integer" },
                    "chapterB": { "type": "integer" },
                    "conflictId": {
                        "type": "string",
                        "description": "Conflict ID for resolve_conflict."
                    },
                    "resolution": {
                        "type": "string",
                        "description": "Resolution explanation for resolve_conflict."
                    },
                    "canonRuling": {
                        "type": "string",
                        "description": "Canon ruling established."
                    },
                    "targetChapter": { "type": "integer" },
                    "status": { "type": "string" },
                    "neededFollowUps": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    }
                },
                "required": ["action"],
                "additionalProperties": true
            }
        },
        {
            "name": "update_project_metadata",
            "description": "Updates standard AO3 work metadata tags (ratings, archive warnings, categories, fandoms, relationships, characters, freeform tags) in metadata.xml.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "title": { "type": "string" },
                    "author": { "type": "string" },
                    "rating": {
                        "type": "string",
                        "enum": ["General Audiences", "Teen And Up Audiences", "Mature", "Explicit", "Not Rated"]
                    },
                    "archiveWarnings": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "categories": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "fandoms": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "relationships": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "characters": {
                        "type": "array",
                        "items": { "type": "string" }
                    },
                    "tags": {
                        "type": "array",
                        "items": { "type": "string" }
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "list_image_embeds",
            "description": "Lists all external image embeds across chapters in the project (or for a specific chapter), including image URLs, alt texts, titles, dimensions, and chapter indices.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "Optional 0-based chapter index to filter image embeds. If omitted, scans all chapters."
                    }
                },
                "additionalProperties": false
            }
        },
        {
            "name": "insert_image_embed",
            "description": "Inserts a standard AO3-compliant external image embed URL into a chapter, automatically validating the URL and generating properly structured AO3 HTML.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "chapterIndex": {
                        "type": "integer",
                        "description": "The 0-based index of the chapter."
                    },
                    "url": {
                        "type": "string",
                        "description": "The public URL of the image (must begin with http:// or https://)."
                    },
                    "alt": {
                        "type": "string",
                        "description": "Optional descriptive alt text for accessibility and AO3 compliance."
                    },
                    "title": {
                        "type": "string",
                        "description": "Optional hover tooltip title text."
                    },
                    "width": {
                        "type": "integer",
                        "description": "Optional display width in pixels."
                    },
                    "height": {
                        "type": "integer",
                        "description": "Optional display height in pixels."
                    },
                    "alignment": {
                        "type": "string",
                        "enum": ["center", "inline", "left", "right"],
                        "description": "Alignment of the image block (default: 'center')."
                    },
                    "wrapInLink": {
                        "type": "boolean",
                        "description": "Whether to wrap the image in an <a> link to the full-size image URL (default: true)."
                    },
                    "position": {
                        "type": "string",
                        "enum": ["append", "prepend"],
                        "description": "Where to insert the image (default: 'append')."
                    },
                    "model": {
                        "type": "string",
                        "description": "Optional AI model identifier (e.g. 'claude-3-7-sonnet', 'gemini-2.5-pro')."
                    }
                },
                "required": ["chapterIndex", "url"],
                "additionalProperties": false
            }
        }
    ])
}
