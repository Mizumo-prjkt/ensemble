# Ensemble Sandboxed Scripting Plugins (.er) Guide

Ensemble includes a sandboxed, memory-bounded scripting runtime using a Python-like Domain-Specific Language (DSL). Plugins are authored as `.er` (Ensemble Runner) files placed in your user plugins directory:
`~/.local/share/Ensemble/Ensemble/plugins/`

Each plugin runs on its own isolated background thread with strictly enforced memory caps (8 MB), recursion limits (64 call frames), execution step caps (500,000 steps per hook), wall-clock timeouts (500ms), and logged network mediation.

---

## 1. Plugin Structure & Metadata

Every `.er` plugin starts with header comments defining its unique ID, display name, and version:

```python
# ID: author.unique-plugin-name
# Name: My Awesome Plugin
# Version: 1.0

@on_toolbar_click("My Action")
def run():
    status.notify("Plugin executed!")
```

---

## 2. Available Built-in Namespaces

### A. `editor` (Editor Interaction)
- `editor.get_chapter_text() -> str`: Returns the plain text of the currently open chapter.
- `editor.get_selection() -> str`: Returns selected text in the active editor.
- `editor.get_chapter_title() -> str`: Returns the title of the active chapter.
- `editor.get_chapter_count() -> int`: Returns the total number of chapters in the project.
- `editor.get_word_count() -> int`: Returns word count of current chapter.
- `editor.get_all_chapters() -> list[dict]`: Returns a list of dicts with `{"index": int, "title": str, "text": str}` for every chapter.
- `editor.get_chapter(index: int) -> dict`: Returns `{"index": int, "title": str, "text": str}` for the specified chapter index.
- `editor.replace_selection(new_text: str)`: *(Requires Editor Write permission)* Replaces active selection with `new_text`.
- `editor.insert_text(text: str)`: *(Requires Editor Write permission)* Inserts text at the cursor.
- `editor.set_chapter_text(text: str)`: *(Requires Editor Write permission)* Replaces current chapter text.

### B. `ui` (Sandboxed UI Windows & Dialogs)
Plugins can create native dialog windows running on the GUI thread without sharing pointers across threads:

#### Object-Oriented Style:
```python
win = ui.create_window("Window Title", 500, 380)
win.add_label("Instructional text")
win.add_input("Placeholder...", "Default value")
win.add_text("Content display area", read_only=True)
win.add_button("Submit", "on_submit")
win.add_button("Close", "close_window")
win.show()
```

#### Window Object Methods:
- `win.show()` / `win.show_window()`: Displays the window in the foreground.
- `win.close()` / `win.close_window()` / `win.dismiss()`: Closes and deletes the window.
- `win.clear()` / `win.clear_window()`: Clears widgets from the window layout.
- `win.add_label(text)`: Adds a word-wrapped label.
- `win.add_text(content, read_only=True)`: Adds a scrollable text area.
- `win.add_input(placeholder, default_text)`: Adds a single-line input field.
- `win.add_button(label, action_name)`: Adds a pushbutton. If the action name or label is `"close"`, `"close_window"`, `"dismiss"`, `"cancel"`, or `"exit"`, clicking it automatically closes the dialog.

#### Flat / Module Style:
- `ui.create_window(title, width, height)`: Creates a new window.
- `ui.show_window([win_or_id])`: Shows active or target window.
- `ui.close_window([win_or_id])`: Closes active or target window.
- `ui.clear_window([win_or_id])`: Clears active or target window.
- `ui.message_box(title, message)`: Shows a native information message box.
- `close_window([win_or_id])`: Global built-in helper to close a window.

### C. `storage` (Isolated Disk Storage)
*(Requires Storage permission)* Each plugin gets an isolated 5 MB storage quota scoped by its secret token:
- `storage.read(key: str) -> str`: Reads string value for key.
- `storage.write(key: str, value: str) -> bool`: Writes string value.
- `storage.delete(key: str) -> bool`: Removes key.
- `storage.list_keys() -> list[str]`: Lists all stored keys.

### D. `http` (Network Access & Mediation)
*(Requires Network permission & Allowed Domain)* Outbound network access is strictly mediated:
- Only HTTPS is permitted (HTTP and cleartext are rejected).
- No automatic redirects (redirects are terminated with security quarantine to prevent link hijacking).
- Unicode homographs and deceptive characters (such as Cyrillic lookalikes) are flagged and rejected.
- `http.get(url: str, [headers_dict]) -> dict`: Performs GET request, returning `{"status": int, "ok": bool, "body": str}`.
- `http.post(url: str, [body: str], [content_type_or_headers]) -> dict`: Performs POST request.
- `http.inspect_link(url: str) -> dict`: Inspects a URL for homographs and character IDs.

### E. `status` (Notifications)
- `status.notify(message: str)`: Displays an informational status message.
- `status.warn(message: str, line: int)`: Displays a warning notification.
- `status.error(message: str, line: int)`: Displays an error notification.

---

## 3. Decorators & Event Hooks

- `@on_toolbar_click("Button Label")`: Registers an action in Ensemble's Tools menu and Plugin Manager runner.
- `@on_chapter_change`: Triggered whenever the user selects or opens a chapter. Function receives `(title, text, index)`.
- `@on_window_action("action_name")`: Triggered when a user clicks a button in a plugin window. Function receives `(inputs_dict, window_id)`.
- `@on_button_click("action_or_label")`: Alias for `@on_window_action`.
- `@on_message`: Handles inter-plugin messages from `plugins.send_message()`.

