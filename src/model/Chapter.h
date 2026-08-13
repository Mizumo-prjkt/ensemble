#pragma once

#include <QString>
#include <QUuid>

class Chapter
{
public:
    Chapter();
    explicit Chapter(const QString &title, int order = 0);

    QUuid id() const { return m_id; }
    QString title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    int order() const { return m_order; }
    void setOrder(int order) { m_order = order; }

    QString html() const { return m_html; }
    void setHtml(const QString &html) { m_html = html; }

private:
    QUuid m_id;
    QString m_title;
    int m_order = 0;
    QString m_html;
};
