#include "EditorPane.h"
#include "ImageEmbedDialog.h"
#include "ToolbarIcons.h"

#include "export/Ao3HtmlExporter.h"
#include "export/Ao3HtmlImporter.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QFont>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFormat>
#include <QTextFragment>
#include <QTextList>
#include <QTextListFormat>
#include <QToolTip>
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

    // Install event filter on viewport for tooltips
    m_editor->viewport()->installEventFilter(this);
    m_editor->viewport()->setMouseTracking(true);

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
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &EditorPane::updateToolbarActiveStates);
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this, &EditorPane::updateToolbarActiveStates);

    updateTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateTheme();
    });
    connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this, [this](SkinFlavor) {
        updateTheme();
    });
}

void EditorPane::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(20, 20));

    m_boldAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onBold);
    m_boldAction->setToolTip(QStringLiteral("Bold (Ctrl+B)"));
    m_boldAction->setShortcut(QKeySequence::Bold);
    m_boldAction->setCheckable(true);

    m_italicAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onItalic);
    m_italicAction->setToolTip(QStringLiteral("Italic (Ctrl+I)"));
    m_italicAction->setShortcut(QKeySequence::Italic);
    m_italicAction->setCheckable(true);

    m_underlineAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onUnderline);
    m_underlineAction->setToolTip(QStringLiteral("Underline (Ctrl+U)"));
    m_underlineAction->setShortcut(QKeySequence::Underline);
    m_underlineAction->setCheckable(true);

    m_strikeAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onStrikethrough);
    m_strikeAction->setToolTip(QStringLiteral("Strikethrough (<s>)"));
    m_strikeAction->setCheckable(true);

    m_toolbar->addSeparator();

    m_h1Action = m_toolbar->addAction(QStringLiteral(""), this, [this]() { onHeading(1); });
    m_h1Action->setToolTip(QStringLiteral("Heading 1 (<h1>)"));
    m_h1Action->setCheckable(true);

    m_h2Action = m_toolbar->addAction(QStringLiteral(""), this, [this]() { onHeading(2); });
    m_h2Action->setToolTip(QStringLiteral("Heading 2 (<h2>)"));
    m_h2Action->setCheckable(true);

    m_h3Action = m_toolbar->addAction(QStringLiteral(""), this, [this]() { onHeading(3); });
    m_h3Action->setToolTip(QStringLiteral("Heading 3 (<h3>)"));
    m_h3Action->setCheckable(true);

    m_paragraphAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onParagraph);
    m_paragraphAction->setToolTip(QStringLiteral("Paragraph (<p>)"));
    m_paragraphAction->setCheckable(true);

    m_toolbar->addSeparator();

    m_blockquoteAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onBlockquote);
    m_blockquoteAction->setToolTip(QStringLiteral("Blockquote (<blockquote>)"));
    m_blockquoteAction->setCheckable(true);

    m_bulletListAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onBulletList);
    m_bulletListAction->setToolTip(QStringLiteral("Bullet List (<ul>)"));
    m_bulletListAction->setCheckable(true);

    m_numberedListAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onNumberedList);
    m_numberedListAction->setToolTip(QStringLiteral("Numbered List (<ol>)"));
    m_numberedListAction->setCheckable(true);

    m_toolbar->addSeparator();

    m_linkAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onInsertLink);
    m_linkAction->setToolTip(QStringLiteral("Insert Link (<a>)"));
    m_linkAction->setCheckable(true);

    m_imageAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onInsertImageEmbed);
    m_imageAction->setToolTip(QStringLiteral("Insert Image Embed (AO3 URL Attachment) (Ctrl+Shift+M)"));
    m_imageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));

    m_hrAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onInsertHorizontalRule);
    m_hrAction->setToolTip(QStringLiteral("Horizontal Rule (<hr>)"));

    m_codeAction = m_toolbar->addAction(QStringLiteral(""), this, &EditorPane::onCodeBlock);
    m_codeAction->setToolTip(QStringLiteral("Code Block (<pre><code>)"));
    m_codeAction->setCheckable(true);

    updateToolbarIcons();
}

