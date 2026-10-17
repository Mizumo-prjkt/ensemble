#include "PreviewPane.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>

static QString jsonEncodeString(const QString &str) {
  const QJsonArray arr{str};
  const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(json.mid(1, json.length() - 2));
}

PreviewPane::PreviewPane(QWidget *parent) : QWidget(parent) {
  QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
  const QString modeStr = settings.value(QStringLiteral("preview/skinMode"), QStringLiteral("default")).toString().toLower();
  m_skinMode = (modeStr == QStringLiteral("reversi")) ? PreviewSkinMode::Ao3Reversi : PreviewSkinMode::Ao3Default;

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  setupHeader();
  layout->addWidget(m_headerWidget);

  m_view = new QWebEngineView(this);
  layout->addWidget(m_view, 1);

  connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
    m_pageLoaded = ok;
    qCDebug(logPreview) << "QWebEngineView loadFinished:" << ok;
  });

  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
    updateTheme();
  });
  connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this, [this](SkinFlavor) {
    updateTheme();
  });

  updateTheme();
}

void PreviewPane::setupHeader() {
  m_headerWidget = new QWidget(this);
  m_headerWidget->setObjectName(QStringLiteral("previewHeader"));

  auto *hLayout = new QHBoxLayout(m_headerWidget);
  hLayout->setContentsMargins(10, 4, 10, 4);
  hLayout->setSpacing(8);

  m_headerTitle = new QLabel(QStringLiteral("👁️ Preview"), m_headerWidget);
  m_headerTitle->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 11px;"));
  hLayout->addWidget(m_headerTitle);

  hLayout->addStretch(1);

  m_skinLabel = new QLabel(QStringLiteral("Skin:"), m_headerWidget);
  m_skinLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
  hLayout->addWidget(m_skinLabel);

  m_defaultSkinBtn = new QPushButton(QStringLiteral("☀️ AO3 Default (Light)"), m_headerWidget);
  m_defaultSkinBtn->setCheckable(true);
  m_defaultSkinBtn->setToolTip(QStringLiteral("Test Work Skin on AO3 Site Default background (Light #ffffff, crimson headers)"));

  m_reversiSkinBtn = new QPushButton(QStringLiteral("🌙 Reversi (Dark)"), m_headerWidget);
  m_reversiSkinBtn->setCheckable(true);
  m_reversiSkinBtn->setToolTip(QStringLiteral("Test Work Skin on AO3 Reversi background (Dark #111111, dark content card)"));

  m_skinButtonGroup = new QButtonGroup(this);
  m_skinButtonGroup->addButton(m_defaultSkinBtn, static_cast<int>(PreviewSkinMode::Ao3Default));
  m_skinButtonGroup->addButton(m_reversiSkinBtn, static_cast<int>(PreviewSkinMode::Ao3Reversi));

  if (m_skinMode == PreviewSkinMode::Ao3Reversi) {
    m_reversiSkinBtn->setChecked(true);
  } else {
    m_defaultSkinBtn->setChecked(true);
  }

  connect(m_defaultSkinBtn, &QPushButton::clicked, this, [this]() {
    setSkinMode(PreviewSkinMode::Ao3Default);
  });
  connect(m_reversiSkinBtn, &QPushButton::clicked, this, [this]() {
    setSkinMode(PreviewSkinMode::Ao3Reversi);
  });

  hLayout->addWidget(m_defaultSkinBtn);
  hLayout->addWidget(m_reversiSkinBtn);
}

void PreviewPane::updateTheme() {
  if (!m_headerWidget) return;

  const bool isDark = ThemeManager::instance().isDarkMode();
  const QString bg = ThemeManager::instance().surfaceBgColor().name();
  const QString text = ThemeManager::instance().primaryTextColor().name();
  const QString muted = ThemeManager::instance().mutedTextColor().name();
  const QString border = ThemeManager::instance().borderColor().name();
  const QString accent = ThemeManager::instance().accentColor().name();
  const QString hoverBg = isDark ? QStringLiteral("rgba(255, 255, 255, 0.08)") : QStringLiteral("rgba(0, 0, 0, 0.05)");

  m_headerWidget->setStyleSheet(QStringLiteral(
      "QWidget#previewHeader {"
      "  background-color: %1;"
      "  border-bottom: 1px solid %2;"
      "}"
      "QLabel {"
      "  color: %3;"
      "}"
      "QPushButton {"
      "  background-color: transparent;"
      "  color: %4;"
      "  border: 1px solid %2;"
      "  border-radius: 4px;"
      "  padding: 3px 8px;"
      "  font-size: 11px;"
      "  font-weight: 500;"
      "}"
      "QPushButton:hover:!checked {"
      "  background-color: %5;"
      "  color: %3;"
      "}"
      "QPushButton:checked {"
      "  background-color: %6;"
      "  color: #ffffff;"
      "  border-color: %6;"
      "  font-weight: bold;"
      "}"
  ).arg(bg, border, text, muted, hoverBg, accent));
}

