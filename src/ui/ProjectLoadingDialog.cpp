#include "ProjectLoadingDialog.h"
#include "AppIcon.h"
#include "themes/ThemeManager.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

ProjectLoadingDialog::ProjectLoadingDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Opening Project…"));
    setWindowIcon(AppIcon::icon());
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setFixedWidth(460);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    m_cardFrame = new QFrame(this);
    m_cardFrame->setObjectName(QStringLiteral("loadingCard"));
    auto *cardLayout = new QVBoxLayout(m_cardFrame);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto *iconLabel = new QLabel(this);
    iconLabel->setPixmap(AppIcon::pixmap(40, 40));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(3);

    m_titleLabel = new QLabel(QStringLiteral("Opening Project…"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700;"));
    titleLayout->addWidget(m_titleLabel);

    const QString fileName = QFileInfo(filePath).fileName();
    m_fileNameLabel = new QLabel(fileName.isEmpty() ? filePath : fileName, this);
    m_fileNameLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600;"));
    titleLayout->addWidget(m_fileNameLabel);

    headerLayout->addLayout(titleLayout, 1);
    cardLayout->addLayout(headerLayout);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("loadingProgressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(QStringLiteral("Preparing to open project…"), this);
    m_statusLabel->setObjectName(QStringLiteral("loadingStatusLabel"));
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    mainLayout->addWidget(m_cardFrame);

    updateTheme();
}

int ProjectLoadingDialog::progressValue() const {
    return m_progressBar ? m_progressBar->value() : 0;
}

QString ProjectLoadingDialog::currentStatus() const {
    return m_statusLabel ? m_statusLabel->text() : QString();
}

void ProjectLoadingDialog::setProgress(int percent, const QString &statusMessage) {
    if (m_progressBar) {
        m_progressBar->setValue(qBound(0, percent, 100));
    }
    if (m_statusLabel) {
        m_statusLabel->setText(statusMessage);
    }
    // Repaint dialog and pump event loop so the window never freezes
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void ProjectLoadingDialog::reject() {
    // Ignore Esc key while loading
}

void ProjectLoadingDialog::finish() {
    setProgress(100, QStringLiteral("Ready!"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    accept();
    close();
}

void ProjectLoadingDialog::updateTheme() {
    const auto &tm = ThemeManager::instance();
    const bool isDark = tm.isDarkMode();
    const int rad = tm.cardRadius();
    const QString bg = tm.windowBgColor().name();
    const QString cardBg = tm.cardBgColor().name();
    const QString surfaceBg = tm.surfaceBgColor().name();
    const QString border = tm.borderColor().name();
    const QString text = tm.primaryTextColor().name();
    const QString muted = tm.mutedTextColor().name();
    const QString accent = tm.accentColor().name();

    setStyleSheet(QStringLiteral("QDialog { background-color: %1; color: %2; }").arg(bg, text));

    if (m_cardFrame) {
        m_cardFrame->setStyleSheet(QStringLiteral(
            "#loadingCard {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: %3px;"
            "}"
        ).arg(cardBg, border, QString::number(rad)));
    }

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700; color: %1;").arg(text));
    }
    if (m_fileNameLabel) {
        m_fileNameLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: %1;").arg(muted));
    }
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(muted));
    }

    if (m_progressBar) {
        m_progressBar->setStyleSheet(QStringLiteral(
            "QProgressBar {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: %3px;"
            "  text-align: center;"
            "  color: %4;"
            "  font-weight: 700;"
            "  font-size: 10px;"
            "  min-height: 18px;"
            "  max-height: 18px;"
            "}"
            "QProgressBar::chunk {"
            "  background-color: %5;"
            "  border-radius: %6px;"
            "}"
        ).arg(surfaceBg, border, QString::number(qMin(rad, 5)),
             isDark ? QStringLiteral("#ffffff") : QStringLiteral("#000000"),
             accent,
             QString::number(qMax(0, qMin(rad, 5) - 1))));
    }
}