void EditorPane::updateToolbarIcons()
{
    const bool isDark = ThemeManager::instance().isDarkMode();
    const QColor normalColor = isDark ? QColor(QStringLiteral("#cbd5e1")) : QColor(QStringLiteral("#334155"));
    const QColor activeColor = QColor(QStringLiteral("#ffffff"));

    if (m_boldAction) m_boldAction->setIcon(ToolbarIcons::bold(normalColor, activeColor));
    if (m_italicAction) m_italicAction->setIcon(ToolbarIcons::italic(normalColor, activeColor));
    if (m_underlineAction) m_underlineAction->setIcon(ToolbarIcons::underline(normalColor, activeColor));
    if (m_strikeAction) m_strikeAction->setIcon(ToolbarIcons::strikethrough(normalColor, activeColor));

    if (m_h1Action) m_h1Action->setIcon(ToolbarIcons::heading1(normalColor, activeColor));
    if (m_h2Action) m_h2Action->setIcon(ToolbarIcons::heading2(normalColor, activeColor));
    if (m_h3Action) m_h3Action->setIcon(ToolbarIcons::heading3(normalColor, activeColor));
    if (m_paragraphAction) m_paragraphAction->setIcon(ToolbarIcons::paragraph(normalColor, activeColor));

    if (m_blockquoteAction) m_blockquoteAction->setIcon(ToolbarIcons::blockquote(normalColor, activeColor));
    if (m_bulletListAction) m_bulletListAction->setIcon(ToolbarIcons::bulletList(normalColor, activeColor));
    if (m_numberedListAction) m_numberedListAction->setIcon(ToolbarIcons::numberedList(normalColor, activeColor));

    if (m_linkAction) m_linkAction->setIcon(ToolbarIcons::link(normalColor, activeColor));
    if (m_imageAction) m_imageAction->setIcon(ToolbarIcons::image(normalColor, activeColor));
    if (m_hrAction) m_hrAction->setIcon(ToolbarIcons::horizontalRule(normalColor, activeColor));
    if (m_codeAction) m_codeAction->setIcon(ToolbarIcons::code(normalColor, activeColor));
}

void EditorPane::updateToolbarActiveStates()
{
    if (!m_editor) return;

    QTextCursor cursor = m_editor->textCursor();
    QTextCharFormat charFmt = cursor.charFormat();
    QTextBlockFormat blockFmt = cursor.blockFormat();

    const bool isBold = (charFmt.fontWeight() == QFont::Bold || m_editor->fontWeight() == QFont::Bold);
    if (m_boldAction && m_boldAction->isChecked() != isBold)
        m_boldAction->setChecked(isBold);

    const bool isItalic = (charFmt.fontItalic() || m_editor->fontItalic());
    if (m_italicAction && m_italicAction->isChecked() != isItalic)
        m_italicAction->setChecked(isItalic);

    const bool isUnderline = (charFmt.fontUnderline() || m_editor->fontUnderline());
    if (m_underlineAction && m_underlineAction->isChecked() != isUnderline)
        m_underlineAction->setChecked(isUnderline);

    const bool isStrike = charFmt.fontStrikeOut();
    if (m_strikeAction && m_strikeAction->isChecked() != isStrike)
        m_strikeAction->setChecked(isStrike);

    const int h = blockFmt.headingLevel();
    if (m_h1Action && m_h1Action->isChecked() != (h == 1))
        m_h1Action->setChecked(h == 1);
    if (m_h2Action && m_h2Action->isChecked() != (h == 2))
        m_h2Action->setChecked(h == 2);
    if (m_h3Action && m_h3Action->isChecked() != (h == 3))
        m_h3Action->setChecked(h == 3);
    if (m_paragraphAction && m_paragraphAction->isChecked() != (h == 0 && blockFmt.property(QTextFormat::BlockQuoteLevel).toInt() == 0))
        m_paragraphAction->setChecked(h == 0 && blockFmt.property(QTextFormat::BlockQuoteLevel).toInt() == 0);

    const bool isLink = charFmt.isAnchor();
    if (m_linkAction && m_linkAction->isChecked() != isLink)
        m_linkAction->setChecked(isLink);

    const bool isQuote = blockFmt.property(QTextFormat::BlockQuoteLevel).toInt() > 0;
    if (m_blockquoteAction && m_blockquoteAction->isChecked() != isQuote)
        m_blockquoteAction->setChecked(isQuote);

    QTextList *list = cursor.currentList();
    const bool isBullet = (list && list->format().style() == QTextListFormat::ListDisc);
    const bool isNumbered = (list && list->format().style() == QTextListFormat::ListDecimal);
    if (m_bulletListAction && m_bulletListAction->isChecked() != isBullet)
        m_bulletListAction->setChecked(isBullet);
    if (m_numberedListAction && m_numberedListAction->isChecked() != isNumbered)
        m_numberedListAction->setChecked(isNumbered);
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
    fetchRemoteImagesInDocument();
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
    runSpellAndGrammarChecks();
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
    updateToolbarActiveStates();
}