---

## 4. Curated Sample Plugins

### Sample 1: Dialogue & Quotation Analysis (`dialogue_checker.er`)
```python
# ID: author.dialogue-checker
# Name: Dialogue Checker
# Version: 1.1

@on_toolbar_click("Check Dialogue")
def check_dialogue():
    text = editor.get_chapter_text()
    lines = text.split("\n")
    unbalanced = 0
    total_quotes = 0
    for line in lines:
        c = line.count('"')
        total_quotes += c
        if c % 2 != 0:
            unbalanced += 1

    win = ui.create_window("Dialogue Analysis", 460, 320)
    win.add_label("Chapter Dialogue & Quotation Analysis:")
    win.add_text("Total quotation marks: " + str(total_quotes) + "\nLines with unmatched pairs: " + str(unbalanced))
    win.add_button("Close", "close_window")
    win.show()

@on_chapter_change
def on_load(title, text, index):
    status.notify("Inspecting chapter: " + title)
```

### Sample 2: Word & Reading Time Statistics (`word_counter.er`)
```python
# ID: author.reading-stats
# Name: Word & Reading Stats
# Version: 1.0

@on_toolbar_click("Reading Stats")
def show_stats():
    title = editor.get_chapter_title()
    words = editor.get_word_count()
    text = editor.get_chapter_text()
    chars = len(text)

    # Average adult reading speed: ~225 words per minute
    reading_min = int(words / 225)
    if reading_min == 0:
        reading_min = 1

    win = ui.create_window("Reading Statistics", 460, 340)
    win.add_label("Statistics for: " + title)
    win.add_text("Word Count: " + str(words) + "\nCharacter Count: " + str(chars) + "\nEstimated Reading Time: ~" + str(reading_min) + " min")
    win.add_button("Close", "close_window")
    win.show()
```

### Sample 3: Typographic Smart Quotes Formatter (`smart_quotes.er`)
*(Requires Editor Write permission)*
```python
# ID: author.smart-quotes
# Name: Smart Quotes Formatter
# Version: 1.0

@on_toolbar_click("Format Smart Quotes")
def format_quotes():
    sel = editor.get_selection()
    target_text = sel
    is_selection = True
    if len(target_text) == 0:
        target_text = editor.get_chapter_text()
        is_selection = False
        if len(target_text) == 0:
            status.warn("No text to format", 1)
            return

    # Replace straight quotes with curly quotes: “ (8220) and ” (8221)
    in_quote = False
    result = ""
    for ch in target_text:
        if ch == '"':
            if in_quote:
                result += chr(8221)
                in_quote = False
            else:
                result += chr(8220)
                in_quote = True
        else:
            result += ch

    if is_selection:
        editor.replace_selection(result)
        status.notify("Converted selection to smart quotes")
    else:
        editor.set_chapter_text(result)
        status.notify("Converted chapter to smart quotes")
```

### Sample 4: Persistent Author Scratchpad (`chapter_scratchpad.er`)
*(Requires Storage and UI permissions)*
```python
# ID: author.scratchpad
# Name: Chapter Scratchpad
# Version: 1.0

@on_toolbar_click("Open Scratchpad")
def open_scratchpad():
    title = editor.get_chapter_title()
    key = "note_" + title
    saved_note = storage.read(key)

    win = ui.create_window("Author Notes — " + title, 480, 360)
    win.add_label("Persistent notes for: " + title)
    win.add_input("Enter notes...", saved_note)
    win.add_button("Save Note", "on_save")
    win.add_button("Close", "close_window")
    win.show()

@on_button_click("on_save")
def handle_save(inputs, wid):
    title = editor.get_chapter_title()
    key = "note_" + title
    note_text = inputs.get("text", "")
    storage.write(key, note_text)
    status.notify("Saved note for: " + title)
    close_window(wid)
```

### Sample 5: Web Thesaurus & Definition Lookup (`web_thesaurus.er`)
*(Requires Network and UI permissions; domain whitelist for `api.dictionaryapi.dev`)*
```python
# ID: author.web-lookup
# Name: Web Dictionary Lookup
# Version: 1.0

@on_toolbar_click("Lookup Word")
def lookup_word():
    sel = editor.get_selection().strip()
    if len(sel) == 0:
        status.warn("Please select a word in the editor to look up", 1)
        return

    word = sel.lower()
    url = "https://api.dictionaryapi.dev/api/v2/entries/en/" + word
    resp = http.get(url)

    status_code = resp.get("status", 0)
    if status_code == 200:
        body = resp.get("body", "")
        win = ui.create_window("Definition: " + word, 500, 380)
        win.add_label("Dictionary definition for '" + word + "':")
        win.add_text(body)
        win.add_button("Close", "close_window")
        win.show()
    else:
        status.warn("Definition lookup failed (HTTP " + str(status_code) + ")", 1)
```

---

## 5. Creating Samples in the UI

You can generate any of the above plugins directly inside Ensemble:
1. Open **Tools → 🧩 [Beta] Scripting Plugins Manager…** (`Ctrl+Shift+U`).
2. Click **✨ Create Sample Plugin (.er)** at the bottom.
3. Select any template from the dropdown list.
4. The template will be automatically saved to your plugins directory and loaded.
