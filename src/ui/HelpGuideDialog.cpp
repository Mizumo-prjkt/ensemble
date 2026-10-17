#include "HelpGuideDialog.h"
#include "AppIcon.h"
#include "themes/ThemeManager.h"
#include "mcp/McpServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

class HelpTopicDelegate : public QStyledItemDelegate
{
public:
    explicit HelpTopicDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // Let stylesheet handle the rounded card selection and hover backgrounds
        opt.text.clear();
        if (opt.widget) {
            opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        } else if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, ThemeManager::instance().accentColor());
        }

        const QString icon = index.data(Qt::UserRole + 1).toString();
        const QString title = index.data(Qt::UserRole + 2).toString();
        const QString summary = index.data(Qt::UserRole + 3).toString();

        const bool isSelected = (opt.state & QStyle::State_Selected);

        QColor titleColor;
        QColor summaryColor;
        if (isSelected) {
            titleColor = QColor(QStringLiteral("#ffffff"));
            summaryColor = ThemeManager::instance().isDarkMode()
                               ? QColor(QStringLiteral("#cbd5e1"))
                               : QColor(QStringLiteral("#f1f5f9"));
        } else {
            titleColor = ThemeManager::instance().primaryTextColor();
            summaryColor = ThemeManager::instance().mutedTextColor();
        }

        const QRect innerRect = opt.rect.adjusted(10, 7, -10, -7);
        const int availWidth = innerRect.width();

        // 1. Title line: icon + title (bold, elided at edge)
        QFont titleFont = opt.font;
        titleFont.setBold(true);
        titleFont.setPointSize(opt.font.pointSize() > 0 ? opt.font.pointSize() : 10);
        QFontMetrics fmTitle(titleFont);

        const QString fullTitle = QStringLiteral("%1  %2").arg(icon, title);
        const QString elidedTitle = fmTitle.elidedText(fullTitle, Qt::ElideRight, availWidth);

        // 2. Summary line: slightly smaller, indented under title text, elided at edge
        QFont summaryFont = opt.font;
        summaryFont.setBold(false);
        summaryFont.setPointSize(qMax(8, (opt.font.pointSize() > 0 ? opt.font.pointSize() - 1 : 9)));
        QFontMetrics fmSummary(summaryFont);

        const int emojiIndent = fmTitle.horizontalAdvance(QStringLiteral("%1  ").arg(icon));
        const int summaryAvailW = qMax(40, availWidth - emojiIndent);
        const QString elidedSummary = fmSummary.elidedText(summary, Qt::ElideRight, summaryAvailW);

        // Draw title
        painter->setFont(titleFont);
        painter->setPen(titleColor);
        painter->drawText(QRect(innerRect.left(), innerRect.top(), availWidth, fmTitle.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

        // Draw summary
        painter->setFont(summaryFont);
        painter->setPen(summaryColor);
        painter->drawText(QRect(innerRect.left() + emojiIndent,
                                innerRect.top() + fmTitle.height() + 3,
                                summaryAvailW, fmSummary.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedSummary);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        QFont titleFont = option.font;
        titleFont.setBold(true);
        QFontMetrics fmTitle(titleFont);

        QFont summaryFont = option.font;
        summaryFont.setPointSize(qMax(8, option.font.pointSize() - 1));
        QFontMetrics fmSummary(summaryFont);

        const int h = fmTitle.height() + fmSummary.height() + 19;
        return QSize(option.rect.width(), qMax(48, h));
    }
};

} // namespace

HelpGuideDialog::HelpGuideDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Help & User Guide — Ensemble"));
    setWindowIcon(AppIcon::icon());
    setModal(false);
    resize(980, 680);
    setMinimumSize(820, 560);

    populateTopics();
    setupUi();
    updateTheme();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](AppTheme) { updateTheme(); });
    connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this,
            [this](SkinFlavor) { updateTheme(); });

    if (!m_topics.isEmpty()) {
        m_topicList->setCurrentRow(0);
    }
}