void EditorPane::onItalic()
{
    QTextCharFormat fmt;
    fmt.setFontItalic(!m_editor->fontItalic());
    mergeCharFormat(fmt);
    updateToolbarActiveStates();
}

void EditorPane::onUnderline()
{
    QTextCharFormat fmt;
    fmt.setFontUnderline(!m_editor->fontUnderline());
    mergeCharFormat(fmt);
    updateToolbarActiveStates();
}

void EditorPane::onStrikethrough()
{
    QTextCharFormat current = m_editor->currentCharFormat();
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(!current.fontStrikeOut());
    mergeCharFormat(fmt);
    updateToolbarActiveStates();
}

void EditorPane::onHeading(int level)
{
    QTextCursor cursor = m_editor->textCursor();
    QTextBlockFormat fmt;
    if (cursor.blockFormat().headingLevel() == level) {
        fmt.setHeadingLevel(0);
    } else {
        fmt.setHeadingLevel(level);
    }
    mergeBlockFormat(fmt);
    updateToolbarActiveStates();
}

void EditorPane::onParagraph()
{
    QTextBlockFormat fmt;
    fmt.setHeadingLevel(0);
    fmt.setIndent(0);
    fmt.setProperty(QTextFormat::BlockQuoteLevel, 0);
    mergeBlockFormat(fmt);
    updateToolbarActiveStates();
}

void EditorPane::onBlockquote()
{
    QTextCursor cursor = m_editor->textCursor();
    QTextBlockFormat fmt;
    if (cursor.blockFormat().property(QTextFormat::BlockQuoteLevel).toInt() > 0) {
        fmt.setProperty(QTextFormat::BlockQuoteLevel, 0);
        fmt.setIndent(0);
    } else {
        fmt.setProperty(QTextFormat::BlockQuoteLevel, 1);
        fmt.setIndent(1);
    }
    mergeBlockFormat(fmt);
    updateToolbarActiveStates();
}

void EditorPane::onBulletList()
{
    QTextCursor cursor = m_editor->textCursor();
    QTextListFormat listFmt;
    listFmt.setStyle(QTextListFormat::ListDisc);
    cursor.createList(listFmt);
    updateToolbarActiveStates();
}

