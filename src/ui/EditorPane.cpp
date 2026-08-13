#include "EditorPane.h"

#include "export/Ao3HtmlExporter.h"
#include "export/Ao3HtmlImporter.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QFont>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFormat>
#include <QTextListFormat>
#include "EditorMinimap.h"
#include "checker/SpellChecker.h"
#include <QHBoxLayout>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

// Custom property ID for storing CSS class names on text formats
static constexpr int CssClassProperty = QTextFormat::Property::UserProperty + 1;

EditorPane::EditorPane(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    setupToolbar();

    m_editor = new QTextEdit(this);
    m_editor->setAcceptRichText(true);
    m_editor->setTabChangesFocus(false);
    QFont editorFont(QStringLiteral("Georgia"), 12);
    m_editor->setFont(editorFont);
    m_editor->document()->setDefaultFont(editorFont);

    // Enable custom context menu
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QTextEdit::customContextMenuRequested,
            this, &EditorPane::showEditorContextMenu);

    // Repaint editor viewport on scroll so document lines never disappear
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_editor && m_editor->viewport())
            m_editor->viewport()->update();
    });

    // Minimap Scroll Preview sidebar
    m_minimap = new EditorMinimap(this);
    m_minimap->setTargetEditor(m_editor);

    auto *editorRow = new QHBoxLayout();
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(0);
    editorRow->addWidget(m_editor, 1);
    editorRow->addWidget(m_minimap);

    layout->addWidget(m_toolbar);
    layout->addLayout(editorRow, 1);

    // Spell & Grammar checking system
    m_spellChecker = new SpellChecker(this);
    m_spellTimer = new QTimer(this);
    m_spellTimer->setSingleShot(true);
    m_spellTimer->setInterval(400);
    connect(m_spellTimer, &QTimer::timeout, this, &EditorPane::runSpellAndGrammarChecks);

    connect(m_editor->document(), &QTextDocument::contentsChanged, this, &EditorPane::onDocumentChanged);
}

void EditorPane::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);

    m_toolbar->addAction(QStringLiteral("B"), this, &EditorPane::onBold)->setShortcut(QKeySequence::Bold);
    m_toolbar->addAction(QStringLiteral("I"), this, &EditorPane::onItalic)->setShortcut(QKeySequence::Italic);
    m_toolbar->addAction(QStringLiteral("U"), this, &EditorPane::onUnderline)->setShortcut(QKeySequence::Underline);
    m_toolbar->addAction(QStringLiteral("S"), this, &EditorPane::onStrikethrough);
    m_toolbar->addSeparator();
    m_toolbar->addAction(QStringLiteral("H1"), this, [this]() { onHeading(1); });
    m_toolbar->addAction(QStringLiteral("H2"), this, [this]() { onHeading(2); });
    m_toolbar->addAction(QStringLiteral("H3"), this, [this]() { onHeading(3); });
    m_toolbar->addAction(QStringLiteral("P"), this, &EditorPane::onParagraph);
    m_toolbar->addSeparator();
    m_toolbar->addAction(QStringLiteral("\""), this, &EditorPane::onBlockquote);
    m_toolbar->addAction(QStringLiteral("•"), this, &EditorPane::onBulletList);
    m_toolbar->addAction(QStringLiteral("1."), this, &EditorPane::onNumberedList);
    m_toolbar->addSeparator();
    m_toolbar->addAction(QStringLiteral("Link"), this, &EditorPane::onInsertLink);
    m_toolbar->addAction(QStringLiteral("HR"), this, &EditorPane::onInsertHorizontalRule);
    m_toolbar->addAction(QStringLiteral("</>"), this, &EditorPane::onCodeBlock);
}

QString EditorPane::currentHtml() const
{
    return Ao3HtmlExporter::exportDocument(m_editor->document());
}

void EditorPane::setHtml(const QString &html)
{
    m_blockChanges = true;
    Ao3HtmlImporter::importHtml(m_editor, html);
    // Re-apply the custom CSS stylesheet after importing new content
    if (!m_customCss.isEmpty())
        m_editor->document()->setDefaultStyleSheet(m_customCss);
    if (m_editor->document())
        m_editor->document()->markContentsDirty(0, m_editor->document()->characterCount());
    if (m_editor->viewport())
        m_editor->viewport()->update();
    m_blockChanges = false;
    runSpellAndGrammarChecks();
}

