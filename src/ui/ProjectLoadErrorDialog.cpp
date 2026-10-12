#include "ProjectLoadErrorDialog.h"
#include "AppIcon.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

ProjectLoadErrorDialog::ProjectLoadErrorDialog(const ProjectLoadReport &report, QWidget *parent)
    : QDialog(parent), m_report(report) {
    setWindowTitle(QStringLiteral("Unable to Open Project"));
    setWindowIcon(AppIcon::icon());
    setModal(true);
    setMinimumWidth(560);
    setSizeGripEnabled(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(14);

    // 1. Header Layout (Icon + Title)
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(14);

    auto *iconLabel = new QLabel(this);
    iconLabel->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(44, 44));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *headerTextLayout = new QVBoxLayout();
    headerTextLayout->setSpacing(4);

    auto *titleLabel = new QLabel(QStringLiteral("<h3 style='margin:0;'>Could Not Open Project</h3>"), this);
    titleLabel->setTextFormat(Qt::RichText);
    headerTextLayout->addWidget(titleLabel);

    const QString fileName = QFileInfo(report.filePath).fileName();
    auto *subtitleLabel = new QLabel(
        QStringLiteral("<span style='color:%1; font-weight:600;'>%2</span>")
            .arg(ThemeManager::instance().mutedTextColor().name(),
                 fileName.isEmpty() ? QStringLiteral("Ensemble Project") : fileName),
        this);
    subtitleLabel->setTextFormat(Qt::RichText);
    headerTextLayout->addWidget(subtitleLabel);

    headerLayout->addLayout(headerTextLayout, 1);
    mainLayout->addLayout(headerLayout);

    // 2. Summary Card
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("errorSummaryCard"));
    card->setStyleSheet(QStringLiteral(
        "#errorSummaryCard { background: %1; border: 1px solid %2; border-radius: %3px; padding: 12px; }")
        .arg(ThemeManager::instance().cardBgColor().name(),
             ThemeManager::instance().borderColor().name(),
             QString::number(ThemeManager::instance().cardRadius())));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(8);

    // Stage Badge
    QString stageStr;
    switch (report.failedStage) {
    case ValidationStage::FileAccess:
        stageStr = QStringLiteral("Stage 0: File Access / Permissions Verification");
        break;
    case ValidationStage::MagicBytesCheck:
        stageStr = QStringLiteral("Stage 1: Magic Bytes Signature Check Failed");
        break;
    case ValidationStage::StructureCheck:
        stageStr = QStringLiteral("Stage 2: Structural Integrity & Manifest Check Failed");
        break;
    case ValidationStage::RenderCheck:
        stageStr = QStringLiteral("Stage 3: Workspace Model Instantiation Failed");
        break;
    default:
        stageStr = QStringLiteral("Project Verification Failed");
        break;
    }

    auto *stageBadge = new QLabel(
        QStringLiteral("<span style='background:#ef4444; color:#ffffff; font-size:11px; font-weight:bold; "
                       "padding:3px 8px; border-radius:4px;'>%1</span>").arg(stageStr),
        card);
    stageBadge->setTextFormat(Qt::RichText);
    cardLayout->addWidget(stageBadge);

    // Error Message
    auto *errDesc = new QLabel(QStringLiteral("<b>Reason:</b> %1").arg(report.summaryError), card);
    errDesc->setWordWrap(true);
    cardLayout->addWidget(errDesc);

    if (!report.remediationHint.isEmpty()) {
        auto *hintLabel = new QLabel(
            QStringLiteral("<span style='color:%1;'>💡 %2</span>")
                .arg(ThemeManager::instance().mutedTextColor().name(), report.remediationHint),
            card);
        hintLabel->setWordWrap(true);
        cardLayout->addWidget(hintLabel);
    }

    // Path
    auto *pathLabel = new QLabel(
        QStringLiteral("<span style='font-size:11px; color:%1;'>Path: %2</span>")
            .arg(ThemeManager::instance().mutedTextColor().name(), report.filePath),
        card);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    cardLayout->addWidget(pathLabel);

    mainLayout->addWidget(card);

    // 3. Details Toggle Button
    auto *btnRow = new QHBoxLayout();
    m_detailsToggleBtn = new QPushButton(QStringLiteral("Show Detailed Explanation ▾"), this);
    m_detailsToggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailsToggleBtn, &QPushButton::clicked, this, &ProjectLoadErrorDialog::toggleDetails);
    btnRow->addWidget(m_detailsToggleBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // 4. Monospace Details Area (Hidden by default)
    m_detailsEdit = new QPlainTextEdit(this);
    m_detailsEdit->setReadOnly(true);
    m_detailsEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_detailsEdit->setPlainText(report.detailedExplanation);
    m_detailsEdit->setVisible(false);
    m_detailsEdit->setMinimumHeight(180);
    mainLayout->addWidget(m_detailsEdit);

    // 5. Action Buttons (Copy & Main Menu)
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);

    m_copyBtn = new QPushButton(QStringLiteral("Copy Explanation"), this);
    connect(m_copyBtn, &QPushButton::clicked, this, &ProjectLoadErrorDialog::copyExplanation);
    actionRow->addWidget(m_copyBtn);

    actionRow->addStretch();

    auto *menuBtn = new QPushButton(QStringLiteral("Back to Main Menu"), this);
    menuBtn->setDefault(true);
    connect(menuBtn, &QPushButton::clicked, this, &QDialog::accept);
    actionRow->addWidget(menuBtn);

    mainLayout->addLayout(actionRow);

    setStyleSheet(ThemeManager::instance().getDialogStyleSheet());
    adjustSize();
}

void ProjectLoadErrorDialog::toggleDetails() {
    m_detailsVisible = !m_detailsVisible;
    m_detailsEdit->setVisible(m_detailsVisible);
    m_detailsToggleBtn->setText(m_detailsVisible ? QStringLiteral("Hide Detailed Explanation ▴")
                                                : QStringLiteral("Show Detailed Explanation ▾"));
    adjustSize();
}

void ProjectLoadErrorDialog::copyExplanation() {
    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(m_report.detailedExplanation);
        m_copyBtn->setText(QStringLiteral("✓ Copied to Clipboard!"));
    }
}
