#include "CssEditorPane.h"
#include "CodeEditor.h"
#include "CssSyntaxHighlighter.h"
#include "EditorMinimap.h"
#include "model/Ao3Project.h"
#include "model/Chapter.h"
#include "themes/ThemeManager.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

CssEditorPane::CssEditorPane(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Issue Banner Card
    m_issueCard = new QFrame(this);
    m_issueCard->setObjectName(QStringLiteral("cssIssueCard"));
    auto *cardLayout = new QVBoxLayout(m_issueCard);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(6);

    auto *cardHeader = new QHBoxLayout();
    cardHeader->setContentsMargins(0, 0, 0, 0);
    cardHeader->setSpacing(8);

    m_issueSummaryLabel = new QLabel(m_issueCard);
    m_issueSummaryLabel->setObjectName(QStringLiteral("issueSummaryLabel"));
    m_issueSummaryLabel->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 11px;"));
    cardHeader->addWidget(m_issueSummaryLabel, 1);

    m_toggleDetailsBtn = new QPushButton(QStringLiteral("Show Details ▾"), m_issueCard);
    m_toggleDetailsBtn->setObjectName(QStringLiteral("toggleDetailsBtn"));
    m_toggleDetailsBtn->setCursor(Qt::PointingHandCursor);
    connect(m_toggleDetailsBtn, &QPushButton::clicked, this, &CssEditorPane::toggleIssueDetails);
    cardHeader->addWidget(m_toggleDetailsBtn);

    m_issueDetailsLabel = new QLabel(m_issueCard);
    m_issueDetailsLabel->setObjectName(QStringLiteral("issueDetailsLabel"));
    m_issueDetailsLabel->setWordWrap(true);
    m_issueDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_issueDetailsLabel->setVisible(false);
    m_issueDetailsLabel->setStyleSheet(QStringLiteral("font-size: 11px; line-height: 1.4; padding-top: 4px;"));

    cardLayout->addLayout(cardHeader);
    cardLayout->addWidget(m_issueDetailsLabel);
    m_issueCard->hide();

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(8, 4, 8, 4);
    m_titleLabel = new QLabel(QStringLiteral("Work Skin CSS"), this);
    topRow->addWidget(m_titleLabel);

    m_editor = new CodeEditor(this);
    m_editor->setSyntaxMode(CodeEditor::SyntaxMode::Css);

    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_editor->setFont(monoFont);
    m_editor->setPlaceholderText(QStringLiteral("/* Custom AO3 work skin CSS */"));

    new CssSyntaxHighlighter(m_editor->document());

    // Minimap Scroll Preview sidebar
    auto *minimap = new EditorMinimap(this);
    minimap->setTargetEditor(m_editor);

    auto *editorRow = new QHBoxLayout();
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(0);
    editorRow->addWidget(m_editor, 1);
    editorRow->addWidget(minimap);

    layout->addWidget(m_issueCard);
    layout->addLayout(topRow);
    layout->addLayout(editorRow, 1);

    connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_blockChanges) {
            QString text = m_editor->toPlainText();
            parseCssClasses(text);
            emit cssChanged(text);
        }
    });

    updateTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateTheme();
    });
    connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this, [this](SkinFlavor) {
        updateTheme();
    });
}

void CssEditorPane::updateTheme()
{
    const bool isDark = ThemeManager::instance().isDarkMode();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: %1;")
            .arg(ThemeManager::instance().mutedTextColor().name()));
    }

    if (m_editor) {
        m_editor->setStyleSheet(ThemeManager::instance().getCodeEditorStyleSheet());
    }

    if (m_issueCard) {
        m_issueCard->setStyleSheet(isDark ? QStringLiteral(
            "#cssIssueCard { background-color: #2b2718; border-bottom: 1px solid #624c16; }"
            "QLabel { color: #ffb86c; }"
            "QPushButton { background: #3d351e; color: #ffb86c; border: 1px solid #624c16; border-radius: 4px; padding: 2px 8px; font-size: 10px; font-weight: 600; }"
            "QPushButton:hover { background: #4d4326; }"
        ) : QStringLiteral(
            "#cssIssueCard { background-color: #fef3c7; border-bottom: 1px solid #fde68a; }"
            "QLabel { color: #92400e; }"
            "QPushButton { background: #fde68a; color: #92400e; border: 1px solid #fcd34d; border-radius: 4px; padding: 2px 8px; font-size: 10px; font-weight: 600; }"
            "QPushButton:hover { background: #fcd34d; }"
        ));
    }
}

