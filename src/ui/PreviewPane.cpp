#include "PreviewPane.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>

PreviewPane::PreviewPane(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QWebEngineView(this);
    layout->addWidget(m_view, 1);

    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_pageLoaded = ok;
    });
}

static QString jsonEncodeString(const QString &str)
{
    const QJsonArray arr{str};
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.length() - 2));
}

void PreviewPane::updatePreview(const QString &chapterTitle, const QString &html, const QString &css)
{
    // If chapter changed or page not loaded yet, do initial full load
    if (!m_pageLoaded || m_lastChapterTitle != chapterTitle) {
        m_lastChapterTitle = chapterTitle;
        m_pageLoaded = false;

        const QString escapedTitle = chapterTitle.toHtmlEscaped();

        const QString page = QStringLiteral(R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  html, body {
    margin: 0;
    padding: 0;
    background-color: #121318;
    color: #e2e2e5;
    font-family: Georgia, "Times New Roman", serif;
    font-size: 15px;
    line-height: 1.6;
  }
  #preview-banner {
    background-color: #1a1c23;
    color: #aeb3c6;
    border-bottom: 1px solid #2d313f;
    padding: 8px 14px;
    font-family: system-ui, -apple-system, sans-serif;
    font-size: 11px;
    line-height: 1.4;
  }
  #preview-banner strong {
    color: #e2e6f6;
  }
  #workskin-wrapper {
    padding: 16px;
    box-sizing: border-box;
  }
  #chapter-title {
    font-family: system-ui, -apple-system, sans-serif;
    font-size: 1.4rem;
    font-weight: bold;
    color: #33ccba;
    margin-top: 0;
    margin-bottom: 1rem;
    border-bottom: 1px solid #333444;
    padding-bottom: 0.4rem;
  }
  %1
</style>
</head>
<body>
<div id="preview-banner">
  ℹ️ <strong>Preview Note:</strong> Rendered with Chromium WebEngine & active AO3 Work Skin.
</div>
<div id="workskin-wrapper">
<div id="workskin">
<h2 id="chapter-title">%2</h2>
<div id="chapter-content">%3</div>
</div>
</div>
</body>
</html>)html").arg(css, escapedTitle, html);

        m_view->setHtml(page, QUrl(QStringLiteral("https://archiveofourown.org/")));
        return;
    }

    // Fast live update via DOM mutation (preserves scroll position 100% without jumping to top)
    const QString js = QStringLiteral(R"js(
        (function() {
            let titleEl = document.getElementById("chapter-title");
            let contentEl = document.getElementById("chapter-content");
            let styleEl = document.querySelector("style");

            if (titleEl) titleEl.textContent = %1;
            if (contentEl) contentEl.innerHTML = %2;
            if (styleEl) {
                let baseCss = `
  html, body { margin: 0; padding: 0; background-color: #121318; color: #e2e2e5; font-family: Georgia, "Times New Roman", serif; font-size: 15px; line-height: 1.6; }
  #preview-banner { background-color: #1a1c23; color: #aeb3c6; border-bottom: 1px solid #2d313f; padding: 8px 14px; font-family: system-ui, -apple-system, sans-serif; font-size: 11px; line-height: 1.4; }
  #preview-banner strong { color: #e2e6f6; }
  #workskin-wrapper { padding: 16px; box-sizing: border-box; }
  #chapter-title { font-family: system-ui, -apple-system, sans-serif; font-size: 1.4rem; font-weight: bold; color: #33ccba; margin-top: 0; margin-bottom: 1rem; border-bottom: 1px solid #333444; padding-bottom: 0.4rem; }
`;
                styleEl.textContent = baseCss + "\n" + %3;
            }
        })();
    )js")
    .arg(jsonEncodeString(chapterTitle), jsonEncodeString(html), jsonEncodeString(css));

    m_view->page()->runJavaScript(js);
}
