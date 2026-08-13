#include "MainMenuWindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MainMenuWidget::MainMenuWidget(QWidget *parent)
    : QWidget(parent)
{
    // Modern high-contrast dark theme stylesheet
    setStyleSheet(QStringLiteral(
        "MainMenuWidget {"
        "  background-color: #14151a;"
        "  color: #f8f8f2;"
        "  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "}"
        "QFrame#centerCard {"
        "  background-color: #21222c;"
        "  border: 1px solid #343746;"
        "  border-radius: 12px;"
        "}"
        "QFrame#infoBox {"
        "  background-color: #282a36;"
        "  border: 1px solid #44475a;"
        "  border-radius: 8px;"
        "}"
        "QPushButton.actionBtn {"
        "  background-color: #282a36;"
        "  color: #f8f8f2;"
        "  border: 1px solid #44475a;"
        "  border-radius: 6px;"
        "  padding: 12px 16px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  text-align: left;"
        "}"
        "QPushButton.actionBtn:hover {"
        "  background-color: #343746;"
        "  border-color: #6272a4;"
        "  color: #50fa7b;"
        "}"
        "QPushButton.actionBtn:pressed {"
        "  background-color: #44475a;"
        "}"
        "QPushButton.startBtn {"
        "  background-color: #bd93f9;"
        "  color: #14151a;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 10px 22px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
        "QPushButton.startBtn:hover {"
        "  background-color: #50fa7b;"
        "  color: #14151a;"
        "}"
    ));

    setupUi();
}

void MainMenuWidget::setupUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Center layout to hold the card neatly
    auto *centerRow = new QHBoxLayout();
    centerRow->addStretch(1);

    auto *centerCard = new QFrame(this);
    centerCard->setObjectName(QStringLiteral("centerCard"));
    centerCard->setFixedWidth(540);

    m_layout = new QVBoxLayout(centerCard);
    m_layout->setContentsMargins(28, 28, 28, 24);
    m_layout->setSpacing(16);

    // Title banner
    auto *titleLabel = new QLabel(QStringLiteral("Ensemble"), centerCard);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 800; color: #8be9fd; background: transparent;"));

    auto *subtitleLabel = new QLabel(QStringLiteral("A specialized rich editor for Archive of Our Own fanfiction"), centerCard);
    subtitleLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #6272a4; background: transparent; margin-bottom: 4px;"));

    m_layout->addWidget(titleLabel);
    m_layout->addWidget(subtitleLabel);

    // Info card
    auto *infoBox = new QFrame(centerCard);
    infoBox->setObjectName(QStringLiteral("infoBox"));
    auto *infoLayout = new QVBoxLayout(infoBox);
    infoLayout->setContentsMargins(16, 14, 16, 14);
    infoLayout->setSpacing(6);

    auto *motdTitle = new QLabel(QStringLiteral("Welcome & Quick Info"), infoBox);
    motdTitle->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 13px; color: #bd93f9; background: transparent;"));

    auto *motdText = new QLabel(
        QStringLiteral("Write fanfiction tailored for AO3 with live HTML preview, work skin CSS recognition, "
                       "autoblock expansions, and built-in style linting."),
        infoBox);
    motdText->setStyleSheet(QStringLiteral("font-size: 12px; color: #f8f8f2; background: transparent; line-height: 1.4;"));
    motdText->setWordWrap(true);

    infoLayout->addWidget(motdTitle);
    infoLayout->addWidget(motdText);

    m_layout->addWidget(infoBox);

    // Quick Actions
    auto *actionsLabel = new QLabel(QStringLiteral("QUICK ACTIONS"), centerCard);
    actionsLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #6272a4; background: transparent; margin-top: 4px;"));
    m_layout->addWidget(actionsLabel);

    m_newBtn = new QPushButton(QStringLiteral("✨  New Project\n     Start a fresh fanfiction document"), centerCard);
    m_newBtn->setProperty("class", "actionBtn");

    m_openBtn = new QPushButton(QStringLiteral("📂  Open Project…\n     Load an existing .ao3proj file"), centerCard);
    m_openBtn->setProperty("class", "actionBtn");

    m_saveBtn = new QPushButton(QStringLiteral("💾  Save Project\n     Save current work to file"), centerCard);
    m_saveBtn->setProperty("class", "actionBtn");

    m_layout->addWidget(m_newBtn);
    m_layout->addWidget(m_openBtn);
    m_layout->addWidget(m_saveBtn);

    m_layout->addSpacing(8);

    // Footer with Start Writing button
    auto *bottomRow = new QHBoxLayout();
    bottomRow->addStretch();

    m_startWritingBtn = new QPushButton(QStringLiteral("Start Writing  →"), centerCard);
    m_startWritingBtn->setProperty("class", "startBtn");
    bottomRow->addWidget(m_startWritingBtn);

    m_layout->addLayout(bottomRow);

    centerRow->addWidget(centerCard);
    centerRow->addStretch(1);

    outerLayout->addStretch(1);
    outerLayout->addLayout(centerRow);
    outerLayout->addStretch(1);

    // Connections
    connect(m_newBtn, &QPushButton::clicked, this, &MainMenuWidget::newProjectRequested);
    connect(m_openBtn, &QPushButton::clicked, this, &MainMenuWidget::openProjectRequested);
    connect(m_saveBtn, &QPushButton::clicked, this, &MainMenuWidget::saveProjectRequested);
    connect(m_startWritingBtn, &QPushButton::clicked, this, &MainMenuWidget::startWritingRequested);
}