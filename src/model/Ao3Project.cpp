#include "Ao3Project.h"
#include "utils/DiffUtil.h"

#include <QFile>
#include <QUuid>
#include <QRegularExpression>
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
    const int order = m_chapters.size();
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

QString Ao3Project::latestCommitHash() const
{
    static const QRegularExpression re(QStringLiteral("commit ([0-9a-fA-F-]+)"));
    const auto match = re.match(m_historyLog);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    return QString();
}

QMap<QString, QString> Ao3Project::currentProjectState() const
{
    QMap<QString, QString> state;

    // 1. Metadata
    QString metaText;
    metaText += QStringLiteral("Title: %1\n").arg(m_title);
    metaText += QStringLiteral("Author: %1\n").arg(m_author);
    metaText += QStringLiteral("Rating: %1\n").arg(m_rating);
    metaText += QStringLiteral("Archive Warnings: %1\n").arg(m_archiveWarnings.join(QStringLiteral(", ")));
    metaText += QStringLiteral("Categories: %1\n").arg(m_categories.join(QStringLiteral(", ")));
    metaText += QStringLiteral("Fandoms: %1\n").arg(m_fandoms.join(QStringLiteral(", ")));
    metaText += QStringLiteral("Relationships: %1\n").arg(m_relationships.join(QStringLiteral(", ")));
    metaText += QStringLiteral("Characters: %1\n").arg(m_characters.join(QStringLiteral(", ")));
    metaText += QStringLiteral("Tags: %1\n").arg(m_tags.join(QStringLiteral(", ")));
    state.insert(QStringLiteral("metadata.txt"), metaText);

    // 2. Work Skin CSS
    state.insert(QStringLiteral("stylesheet/workskin.css"), m_workSkinCss);

    // 3. Chapters
    for (int i = 0; i < m_chapters.size(); ++i) {
        const Chapter &ch = m_chapters.at(i);
        const QString safeTitle = ch.title().simplified().replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_]")), QStringLiteral("_"));
        const QString chKey = QStringLiteral("chapters/%1_%2.html").arg(i + 1, 3, 10, QLatin1Char('0')).arg(safeTitle);
        QString chContent = QStringLiteral("<!-- Title: %1 -->\n%2").arg(ch.title(), ch.html());
        state.insert(chKey, chContent);
    }

    return state;
}

QString Ao3Project::appendHistoryCommit(const QString &author, const QString &message)
{
    const QString parentHash = latestCommitHash();
    const QString commitHash = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // Generate author with host tag if author does not already contain an '@'
    QString commitAuthor = author.trimmed();
    if (commitAuthor.isEmpty()) {
        commitAuthor = DiffUtil::defaultAuthorTag(m_author.isEmpty() ? QStringLiteral("Anonymous") : m_author);
    } else if (!commitAuthor.contains(QLatin1Char('@'))) {
        commitAuthor = DiffUtil::defaultAuthorTag(commitAuthor);
    }

    // Generate diff against last snapshot state
    const QMap<QString, QString> currentState = currentProjectState();
    QString diffBody;
    if (m_lastSnapshotState.isEmpty() && m_snapshotDiffs.isEmpty()) {
        // Initial project creation diff
        QMap<QString, QString> emptyState;
        diffBody = DiffUtil::createProjectDiff(emptyState, currentState);
    } else {
        diffBody = DiffUtil::createProjectDiff(m_lastSnapshotState, currentState);
    }

    // Prepend commit header to diff file
    QString fullDiff;
    fullDiff += QStringLiteral("# Snapshot: %1\n").arg(commitHash);
    if (!parentHash.isEmpty()) {
        fullDiff += QStringLiteral("# Parent:   %1\n").arg(parentHash);
    }
    fullDiff += QStringLiteral("# Author:   %1\n").arg(commitAuthor);
    fullDiff += QStringLiteral("# Date:     %1\n").arg(timestamp);
    fullDiff += QStringLiteral("# Message:  %1\n\n").arg(message.trimmed());
    fullDiff += diffBody;

    m_snapshotDiffs.insert(commitHash, fullDiff);
    m_lastSnapshotState = currentState;

    const QString entry = QStringLiteral(
        "commit %1\n"
        "Author: %2\n"
        "Date:   %3\n\n"
        "    %4\n\n")
        .arg(commitHash, commitAuthor, timestamp, message.trimmed());

    m_historyLog.prepend(entry);
    return commitHash;
}

