#include "MainMenuWindow.h"
#include "themes/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

MainMenuWidget::MainMenuWidget(QWidget *parent) : QWidget(parent) {
  setupUi();
  updateTheme();

  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
          [this](AppTheme) { updateTheme(); });
  connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this,
          [this](SkinFlavor) { updateTheme(); });
}

void MainMenuWidget::setupUi() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  auto *scrollArea = new QScrollArea(this);
  scrollArea->setObjectName(QStringLiteral("menuScrollArea"));
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setAlignment(Qt::AlignCenter);

  auto *containerWidget = new QWidget(scrollArea);
  containerWidget->setStyleSheet(QStringLiteral("background: transparent;"));

  auto *outerLayout = new QVBoxLayout(containerWidget);
  outerLayout->setContentsMargins(16, 16, 16, 16);

  // Center layout to hold the card neatly
  auto *centerRow = new QHBoxLayout();
  centerRow->addStretch(1);

  auto *centerCard = new QFrame(containerWidget);
  centerCard->setObjectName(QStringLiteral("centerCard"));
  centerCard->setMinimumWidth(440);
  centerCard->setMaximumWidth(620);
  centerCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

  m_layout = new QVBoxLayout(centerCard);
  m_layout->setContentsMargins(24, 24, 24, 20);
  m_layout->setSpacing(12);

  // Title banner with quick theme toggle
  auto *topRow = new QHBoxLayout();
  topRow->setSpacing(8);

  auto *titleCol = new QVBoxLayout();
  titleCol->setSpacing(2);

  m_titleLabel = new QLabel(QStringLiteral("Ensemble"), centerCard);
  m_subtitleLabel = new QLabel(
      QStringLiteral(
          "A specialized rich editor for Archive of Our Own fanfiction"),
      centerCard);

  titleCol->addWidget(m_titleLabel);
  titleCol->addWidget(m_subtitleLabel);
  topRow->addLayout(titleCol, 1);

  m_themeToggleBtn = new QPushButton(centerCard);
  m_themeToggleBtn->setObjectName(QStringLiteral("themeToggleBtn"));
  m_themeToggleBtn->setCursor(Qt::PointingHandCursor);
  m_themeToggleBtn->setToolTip(QStringLiteral("Toggle Dark / Light Theme"));
  connect(m_themeToggleBtn, &QPushButton::clicked, this,
          []() { ThemeManager::instance().toggleTheme(); });
  topRow->addWidget(m_themeToggleBtn, 0, Qt::AlignTop);

  m_layout->addLayout(topRow);

  // Info card
  m_infoBox = new QFrame(centerCard);
  m_infoBox->setObjectName(QStringLiteral("infoBox"));
  m_infoBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  auto *infoLayout = new QVBoxLayout(m_infoBox);
  infoLayout->setContentsMargins(16, 12, 16, 14);
  infoLayout->setSpacing(4);

  m_motdTitle = new QLabel(QStringLiteral("Welcome & Quick Info"), m_infoBox);

  m_motdText = new QLabel(
      QStringLiteral("Write fanfiction tailored for AO3 with live HTML "
                     "preview, work skin CSS recognition, "
                     "autoblock expansions, and built-in style linting."),
      m_infoBox);
  m_motdText->setWordWrap(true);
  m_motdText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  infoLayout->addWidget(m_motdTitle);
  infoLayout->addWidget(m_motdText);

  m_layout->addWidget(m_infoBox);

  // Quick Actions
  m_actionsLabel = new QLabel(QStringLiteral("QUICK ACTIONS"), centerCard);
  m_layout->addWidget(m_actionsLabel);

  m_continueBtn = createActionButton(
      QStringLiteral("continueBtn"),
      QStringLiteral("✍️  Continue Writing"),
      QStringLiteral("Resume working on active project"),
      &m_continueTitleLabel, &m_continueSubLabel);
  m_continueBtn->setVisible(false);

  m_newBtn = createActionButton(
      QStringLiteral("newBtn"),
      QStringLiteral("✨  New Project"),
      QStringLiteral("Start a fresh fanfiction document with standard AO3 chapter layout"));

  m_openBtn = createActionButton(
      QStringLiteral("openBtn"),
      QStringLiteral("📂  Open Project…"),
      QStringLiteral("Load an existing .ao3proj file archive from disk"));

  m_saveBtn = createActionButton(
      QStringLiteral("saveBtn"),
      QStringLiteral("💾  Save Project"),
      QStringLiteral("Save all chapters, metadata, and Work Skin to a unified project file"));
  m_saveBtn->setEnabled(false);

  m_closeBtn = createActionButton(
      QStringLiteral("closeBtn"),
      QStringLiteral("🚪  Close Project"),
      QStringLiteral("Safely close current work and return to welcome state"));
  m_closeBtn->setVisible(false);

  m_helpBtn = createActionButton(
      QStringLiteral("helpBtn"),
      QStringLiteral("📖  Help & User Guide…"),
      QStringLiteral("Documentation, AO3 formatting rules, Work Skins, and shortcuts"));

  m_settingsBtn = createActionButton(
      QStringLiteral("settingsBtn"),
      QStringLiteral("⚙️  Preferences…"),
      QStringLiteral("Customize theme, WebEngine, and AI settings"));

  m_layout->addWidget(m_continueBtn);
  m_layout->addWidget(m_newBtn);
  m_layout->addWidget(m_openBtn);
  m_layout->addWidget(m_saveBtn);
  m_layout->addWidget(m_closeBtn);
  m_layout->addWidget(m_helpBtn);
  m_layout->addWidget(m_settingsBtn);

  m_layout->addSpacing(4);

  // Footer
  auto *bottomRow = new QHBoxLayout();
  bottomRow->addStretch();
  m_layout->addLayout(bottomRow);

  centerRow->addWidget(centerCard);
  centerRow->addStretch(1);

  outerLayout->addStretch(1);
  outerLayout->addLayout(centerRow);
  outerLayout->addStretch(1);

  scrollArea->setWidget(containerWidget);
  mainLayout->addWidget(scrollArea);

  // Connections
  connect(m_continueBtn, &QPushButton::clicked, this,
          &MainMenuWidget::startWritingRequested);
  connect(m_newBtn, &QPushButton::clicked, this,
          &MainMenuWidget::newProjectRequested);
  connect(m_openBtn, &QPushButton::clicked, this,
          &MainMenuWidget::openProjectRequested);
  connect(m_saveBtn, &QPushButton::clicked, this,
          &MainMenuWidget::saveProjectRequested);
  connect(m_closeBtn, &QPushButton::clicked, this,
          &MainMenuWidget::closeProjectRequested);
  connect(m_helpBtn, &QPushButton::clicked, this,
          &MainMenuWidget::helpRequested);
  connect(m_settingsBtn, &QPushButton::clicked, this,
          &MainMenuWidget::settingsRequested);
}

