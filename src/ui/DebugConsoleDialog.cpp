#include "DebugConsoleDialog.h"
#include "debug/debug.hpp"
#include "AppIcon.h"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

DebugConsoleDialog::DebugConsoleDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Ensemble Debug Console"));
    setWindowIcon(AppIcon::icon());
    resize(820, 520);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(10);

    // Top Controls
    auto *topLayout = new QHBoxLayout();

    m_filterLabel = new QLabel(QStringLiteral("Category Filter:"), this);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItems({QStringLiteral("All Categories"),
                             QStringLiteral("app"),
                             QStringLiteral("editor"),
                             QStringLiteral("preview"),
                             QStringLiteral("network"),
                             QStringLiteral("parser"),
                             QStringLiteral("model"),
                             QStringLiteral("mcp")});
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DebugConsoleDialog::refreshLogs);

    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DebugConsoleDialog::refreshLogs);

    m_copyBtn = new QPushButton(QStringLiteral("Copy All"), this);
    connect(m_copyBtn, &QPushButton::clicked, this, &DebugConsoleDialog::copyLogs);

    m_clearBtn = new QPushButton(QStringLiteral("Clear Logs"), this);
    connect(m_clearBtn, &QPushButton::clicked, this, &DebugConsoleDialog::clearLogs);

    m_hwDiagBtn = new QPushButton(QStringLiteral("Hardware Diagnostics"), this);
    connect(m_hwDiagBtn, &QPushButton::clicked, this, [this]() {
        const QString report = EnsembleDebug::formatHardwareReport(EnsembleDebug::queryHardwareInfo());
        QMessageBox::information(this, QStringLiteral("System & Hardware Diagnostics"), report);
    });

    topLayout->addWidget(m_filterLabel);
    topLayout->addWidget(m_filterCombo);
    topLayout->addWidget(m_refreshBtn);
    topLayout->addWidget(m_copyBtn);
    topLayout->addWidget(m_clearBtn);
    topLayout->addWidget(m_hwDiagBtn);
    topLayout->addStretch(1);

    mainLayout->addLayout(topLayout);

    // Log Text Area
    m_logTextEdit = new QTextEdit(this);
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setFont(QFont(QStringLiteral("monospace"), 10));
    mainLayout->addWidget(m_logTextEdit, 1);

    // Auto-refresh timer
    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(1000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &DebugConsoleDialog::refreshLogs);
    m_autoRefreshTimer->start();

    updateTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateTheme();
    });

    refreshLogs();
}

void DebugConsoleDialog::updateTheme()
{
    const bool isDark = ThemeManager::instance().isDarkMode();

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

    if (m_filterLabel) {
        m_filterLabel->setStyleSheet(isDark ? QStringLiteral("color: #aeb3c6; font-weight: bold;")
                                            : QStringLiteral("color: #475569; font-weight: bold;"));
    }

    if (m_filterCombo) {
        m_filterCombo->setStyleSheet(isDark ? QStringLiteral(
            "background-color: #1e1e28; color: #f8f8f2; border: 1px solid #383a4c; padding: 4px 8px; border-radius: 4px;"
        ) : QStringLiteral(
            "background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; padding: 4px 8px; border-radius: 4px;"
        ));
    }

    if (m_refreshBtn) {
        m_refreshBtn->setStyleSheet(isDark ? QStringLiteral(
            "background-color: #2b2d3c; color: #f8f8f2; padding: 5px 12px; border-radius: 4px; border: 1px solid #44475a;"
        ) : QStringLiteral(
            "background-color: #f1f5f9; color: #0f172a; padding: 5px 12px; border-radius: 4px; border: 1px solid #cbd5e1;"
        ));
    }

    if (m_copyBtn) {
        m_copyBtn->setStyleSheet(isDark ? QStringLiteral(
            "background-color: #2b2d3c; color: #f8f8f2; padding: 5px 12px; border-radius: 4px; border: 1px solid #44475a;"
        ) : QStringLiteral(
            "background-color: #f1f5f9; color: #0f172a; padding: 5px 12px; border-radius: 4px; border: 1px solid #cbd5e1;"
        ));
    }

    if (m_clearBtn) {
        m_clearBtn->setStyleSheet(QStringLiteral(
            "background-color: #e53e3e; color: #ffffff; padding: 5px 12px; border-radius: 4px; font-weight: bold; border: none;"
        ));
    }

    if (m_logTextEdit) {
        m_logTextEdit->setStyleSheet(isDark ? QStringLiteral(
            "background-color: #111116; color: #50fa7b; border: 1px solid #282a36; border-radius: 6px; padding: 8px;"
        ) : QStringLiteral(
            "background-color: #0f172a; color: #38bdf8; border: 1px solid #334155; border-radius: 6px; padding: 8px;"
        ));
    }
}

void DebugConsoleDialog::refreshLogs()
{
    const QString allLogs = EnsembleDebug::getLogBuffer();
    if (m_filterCombo->currentIndex() == 0) {
        m_logTextEdit->setPlainText(allLogs);
    } else {
        const QString selectedCat = m_filterCombo->currentText();
        QStringList filtered;
        const QStringList lines = allLogs.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.contains(QStringLiteral("[%1]").arg(selectedCat))) {
                filtered << line;
            }
        }
        m_logTextEdit->setPlainText(filtered.join(QLatin1Char('\n')));
    }

    // Auto-scroll to bottom if user is already near bottom
    QScrollBar *sb = m_logTextEdit->verticalScrollBar();
    if (sb) {
        sb->setValue(sb->maximum());
    }
}

void DebugConsoleDialog::clearLogs()
{
    EnsembleDebug::clearLogBuffer();
    m_logTextEdit->clear();
}

void DebugConsoleDialog::copyLogs()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(m_logTextEdit->toPlainText());
    }
}
