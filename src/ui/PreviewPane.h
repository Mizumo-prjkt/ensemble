#pragma once

#include <QPixmap>
#include <QWidget>

class QWebEngineView;
class QPushButton;
class QButtonGroup;
class QLabel;

enum class PreviewSkinMode {
    Ao3Default, // Standard Light AO3 skin (#ffffff background, #2a2a2a text, #990000 headers)
    Ao3Reversi  // Standard Dark AO3 Reversi skin (#111111 background, #1e1e1e content, #eeeeee text, #e25f5f headers)
};

class PreviewPane : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPane(QWidget *parent = nullptr);

    QPixmap captureScreenshot();

    PreviewSkinMode skinMode() const { return m_skinMode; }

public slots:
    void setSkinMode(PreviewSkinMode mode);
    void updatePreview(const QString &chapterTitle, const QString &html, const QString &css);
    void updateTheme();

private:
    void setupHeader();
    QString buildFullHtml(const QString &chapterTitle, const QString &html, const QString &css) const;
    QString baseCssForMode() const;
    void applySkinModeToPage();

    QWebEngineView *m_view = nullptr;
    QWidget *m_headerWidget = nullptr;
    QLabel *m_headerTitle = nullptr;
    QLabel *m_skinLabel = nullptr;
    QPushButton *m_defaultSkinBtn = nullptr;
    QPushButton *m_reversiSkinBtn = nullptr;
    QButtonGroup *m_skinButtonGroup = nullptr;

    bool m_pageLoaded = false;
    PreviewSkinMode m_skinMode = PreviewSkinMode::Ao3Default;
    QString m_lastChapterTitle;
    QString m_lastHtml;
    QString m_lastCss;
};