void HelpGuideDialog::populateTopics()
{
    m_topics.clear();

    // 1. Getting Started
    {
        HelpTopic t;
        t.id = QStringLiteral("getting_started");
        t.icon = QStringLiteral("🚀");
        t.title = QStringLiteral("Getting Started with Ensemble");
        t.summary = QStringLiteral("Overview, project management, and writing your first work.");
        t.contentHtml = QStringLiteral(
            "<h1>🚀 Getting Started with Ensemble</h1>"
            "<p class='lead'>Ensemble is a next-generation fanfiction typewriter designed specifically for authors on <b>Archive of Our Own (AO3)</b>. It combines a clean, distraction-free WYSIWYG editor with live AO3 HTML preview, native Work Skin CSS authorship, visual Chapter Manager, and unified single-file project archiving.</p>"
            "<hr>"
            "<h3>Key Workflows</h3>"
            "<ul>"
            "  <li><b>Starting a New Work:</b> From the Main Menu or <kbd>Ctrl+N</kbd>, select <i>New Project</i>. A fresh work is created with an initial Chapter 1 and standard AO3 HTML template.</li>"
            "  <li><b>Opening Existing Works:</b> Open any <code>.ao3proj</code> file directly with <kbd>Ctrl+O</kbd> or through the file open dialog.</li>"
            "  <li><b>Saving Your Work:</b> Press <kbd>Ctrl+S</kbd> to save. If saving for the first time, you will be prompted to pick a location for your <code>.ao3proj</code> archive.</li>"
            "  <li><b>Chapter Organization:</b> Manage multi-chapter works, inspect syntax tag usage, and reorder chapters from the dedicated <i>📑 Chapter Manager</i> (<kbd>Ctrl+Shift+O</kbd>).</li>"
            "  <li><b>In-App GitHub Updates:</b> Check for new releases directly from <i>Help → Check for Updates…</i> or the <i>About Ensemble</i> dialog, with 1-click redirection to the newest release tag on GitHub.</li>"
            "  <li><b>Closing & Reverting:</b> Use <i>File → Close Project</i> (<kbd>Ctrl+W</kbd>) or <i>Main Menu</i> to safely return to the welcome screen without keeping lingering unsaved edits.</li>"
            "</ul>"
            "<div class='callout-note'>"
            "  <b>💡 Automatic Dirty Tracking:</b> Ensemble monitors changes in rich text, raw HTML, chapter order, and Work Skin CSS. It will only prompt you to save if your active project actually contains unsaved modifications."
            "</div>"
        );
        m_topics.append(t);
    }

    // 2. Editor & Text Formatting
    {
        HelpTopic t;
        t.id = QStringLiteral("editor_formatting");
        t.icon = QStringLiteral("✍️");
        t.title = QStringLiteral("WYSIWYG Editor & Formatting");
        t.summary = QStringLiteral("Rich text tools, headings, blockquotes, and AO3 paragraphs.");
        t.contentHtml = QStringLiteral(
            "<h1>✍️ WYSIWYG Editor & Text Formatting</h1>"
            "<p class='lead'>The primary editor tab lets you compose naturally with familiar rich-text shortcuts while internally producing standard, clean HTML that mirrors AO3's posting standards.</p>"
            "<hr>"
            "<h3>Vector Toolbar & Active States</h3>"
            "<p>Ensemble features clean, consistent vector SVG toolbar buttons (Bold, Italic, Underline, Strikethrough, Heading 1, Link, Horizontal Rule) that dynamically highlight their active state whenever your text cursor enters styled content.</p>"
            "<h3>Exact Word Count Breakdown</h3>"
            "<p>The bottom-left status bar displays exact word counts without approximations: <code>Words: 2,450 (Total: 14,200)</code> showing both current chapter words and total work words. Highlighting any text instantly displays the selection count as well (e.g. <code>[Selected: 120]</code>).</p>"
            "<h3>Standard Formatting Shortcuts</h3>"
            "<table border='0' cellpadding='7' cellspacing='0' width='100%'>"
            "  <tr><th>Shortcut</th><th>Action</th><th>AO3 HTML Output</th></tr>"
            "  <tr><td><kbd>Ctrl+B</kbd></td><td>Bold</td><td><code>&lt;strong&gt;text&lt;/strong&gt;</code></td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+I</kbd></td><td>Italic</td><td><code>&lt;em&gt;text&lt;/em&gt;</code></td></tr>"
            "  <tr><td><kbd>Ctrl+U</kbd></td><td>Underline</td><td><code>&lt;u&gt;text&lt;/u&gt;</code></td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Shift+X</kbd></td><td>Strikethrough</td><td><code>&lt;s&gt;text&lt;/s&gt;</code></td></tr>"
            "  <tr><td><kbd>Ctrl+F</kbd></td><td>Find in Chapter</td><td>Opens Find/Replace dialog</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+R</kbd></td><td>Replace in Chapter</td><td>Opens Replace bar</td></tr>"
            "</table>"
            "<h3>Paragraph Structure & Dividers</h3>"
            "<p>Ensemble avoids double-spacing pitfalls by structuring lines into standard <code>&lt;p&gt;</code> tags. Scene breaks can be created using horizontal dividers (<code>&lt;hr/&gt;</code>) or styled separator asterisks.</p>"
            "<div class='callout-warning'>"
            "  <b>⚠️ Crucial Formatting Rule — Avoid <code>&lt;span&gt;</code> Tags:</b><br>"
            "  Even though Archive of Our Own technically permits <code>&lt;span&gt;</code> tags, <b>do not use <code>&lt;span&gt;</code> tags in chapter prose</b>. In rich-text and WYSIWYG authoring, <code>&lt;span&gt;</code> elements break cursor navigation, cause fragmented line styling, and disrupt word wrapping. Instead, use semantic formatting tags (<code>&lt;em&gt;</code>, <code>&lt;strong&gt;</code>) or apply custom Work Skin classes directly to paragraph blocks (<code>&lt;p class=\"...\"&gt;</code>) or division containers (<code>&lt;div class=\"...\"&gt;</code>). Ensemble's editor and MCP server actively disallow <code>&lt;span&gt;</code> to protect your story's typography."
            "</div>"
            "<div class='callout-tip'>"
            "  <b>✨ Tip:</b> Right-click any text inside the editor to quickly apply custom Work Skin CSS classes directly to paragraphs or blockquotes!"
            "</div>"
        );
        m_topics.append(t);
    }

    // 3. Visual Chapter Manager & Metrics
    {
        HelpTopic t;
        t.id = QStringLiteral("chapter_manager");
        t.icon = QStringLiteral("📑");
        t.title = QStringLiteral("Visual Chapter Manager & Metrics");
        t.summary = QStringLiteral("Multi-chapter organization, 1-based ordering, drag-and-drop sorting, and syntax tag diagnostics.");
        t.contentHtml = QStringLiteral(
            "<h1>📑 Visual Chapter Manager & Metrics</h1>"
            "<p class='lead'>Ensemble includes a dedicated Chapter Manager window for organizing multi-chapter works, inspecting syntax usage, and reordering chapters without cluttering the primary writing workspace.</p>"
            "<hr>"
            "<h3>Opening the Chapter Manager</h3>"
            "<p>Click the <b>📑 Chapter Manager</b> button located below the chapter list in the left sidebar, or choose <i>Chapter → Chapter Manager…</i> (<kbd>Ctrl+Shift+O</kbd>).</p>"
            "<h3>Comprehensive Chapter Statistics</h3>"
            "<p>The Chapter Manager displays a complete tabular breakdown of every chapter in your work:</p>"
            "<ul>"
            "  <li><b>Chapter Number &amp; Title:</b> View and edit chapter titles in place.</li>"
            "  <li><b>Word Count:</b> Exact word counts calculated for each individual chapter.</li>"
            "  <li><b>Line Count:</b> Paragraph and line metrics.</li>"
            "  <li><b>Syntax Tag Diagnostics:</b> Detailed breakdown of HTML formatting tags used in each chapter (e.g. <code>&lt;b&gt;</code>, <code>&lt;i&gt;</code>, <code>&lt;u&gt;</code>, <code>&lt;s&gt;</code>, <code>&lt;div&gt;</code>, <code>&lt;blockquote&gt;</code>, <code>&lt;hr&gt;</code>, <code>&lt;a&gt;</code>).</li>"
            "</ul>"
            "<h3>Strict 1-Based Chapter Numbering</h3>"
            "<p>In accordance with AO3 standards, Ensemble enforces <b>strict 1-based chapter numbering</b>. Chapter 1 is always Chapter 1; requests or attempts to use Chapter 0 are blocked across both the UI and AI MCP server to prevent index corruption.</p>"
            "<h3>Sorting &amp; Reordering</h3>"
            "<ul>"
            "  <li><b>Drag-and-Drop:</b> Drag any row in the chapter table to reorder chapters interactively.</li>"
            "  <li><b>Target Index Input:</b> Type a destination chapter number to move a chapter instantly to a specific slot.</li>"
            "  <li><b>Chapter Management Toolbar:</b> Add new chapters or delete unwanted chapters cleanly from the manager interface.</li>"
            "</ul>"
        );
        m_topics.append(t);
    }

    // 3. Image Embed Attachments (URL-Based)
    {
        HelpTopic t;
        t.id = QStringLiteral("image_embeds");
        t.icon = QStringLiteral("🖼️");
        t.title = QStringLiteral("Image Embed Attachments (URL-Based)");
        t.summary = QStringLiteral("Embed remote illustrations via URL, preview thumbnails, alt text, and AO3 snippets.");
        t.contentHtml = QStringLiteral(
            "<h1>🖼️ Image Embed Attachments (URL-Based)</h1>"
            "<p class='lead'>Incorporate fanart, cover illustrations, letters, maps, and aesthetic dividers into your AO3 stories using external URL-based image embeds.</p>"
            "<hr>"
            "<h3>Why External URL Embeds?</h3>"
            "<p>Archive of Our Own does not host user-uploaded image files on its servers. Instead, AO3 expects authors to host images on third-party image hosts (e.g. Imgur, Postimages, personal web hosts, or Discord CDN) and embed them using standard HTML <code>&lt;img src=\"...\"&gt;</code> tags.</p>"
            "<p>Ensemble provides a streamlined image embed workflow so you never have to re-upload files or hand-write image markup.</p>"
            "<h3>Inserting an Image Embed</h3>"
            "<ul>"
            "  <li>Click the <b>🖼️ Image</b> button on the editor toolbar, select <i>Insert → Image Embed…</i>, or press <kbd>Ctrl+Shift+M</kbd>.</li>"
            "  <li>Paste any direct image URL (PNG, JPEG, GIF, WebP, SVG). Ensemble's background engine immediately downloads and displays a live thumbnail preview.</li>"
            "  <li><b>Alt Text:</b> Provide a descriptive text description. AO3 strongly encourages alt text so visually impaired readers and screen reader users can enjoy your story.</li>"
            "  <li><b>Hover Title:</b> Optional tooltip text displayed when a desktop reader hovers their cursor over the image.</li>"
            "  <li><b>Dimensions & Aspect Ratio Lock:</b> Specify custom width and height in pixels. Enabling <b>🔒 Lock Aspect Ratio</b> automatically recalculates height when you adjust width (and vice versa) based on the image's original dimensions.</li>"
            "  <li><b>Alignment:</b> Choose <i>Center</i> (wrapped in <code>&lt;p align=\"center\"&gt;</code>), <i>Left</i>, or <i>Right</i>.</li>"
            "  <li><b>Wrap in Hyperlink:</b> Check this to wrap your image in an <code>&lt;a href=\"...\"&gt;</code> link, allowing readers to click the illustration to view the full-resolution artwork or visit the artist's profile.</li>"
            "</ul>"
            "<h3>1-Click 'Copy AO3 HTML Snippet'</h3>"
            "<p>Need to paste the image tag directly into AO3 without inserting it into the chapter right now? Click the <b>📋 Copy AO3 HTML Snippet</b> button inside the dialog to generate clean, sanitized HTML directly to your clipboard:</p>"
            "<pre class='code-block'>&lt;p align=\"center\"&gt;\n"
            "  &lt;a href=\"https://example.com/art_hires.jpg\" target=\"_blank\"&gt;\n"
            "    &lt;img src=\"https://example.com/art_display.jpg\" alt=\"Fanart of Hero and Rival under cherry blossoms\" width=\"640\" height=\"400\" /&gt;\n"
            "  &lt;/a&gt;\n"
            "&lt;/p&gt;</pre>"
            "<h3>In-Editor Remote Image Rendering</h3>"
            "<p>When an image embed is inserted, Ensemble's editor pane requests the image asynchronously in the background and renders it directly inside your rich-text writing view. No placeholder icons or broken boxes!</p>"
            "<h3>AO3 Crawler & Story Import Support</h3>"
            "<p>When importing an existing story from AO3 using <kbd>Ctrl+Shift+I</kbd>, Ensemble's HTML parser automatically detects all embedded images, extracts their URLs, alt text, and dimensions, and logs them in your project.</p>"
            "<h3>Managing Images in Project Properties</h3>"
            "<p>Open <i>File → Properties</i> (<kbd>Alt+Enter</kbd>) and navigate to the <b>🖼️ Image Embeds</b> tab. You will see a complete inventory of every external image embedded across all chapters in your story, with thumbnail previews, chapter locations, and copy snippet buttons.</p>"
            "<div class='callout-tip'>"
            "  <b>✨ Tip for AI Agents:</b> AI models can inspect all images via the MCP tool <code>list_image_embeds</code> and insert new illustrations via <code>insert_image_embed</code>!"
            "</div>"
        );
        m_topics.append(t);
    }

    // 4. Work Skins & Custom CSS
    {
        HelpTopic t;
        t.id = QStringLiteral("work_skins");
        t.icon = QStringLiteral("🎨");
        t.title = QStringLiteral("AO3 Work Skins & Custom CSS");
        t.summary = QStringLiteral("Writing custom CSS for chat bubbles, letters, and special formatting.");
        t.contentHtml = QStringLiteral(
            "<h1>🎨 AO3 Work Skins & Custom CSS</h1>"
            "<p class='lead'>Work Skins are custom CSS stylesheets defined on AO3 to enhance fanfiction formatting — such as rendering realistic SMS text conversations, letters, newspaper clippings, or game UI interfaces.</p>"
            "<hr>"
            "<h3>How Work Skins Work in Ensemble</h3>"
            "<p>1. Switch to the <b>Work Skin CSS</b> editor tab.<br>"
            "2. Define classes like <code>.text-msg</code>, <code>.sender</code>, or <code>.epistolary</code>.<br>"
            "3. Ensemble's live CSS engine immediately parses your class selectors, highlights syntax, and exposes them in the editor's right-click context menu.<br>"
            "4. The <b>Live WebEngine Preview</b> instantly renders your custom styles in real-time!</p>"
            "<h3>Example: Chat Message Work Skin</h3>"
            "<pre class='code-block'>/* Text Message Chat Bubbles */\n"
            ".chat-box {\n"
            "  max-width: 360px;\n"
            "  margin: 1.5em auto;\n"
            "  padding: 10px;\n"
            "  background: #f1f5f9;\n"
            "  border-radius: 12px;\n"
            "}\n"
            ".msg-sent {\n"
            "  background: #3b82f6;\n"
            "  color: #ffffff;\n"
            "  border-radius: 16px 16px 2px 16px;\n"
            "  padding: 8px 12px;\n"
            "  margin-left: auto;\n"
            "  width: fit-content;\n"
            "}</pre>"
            "<div class='callout-note'>"
            "  <b>🔒 AO3 CSS Restrictions:</b> Remember that AO3 strips <code>position: fixed</code>, external fonts via <code>@import</code>, and JavaScript. Stick to flexbox, margins, borders, and colors for cross-platform compatibility."
            "</div>"
        );
        m_topics.append(t);
    }

    // 5. Live WebEngine Preview
    {
        HelpTopic t;
        t.id = QStringLiteral("live_preview");
        t.icon = QStringLiteral("🌐");
        t.title = QStringLiteral("Live WebEngine Preview");
        t.summary = QStringLiteral("Chromium preview, pop-out windows, and true AO3 skin rendering.");
        t.contentHtml = QStringLiteral(
            "<h1>🌐 Live WebEngine Preview</h1>"
            "<p class='lead'>Ensemble includes an integrated Chromium-powered WebEngine preview pane that simulates the exact visual environment of an Archive of Our Own story page.</p>"
            "<hr>"
            "<h3>Features</h3>"
            "<ul>"
            "  <li><b>Split Live Preview (<kbd>Ctrl+P</kbd>):</b> Toggles the inline preview pane on the right side of the main workspace.</li>"
            "  <li><b>Pop-Out Preview Window (<kbd>Ctrl+Shift+P</kbd>):</b> Opens an independent floating preview window, ideal for dual-monitor writing setups.</li>"
            "  <li><b>Zero-Flicker Live DOM Updates:</b> Edits in rich text or raw HTML are synced via lightweight DOM mutations, eliminating full-page reload flashes.</li>"
            "  <li><b>Hardware GPU Acceleration:</b> WebEngine runs with full OpenGL/Vulkan GPU acceleration enabled for smooth scrolling and responsive rendering.</li>"
            "</ul>"
            "<h3>AO3 Default (Light) vs Reversi (Dark) Testing</h3>"
            "<p>A massive pain point for AO3 creators is ensuring custom Work Skins look legible and visually balanced on both light and dark backgrounds. The Live Preview header includes a dedicated <b>Skin Toggle</b> allowing you to test how your work skin and chapter formatting look against:</p>"
            "<ul>"
            "  <li><b>AO3 Default:</b> The official standard light background (<code>#ffffff</code> / cream).</li>"
            "  <li><b>AO3 Reversi:</b> The official AO3 dark reading skin (<code>#111111</code> background with crimson accents), allowing you to verify text contrast, chat bubble borders, and link visibility before publishing.</li>"
            "</ul>"
        );
        m_topics.append(t);
    }

    // 6. Project Properties & AO3 Metadata
    {
        HelpTopic t;
        t.id = QStringLiteral("project_properties");
        t.icon = QStringLiteral("⚙️");
        t.title = QStringLiteral("Project Properties & AO3 Metadata");
        t.summary = QStringLiteral("Manage story tags, ratings, archive warnings, relationships, fandoms, and inspect image embeds & history.");
        t.contentHtml = QStringLiteral(
            "<h1>⚙️ Project Properties & AO3 Metadata</h1>"
            "<p class='lead'>Configure your story's metadata, AO3 classification tags, ratings, content warnings, and author notes in one unified hub.</p>"
            "<hr>"
            "<h3>Accessing Project Properties</h3>"
            "<p>Select <i>File → Properties…</i> from the top menu bar, or press <kbd>Alt+Enter</kbd> (or <kbd>Alt+Return</kbd>). Note: This option is safely disabled until a project is open.</p>"
            "<h3>Comprehensive Metadata Tabs</h3>"
            "<table border='0' cellpadding='7' cellspacing='0' width='100%'>"
            "  <tr><th>Tab</th><th>Description &amp; Controlled Fields</th></tr>"
            "  <tr><td><b>General</b></td><td>Story Title, Author / Pseud, Series Name, Total Word Count, and Chapter Count.</td></tr>"
            "  <tr class='row-alt'><td><b>Rating &amp; Warnings</b></td><td>AO3 Age Ratings (General Audiences, Teen And Up, Mature, Explicit, Not Rated) and the 6 official Archive Warnings (e.g. <i>Graphic Depictions Of Violence</i>, <i>Major Character Death</i>, <i>Choose Not To Use Archive Warnings</i>).</td></tr>"
            "  <tr><td><b>Categories &amp; Cast</b></td><td>Relationship Categories (F/M, M/M, F/F, Multi, Gen, Other), Relationship tags (e.g. <code>Harry Potter/Draco Malfoy</code> or <code>Dean Winchester &amp; Sam Winchester</code>), and Character cast tags.</td></tr>"
            "  <tr class='row-alt'><td><b>Fandoms &amp; Tags</b></td><td>Primary and crossover Fandom tags, plus Freeform / Additional Tags (e.g. <i>Coffee Shop AU</i>, <i>Slow Burn</i>, <i>Mutual Pining</i>, <i>Angst with a Happy Ending</i>).</td></tr>"
            "  <tr><td><b>Summary &amp; Notes</b></td><td>Work Summary (the blurb shown in AO3 search listings), Starting Author's Notes (preface), and End Notes (postscript acknowledgments).</td></tr>"
            "  <tr class='row-alt'><td><b>🖼️ Image Embeds</b></td><td>Interactive table listing all external image URLs embedded in the story, thumbnail previews, dimensions, and 1-click HTML snippet copying.</td></tr>"
            "  <tr><td><b>📜 Revision History</b></td><td>Git-like commit ledger tracking 12-char SHA-1 commit hashes, author attributions, ISO timestamps, and commit messages recorded in <code>history.cmt</code>.</td></tr>"
            "</table>"
            "<h3>Seamless Storage in <code>metadata.xml</code></h3>"
            "<p>When you click <b>Save Properties</b>, Ensemble serializes your metadata directly into <code>metadata.xml</code> inside the <code>.ao3proj</code> container. No external database or sidecar files required!</p>"
            "<h3>Automatic Crawler Extraction</h3>"
            "<p>When you import an existing fanfiction work using <i>File → Import from AO3…</i> (<kbd>Ctrl+Shift+I</kbd>), Ensemble's crawler automatically extracts all ratings, archive warnings, relationship tags, characters, fandoms, additional tags, and author summary from the live AO3 page and populates them into your Project Properties.</p>"
            "<div class='callout-note'>"
            "  <b>🤖 AI Agent Control:</b> Connected MCP AI agents can view and modify metadata at runtime using <code>get_project_summary</code> and <code>update_project_metadata</code>."
            "</div>"
        );
        m_topics.append(t);
    }

    // 7. Lore Graph & Story Mapping (.grph)
    {
        HelpTopic t;
        t.id = QStringLiteral("lore_graph");
        t.icon = QStringLiteral("🗺️");
        t.title = QStringLiteral("Lore Graph & Story Mapping (.grph)");
        t.summary = QStringLiteral("Visual worldbuilding canvas, character relationship diagrams, plot conflicts, and the independent .grph DSL language.");
        t.contentHtml = QStringLiteral(
            "<h1>🗺️ Lore Graph & Story Mapping (.grph)</h1>"
            "<p class='lead'>Ensemble includes a custom narrative graphing engine designed specifically for authors to map out complex worldbuilding, faction allegiances, character webs, and plot conflicts without relying on generic external charting tools.</p>"
            "<hr>"
            "<h3>Opening the Lore Graph</h3>"
            "<p>Select <i>View → 🗺️ Lore Graph &amp; Narrative Map…</i> or press <kbd>Ctrl+Shift+L</kbd>. (The menu item is disabled when no project is open to ensure narrative data is always bound to your active <code>.ao3proj</code> archive).</p>"
            "<h3>The Independent <code>.grph</code> DSL Language</h3>"
            "<p>Ensemble avoids third-party diagram limitations (like Mermaid) by using a specialized, human-readable narrative Domain-Specific Language (DSL) formatted as <code>.grph</code>.</p>"
            "<h3>Binary Magic Bytes Header</h3>"
            "<p>Every valid <code>.grph</code> file begins with a strict 8-byte binary signature that guarantees data integrity and distinguishes it from raw text:</p>"
            "<pre class='code-block'>\\x89GRPH\\x01\\r\\n  (Hex: 0x89 0x47 0x52 0x50 0x48 0x01 0x0D 0x0A)</pre>"
            "<h3>DSL Grammar &amp; Code Example</h3>"
            "<pre class='code-block'>graph \"Fantasy Kingdom Lore\" [direction=LR]\n\n"
            "# 1. Story Entity Nodes\n"
            "node hero [label=\"Eldrin\", type=\"character\", pos(120, 180), shape=\"rounded\", color=\"#3b82f6\"]\n"
            "node villain [label=\"Malakar\", type=\"character\", pos(580, 180), shape=\"rectangle\", color=\"#ef4444\"]\n"
            "node relic [label=\"Amulet of Dawn\", type=\"item\", pos(350, 80), shape=\"diamond\", color=\"#f59e0b\"]\n\n"
            "# 2. Connectors &amp; Relationships\n"
            "hero -&gt; relic [label=\"seeks\", style=\"solid\", color=\"#3b82f6\"]\n"
            "hero &lt;-&gt; villain [label=\"rivals\", style=\"dashed\", color=\"#8b5cf6\"]\n"
            "villain -x hero [label=\"sworn hatred\", relation=\"conflict\", color=\"#ef4444\"]\n\n"
            "# 3. Grouping Containers\n"
            "box capital [label=\"The High Citadel\", pos(80, 120), size(400, 260), color=\"#64748b\"] {\n"
            "    hero\n"
            "    relic\n"
            "}\n\n"
            "# 4. Follow-up Narrative Hook\n"
            "followup relic [targetChapter=3, status=\"pending\", note=\"Reveal the relic's curse during the duel.\"]</pre>"
            "<h3>Connector Syntax Types</h3>"
            "<ul>"
            "  <li><code>a -&gt; b</code>: Directed relationship or causal influence.</li>"
            "  <li><code>a &lt;-&gt; b</code>: Mutual / reciprocal relationship (e.g. mutual pining, lovers, allies).</li>"
            "  <li><code>a -- b</code>: Undirected connection or neutral association.</li>"
            "  <li><code>a -x b</code>: Plot conflict, enmity, or contradictory lore fact.</li>"
            "</ul>"
            "<h3>Story Lore Architecture in <code>.ao3proj</code></h3>"
            "<p>All narrative data lives within the <code>lore/</code> directory inside your project archive:</p>"
            "<pre class='code-block'>lore/\n"
            "├── graphs/\n"
            "│   └── data.grph        (Primary canvas layout with \\x89GRPH\\x01\\r\\n signature)\n"
            "├── lore_data.json       (Codex of characters, factions, artifacts, and concepts)\n"
            "├── history.json         (Chronological backstory timeline and world events)\n"
            "├── conflicts.json       (Detected continuity errors and plot discrepancies)\n"
            "├── resolved.json        (Resolved plot points and canonical author rulings)\n"
            "├── plans.json           (Story follow-up hooks and future chapter arcs)\n"
            "├── references.json      (Canon sources, research notes, and citations)\n"
            "└── metadata.xml         (Lore manifest and chapter integration tags)</pre>"
            "<h3>Interactive Canvas Features</h3>"
            "<ul>"
            "  <li><b>Drag-and-Drop:</b> Move nodes anywhere on the 2D infinite canvas. Coordinates update automatically.</li>"
            "  <li><b>Auto-Layout:</b> Automatically rearranges cluttered networks into clean hierarchical trees.</li>"
            "  <li><b>Conflict Detection:</b> Flags conflicting plot threads (<code>-x</code>) in red for easy revision.</li>"
            "  <li><b>Export Image:</b> Export the visual graph as high-resolution PNG for reference.</li>"
            "</ul>"
            "<div class='callout-tip'>"
            "  <b>🤖 MCP AI Tool Support:</b> AI agents can read and modify your story graph using <code>get_lore_graph</code>, <code>update_lore_graph</code>, <code>add_lore_node</code>, <code>connect_lore_nodes</code>, <code>move_lore_node</code>, <code>get_lore_data</code>, and <code>manage_lore_data</code>."
            "</div>"
        );
        m_topics.append(t);
    }

    // 8. AI Agent MCP Setup & Co-Writing
    {
        HelpTopic t;
        t.id = QStringLiteral("mcp_setup");
        t.icon = QStringLiteral("🤖");
        t.title = QStringLiteral("AI Agent MCP Setup & Co-Writing");
        t.summary = QStringLiteral("Connect Claude Desktop, Claude Code CLI, Cursor, and Gemini Antigravity via embedded MCP server.");
        const QString discoveryPath = QDir::toNativeSeparators(McpServer::discoveryFilePath());
        QString appExe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        if (appExe.isEmpty()) {
#if defined(Q_OS_WIN)
            appExe = QStringLiteral("C:\\Program Files\\Ensemble\\ensemble.exe");
#else
            appExe = QStringLiteral("/usr/local/bin/ensemble");
#endif
        }
        const QString escapedExeJson = QString(appExe).replace(QLatin1Char('\\'), QStringLiteral("\\\\"));

        t.contentHtml = QStringLiteral(
            "<h1>🤖 AI Agent MCP Setup & Co-Writing</h1>"
            "<p class='lead'>Ensemble includes a built-in <b>Model Context Protocol (MCP)</b> server that allows autonomous AI models (Claude, Cursor, Gemini Antigravity, etc.) to act as interactive fanfiction co-authors, dialogue editors, and lore consultants in real time.</p>"
            "<hr>"
            "<h3>What is Model Context Protocol?</h3>"
            "<p>Model Context Protocol (MCP) is an open specification that connects AI models to external tools, editors, and local environments. With Ensemble's MCP integration, an AI agent can:</p>"
            "<ul>"
            "  <li>Inspect your project structure, word counts, and chapter outlines.</li>"
            "  <li>Draft or polish chapter prose while preserving strict AO3 HTML formatting.</li>"
            "  <li>Craft and test custom AO3 Work Skin CSS stylesheets with instant live preview.</li>"
            "  <li>Query and update character relationships and worldbuilding in the Lore Graph.</li>"
            "  <li>Commit revisions to your Git-like story history ledger.</li>"
            "  <li>Broadcast reasoning and suggestions live to Ensemble's Agent Thoughts Monitor.</li>"
            "</ul>"
            "<h3>Two Connection Modes</h3>"
            "<ol>"
            "  <li><b>Stdio Bridge Mode (Recommended):</b> The agent launches Ensemble's bridge as a standard JSON-RPC subprocess via <code>--mcp-bridge</code>. The bridge communicates with the running Ensemble desktop instance via local IPC.</li>"
            "  <li><b>Direct WebSocket Loopback:</b> Ensemble listens on <code>ws://127.0.0.1:26303</code> (or dynamic port). Connection parameters and active port are automatically published to your system's discovery manifest:<br>"
            "    • <b>Windows:</b> <code>%LOCALAPPDATA%\\Ensemble\\mcp.json</code><br>"
            "    • <b>Linux:</b> <code>~/.config/Ensemble/mcp.json</code><br>"
            "    • <b>macOS:</b> <code>~/Library/Application Support/Ensemble/mcp.json</code><br>"
            "    <i>(Active path on this machine: <code>%1</code>)</i>"
            "  </li>"
            "</ol>"
            "<h3>Setup Guide for AI Assistants</h3>"
            "<h4>1. Anthropic Claude Desktop</h4>"
            "<p>Open Claude Desktop's configuration file (<i>Settings → Developer → Edit Config</i>):</p>"
            "<ul>"
            "  <li><b>Windows:</b> <code>%APPDATA%\\Claude\\claude_desktop_config.json</code></li>"
            "  <li><b>Linux:</b> <code>~/.config/Claude/claude_desktop_config.json</code></li>"
            "  <li><b>macOS:</b> <code>~/Library/Application Support/Claude/claude_desktop_config.json</code></li>"
            "</ul>"
            "<p><b>Windows Configuration:</b></p>"
            "<pre class='code-block'>{\n"
            "  \"mcpServers\": {\n"
            "    \"ensemble\": {\n"
            "      \"command\": \"%2\",\n"
            "      \"args\": [\"--mcp-bridge\"]\n"
            "    }\n"
            "  }\n"
            "}</pre>"
            "<p><b>Linux / macOS Configuration:</b></p>"
            "<pre class='code-block'>{\n"
            "  \"mcpServers\": {\n"
            "    \"ensemble\": {\n"
            "      \"command\": \"/usr/local/bin/ensemble\",\n"
            "      \"args\": [\"--mcp-bridge\"]\n"
            "    }\n"
            "  }\n"
            "}</pre>"
            "<div class='callout-tip'>"
            "  <b>💡 1-Click Setup:</b> In Ensemble, open <b>Preferences → AI Settings</b> and click <b>📋 Copy MCP Config</b> to copy the pre-escaped configuration snippet tailored for your system, or click <b>📤 Send MCP Manual &amp; Setup to Agents</b> to configure Claude and Antigravity automatically!"
            "</div>"
            "<h4>2. Claude Code CLI</h4>"
            "<p>Add Ensemble directly to Claude Code from your terminal or command prompt:</p>"
            "<p><b>Windows (PowerShell / Command Prompt):</b></p>"
            "<pre class='code-block'>claude mcp add ensemble \"%3\" -- --mcp-bridge</pre>"
            "<p><i>(Or <code>claude mcp add ensemble ensemble.exe -- --mcp-bridge</code> if Ensemble is in your system PATH)</i></p>"
            "<p><b>Linux / macOS (Terminal):</b></p>"
            "<pre class='code-block'>claude mcp add ensemble /usr/local/bin/ensemble -- --mcp-bridge</pre>"
            "<p>Now run <code>claude</code> inside any terminal. You can ask: <i>\"Inspect the current chapter in Ensemble, check for pacing issues, and draft the next scene in chapter 2.\"</i></p>"
            "<h4>3. Cursor IDE</h4>"
            "<p>In Cursor, navigate to <i>Settings → Features → MCP</i>:</p>"
            "<ul>"
            "  <li>Click <b>+ Add New MCP Server</b>.</li>"
            "  <li>Name: <code>ensemble</code></li>"
            "  <li>Type: <code>command</code></li>"
            "  <li>Command on <b>Windows:</b> <code>\"%3\" --mcp-bridge</code> (or <code>ensemble.exe --mcp-bridge</code>)</li>"
            "  <li>Command on <b>Linux / macOS:</b> <code>/usr/local/bin/ensemble --mcp-bridge</code></li>"
            "</ul>"
            "<h4>4. Gemini Antigravity</h4>"
            "<p>Antigravity automatically discovers Ensemble via the configuration file at <code>%1</code> or by adding the stdio bridge command above to your <code>mcp_config.json</code>.</p>"
            "<h4>5. Local LLMs &amp; Privacy-First Models (Beta MCP Connector)</h4>"
            "<p>Ensemble provides <b>Beta support</b> for running local LLMs (via <b>llama.cpp</b>, <b>Ollama</b>, <b>LM Studio</b>, <b>Jan</b>, <b>LocalAI</b>, <b>Open WebUI</b>, or local Python agent scripts) to co-author and review your stories without cloud dependency.</p>"
            "<div class='callout-note'>"
            "  <b>🧪 Beta Notice &amp; Security Boundary:</b> Local LLM integration operates <b>only like an MCP connector</b>. All interactions are strictly confined to Ensemble's standard MCP tools and resources (chapter editing, AO3 sanitization, word counts, lore graph management, problem diagnostics). Local models cannot execute shell commands or access arbitrary files on your machine."
            "</div>"
            "<p><b>Ollama &amp; llama.cpp Setup:</b></p>"
            "<p>Local LLMs interact with Ensemble through the stdio bridge <code>%3 --mcp-bridge</code> or by executing the generated Python connector script:</p>"
            "<pre class='code-block'># For Ollama:\n"
            "python ensemble_local_llm_agent.py --ollama --model llama3.3:70b\n\n"
            "# For llama.cpp:\n"
            "python ensemble_local_llm_agent.py --llama-cpp --url http://127.0.0.1:8080/v1</pre>"
            "<p><b>LM Studio, Jan &amp; Open WebUI:</b></p>"
            "<p>Configure the MCP server command to run <code>%3 --mcp-bridge</code>, or connect to the local loopback WebSocket at <code>ws://127.0.0.1:26303</code> using the discovery file at <code>%1</code>.</p>"
            "<h3>Available MCP Tools Reference</h3>"
            "<table border='0' cellpadding='6' cellspacing='0' width='100%'>"
            "  <tr><th>MCP Tool</th><th>Function &amp; Capability</th></tr>"
            "  <tr><td><code>get_project_summary</code></td><td>Returns title, author, chapter stats, word counts, and unsaved edit state.</td></tr>"
            "  <tr class='row-alt'><td><code>list_chapters</code></td><td>Lists all chapters with indices, UUIDs, titles, and word counts.</td></tr>"
            "  <tr><td><code>get_chapter</code></td><td>Retrieves sanitized HTML or plain text of a chapter.</td></tr>"
            "  <tr class='row-alt'><td><code>update_chapter_html</code></td><td>Updates chapter content with automatic AO3 HTML sanitization.</td></tr>"
            "  <tr><td><code>add_chapter</code> / <code>delete_chapter</code></td><td>Adds new chapters or deletes existing chapters.</td></tr>"
            "  <tr class='row-alt'><td><code>reorder_chapters</code></td><td>Reorders chapters and updates story sequence.</td></tr>"
            "  <tr><td><code>get_workskin_css</code> / <code>set_workskin_css</code></td><td>Inspects or writes Work Skin CSS with instant live Chromium preview re-render.</td></tr>"
            "  <tr class='row-alt'><td><code>load_project</code> / <code>save_project</code></td><td>Loads and saves <code>.ao3proj</code> archives with optional commit messages.</td></tr>"
            "  <tr><td><code>get_project_history</code> / <code>add_history_commit</code></td><td>Inspects or records Git-like commits in <code>history.cmt</code>.</td></tr>"
            "  <tr class='row-alt'><td><code>get_lore_graph</code> / <code>update_lore_graph</code></td><td>Reads or updates the narrative <code>.grph</code> visual canvas.</td></tr>"
            "  <tr><td><code>add_lore_node</code> / <code>connect_lore_nodes</code></td><td>Adds story blocks and relationship connectors to the lore graph.</td></tr>"
            "  <tr class='row-alt'><td><code>get_lore_data</code> / <code>manage_lore_data</code></td><td>Queries and manages entities, history, conflicts, and plans in <code>lore/</code>.</td></tr>"
            "  <tr><td><code>update_project_metadata</code></td><td>Updates AO3 ratings, warnings, relationships, and tags in <code>metadata.xml</code>.</td></tr>"
            "  <tr class='row-alt'><td><code>insert_image_embed</code> / <code>list_image_embeds</code></td><td>Inserts or queries external URL-based image embeds in chapters.</td></tr>"
            "  <tr><td><code>post_agent_thought</code></td><td>Broadcasts model reasoning and plans to the live Agent Thoughts Monitor UI.</td></tr>"
            "  <tr class='row-alt'><td><code>get_agent_presence</code> / <code>register_agent_presence</code></td><td>Enables multi-agent awareness and co-author coordination.</td></tr>"
            "  <tr><td><code>get_agentic_summary</code> / <code>read_agentic_file</code> / <code>write_agentic_file</code></td><td>Accesses shared (5 MB cap) and private (20 MB cap) agent memory in <code>agentic-datas/</code>.</td></tr>"
            "  <tr class='row-alt'><td><code>get_problems</code></td><td>Returns real-time HTML sanitization and CSS lint diagnostics.</td></tr>"
            "  <tr><td><code>get_preview_screenshot</code></td><td>Captures a base64 PNG screenshot of the live preview pane for visual inspection.</td></tr>"
            "</table>"
            "<div class='callout-note'>"
            "  <b>🔒 Strict Local Security:</b> Ensemble's MCP server strictly binds to <code>127.0.0.1</code> (localhost loopback) and is completely invisible to external networks. You have total privacy and control over your writing."
            "</div>"
        ).arg(discoveryPath, escapedExeJson, appExe);
        m_topics.append(t);
    }

    // 9. Multi-Agent Collaboration & Thoughts
    {
        HelpTopic t;
        t.id = QStringLiteral("agentic_data");
        t.icon = QStringLiteral("🧠");
        t.title = QStringLiteral("Multi-Agent Collaboration & Thoughts");
        t.summary = QStringLiteral("Shared coroutine storage, localized model memory, quota enforcement, and live Agent Thoughts Monitor.");
        t.contentHtml = QStringLiteral(
            "<h1>🧠 Multi-Agent Collaboration & Thoughts</h1>"
            "<p class='lead'>Ensemble is built from the ground up for the multi-model era. Multiple AI agents (Claude, Gemini Antigravity, Cursor) can co-work on the same story, share notes, maintain private working memory, and broadcast their creative thoughts live to the author.</p>"
            "<hr>"
            "<h3>The <code>agentic-datas/</code> Storage Architecture</h3>"
            "<p>Inside every <code>.ao3proj</code> archive, Ensemble allocates a specialized container for AI collaboration:</p>"
            "<pre class='code-block'>agentic-datas/\n"
            "├── index.json               (Storage catalog, registered agents, and quota metrics)\n"
            "├── agent_model_history.xml  (Chronological XML audit log of model actions &amp; word deltas)\n"
            "├── agentifiers.json         (Encrypted AES-256-CBC-HMAC-SHA256 agent identity tokens)\n"
            "├── shared/                  (Coroutine collaboration directory shared by all models)\n"
            "└── &lt;agent&gt;/                 (Private localized memory, e.g. claude/, antigravity/, cursor/)</pre>"
            "<h3>Storage Quotas &amp; Hard Caps</h3>"
            "<p>To ensure project archives remain lightweight, tidy, and fast to load, Ensemble strictly enforces storage limits on agent data:</p>"
            "<ul>"
            "  <li><b>Shared Data (<code>shared/</code>):</b> Hard cap of <b>5 MB</b> (5,242,880 bytes). This space is shared between all AI models for collaborative notes, plot summaries, and world references.</li>"
            "  <li><b>Localized Agent Data (<code>&lt;agent&gt;/</code>):</b> Hard cap of <b>20 MB</b> (20,971,520 bytes) per agent. This private directory allows each AI model to store its own scratchpad, prompt chain cache, or model-specific embeddings.</li>"
            "</ul>"
            "<div class='callout-warning'>"
            "  <b>⚠️ Quota Enforcement:</b> Any MCP file write that would exceed either the 5 MB shared limit or the 20 MB localized limit is immediately rejected by Ensemble with an error."
            "</div>"
            "<h3>Real-Time Agent Thoughts Monitor (<kbd>Ctrl+Shift+A</kbd>)</h3>"
            "<p>Curious what your AI co-authors are thinking while they write or review your story? Open <i>View → 🧠 AI Agent Thoughts Monitor…</i> or press <kbd>Ctrl+Shift+A</kbd>.</p>"
            "<ul>"
            "  <li><b>Live Thought Stream:</b> Models use <code>post_agent_thought</code> to broadcast their internal reasoning, critique, pacing feedback, and planned edits.</li>"
            "  <li><b>Filter by Model:</b> Select <i>All Models</i> or isolate a specific assistant (e.g. <code>claude-3-7-sonnet</code> or <code>gemini-2.5-pro</code>) using the dropdown filter.</li>"
            "  <li><b>Chronological Date &amp; Time Sorting:</b> All thoughts and history entries are displayed with explicit calendar dates and timestamps in chronological sequence.</li>"
            "  <li><b>Recent Changes Audit:</b> Inspect each model's latest action, affected chapter index, and word count deltas (+/- words) in the summary pane.</li>"
            "</ul>"
            "<h3>Multi-Agent Presence Discovery</h3>"
            "<p>Connected models can call <code>get_agent_presence</code> and <code>register_agent_presence</code> to announce their roles (e.g. <i>\"Reviewer\"</i>, <i>\"Co-Author\"</i>, <i>\"Beta Reader\"</i>) and current working notes. This prevents agents from accidentally overwriting the same chapter at the same time.</p>"
        );
        m_topics.append(t);
    }

    // 11. Git-Like Snapshots & Revision Diffs (history/)
    {
        HelpTopic t;
        t.id = QStringLiteral("git_history");
        t.icon = QStringLiteral("📸");
        t.title = QStringLiteral("Git-Like Snapshots & Revision Diffs (history/)");
        t.summary = QStringLiteral("Internal story revision tracking, line-by-line diffs, host/user author tags, and dual timezone display.");
        t.contentHtml = QStringLiteral(
            "<h1>📸 Git-Like Snapshots & Revision Diffs (history/)</h1>"
            "<p class='lead'>Ensemble brings software engineering-grade revision tracking to fanfiction. Every chapter update, Work Skin adjustment, and author edit can be captured as a Snapshot with line-by-line diffs stored in your <code>.ao3proj</code> archive.</p>"
            "<hr>"
            "<h3>How Story Snapshots Work</h3>"
            "<p>Instead of saving dozens of confusing file copies like <code>Chapter_2_final_v3.docx</code>, Ensemble maintains an append-only commit ledger in <code>history.cmt</code> paired with unified diff files in the <code>history/</code> directory:</p>"
            "<pre class='code-block'>history/\n"
            "├── 3f9a1b2c4e5d.diff   (Line-by-line unified diff of modifications)\n"
            "└── history.cmt         (Ledger with 12-char SHA-1 hashes, author tags, and messages)</pre>"
            "<h3>Line-by-Line Diff Viewer</h3>"
            "<p>Open <i>File → Properties…</i> (<kbd>Alt+Enter</kbd>) and select <b>📜 Revision History</b>. Selecting any snapshot displays an interactive line-by-line diff showing precisely what was added (<code>+</code>) in green and removed (<code>-</code>) in red.</p>"
            "<h3>Host &amp; User Author Tagging</h3>"
            "<p>To keep multi-device and collaborative writing organized, snapshots automatically tag authors with a host signature: <code>&lt;username&gt;@&lt;desktop-id&gt;</code> (e.g. <code>miku@DESKTOP-HH37G7</code>). Connected AI models are required to identify themselves with their unique model ID.</p>"
            "<h3>Dual Timezone Timestamps</h3>"
            "<p>Snapshots preserve standard ISO-8601 UTC timestamps while simultaneously rendering date and time adjusted to your system's configured local timezone.</p>"
            "<h3>Backwards Compatibility</h3>"
            "<p>Projects created before the snapshot diff system was introduced are safely handled with a gentle fallback notice indicating that older revisions predate diff storage.</p>"
            "<h3>Recording Snapshots via MCP</h3>"
            "<p>AI agents automatically record their changes into the ledger using:</p>"
            "<ul>"
            "  <li><code>add_history_commit(message, author)</code>: Records a new snapshot entry and generates a diff.</li>"
            "  <li><code>save_project(commitMessage, author)</code>: Saves the project file to disk and records the snapshot in a single atomic step.</li>"
            "  <li><code>get_project_history(limit)</code>: Inspects recent commits to understand what was changed previously.</li>"
            "</ul>"
            "<div class='callout-tip'>"
            "  <b>✨ Peace of Mind:</b> Because <code>history/</code> travels inside your portable <code>.ao3proj</code> container, your entire creative journey and drafting history is preserved wherever you move your story file."
            "</div>"
        );
        m_topics.append(t);
    }

    // 11. Raw HTML & Sanitizer
    {
        HelpTopic t;
        t.id = QStringLiteral("html_sanitizer");
        t.icon = QStringLiteral("🏷️");
        t.title = QStringLiteral("Raw HTML & AO3 Sanitization");
        t.summary = QStringLiteral("Permitted tags, source view, and security rules.");
        t.contentHtml = QStringLiteral(
            "<h1>🏷️ Raw HTML & AO3 Sanitization</h1>"
            "<p class='lead'>For advanced authors who prefer writing HTML directly or inspecting markup before uploading, Ensemble features a full-fledged <b>Raw HTML</b> source pane with syntax highlighting and instant tag linting.</p>"
            "<hr>"
            "<h3>Permitted AO3 HTML Tags</h3>"
            "<p>AO3 enforces strict HTML sanitization rules to protect readers. Ensemble's built-in sanitizer checks against the official whitelist:</p>"
            "<ul>"
            "  <li><b>Structure:</b> <code>&lt;p&gt;</code>, <code>&lt;div&gt;</code>, <code>&lt;blockquote&gt;</code>, <code>&lt;hr&gt;</code>, <code>&lt;br&gt;</code></li>"
            "  <li><b>Typography:</b> <code>&lt;h1&gt;</code> to <code>&lt;h6&gt;</code>, <code>&lt;em&gt;</code>, <code>&lt;strong&gt;</code>, <code>&lt;b&gt;</code>, <code>&lt;i&gt;</code>, <code>&lt;u&gt;</code>, <code>&lt;s&gt;</code>, <code>&lt;strike&gt;</code></li>"
            "  <li><b>Details &amp; Lists:</b> <code>&lt;ul&gt;</code>, <code>&lt;ol&gt;</code>, <code>&lt;li&gt;</code>, <code>&lt;dl&gt;</code>, <code>&lt;dt&gt;</code>, <code>&lt;dd&gt;</code>, <code>&lt;details&gt;</code>, <code>&lt;summary&gt;</code></li>"
            "  <li><b>Tables &amp; Links:</b> <code>&lt;table&gt;</code>, <code>&lt;tr&gt;</code>, <code>&lt;td&gt;</code>, <code>&lt;th&gt;</code>, <code>&lt;a&gt;</code> (with <code>href</code>)</li>"
            "</ul>"
            "<div class='callout-warning'>"
            "  <b>⚠️ Disallowed Tags &amp; <code>&lt;span&gt;</code> Restriction:</b> Elements like <code>&lt;script&gt;</code>, <code>&lt;iframe&gt;</code>, <code>&lt;style&gt;</code>, and inline <code>onclick</code> attributes are automatically stripped by AO3's parser. In addition, <b><code>&lt;span&gt;</code> is actively disallowed in chapter text</b> by Ensemble because inline spans fragment WYSIWYG selection, cursor navigation, and line-spacing. Use <code>&lt;em&gt;</code>, <code>&lt;strong&gt;</code>, or class-styled <code>&lt;p&gt;</code> and <code>&lt;div&gt;</code> blocks instead."
            "</div>"
        );
        m_topics.append(t);
    }

    // 13. Diagnostics & Problems
    {
        HelpTopic t;
        t.id = QStringLiteral("diagnostics");
        t.icon = QStringLiteral("🔍");
        t.title = QStringLiteral("Diagnostics & Linting Panel");
        t.summary = QStringLiteral("Finding broken CSS classes, syntax issues, and tag errors.");
        t.contentHtml = QStringLiteral(
            "<h1>🔍 Diagnostics & Linting Panel</h1>"
            "<p class='lead'>Ensemble continuously inspects your active work and chapters for common authoring issues before you post to AO3.</p>"
            "<hr>"
            "<h3>What Does Diagnostics Check?</h3>"
            "<ul>"
            "  <li><b>Missing CSS Classes:</b> Detects HTML elements using classes not declared in your Work Skin CSS.</li>"
            "  <li><b>Dead CSS Rules:</b> Flags custom classes in your Work Skin that are never referenced across any chapters.</li>"
            "  <li><b>Sanitization Warnings:</b> Identifies non-whitelisted HTML tags or malformed attributes.</li>"
            "</ul>"
            "<h3>Accessing the Problems Dialog</h3>"
            "<p>Click the <b>Problems</b> pill button in the status bar at the bottom right (e.g. <code>✓ 0 Problems</code> or <code>⚠️ 2 Problems</code>). Clicking any diagnostic entry jumps directly to the corresponding chapter or Work Skin line!</p>"
        );
        m_topics.append(t);
    }

    // 14. Project Archiving (.ao3proj)
    {
        HelpTopic t;
        t.id = QStringLiteral("archiving");
        t.icon = QStringLiteral("📦");
        t.title = QStringLiteral("Project Files & Archiving");
        t.summary = QStringLiteral(".ao3proj archive format, space savings, and unified storage.");
        t.contentHtml = QStringLiteral(
            "<h1>📦 Project Files & Archiving (.ao3proj)</h1>"
            "<p class='lead'>Ensemble stores all story metadata, chapter drafts, revisions, and Work Skin stylesheets in a single unified <code>.ao3proj</code> project archive.</p>"
            "<hr>"
            "<h3>Unified Single-File Storage</h3>"
            "<p>Traditional authoring often involves juggling dozens of fragmented chapter text files, separate CSS stylesheets, or bulky word processor documents (such as <code>.docx</code>) that consume unnecessary space and risk losing pieces during file management.</p>"
            "<p>Ensemble avoids this clutter by bundling your entire work into a single portable <code>.ao3proj</code> file, keeping everything synchronized, organized, and safe in one place.</p>"
            "<h3>Archive Specifications</h3>"
            "<ul>"
            "  <li><b>Space-Efficient Archiving:</b> Saves disk space compared to keeping fragmented files or uncompressed document formats. While extensive projects with dozens of chapters and long revision logs will naturally result in larger file sizes, the unified structure keeps overall disk usage minimal and tidy.</li>"
            "  <li><b>Real-Time Lossless Compression:</b> Packages chapters and metadata efficiently without degrading formatting or text fidelity.</li>"
            "  <li><b>Backward Compatibility:</b> Transparently reads legacy ZIP/Deflate project archives created with earlier versions of the software.</li>"
            "  <li><b>Git-Friendly Metadata:</b> Includes structured metadata and an internal history log tracking project modifications and edits.</li>"
            "</ul>"
            "<div class='callout-tip'>"
            "  <b>💡 Portable & All-in-One:</b> Because your entire work—including custom Work Skin CSS rules—is contained within the <code>.ao3proj</code> archive, you can easily share, back up, or transfer your stories between devices without missing external assets."
            "</div>"
        );
        m_topics.append(t);
    }

    // 15. Exporting to AO3
    {
        HelpTopic t;
        t.id = QStringLiteral("exporting");
        t.icon = QStringLiteral("📤");
        t.title = QStringLiteral("Exporting & Posting to AO3");
        t.summary = QStringLiteral("Copy HTML, export chapters, and import existing stories.");
        t.contentHtml = QStringLiteral(
            "<h1>📤 Exporting & Posting to AO3</h1>"
            "<p class='lead'>Posting your finished chapter to Archive of Our Own takes only seconds with Ensemble's dedicated export tools.</p>"
            "<hr>"
            "<h3>One-Click HTML Copy</h3>"
            "<p>Press <kbd>Ctrl+Shift+C</kbd> (or select <i>Export → Copy HTML to Clipboard</i>). Ensemble sanitizes the chapter, optimizes paragraph spacing, and copies the clean HTML directly to your clipboard. Then simply paste it into AO3's <b>HTML</b> editor box!</p>"
            "<h3>Exporting Files</h3>"
            "<ul>"
            "  <li><i>Export Current Chapter HTML…</i>: Saves a standalone <code>.html</code> file with work skin headers embedded.</li>"
            "  <li><i>Export All Chapters…</i>: Batch exports all chapters into a dedicated directory.</li>"
            "</ul>"
            "<h3>Import from AO3</h3>"
            "<p>Select <i>File → Import from AO3…</i> (<kbd>Ctrl+Shift+I</kbd>) and paste an AO3 story URL. Ensemble parses the title, chapters, and work skin, importing the work directly into an editable project.</p>"
        );
        m_topics.append(t);
    }

    // 16. Themes & Design Systems
    {
        HelpTopic t;
        t.id = QStringLiteral("themes_design");
        t.icon = QStringLiteral("⚙️");
        t.title = QStringLiteral("Themes & Design Systems");
        t.summary = QStringLiteral("Material You, KDE Breeze, Qt Fusion, Dark, and OLED modes.");
        t.contentHtml = QStringLiteral(
            "<h1>⚙️ Themes & Design Systems</h1>"
            "<p class='lead'>Customize Ensemble's visual aesthetics to match your desktop environment and personal writing preferences in <b>Preferences</b> (<kbd>Ctrl+,</kbd>).</p>"
            "<hr>"
            "<h3>Color Schemes</h3>"
            "<ul>"
            "  <li><b>Dark Mode (<kbd>Ctrl+Alt+1</kbd>):</b> Deep slate dark palette with high-contrast pastel accents.</li>"
            "  <li><b>Darker Mode / OLED (<kbd>Ctrl+Alt+2</kbd>):</b> Pure <code>#000000</code> pitch-black background optimized for OLED and AMOLED displays.</li>"
            "  <li><b>Light Mode (<kbd>Ctrl+Alt+3</kbd>):</b> Clean, high-contrast bright theme using Slate 900 (<code>#0f172a</code>) text for crystal-clear readability.</li>"
            "  <li><b>AO3 Reversi Mode (<kbd>Ctrl+Alt+T</kbd>):</b> Authentic AO3 dark theme palette with <code>#111111</code> dark charcoal background and crimson red accents.</li>"
            "</ul>"
            "<h3>Skin Flavors</h3>"
            "<ul>"
            "  <li><b>🤖 Android Material You:</b> Dynamically queries your desktop environment (Windows Desktop Wallpaper &amp; DWM Accent Color, KDE Plasma DBus, GNOME gsettings, etc.) to extract dominant hues directly from your active desktop wallpaper. Features authentic Material 3 pill buttons (20px radius) and fluid selection highlights.</li>"
            "  <li><b>🐧 KDE UI:</b> Integrates with native KDE Breeze styling (3px button corners, Breeze blue accents).</li>"
            "  <li><b>🖥️ Standard Qt:</b> Uses Qt's native Fusion desktop engine with clean 4px control geometry.</li>"
            "  <li><b>✨ Standard Ensemble:</b> Curated violet and slate aesthetic.</li>"
            "</ul>"
            "<div class='callout-tip'>"
            "  <b>⚡ Live Switching:</b> Theme and Skin Flavor changes apply instantly in real-time across the entire UI without requiring an application restart!"
            "</div>"
        );
        m_topics.append(t);
    }

    // 17. Keyboard Shortcuts
    {
        HelpTopic t;
        t.id = QStringLiteral("shortcuts");
        t.icon = QStringLiteral("⌨️");
        t.title = QStringLiteral("Keyboard Shortcuts Reference");
        t.summary = QStringLiteral("Complete hotkey reference for navigation and editing.");
        t.contentHtml = QStringLiteral(
            "<h1>⌨️ Keyboard Shortcuts Reference</h1>"
            "<p class='lead'>Speed up your fanfiction authoring workflow with keyboard shortcuts.</p>"
            "<hr>"
            "<table border='0' cellpadding='6' cellspacing='0' width='100%'>"
            "  <tr><th>Shortcut</th><th>Action</th><th>Context</th></tr>"
            "  <tr><td><kbd>Ctrl+N</kbd></td><td>New Project</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+O</kbd></td><td>Open Project…</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+S</kbd></td><td>Save Project</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Shift+S</kbd></td><td>Save Project As…</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+W</kbd></td><td>Close Project / Main Menu</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Q</kbd></td><td>Quit Ensemble</td><td>Global</td></tr>"
            "  <tr><td><kbd>Alt+Enter</kbd></td><td>Project Properties &amp; Metadata</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Shift+O</kbd></td><td>Open Chapter Manager</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+Shift+L</kbd></td><td>Lore Graph &amp; Narrative Map</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Shift+A</kbd></td><td>AI Agent Thoughts Monitor</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+Shift+M</kbd></td><td>Insert Image Embed Attachment</td><td>Editor</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Shift+C</kbd></td><td>Copy Chapter HTML to Clipboard</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+Shift+I</kbd></td><td>Import from AO3…</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+P</kbd></td><td>Toggle Live Preview Pane</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+Shift+P</kbd></td><td>Pop Out Preview Window</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+F</kbd></td><td>Find Text</td><td>Editor</td></tr>"
            "  <tr><td><kbd>Ctrl+R</kbd></td><td>Find and Replace Text</td><td>Editor</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+B</kbd></td><td>Bold Selection</td><td>Rich Text</td></tr>"
            "  <tr><td><kbd>Ctrl+I</kbd></td><td>Italic Selection</td><td>Rich Text</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+U</kbd></td><td>Underline Selection</td><td>Rich Text</td></tr>"
            "  <tr><td><kbd>Ctrl+Shift+X</kbd></td><td>Strikethrough Selection</td><td>Rich Text</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+,</kbd></td><td>Open Preferences / Settings</td><td>Global</td></tr>"
            "  <tr><td><kbd>F1</kbd></td><td>Open Help &amp; User Guide</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Alt+1</kbd></td><td>Dark Mode</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+Alt+2</kbd></td><td>Darker / OLED Mode</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Alt+3</kbd></td><td>Light Mode</td><td>Global</td></tr>"
            "  <tr><td><kbd>Ctrl+Alt+T</kbd></td><td>Cycle Theme (Light / Dark / Reversi)</td><td>Global</td></tr>"
            "  <tr class='row-alt'><td><kbd>Ctrl+Alt+D</kbd></td><td>Debug Console (Debug build)</td><td>Global</td></tr>"
            "</table>"
        );
        m_topics.append(t);
    }
}

void HelpGuideDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    // Header banner
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto *iconLabel = new QLabel(this);
    iconLabel->setPixmap(AppIcon::pixmap(40, 40));
    headerLayout->addWidget(iconLabel);

    auto *headerTextLayout = new QVBoxLayout();
    headerTextLayout->setSpacing(2);

    m_headerTitle = new QLabel(QStringLiteral("Ensemble Help & User Guide"), this);
    m_headerTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    m_headerSubTitle = new QLabel(
        QStringLiteral("Comprehensive documentation for writing, styling, lore mapping, AI co-authoring, and publishing AO3 fanfiction."), this);
    m_headerSubTitle->setStyleSheet(QStringLiteral("font-size: 12px;"));

    headerTextLayout->addWidget(m_headerTitle);
    headerTextLayout->addWidget(m_headerSubTitle);
    headerLayout->addLayout(headerTextLayout, 1);

    mainLayout->addLayout(headerLayout);

    // Filter box
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("🔍 Filter topics or search documentation..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HelpGuideDialog::onSearchTextChanged);
    mainLayout->addWidget(m_searchEdit);

    // Main Splitter (Left: Topics, Right: Content)
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(8);

    m_topicList = new QListWidget(splitter);
    m_topicList->setObjectName(QStringLiteral("topicList"));
    m_topicList->setMinimumWidth(240);
    m_topicList->setMaximumWidth(450);
    m_topicList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_topicList->setTextElideMode(Qt::ElideRight);
    m_topicList->setMouseTracking(true);
    m_topicList->setItemDelegate(new HelpTopicDelegate(m_topicList));

    for (const auto &topic : m_topics) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2\n    %3").arg(topic.icon, topic.title, topic.summary),
            m_topicList);
        item->setData(Qt::UserRole, topic.id);
        item->setData(Qt::UserRole + 1, topic.icon);
        item->setData(Qt::UserRole + 2, topic.title);
        item->setData(Qt::UserRole + 3, topic.summary);
        item->setToolTip(QStringLiteral("%1 %2\n%3").arg(topic.icon, topic.title, topic.summary));
    }
    connect(m_topicList, &QListWidget::itemSelectionChanged, this,
            &HelpGuideDialog::onTopicSelectionChanged);

    m_contentBrowser = new QTextBrowser(splitter);
    m_contentBrowser->setOpenExternalLinks(true);

    splitter->addWidget(m_topicList);
    splitter->addWidget(m_contentBrowser);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 680});

    mainLayout->addWidget(splitter, 1);

    // Footer
    auto *footerLayout = new QHBoxLayout();
    auto *footerNote = new QLabel(
        QStringLiteral("Press F1 anytime to access this guide. For updates, visit Archive of Our Own."), this);
    footerNote->setStyleSheet(QStringLiteral("font-size: 11px; opacity: 0.8;"));

    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    m_closeBtn->setObjectName(QStringLiteral("closeBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    footerLayout->addWidget(footerNote);
    footerLayout->addStretch(1);
    footerLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(footerLayout);
}

void HelpGuideDialog::updateTheme()
{
    const bool isDark = ThemeManager::instance().isDarkMode();
    const bool isOled = ThemeManager::instance().isOledMode();
    const QString bg = isOled ? QStringLiteral("#000000") : ThemeManager::instance().windowBgColor().name();
    const QString card = isOled ? QStringLiteral("#0a0a0c") : ThemeManager::instance().cardBgColor().name();
    const QString text = ThemeManager::instance().primaryTextColor().name();
    const QString muted = isOled ? QStringLiteral("#94a3b8") : ThemeManager::instance().mutedTextColor().name();
    const QString border = isOled ? QStringLiteral("#334155") : ThemeManager::instance().borderColor().name();
    const QString accent = ThemeManager::instance().accentColor().name();
    const QString accentHover = ThemeManager::instance().accentHoverColor().name();
    const int btnRad = ThemeManager::instance().buttonRadius();
    const int cRad = ThemeManager::instance().cardRadius();
    const int inRad = ThemeManager::instance().inputRadius();

    setStyleSheet(isDark ? QStringLiteral(
        "QDialog {"
        "  background-color: %1;"
        "  color: %2;"
        "}"
        "QLineEdit {"
        "  background-color: %3;"
        "  color: %2;"
        "  border: 1px solid %4;"
        "  border-radius: %5px;"
        "  padding: 7px 12px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "  border: 2px solid %6;"
        "}"
        "QListWidget {"
        "  background-color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: %7px;"
        "  padding: 6px;"
        "  color: %2;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 10px;"
        "  border-radius: %5px;"
        "  margin-bottom: 3px;"
        "  color: %2;"
        "}"
        "QListWidget::item:hover:!selected {"
        "  background-color: rgba(255, 255, 255, 0.08);"
        "  color: %2;"
        "}"
        "QListWidget::item:selected, QListWidget::item:selected:!active {"
        "  background-color: %6;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "}"
        "QListWidget::item:selected:hover {"
        "  background-color: %9;"
        "  color: #ffffff;"
        "}"
        "QTextBrowser {"
        "  background-color: %3;"
        "  color: %2;"
        "  border: 1px solid %4;"
        "  border-radius: %7px;"
        "  padding: 18px 22px;"
        "}"
        "QPushButton#closeBtn {"
        "  background-color: %6;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %8px;"
        "  padding: 8px 24px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#closeBtn:hover {"
        "  background-color: %9;"
        "}"
    ).arg(bg, text, card, border).arg(inRad).arg(accent).arg(cRad).arg(btnRad).arg(accentHover) : QStringLiteral(
        "QDialog {"
        "  background-color: %1;"
        "  color: %2;"
        "}"
        "QLineEdit {"
        "  background-color: #ffffff;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  padding: 7px 12px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "  border: 2px solid %5;"
        "}"
        "QListWidget {"
        "  background-color: #ffffff;"
        "  border: 1px solid %3;"
        "  border-radius: %6px;"
        "  padding: 6px;"
        "  color: %2;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 10px;"
        "  border-radius: %4px;"
        "  margin-bottom: 3px;"
        "  color: %2;"
        "}"
        "QListWidget::item:hover:!selected {"
        "  background-color: #f1f5f9;"
        "  color: %2;"
        "}"
        "QListWidget::item:selected, QListWidget::item:selected:!active {"
        "  background-color: %5;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "}"
        "QListWidget::item:selected:hover {"
        "  background-color: %8;"
        "  color: #ffffff;"
        "}"
        "QTextBrowser {"
        "  background-color: #ffffff;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %6px;"
        "  padding: 18px 22px;"
        "}"
        "QPushButton#closeBtn {"
        "  background-color: %5;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %7px;"
        "  padding: 8px 24px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#closeBtn:hover {"
        "  background-color: %8;"
        "}"
    ).arg(bg, text, border).arg(inRad).arg(accent).arg(cRad).arg(btnRad).arg(accentHover));

    if (m_headerTitle) {
        m_headerTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: %1;").arg(text));
    }
    if (m_headerSubTitle) {
        m_headerSubTitle->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;").arg(muted));
    }

    // Refresh active content styling
    onTopicSelectionChanged();
}

