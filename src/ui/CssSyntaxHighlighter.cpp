#include "CssSyntaxHighlighter.h"

CssSyntaxHighlighter::CssSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Dark theme friendly CSS styles
    selectorFormat.setForeground(QColor(QStringLiteral("#D7BA7D"))); // Yellowish-brown for selectors
    selectorFormat.setFontWeight(QFont::Bold);

    propertyFormat.setForeground(QColor(QStringLiteral("#9CDCFE"))); // Light Blue for properties

    valueFormat.setForeground(QColor(QStringLiteral("#CE9178"))); // Orange/brown for values

    commentFormat.setForeground(QColor(QStringLiteral("#6A9955"))); // Green for comments
    commentFormat.setFontItalic(true);

    HighlightingRule rule;

    // 1. Properties (name before :)
    rule.pattern = QRegularExpression(R"(\b[a-zA-Z0-9_-]+\s*(?=:))");
    rule.format = propertyFormat;
    highlightingRules.append(rule);

    // 2. Selectors (everything before { or ,)
    rule.pattern = QRegularExpression(R"([.#a-zA-Z0-9_-]+(?=\s*\{|\s*,))");
    rule.format = selectorFormat;
    highlightingRules.append(rule);

    // 3. Values (after : up to ;)
    rule.pattern = QRegularExpression(R"(:\s*([^;\}]+))");
    rule.format = valueFormat;
    highlightingRules.append(rule);

    commentStartExpression = QRegularExpression(R"(/\*)");
    commentEndExpression = QRegularExpression(R"(\*/)");
}

void CssSyntaxHighlighter::highlightBlock(const QString &text)
{
    // Apply normal syntax rules
    for (const HighlightingRule &rule : highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            if (rule.format == valueFormat) {
                int colonIndex = match.capturedStart(0);
                int length = match.capturedLength(0);
                if (length > 1) {
                    setFormat(colonIndex + 1, length - 1, rule.format);
                }
            } else {
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

    // CSS Multi-line Comments
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
