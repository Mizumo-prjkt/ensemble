#pragma once

#include <QString>
#include <QUuid>

class Chapter
{
public:
    Chapter();
    explicit Chapter(const QString &title, int order = 0);

    QUuid id() const { return m_id; }
    void setId(const QUuid &id) { m_id = id; }

    QString title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    int order() const { return m_order; }
    void setOrder(int order) { m_order = order; }

    bool isLoaded() const { return m_isLoaded; }

    // Memory usage footprint in bytes
    qint64 memoryUsageBytes() const;

    // Total content byte size (loaded or cached)
    qint64 htmlByteSize() const;

    // Retrieves HTML string, loading from session cache on demand if unloaded
    QString html() const;
    void setHtml(const QString &html);

    // Forces HTML to be loaded into RAM
    void ensureLoaded() const;

    // Unloads HTML from RAM to session cache on disk to minimize memory footprint
    void unloadToSessionCache(const QString &cacheDir = QString());

    QString toPlainText() const;

    QString cacheFilePath() const { return m_cacheFilePath; }

private:
    QUuid m_id;
    QString m_title;
    int m_order = 0;
    mutable QString m_html;
    mutable bool m_isLoaded = true;
    mutable QString m_cacheFilePath;
    mutable qint64 m_cachedByteSize = 0;
};
