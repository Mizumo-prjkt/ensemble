<img src="src/icons/icon.png" width="100" height="100" align="left" alt="Ensemble logo">

# Ensemble

<p align="left">
  <strong>Next-Generation AO3 Writing Suite, Work Skin Composer & Narrative Studio</strong>
</p>

<p align="left">
  A high-performance Qt 6 & Chromium C++ desktop application for Archive of Our Own (AO3) authors. Features a distraction-free WYSIWYG composer, live AO3 HTML source synchronization, authentic Chromium Work Skin rendering, direct AO3 work/series import, visual lore mapping, sandboxed <code>.er</code> plugins, local/cloud AI co-authoring via MCP, and native ZSTD project archiving with optional AES-256-GCM encryption.
</p>

---

## ✨ Key Features

- 🎭 **Authentic Chromium Work Skin Preview**: Full Qt 6 `QWebEngineView` rendering supporting 100% of AO3 Work Skin features (`border-radius`, `box-shadow`, custom cards like `.chapter-card` and `.liyue-box`, multi-theme skins, and custom typography).
- ⚡ **Zero-Jump Live Preview**: Real-time DOM mutation engine updates your live preview as you type while preserving your scroll position with 100% precision.
- 📥 **Direct AO3 Import & Crawler Extraction**: Authenticate with your AO3 account (via Session Cookie or login) to import multi-chapter works, series, co-authored stories, and custom Work Skins. The built-in crawler automatically extracts ratings, archive warnings, relationship tags, character rosters, and summaries.
- 🎨 **Work Skin Composer & Visual Inspector**: Edit custom CSS with syntax highlighting, auto-detect classes, apply styles via context menus, and sync Work Skins with AO3 via the built-in Skin Diff Tool.
- 🧩 **Sandboxed Scripting Plugins (`.er`) [Beta]**: Extend Ensemble using a custom Python-like DSL and tree-walking AST interpreter written from scratch in C++. Runs in an isolated worker thread with memory caps (8 MB), execution watchdogs, declarative UI compositing, granular permissions, strict HTTPS enforcement, anti-diversion quarantine, and persistent audit logging.
- 🗺️ **Visual Lore Graph & Narrative Mapping (`.grph`)**: Dedicated 2D canvas for mapping out characters, factions, artifacts, and plot conflicts using Ensemble's independent `.grph` DSL language with a strict binary header (`\x89GRPH\x01\r\n`).
- ⚙️ **Project Properties & AO3 Metadata Hub**: Manage ratings, archive warnings, relationship categories, cast lists, fandoms, freeform tags, preface notes, and postscript end notes in one unified hub serialized into `metadata.xml`.
- 🖼️ **URL-Based Image Embed Attachments**: Easily embed external images with live aspect-ratio previews, custom alignment, and 1-click HTML snippet generation, with automatic detection during AO3 story import.
- 📜 **Git-Like Revision History (`history.cmt`)**: Internal commit ledger tracking 12-char SHA-1 commit hashes, author attributions, ISO timestamps, and commit messages inside your project file.
- 🤖 **Model Context Protocol (MCP) & Local LLM Support**: Embedded loopback WebSocket server (`ws://127.0.0.1:26303`) and Stdio bridge for AI pair-programmers (Google Antigravity, Claude, Cursor) plus beta support for local, privacy-first models (Ollama, llama.cpp, LM Studio, Jan).
- 🌓 **Dynamic Theme Engine & Design Systems**: Choose between Light, Dark, Darker (OLED pure black #000000), and official AO3 Reversi themes, paired with Android Material You (dynamic wallpaper hue extraction), KDE Breeze, or Standard Qt Fusion widget styling with mutual exclusivity guards.
- 🗺️ **Proportional Minimap Sidebar**: High-visibility scrollbar minimap offering an instant visual overview of your document structure with color-coded syntax highlights.
- 🔀 **Split Workspace**: Simultaneous WYSIWYG rich text editor, live AO3 HTML source view, and Work Skin CSS editor with debounced auto-sync.
- 📑 **Chapter Management & Filter Search**: Add, rename, delete, reorder chapters, and filter long chapter lists instantly with a built-in search bar.
- 🚀 **Single-Instance Application**: Direct integration with system file managers to open `.ao3proj` files seamlessly without creating duplicate processes.

---

## 🧩 [Beta] Sandboxed Scripting Plugins (`.er`)

Ensemble includes a custom, memory-bounded, Python-like scripting engine designed specifically for third-party plugins (<b><code>.er</code></b>, Ensemble Runner scripts) without freezing the UI or risking host stability:

- **No Native Shared Libraries:** Does not load dynamic libraries (`.dll`, `.so`) or embed standard Python/Lua runtimes. Pure C++ lexer, recursive descent parser, and AST interpreter.
- **Worker Thread Isolation:** All plugin hooks execute inside isolated `QThread` workers. Zero GUI pointers (`QWidget*`) cross threads.
- **Strict Resource Quotas (`ResourceGuard`):** 8 MB memory cap per plugin, 150,000 AST execution steps / 250ms watchdog per hook, and a 64-frame call stack depth cap.
- **Declarative UI Compositing:** Plugins compose native dialog windows on the GUI thread via `ui.create_window()`, `add_label()`, `add_text()`, `add_input()`, and `add_button()`.
- **Strict HTTPS & Anti-Diversion Quarantine:**
  - All network traffic mediated by Ensemble strictly requires `https://`. Insecure `http://` or downgrade attempts trigger immediate security quarantine.
  - Automatic HTTP redirects are forbidden (`QNetworkRequest::ManualRedirectPolicy`). Any attempt to redivert traffic to another website terminates execution and **permanently quarantines the plugin** with an alert to report the incident to developers.
- **Granular Permissions & Audit Logging:** Manage permissions (Editor Read, Editor Write, Storage, UI, Network) with per-domain whitelisting and view live audit trails in `plugin_activity.log`.
- **Uninstallation Support:** 1-click clean deletion of `.er` scripts, manifest records, and private storage.

---

## 🗺️ Visual Lore Graph & Narrative Studio (`.grph`)

Ensemble provides an independent narrative canvas engine for worldbuilders and fanfiction authors:
- **Independent `.grph` Language:** Human-readable domain-specific language beginning with binary magic signature `\x89GRPH\x01\r\n`.
- **Relationship Connectors:** Directed influence (`a -> b`), mutual connections (`a <-> b`), neutral links (`a -- b`), and plot conflict markers (`a -x b`).
- **Comprehensive Project Lore Directory:**
  ```text
  lore/
  ├── graphs/data.grph        (Primary narrative canvas layout)
  ├── lore_data.json          (Codex of characters, factions, and artifacts)
  ├── history.json            (Chronological backstory timeline)
  ├── conflicts.json          (Detected continuity errors and plot holes)
  ├── resolved.json           (Author rulings on resolved plot points)
  └── plans.json              (Story follow-up hooks and future arcs)
  ```

---

## 🤖 Model Context Protocol (MCP) & AI Integration

Ensemble exposes an embedded **Model Context Protocol (MCP)** server over localhost WebSocket (`ws://127.0.0.1:26303`) with a hybrid Stdio bridge for AI pair-programmers:

- **Supported AI Environments:** Claude Desktop, Claude Code CLI, Cursor IDE, Gemini Antigravity, and Local LLMs (Ollama, llama.cpp, LM Studio, Jan, LocalAI).
- **Available MCP Tools:**
  `get_project_summary`, `list_chapters`, `get_chapter`, `update_chapter_html`, `add_chapter`, `delete_chapter`, `reorder_chapters`, `get_workskin_css`, `set_workskin_css`, `load_project`, `save_project`, `get_project_history`, `add_history_commit`, `get_lore_graph`, `update_lore_graph`, `add_lore_node`, `connect_lore_nodes`, `get_lore_data`, `manage_lore_data`, `update_project_metadata`, `list_image_embeds`, `insert_image_embed`, `post_agent_thought`, `get_agent_presence`, `register_agent_presence`, `get_agentic_summary`, `read_agentic_file`, `write_agentic_file`, `get_problems`, `get_preview_screenshot`.

```bash
# 1-Click setup: Export configuration and schemas for Claude and Antigravity
ensemble --send-mcp-manual-to-agents

# Diagnose loopback connectivity and reachability
ensemble --mcp-status
```

---

## 🎨 Themes & Design Systems

- **Color Themes:**
  - ☀️ **Light Mode:** High-contrast crisp bright palette (`#0f172a` primary text).
  - 🌙 **Dark Mode:** Deep slate theme tailored for late-night authoring.
  - 🖤 **Darker Mode (OLED):** Pure pitch black (`#000000`) for OLED power savings.
  - 🔴 **AO3 Reversi Mode:** Authentic AO3 dark crimson reading palette (`#111111` dark background, `#990000` accents).
- **Skin Flavors & Design Systems:**
  - 🤖 **Android Material You:** Dynamically extracts the dominant accent color from your active desktop wallpaper (GNOME, KDE DBus, Windows Desktop) with authentic Material 3 pill buttons (20px radius).
  - ✨ **Standard Ensemble:** Curated violet modern styling.
  - 🖥️ **Standard Qt:** Clean native desktop widgets (Fusion).
  - 🐧 **KDE UI:** Native Breeze design integration.
- **Mutual Exclusivity Guard:** Due to contrasting color schemes, AO3 Reversi and Android Material You styling are mutually exclusive. Selecting Reversi disables Material You in the skin settings, and selecting Material You disables Reversi theme options.

---

## ⌨️ Keyboard Shortcuts Reference

| Shortcut | Action | Scope |
|----------|--------|-------|
| `Ctrl+N` | New Project | Global |
| `Ctrl+O` | Open Project (`.ao3proj`) | Global |
| `Ctrl+S` | Save Project | Global |
| `Ctrl+Shift+S` | Save Project As… | Global |
| `Alt+Enter` | Project Properties & Metadata | Global |
| `Ctrl+Shift+O` | Open Chapter Manager & Search | Global |
| `Ctrl+Shift+L` | Lore Graph & Narrative Map | Global |
| `Ctrl+Shift+A` | AI Agent Thoughts Monitor | Global |
| `Ctrl+Shift+U` | [Beta] Plugin Manager | Global |
| `Ctrl+Shift+M` | Insert Image Embed Attachment | Editor |
| `Ctrl+Shift+I` | Import Work from AO3… | Global |
| `Ctrl+Shift+C` | Copy AO3 Clean HTML to Clipboard | Global |
| `Ctrl+P` | Toggle Side-by-Side WebEngine Preview | Global |
| `Ctrl+Shift+P` | Pop Out WebEngine Preview Window | Global |
| `Ctrl+F` | Find Text | Editor |
| `Ctrl+R` | Find and Replace Text | Editor |
| `Ctrl+B` / `Ctrl+I` / `Ctrl+U` / `Ctrl+Shift+X` | Bold / Italic / Underline / Strikethrough | Editor |
| `Ctrl+,` | Preferences & Settings | Global |
| `F1` | Help & User Guide | Global |
| `Ctrl+Alt+1` | Light Theme | Global |
| `Ctrl+Alt+2` | Dark Mode | Global |
| `Ctrl+Alt+3` | AO3 Reversi Mode | Global |
| `Ctrl+Alt+4` | OLED Darker Mode | Global |
| `Ctrl+Alt+T` | Cycle Themes | Global |

---

## 🐘 The Elephant in the Room: "Wait, Do I Need to Know Git?"

**Short answer: Absolutely not.**

When writers see technical terms like *"Git-like commit ledger"*, *"12-char SHA-1 hashes"*, or *"line-by-line unified diffs"*, the immediate reaction is often panic:
> *"Do I need to install Git? Do I have to open a black terminal window and type `git rebase`? Do I need a GitHub account? What if I accidentally delete my story with a merge conflict?!"*

Let's clear the air completely:

1. **Zero Git Required:** You do **not** need Git installed on your computer. You never touch a terminal, you never run command-line tools, and you don't need a GitHub account.
2. **Why We Borrowed the Idea from Git:**
   Software developers have enjoyed a superpower for decades that creative writers have sorely lacked: **the ability to save checkpoints with commit notes, inspect line-by-line diffs, and never lose a single sentence.**
   Traditional writers usually end up with a folder full of messy files like:
   `Chapter_2_draft_v2_final_REALLY_FINAL_comments.docx`.
3. **How It Works Behind the Scenes in Ensemble:**
   - Every `.ao3proj` project archive contains its own internal, automatic history ledger (`history.cmt`) and snapshot diffs (`history/`).
   - Whenever you save or record a milestone, Ensemble creates a lightweight snapshot tracking what changed.
   - Open **File → Properties → 📜 Revision History** (<kbd>Alt+Enter</kbd>) anytime to browse your revision timeline. You'll see author signatures, dual local/UTC timestamps, and visual line-by-line diffs showing exactly what was added in green (`+`) and removed in red (`-`).
   - No merge conflicts, no detached HEAD states, and no cryptic terminal errors.
4. **For Coders & Technical Writers:**
   If you *are* a developer who loves Git and wants to track your `.ao3proj` files in a real Git repository, you can! Because `.ao3proj` is a deterministic Zstandard archive containing clean text structures, it plays nicely with backup scripts and version control. Furthermore, connected AI agents via MCP can read and record snapshots programmatically using `get_project_history` and `add_history_commit`.

---

## 📄 Project File Format (`.ao3proj`)

Ensemble saves stories in a unified binary container compressed with **Zstandard (ZSTD)**, with optional **AES-256-GCM** encryption:
- `manifest.json`: Project schema, chapters index, and revision pointer.
- `metadata.xml`: Structured AO3 tags, ratings, warnings, fandoms, relationships, and author notes.
- `history.cmt`: Git-like commit ledger tracking 12-char SHA-1 hashes and commit notes.
- `chapters/`: Individual chapter HTML drafts.
- `workskin.css`: Custom AO3 Work Skin stylesheet.
- `lore/`: Narrative graph (`.grph`), entities, timelines, conflicts, and plans.
- `agentic-datas/`: Shared (5 MB) and private (20 MB) AI memory workspaces.

---

## 🛠️ System Requirements & Dependencies

### Arch Linux
```bash
sudo pacman -S qt6-base qt6-webengine qt6-websockets cmake gcc ninja zstd vulkan-tools
```

### Debian / Ubuntu (24.04+)
```bash
sudo apt install qt6-base-dev qt6-base-private-dev qt6-tools-dev qt6-webengine-dev \
    libqt6websockets6-dev cmake g++ ninja-build libzstd-dev ccache vulkan-tools libvulkan1
```

### Windows (MSVC 2022)
- Visual Studio 2022 (with Desktop Development with C++)
- Qt 6.8+ (including `qtwebengine`, `qtwebsockets`, `qtwebchannel`, `qtpositioning`)
- CMake 3.16+
- Python 3.9+
- *(Optional)* [Inno Setup 6](https://jrsoftware.org/isdl.php) (for building the `.exe` Windows installer)

---

## 🚀 Building & Packaging

Ensemble includes a unified Python build orchestrator (`build.py`) with cross-platform wrappers (`build.sh`, `build.ps1`, `build.bat`):

```bash
# Interactive Build Studio GUI
python3 build.py --gui

# Linux Release Build & Unit Tests
./build.sh release --test

# Standalone AppImage Package
./build.sh appimage

# Windows PowerShell (Portable .zip & Installer .exe)
.\build.ps1 release --installer --test
```

---

## 📜 License & Community

- **License**: MIT License ([LICENSE](LICENSE))
- **Code of Conduct**: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)
- **Security Policy**: [SECURITY.md](SECURITY.md)

### ⚖️ LGPLv3 Compliance & Freedom to Modify Qt

Ensemble's application code is licensed under the **MIT License**. Ensemble dynamically links against the **Qt 6 Framework** (and Qt WebEngine), licensed under the **GNU LGPLv3**. In compliance with the LGPLv3:
1. **Dynamic Linking**: Qt components are dynamically linked (`.so` on Linux, `.dll` on Windows).
2. **Freedom to Modify & Replace**: Distributed AppImage packages are standard, unencrypted SquashFS containers. Users can extract the bundle (`./Ensemble-*.AppImage --appimage-extract`), swap any Qt shared libraries in `squashfs-root/usr/lib/`, or inject custom builds using `LD_LIBRARY_PATH`. On Windows, dynamic `.dll` files can be updated directly in the program folder.

## ⚖️ Legal Notice

Ensemble is an independent community project and is not affiliated with, endorsed by, or associated with Archive of Our Own (AO3) or the Organization for Transformative Works (OTW). AO3 and OTW are registered trademarks of the Organization for Transformative Works.
