#include "AboutDialog.h"
#include "AppIcon.h"
#include "debug/debug.hpp"
#include "net/UpdateChecker.h"
#include "themes/ThemeManager.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , m_updater(new UpdateChecker(this))
{
  setWindowTitle(QStringLiteral("About Ensemble"));
  setWindowIcon(AppIcon::icon());
  setModal(true);
  setFixedSize(560, 495);
  setupUi();
  updateTheme();

  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
          [this](AppTheme) { updateTheme(); });
  connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this,
          [this](SkinFlavor) { updateTheme(); });

  connect(m_updater, &UpdateChecker::updateCheckStarted, this, &AboutDialog::onUpdateCheckStarted);
  connect(m_updater, &UpdateChecker::updateCheckFinished, this, &AboutDialog::onUpdateCheckFinished);
  connect(m_updater, &UpdateChecker::updateCheckError, this, &AboutDialog::onUpdateCheckError);
}

AboutDialog::~AboutDialog()
{
  if (m_updater) {
    m_updater->cancel();
  }
}

void AboutDialog::setupUi() {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(22, 20, 22, 18);
  m_layout->setSpacing(12);

  // Header Banner
  auto *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(14);

  auto *iconLabel = new QLabel(this);
  const QPixmap iconPix = AppIcon::pixmap(52, 52);
  if (!iconPix.isNull()) {
    iconLabel->setPixmap(iconPix);
  } else {
    iconLabel->setText(QStringLiteral(
        "<div style=\"background: linear-gradient(135deg, #7f5af0, #2cb67d); "
        "border-radius: 10px; width: 52px; height: 52px; line-height: 52px; "
        "text-align: center; color: #ffffff; font-weight: bold; font-size: "
        "24px;\">E</div>"));
    iconLabel->setTextFormat(Qt::RichText);
  }

  auto *titleContainer = new QWidget(this);
  auto *titleLayout = new QVBoxLayout(titleContainer);
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(2);

  auto *titleRow = new QHBoxLayout();
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(8);
  titleRow->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  m_titleLabel = new QLabel(QStringLiteral("Ensemble"), titleContainer);
  m_versionBadge = new QLabel(titleContainer);
  m_versionBadge->setObjectName(QStringLiteral("versionBadge"));

  titleRow->addWidget(m_titleLabel);
  titleRow->addWidget(m_versionBadge);
  titleRow->addStretch(1);

  m_subTitleLabel = new QLabel(
      QStringLiteral("Next-Gen Fanfiction Editor & Work Skin Composer"),
      titleContainer);

  titleLayout->addLayout(titleRow);
  titleLayout->addWidget(m_subTitleLabel);

  headerLayout->addWidget(iconLabel);
  headerLayout->addWidget(titleContainer, 1);

  m_layout->addLayout(headerLayout);

  // Unified Main Info Panel
  m_infoCard = new QFrame(this);
  m_infoCard->setObjectName(QStringLiteral("infoCard"));
  auto *infoLayout = new QVBoxLayout(m_infoCard);
  infoLayout->setContentsMargins(16, 14, 16, 14);
  infoLayout->setSpacing(8);

  m_aboutText = new QLabel(
      QStringLiteral(
          "A specialized desktop writing suite for Archive of Our Own (AO3) "
          "fanfiction authors. Features a distraction-free WYSIWYG composer, "
          "live AO3 HTML source view, Work Skin CSS recognition, live dual-flavor preview, "
          "visual Chapter Manager, Git-like Snapshots with diffs, and Model Context Protocol (MCP) AI co-authoring."),
      m_infoCard);
  m_aboutText->setWordWrap(true);

  m_divider = new QFrame(m_infoCard);
  m_divider->setFrameShape(QFrame::HLine);

  m_creditsText = new QLabel(
      QStringLiteral("<span style=\"color: #2cb67d; font-weight: "
                     "bold;\">Developer:</span> <span>Mizumo-prjkt</span> &nbsp;•&nbsp; "
                     "<span style=\"color: #7f5af0; font-weight: bold;\">Built "
                     "with:</span> <span>Qt 6, "
                     "Chromium (Qt WebEngine) & Zstandard</span>"),
      m_infoCard);
  m_creditsText->setTextFormat(Qt::RichText);

  m_licenseText =
      new QLabel(QStringLiteral("Released under the MIT License. Dynamically links with Qt 6 (GNU LGPLv3). "
                                "Portions generated with AI assistance."),
                 m_infoCard);
  m_licenseText->setWordWrap(true);

  infoLayout->addWidget(m_aboutText);
  infoLayout->addWidget(m_divider);
  infoLayout->addWidget(m_creditsText);
  infoLayout->addWidget(m_licenseText);

  m_layout->addWidget(m_infoCard);

  // GitHub Releases Updater Card
  m_updateCard = new QFrame(this);
  m_updateCard->setObjectName(QStringLiteral("updateCard"));
  auto *updateLayout = new QHBoxLayout(m_updateCard);
  updateLayout->setContentsMargins(14, 10, 14, 10);
  updateLayout->setSpacing(12);

  m_updateStatusLabel = new QLabel(
      QStringLiteral("<b>Updates:</b> Check for new releases on GitHub."),
      m_updateCard);
  m_updateStatusLabel->setTextFormat(Qt::RichText);
  m_updateStatusLabel->setOpenExternalLinks(true);
  m_updateStatusLabel->setWordWrap(true);

  m_checkUpdateBtn = new QPushButton(QStringLiteral("🔄 Check for Updates"), m_updateCard);
  m_checkUpdateBtn->setCursor(Qt::PointingHandCursor);

  m_getUpdateBtn = new QPushButton(QStringLiteral("🚀 Get Update"), m_updateCard);
  m_getUpdateBtn->setCursor(Qt::PointingHandCursor);
  m_getUpdateBtn->hide();

  updateLayout->addWidget(m_updateStatusLabel, 1);
  updateLayout->addWidget(m_checkUpdateBtn);
  updateLayout->addWidget(m_getUpdateBtn);

  m_layout->addWidget(m_updateCard);

  connect(m_checkUpdateBtn, &QPushButton::clicked, this, &AboutDialog::checkForUpdates);
  connect(m_getUpdateBtn, &QPushButton::clicked, this, &AboutDialog::onOpenReleaseUrl);

  // Action Bar
  m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
  m_closeBtn->setMinimumWidth(100);
  m_closeBtn->setMinimumHeight(32);
  m_closeBtn->setDefault(true);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

  m_layout->addWidget(m_closeBtn, 0, Qt::AlignRight);
}

