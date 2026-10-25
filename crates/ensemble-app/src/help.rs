use ensemble_mcp::McpServer;
use ensemble_theme::ThemeManager;

#[derive(Debug, Clone)]
pub struct HelpTopic {
    pub id: String,
    pub icon: String,
    pub title: String,
    pub summary: String,
    pub content_html: String,
    pub hidden: bool,
}

pub struct HelpGuideController {
    topics: Vec<HelpTopic>,
    selected_index: usize,
    search_query: String,
    rendered_html: String,
}

impl Default for HelpGuideController {
    fn default() -> Self {
        Self::new()
    }
}

impl HelpGuideController {
    pub fn new() -> Self {
        let mut ctrl = Self {
            topics: Self::populate_topics(),
            selected_index: 0,
            search_query: String::new(),
            rendered_html: String::new(),
        };
        ctrl.update_theme();
        ctrl
    }

    fn populate_topics() -> Vec<HelpTopic> {
        let discovery_path = McpServer::discovery_file_path();
        let app_exe = std::env::current_exe()
            .ok()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|| "/usr/local/bin/ensemble".to_string());

        vec![
            HelpTopic {
                id: "getting_started".to_string(),
                icon: "🚀".to_string(),
                title: "1. Getting Started, Archives (.ao3proj) & Exporting".to_string(),
                summary: "Workspace overview, single-file .ao3proj containers, memory offloading, and posting to AO3.".to_string(),
                content_html: r#"
<h1>🚀 Getting Started, Project Archives &amp; Exporting to AO3</h1>
<p class="lead">Ensemble is a purpose-built fanfiction typewriter and IDE designed specifically for authors on <b>Archive of Our Own (AO3)</b>. It unifies distraction-free rich-text writing, an IDE-grade Raw HTML &amp; CSS editor, live AO3 skin previewing, worldbuilding lore graphs, git-like snapshots, and sandboxed <code>.er</code> plugins inside a single-file project archive.</p>

<div class="callout">
  <b>Quick Start Workflow:</b> From the Welcome Screen, click <b>New Project</b> (<kbd>Ctrl+N</kbd>) or <b>Open Project…</b> (<kbd>Ctrl+O</kbd>). Write in the <b>Rich Text (WYSIWYG)</b> tab or switch to <b>Raw HTML</b> for fine-grained AO3 markup control, preview live with <kbd>Ctrl+P</kbd>, and click the <b>Copy AO3 HTML</b> button in the header bar when you are ready to post to AO3.
</div>

<h2>📦 Unified Project Files &amp; Archiving (<code>.ao3proj</code>)</h2>
<p>Every Ensemble story is saved as a single, portable <code>.ao3proj</code> container compressed with <b>Zstandard (zstd)</b> for high-speed saving and minimal disk footprint. Inside a <code>.ao3proj</code> archive, Ensemble organizes:</p>
<table>
  <thead>
    <tr><th>Internal Path</th><th>Purpose</th></tr>
  </thead>
  <tbody>
    <tr><td><code>metadata.xml</code></td><td>Story title, author, AO3 Rating, Archive Warnings, Categories, Fandoms, Relationships, Characters, Additional Tags, Summary, and Author Notes.</td></tr>
    <tr><td><code>chapters/chapter_XXX.html</code></td><td>Individual chapter HTML files stored with <b>Strict 1-Based</b> numbering.</td></tr>
    <tr><td><code>workskin.css</code></td><td>Custom AO3 Work Skin CSS stylesheet automatically scoped to <code>#workskin</code> during preview and linting.</td></tr>
    <tr><td><code>lore.grph</code></td><td>Visual Lore Graph &amp; Story Mapping database using the binary-header <code>.grph</code> DSL (<code>\x89GRPH\x01\r\n</code>).</td></tr>
    <tr><td><code>history/</code> &amp; <code>history.cmt</code></td><td>Git-like story snapshots, commit metadata, and line-by-line unified revision diffs.</td></tr>
    <tr><td><code>embeds.json</code></td><td>External URL-based image embed definitions, dimensions, and accessibility alt-text.</td></tr>
    <tr><td><code>agentic-datas/</code></td><td>Isolated multi-agent co-writing memory and audit logs.</td></tr>
  </tbody>
</table>

<h2>⚡ Smart Chapter Memory Offloading &amp; Autosave</h2>
<p>When working on long multi-chapter fanfictions (100k+ words), Ensemble keeps memory consumption low through automatic chapter offloading:</p>
<ul>
  <li><b>Inactive Chapter Unloading:</b> Chapters that are not currently being viewed are unloaded from active RAM.</li>
  <li><b>Autosave Recovery:</b> If an inactive chapter has unsaved edits when you switch away, Ensemble writes those edits to its on-disk autosave swap before freeing its in-memory buffers, then transparently reloads it with a progress indicator when you return.</li>
  <li><b>Unsaved Changes Guard:</b> Closing the window or returning to the Welcome Screen while a project has unsaved changes prompts you to <b>Save &amp; Continue</b>, <b>Discard Changes</b>, or <b>Cancel</b>.</li>
</ul>

<h2>📤 Exporting &amp; Posting to Archive of Our Own</h2>
<ul>
  <li><b>One-Click Copy Chapter HTML (<kbd>Ctrl+Shift+C</kbd> or Header Copy Button):</b> Sanitizes and copies the active chapter's clean AO3-compliant HTML directly to your system clipboard, ready to paste into AO3's <i>HTML</i> editor tab.</li>
  <li><b>Work Skin CSS Upload:</b> Copy the contents of the <b>Work Skin CSS</b> tab into <i>AO3 → Skins → My Work Skins</i> and select that Work Skin on your AO3 work settings page.</li>
  <li><b>Importing Existing Works (<kbd>Ctrl+Shift+I</kbd>):</b> Import existing HTML chapters or `.ao3proj` files directly into your workspace.</li>
</ul>
"#.to_string(),
                hidden: false,
            },
            HelpTopic {
                id: "editor_formatting".to_string(),
                icon: "✍️".to_string(),
                title: "2. Rich Text (WYSIWYG), Raw HTML IDE & Sanitization".to_string(),
                summary: "WYSIWYG formatting toolbar, Raw HTML code editor, minimap, line gutter, and AO3 HTML rules.".to_string(),
                content_html: r#"
<h1>✍️ Rich Text (WYSIWYG), Raw HTML IDE &amp; AO3 Sanitization</h1>
<p class="lead">Compose naturally in the <b>Rich Text (WYSIWYG)</b> visual editor or switch seamlessly to the <b>Raw HTML</b> IDE tab. Both editors stay synchronized and enforce Archive of Our Own's HTML sanitizer rules.</p>

<h2>🖋️ Rich Text (WYSIWYG) Editor</h2>
<p>The Rich Text editor provides a word-processor experience tailored to AO3's semantic HTML output:</p>
<ul>
  <li><b>Inline Formatting:</b> Bold (<code>&lt;strong&gt;</code>), Italic (<code>&lt;em&gt;</code>), Underline (<code>&lt;u&gt;</code>), Strikethrough (<code>&lt;s&gt;</code>), Inline Code (<code>&lt;code&gt;</code>), Subscript, and Superscript.</li>
  <li><b>Block Formatting:</b> Paragraph (<code>&lt;p&gt;</code>), Headings H1–H4, Blockquote (<code>&lt;blockquote&gt;</code>), Preformatted Code Block (<code>&lt;pre&gt;</code>), Bullet Lists (<code>&lt;ul&gt;</code>), and Numbered Lists (<code>&lt;ol&gt;</code>).</li>
  <li><b>Alignment &amp; Scene Breaks:</b> Left, Center, Right, and Justify paragraph alignment, plus 1-click Horizontal Scene Break (<code>&lt;hr&gt;</code>), Link insertion (<code>&lt;a href="…"&gt;</code>), Image insertion (<code>&lt;img src="…"&gt;</code>), and Clear Formatting.</li>
  <li><b>AO3 Paragraph Semantics:</b> Pressing <kbd>Enter</kbd> creates clean <code>&lt;p&gt;…&lt;/p&gt;</code> blocks rather than brittle double <code>&lt;br&gt;</code> tags, preventing spacing glitches when AO3's parser processes your chapter. Avoid arbitrary inline <code>&lt;span style="…"&gt;</code> styles in prose—use Work Skin CSS classes instead!</li>
</ul>

<h2>💻 Raw HTML IDE Editor</h2>
<p>Switching to the <b>Raw HTML</b> tab opens an IDE-style source editor equipped with:</p>
<ul>
  <li><b>Synchronized Line Number Gutter:</b> Hardware-accelerated Cairo line number gutter that scrolls smoothly even on massive chapters.</li>
  <li><b>Interactive Code Minimap:</b> Visual overview of your chapter structure on the right edge—click or drag the viewport rectangle to jump through long chapters instantaneously.</li>
  <li><b>Live Tag Breadcrumb Bar:</b> Displays the exact ancestor HTML tag path at your cursor (e.g., <code>html &gt; body &gt; div.chat &gt; p.alice</code>) along with real-time <code>Ln X, Col Y</code> coordinates.</li>
  <li><b>Format &amp; Indent HTML:</b> Click <b>Format &amp; Indent</b> in the Raw HTML toolbar to automatically pretty-print and indent nested HTML tags for readability.</li>
</ul>

<h2>🏷️ Permitted AO3 HTML Tags &amp; Sanitization</h2>
<p>AO3 strips unsupported tags (such as <code>&lt;script&gt;</code>, <code>&lt;style&gt;</code>, <code>&lt;iframe&gt;</code>, or <code>&lt;form&gt;</code>) and restricts attributes to safe properties (<code>class</code>, <code>id</code>, <code>href</code>, <code>src</code>, <code>alt</code>, <code>width</code>, <code>height</code>, <code>title</code>, <code>align</code>). Ensemble's built-in sanitizer and linter validate your markup against AO3's whitelist in real time.</p>
"#.to_string(),
                hidden: false,
            },
            HelpTopic {
                id: "chapter_manager".to_string(),
                icon: "📑".to_string(),
                title: "3. Chapters, Project Properties, Metadata & Image Embeds".to_string(),
                summary: "Strict 1-based Chapter Manager, AO3 metadata.xml tags, ratings, warnings, and URL image embeds.".to_string(),
                content_html: r#"
<h1>📑 Chapters, Project Properties, Metadata &amp; Image Embeds</h1>
<p class="lead">Manage multi-chapter story structure, configure full Archive of Our Own work metadata, and attach external URL illustrations with generated AO3 HTML snippets.</p>

<h2>📑 Visual Chapter Manager &amp; Strict 1-Based Numbering</h2>
<p>In accordance with Archive of Our Own standards, Ensemble enforces <b>Strict 1-Based</b> chapter numbering—Chapter 1 is always Chapter 1.</p>
<ul>
  <li><b>Sidebar Quick Controls:</b> Use the <b>+</b> button to add chapters, the trash button to delete a chapter, or the list icon to open the full <b>Chapter Manager</b> dialog.</li>
  <li><b>Reordering &amp; Bulk Management:</b> Inside the Chapter Manager, move chapters up or down, rename chapter titles, inspect per-chapter word counts and HTML tag metrics, or click <b>Delete All Chapters</b> to reset the work to a clean state (which displays an <i>"Add a New Chapter to Get Started"</i> prompt).</li>
</ul>

<h2>⚙️ Project Properties &amp; AO3 Metadata (<code>metadata.xml</code>)</h2>
<p>Open <b>Project Properties &amp; Tags…</b> from the main menu to configure all AO3 work metadata stored in <code>metadata.xml</code>:</p>
<table>
  <thead>
    <tr><th>Field</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><b>Work Title &amp; Author</b></td><td>Primary story title and pseud/author credit displayed in the AO3 header.</td></tr>
    <tr><td><b>AO3 Rating</b></td><td><i>Not Rated</i>, <i>General Audiences</i>, <i>Teen And Up Audiences</i>, <i>Mature</i>, or <i>Explicit</i>.</td></tr>
    <tr><td><b>Archive Warnings</b></td><td><i>Creator Chose Not To Use Archive Warnings</i>, <i>No Archive Warnings Apply</i>, <i>Graphic Depictions Of Violence</i>, <i>Major Character Death</i>, <i>Rape/Non-Con</i>, <i>Underage Sex</i>.</td></tr>
    <tr><td><b>Categories &amp; Tags</b></td><td>F/F, F/M, Gen, M/M, Multi, Other, plus comma-separated <b>Fandoms</b>, <b>Relationships</b>, <b>Characters</b>, and <b>Additional (Freeform) Tags</b>.</td></tr>
    <tr><td><b>Summary &amp; Notes</b></td><td>Story summary, Beginning Author's Notes, and End Author's Notes rendered in the Live Preview.</td></tr>
  </tbody>
</table>
<p>If you close Project Properties (`✕`) with unsaved metadata modifications, Ensemble automatically asks whether you want to save or discard your changes.</p>

<h2>🖼️ Image Embed Attachments (URL-Based)</h2>
<p>AO3 hosts text rather than binary image files, requiring illustrations to be linked via external HTTPS URLs. Inside the <b>Image Embed</b> manager:</p>
<ul>
  <li>Register remote image URLs along with descriptive <b>Alt Text</b> (for screen readers) and optional width/height constraints.</li>
  <li>Click <b>1-Click 'Copy AO3 HTML Snippet'</b> to copy a sanitized <code>&lt;img src="…" alt="…" /&gt;</code> snippet directly to your clipboard or insert it into the active chapter.</li>
</ul>
"#.to_string(),
                hidden: false,
            },
            HelpTopic {
                id: "work_skins".to_string(),
                icon: "🎨".to_string(),
                title: "4. AO3 Work Skins (CSS), Live Preview & Diagnostics".to_string(),
                summary: "Custom CSS for chat bubbles & letters, Live Sideview Preview (Default & Reversi), and HTML/CSS linting.".to_string(),
                content_html: r#"
<h1>🎨 AO3 Work Skins (CSS), Live Preview &amp; Diagnostics</h1>
<p class="lead">Work Skin stylesheets defined on AO3 let you create custom visual elements—such as SMS text message bubbles, social media posts, handwritten letters, terminal screens, or RPG status boxes—using pure HTML and CSS.</p>

<h2>🎨 Authoring Work Skin CSS</h2>
<p>Switch to the <b>Work Skin CSS</b> tab in the editor to write stylesheet rules for your project:</p>
<pre><code>#workskin .chat-container {
  max-width: 420px;
  margin: 1em auto;
  font-family: sans-serif;
}

#workskin .bubble-left {
  background: #e5e5ea;
  color: #000000;
  border-radius: 16px 16px 16px 4px;
  padding: 8px 14px;
  margin: 6px 0;
}</code></pre>
<p>Then reference those classes in your chapter HTML (e.g. <code>&lt;div class="chat-container"&gt;&lt;p class="bubble-left"&gt;Hey!&lt;/p&gt;&lt;/div&gt;</code>).</p>

<h2>🌐 Live WebEngine Preview (<kbd>Ctrl+P</kbd>)</h2>
<p>Press <kbd>Ctrl+P</kbd> or click the dual-pane icon in the header bar to toggle the <b>Live Preview Sideview</b> right alongside your editor:</p>
<ul>
  <li><b>Default Light vs. AO3 Reversi Dark:</b> Toggle between <b>Default Light</b> (classic white/warm AO3 page) and <b>Reversi Dark</b> (<code>#111111</code> background with <code>#990000</code> crimson accents) to verify that your custom Work Skin colors remain legible for dark-mode readers!</li>
  <li><b>Full AO3 Header Simulation:</b> Renders your project's Rating, Warnings, Fandoms, Relationships, Characters, Additional Tags, Summary, Beginning/End Notes, and Work Skin CSS exactly as they appear on Archive of Our Own.</li>
</ul>

<h2>🔍 Diagnostics &amp; Linting Panel</h2>
<p>Ensemble continuously validates your chapter HTML and Work Skin CSS:</p>
<ul>
  <li><b>Header Status Badge (<code>✓ 0 Problems</code>):</b> Displays the live count of warnings and errors in the top header bar and the bottom editor diagnostic strip.</li>
  <li><b>Cross-Checking HTML &amp; CSS:</b> Detects unclosed HTML tags, disallowed AO3 tags/attributes, and undefined CSS classes.</li>
  <li><b>Click-to-Jump:</b> Open the <b>Problems</b> dialog and click any diagnostic row to jump directly to the offending line in the Raw HTML editor.</li>
</ul>
"#.to_string(),
                hidden: false,
            },
            HelpTopic {
                id: "lore_graph".to_string(),
                icon: "🗺️".to_string(),
                title: "5. Lore Graph (.grph) & Story Snapshots (history/)".to_string(),
                summary: "Visual worldbuilding canvas (.grph DSL) and Git-like revision snapshots with unified diffs (history.cmt).".to_string(),
                content_html: r#"
<h1>🗺️ Lore Graph (<code>.grph</code>) &amp; Git-Like Story Snapshots (<code>history/</code>)</h1>
<p class="lead">Keep track of complex character relationships, factions, timelines, and story revisions directly inside your <code>.ao3proj</code> file without needing external tools.</p>

<h2>🗺️ Lore Graph &amp; Story Mapping (<code>.grph</code>)</h2>
<p>Accessible from <b>Project Properties → Lore Graph</b>, the visual worldbuilding canvas lets you map out your story universe:</p>
<ul>
  <li><b>Entity Nodes &amp; Kinds:</b> Create nodes for <b>Characters</b>, <b>Locations</b>, <b>Items / Artifacts</b>, <b>Factions</b>, and <b>Plot Events</b>, complete with custom colors, summaries, and canvas coordinates.</li>
  <li><b>Relationship Edges:</b> Connect nodes with labeled, directional or bidirectional relationship links (e.g., <i>"Rivals"</i>, <i>"Secretly Allies"</i>, <i>"Mentors"</i>).</li>
  <li><b>Independent <code>.grph</code> DSL Format:</b> Stored in <code>lore.grph</code> with the binary magic header <code>\x89GRPH\x01\r\n</code> followed by declarative node and edge statements.</li>
</ul>

<h2>📸 Git-Like Snapshots &amp; Revision Diffs (<code>history/</code>)</h2>
<p>Accessible from <b>Project Properties → Snapshots &amp; History</b>, Ensemble includes a built-in version control system tailored for prose:</p>
<ul>
  <li><b>Snapshot Commits (<code>history.cmt</code>):</b> Capture named checkpoints of your entire project at any time (e.g., <i>"Draft 1 before rewriting Chapter 3 ending"</i>). Each commit records the timestamp, author/host tag, word count delta, and compressed chapter snapshots inside <code>history/</code>.</li>
  <li><b>Unified Line-by-Line Diff Viewer:</b> Select any historical snapshot to inspect a color-coded unified diff (<code>+</code> additions in green, <code>-</code> removals in red) comparing that snapshot against your current working draft.</li>
</ul>
"#.to_string(),
                hidden: false,
            },
            HelpTopic {
                id: "mcp_setup".to_string(),
                icon: "🤖".to_string(),
                title: "6. AI Agent MCP Setup & Multi-Agent Collaboration".to_string(),
                summary: "Embedded Model Context Protocol server, --mcp-bridge, agentic-datas/ quotas, and live co-authoring.".to_string(),
                content_html: format!(
                    r#"
<h1>🤖 AI Agent MCP Setup &amp; Multi-Agent Collaboration</h1>
<p class="lead">Ensemble features an embedded <b>Model Context Protocol (MCP)</b> server that lets external AI assistants (Claude Desktop, Claude Code CLI, Cursor, Gemini Antigravity, etc.) inspect your lore graph, lint chapters, edit Work Skin CSS, and collaborate with you in real time.</p>

<div class="callout">
  <b>Transparency Note:</b> As reminded on Ensemble's Welcome Screen—always be honest with your readers if you use AI assistance in your fanfiction!
</div>

<h2>🔌 Connecting an MCP Client</h2>
<p>When Ensemble is running, its local WebSocket MCP server listens on <code>ws://127.0.0.1:26303</code> and writes an automatic discovery manifest to:</p>
<pre><code>{}</code></pre>
<p>To connect stdio-based MCP clients (such as Claude Desktop, Cursor, or Antigravity), configure the command to launch Ensemble with the <code>--mcp-bridge</code> flag:</p>
<pre><code>{{
  "mcpServers": {{
    "ensemble": {{
      "command": "{}",
      "args": ["--mcp-bridge"]
    }}
  }}
}}</code></pre>

<h2>🛠️ Exposed MCP Tools</h2>
<table>
  <thead>
    <tr><th>Category</th><th>Tool Names</th></tr>
  </thead>
  <tbody>
    <tr><td><b>Project &amp; Chapters</b></td><td><code>get_project_summary</code>, <code>list_chapters</code>, <code>get_chapter</code>, <code>update_chapter_html</code>, <code>add_chapter</code>, <code>delete_chapter</code>, <code>reorder_chapters</code>, <code>update_project_metadata</code></td></tr>
    <tr><td><b>Work Skin &amp; Linting</b></td><td><code>get_workskin_css</code>, <code>set_workskin_css</code>, <code>get_problems</code>, <code>get_preview_screenshot</code></td></tr>
    <tr><td><b>Lore &amp; History</b></td><td><code>get_lore_graph</code>, <code>add_lore_node</code>, <code>connect_lore_nodes</code>, <code>get_project_history</code>, <code>add_history_commit</code>, <code>get_snapshot_diff</code></td></tr>
    <tr><td><b>Multi-Agent Memory</b></td><td><code>register_agent_presence</code>, <code>get_agentic_summary</code>, <code>read_agentic_file</code>, <code>write_agentic_file</code>, <code>post_agent_thought</code>, <code>append_agent_history</code></td></tr>
  </tbody>
</table>

<h2>🧠 Multi-Agent Collaboration &amp; Quotas (<code>agentic-datas/</code>)</h2>
<p>Multiple agents can collaborate safely inside the archive's <code>agentic-datas/</code> namespace with strict storage quotas enforced by Ensemble:</p>
<ul>
  <li><b>Shared Coroutine Storage:</b> Capped at <b>5 MB</b> across shared outline/notes files.</li>
  <li><b>Per-Agent Localized Memory:</b> Capped at <b>20 MB</b> per registered agent ID, with full audit logging via <code>get_agent_history</code>.</li>
</ul>
"#,
                    discovery_path, app_exe
                ),
                hidden: false,
            },
            HelpTopic {
                id: "plugins_dsl".to_string(),
                icon: "🧩".to_string(),
                title: "7. Sandboxed Plugins (.er), UI SDK & Actions Guide".to_string(),
                summary: "Complete guide to .er plugins, 1-click file import, exposed Ensemble UI widgets, editor actions, and security.".to_string(),
                content_html: r#"
<h1>🧩 Sandboxed Scripting Engine (<code>.er</code>), Exposed UI SDK &amp; Actions</h1>
<p class="lead">Ensemble includes a hardened, memory-bounded Python-like scripting engine (<code>.er</code>) that lets you extend the editor with custom automation, text transforms, toolbar actions, and interactive plugin windows built using <b>Ensemble's Exposed UI Widget Library</b>.</p>

<h2>📦 Managing &amp; Importing Plugins in the Plugin Manager</h2>
<p>Open <b>Plugins &amp; Extensions…</b> from the Welcome Screen or the Header Menu to launch the standalone <b>Ensemble Plugin Manager</b>:</p>
<ul>
  <li><b>1-Click File Import (<code>+ Import Plugin (.er)…</code>):</b> Click <b>Import Plugin (.er)…</b> and pick any <code>.er</code> script file from your computer. Ensemble validates the script, copies it into your user plugins directory, assigns it an isolated storage UIDP, and activates it immediately.</li>
  <li><b>Plugin UI &amp; Actions Runner:</b> Trigger any plugin's registered <code>@on_toolbar_click</code> actions directly, or open the plugin's custom UI panel rendered natively with Ensemble's safe widget toolkit.</li>
  <li><b>Granular Permissions &amp; Quarantine:</b> Toggle per-plugin permissions for <b>Editor Read</b>, <b>Editor Write</b>, <b>Isolated Storage</b>, <b>Custom UI Windows</b>, and <b>HTTPS Network</b> allowlists.</li>
  <li><b>Plugin Repository (Future-Ready):</b> Browse curated community plugin templates and configure repository endpoints for future 1-click store installations.</li>
</ul>

<h2>📝 Plugin Header Metadata &amp; Lifecycle Decorators</h2>
<p>Every <code>.er</code> script begins with metadata comments followed by Python-style functions decorated with event hooks:</p>
<pre><code># @name: Dialogue &amp; Scene Formatter
# @version: 1.2.0
# @author: YourName
# @description: Opens a custom UI tool to wrap selected text in AO3 Work Skin chat bubbles.

@on_chapter_change
def handle_chapter_switch(chapter_idx):
    words = editor.get_word_count()
    status.set_badge("~" + str(int(words / 200)) + " min read")

@on_toolbar_click("Open Scene Formatter UI")
def open_formatter_ui():
    ui.create_window("AO3 Scene &amp; Dialogue Builder", 460, 360)
    ui.add_label("lbl_info", "Configure speaker class and dialogue line:")
    ui.add_input("inp_speaker", "CSS class (e.g. bubble-left)", "bubble-left")
    ui.add_text_area("txt_line", editor.get_selection())
    ui.add_button("btn_insert", "Insert Chat Bubble into Chapter", "on_insert_clicked")
    ui.show_window()

def on_insert_clicked(widget_id):
    cls = ui.get_text("inp_speaker")
    line = ui.get_text("txt_line")
    snippet = '&lt;p class="' + cls + '"&gt;' + line + '&lt;/p&gt;\n'
    editor.insert_html(snippet)
    status.notify("Inserted chat bubble (" + cls + ")")
    ui.close_window()</code></pre>

<h2>🪟 Exposed Ensemble UI SDK (<code>ui.*</code> Namespace)</h2>
<p>For security, plugins <b>cannot</b> access raw OS windows or arbitrary GTK pointers. Instead, plugins construct their own user interfaces exclusively through Ensemble's curated <code>ui.*</code> API, which renders clean, native Libadwaita/GTK4 cards, inputs, and buttons:</p>
<table>
  <thead>
    <tr><th>UI Function</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><code>ui.create_window(title, [width], [height])</code></td><td>Initializes a new custom UI surface/window for the plugin (clamped to safe dimensions 200×150 – 1200×900).</td></tr>
    <tr><td><code>ui.add_label(widget_id, text)</code></td><td>Adds a styled text label to the plugin's UI container.</td></tr>
    <tr><td><code>ui.add_input(widget_id, [placeholder], [default])</code></td><td>Adds a single-line text entry bound to <code>widget_id</code>.</td></tr>
    <tr><td><code>ui.add_text_area(widget_id, [default])</code></td><td>Adds a multi-line scrollable text editor box bound to <code>widget_id</code>.</td></tr>
    <tr><td><code>ui.add_button(widget_id, label, [callback_fn])</code></td><td>Adds an interactive action button. When clicked, Ensemble invokes <code>def callback_fn(widget_id):</code> inside the plugin sandbox!</td></tr>
    <tr><td><code>ui.get_text(widget_id)</code></td><td>Reads the current user-entered string value of an input or text area widget.</td></tr>
    <tr><td><code>ui.set_text(widget_id, text)</code></td><td>Dynamically updates the text of a label, input, or text area widget.</td></tr>
    <tr><td><code>ui.show_window()</code></td><td>Presents the constructed plugin UI in the Plugin UI Host / dialog window.</td></tr>
    <tr><td><code>ui.clear_window()</code> / <code>ui.close_window()</code></td><td>Clears all widgets from the plugin UI container or closes the active plugin window.</td></tr>
    <tr><td><code>ui.message_box(title, message)</code></td><td>Displays a modal alert dialog with the given title and message.</td></tr>
  </tbody>
</table>

<h2>✍️ Exposed Editor &amp; Workspace Actions (<code>editor.*</code> &amp; <code>status.*</code>)</h2>
<table>
  <thead>
    <tr><th>Function</th><th>Permission</th><th>Description</th></tr>
  </thead>
  <tbody>
    <tr><td><code>editor.get_html()</code></td><td>Editor Read</td><td>Returns the full HTML source of the currently active chapter.</td></tr>
    <tr><td><code>editor.set_html(html)</code></td><td>Editor Write</td><td>Replaces the active chapter's HTML content and refreshes the editor &amp; preview.</td></tr>
    <tr><td><code>editor.insert_html(html)</code></td><td>Editor Write</td><td>Inserts an HTML snippet at the current cursor position (or appends to chapter).</td></tr>
    <tr><td><code>editor.get_plain_text()</code></td><td>Editor Read</td><td>Returns the plain text of the active chapter with HTML tags stripped.</td></tr>
    <tr><td><code>editor.get_selection()</code></td><td>Editor Read</td><td>Returns the currently highlighted text in the editor.</td></tr>
    <tr><td><code>editor.replace_selection(text)</code></td><td>Editor Write</td><td>Replaces the current editor selection with <code>text</code>.</td></tr>
    <tr><td><code>editor.get_chapter_title()</code> / <code>set_chapter_title(t)</code></td><td>Read / Write</td><td>Gets or updates the title of the active chapter.</td></tr>
    <tr><td><code>editor.get_word_count()</code></td><td>Editor Read</td><td>Returns the word count of the active chapter.</td></tr>
    <tr><td><code>editor.get_chapter_count()</code> / <code>get_chapter_info(i)</code></td><td>Editor Read</td><td>Returns total chapter count or a dict <code>{"title", "word_count", "html"}</code> for chapter <code>i</code>.</td></tr>
    <tr><td><code>editor.get_work_skin_css()</code> / <code>set_work_skin_css(css)</code></td><td>Read / Write</td><td>Reads or updates the project's <code>workskin.css</code> stylesheet.</td></tr>
    <tr><td><code>status.set_badge(text)</code> / <code>clear_badge()</code></td><td>Status</td><td>Displays or clears a custom status indicator in the Ensemble status bar.</td></tr>
    <tr><td><code>status.notify(message)</code></td><td>Status</td><td>Displays an in-app toast notification banner.</td></tr>
  </tbody>
</table>

<h2>🔒 Storage, Inter-Plugin Messaging &amp; Hardened Security</h2>
<ul>
  <li><b>Isolated Key-Value Storage (<code>storage.*</code>):</b> Use <code>storage.set(key, val)</code>, <code>storage.get(key, default)</code>, <code>storage.remove(key)</code>, and <code>storage.list_keys()</code>. Keys are strictly alphanumeric (<code>A-Za-z0-9_-</code>) and stored in an isolated per-plugin UIDP directory capped at <b>5 MB</b>.</li>
  <li><b>Inter-Plugin IPC (<code>plugins.*</code>):</b> Call <code>plugins.send_message(target_id, topic, payload)</code> to communicate with other active plugins that define an <code>@on_message</code> handler.</li>
  <li><b>Memory &amp; CPU Watchdog:</b> Each execution is strictly bounded to an <b>8 MB heap limit</b> and <b>150,000 instructions / 250 ms</b> timeout so a buggy script can never freeze Ensemble.</li>
  <li><b>Zero-Redirect HTTPS Enforcement (<code>http.*</code>):</b> Network calls require explicit user opt-in per domain and strictly enforce <code>https://</code> with zero redirect tolerance. Any attempt to use <code>http://</code> or redirect to an unapproved host immediately quarantines the plugin.</li>
</ul>
"#.to_string(),
                hidden: false,
            },
            HelpTopic {
                id: "shortcuts".to_string(),
                icon: "⌨️".to_string(),
                title: "8. Keyboard Shortcuts, Themes & Task Manager".to_string(),
                summary: "Complete hotkey cheat sheet, Task Manager resource telemetry (Ctrl+Shift+M), and UI theme flavors.".to_string(),
                content_html: r#"
<h1>⌨️ Keyboard Shortcuts, Themes &amp; Task Manager</h1>
<p class="lead">Reference for all global hotkeys, editor formatting shortcuts, the built-in Task Manager &amp; Resource Monitor, and visual theme modes.</p>

<h2>⌨️ Global &amp; Editor Keyboard Shortcuts</h2>
<table>
  <thead>
    <tr><th>Shortcut</th><th>Action</th></tr>
  </thead>
  <tbody>
    <tr><td><kbd>Ctrl+N</kbd></td><td>Create a New AO3 Project</td></tr>
    <tr><td><kbd>Ctrl+O</kbd></td><td>Open an existing <code>.ao3proj</code> Project</td></tr>
    <tr><td><kbd>Ctrl+S</kbd></td><td>Save Project</td></tr>
    <tr><td><kbd>Ctrl+Shift+S</kbd></td><td>Save Project As…</td></tr>
    <tr><td><kbd>Ctrl+P</kbd></td><td>Toggle Live AO3 Sideview Preview</td></tr>
    <tr><td><kbd>Ctrl+Shift+M</kbd></td><td>Open Task Manager &amp; Resource Monitor</td></tr>
    <tr><td><kbd>Ctrl+Shift+C</kbd></td><td>Copy Sanitized AO3 Chapter HTML to Clipboard</td></tr>
    <tr><td><kbd>Ctrl+,</kbd></td><td>Open Preferences</td></tr>
    <tr><td><kbd>F1</kbd></td><td>Open Ensemble Help Guide</td></tr>
    <tr><td><kbd>Ctrl+B</kbd> / <kbd>Ctrl+I</kbd> / <kbd>Ctrl+U</kbd></td><td>Bold / Italic / Underline in Rich Text Editor</td></tr>
    <tr><td><kbd>Ctrl+Z</kbd> / <kbd>Ctrl+Y</kbd></td><td>Undo / Redo</td></tr>
  </tbody>
</table>

<h2>📊 Task Manager &amp; Resource Monitor (<kbd>Ctrl+Shift+M</kbd>)</h2>
<p>Open the Task Manager from the header bar monitor icon or by pressing <kbd>Ctrl+Shift+M</kbd> to inspect real-time resource usage:</p>
<ul>
  <li><b>Core Process Telemetry:</b> Live Process RSS (Resident Memory), Virtual Memory, CPU utilization, thread count, and open file descriptors.</li>
  <li><b>Ensemble Component Breakdown:</b> Shows how many chapters are currently loaded in RAM vs. offloaded to disk, plus buffer sizes for the Rich Text WebView, Raw HTML Buffer, Work Skin CSS, and Live Preview.</li>
  <li><b>Plugin Sandbox Telemetry:</b> Lists every installed <code>.er</code> plugin along with its peak heap allocation (out of the 8 MB limit) and last execution duration in milliseconds.</li>
  <li><b>1-Click Memory Trim:</b> Click <b>Offload Inactive Chapters Now</b> to immediately flush all background chapters from RAM.</li>
</ul>

<h2>🎨 Themes &amp; Design Systems</h2>
<p>Switch effortlessly between <b>Follow Desktop Theme</b>, <b>Light Mode</b>, and <b>Dark Mode</b> from the header bar moon/sun menu, or customize appearance in <b>Preferences</b> (including <i>Darker OLED #000000</i>, <i>AO3 Reversi #111111 + #990000 crimson</i>, <i>Standard Ensemble</i>, <i>KDE UI</i>, <i>Standard Qt</i>, and <i>Android Material You</i> flavors).</p>
"#.to_string(),
                hidden: false,
            },
        ]
    }

    pub fn topics(&self) -> &[HelpTopic] {
        &self.topics
    }

    pub fn topic_count(&self) -> usize {
        self.topics.len()
    }

    pub fn visible_topic_count(&self) -> usize {
        self.topics.iter().filter(|t| !t.hidden).count()
    }

    pub fn select_topic(&mut self, topic_id: &str) {
        // Support both consolidated topic IDs and legacy topic aliases seamlessly
        let resolved_id = match topic_id {
            "archiving" | "exporting" => "getting_started",
            "html_sanitizer" => "editor_formatting",
            "project_properties" | "image_embeds" => "chapter_manager",
            "live_preview" | "diagnostics" => "work_skins",
            "git_history" => "lore_graph",
            "agentic_data" => "mcp_setup",
            "themes_design" => "shortcuts",
            other => other,
        };
        if let Some(idx) = self.topics.iter().position(|t| t.id == resolved_id) {
            self.selected_index = idx;
            self.render_current_topic();
        }
    }

    pub fn set_search_filter(&mut self, text: &str) {
        self.search_query = text.trim().to_ascii_lowercase();
        let mut first_visible: Option<usize> = None;
        for (idx, topic) in self.topics.iter_mut().enumerate() {
            if self.search_query.is_empty() {
                topic.hidden = false;
            } else {
                let matched = topic.title.to_ascii_lowercase().contains(&self.search_query)
                    || topic.summary.to_ascii_lowercase().contains(&self.search_query)
                    || topic
                        .content_html
                        .to_ascii_lowercase()
                        .contains(&self.search_query);
                topic.hidden = !matched;
            }
            if !topic.hidden && first_visible.is_none() {
                first_visible = Some(idx);
            }
        }
        if let Some(first_idx) = first_visible {
            if self.topics.get(self.selected_index).map_or(true, |t| t.hidden) {
                self.selected_index = first_idx;
            }
        }
        self.render_current_topic();
    }

    pub fn search_query(&self) -> &str {
        &self.search_query
    }

    pub fn clear_search_filter(&mut self) {
        self.set_search_filter("");
    }

    pub fn to_html(&self) -> &str {
        &self.rendered_html
    }

    pub fn selected_topic(&self) -> &HelpTopic {
        &self.topics[self.selected_index]
    }

    pub fn update_theme(&mut self) {
        self.render_current_topic();
    }

    fn render_current_topic(&mut self) {
        let tm = ThemeManager::instance();
        let text_color = tm.primary_text_color().to_hex();
        let bg_color = tm.card_bg_color().to_hex();
        let content = &self.topics[self.selected_index].content_html;
        self.rendered_html = format!(
            "<html><head><style>body {{ color: {}; background-color: {}; font-family: sans-serif; }}</style></head><body>{}</body></html>",
            text_color, bg_color, content
        );
    }
}