void EditorPane::focusEditor()
{
    m_editor->setFocus();
}

void EditorPane::setCustomCss(const QString &css)
{
    m_customCss = css;
    m_editor->document()->setDefaultStyleSheet(css);
    if (m_editor->document())
        m_editor->document()->markContentsDirty(0, m_editor->document()->characterCount());
    if (m_editor->viewport())
        m_editor->viewport()->update();
}

void EditorPane::setAvailableCssClasses(const QStringList &classes)
{
    m_availableCssClasses = classes;
}

void EditorPane::mergeCharFormat(const QTextCharFormat &format)
{
    QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);
    cursor.mergeCharFormat(format);
    m_editor->mergeCurrentCharFormat(format);
}

void EditorPane::mergeBlockFormat(const QTextBlockFormat &format)
{
    QTextCursor cursor = m_editor->textCursor();
    cursor.mergeBlockFormat(format);
    m_editor->setTextCursor(cursor);
}

void EditorPane::onBold()
{
    QTextCharFormat fmt;
    fmt.setFontWeight(m_editor->fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
    mergeCharFormat(fmt);
}

void EditorPane::onItalic()
{
    QTextCharFormat fmt;
    fmt.setFontItalic(!m_editor->fontItalic());
    mergeCharFormat(fmt);
}

void EditorPane::onUnderline()
{
    QTextCharFormat fmt;
    fmt.setFontUnderline(!m_editor->fontUnderline());
    mergeCharFormat(fmt);
}

void EditorPane::onStrikethrough()
{
    QTextCharFormat current = m_editor->currentCharFormat();
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(!current.fontStrikeOut());
    mergeCharFormat(fmt);
}

void EditorPane::onHeading(int level)
{
    QTextBlockFormat fmt;
    fmt.setHeadingLevel(level);
    mergeBlockFormat(fmt);
}

void EditorPane::onParagraph()
{
    QTextBlockFormat fmt;
    fmt.setHeadingLevel(0);
    fmt.setIndent(0);
    mergeBlockFormat(fmt);
}

void EditorPane::onBlockquote()
{
    QTextBlockFormat fmt;
    fmt.setProperty(QTextFormat::BlockQuoteLevel, 1);
    mergeBlockFormat(fmt);
}

void EditorPane::onBulletList()
{
    QTextCursor cursor = m_editor->textCursor();
    QTextListFormat listFmt;
    listFmt.setStyle(QTextListFormat::ListDisc);
    cursor.createList(listFmt);
}

void EditorPane::onNumberedList()
{
    QTextCursor cursor = m_editor->textCursor();
    QTextListFormat listFmt;
    listFmt.setStyle(QTextListFormat::ListDecimal);
    cursor.createList(listFmt);
}

void EditorPane::onInsertLink()
{
    const QString url = QInputDialog::getText(this, QStringLiteral("Insert Link"),
                                              QStringLiteral("URL:"), QLineEdit::Normal,
                                              QStringLiteral("https://"));
    if (url.isEmpty())
        return;

    QTextCharFormat fmt;
    fmt.setAnchor(true);
    fmt.setAnchorHref(url);
    fmt.setForeground(Qt::blue);
    fmt.setFontUnderline(true);
    mergeCharFormat(fmt);
}

void EditorPane::onInsertHorizontalRule()
{
    QTextCursor cursor = m_editor->textCursor();
    cursor.insertHtml(QStringLiteral("<hr />"));
}

void EditorPane::onCodeBlock()
{
    QTextBlockFormat fmt;
    fmt.setNonBreakableLines(true);
    mergeBlockFormat(fmt);

    QTextCharFormat charFmt;
    charFmt.setFontFamilies({QStringLiteral("monospace")});
    charFmt.setFontFixedPitch(true);
    mergeCharFormat(charFmt);
}

void EditorPane::onDocumentChanged()
{
    if (m_blockChanges)
        return;

    emit contentChanged();
    emit htmlExported(currentHtml());

    if (m_spellTimer)
        m_spellTimer->start();
}

void EditorPane::runSpellAndGrammarChecks()
{
    if (!m_editor || !m_spellChecker)
        return;

    const QString text = m_editor->toPlainText();
    const QList<CheckResult> results = m_spellChecker->checkText(text);

    QList<QTextEdit::ExtraSelection> extraSelections;

    for (const CheckResult &res : results) {
        QTextEdit::ExtraSelection sel;
        QTextCursor cursor(m_editor->document());
        cursor.setPosition(res.startPos);
        cursor.setPosition(res.startPos + res.length, QTextCursor::KeepAnchor);
        sel.cursor = cursor;

        if (res.type == CheckResult::SpellingError) {
            sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            sel.format.setUnderlineColor(QColor(255, 85, 85)); // Bright Red wave underline
            sel.format.setToolTip(res.message);
        } else {
            sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            sel.format.setUnderlineColor(QColor(128, 160, 255)); // Soft Blue wave underline
            sel.format.setToolTip(res.message);
        }
        extraSelections.append(sel);
    }

    m_editor->setExtraSelections(extraSelections);
}

void EditorPane::onApplyCssClass(const QString &className)
{
    QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);

    QTextCharFormat fmt;
    fmt.setProperty(CssClassProperty, className);
    cursor.mergeCharFormat(fmt);
    m_editor->setTextCursor(cursor);
}

