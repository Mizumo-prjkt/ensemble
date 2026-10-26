#pragma once

#include "Chapter.h"
#include "AgenticData.h"
#include "LoreManager.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class Ao3Project
{
public:
    Ao3Project();

    QString title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    QString author() const { return m_author; }
    void setAuthor(const QString &author) { m_author = author; }

    // AO3 Tag metadata attributes
    QString rating() const { return m_rating; }
    void setRating(const QString &rating) { m_rating = rating; }

    QStringList &archiveWarnings() { return m_archiveWarnings; }
    const QStringList &archiveWarnings() const { return m_archiveWarnings; }
    void setArchiveWarnings(const QStringList &warnings) { m_archiveWarnings = warnings; }

    QStringList &categories() { return m_categories; }
    const QStringList &categories() const { return m_categories; }
    void setCategories(const QStringList &categories) { m_categories = categories; }

    QStringList &fandoms() { return m_fandoms; }
    const QStringList &fandoms() const { return m_fandoms; }
    void setFandoms(const QStringList &fandoms) { m_fandoms = fandoms; }

    QStringList &relationships() { return m_relationships; }
    const QStringList &relationships() const { return m_relationships; }
    void setRelationships(const QStringList &relationships) { m_relationships = relationships; }

    QStringList &characters() { return m_characters; }
    const QStringList &characters() const { return m_characters; }
    void setCharacters(const QStringList &characters) { m_characters = characters; }

    QStringList &tags() { return m_tags; }
    const QStringList &tags() const { return m_tags; }
    void setTags(const QStringList &tags) { m_tags = tags; }

    void addCharacter(const QString &character) {
        if (!character.isEmpty() && !m_characters.contains(character)) {
            m_characters.append(character);
        }
    }
    void addRelationship(const QString &rel) {
        if (!rel.isEmpty() && !m_relationships.contains(rel)) {
            m_relationships.append(rel);
        }
    }
    void addFandom(const QString &fandom) {
        if (!fandom.isEmpty() && !m_fandoms.contains(fandom)) {
            m_fandoms.append(fandom);
        }
    }
    void addTag(const QString &tag) {
        if (!tag.isEmpty() && !m_tags.contains(tag)) {
            m_tags.append(tag);
        }
    }

    QDateTime createdDate() const { return m_createdDate; }
    void setCreatedDate(const QDateTime &dt) { m_createdDate = dt; }

    QDateTime modifiedDate() const { return m_modifiedDate; }
    void setModifiedDate(const QDateTime &dt) { m_modifiedDate = dt; }

    QString historyLog() const { return m_historyLog; }
    void setHistoryLog(const QString &log) { m_historyLog = log; }
    QString appendHistoryCommit(const QString &author, const QString &message);
    QString createSnapshot(const QString &author, const QString &message) { return appendHistoryCommit(author, message); }
    QString latestCommitHash() const;

    const QMap<QString, QString> &snapshotDiffs() const { return m_snapshotDiffs; }
    QMap<QString, QString> &snapshotDiffs() { return m_snapshotDiffs; }
    QString snapshotDiff(const QString &hash) const { return m_snapshotDiffs.value(hash); }
    void setSnapshotDiff(const QString &hash, const QString &diffText) { m_snapshotDiffs.insert(hash, diffText); }

    QMap<QString, QString> currentProjectState() const;
    void setLastSnapshotState(const QMap<QString, QString> &state) { m_lastSnapshotState = state; }
    const QMap<QString, QString> &lastSnapshotState() const { return m_lastSnapshotState; }

    QString workSkinCss() const { return m_workSkinCss; }
    void setWorkSkinCss(const QString &css) { m_workSkinCss = css; }

    AgenticData &agenticData() { return m_agenticData; }
    const AgenticData &agenticData() const { return m_agenticData; }

    LoreManager &lore() { return m_lore; }
    const LoreManager &lore() const { return m_lore; }

    QList<Chapter> &chapters() { return m_chapters; }
    const QList<Chapter> &chapters() const { return m_chapters; }

    int activeChapterIndex() const { return m_activeChapterIndex; }
    void setActiveChapterIndex(int index) { m_activeChapterIndex = index; }

    Chapter *activeChapter();
    const Chapter *activeChapter() const;

    void addChapter(const QString &title = QString());
    void removeChapter(int index);
    void moveChapter(int from, int to);
    void sortChaptersByOrder();

    // Memory and resource management
    int loadedChaptersCount() const;
    qint64 inMemoryHtmlBytes() const;
    qint64 totalStoryHtmlBytes() const;
    qint64 chaptersMemoryBytes() const;
    qint64 workSkinMemoryBytes() const;
    qint64 loreMemoryBytes() const;
    void trimInactiveChaptersMemory(int preserveIndex = -1);

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    QString lastModifiedBy() const { return m_lastModifiedBy; }
    void setLastModifiedBy(const QString &by) { m_lastModifiedBy = by; }

    QDateTime lastModifiedAt() const { return m_lastModifiedAt; }
    void setLastModifiedAt(const QDateTime &dt) { m_lastModifiedAt = dt; }

    void markDirtyBy(const QString &modifier = QStringLiteral("user")) {
        m_dirty = true;
        m_lastModifiedBy = modifier;
        m_lastModifiedAt = QDateTime::currentDateTimeUtc();
        m_modifiedDate = m_lastModifiedAt;
    }

    bool isLegacyFormat() const { return m_isLegacyFormat; }
    void setLegacyFormat(bool legacy) { m_isLegacyFormat = legacy; }

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    void resetToNew();

private:
    QString m_title;
    QString m_author;
    QString m_rating = QStringLiteral("Not Rated");
    QStringList m_archiveWarnings;
    QStringList m_categories;
    QStringList m_fandoms;
    QStringList m_relationships;
    QStringList m_characters;
    QStringList m_tags;
    QDateTime m_createdDate;
    QDateTime m_modifiedDate;
    QString m_lastModifiedBy;
    QDateTime m_lastModifiedAt;
    QString m_historyLog;
    QMap<QString, QString> m_snapshotDiffs;
    QMap<QString, QString> m_lastSnapshotState;
    QString m_workSkinCss;
    AgenticData m_agenticData;
    LoreManager m_lore;
    QList<Chapter> m_chapters;
    int m_activeChapterIndex = 0;
    bool m_dirty = false;
    bool m_isLegacyFormat = false;
    QString m_filePath;
};