void PreviewPane::setSkinMode(PreviewSkinMode mode) {
  m_skinMode = mode;
  QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
  settings.setValue(QStringLiteral("preview/skinMode"),
                    mode == PreviewSkinMode::Ao3Reversi ? QStringLiteral("reversi") : QStringLiteral("default"));

  if (m_defaultSkinBtn) m_defaultSkinBtn->setChecked(mode == PreviewSkinMode::Ao3Default);
  if (m_reversiSkinBtn) m_reversiSkinBtn->setChecked(mode == PreviewSkinMode::Ao3Reversi);

  applySkinModeToPage();
}

QString PreviewPane::baseCssForMode() const {
  if (m_skinMode == PreviewSkinMode::Ao3Reversi) {
    return QStringLiteral(
      "html, body { margin: 0; padding: 0; background-color: #111111; color: #eeeeee; font-family: Georgia, \"Times New Roman\", serif; font-size: 15px; line-height: 1.6; }\n"
      "#preview-banner { background-color: #1a1a1a; color: #a0a0a0; border-bottom: 1px solid #333333; padding: 8px 14px; font-family: system-ui, -apple-system, sans-serif; font-size: 11px; line-height: 1.4; }\n"
      "#preview-banner strong { color: #e25f5f; }\n"
      "#workskin-wrapper { background-color: #1e1e1e; color: #eeeeee; border: 1px solid #333333; border-radius: 4px; padding: 20px; box-sizing: border-box; margin: 12px; }\n"
      "#chapter-title { font-family: system-ui, -apple-system, sans-serif; font-size: 1.4rem; font-weight: bold; color: #e25f5f; margin-top: 0; margin-bottom: 1rem; border-bottom: 1px solid #333333; padding-bottom: 0.4rem; }\n"
      "a { color: #ffffff; text-decoration: underline; }\n"
      "blockquote { border-left: 3px solid #444444; margin-left: 1rem; padding-left: 1rem; color: #cccccc; }\n"
      "hr { border: 0; height: 1px; background: #333333; margin: 1.5rem 0; }\n"
    );
  }

  // Ao3Default (Light background, standard crimson AO3 accents)
  return QStringLiteral(
    "html, body { margin: 0; padding: 0; background-color: #ffffff; color: #2a2a2a; font-family: Georgia, \"Times New Roman\", serif; font-size: 15px; line-height: 1.6; }\n"
    "#preview-banner { background-color: #f7f7f7; color: #555555; border-bottom: 1px solid #e0e0e0; padding: 8px 14px; font-family: system-ui, -apple-system, sans-serif; font-size: 11px; line-height: 1.4; }\n"
    "#preview-banner strong { color: #990000; }\n"
    "#workskin-wrapper { background-color: #ffffff; padding: 20px; box-sizing: border-box; }\n"
    "#chapter-title { font-family: system-ui, -apple-system, sans-serif; font-size: 1.4rem; font-weight: bold; color: #990000; margin-top: 0; margin-bottom: 1rem; border-bottom: 1px solid #dddddd; padding-bottom: 0.4rem; }\n"
    "a { color: #990000; text-decoration: underline; }\n"
    "blockquote { border-left: 3px solid #dddddd; margin-left: 1rem; padding-left: 1rem; color: #444444; }\n"
    "hr { border: 0; height: 1px; background: #dddddd; margin: 1.5rem 0; }\n"
  );
}