void EditorPane::onNumberedList()
{
    QTextCursor cursor = m_editor->textCursor();
    QTextListFormat listFmt;
    listFmt.setStyle(QTextListFormat::ListDecimal);
    cursor.createList(listFmt);
    updateToolbarActiveStates();
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

void EditorPane::onInsertImageEmbed()
{
    ImageEmbedDialog dlg(this);

    // If cursor is on an existing image, pre-populate dialog
    QTextCursor cursor = m_editor->textCursor();
    QTextCharFormat charFmt = cursor.charFormat();
    if (charFmt.isImageFormat()) {
        const QTextImageFormat imgFmt = charFmt.toImageFormat();
        dlg.setImageUrl(imgFmt.name());
        dlg.setAltText(imgFmt.property(QTextFormat::ImageAltText).toString());
        dlg.setTitleText(imgFmt.property(QTextFormat::ImageTitle).toString());
        if (imgFmt.hasProperty(QTextFormat::ImageWidth))
            dlg.setImageWidth(static_cast<int>(imgFmt.width()));
        if (imgFmt.hasProperty(QTextFormat::ImageHeight))
            dlg.setImageHeight(static_cast<int>(imgFmt.height()));
        if (charFmt.isAnchor())
            dlg.setWrapInLink(true);
    }

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString url = dlg.imageUrl();
    if (url.isEmpty())
        return;

    QTextImageFormat imgFmt;
    imgFmt.setName(url);
    if (!dlg.altText().isEmpty()) {
        imgFmt.setProperty(QTextFormat::ImageAltText, dlg.altText());
    }
    if (!dlg.titleText().isEmpty()) {
        imgFmt.setProperty(QTextFormat::ImageTitle, dlg.titleText());
    }
    if (dlg.imageWidth() > 0) {
        imgFmt.setWidth(dlg.imageWidth());
    }
    if (dlg.imageHeight() > 0) {
        imgFmt.setHeight(dlg.imageHeight());
    }
    if (dlg.wrapInLink()) {
        imgFmt.setAnchor(true);
        imgFmt.setAnchorHref(url);
    }

    // Cache the downloaded preview pixmap into the document if available
    const QPixmap preview = dlg.previewPixmap();
    if (!preview.isNull()) {
        m_editor->document()->addResource(QTextDocument::ImageResource, QUrl(url), preview);
    }

    cursor.beginEditBlock();
    const QString align = dlg.alignment();
    if (align == QStringLiteral("center") || align == QStringLiteral("left") || align == QStringLiteral("right")) {
        Qt::Alignment blockAlign = Qt::AlignCenter;
        if (align == QStringLiteral("left")) blockAlign = Qt::AlignLeft;
        else if (align == QStringLiteral("right")) blockAlign = Qt::AlignRight;

        if (!cursor.block().text().trimmed().isEmpty()) {
            cursor.insertBlock();
        }
        QTextBlockFormat blockFmt = cursor.blockFormat();
        blockFmt.setAlignment(blockAlign);
        cursor.setBlockFormat(blockFmt);
        cursor.insertImage(imgFmt);
        cursor.insertBlock();
        QTextBlockFormat normalFmt;
        cursor.setBlockFormat(normalFmt);
    } else {
        cursor.insertImage(imgFmt);
    }
    cursor.endEditBlock();

    if (preview.isNull()) {
        fetchRemoteImagesInDocument();
    }
}

void EditorPane::fetchRemoteImagesInDocument()
{
    if (!m_editor || !m_editor->document())
        return;

    if (!m_imageNam) {
        m_imageNam = new QNetworkAccessManager(this);
    }

    QTextDocument *doc = m_editor->document();
    QSet<QString> urlsToFetch;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto fragIt = block.begin(); !fragIt.atEnd(); ++fragIt) {
            const QTextFragment frag = fragIt.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat fmt = frag.charFormat();
            if (fmt.isImageFormat()) {
                const QString src = fmt.toImageFormat().name();
                if ((src.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                     src.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) &&
                    !doc->resource(QTextDocument::ImageResource, QUrl(src)).isValid()) {
                    urlsToFetch.insert(src);
                }
            }
        }
    }

    for (const QString &urlStr : urlsToFetch) {
        const QUrl qurl(urlStr);
        QNetworkRequest req(qurl);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EnsembleAO3Typewriter/1.0 (Editor Image Loader)"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_imageNam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, qurl]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                const QByteArray bytes = reply->readAll();
                QPixmap pixmap;
                if (pixmap.loadFromData(bytes) && !pixmap.isNull()) {
                    if (m_editor && m_editor->document()) {
                        m_editor->document()->addResource(QTextDocument::ImageResource, qurl, pixmap);
                        m_editor->document()->markContentsDirty(0, m_editor->document()->characterCount());
                        if (m_editor->viewport()) {
                            m_editor->viewport()->update();
                        }
                    }
                }
            }
        });
    }
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
    if (!m_editor)
        return;

    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    const bool highlightErrors = settings.value(QStringLiteral("editor/highlightErrors"), true).toBool();

    QList<QTextEdit::ExtraSelection> extraSelections;

    if (highlightErrors) {
        // 1. Spell & Grammar checking
        if (m_spellChecker) {
            const QString text = m_editor->toPlainText();
            const QList<CheckResult> results = m_spellChecker->checkText(text);

            for (const CheckResult &res : results) {
                QTextEdit::ExtraSelection sel;
                QTextCursor cursor(m_editor->document());
                cursor.setPosition(res.startPos);
                cursor.setPosition(res.startPos + res.length, QTextCursor::KeepAnchor);
                sel.cursor = cursor;

                sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
                if (res.type == CheckResult::SpellingError) {
                    sel.format.setUnderlineColor(QColor(255, 85, 85)); // Bright Red wave underline
                } else {
                    sel.format.setUnderlineColor(QColor(128, 160, 255)); // Soft Blue wave underline
                }
                sel.format.setToolTip(res.message);
                extraSelections.append(sel);
            }
        }

        // 2. CSS undefined class checking
        if (!m_availableCssClasses.isEmpty()) {
            QTextDocument *doc = m_editor->document();
            for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    QTextFragment frag = it.fragment();
                    if (!frag.isValid())
                        continue;
                    QTextCharFormat fmt = frag.charFormat();
                    QVariant cssProp = fmt.property(CssClassProperty);
                    if (cssProp.isValid()) {
                        QString classStr = cssProp.toString().trimmed();
                        if (!classStr.isEmpty()) {
                            QStringList appliedClasses = classStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                            QStringList undefinedClasses;
                            for (const QString &cls : appliedClasses) {
                                if (!m_availableCssClasses.contains(cls)) {
                                    undefinedClasses.append(cls);
                                }
                            }
                            if (!undefinedClasses.isEmpty()) {
                                QTextEdit::ExtraSelection sel;
                                QTextCursor cursor(doc);
                                cursor.setPosition(frag.position());
                                cursor.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
                                sel.cursor = cursor;
                                sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
                                sel.format.setUnderlineColor(QColor(255, 121, 198)); // Magenta / Pink-red wave underline
                                sel.format.setToolTip(QStringLiteral("Undefined CSS class: %1").arg(undefinedClasses.join(QLatin1Char(' '))));
                                extraSelections.append(sel);
                            }
                        }
                    }
                }
            }
        }
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
        stylesMenu->setStyleSheet(QStringLiteral("QMenu { max-height: 420px; }"));

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

        // Helper lambda to add a class action to any target QMenu
        auto addClassAction = [this, &activeClasses](QMenu *targetMenu, const QString &className) {
            QAction *action = targetMenu->addAction(className);
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
                    classes.removeAll(className);
                } else {
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
                    newFmt.setBackground(QColor(40, 42, 54));   // #282a36 dark badge bg
                    newFmt.setForeground(QColor(80, 250, 123)); // #50fa7b emerald text
                    newFmt.setUnderlineStyle(QTextCharFormat::DashUnderline);
                    newFmt.setUnderlineColor(QColor(139, 233, 253)); // #8be9fd cyan dash
                }

                cursor.mergeCharFormat(newFmt);
                m_editor->setTextCursor(cursor);
            });
        };

        // If there are many classes (> 12), organize into prefix submenus (e.g. hsr-, doc-, news-)
        if (m_availableCssClasses.size() > 12) {
            QMap<QString, QStringList> prefixMap;
            QStringList standaloneClasses;

            for (const QString &className : m_availableCssClasses) {
                int dashIdx = className.indexOf(QLatin1Char('-'));
                int underscoreIdx = className.indexOf(QLatin1Char('_'));
                int splitIdx = -1;
                if (dashIdx != -1 && underscoreIdx != -1)
                    splitIdx = qMin(dashIdx, underscoreIdx);
                else if (dashIdx != -1)
                    splitIdx = dashIdx;
                else if (underscoreIdx != -1)
                    splitIdx = underscoreIdx;

                if (splitIdx > 0) {
                    const QString prefix = className.left(splitIdx);
                    prefixMap[prefix].append(className);
                } else {
                    standaloneClasses.append(className);
                }
            }

            QMap<QString, QMenu *> categoryMenus;
            for (auto it = prefixMap.constBegin(); it != prefixMap.constEnd(); ++it) {
                if (it.value().size() >= 2) {
                    QMenu *catMenu = stylesMenu->addMenu(QStringLiteral("%1...").arg(it.key()));
                    catMenu->setStyleSheet(QStringLiteral("QMenu { max-height: 420px; }"));
                    categoryMenus[it.key()] = catMenu;
                    for (const QString &cls : it.value()) {
                        addClassAction(catMenu, cls);
                    }
                } else {
                    for (const QString &cls : it.value()) {
                        standaloneClasses.append(cls);
                    }
                }
            }

            if (!categoryMenus.isEmpty() && !standaloneClasses.isEmpty()) {
                stylesMenu->addSeparator();
            }

            for (const QString &className : standaloneClasses) {
                addClassAction(stylesMenu, className);
            }
        } else {
            for (const QString &className : m_availableCssClasses) {
                addClassAction(stylesMenu, className);
            }
        }
    }

    menu->addSeparator();
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.charFormat().isImageFormat()) {
        menu->addAction(QStringLiteral("🖼️ Edit Image Embed…"), this, &EditorPane::onInsertImageEmbed);
    } else {
        menu->addAction(QStringLiteral("🖼️ Insert Image Embed…"), this, &EditorPane::onInsertImageEmbed);
    }

    menu->exec(m_editor->mapToGlobal(pos));
    delete menu;
}

