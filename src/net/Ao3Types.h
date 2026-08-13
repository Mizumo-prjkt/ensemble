#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct Ao3Pseud {
    QString name;
    QString url;
    bool isDefault = false;
};

struct Ao3WorkSummary {
    int workId = 0;
    QString title;
    QString pseud;
    QString fandom;
    int wordCount = 0;
    int chapterCount = 0;
    int totalChapters = 0; // -1 = "?"
    QDateTime lastUpdated;
    bool isComplete = false;
    bool isDraft = false;
};

struct Ao3ChapterContent {
    int chapterNumber = 0;
    QString title;
    QString bodyHtml;
    QString notesBegin;
    QString notesEnd;
};

struct Ao3FullWork {
    Ao3WorkSummary summary;
    QList<Ao3ChapterContent> chapters;
    QString workSkinCss;
};

struct Ao3WorkSkin {
    int skinId = 0;
    QString name;
    QString cssContent;
    bool isWorkSkin = false;
};
