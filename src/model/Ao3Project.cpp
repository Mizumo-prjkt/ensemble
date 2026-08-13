#include "Ao3Project.h"

#include <QFile>
#include <QUuid>
#include <algorithm>

Ao3Project::Ao3Project()
{
    resetToNew();
}

Chapter *Ao3Project::activeChapter()
{
    if (m_activeChapterIndex < 0 || m_activeChapterIndex >= m_chapters.size())
        return nullptr;
    return &m_chapters[m_activeChapterIndex];
}

const Chapter *Ao3Project::activeChapter() const
{
    if (m_activeChapterIndex < 0 || m_activeChapterIndex >= m_chapters.size())
        return nullptr;
    return &m_chapters[m_activeChapterIndex];
}

void Ao3Project::addChapter(const QString &title)
{
    const int order = m_chapters.isEmpty() ? 0 : m_chapters.last().order() + 1;
    const QString chapterTitle = title.isEmpty()
        ? QStringLiteral("Chapter %1").arg(m_chapters.size() + 1)
        : title;
    m_chapters.append(Chapter(chapterTitle, order));
    m_activeChapterIndex = m_chapters.size() - 1;
    m_dirty = true;
}

void Ao3Project::removeChapter(int index)
{
    if (index < 0 || index >= m_chapters.size())
        return;
    if (m_chapters.size() <= 1)
        return;

    m_chapters.removeAt(index);
    for (int i = 0; i < m_chapters.size(); ++i)
        m_chapters[i].setOrder(i);

    if (m_activeChapterIndex >= m_chapters.size())
        m_activeChapterIndex = m_chapters.size() - 1;
    else if (m_activeChapterIndex > index)
        --m_activeChapterIndex;

    m_dirty = true;
}

void Ao3Project::moveChapter(int from, int to)
{
    if (from < 0 || from >= m_chapters.size() || to < 0 || to >= m_chapters.size() || from == to)
        return;

    m_chapters.move(from, to);
    for (int i = 0; i < m_chapters.size(); ++i)
        m_chapters[i].setOrder(i);

    if (m_activeChapterIndex == from)
        m_activeChapterIndex = to;
    else if (from < m_activeChapterIndex && to >= m_activeChapterIndex)
        --m_activeChapterIndex;
    else if (from > m_activeChapterIndex && to <= m_activeChapterIndex)
        ++m_activeChapterIndex;

    m_dirty = true;
}

void Ao3Project::sortChaptersByOrder()
{
    std::stable_sort(m_chapters.begin(), m_chapters.end(), [](const Chapter &a, const Chapter &b) {
        return a.order() < b.order();
    });
}

void Ao3Project::appendHistoryCommit(const QString &author, const QString &message)
{
    const QString commitHash = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString commitAuthor = author.isEmpty() ? (m_author.isEmpty() ? QStringLiteral("Anonymous") : m_author) : author;

    const QString entry = QStringLiteral(
        "commit %1\n"
        "Author: %2\n"
        "Date:   %3\n\n"
        "    %4\n\n")
        .arg(commitHash, commitAuthor, timestamp, message);

    m_historyLog.prepend(entry);
}

void Ao3Project::resetToNew()
{
    m_title = QStringLiteral("Untitled Work");
    m_author = QStringLiteral("Anonymous");
    m_createdDate = QDateTime::currentDateTimeUtc();
    m_modifiedDate = QDateTime::currentDateTimeUtc();
    m_historyLog.clear();
    m_filePath.clear();
    m_dirty = false;
    m_activeChapterIndex = 0;
    m_chapters.clear();

    QFile defaultCss(QStringLiteral(":/default-work-skin.css"));
    if (defaultCss.open(QIODevice::ReadOnly | QIODevice::Text))
        m_workSkinCss = QString::fromUtf8(defaultCss.readAll());
    else
        m_workSkinCss.clear();

    m_chapters.append(Chapter(QStringLiteral("Chapter 1"), 0));
    m_chapters.first().setHtml(QStringLiteral("<p></p>"));

    appendHistoryCommit(m_author, QStringLiteral("Initial project creation"));
}
