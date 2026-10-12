#include "AboutDialog.h"
#include "AppIcon.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("About Ensemble"));
  setWindowIcon(AppIcon::icon());
  setModal(true);
  setFixedSize(520, 370);
  setupUi();
  updateTheme();

  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
          [this](AppTheme) { updateTheme(); });
  connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this,
          [this](SkinFlavor) { updateTheme(); });
}

void AboutDialog::setupUi() {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(22, 22, 22, 22);
  m_layout->setSpacing(16);

  // Header Banner
  auto *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(16);

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
  titleLayout->setSpacing(3);

  const QString fullVersion = EnsembleDebug::fullVersion();
  const QString badgeColor = fullVersion.contains(QStringLiteral("-dirty"))
                                 ? QStringLiteral("#e53170")
                                 : QStringLiteral("#7f5af0");

  m_titleLabel = new QLabel(titleContainer);
  m_titleLabel->setTextFormat(Qt::RichText);

  m_subTitleLabel = new QLabel(
      QStringLiteral(
          "Next-Gen Fanfiction Editor & Work Skin Composer"),
      titleContainer);

  titleLayout->addWidget(m_titleLabel);
  titleLayout->addWidget(m_subTitleLabel);

  headerLayout->addWidget(iconLabel);
  headerLayout->addWidget(titleContainer, 1);

  m_layout->addLayout(headerLayout);

  // Unified Main Info Panel
  m_infoCard = new QFrame(this);
  m_infoCard->setObjectName(QStringLiteral("infoCard"));
  auto *infoLayout = new QVBoxLayout(m_infoCard);
  infoLayout->setContentsMargins(16, 16, 16, 16);
  infoLayout->setSpacing(10);

  m_aboutText = new QLabel(
      QStringLiteral(
          "A specialized desktop writing suite for Archive of Our Own (AO3) "
          "fanfiction authors. "
          "Features a distraction-free WYSIWYG composer, live AO3 HTML source "
          "view, Work Skin CSS recognition, "
          "ZSTD project archiving, live preview, and spell checking."),
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
      new QLabel(QStringLiteral("Portions generated with AI assistance. "
                                "Released under the MIT License."),
                 m_infoCard);

  infoLayout->addWidget(m_aboutText);
  infoLayout->addWidget(m_divider);
  infoLayout->addWidget(m_creditsText);
  infoLayout->addWidget(m_licenseText);

  m_layout->addWidget(m_infoCard, 1);

  // Action Bar
  m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
  m_closeBtn->setMinimumWidth(100);
  m_closeBtn->setMinimumHeight(32);
  m_closeBtn->setDefault(true);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

  m_layout->addWidget(m_closeBtn, 0, Qt::AlignRight);
}

void AboutDialog::updateTheme() {
  const bool isDark = ThemeManager::instance().isDarkMode();
  const QString fullVersion = EnsembleDebug::fullVersion();
  const QString badgeColor = fullVersion.contains(QStringLiteral("-dirty"))
                                 ? QStringLiteral("#e53170")
                                 : (isDark ? QStringLiteral("#7f5af0") : QStringLiteral("#6366f1"));

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
    m_titleLabel->setText(
        QString(
            "<span style=\"color: %1; font-size: 22px; font-weight: 700;\">Ensemble</span> "
            "<span style=\"background-color: %2; color: #ffffff; padding: 3px 8px; border-radius: 8px; font-size: 11px; font-weight: 600;\">v%3</span>")
            .arg(isDark ? QStringLiteral("#fffffe") : QStringLiteral("#0f172a"), badgeColor, fullVersion));
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