QPushButton *MainMenuWidget::createActionButton(const QString &objectName,
                                                const QString &iconAndTitle,
                                                const QString &subtext,
                                                QLabel **outTitleLabel,
                                                QLabel **outSubtextLabel) {
  auto *btn = new QPushButton(this);
  btn->setObjectName(objectName);
  btn->setProperty("class", "actionBtn");
  btn->setCursor(Qt::PointingHandCursor);
  btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  auto *btnLayout = new QVBoxLayout(btn);
  btnLayout->setContentsMargins(16, 12, 16, 12);
  btnLayout->setSpacing(4);

  auto *titleLbl = new QLabel(iconAndTitle, btn);
  titleLbl->setObjectName(QStringLiteral("actionBtnTitle"));
  titleLbl->setTextFormat(Qt::PlainText);
  titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

  auto *subLbl = new QLabel(subtext, btn);
  subLbl->setObjectName(QStringLiteral("actionBtnSubtext"));
  subLbl->setTextFormat(Qt::PlainText);
  subLbl->setWordWrap(true);
  subLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

  btnLayout->addWidget(titleLbl);
  btnLayout->addWidget(subLbl);

  if (outTitleLabel)
    *outTitleLabel = titleLbl;
  if (outSubtextLabel)
    *outSubtextLabel = subLbl;

  return btn;
}

