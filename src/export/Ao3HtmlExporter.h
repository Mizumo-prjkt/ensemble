#pragma once

#include <QString>

class QTextDocument;

class Ao3HtmlExporter
{
public:
    static QString exportDocument(const QTextDocument *document);
};