void AboutDialog::checkForUpdates()
{
  if (m_updater) {
    m_updater->checkForUpdates();
  }
}

void AboutDialog::onUpdateCheckStarted()
{
  m_checkUpdateBtn->setEnabled(false);
  m_checkUpdateBtn->setText(QStringLiteral("Checking…"));
  m_updateStatusLabel->setText(QStringLiteral("<b>Updates:</b> Querying GitHub releases for updates…"));
  m_getUpdateBtn->hide();
}

void AboutDialog::onUpdateCheckFinished(const ReleaseInfo &info)
{
  m_checkUpdateBtn->setEnabled(true);
  m_checkUpdateBtn->setText(QStringLiteral("Check Again"));
  m_latestReleaseUrl = info.htmlUrl;

  if (info.isUpdateAvailable) {
    const QString releaseLink = QStringLiteral("<a href=\"%1\" style=\"color: #2cb67d; font-weight: bold; text-decoration: underline;\">%2</a>")
                                    .arg(info.htmlUrl, info.tagName);
    QString curVer;
#ifdef ENSEMBLE_APP_VERSION
    curVer = QStringLiteral("v") + QStringLiteral(ENSEMBLE_APP_VERSION);
#else
    curVer = QStringLiteral("v1.1.1");
#endif
    m_updateStatusLabel->setText(
        QStringLiteral("<b>🎉 New Update Available:</b> %1 &nbsp;<span style=\"color: #64748b; font-size: 11px;\">(Current: %2)</span>")
            .arg(releaseLink, curVer));
    m_getUpdateBtn->setText(QStringLiteral("🚀 Get %1").arg(info.tagName));
    m_getUpdateBtn->show();
  } else {
    QString curVer;
#ifdef ENSEMBLE_APP_VERSION
    curVer = QStringLiteral("v") + QStringLiteral(ENSEMBLE_APP_VERSION);
#else
    curVer = QStringLiteral("v1.1.1");
#endif
    m_updateStatusLabel->setText(
        QStringLiteral("<b>✓ Ensemble is up to date:</b> You are running the latest release (<b>%1</b>).")
            .arg(!info.tagName.isEmpty() ? info.tagName : curVer));
    m_getUpdateBtn->hide();
  }
}