void HelpGuideDialog::selectTopic(const QString &topicId)
{
    for (int i = 0; i < m_topicList->count(); ++i) {
        if (m_topicList->item(i)->data(Qt::UserRole).toString() == topicId) {
            m_topicList->setCurrentRow(i);
            onTopicSelectionChanged();
            break;
        }
    }
}

void HelpGuideDialog::onTopicSelectionChanged()
{
    QListWidgetItem *item = m_topicList->currentItem();
    if (!item) return;

    const QString id = item->data(Qt::UserRole).toString();
    for (const auto &topic : m_topics) {
        if (topic.id == id) {
            renderTopic(topic);
            break;
        }
    }
}

void HelpGuideDialog::onSearchTextChanged(const QString &text)
{
    const QString filter = text.trimmed().toLower();
    for (int i = 0; i < m_topicList->count(); ++i) {
        QListWidgetItem *item = m_topicList->item(i);
        const QString id = item->data(Qt::UserRole).toString();
        bool match = false;
        if (filter.isEmpty()) {
            match = true;
        } else {
            for (const auto &topic : m_topics) {
                if (topic.id == id) {
                    if (topic.title.toLower().contains(filter) ||
                        topic.summary.toLower().contains(filter) ||
                        topic.contentHtml.toLower().contains(filter)) {
                        match = true;
                    }
                    break;
                }
            }
        }
        item->setHidden(!match);
    }
}