void Ao3Project::resetToNew()
{
    m_title = QStringLiteral("Untitled Work");
    m_author = QStringLiteral("Anonymous");
    m_rating = QStringLiteral("Not Rated");
    m_archiveWarnings.clear();
    m_categories.clear();
    m_fandoms.clear();
    m_relationships.clear();
    m_characters.clear();
    m_tags.clear();
    m_createdDate = QDateTime::currentDateTimeUtc();
    m_modifiedDate = QDateTime::currentDateTimeUtc();
    m_historyLog.clear();
    m_snapshotDiffs.clear();
    m_lastSnapshotState.clear();
    m_filePath.clear();
    m_lastModifiedBy.clear();
    m_lastModifiedAt = QDateTime();
    m_dirty = false;
    m_isLegacyFormat = false;
    m_activeChapterIndex = 0;
    m_chapters.clear();
    m_agenticData.clear();
    m_lore.clear();
    m_lore.ensureDefaults();

    QFile defaultCss(QStringLiteral(":/default-work-skin.css"));
    if (defaultCss.open(QIODevice::ReadOnly | QIODevice::Text))
        m_workSkinCss = QString::fromUtf8(defaultCss.readAll());
    else
        m_workSkinCss.clear();

    m_chapters.append(Chapter(QStringLiteral("Chapter 1"), 0));
    m_chapters.first().setHtml(QStringLiteral("<p></p>"));

    appendHistoryCommit(m_author, QStringLiteral("Initial project creation"));
}

int Ao3Project::loadedChaptersCount() const
{
    int count = 0;
    for (const Chapter &ch : m_chapters) {
        if (ch.isLoaded()) count++;
    }
    return count;
}

qint64 Ao3Project::inMemoryHtmlBytes() const
{
    qint64 bytes = 0;
    for (const Chapter &ch : m_chapters) {
        if (ch.isLoaded()) {
            bytes += ch.html().toUtf8().size();
        }
    }
    return bytes;
}

qint64 Ao3Project::totalStoryHtmlBytes() const
{
    qint64 bytes = 0;
    for (const Chapter &ch : m_chapters) {
        bytes += ch.htmlByteSize();
    }
    return bytes;
}

qint64 Ao3Project::chaptersMemoryBytes() const
{
    qint64 bytes = sizeof(QList<Chapter>) + m_chapters.capacity() * sizeof(Chapter);
    for (const Chapter &ch : m_chapters) {
        bytes += ch.memoryUsageBytes();
    }
    return bytes;
}

qint64 Ao3Project::workSkinMemoryBytes() const
{
    return sizeof(QString) + m_workSkinCss.capacity() * sizeof(QChar);
}

qint64 Ao3Project::loreMemoryBytes() const
{
    return static_cast<qint64>(m_lore.entities().size() * 256 +
                               m_lore.historyEvents().size() * 128 +
                               m_lore.conflicts().size() * 128 +
                               m_lore.plans().size() * 128 +
                               m_lore.defaultGraph().nodes().size() * 128 +
                               m_lore.defaultGraph().connectors().size() * 64);
}

void Ao3Project::trimInactiveChaptersMemory(int preserveIndex)
{
    if (preserveIndex < 0) preserveIndex = m_activeChapterIndex;
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (i != preserveIndex && m_chapters.at(i).isLoaded()) {
            m_chapters[i].unloadToSessionCache();
        }
    }
}

