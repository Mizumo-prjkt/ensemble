#include "DebugConsoleDialog.h"
#include "debug/debug.hpp"
#include "AppIcon.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
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

    auto *filterLabel = new QLabel(QStringLiteral("Category Filter:"), this);
    filterLabel->setStyleSheet(QStringLiteral("color: #aeb3c6; font-weight: bold;"));

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItems({QStringLiteral("All Categories"),
                             QStringLiteral("app"),
                             QStringLiteral("editor"),
                             QStringLiteral("preview"),
                             QStringLiteral("network"),
                             QStringLiteral("parser"),
                             QStringLiteral("model")});
    m_filterCombo->setStyleSheet(QStringLiteral("background-color: #1e1e28; color: #f8f8f2; border: 1px solid #383a4c; padding: 4px 8px; border-radius: 4px;"));
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DebugConsoleDialog::refreshLogs);

    auto *refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    refreshBtn->setStyleSheet(QStringLiteral("background-color: #2b2d3c; color: #f8f8f2; padding: 5px 12px; border-radius: 4px;"));
    connect(refreshBtn, &QPushButton::clicked, this, &DebugConsoleDialog::refreshLogs);

    auto *copyBtn = new QPushButton(QStringLiteral("Copy All"), this);
    copyBtn->setStyleSheet(QStringLiteral("background-color: #2b2d3c; color: #f8f8f2; padding: 5px 12px; border-radius: 4px;"));
    connect(copyBtn, &QPushButton::clicked, this, &DebugConsoleDialog::copyLogs);

    auto *clearBtn = new QPushButton(QStringLiteral("Clear Logs"), this);
    clearBtn->setStyleSheet(QStringLiteral("background-color: #ff5555; color: #ffffff; padding: 5px 12px; border-radius: 4px; font-weight: bold;"));
    connect(clearBtn, &QPushButton::clicked, this, &DebugConsoleDialog::clearLogs);

    topLayout->addWidget(filterLabel);
    topLayout->addWidget(m_filterCombo);
    topLayout->addWidget(refreshBtn);
    topLayout->addWidget(copyBtn);
    topLayout->addWidget(clearBtn);
    topLayout->addStretch(1);

    mainLayout->addLayout(topLayout);

    // Log Text Area
    m_logTextEdit = new QTextEdit(this);
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setFont(QFont(QStringLiteral("monospace"), 10));
    m_logTextEdit->setStyleSheet(QStringLiteral("background-color: #111116; color: #50fa7b; border: 1px solid #282a36; border-radius: 6px; padding: 8px;"));
    mainLayout->addWidget(m_logTextEdit, 1);

    // Auto-refresh timer
    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(1000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &DebugConsoleDialog::refreshLogs);
    m_autoRefreshTimer->start();

    refreshLogs();
}

void DebugConsoleDialog::refreshLogs()
{
    const QString fullLogs = EnsembleDebug::getLogBuffer();
    const QString filter = m_filterCombo->currentText();

    if (filter == QStringLiteral("All Categories")) {
        m_logTextEdit->setPlainText(fullLogs);
    } else {
        const QStringList lines = fullLogs.split(QLatin1Char('\n'));
        QStringList filteredLines;
        for (const QString &line : lines) {
            if (line.contains(QStringLiteral("[%1]").arg(filter))) {
                filteredLines.append(line);
            }
        }
        m_logTextEdit->setPlainText(filteredLines.join(QLatin1Char('\n')));
    }

    // Scroll to bottom
    m_logTextEdit->verticalScrollBar()->setValue(m_logTextEdit->verticalScrollBar()->maximum());
}

void DebugConsoleDialog::clearLogs()
{
    EnsembleDebug::clearLogBuffer();
    refreshLogs();
}

void DebugConsoleDialog::copyLogs()
{
    QApplication::clipboard()->setText(m_logTextEdit->toPlainText());
}
