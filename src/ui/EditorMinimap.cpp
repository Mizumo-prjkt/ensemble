#include "EditorMinimap.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>

EditorMinimap::EditorMinimap(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(90);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(QStringLiteral("background-color: #18181f; border-left: 1px solid #282833;"));
}

void EditorMinimap::setTargetEditor(QTextEdit *editor)
{
    m_textEdit = editor;
    m_plainEdit = nullptr;

    if (editor) {
        connect(editor->document(), &QTextDocument::contentsChanged, this, QOverload<>::of(&QWidget::update));
        connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, QOverload<>::of(&QWidget::update));
        connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, QOverload<>::of(&QWidget::update));
    }
    update();
}

void EditorMinimap::setTargetEditor(QPlainTextEdit *editor)
{
    m_plainEdit = editor;
    m_textEdit = nullptr;

    if (editor) {
        connect(editor->document(), &QTextDocument::contentsChanged, this, QOverload<>::of(&QWidget::update));
        connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, QOverload<>::of(&QWidget::update));
        connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, QOverload<>::of(&QWidget::update));
    }
    update();
}

QScrollBar *EditorMinimap::activeScrollBar() const
{
    if (m_textEdit) return m_textEdit->verticalScrollBar();
    if (m_plainEdit) return m_plainEdit->verticalScrollBar();
    return nullptr;
}

QTextDocument *EditorMinimap::activeDocument() const
{
    if (m_textEdit) return m_textEdit->document();
    if (m_plainEdit) return m_plainEdit->document();
    return nullptr;
}

void EditorMinimap::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#14141a")));

    QTextDocument *doc = activeDocument();
    QScrollBar *sb = activeScrollBar();
    if (!doc || !sb)
        return;

    int totalBlocks = doc->blockCount();
    if (totalBlocks == 0)
        return;

    // Draw miniature text lines proportionally across the full height
    double lineSpacing = static_cast<double>(height() - 12) / qMax(1, totalBlocks);
    lineSpacing = qBound(0.2, lineSpacing, 8.0);

    QTextBlock block = doc->begin();
    int blockIdx = 0;

    painter.setPen(Qt::NoPen);

    while (block.isValid()) {
        const int yPos = static_cast<int>(blockIdx * lineSpacing) + 4;
        if (yPos >= height() - 4)
            break;

        const QString text = block.text().trimmed();
        if (!text.isEmpty()) {
            int lineLength = qMin(static_cast<int>(text.length()), 70);
            int lineWidth = qMax(3, (lineLength * (width() - 16)) / 70);

            // Color code based on headings, links, work skins, or normal text
            QColor blockColor = QColor(QStringLiteral("#555768"));
            if (block.blockFormat().headingLevel() > 0) {
                blockColor = QColor(QStringLiteral("#ffb86c")); // Gold amber for headings
            } else {
                for (QTextBlock::iterator fragIt = block.begin(); !fragIt.atEnd(); ++fragIt) {
                    const QTextFragment frag = fragIt.fragment();
                    if (frag.isValid()) {
                        const QTextCharFormat charFmt = frag.charFormat();
                        if (charFmt.isAnchor()) {
                            blockColor = QColor(QStringLiteral("#8be9fd")); // Cyan for links
                            break;
                        } else if (charFmt.hasProperty(QTextFormat::Property::UserProperty + 1)) {
                            blockColor = QColor(QStringLiteral("#50fa7b")); // Emerald green for Work Skin CSS
                            break;
                        }
                    }
                }
                if (blockColor == QColor(QStringLiteral("#555768"))) {
                    if (text.startsWith(QLatin1Char('<')) || text.startsWith(QLatin1Char('#')) || text.startsWith(QLatin1Char('.'))) {
                        blockColor = QColor(QStringLiteral("#8be9fd")); // Accent blue/cyan for tags/selectors
                    } else if (text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1String("<!--"))) {
                        blockColor = QColor(QStringLiteral("#6272a4")); // Muted green/purple for comments
                    }
                }
            }

            const int lineHeight = qMax(1, static_cast<int>(lineSpacing * 0.85));
            painter.fillRect(8, yPos, lineWidth, lineHeight, blockColor);
        }
        block = block.next();
        blockIdx++;
    }

    // Viewport Highlight Rectangle
    int maxScroll = sb->maximum();
    int pageStep = sb->pageStep();
    int val = sb->value();

    int totalRange = maxScroll + pageStep;
    if (totalRange > 0) {
        double visibleRatio = static_cast<double>(pageStep) / totalRange;
        double scrollRatio = static_cast<double>(val) / totalRange;

        int viewportH = qMax(20, static_cast<int>(height() * visibleRatio));
        int viewportY = static_cast<int>(height() * scrollRatio);
        viewportY = qBound(0, viewportY, height() - viewportH);

        // Highlight box overlay
        painter.fillRect(QRect(0, viewportY, width(), viewportH), QColor(255, 255, 255, 25));
        painter.setPen(QPen(QColor(QStringLiteral("#6272a4")), 1));
        painter.drawRect(QRect(0, viewportY, width() - 1, viewportH - 1));
    }
}

void EditorMinimap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        handleMouseScroll(event->position().toPoint().y());
    }
}

void EditorMinimap::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        handleMouseScroll(event->position().toPoint().y());
    }
}

void EditorMinimap::handleMouseScroll(int y)
{
    QScrollBar *sb = activeScrollBar();
    if (!sb || height() == 0)
        return;

    double ratio = static_cast<double>(y) / height();
    int targetVal = static_cast<int>(ratio * (sb->maximum() + sb->pageStep()) - (sb->pageStep() / 2));
    sb->setValue(qBound(0, targetVal, sb->maximum()));
}
