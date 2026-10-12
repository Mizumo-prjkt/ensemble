<img src="src/icons/icon.png" width="100" height="100" align="left" alt="Ensemble logo">

# Ensemble

<p align="left">
  <strong>Next-Generation AO3 Writing Suite & Work Skin Composer</strong>
</p>

<p align="left">
  A high-performance Qt 6 & Chromium C++ desktop application for Archive of Our Own (AO3) authors. Features a distraction-free WYSIWYG composer, live AO3 HTML source synchronization, authentic Chromium Work Skin rendering, direct AO3 work/series import, agentic lore graphs, and native ZSTD project archiving with optional AES-256-GCM encryption.
</p>

---

## ✨ Key Features

- 🎭 **Authentic Chromium Work Skin Preview**: Full Qt6 `QWebEngineView` rendering supporting 100% of AO3 Work Skin features (`border-radius`, `box-shadow`, custom cards like `.chapter-card` and `.liyue-box`, multi-theme Work Skins, and custom typography).
- ⚡ **Zero-Jump Live Preview**: Real-time DOM mutation engine updates your live preview as you type while preserving your scroll position with 100% precision.
- 📥 **Direct AO3 Import**: Authenticate with your AO3 account (via Session Cookie or login credentials) to import entire multi-chapter works, series, pseuds, co-authored works, and custom Work Skins seamlessly.
- 🎨 **Work Skin Composer & Visual Inspector**: Edit custom CSS with syntax highlighting, auto-detect classes, apply styles via context menu, and sync Work Skins with AO3 via the built-in Skin Diff Tool.
- 🕸️ **Agentic Lore Graph & Management**: Interactive node graph mapping characters, factions, relationships, and lore timelines, with real-time AI context generation. (**In Beta**)
- 🌓 **Dynamic Theme Engine**: Tailor your writing space with customizable Dark, Light, High Contrast, and Custom UI color palettes.
- 🗺️ **Proportional Minimap Sidebar**: High-visibility scrollbar minimap offering an instant visual overview of your document structure with color-coded syntax highlights.
- 🔀 **Split Workspace**: Simultaneous WYSIWYG rich text editor, live AO3 HTML source view, and Work Skin CSS editor with debounced auto-sync.
- 📑 **Chapter Management**: Effortlessly add, rename, delete, and reorder chapters.
- 🚀 **Single-Instance Application**: Direct integration with system file managers to open `.ao3proj` files seamlessly without creating accidental duplicate processes.

---

## 🛠️ System Requirements & Dependencies

### Arch Linux
```bash
sudo pacman -S qt6-base qt6-webengine cmake gcc ninja zstd vulkan-tools
```

### Debian / Ubuntu (24.04+)
```bash
sudo apt install qt6-base-dev qt6-base-private-dev qt6-tools-dev qt6-webengine-dev \
    libqt6websockets6-dev cmake g++ ninja-build libzstd-dev ccache vulkan-tools libvulkan1
```

