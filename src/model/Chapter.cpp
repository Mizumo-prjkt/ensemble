#include "Chapter.h"
#include <QTextDocument>

Chapter::Chapter()
    : m_id(QUuid::createUuid())
    , m_title(QStringLiteral("Chapter 1"))
{
}

Chapter::Chapter(const QString &title, int order)
    : m_id(QUuid::createUuid())
    , m_title(title)
    , m_order(order)
{
}

QString Chapter::toPlainText() const
{
    if (m_html.isEmpty()) {
        return QString();
    }
    QTextDocument doc;
    doc.setHtml(m_html);
    return doc.toPlainText();
}
