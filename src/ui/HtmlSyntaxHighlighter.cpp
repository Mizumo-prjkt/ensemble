#include "HtmlSyntaxHighlighter.h"
#include "themes/ThemeManager.h"

HtmlSyntaxHighlighter::HtmlSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    updateThemeColors();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateThemeColors();
        rehighlight();
    });

    commentStartExpression = QRegularExpression(R"(<!--)");
    commentEndExpression = QRegularExpression(R"(-->)");
}

void HtmlSyntaxHighlighter::updateThemeColors()
{
    highlightingRules.clear();

    const ThemeManager &tm = ThemeManager::instance();

    tagFormat.setForeground(tm.syntaxTagColor());
    tagFormat.setFontWeight(QFont::Bold);

    attributeFormat.setForeground(tm.syntaxAttributeColor());

    valueFormat.setForeground(tm.syntaxValueColor());

    commentFormat.setForeground(tm.syntaxCommentColor());
    commentFormat.setFontItalic(true);

    entityFormat.setForeground(tm.syntaxEntityColor());

    // 1. Attribute Values (quoted strings)
    HighlightingRule rule;
    rule.pattern = QRegularExpression(R"("[^"]*"|'[^']*')");
    rule.format = valueFormat;
    highlightingRules.append(rule);

    // 2. Attributes (names before =)
    rule.pattern = QRegularExpression(R"(\b[a-zA-Z_-]+(?=\s*=))");
    rule.format = attributeFormat;
    highlightingRules.append(rule);

    // 3. HTML Tags (<tag, </tag, >)
    rule.pattern = QRegularExpression(R"(<\/?([a-zA-Z0-9:-]+)|/?>)");
    rule.format = tagFormat;
    highlightingRules.append(rule);

    // 4. HTML Entities (&amp; &quot; etc.)
    rule.pattern = QRegularExpression(R"(&[a-zA-Z0-9#]+;)");
    rule.format = entityFormat;
    highlightingRules.append(rule);
}

void HtmlSyntaxHighlighter::highlightBlock(const QString &text)
{
    // Apply normal syntax rules
    for (const HighlightingRule &rule : highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Multi-line HTML comments
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1) {
        startIndex = text.indexOf(commentStartExpression);
    }

    while (startIndex >= 0) {
        QRegularExpressionMatch match = commentEndExpression.match(text, startIndex);
        int endIndex = match.capturedStart();
        int commentLength = 0;

        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + match.capturedLength();
        }

        setFormat(startIndex, commentLength, commentFormat);
        startIndex = text.indexOf(commentStartExpression, startIndex + commentLength);
    }
}
