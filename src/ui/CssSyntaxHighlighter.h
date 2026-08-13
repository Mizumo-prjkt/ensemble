#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class CssSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit CssSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightingRule> highlightingRules;

    QRegularExpression commentStartExpression;
    QRegularExpression commentEndExpression;

    QTextCharFormat selectorFormat;
    QTextCharFormat propertyFormat;
    QTextCharFormat valueFormat;
    QTextCharFormat commentFormat;
};
