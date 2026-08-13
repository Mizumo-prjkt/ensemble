#include "AboutDialog.h"
#include "AppIcon.h"

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

#ifndef ENSEMBLE_FULL_VERSION
#define ENSEMBLE_FULL_VERSION "1.1.0 (Debug)"
#endif

  const QString fullVersion = QStringLiteral(ENSEMBLE_FULL_VERSION);
  const QString badgeColor = fullVersion.contains(QStringLiteral("-dirty"))
                                 ? QStringLiteral("#e53170")
                                 : QStringLiteral("#7f5af0");

  auto *titleLabel = new QLabel(
      QString(
          "<span style=\"color: #fffffe; font-size: 22px; font-weight: "
          "700;\">Ensemble</span> "
          "<span style=\"background-color: %1; color: #ffffff; padding: 3px "
          "8px; "
          "border-radius: 8px; font-size: 11px; font-weight: 600;\">v%2</span>")
          .arg(badgeColor, fullVersion),
      titleContainer);
  titleLabel->setTextFormat(Qt::RichText);

  auto *subTitleLabel = new QLabel(
      QStringLiteral(
          "<span style=\"color: #94a3b8; font-size: 12px;\">Next-Gen "
          "Fanfiction Editor & Work Skin Composer</span>"),
      titleContainer);
  subTitleLabel->setTextFormat(Qt::RichText);

  titleLayout->addWidget(titleLabel);
  titleLayout->addWidget(subTitleLabel);

  headerLayout->addWidget(iconLabel);
  headerLayout->addWidget(titleContainer, 1);

  m_layout->addLayout(headerLayout);

  // Unified Main Info Panel with scoped ID selector
  auto *infoCard = new QFrame(this);
  infoCard->setObjectName(QStringLiteral("infoCard"));
  infoCard->setStyleSheet(QStringLiteral("#infoCard {"
                                         "  background-color: #16161a;"
                                         "  border: 1px solid #2e2f3e;"
                                         "  border-radius: 10px;"
                                         "}"));
  auto *infoLayout = new QVBoxLayout(infoCard);
  infoLayout->setContentsMargins(16, 16, 16, 16);
  infoLayout->setSpacing(10);

  auto *aboutText = new QLabel(
      QStringLiteral(
          "A specialized desktop writing suite for Archive of Our Own (AO3) "
          "fanfiction authors. "
          "Features a distraction-free WYSIWYG composer, live AO3 HTML source "
          "view, Work Skin CSS recognition, "
          "ZSTD project archiving, live preview, and spell checking."),
      infoCard);
  aboutText->setStyleSheet(
      QStringLiteral("color: #fffffe; font-size: 12px; line-height: 1.5; "
                     "background: transparent; border: none;"));
  aboutText->setWordWrap(true);

  auto *divider = new QFrame(infoCard);
  divider->setFrameShape(QFrame::HLine);
  divider->setStyleSheet(
      QStringLiteral("background-color: #2e2f3e; border: none; max-height: "
                     "1px; min-height: 1px;"));

  auto *creditsText = new QLabel(
      QStringLiteral("<span style=\"color: #2cb67d; font-weight: "
                     "bold;\">Developer:</span> <span style=\"color: "
                     "#94a3b8;\">Mizumo-prjkt</span> &nbsp;•&nbsp; "
                     "<span style=\"color: #7f5af0; font-weight: bold;\">Built "
                     "with:</span> <span style=\"color: #94a3b8;\">Qt 6, "
                     "Chromium (Qt WebEngine) & Zstandard</span>"),
      infoCard);
  creditsText->setTextFormat(Qt::RichText);
  creditsText->setStyleSheet(QStringLiteral(
      "font-size: 11px; background: transparent; border: none;"));

  auto *licenseText =
      new QLabel(QStringLiteral("Portions generated with AI assistance. "
                                "Released under the MIT License."),
                 infoCard);
  licenseText->setStyleSheet(
      QStringLiteral("color: #72757e; font-size: 11px; background: "
                     "transparent; border: none;"));

  infoLayout->addWidget(aboutText);
  infoLayout->addWidget(divider);
  infoLayout->addWidget(creditsText);
  infoLayout->addWidget(licenseText);

  m_layout->addWidget(infoCard, 1);

  // Action Bar
  auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
  closeBtn->setMinimumWidth(100);
  closeBtn->setMinimumHeight(32);
  closeBtn->setStyleSheet(QStringLiteral("QPushButton {"
                                         "  background-color: #7f5af0;"
                                         "  color: #ffffff;"
                                         "  font-weight: 600;"
                                         "  font-size: 12px;"
                                         "  border-radius: 6px;"
                                         "  border: none;"
                                         "  padding: 5px 16px;"
                                         "}"
                                         "QPushButton:hover {"
                                         "  background-color: #6c47d9;"
                                         "}"
                                         "QPushButton:pressed {"
                                         "  background-color: #5935c4;"
                                         "}"));
  closeBtn->setDefault(true);

  m_layout->addWidget(closeBtn, 0, Qt::AlignRight);

  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}