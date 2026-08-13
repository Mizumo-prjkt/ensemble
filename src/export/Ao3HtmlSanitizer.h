#pragma once

#include <QString>

class Ao3HtmlSanitizer
{
public:
    static QString sanitize(const QString &html);
};
