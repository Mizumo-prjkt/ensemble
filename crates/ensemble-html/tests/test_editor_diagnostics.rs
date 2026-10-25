use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use ensemble_core::{
    init_debug_system, log_message, set_aggressive_checks_alert_callback, Ao3Project, DiffUtil,
    RgbColor,
};
use ensemble_html::{Ao3HtmlSanitizer, CssLinter, HtmlFormatter, HtmlLinter, SpellChecker};

#[test]
fn test_editor_diagnostics_full_suite() {
    init_debug_system();

    // 1. Test HtmlFormatter auto-formatting & line breaks
    {
        let raw_one_liner = "<p>First paragraph here.</p><p>Second paragraph here.</p><blockquote>A quoted line.</blockquote><p>Final paragraph.</p>";
        let formatted = HtmlFormatter::format_html_source(raw_one_liner);
        assert!(formatted.contains("</p>\n\n<p>"));
        assert!(formatted.contains("</p>\n\n<blockquote>"));
        assert!(formatted.contains("</blockquote>\n\n<p>"));

        let para_ab = HtmlFormatter::format_html_source("<p>Para A</p><p>Para B</p>");
        assert!(para_ab.contains("</p>\n\n<p>"));

        // Test Linter diagnostics for disallowed AO3 tags & inline styles
        let unsafe_html = "<p style=\"color: red;\">Inline style</p><script>alert('bad');</script>";
        let report = HtmlLinter::lint_html(unsafe_html, 1);
        assert!(!report.issues.is_empty());
        assert!(report
            .issues
            .iter()
            .any(|i| i.message.contains("Inline style")));
        assert!(report
            .issues
            .iter()
            .any(|i| i.message.contains("<script>")));
        assert!(report.summary_text.contains("3 HTML issues detected"));
        assert!(report.summary_text.contains("2 errors, 1 warning"));
        assert!(report.details_html.to_lowercase().contains("inline style"));
        assert!(report.details_html.to_lowercase().contains("script"));

        // Test HTML Tag Path / Breadcrumbs calculation ("say the tags")
        let nested_html =
            "<div class=\"card-box\"><p>Hello <span class=\"speaker\">world</span></p></div>";
        let span_pos = nested_html.find("world").unwrap();
        let tag_path = HtmlFormatter::get_tag_path_at(nested_html, span_pos);
        assert_eq!(tag_path, vec!["div.card-box", "p", "span.speaker"]);
        let breadcrumbs = HtmlFormatter::format_tag_breadcrumbs(&tag_path);
        assert!(breadcrumbs.contains("<div.card-box> › <p> › <span.speaker>"));

        // Test Tag Insertion for tags rich text can't work out (span with class)
        let wrapped_span =
            HtmlFormatter::wrap_or_insert_tag("Dialogue snippet", "span", "speech", "");
        assert_eq!(wrapped_span, "<span class=\"speech\">Dialogue snippet</span>");

        // Test Alternative Copy for AO3 with live sanitization
        let dirty = "<p>Story text with <script>alert(1)</script> dangerous tag.</p>";
        let clean = Ao3HtmlSanitizer::sanitize(dirty);
        assert!(!clean.contains("<script>"));
        assert!(clean.contains("Story text with"));
    }

    // 2. Test CssLinter duplicate selector detection & issue card
    {
        let mut project = Ao3Project::new();
        project.chapters[0].html = "<p class=\"missing-hero\">Hello</p>".to_string();

        let sample_css = r#"#workskin .ichika {
    font-weight: bold;
}
#workskin .haruka {
    color: #e11d48;
}
#workskin .ichika {
    color: #3b82f6;
}
"#;
        let report = CssLinter::lint_css(sample_css, None);
        assert!(report.class_names.contains(&"ichika".to_string()));
        assert!(report.class_names.contains(&"haruka".to_string()));
        assert!(!report.issues.is_empty());
        assert!(report
            .issues
            .iter()
            .any(|i| i.message.contains("Duplicate selector") && i.message.contains(".ichika")));
        assert!(report.summary_text.contains("1 CSS issue"));
        assert!(report.details_html.contains("ichika"));

        // Test CSS unclosed brace / syntax error
        let broken_css = "#workskin .broken {\n    color: red;\n";
        let broken_report = CssLinter::lint_css(broken_css, None);
        assert!(!broken_report.issues.is_empty());
        assert!(broken_report
            .issues
            .iter()
            .any(|i| i.message.contains("Unclosed block") && i.is_error));
    }

    // 3. Test DiffUtil safe CSS extraction with hyphenated classes & warning throttling
    {
        let sample_css = r#"#workskin .school-sekai-box {
    background-color: #f8fafc;
    color: #0284c7;
}
.stage-sekai-box, .street-sekai-box {
    color: #e11d48;
}
.miku {
    color: #39c5bb;
}
#workskin .abyss-box:hover {
    color: #6366f1;
}
"#;

        let decl1 = DiffUtil::extract_css_class_declarations(sample_css, "school-sekai-box");
        assert!(decl1.contains("color: #0284c7"));
        assert!(decl1.contains("background-color: #f8fafc"));

        let col1 = DiffUtil::extract_css_color(sample_css, "school-sekai-box");
        assert_eq!(col1, RgbColor::from_css_str("#0284c7"));

        let col2 = DiffUtil::extract_css_color(sample_css, "stage-sekai-box");
        assert_eq!(col2, RgbColor::from_css_str("#e11d48"));

        let col3 = DiffUtil::extract_css_color(sample_css, "street-sekai-box");
        assert_eq!(col3, RgbColor::from_css_str("#e11d48"));

        let col4 = DiffUtil::extract_css_color(sample_css, "miku");
        assert_eq!(col4, RgbColor::from_css_str("#39c5bb"));

        let col5 = DiffUtil::extract_css_color(sample_css, "abyss-box");
        assert_eq!(col5, RgbColor::from_css_str("#6366f1"));

        let col_none = DiffUtil::extract_css_color(sample_css, "nonexistent-box");
        assert!(col_none.is_none());

        let col_cached = DiffUtil::extract_css_color(sample_css, "miku");
        assert_eq!(col_cached, col4);

        // Anti-spam warning throttling and aggressive check alert callback verification
        let alert_call_count = Arc::new(AtomicUsize::new(0));
        let alerted_suppressed_count = Arc::new(AtomicUsize::new(0));
        let acc_clone = Arc::clone(&alert_call_count);
        let asc_clone = Arc::clone(&alerted_suppressed_count);

        set_aggressive_checks_alert_callback(Some(Box::new(move |_msg, count| {
            acc_clone.fetch_add(1, Ordering::SeqCst);
            asc_clone.store(count, Ordering::SeqCst);
        })));

        for _ in 0..15 {
            log_message(
                "WARN",
                "default",
                "Simulated repeated regex warning spam test message",
            );
        }

        assert!(alert_call_count.load(Ordering::SeqCst) > 0);
        assert!(alerted_suppressed_count.load(Ordering::SeqCst) >= 5);
        set_aggressive_checks_alert_callback(None);
    }

    // 4. Test SpellChecker grammar checks
    {
        let grammar_issues =
            SpellChecker::check_grammar("It was the the best of times ,and hello,world.");
        assert!(grammar_issues.len() >= 3);
        assert!(grammar_issues
            .iter()
            .any(|r| r.message.contains("Repeated word")));
        assert!(grammar_issues
            .iter()
            .any(|r| r.message.contains("Unexpected space before punctuation")));
        assert!(grammar_issues
            .iter()
            .any(|r| r.message.contains("Missing space after punctuation")));
    }
}
