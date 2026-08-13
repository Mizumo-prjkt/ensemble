<img src="src/icons/icon.png" width="100" height="100" align="left" alt="Ensemble logo">

# Ensemble

<p align="left">
  <strong>Next-Generation AO3 Writing Suite & Work Skin Composer</strong>
</p>

<p align="left">
  A high-performance Qt 6 & Chromium C++ desktop application for Archive of Our Own (AO3) authors. Features a distraction-free WYSIWYG composer, live AO3 HTML source synchronization, authentic Chromium Work Skin rendering, direct AO3 work/series import, and native ZSTD project archiving.
</p>

---

## ✨ Key Features

- 🎭 **Authentic Chromium Work Skin Preview**: Full Qt6 `QWebEngineView` rendering supporting 100% of AO3 Work Skin features (`border-radius`, `box-shadow`, custom cards like `.chapter-card` and `.liyue-box`, multi-theme Work Skins, and custom typography).
- ⚡ **Zero-Jump Live Preview**: Real-time DOM mutation engine updates your live preview as you type while preserving your scroll position with 100% precision.
- 📥 **Direct AO3 Import**: Authenticate with your AO3 account (via Session Cookie or login credentials) to import entire multi-chapter works, series, pseuds, co-authored works, and custom Work Skins seamlessly.
- 🎨 **Work Skin Composer & Visual Inspector**: Edit custom CSS with syntax highlighting, auto-detect classes, apply styles via context menu, and sync Work Skins with AO3 via the built-in Skin Diff Tool.
- 📦 **Native ZSTD Archiving**: Save and load `.ao3proj` files using Zstandard compression for lightning-fast save times and minimal disk footprint.
- 🗺️ **Proportional Minimap Sidebar**: High-visibility scrollbar minimap offering an instant visual overview of your document structure with color-coded syntax highlights.
- 🔀 **Split Workspace**: Simultaneous WYSIWYG rich text editor, live AO3 HTML source view, and Work Skin CSS editor with debounced auto-sync.
- 📑 **Chapter Management**: Effortlessly add, rename, delete, and reorder chapters.

---

## 🛠️ Dependencies

### Arch Linux
```bash
sudo pacman -S qt6-base qt6-webengine cmake gcc ninja zstd
```

### Debian / Ubuntu
```bash
sudo apt install qt6-base-dev qt6-webengine-dev cmake g++ ninja-build libzstd-dev
```

---

## 🚀 Building & Running

Ensemble includes a convenient build script supporting both **Release** and **Debug** compilation modes with automatic Git status tagging (`-dirty`).

### Release Mode (Recommended)
```bash
./build.sh release
```
*Or manually via CMake:*
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/ensemble
```

### Debug Mode
```bash
./build.sh
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

---

## 📄 Project File Format

Ensemble saves projects as `.ao3proj` binary archives compressed with **Zstandard (ZSTD)**. Inside each project file is a structured JSON metadata document containing:
- `formatVersion`
- `title`
- `workSkinCss`
- `chapters` array (`id`, `title`, `order`, `html`)

---

## 📜 License & Credits

- **Developer**: [Mizumo-prjkt](https.github.com/Mizumo-prjkt)
- **Built with**: Qt 6, Chromium WebEngine & Zstandard
- **License**: MIT License
