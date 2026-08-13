#include "CssEditorPane.h"
#include "CodeEditor.h"
#include "CssSyntaxHighlighter.h"
#include "EditorMinimap.h"
#include "model/Ao3Project.h"
#include "model/Chapter.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QRegularExpression>
#include <QVBoxLayout>

CssEditorPane::CssEditorPane(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Warning label for duplicates and removed classes still in use
    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #3a2a00; color: #ffcc00; padding: 6px 10px; "
        "border-bottom: 1px solid #664d00; font-size: 11px; font-weight: bold; }"));
    m_warningLabel->hide();

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(4, 4, 4, 4);
    auto *label = new QLabel(QStringLiteral("Work Skin CSS"), this);
    topRow->addWidget(label);

    m_editor = new CodeEditor(this);

    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_editor->setFont(monoFont);
    m_editor->setPlaceholderText(QStringLiteral("/* Custom AO3 work skin CSS */"));

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

    new CssSyntaxHighlighter(m_editor->document());

    // Minimap Scroll Preview sidebar
    auto *minimap = new EditorMinimap(this);
    minimap->setTargetEditor(m_editor);

    auto *editorRow = new QHBoxLayout();
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(0);
    editorRow->addWidget(m_editor, 1);
    editorRow->addWidget(minimap);

    layout->addWidget(m_warningLabel);
    layout->addLayout(topRow);
    layout->addLayout(editorRow, 1);

    connect(m_editor, &CodeEditor::textChanged, this, [this]() {
        if (m_blockChanges)
            return;
        const QString text = m_editor->toPlainText();
        parseCssClasses(text);
        emit cssChanged(text);
    });
}

QString CssEditorPane::css() const
{
    return m_editor->toPlainText();
}

void CssEditorPane::setCss(const QString &css)
{
    m_blockChanges = true;
    m_editor->setPlainText(css);
    m_blockChanges = false;
    parseCssClasses(css);
}

void CssEditorPane::parseCssClasses(const QString &css)
{
    static const QRegularExpression classRe(
        QStringLiteral(R"(\.([a-zA-Z_-][a-zA-Z0-9_-]*)\s*[{,:\[])"));

    QMap<QString, int> classCounts;
    QStringList uniqueClasses;

    QRegularExpressionMatchIterator it = classRe.globalMatch(css);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        classCounts[name]++;
        if (!uniqueClasses.contains(name))
            uniqueClasses << name;
    }
    uniqueClasses.sort();

    QStringList warnings;

    // 1. Check for duplicates
    for (auto iter = classCounts.constBegin(); iter != classCounts.constEnd(); ++iter) {
        if (iter.value() > 1) {
            warnings << QStringLiteral("⚠️ Warning: Duplicate CSS class '.%1' defined %2 times in Work Skin.")
                            .arg(iter.key())
                            .arg(iter.value());
        }
    }

    // 2. Check for removed classes still in use in chapters
    if (m_project) {
        for (const QString &oldClass : m_classNames) {
            if (!uniqueClasses.contains(oldClass)) {
                // Class was removed. Check if any chapter HTML still uses it.
                int chNum = 1;
                for (const Chapter &ch : m_project->chapters()) {
                    if (ch.html().contains(oldClass)) {
                        warnings << QStringLiteral("⚠️ Warning: CSS class '.%1' was removed from Work Skin, but is still used in Chapter %2 (\"%3\").")
                                        .arg(oldClass)
                                        .arg(chNum)
                                        .arg(ch.title());
                    }
                    chNum++;
                }
            }
        }
    }

    if (!warnings.isEmpty()) {
        m_warningLabel->setText(warnings.join(QStringLiteral("\n")));
        m_warningLabel->show();
    } else {
        m_warningLabel->hide();
    }

    if (uniqueClasses != m_classNames) {
        m_classNames = uniqueClasses;
        emit cssClassesChanged(m_classNames);
    }
}