void EditorPane::showEditorContextMenu(const QPoint &pos)
{
    QMenu *menu = m_editor->createStandardContextMenu();

    // Add CSS styles submenu if classes are available
    if (!m_availableCssClasses.isEmpty()) {
        menu->addSeparator();
        QMenu *stylesMenu = menu->addMenu(QStringLiteral("Apply Style"));

        // Currently applied classes on cursor selection
        QTextCursor cursor = m_editor->textCursor();
        QTextCharFormat curFmt = cursor.charFormat();
        QVariant prop = curFmt.property(CssClassProperty);
        QString currentClassesStr = prop.isValid() ? prop.toString() : QString();
        QStringList activeClasses = currentClassesStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);

        // "Clear All Styles" action
        QAction *clearAction = stylesMenu->addAction(QStringLiteral("Clear All Styles"));
        connect(clearAction, &QAction::triggered, this, [this]() {
            QTextCursor cursor = m_editor->textCursor();
            if (!cursor.hasSelection())
                cursor.select(QTextCursor::WordUnderCursor);
            QTextCharFormat fmt;
            fmt.setProperty(CssClassProperty, QVariant());
            fmt.setBackground(Qt::NoBrush);
            fmt.setForeground(Qt::NoBrush);
            fmt.setUnderlineStyle(QTextCharFormat::NoUnderline);
            cursor.mergeCharFormat(fmt);
            m_editor->setTextCursor(cursor);
        });

        stylesMenu->addSeparator();

        for (const QString &className : m_availableCssClasses) {
            QAction *action = stylesMenu->addAction(className);
            action->setCheckable(true);
            bool isApplied = activeClasses.contains(className);
            action->setChecked(isApplied);

            connect(action, &QAction::triggered, this, [this, className, isApplied]() {
                QTextCursor cursor = m_editor->textCursor();
                if (!cursor.hasSelection())
                    cursor.select(QTextCursor::WordUnderCursor);

                QTextCharFormat charFmt = cursor.charFormat();
                QString existingStr = charFmt.property(CssClassProperty).toString();
                QStringList classes = existingStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);

                if (isApplied) {
                    // Toggle OFF
                    classes.removeAll(className);
                } else {
                    // Toggle ON
                    if (!classes.contains(className))
                        classes.append(className);
                }

                QTextCharFormat newFmt;
                if (classes.isEmpty()) {
                    newFmt.setProperty(CssClassProperty, QVariant());
                    newFmt.setBackground(Qt::NoBrush);
                    newFmt.setForeground(Qt::NoBrush);
                    newFmt.setUnderlineStyle(QTextCharFormat::NoUnderline);
                } else {
                    newFmt.setProperty(CssClassProperty, classes.join(QLatin1Char(' ')));
                    newFmt.setBackground(QColor(40, 42, 54)); // #282a36 dark badge bg
                    newFmt.setForeground(QColor(80, 250, 123)); // #50fa7b emerald text
                    newFmt.setUnderlineStyle(QTextCharFormat::DashUnderline);
                    newFmt.setUnderlineColor(QColor(139, 233, 253)); // #8be9fd cyan dash
                }

                cursor.mergeCharFormat(newFmt);
                m_editor->setTextCursor(cursor);
            });
        }
    }

    menu->exec(m_editor->mapToGlobal(pos));
    delete menu;
}
