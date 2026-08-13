#pragma once

#include <QWidget>

class QWebEngineView;

class PreviewPane : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPane(QWidget *parent = nullptr);

public slots:
    void updatePreview(const QString &chapterTitle, const QString &html, const QString &css);

private:
    QWebEngineView *m_view = nullptr;
    bool m_pageLoaded = false;
    QString m_lastChapterTitle;
};
