#include "Chapter.h"

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