void MainMenuWidget::updateTheme() {
  const bool isDark = ThemeManager::instance().isDarkMode();
  setStyleSheet(ThemeManager::instance().getMainMenuStyleSheet());

  if (m_themeToggleBtn) {
    m_themeToggleBtn->setText(isDark ? QStringLiteral("🌙 Dark")
                                     : QStringLiteral("☀️ Light"));
    m_themeToggleBtn->setStyleSheet(
        isDark
            ? QStringLiteral("QPushButton { background-color: #282a36; color: "
                             "#f8f8f2; border: 1px solid #44475a; "
                             "border-radius: 6px; padding: 4px 10px; "
                             "font-size: 11px; font-weight: 600; }"
                             "QPushButton:hover { background-color: #343746; "
                             "color: #50fa7b; border-color: #6272a4; }")
            : QStringLiteral("QPushButton { background-color: #f1f5f9; color: "
                             "#0f172a; border: 1px solid #cbd5e1; "
                             "border-radius: 6px; padding: 4px 10px; "
                             "font-size: 11px; font-weight: 600; }"
                             "QPushButton:hover { background-color: #e2e8f0; "
                             "color: #4338ca; border-color: #94a3b8; }"));
  }

  if (m_titleLabel) {
    m_titleLabel->setStyleSheet(
        isDark ? QStringLiteral("font-size: 24px; font-weight: 800; color: "
                                "#8be9fd; background: transparent;")
               : QStringLiteral("font-size: 24px; font-weight: 800; color: "
                                "#4f46e5; background: transparent;"));
  }

  if (m_subtitleLabel) {
    m_subtitleLabel->setStyleSheet(
        isDark ? QStringLiteral("font-size: 12px; color: #6272a4; background: "
                                "transparent; margin-bottom: 2px;")
               : QStringLiteral("font-size: 12px; color: #64748b; background: "
                                "transparent; margin-bottom: 2px;"));
  }

  if (m_motdTitle) {
    m_motdTitle->setStyleSheet(
        isDark ? QStringLiteral("font-weight: 700; font-size: 13px; color: "
                                "#bd93f9; background: transparent;")
               : QStringLiteral("font-weight: 700; font-size: 13px; color: "
                                "#6366f1; background: transparent;"));
  }

  if (m_motdText) {
    m_motdText->setStyleSheet(
        isDark ? QStringLiteral("font-size: 12px; color: #f8f8f2; background: "
                                "transparent; line-height: 1.4;")
               : QStringLiteral("font-size: 12px; color: #1e293b; background: "
                                "transparent; line-height: 1.4;"));
  }

  if (m_actionsLabel) {
    m_actionsLabel->setStyleSheet(
        isDark ? QStringLiteral(
                     "font-size: 11px; font-weight: 700; color: #6272a4; "
                     "background: transparent; margin-top: 2px;")
               : QStringLiteral(
                     "font-size: 11px; font-weight: 700; color: #64748b; "
                     "background: transparent; margin-top: 2px;"));
  }

  const QString primaryText = ThemeManager::instance().primaryTextColor().name();
  const QString mutedText = ThemeManager::instance().mutedTextColor().name();

  const QString titleStyle = QStringLiteral(
      "QLabel#actionBtnTitle { font-size: 13px; font-weight: 700; color: %1; background: transparent; }")
      .arg(primaryText);
  const QString subtextStyle = QStringLiteral(
      "QLabel#actionBtnSubtext { font-size: 11px; color: %1; background: transparent; line-height: 1.35; }")
      .arg(mutedText);

  for (auto *lbl : findChildren<QLabel *>(QStringLiteral("actionBtnTitle"))) {
    lbl->setStyleSheet(titleStyle);
  }
  for (auto *lbl : findChildren<QLabel *>(QStringLiteral("actionBtnSubtext"))) {
    lbl->setStyleSheet(subtextStyle);
  }
}

void MainMenuWidget::setProjectState(bool hasActiveProject, const QString &title) {
  m_hasActiveProject = hasActiveProject;
  if (m_saveBtn) {
    m_saveBtn->setEnabled(hasActiveProject);
  }
  if (m_continueBtn) {
    m_continueBtn->setVisible(hasActiveProject);
    if (hasActiveProject && m_continueSubLabel) {
      const QString displayTitle = title.trimmed().isEmpty() ? QStringLiteral("Untitled Work") : title.trimmed();
      m_continueSubLabel->setText(QStringLiteral("Resume working on \"%1\"").arg(displayTitle));
    }
  }
  if (m_closeBtn) {
    m_closeBtn->setVisible(hasActiveProject);
  }
}