void AboutDialog::onUpdateCheckError(const QString &errorMessage)
{
  m_checkUpdateBtn->setEnabled(true);
  m_checkUpdateBtn->setText(QStringLiteral("Retry"));
  m_updateStatusLabel->setText(
      QStringLiteral("<b>Updates:</b> %1 &nbsp;•&nbsp; <a href=\"https://github.com/Mizumo-prjkt/ensemble/releases/latest\" style=\"color: #7f5af0;\">View Releases</a>")
          .arg(errorMessage));
  m_getUpdateBtn->hide();
}

void AboutDialog::onOpenReleaseUrl()
{
  const QUrl url = !m_latestReleaseUrl.isEmpty()
      ? QUrl(m_latestReleaseUrl)
      : QUrl(QStringLiteral("https://github.com/Mizumo-prjkt/ensemble/releases/latest"));
  QDesktopServices::openUrl(url);
}

void AboutDialog::updateTheme() {
  const bool isDark = ThemeManager::instance().isDarkMode();
  QString cleanVersion;
#ifdef ENSEMBLE_APP_VERSION
  cleanVersion = QStringLiteral("v") + QStringLiteral(ENSEMBLE_APP_VERSION);
#else
  cleanVersion = QStringLiteral("v1.1.1");
#endif

  setStyleSheet(isDark ? QStringLiteral(
      "QDialog {"
      "  background-color: #14151a;"
      "  color: #f8f8f2;"
      "  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
      "}"
  ) : QStringLiteral(
      "QDialog {"
      "  background-color: #f8fafc;"
      "  color: #0f172a;"
      "  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
      "}"
  ));

  if (m_titleLabel) {
    m_titleLabel->setStyleSheet(isDark
        ? QStringLiteral("color: #fffffe; font-size: 20px; font-weight: 700; background: transparent;")
        : QStringLiteral("color: #0f172a; font-size: 20px; font-weight: 700; background: transparent;"));
  }

  if (m_versionBadge) {
    m_versionBadge->setText(cleanVersion);
    m_versionBadge->setToolTip(EnsembleDebug::fullVersion());
    m_versionBadge->setStyleSheet(isDark ? QStringLiteral(
        "QLabel#versionBadge {"
        "  background-color: #282a36;"
        "  color: #bd93f9;"
        "  border: 1px solid #44475a;"
        "  border-radius: 6px;"
        "  padding: 2px 7px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
    ) : QStringLiteral(
        "QLabel#versionBadge {"
        "  background-color: #e0e7ff;"
        "  color: #4338ca;"
        "  border: 1px solid #c7d2fe;"
        "  border-radius: 6px;"
        "  padding: 2px 7px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
    ));
  }

  if (m_subTitleLabel) {
    m_subTitleLabel->setStyleSheet(isDark ? QStringLiteral("color: #94a3b8; font-size: 12px; background: transparent;")
                                          : QStringLiteral("color: #64748b; font-size: 12px; background: transparent;"));
  }

  if (m_infoCard) {
    m_infoCard->setStyleSheet(isDark ? QStringLiteral(
        "#infoCard {"
        "  background-color: #1a1b23;"
        "  border: 1px solid #282a36;"
        "  border-radius: 10px;"
        "}"
    ) : QStringLiteral(
        "#infoCard {"
        "  background-color: #ffffff;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 10px;"
        "}"
    ));
  }

  if (m_updateCard) {
    m_updateCard->setStyleSheet(isDark ? QStringLiteral(
        "#updateCard {"
        "  background-color: #1a1b23;"
        "  border: 1px solid #282a36;"
        "  border-radius: 10px;"
        "}"
    ) : QStringLiteral(
        "#updateCard {"
        "  background-color: #ffffff;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 10px;"
        "}"
    ));
  }

  if (m_updateStatusLabel) {
    m_updateStatusLabel->setStyleSheet(isDark ? QStringLiteral("color: #f8f8f2; font-size: 12px; background: transparent;")
                                              : QStringLiteral("color: #1e293b; font-size: 12px; background: transparent;"));
  }

  if (m_checkUpdateBtn) {
    const int rad = ThemeManager::instance().buttonRadius();
    m_checkUpdateBtn->setStyleSheet(isDark ? QStringLiteral(
        "QPushButton {"
        "  background-color: #282a36;"
        "  color: #f8f8f2;"
        "  border: 1px solid #44475a;"
        "  border-radius: %1px;"
        "  padding: 6px 12px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #3b3e51; }"
        "QPushButton:disabled { color: #6272a4; background-color: #1e1f29; }"
    ).arg(rad) : QStringLiteral(
        "QPushButton {"
        "  background-color: #f1f5f9;"
        "  color: #1e293b;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: %1px;"
        "  padding: 6px 12px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background-color: #e2e8f0; }"
        "QPushButton:disabled { color: #94a3b8; background-color: #f8fafc; }"
    ).arg(rad));
  }

  if (m_getUpdateBtn) {
    const int rad = ThemeManager::instance().buttonRadius();
    m_getUpdateBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #2cb67d;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %1px;"
        "  padding: 6px 14px;"
        "  font-size: 11px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background-color: #249966; }"
    ).arg(rad));
  }

  if (m_aboutText) {
    m_aboutText->setStyleSheet(isDark ? QStringLiteral("color: #f8f8f2; font-size: 12px; line-height: 1.5; background: transparent; border: none;")
                                      : QStringLiteral("color: #334155; font-size: 12px; line-height: 1.5; background: transparent; border: none;"));
  }

  if (m_divider) {
    m_divider->setStyleSheet(isDark ? QStringLiteral("background-color: #282a36; border: none; max-height: 1px; min-height: 1px;")
                                    : QStringLiteral("background-color: #e2e8f0; border: none; max-height: 1px; min-height: 1px;"));
  }

  if (m_creditsText) {
    m_creditsText->setText(isDark ? QStringLiteral(
        "<span style=\"color: #50fa7b; font-weight: bold;\">Developer:</span> <span style=\"color: #f8f8f2;\">Mizumo-prjkt</span> &nbsp;•&nbsp; "
        "<span style=\"color: #bd93f9; font-weight: bold;\">Built with:</span> <span style=\"color: #f8f8f2;\">Qt 6, "
        "Chromium (Qt WebEngine) & Zstandard</span>"
    ) : QStringLiteral(
        "<span style=\"color: #065f46; font-weight: bold;\">Developer:</span> <span style=\"color: #0f172a;\">Mizumo-prjkt</span> &nbsp;•&nbsp; "
        "<span style=\"color: #4338ca; font-weight: bold;\">Built with:</span> <span style=\"color: #0f172a;\">Qt 6, "
        "Chromium (Qt WebEngine) & Zstandard</span>"
    ));
    m_creditsText->setStyleSheet(isDark ? QStringLiteral("color: #f8f8f2; font-size: 11px; background: transparent; border: none;")
                                        : QStringLiteral("color: #0f172a; font-size: 11px; background: transparent; border: none;"));
  }

  if (m_licenseText) {
    m_licenseText->setStyleSheet(isDark ? QStringLiteral("color: #94a3b8; font-size: 11px; background: transparent; border: none;")
                                        : QStringLiteral("color: #334155; font-size: 11px; background: transparent; border: none;"));
  }

  if (m_closeBtn) {
    const QString accent = ThemeManager::instance().accentColor().name();
    const QString accentHover = ThemeManager::instance().accentHoverColor().name();
    const int btnRad = ThemeManager::instance().buttonRadius();
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "  border-radius: %2px;"
        "  border: none;"
        "  padding: 6px 20px;"
        "}"
        "QPushButton:hover { background-color: %3; }"
    ).arg(accent).arg(btnRad).arg(accentHover));
  }
}