void EditorPane::updateTheme()
{
    if (m_editor) {
        m_editor->setStyleSheet(ThemeManager::instance().getEditorPaneStyleSheet());
    }
    updateToolbarIcons();
    updateToolbarActiveStates();
}

void EditorPane::updateSettings()
{
    runSpellAndGrammarChecks();
}

bool EditorPane::eventFilter(QObject *obj, QEvent *event)
{
    if (m_editor && obj == m_editor->viewport() && event->type() == QEvent::ToolTip) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        handleEditorTooltip(helpEvent);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void EditorPane::handleEditorTooltip(QHelpEvent *helpEvent)
{
    if (!m_editor || !helpEvent)
        return;

    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    const bool showElementInfo = settings.value(QStringLiteral("editor/hoverElementInfo"), true).toBool();
    const bool showErrorDetails = settings.value(QStringLiteral("editor/hoverErrorDetails"), true).toBool();

    if (!showElementInfo && !showErrorDetails) {
        QToolTip::hideText();
        return;
    }

    const QPoint pos = helpEvent->pos();
    const QTextCursor cursor = m_editor->cursorForPosition(pos);
    const int charPos = cursor.position();

    QTextDocument *doc = m_editor->document();
    if (!doc || charPos < 0 || charPos >= doc->characterCount()) {
        QToolTip::hideText();
        return;
    }

    QTextBlock block = doc->findBlock(charPos);
    if (!block.isValid()) {
        QToolTip::hideText();
        return;
    }

    // Find the fragment covering charPos
    QTextCharFormat charFmt;
    bool foundFrag = false;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        QTextFragment frag = it.fragment();
        if (frag.isValid()) {
            if (charPos >= frag.position() && charPos < frag.position() + frag.length()) {
                charFmt = frag.charFormat();
                foundFrag = true;
                break;
            }
        }
    }
    if (!foundFrag && charPos > 0) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (frag.isValid() && (charPos - 1) >= frag.position() && (charPos - 1) < frag.position() + frag.length()) {
                charFmt = frag.charFormat();
                foundFrag = true;
                break;
            }
        }
    }
    if (!foundFrag) {
        charFmt = cursor.charFormat();
    }

    QStringList issues;
    QStringList elements;

    // 1. Block-level container
    QTextBlockFormat blockFmt = block.blockFormat();
    if (blockFmt.headingLevel() > 0) {
        elements.append(QStringLiteral("<h%1>").arg(blockFmt.headingLevel()));
    } else if (blockFmt.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
        elements.append(QStringLiteral("<blockquote>"));
    } else if (block.textList()) {
        QTextListFormat listFmt = block.textList()->format();
        if (listFmt.style() == QTextListFormat::ListDecimal) {
            elements.append(QStringLiteral("<ol>"));
            elements.append(QStringLiteral("<li>"));
        } else {
            elements.append(QStringLiteral("<ul>"));
            elements.append(QStringLiteral("<li>"));
        }
    } else {
        elements.append(QStringLiteral("<p>"));
    }

    // 2. CSS class attribute
    QVariant cssProp = charFmt.property(CssClassProperty);
    QString classStr = cssProp.isValid() ? cssProp.toString().trimmed() : QString();
    if (!classStr.isEmpty()) {
        QStringList appliedClasses = classStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &cls : appliedClasses) {
            elements.append(QStringLiteral("<span class=\"%1\">").arg(cls));
            if (!m_availableCssClasses.contains(cls)) {
                issues.append(QStringLiteral("Undefined CSS class \".%1\" — not declared in Work Skin CSS.")
                                  .arg(cls));
            }
        }
    }

    // 3. Inline tags / styling
    if (charFmt.isImageFormat()) {
        const QTextImageFormat imgFmt = charFmt.toImageFormat();
        const QString alt = imgFmt.property(QTextFormat::ImageAltText).toString();
        if (!alt.isEmpty()) {
            elements.append(QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(imgFmt.name(), alt));
        } else {
            elements.append(QStringLiteral("<img src=\"%1\">").arg(imgFmt.name()));
        }
    }
    if (charFmt.isAnchor() && !charFmt.anchorHref().isEmpty()) {
        elements.append(QStringLiteral("<a href=\"%1\">").arg(charFmt.anchorHref()));
    }
    if (charFmt.fontWeight() >= QFont::Bold) {
        elements.append(QStringLiteral("<strong>"));
    }
    if (charFmt.fontItalic()) {
        elements.append(QStringLiteral("<em>"));
    }
    if (charFmt.fontUnderline()) {
        elements.append(QStringLiteral("<u>"));
    }
    if (charFmt.fontStrikeOut()) {
        elements.append(QStringLiteral("<s>"));
    }
    const QStringList families = charFmt.fontFamilies().toStringList();
    if (families.join(QString()).contains(QStringLiteral("monospace"), Qt::CaseInsensitive) || charFmt.fontFixedPitch()) {
        elements.append(QStringLiteral("<code>"));
    }

    // 4. Spelling / Grammar errors at this character position
    if (showErrorDetails && m_spellChecker) {
        const QList<CheckResult> checkResults = m_spellChecker->checkText(m_editor->toPlainText());
        for (const CheckResult &res : checkResults) {
            if (charPos >= res.startPos && charPos <= (res.startPos + res.length)) {
                issues.append(res.message);
            }
        }
    }

    if ((!showErrorDetails || issues.isEmpty()) && (!showElementInfo || elements.isEmpty())) {
        QToolTip::hideText();
        return;
    }

    QString tooltipHtml;
    if (showErrorDetails && !issues.isEmpty()) {
        tooltipHtml += QStringLiteral(
            "<div style='font-family: sans-serif; font-size: 11px; max-width: 380px;'>"
            "<b style='color: #ef4444;'>❌ Issue%1:</b><br/>"
        ).arg(issues.size() > 1 ? QStringLiteral("s") : QString());
        for (const QString &issue : issues) {
            tooltipHtml += QStringLiteral("<div style='margin-left: 6px; margin-top: 2px;'>• %1</div>")
                               .arg(issue.toHtmlEscaped());
        }
        tooltipHtml += QStringLiteral("</div>");
    }

    if (showElementInfo && !elements.isEmpty()) {
        if (!tooltipHtml.isEmpty()) {
            tooltipHtml += QStringLiteral("<hr style='border: none; border-top: 1px solid #4b5563; margin: 6px 0;'/>");
        }
        tooltipHtml += QStringLiteral(
            "<div style='font-family: sans-serif; font-size: 11px;'>"
            "<b style='color: #38bdf8;'>Embedded Elements:</b><br/>"
            "<code style='font-family: monospace; font-size: 11px; color: #a7f3d0;'>%1</code>"
            "</div>"
        ).arg(elements.join(QStringLiteral(" → ")).toHtmlEscaped());
    }

    if (!tooltipHtml.isEmpty()) {
        QToolTip::showText(helpEvent->globalPos(), tooltipHtml, m_editor->viewport());
    } else {
        QToolTip::hideText();
    }
}