void CssEditorPane::updateSettings()
{
    if (m_editor) {
        m_editor->refreshHighlights();
    }
    parseCssClasses(m_editor->toPlainText());
}

void CssEditorPane::toggleIssueDetails()
{
    m_detailsExpanded = !m_detailsExpanded;
    m_issueDetailsLabel->setVisible(m_detailsExpanded);
    m_toggleDetailsBtn->setText(m_detailsExpanded ? QStringLiteral("Hide Details ▴")
                                                  : QStringLiteral("Show Details ▾"));
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
    QStringList newClasses;
    QStringList duplicates;
    QList<CodeDiagnosticIssue> issues;

    // Matches CSS class selectors like .my-class or .custom_btn
    static const QRegularExpression classRe(R"(\.([a-zA-Z0-9_-]+))");
    QRegularExpressionMatchIterator it = classRe.globalMatch(css);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString className = match.captured(1);

        if (className.contains(QLatin1Char(':'))) {
            className = className.section(QLatin1Char(':'), 0, 0);
        }

        if (!className.isEmpty()) {
            if (newClasses.contains(className)) {
                if (!duplicates.contains(className)) {
                    duplicates << className;
                }

                // Highlight duplicate selector in editor with wavy underline and hover tooltip
                CodeDiagnosticIssue dupIssue;
                dupIssue.startPos = match.capturedStart();
                dupIssue.length = match.capturedLength();
                dupIssue.isError = false; // warning
                dupIssue.message = QStringLiteral("Duplicate selector '.%1' is declared multiple times in this Work Skin.").arg(className);
                issues.append(dupIssue);
            } else {
                newClasses << className;
            }
        }
    }

    // Check syntax issues: unclosed comments /* ...
    int commentStart = css.indexOf(QStringLiteral("/*"));
    while (commentStart != -1) {
        int commentEnd = css.indexOf(QStringLiteral("*/"), commentStart + 2);
        if (commentEnd == -1) {
            CodeDiagnosticIssue commentIssue;
            commentIssue.startPos = commentStart;
            commentIssue.length = qMax(2, css.length() - commentStart);
            commentIssue.isError = true;
            commentIssue.message = QStringLiteral("Unclosed CSS comment: missing '*/'");
            issues.append(commentIssue);
            break;
        }
        commentStart = css.indexOf(QStringLiteral("/*"), commentEnd + 2);
    }

    // Check syntax issues: unclosed block braces { ... }
    int openBraces = 0;
    int lastOpenBracePos = -1;
    for (int i = 0; i < css.length(); ++i) {
        if (css[i] == QLatin1Char('{')) {
            openBraces++;
            lastOpenBracePos = i;
        } else if (css[i] == QLatin1Char('}')) {
            if (openBraces > 0) {
                openBraces--;
            } else {
                CodeDiagnosticIssue braceIssue;
                braceIssue.startPos = i;
                braceIssue.length = 1;
                braceIssue.isError = true;
                braceIssue.message = QStringLiteral("Unexpected closing brace '}' without opening '{'");
                issues.append(braceIssue);
            }
        }
    }
    if (openBraces > 0 && lastOpenBracePos != -1) {
        CodeDiagnosticIssue braceIssue;
        braceIssue.startPos = lastOpenBracePos;
        braceIssue.length = 1;
        braceIssue.isError = true;
        braceIssue.message = QStringLiteral("Unclosed block: missing closing brace '}'");
        issues.append(braceIssue);
    }

    // Apply code-level highlights
    m_editor->setDiagnosticIssues(issues);

    // Check if any classes used across chapters have been removed or are missing from Work Skin CSS
    QStringList orphanedWarnings;
    if (m_project) {
        QMap<QString, QStringList> usedClasses;
        static const QRegularExpression htmlClassAttrRe(R"(\bclass\s*=\s*["']([^"']+)["'])");

        for (int i = 0; i < m_project->chapters().size(); ++i) {
            const Chapter &ch = m_project->chapters().at(i);
            QRegularExpressionMatchIterator htmlIt = htmlClassAttrRe.globalMatch(ch.html());
            while (htmlIt.hasNext()) {
                QRegularExpressionMatch m = htmlIt.next();
                QStringList classNames = m.captured(1).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                for (const QString &cls : classNames) {
                    if (!usedClasses[cls].contains(ch.title())) {
                        usedClasses[cls] << ch.title();
                    }
                }
            }
        }

        for (auto clsIt = usedClasses.constBegin(); clsIt != usedClasses.constEnd(); ++clsIt) {
            const QString &cls = clsIt.key();
            const QStringList &chaptersUsing = clsIt.value();

            if (!newClasses.contains(cls)) {
                if (chaptersUsing.size() <= 2) {
                    orphanedWarnings << QStringLiteral("• Class '.%1' is used in: %2").arg(cls, chaptersUsing.join(QStringLiteral(", ")));
                } else {
                    orphanedWarnings << QStringLiteral("• Class '.%1' is used across %2 chapters").arg(cls).arg(chaptersUsing.size());
                }
            }
        }
    }

    // Update issue banner
    const int totalIssues = issues.size() + orphanedWarnings.size();
    if (totalIssues == 0) {
        m_issueCard->hide();
    } else {
        QString summaryText = QStringLiteral("⚠️ %1 CSS issue%2 detected")
                                  .arg(totalIssues)
                                  .arg(totalIssues == 1 ? QString() : QStringLiteral("s"));
        if (!duplicates.isEmpty() && !orphanedWarnings.isEmpty()) {
            summaryText += QStringLiteral(" (%1 duplicate selector%2, %3 missing class%4)")
                               .arg(duplicates.size())
                               .arg(duplicates.size() == 1 ? QString() : QStringLiteral("s"))
                               .arg(orphanedWarnings.size())
                               .arg(orphanedWarnings.size() == 1 ? QString() : QStringLiteral("es"));
        } else if (!duplicates.isEmpty()) {
            summaryText += QStringLiteral(" (%1 duplicate selector%2)")
                               .arg(duplicates.size())
                               .arg(duplicates.size() == 1 ? QString() : QStringLiteral("s"));
        } else if (!orphanedWarnings.isEmpty()) {
            summaryText += QStringLiteral(" (%1 missing class%2)")
                               .arg(orphanedWarnings.size())
                               .arg(orphanedWarnings.size() == 1 ? QString() : QStringLiteral("es"));
        }
        m_issueSummaryLabel->setText(summaryText);

        QStringList detailParts;
        if (!duplicates.isEmpty()) {
            detailParts << QStringLiteral("<b>Duplicate Selectors:</b><br>%1").arg(duplicates.join(QStringLiteral(", ")));
        }
        if (!orphanedWarnings.isEmpty()) {
            detailParts << QStringLiteral("<b>Missing CSS Classes used in Chapters:</b><br>%1").arg(orphanedWarnings.join(QStringLiteral("<br>")));
        }
        m_issueDetailsLabel->setText(detailParts.join(QStringLiteral("<br><br>")));
        m_issueCard->show();
    }

    m_classNames = newClasses;
    emit cssClassesChanged(m_classNames);
}
