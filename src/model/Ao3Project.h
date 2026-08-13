#pragma once

#include "Chapter.h"

#include <QDateTime>
#include <QList>
#include <QString>

class Ao3Project
{
public:
    Ao3Project();

    QString title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    QString author() const { return m_author; }
    void setAuthor(const QString &author) { m_author = author; }

    QDateTime createdDate() const { return m_createdDate; }
    void setCreatedDate(const QDateTime &dt) { m_createdDate = dt; }

    QDateTime modifiedDate() const { return m_modifiedDate; }
    void setModifiedDate(const QDateTime &dt) { m_modifiedDate = dt; }

    QString historyLog() const { return m_historyLog; }
    void setHistoryLog(const QString &log) { m_historyLog = log; }
    void appendHistoryCommit(const QString &author, const QString &message);

    QString workSkinCss() const { return m_workSkinCss; }
    void setWorkSkinCss(const QString &css) { m_workSkinCss = css; }

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

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    void resetToNew();

private:
    QString m_title;
    QString m_author;
    QDateTime m_createdDate;
    QDateTime m_modifiedDate;
    QString m_historyLog;
    QString m_workSkinCss;
    QList<Chapter> m_chapters;
    int m_activeChapterIndex = 0;
    bool m_dirty = false;
    QString m_filePath;
};