### Windows (MSVC 2022)
- Visual Studio 2022 (with Desktop Development with C++)
- Qt 6.8+ (including `qtwebengine`, `qtwebchannel`, `qtpositioning`)
- CMake 3.16+
- Python 3.9+
- *(Optional)* [Inno Setup 6](https://jrsoftware.org/isdl.php) (for building the `.exe` Windows installer)

---

## 🚀 Building & Packaging

Ensemble provides a unified Python build orchestrator (`build.py`) with cross-platform wrappers (`build.sh`, `build.ps1`, `build.bat`), supporting an interactive GUI build studio, automated packaging, compiler flag guidance, and CI/CD automation.

### Interactive Build Studio (GUI)
Launch the visual build studio with live compiler flag assist and CI one-liner generator:
```bash
python3 build.py --gui
```
From the GUI, configure targets, custom compiler flags, version tags, and click **"📋 Copy One-Liner"** to export exact command-line arguments.

### Compiler & CMake Flag Guidance
Query the built-in compiler optimization and security flag knowledge base:
```bash
python3 build.py --list-flags
```

### CLI One-Liners (Linux & macOS)

```bash
# 1. Native Linux Release Build
python3 build.py --target linux --build-type Release

# 2. Package Standalone Linux Tarball & Run Unit Tests
python3 build.py --target linux --build-type Release --package --test

# 3. Package Standalone AppImage with Custom Codename & Download Tools
python3 build.py --target appimage --build-type Release --localversion agnestachyon --download-tools --test

# 4. Custom Compiler Optimization Flags (-O3, CPU tuning)
python3 build.py --cxxflags="-O3 -march=x86-64-v3" --build-type Release

# 5. Shell wrapper shorthand
./build.sh release
./build.sh appimage
```

### CLI One-Liners (Windows PowerShell / CMD)

```powershell
# 1. Native Windows Release Build
.\build.ps1 release

# 2. Build and Package Standalone Portable Zip (.zip)
.\build.ps1 release --package

# 3. Build and Package Both Portable Zip & Inno Setup Installer (.exe)
.\build.ps1 release --installer

# 4. Run Unit Tests with Debug symbols
.\build.ps1 debug --test
```

---

## 💻 Command-Line Options

Ensemble can be launched directly from the command line, terminal, or file manager:

```bash
# Open an existing story directly
ensemble /path/to/my_story.ao3proj

# Force software rasterization (useful for headless setups or GPU-less virtual machines)
ensemble --disable-gpu-acceleration
```

| Option | Description |
|--------|-------------|
| `[project.ao3proj]` | Optional path to a `.ao3proj` file to load immediately on startup. |
| `--disable-gpu-acceleration`, `--disable-gpu` | Disable hardware GPU acceleration and force software rasterization across WebEngine and Qt. |
| `--allow-multiple-instances`, `--new-instance` | Allow multiple independent instances of Ensemble to run concurrently. |
| `--enable-mcp` | Start embedded Model Context Protocol (MCP) WebSocket server on launch. |
| `--mcp-port <port>` | Specify preferred port for MCP server (default: `26303`). |
| `--mcp-bridge` | Run in Model Context Protocol (MCP) Stdio Bridge mode for AI agents. |
| `--mcp-status` | Query active MCP Server status and verify loopback reachability. |
| `--send-mcp-manual-to-agents` | Export `ENSEMBLE_MCP_MANUAL.md` and configure MCP schemas in Antigravity & Claude. |
| `-h`, `--help` | Displays command-line options help. |
| `-v`, `--version` | Displays version and build configuration information. |

---

## 🤖 Model Context Protocol (MCP) & AI Integration

Ensemble exposes an embedded **Model Context Protocol (MCP)** server over localhost WebSocket (`ws://127.0.0.1:26303`) with a hybrid Stdio bridge for developer AI pair-programmers (such as **Google Antigravity**, **Claude Code**, and **Claude Desktop**).

### Quick Setup for AI Agents
Export the agent manual and automatically register tool schemas:
```bash
ensemble --send-mcp-manual-to-agents
```

### Checking Server & Firewall Status
Diagnose loopback connectivity and reachability:
```bash
ensemble --mcp-status
```

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | New Project |
| `Ctrl+O` | Open Project (`.ao3proj`) |
| `Ctrl+S` | Save Project |
| `Ctrl+Shift+S` | Save Project As |
| `Ctrl+Shift+I` | Import Work from AO3 |
| `Ctrl+Shift+C` | Copy AO3 Clean HTML to Clipboard |
| `Ctrl+P` | Toggle Side-by-Side WebEngine Preview |
| `Ctrl+Shift+P` | Pop Out WebEngine Preview Window |
| `Ctrl+F` | Find & Replace |
| `Ctrl+Shift+L` | Open Lore Graph Studio |
| `Ctrl+Shift+T` | Open Agentic Thinking & Thoughts Viewer |

---

## 📄 Project File Format (`.ao3proj`)

Ensemble saves projects as binary archives compressed with **Zstandard (ZSTD)**, with optional **AES-256-GCM** encryption for privacy and security. Inside each unencrypted or unlocked project file is a structured JSON metadata document:
- `formatVersion`
- `title`
- `workSkinCss`
- `chapters` array (`id`, `title`, `order`, `html`)
- `lore` metadata (characters, affiliations, relations, timeline nodes)
- `agentic` thoughts and metadata cache

---

## 🔄 Automated CI/CD Workflows

Ensemble utilizes a fault-tolerant multi-platform GitHub Actions workflow (`.github/workflows/build.yaml`):
- **Matrix Parallelism**: Concurrently compiles Linux AppImage, Linux portable archive, and Windows 64-bit packages (portable `.zip` and Inno Setup installer `.exe`) across both `Release` and `Debug` variants.
- **Fault-Tolerant Deployment**: If an isolated target encounters an environment error, sibling builds are not interrupted; all successful targets and verified tests are staged and released.
- **Automated Verification**: Integrates Git tag annotations, release notes extraction, and cryptographic SHA256 checksum generation into every GitHub Release.

---

## 📜 License & Credits

- **Developer**: [Mizumo-prjkt](https://github.com/Mizumo-prjkt)
- **Built with**: Qt 6, Chromium WebEngine, Zstandard, and Miniz
- **License**: MIT License

## ⚖️ Legal Notice

This project is an independent community tool and is not affiliated with, endorsed by, or associated with Archive of Our Own (AO3), the Organization for Transformative Works (OTW), or any of their affiliates. AO3 and OTW are registered trademarks of the Organization for Transformative Works.

Qt is a trademark of Qt Group Plc. Chromium is a trademark of Google LLC. Zstandard is a trademark of Meta Platforms, Inc.
