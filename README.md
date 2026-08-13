# AO3 Typewriter

A Qt 6 desktop editor for writing Archive of Our Own fanfiction with a Word-like WYSIWYG composer, live AO3 HTML source view, chapter organization, custom work-skin CSS, and preview.

## Features

- **Split editor** — rich-text WYSIWYG pane alongside live AO3 HTML source
- **AO3 HTML export** — semantic tags (`<strong>`, `<em>`, `<h1>`–`<h6>`, lists, blockquotes, links)
- **Chapter management** — add, rename, delete, reorder chapters
- **Work skin CSS** — edit custom body CSS with live preview
- **Project files** — save/load `.ao3proj` JSON (all chapters + CSS in one file)
- **Export** — single chapter or all chapters as HTML files; copy HTML to clipboard

## Dependencies

### Arch Linux

```bash
sudo pacman -S qt6-base qt6-webengine cmake gcc
```

### Debian / Ubuntu

```bash
sudo apt install qt6-base-dev qt6-webengine-dev cmake g++
```

## Build

```bash
cd ao3-typewriter
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run:

```bash
./build/ao3-typewriter
```

## Usage

1. Write in the left editor using the formatting toolbar.
2. Watch AO3 HTML update in the right pane (debounced).
3. Fine-tune HTML in the source pane, then click **Apply to Editor**.
4. Organize chapters in the left sidebar.
5. Edit work-skin CSS in the bottom tab and preview in the **Preview** tab.
6. Save as `.ao3proj` and export individual chapter HTML for pasting into AO3.

## Project File Format

`.ao3proj` is JSON with `formatVersion`, `title`, `workSkinCss`, and a `chapters` array. Each chapter has `id`, `title`, `order`, and `html`.

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N | New project |
| Ctrl+O | Open project |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save As |
| Ctrl+Shift+C | Copy HTML to clipboard |

## Notes

- AO3 server-side HTML cleanup may differ slightly from this editor's sanitizer.
- Image support is via `<img src="url">` links only (no upload/hosting).
- For best results, use semantic HTML (paragraphs in `<p>`, emphasis in `<em>`, etc.).
