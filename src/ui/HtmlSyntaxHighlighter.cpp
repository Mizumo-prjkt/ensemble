#include "HtmlSyntaxHighlighter.h"

HtmlSyntaxHighlighter::HtmlSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // VSCode / IDE-like colors (dark theme suitable)
    tagFormat.setForeground(QColor(QStringLiteral("#569CD6"))); // Blue for tags
    tagFormat.setFontWeight(QFont::Bold);

    attributeFormat.setForeground(QColor(QStringLiteral("#9CDCFE"))); // Light Blue for attributes

    valueFormat.setForeground(QColor(QStringLiteral("#CE9178"))); // Orange/brown for values

    commentFormat.setForeground(QColor(QStringLiteral("#6A9955"))); // Green for comments
    commentFormat.setFontItalic(true);

    entityFormat.setForeground(QColor(QStringLiteral("#DCDCAA"))); // Light yellow for entities like &amp;

    // Rules
    HighlightingRule rule;

    // 1. Attribute Values (quoted strings)
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

    commentStartExpression = QRegularExpression(R"(<!--)");
    commentEndExpression = QRegularExpression(R"(-->)");
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
    if (previousBlockState() != 1)
        startIndex = text.indexOf(commentStartExpression);

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