void PreviewPane::applySkinModeToPage() {
  if (!m_pageLoaded || !m_view || !m_view->page()) {
    if (!m_lastHtml.isEmpty()) {
      m_pageLoaded = false;
      updatePreview(m_lastChapterTitle, m_lastHtml, m_lastCss);
    }
    return;
  }

  const QString bannerText = (m_skinMode == PreviewSkinMode::Ao3Reversi)
      ? QStringLiteral("ℹ️ <strong>AO3 Reversi Preview:</strong> Testing Work Skin contrast on dark background.")
      : QStringLiteral("ℹ️ <strong>AO3 Default Preview:</strong> Testing Work Skin on standard white site skin.");

  const QString baseCss = baseCssForMode();
  const QString js = QStringLiteral(R"js(
        (function() {
            let styleEl = document.getElementById("ensemble-base-style");
            if (styleEl) {
                styleEl.textContent = %1 + "\n" + %2;
            }
            let bannerEl = document.getElementById("preview-banner");
            if (bannerEl) {
                bannerEl.innerHTML = %3;
            }
        })();
    )js")
    .arg(jsonEncodeString(baseCss), jsonEncodeString(m_lastCss), jsonEncodeString(bannerText));

  m_view->page()->runJavaScript(js);
}

QString PreviewPane::buildFullHtml(const QString &chapterTitle, const QString &html, const QString &css) const {
  const QString escapedTitle = chapterTitle.toHtmlEscaped();
  const QString baseCss = baseCssForMode();
  const QString bannerText = (m_skinMode == PreviewSkinMode::Ao3Reversi)
      ? QStringLiteral("ℹ️ <strong>AO3 Reversi Preview:</strong> Testing Work Skin contrast on dark background.")
      : QStringLiteral("ℹ️ <strong>AO3 Default Preview:</strong> Testing Work Skin on standard white site skin.");

  return QStringLiteral(R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style id="ensemble-base-style">
%1
%2
</style>
</head>
<body>
<div id="preview-banner">%3</div>
<div id="workskin-wrapper">
<div id="workskin">
<h2 id="chapter-title">%4</h2>
<div id="chapter-content">%5</div>
</div>
</div>
</body>
</html>)html")
    .arg(baseCss, css, bannerText, escapedTitle, html);
}

void PreviewPane::updatePreview(const QString &chapterTitle,
                                const QString &html, const QString &css) {
  m_lastChapterTitle = chapterTitle;
  m_lastHtml = html;
  m_lastCss = css;

  // If chapter changed or page not loaded yet, do initial full load
  if (!m_pageLoaded) {
    ENSEMBLE_PROFILE_CAT_SCOPE("PreviewPane Full HTML Load", logPreview);
    qCDebug(logPreview)
        << "Performing full QWebEngineView HTML load for chapter:"
        << chapterTitle;
    m_pageLoaded = false;

    const QString page = buildFullHtml(chapterTitle, html, css);
    m_view->setHtml(page, QUrl(QStringLiteral("https://archiveofourown.org/")));
    return;
  }

  // Fast live update via DOM mutation (preserves scroll position 100% without jumping to top)
  const QString baseCss = baseCssForMode();
  const QString bannerText = (m_skinMode == PreviewSkinMode::Ao3Reversi)
      ? QStringLiteral("ℹ️ <strong>AO3 Reversi Preview:</strong> Testing Work Skin contrast on dark background.")
      : QStringLiteral("ℹ️ <strong>AO3 Default Preview:</strong> Testing Work Skin on standard white site skin.");

  const QString js = QStringLiteral(R"js(
        (function() {
            let titleEl = document.getElementById("chapter-title");
            let contentEl = document.getElementById("chapter-content");
            let styleEl = document.getElementById("ensemble-base-style");
            let bannerEl = document.getElementById("preview-banner");

            let titleChanged = (titleEl && titleEl.textContent !== %1);
            if (titleEl) titleEl.textContent = %1;
            if (contentEl) contentEl.innerHTML = %2;
            if (bannerEl) bannerEl.innerHTML = %3;
            if (styleEl) {
                styleEl.textContent = %4 + "\n" + %5;
            }
            if (titleChanged) {
                window.scrollTo(0, 0);
            }
        })();
    )js")
    .arg(jsonEncodeString(chapterTitle),
         jsonEncodeString(html),
         jsonEncodeString(bannerText),
         jsonEncodeString(baseCss),
         jsonEncodeString(css));

  qCDebug(logPreview) << "Executing live DOM mutation update for chapter:"
                      << chapterTitle << "HTML bytes:" << html.length();
  m_view->page()->runJavaScript(js);
}

QPixmap PreviewPane::captureScreenshot() {
  if (!m_view) return QPixmap();
  if (m_view->width() < 100 || m_view->height() < 100) {
    m_view->resize(1024, 768);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  return m_view->grab();
}
