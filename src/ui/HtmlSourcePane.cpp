#include "HtmlSourcePane.h"
#include "CodeEditor.h"
#include "HtmlSyntaxHighlighter.h"
#include "EditorMinimap.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QXmlStreamReader>

HtmlSourcePane::HtmlSourcePane(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Linter warning label (hidden by default)
    m_linterWarning = new QLabel(this);
    m_linterWarning->setWordWrap(true);
    m_linterWarning->setStyleSheet(QStringLiteral(
        "QLabel { background: #3a2a00; color: #ffcc00; padding: 4px 8px; "
        "border-bottom: 1px solid #664d00; font-size: 11px; }"));
    m_linterWarning->hide();

    m_editor = new CodeEditor(this);
    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_editor->setFont(monoFont);
    m_editor->setPlaceholderText(QStringLiteral("AO3 HTML source…"));
    
    // Modern VSCode-like dark theme styling
    m_editor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  border: 1px solid #3c3c3c;"
        "  selection-background-color: #264f78;"
        "  selection-color: #ffffff;"
        "}"
    ));

    new HtmlSyntaxHighlighter(m_editor->document());
    m_editor->installEventFilter(this);

    // Minimap Scroll Preview sidebar
    auto *minimap = new EditorMinimap(this);
    minimap->setTargetEditor(m_editor);

    auto *editorRow = new QHBoxLayout();
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(0);
    editorRow->addWidget(m_editor, 1);
    editorRow->addWidget(minimap);

    m_applyButton = new QPushButton(QStringLiteral("Apply to Editor"), this);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    buttonRow->addWidget(m_applyButton);

    layout->addWidget(m_linterWarning);
    layout->addLayout(editorRow, 1);
    layout->addLayout(buttonRow);

    // Linter timer (debounced at 500ms)
    m_linterTimer = new QTimer(this);
    m_linterTimer->setSingleShot(true);
    m_linterTimer->setInterval(500);
    connect(m_linterTimer, &QTimer::timeout, this, &HtmlSourcePane::runLinter);

    connect(m_applyButton, &QPushButton::clicked, this, &HtmlSourcePane::onApplyClicked);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &HtmlSourcePane::onTextChanged);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (!m_editing) {
            m_editing = true;
            emit editingStarted();
        }
    });
}

QString HtmlSourcePane::html() const
{
    return m_editor->toPlainText();
}

void HtmlSourcePane::setHtml(const QString &html)
{
    if (m_editing)
        return;

    m_editor->blockSignals(true);
    m_editor->setPlainText(html);
    m_editor->blockSignals(false);
}

void HtmlSourcePane::onApplyClicked()
{
    forceApply();
}

void HtmlSourcePane::forceApply()
{
    m_editing = false;
    emit editingFinished();
    emit applyRequested(m_editor->toPlainText());
}

void HtmlSourcePane::onTextChanged()
{
    if (m_blockChanges)
        return;

    if (!m_editing) {
        m_editing = true;
        emit editingStarted();
    }

    emit htmlChanged(m_editor->toPlainText());
    m_linterTimer->start();
}

// --- Autoblock (Emmet-like expansion) ---

bool HtmlSourcePane::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_editor && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier) {
            if (tryExpandAutoblock())
                return true; // consumed
        }
    }
    return QWidget::eventFilter(obj, event);
}

#include <QDebug>

bool HtmlSourcePane::tryExpandAutoblock()
{
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection())
        return false;

    // Get text from the start of the current line to the cursor
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    const QString lineText = cursor.selectedText().trimmed();
    qDebug() << "tryExpandAutoblock: lineText = " << lineText;

    // Match patterns like: tag.class1.class2  or  tag.class1
    // Examples: div.flashback, span.ao3-red.bold, p.center
    static const QRegularExpression autoblockRe(
        R"(^([a-zA-Z][a-zA-Z0-9]*)(\.[a-zA-Z_-][a-zA-Z0-9_-]*)(\.[a-zA-Z_-][a-zA-Z0-9_-]*)*$)");

    const QRegularExpressionMatch match = autoblockRe.match(lineText);
    qDebug() << "tryExpandAutoblock: match.hasMatch() = " << match.hasMatch();
    if (!match.hasMatch())
        return false;

    const QString tagName = match.captured(1);

    // Extract all classes from the dotted notation
    QStringList classes;
    static const QRegularExpression classParts(R"(\.([a-zA-Z_-][a-zA-Z0-9_-]*))");
    QRegularExpressionMatchIterator it = classParts.globalMatch(lineText);
    while (it.hasNext())
        classes << it.next().captured(1);

    const QString classAttr = classes.isEmpty()
        ? QString()
        : QStringLiteral(" class=\"%1\"").arg(classes.join(QStringLiteral(" ")));

    // Void elements don't get closing tags
    static const QSet<QString> voidTags = {
        QStringLiteral("br"), QStringLiteral("hr"), QStringLiteral("img"),
        QStringLiteral("input"), QStringLiteral("col"),
    };

    QString expansion;
    if (voidTags.contains(tagName.toLower())) {
        expansion = QStringLiteral("<%1%2 />").arg(tagName, classAttr);
    } else {
        expansion = QStringLiteral("<%1%2>\n\n</%1>").arg(tagName, classAttr);
    }

    // Replace the abbreviation text with the expansion
    cursor.insertText(expansion);

    // Position cursor between the opening and closing tags (on the blank line)
    if (!voidTags.contains(tagName.toLower())) {
        cursor.movePosition(QTextCursor::Up, QTextCursor::MoveAnchor, 1);
        cursor.movePosition(QTextCursor::EndOfBlock);
    }
    m_editor->setTextCursor(cursor);

    return true;
}

// --- Linter ---

void HtmlSourcePane::runLinter()
{
    const QString text = m_editor->toPlainText();
    if (text.trimmed().isEmpty()) {
        m_linterWarning->hide();
        return;
    }

    QStringList warnings;

    // Check 1: Inline style attributes (not allowed on AO3)
    static const QRegularExpression styleAttrRe(
        R"(\bstyle\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))",
        QRegularExpression::CaseInsensitiveOption);
    if (text.contains(styleAttrRe)) {
        warnings << QStringLiteral("⚠ Inline style= attributes detected (not allowed on AO3).");
    }

    // Check 2: Mismatched tags using QXmlStreamReader
    // Wrap in a root element so it's valid XML
    const QString wrapped = QStringLiteral("<root>%1</root>").arg(text);
    QXmlStreamReader reader(wrapped);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError()) {
            // Try to give a useful message
            const QString err = reader.errorString();
            const qint64 line = reader.lineNumber() - 1; // subtract 1 for <root>
            warnings << QStringLiteral("⚠ HTML parse error near line %1: %2").arg(line).arg(err);
            break;
        }
    }

    if (warnings.isEmpty()) {
        m_linterWarning->hide();
    } else {
        m_linterWarning->setText(warnings.join(QStringLiteral("  |  ")));
        m_linterWarning->show();
    }
}
