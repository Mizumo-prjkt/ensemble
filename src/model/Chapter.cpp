#include "Chapter.h"
#include "utils/AppPaths.h"
#include <QDir>
#include <QFile>
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

qint64 Chapter::memoryUsageBytes() const
{
    qint64 bytes = sizeof(Chapter);
    bytes += m_title.capacity() * sizeof(QChar);
    if (m_isLoaded) {
        bytes += m_html.capacity() * sizeof(QChar);
    }
    return bytes;
}

qint64 Chapter::htmlByteSize() const
{
    if (m_isLoaded) {
        return m_html.toUtf8().size();
    }
    return m_cachedByteSize;
}

void Chapter::ensureLoaded() const
{
    if (m_isLoaded) {
        return;
    }
    if (!m_cacheFilePath.isEmpty() && QFile::exists(m_cacheFilePath)) {
        QFile file(m_cacheFilePath);
        if (file.open(QIODevice::ReadOnly)) {
            m_html = QString::fromUtf8(file.readAll());
            m_isLoaded = true;
            m_cachedByteSize = m_html.toUtf8().size();
            return;
        }
    }
    m_isLoaded = true;
}

QString Chapter::html() const
{
    ensureLoaded();
    return m_html;
}

void Chapter::setHtml(const QString &html)
{
    m_html = html;
    m_isLoaded = true;
    m_cachedByteSize = html.toUtf8().size();
}

void Chapter::unloadToSessionCache(const QString &cacheDir)
{
    if (!m_isLoaded) {
        return;
    }

    QString dirPath = cacheDir;
    if (dirPath.isEmpty()) {
        dirPath = AppPaths::autosaveDir() + QStringLiteral("/chapter_cache");
    }
    QDir().mkpath(dirPath);
    m_cacheFilePath = QDir(dirPath).filePath(m_id.toString(QUuid::WithoutBraces) + QStringLiteral(".chcache"));

    QFile file(m_cacheFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QByteArray utf8 = m_html.toUtf8();
        m_cachedByteSize = utf8.size();
        file.write(utf8);
        file.close();
    }

    m_html.clear();
    m_html.squeeze();
    m_isLoaded = false;
}

QString Chapter::toPlainText() const
{
    ensureLoaded();
    if (m_html.isEmpty()) {
        return QString();
    }
    QTextDocument doc;
    doc.setHtml(m_html);
    return doc.toPlainText();
}
