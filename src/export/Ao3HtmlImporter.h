#pragma once

#include <QString>

class QTextEdit;

class Ao3HtmlImporter
{
public:
    static void importHtml(QTextEdit *editor, const QString &html);
};