void HelpGuideDialog::renderTopic(const HelpTopic &topic)
{
    const bool isDark = ThemeManager::instance().isDarkMode();
    const bool isOled = ThemeManager::instance().isOledMode();
    const QString textColor = ThemeManager::instance().primaryTextColor().name();
    const QString mutedColor = isOled ? QStringLiteral("#94a3b8") : ThemeManager::instance().mutedTextColor().name();

    const QString h3Color = isDark ? QStringLiteral("#38bdf8") : QStringLiteral("#4338ca");
    const QString h1Color = textColor;

    // Inline code styling (clean monospaced highlight without jagged borders)
    const QString codeBg = isOled ? QStringLiteral("#111827") : (isDark ? QStringLiteral("#1e293b") : QStringLiteral("#e2e8f0"));
    const QString codeText = isDark ? QStringLiteral("#38bdf8") : QStringLiteral("#0284c7");

    // Keycap badge styling
    const QString kbdBg = isOled ? QStringLiteral("#18181b") : (isDark ? QStringLiteral("#27272a") : QStringLiteral("#f8fafc"));
    const QString kbdBorder = isOled ? QStringLiteral("#3f3f46") : (isDark ? QStringLiteral("#52525b") : QStringLiteral("#cbd5e1"));
    const QString kbdText = textColor;

    // Table styling
    const QString thBg = isOled ? QStringLiteral("#111827") : (isDark ? QStringLiteral("#1e293b") : QStringLiteral("#f1f5f9"));
    const QString tableBorder = isOled ? QStringLiteral("#27272a") : (isDark ? QStringLiteral("#3f3f46") : QStringLiteral("#e2e8f0"));
    const QString thBorder = isDark ? QStringLiteral("#38bdf8") : QStringLiteral("#6366f1");
    const QString trAltBg = isOled ? QStringLiteral("#09090b") : (isDark ? QStringLiteral("#16171d") : QStringLiteral("#f8fafc"));

    // Callout boxes
    const QString tipBg = isOled ? QStringLiteral("#061f17") : (isDark ? QStringLiteral("#062b1f") : QStringLiteral("#ecfdf5"));
    const QString tipBorder = isOled ? QStringLiteral("#064e3b") : (isDark ? QStringLiteral("#065f46") : QStringLiteral("#a7f3d0"));
    const QString tipBar = QStringLiteral("#10b981");
    const QString tipText = isDark ? QStringLiteral("#a7f3d0") : QStringLiteral("#065f46");

    const QString warnBg = isOled ? QStringLiteral("#241403") : (isDark ? QStringLiteral("#2b1d06") : QStringLiteral("#fffbeb"));
    const QString warnBorder = isOled ? QStringLiteral("#5e2908") : (isDark ? QStringLiteral("#78350f") : QStringLiteral("#fde68a"));
    const QString warnBar = QStringLiteral("#f59e0b");
    const QString warnText = isDark ? QStringLiteral("#fde68a") : QStringLiteral("#78350f");

    const QString noteBg = isOled ? QStringLiteral("#081b2e") : (isDark ? QStringLiteral("#0c2138") : QStringLiteral("#eff6ff"));
    const QString noteBorder = isOled ? QStringLiteral("#1e3a8a") : (isDark ? QStringLiteral("#1e40af") : QStringLiteral("#bfdbfe"));
    const QString noteBar = QStringLiteral("#38bdf8");
    const QString noteText = isDark ? QStringLiteral("#bae6fd") : QStringLiteral("#1e40af");

    // Code block container <pre> (clean uniform box without individual line stripes)
    const QString preBg = isOled ? QStringLiteral("#090a0f") : (isDark ? QStringLiteral("#13141a") : QStringLiteral("#f8fafc"));
    const QString preBorder = isOled ? QStringLiteral("#27272a") : (isDark ? QStringLiteral("#3f3f46") : QStringLiteral("#cbd5e1"));
    const QString preBar = isDark ? QStringLiteral("#38bdf8") : QStringLiteral("#6366f1");
    const QString preText = isDark ? QStringLiteral("#f8f8f2") : QStringLiteral("#0f172a");

    QString doc = QStringLiteral(
        "<html><head><style>"
        "body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; color: %TEXT%; line-height: 1.6; margin: 0; padding: 0; }"
        "h1 { font-size: 20px; font-weight: 700; color: %H1%; margin-top: 0; margin-bottom: 8px; }"
        "h3 { font-size: 15px; font-weight: 700; color: %H3%; margin-top: 18px; margin-bottom: 6px; }"
        ".lead { font-size: 13px; color: %MUTED%; margin-bottom: 12px; }"
        "hr { border: none; height: 1px; background-color: %TABLE_BORDER%; margin: 12px 0; }"
        "p, li { font-size: 12px; color: %TEXT%; }"
        "ul { padding-left: 20px; margin: 8px 0; }"
        "li { margin-bottom: 6px; }"
        "code { font-family: monospace; font-size: 11px; color: %CODE_TEXT%; background-color: %CODE_BG%; }"
        "pre, .code-block { font-family: monospace; font-size: 11px; background-color: %PRE_BG%; border: 1px solid %PRE_BORDER%; border-left: 4px solid %PRE_BAR%; padding: 12px 14px; margin: 12px 0; color: %PRE_TEXT%; }"
        "kbd { font-family: monospace; font-size: 11px; font-weight: bold; color: %KBD_TEXT%; background-color: %KBD_BG%; border: 1px solid %KBD_BORDER%; }"
        "table { width: 100%; border-collapse: collapse; margin: 14px 0; font-size: 12px; }"
        "th { text-align: left; padding: 8px 10px; background-color: %TH_BG%; border-bottom: 2px solid %TH_BORDER%; font-weight: bold; color: %TEXT%; }"
        "td { padding: 6px 10px; border-bottom: 1px solid %TABLE_BORDER%; color: %TEXT%; }"
        ".row-alt { background-color: %TR_ALT%; }"
        ".callout-box, .callout-tip, .callout-note, .callout-warning { padding: 10px 14px; margin: 12px 0; font-size: 12px; }"
        ".callout-tip { background-color: %TIP_BG%; border: 1px solid %TIP_BORDER%; border-left: 4px solid %TIP_BAR%; color: %TIP_TEXT%; }"
        ".callout-warning { background-color: %WARN_BG%; border: 1px solid %WARN_BORDER%; border-left: 4px solid %WARN_BAR%; color: %WARN_TEXT%; }"
        ".callout-note { background-color: %NOTE_BG%; border: 1px solid %NOTE_BORDER%; border-left: 4px solid %NOTE_BAR%; color: %NOTE_TEXT%; }"
        "</style></head><body>"
        "%CONTENT%"
        "</body></html>"
    );

    doc.replace(QStringLiteral("%TEXT%"), textColor);
    doc.replace(QStringLiteral("%MUTED%"), mutedColor);
    doc.replace(QStringLiteral("%H1%"), h1Color);
    doc.replace(QStringLiteral("%H3%"), h3Color);
    doc.replace(QStringLiteral("%CODE_BG%"), codeBg);
    doc.replace(QStringLiteral("%CODE_TEXT%"), codeText);
    doc.replace(QStringLiteral("%KBD_BG%"), kbdBg);
    doc.replace(QStringLiteral("%KBD_BORDER%"), kbdBorder);
    doc.replace(QStringLiteral("%KBD_TEXT%"), kbdText);
    doc.replace(QStringLiteral("%TABLE_BORDER%"), tableBorder);
    doc.replace(QStringLiteral("%TH_BG%"), thBg);
    doc.replace(QStringLiteral("%TH_BORDER%"), thBorder);
    doc.replace(QStringLiteral("%TR_ALT%"), trAltBg);
    doc.replace(QStringLiteral("%TIP_BG%"), tipBg);
    doc.replace(QStringLiteral("%TIP_BORDER%"), tipBorder);
    doc.replace(QStringLiteral("%TIP_BAR%"), tipBar);
    doc.replace(QStringLiteral("%TIP_TEXT%"), tipText);
    doc.replace(QStringLiteral("%WARN_BG%"), warnBg);
    doc.replace(QStringLiteral("%WARN_BORDER%"), warnBorder);
    doc.replace(QStringLiteral("%WARN_BAR%"), warnBar);
    doc.replace(QStringLiteral("%WARN_TEXT%"), warnText);
    doc.replace(QStringLiteral("%NOTE_BG%"), noteBg);
    doc.replace(QStringLiteral("%NOTE_BORDER%"), noteBorder);
    doc.replace(QStringLiteral("%NOTE_BAR%"), noteBar);
    doc.replace(QStringLiteral("%NOTE_TEXT%"), noteText);
    doc.replace(QStringLiteral("%PRE_BG%"), preBg);
    doc.replace(QStringLiteral("%PRE_BORDER%"), preBorder);
    doc.replace(QStringLiteral("%PRE_BAR%"), preBar);
    doc.replace(QStringLiteral("%PRE_TEXT%"), preText);
    doc.replace(QStringLiteral("%CONTENT%"), topic.contentHtml);

    m_contentBrowser->setHtml(doc);
}
