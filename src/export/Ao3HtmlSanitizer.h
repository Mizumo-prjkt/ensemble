#pragma once

#include <QString>
#include <QStringList>

struct SanitizeResult {
    QString cleanHtml;
    QStringList strippedTags;
    QStringList strippedAttributes;
    bool wasModified = false;
};

class Ao3HtmlSanitizer
{
public:
    static QString sanitize(const QString &html);
    static SanitizeResult sanitizeDetailed(const QString &html);
};
