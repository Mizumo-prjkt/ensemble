#include "CodeEditor.h"
#include "themes/ThemeManager.h"

#include <QEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QTextBlock>
#include <QToolTip>

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor)
        , codeEditor(editor)
    {
    }

    QSize sizeHint() const override
    {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *codeEditor = nullptr;
};

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    lineNumberArea = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        highlightCurrentLine();
        if (lineNumberArea) lineNumberArea->update();
    });

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    // space for digits + padding
    int space = 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::setDiagnosticIssues(const QList<CodeDiagnosticIssue> &issues)
{
    m_diagnosticIssues = issues;
    updateExtraSelections();
}

void CodeEditor::clearDiagnosticIssues()
{
    m_diagnosticIssues.clear();
    updateExtraSelections();
}

void CodeEditor::refreshHighlights()
{
    updateExtraSelections();
}

void CodeEditor::highlightCurrentLine()
{
    updateExtraSelections();
}

void CodeEditor::updateExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    // 1. Current line highlight
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        const bool isDark = ThemeManager::instance().isDarkMode();
        const QColor lineColor = isDark ? QColor(QStringLiteral("#21222c")) : QColor(QStringLiteral("#f1f5f9"));

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }

    // 2. Diagnostic issues highlight
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    const bool highlightEnabled = settings.value(QStringLiteral("editor/highlightErrors"), true).toBool();

    if (highlightEnabled) {
        for (const auto &issue : m_diagnosticIssues) {
            if (issue.startPos >= 0 && issue.length > 0 &&
                (issue.startPos + issue.length) <= document()->characterCount()) {
                QTextEdit::ExtraSelection sel;
                QTextCursor cur(document());
                cur.setPosition(issue.startPos);
                cur.setPosition(issue.startPos + issue.length, QTextCursor::KeepAnchor);
                sel.cursor = cur;

                const QColor underlineColor = issue.isError ? QColor(QStringLiteral("#ef4444"))
                                                            : QColor(QStringLiteral("#f59e0b"));
                sel.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
                sel.format.setUnderlineColor(underlineColor);

                if (issue.isError) {
                    sel.format.setBackground(QColor(239, 68, 68, 35));
                } else {
                    sel.format.setBackground(QColor(245, 158, 11, 28));
                }

                selections.append(sel);
            }
        }
    }

    setExtraSelections(selections);
}

