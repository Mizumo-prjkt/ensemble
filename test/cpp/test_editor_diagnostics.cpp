#include "ui/CodeEditor.h"
#include "ui/HtmlSourcePane.h"
#include "ui/CssEditorPane.h"
#include "model/Ao3Project.h"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QSettings>
#include <QLabel>
#include <QPushButton>
#include <cassert>
#include <iostream>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Ensemble"));
    app.setOrganizationName(QStringLiteral("Ensemble"));

    std::cout << "Testing CodeEditor, HtmlSourcePane, and CssEditorPane diagnostics..." << std::endl;

    // 1. Test HtmlSourcePane auto-formatting & line breaks
    {
        HtmlSourcePane htmlPane;

        // Ensure auto-formatting setting is enabled
        QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
        settings.setValue(QStringLiteral("editor/autoFormatHtml"), true);

        // Given a typical AO3 single-line unformatted string
        const QString rawOneLiner = QStringLiteral("<p>First paragraph here.</p><p>Second paragraph here.</p><blockquote>A quoted line.</blockquote><p>Final paragraph.</p>");
        htmlPane.setHtml(rawOneLiner);

        const QString formatted = htmlPane.html();
        // Should contain double line breaks separating blocks
        assert(formatted.contains(QStringLiteral("</p>\n\n<p>")));
        assert(formatted.contains(QStringLiteral("</p>\n\n<blockquote>")));
        assert(formatted.contains(QStringLiteral("</blockquote>\n\n<p>")));
        std::cout << "✓ Raw HTML one-liner auto-formatting verified (linebreaks inserted between blocks)." << std::endl;

        // Test manual formatHtml slot
        htmlPane.editor()->setPlainText(QStringLiteral("<p>Para A</p><p>Para B</p>"));
        htmlPane.formatHtml();
        assert(htmlPane.html().contains(QStringLiteral("</p>\n\n<p>")));
        std::cout << "✓ Manual formatHtml() action verified." << std::endl;

        // Test Linter diagnostics for disallowed AO3 tags & inline styles
        const QString unsafeHtml = QStringLiteral("<p style=\"color: red;\">Inline style</p><script>alert('bad');</script>");
        htmlPane.editor()->setPlainText(unsafeHtml);
        // Force linter check
        QMetaObject::invokeMethod(&htmlPane, "runLinter", Qt::DirectConnection);

        const auto issues = htmlPane.editor()->diagnosticIssues();
        assert(!issues.isEmpty());
        bool foundInlineStyle = false;
        bool foundDisallowedTag = false;
        for (const auto &iss : issues) {
            if (iss.message.contains(QStringLiteral("Inline style"))) foundInlineStyle = true;
            if (iss.message.contains(QStringLiteral("<script>"))) foundDisallowedTag = true;
        }
        assert(foundInlineStyle);
        assert(foundDisallowedTag);
        std::cout << "✓ HTML Linter diagnostic issue tracking and extra selections verified." << std::endl;
    }

    // 2. Test CssEditorPane duplicate selector detection & issue card
    {
        CssEditorPane cssPane;
        Ao3Project project;
        cssPane.bindProject(&project);

        const QString sampleCss = QStringLiteral(
            "#workskin .ichika {\n"
            "    font-weight: bold;\n"
            "}\n"
            "#workskin .haruka {\n"
            "    color: #e11d48;\n"
            "}\n"
            "#workskin .ichika {\n"
            "    color: #3b82f6;\n"
            "}\n"
        );

        cssPane.setCss(sampleCss);

        const QStringList classes = cssPane.cssClassNames();
        assert(classes.contains(QStringLiteral("ichika")));
        assert(classes.contains(QStringLiteral("haruka")));

        // Duplicate selector detection
        const auto issues = cssPane.editor()->diagnosticIssues();
        assert(!issues.isEmpty());
        bool foundDuplicate = false;
        for (const auto &iss : issues) {
            if (iss.message.contains(QStringLiteral("Duplicate selector")) && iss.message.contains(QStringLiteral(".ichika"))) {
                foundDuplicate = true;
            }
        }
        assert(foundDuplicate);

        // Check issue banner UI widgets
        auto *summaryLabel = cssPane.findChild<QLabel *>(QStringLiteral("issueSummaryLabel"));
        auto *toggleBtn = cssPane.findChild<QPushButton *>(QStringLiteral("toggleDetailsBtn"));
        auto *detailsLabel = cssPane.findChild<QLabel *>(QStringLiteral("issueDetailsLabel"));

        assert(summaryLabel != nullptr);
        assert(toggleBtn != nullptr);
        assert(detailsLabel != nullptr);
        assert(summaryLabel->text().contains(QStringLiteral("1 CSS issue")));
        assert(detailsLabel->isHidden());

        // Toggle details
        toggleBtn->click();
        assert(!detailsLabel->isHidden());
        assert(detailsLabel->text().contains(QStringLiteral("ichika")));

        toggleBtn->click();
        assert(detailsLabel->isHidden());
        std::cout << "✓ CSS duplicate selector diagnostics and expandable issue card verified." << std::endl;

        // Test CSS unclosed brace / syntax error
        const QString brokenCss = QStringLiteral("#workskin .broken {\n    color: red;\n");
        cssPane.setCss(brokenCss);
        const auto brokenIssues = cssPane.editor()->diagnosticIssues();
        assert(!brokenIssues.isEmpty());
        bool foundUnclosedBrace = false;
        for (const auto &iss : brokenIssues) {
            if (iss.message.contains(QStringLiteral("Unclosed block")) && iss.isError) {
                foundUnclosedBrace = true;
            }
        }
        assert(foundUnclosedBrace);
        std::cout << "✓ CSS syntax error diagnostics verified." << std::endl;
    }

    // 3. Test CodeEditor extra selections & wavy underlays
    {
        CodeEditor editor;
        editor.setSyntaxMode(CodeEditor::SyntaxMode::Css);
        editor.setPlainText(QStringLiteral(".selector { color: red; }"));

        CodeDiagnosticIssue issue;
        issue.startPos = 0;
        issue.length = 9;
        issue.message = QStringLiteral("Warning test");
        issue.isError = false;

        editor.setDiagnosticIssues({issue});
        assert(editor.extraSelections().size() >= 1);

        editor.clearDiagnosticIssues();
        editor.refreshHighlights();
        // Only current line highlight remains (if enabled)
        for (const auto &sel : editor.extraSelections()) {
            assert(sel.format.underlineStyle() != QTextCharFormat::SpellCheckUnderline);
        }
        std::cout << "✓ CodeEditor diagnostic wavy underlays and clearing verified." << std::endl;
    }

    std::cout << "All Editor Diagnostics, Formatting, and Issue Highlighting tests passed successfully!" << std::endl;
    return 0;
}
