#include "ui/CodeEditor.h"
#include "ui/HtmlSourcePane.h"
#include "ui/CssEditorPane.h"
#include "model/Ao3Project.h"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include <QLabel>
#include <QPushButton>
#include <cassert>
#include <iostream>

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#endif

int main(int argc, char *argv[])
{
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif

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

        // Test HTML Issue Banner widgets: counts issues instead of listing them, collapsible details, and problem window button
        auto *htmlSummaryLabel = htmlPane.findChild<QLabel *>(QStringLiteral("issueSummaryLabel"));
        auto *htmlToggleBtn = htmlPane.findChild<QPushButton *>(QStringLiteral("toggleDetailsBtn"));
        auto *htmlDetailsLabel = htmlPane.findChild<QLabel *>(QStringLiteral("issueDetailsLabel"));
        auto *htmlProblemsBtn = htmlPane.findChild<QPushButton *>(QStringLiteral("viewProblemsBtn"));
        auto *htmlBreadcrumbLabel = htmlPane.findChild<QLabel *>(QStringLiteral("tagBreadcrumbLabel"));
        auto *htmlCopyBtn = htmlPane.findChild<QPushButton *>(QStringLiteral("copyAo3Button"));

        assert(htmlSummaryLabel != nullptr);
        assert(htmlToggleBtn != nullptr);
        assert(htmlDetailsLabel != nullptr);
        assert(htmlProblemsBtn != nullptr);
        assert(htmlBreadcrumbLabel != nullptr);
        assert(htmlCopyBtn != nullptr);

        // Put unsafe HTML with disallowed tag (<script> and </script>) and 1 warning (inline style)
        htmlPane.editor()->setPlainText(unsafeHtml);
        QMetaObject::invokeMethod(&htmlPane, "runLinter", Qt::DirectConnection);
        assert(htmlSummaryLabel->text().contains(QStringLiteral("3 HTML issues detected")));
        assert(htmlSummaryLabel->text().contains(QStringLiteral("2 errors, 1 warning")));
        assert(htmlDetailsLabel->isHidden()); // Collapsed by default

        // Expand and collapse details
        htmlToggleBtn->click();
        assert(!htmlDetailsLabel->isHidden());
        assert(htmlDetailsLabel->text().contains(QStringLiteral("inline style"), Qt::CaseInsensitive));
        assert(htmlDetailsLabel->text().contains(QStringLiteral("script"), Qt::CaseInsensitive));
        htmlToggleBtn->click();
        assert(htmlDetailsLabel->isHidden());

        // Test Problems Window button signal
        bool problemSignalEmitted = false;
        QObject::connect(&htmlPane, &HtmlSourcePane::openProblemsRequested, [&]() {
            problemSignalEmitted = true;
        });
        htmlProblemsBtn->click();
        assert(problemSignalEmitted);
        std::cout << "✓ HTML Issue Card count summary, collapsible details, and Problems Dialog button verified." << std::endl;

        // Test HTML Tag Path / Breadcrumbs calculation ("say the tags")
        const QString nestedHtml = QStringLiteral("<div class=\"card-box\"><p>Hello <span class=\"speaker\">world</span></p></div>");
        int spanPos = nestedHtml.indexOf(QStringLiteral("world"));
        QStringList tagPath = HtmlSourcePane::getTagPathAt(nestedHtml, spanPos);
        assert(tagPath.size() == 3);
        assert(tagPath.at(0) == QStringLiteral("div.card-box"));
        assert(tagPath.at(1) == QStringLiteral("p"));
        assert(tagPath.at(2) == QStringLiteral("span.speaker"));
        std::cout << "✓ HTML tag path breadcrumbs calculation verified." << std::endl;

        // Test Tag Insertion for tags rich text can't work out (span with class)
        htmlPane.editor()->setPlainText(QStringLiteral("Dialogue snippet"));
        QTextCursor cur = htmlPane.editor()->textCursor();
        cur.select(QTextCursor::Document);
        htmlPane.editor()->setTextCursor(cur);
        htmlPane.insertTag(QStringLiteral("span"), QStringLiteral("speech"));
        assert(htmlPane.html().contains(QStringLiteral("<span class=\"speech\">Dialogue snippet</span>")));
        std::cout << "✓ HTML Quick Tag wrapping and insertion verified." << std::endl;

        // Test Alternative Copy for AO3
        htmlPane.editor()->setPlainText(QStringLiteral("<p>Story text with <script>alert(1)</script> dangerous tag.</p>"));
        htmlPane.copyAo3Html();
        QString clipboardText = QApplication::clipboard()->text();
        assert(!clipboardText.contains(QStringLiteral("<script>")));
        assert(clipboardText.contains(QStringLiteral("Story text with")));
        std::cout << "✓ Alternative Copy for AO3 with live sanitization verified." << std::endl;
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