bool CodeEditor::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
        const bool hoverElementEnabled = settings.value(QStringLiteral("editor/hoverElementInfo"), true).toBool();
        const bool hoverErrorEnabled = settings.value(QStringLiteral("editor/hoverErrorDetails"), true).toBool();

        QTextCursor cursor = cursorForPosition(helpEvent->pos());
        const int pos = cursor.position();

        // 1. Check if hovering over any diagnostic issue
        if (hoverErrorEnabled && !m_diagnosticIssues.isEmpty()) {
            for (const auto &issue : m_diagnosticIssues) {
                if (pos >= issue.startPos && pos <= (issue.startPos + issue.length)) {
                    const QString icon = issue.isError ? QStringLiteral("❌") : QStringLiteral("⚠️");
                    const QString color = issue.isError ? QStringLiteral("#ef4444") : QStringLiteral("#f59e0b");
                    const QString title = issue.isError ? QStringLiteral("Error") : QStringLiteral("Warning");
                    const QString tip = QStringLiteral(
                        "<div style='font-family: sans-serif; font-size: 11px; max-width: 380px;'>"
                        "<b style='color:%1;'>%2 %3:</b> %4"
                        "</div>"
                    ).arg(color, icon, title, issue.message);
                    QToolTip::showText(helpEvent->globalPos(), tip, viewport());
                    return true;
                }
            }
        }

        // 2. Check if hovering over element / tag
        if (hoverElementEnabled) {
            QString tip;
            if (m_syntaxMode == SyntaxMode::Html) {
                QTextBlock block = cursor.block();
                const QString text = block.text();
                const int col = cursor.positionInBlock();

                // Look for enclosing HTML tag around mouse column
                int tagStart = text.lastIndexOf(QLatin1Char('<'), col);
                int tagEnd = text.indexOf(QLatin1Char('>'), col);
                if (tagStart != -1 && tagEnd != -1 && tagEnd >= col && (tagEnd - tagStart) < 300) {
                    const QString fullTag = text.mid(tagStart, tagEnd - tagStart + 1);
                    static const QRegularExpression tagRe(R"(<\/?([a-zA-Z0-9]+))");
                    static const QRegularExpression classRe(R"(\bclass\s*=\s*["']([^"']+)["'])");
                    QRegularExpressionMatch tagM = tagRe.match(fullTag);
                    if (tagM.hasMatch()) {
                        const QString tagName = tagM.captured(1).toLower();
                        QRegularExpressionMatch classM = classRe.match(fullTag);
                        const QString className = classM.hasMatch() ? classM.captured(1) : QString();

                        tip = QStringLiteral(
                            "<div style='font-family: sans-serif; font-size: 11px;'>"
                            "<b>HTML Element:</b> &lt;%1&gt;"
                        ).arg(tagName);
                        if (!className.isEmpty()) {
                            tip += QStringLiteral("<br><b>Class:</b> <code>.%1</code>").arg(className);
                        }
                        tip += QStringLiteral("<br><span style='color:#94a3b8; font-size:10px;'>Tag: %1</span></div>")
                                   .arg(fullTag.toHtmlEscaped());
                    }
                }
            } else if (m_syntaxMode == SyntaxMode::Css) {
                QTextBlock block = cursor.block();
                const QString line = block.text().trimmed();
                if (line.contains(QLatin1Char('{')) || line.startsWith(QLatin1Char('.')) || line.startsWith(QLatin1Char('#'))) {
                    tip = QStringLiteral(
                        "<div style='font-family: sans-serif; font-size: 11px;'>"
                        "<b>CSS Selector Rule:</b> <code>%1</code>"
                        "</div>"
                    ).arg(line.toHtmlEscaped());
                } else if (line.contains(QLatin1Char(':'))) {
                    const QString prop = line.section(QLatin1Char(':'), 0, 0).trimmed();
                    tip = QStringLiteral(
                        "<div style='font-family: sans-serif; font-size: 11px;'>"
                        "<b>CSS Property:</b> <code>%1</code>"
                        "</div>"
                    ).arg(prop.toHtmlEscaped());
                }
            }

            if (!tip.isEmpty()) {
                QToolTip::showText(helpEvent->globalPos(), tip, viewport());
                return true;
            }
        }
    }
    return QPlainTextEdit::event(event);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);

    const bool isDark = ThemeManager::instance().isDarkMode();
    const bool isOled = ThemeManager::instance().isOledMode();

    QColor bgColor;
    if (isOled) {
        bgColor = QColor(0x00, 0x00, 0x00);
    } else if (isDark) {
        bgColor = QColor(0x18, 0x19, 0x20);
    } else {
        bgColor = QColor(0xf8, 0xfa, 0xfc);
    }

    const QColor numColor = isDark ? QColor(0x62, 0x72, 0xa4) : QColor(0x94, 0xa3, 0xb8);
    const QColor activeNumColor = ThemeManager::instance().accentColor();
    const QColor dividerColor = isDark ? QColor(0x28, 0x2a, 0x36) : QColor(0xe2, 0xe8, 0xf0);

    painter.fillRect(event->rect(), bgColor);

    // Right divider line
    painter.setPen(dividerColor);
    painter.drawLine(lineNumberArea->width() - 1, event->rect().top(),
                     lineNumberArea->width() - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    const int currentBlockNum = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(blockNumber + 1);
            if (blockNumber == currentBlockNum) {
                painter.setPen(activeNumColor);
                QFont f = painter.font();
                f.setBold(true);
                painter.setFont(f);
            } else {
                painter.setPen(numColor);
                QFont f = painter.font();
                f.setBold(false);
                painter.setFont(f);
            }
            painter.drawText(0, top, lineNumberArea->width() - 6, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
