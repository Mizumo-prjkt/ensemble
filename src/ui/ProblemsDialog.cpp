#include "ProblemsDialog.h"
#include "AppIcon.h"
#include "themes/ThemeManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

ProblemsDialog::ProblemsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Project Problems & Diagnostics"));
    setWindowIcon(AppIcon::icon());
    resize(780, 440);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Title header
    m_titleLabel = new QLabel(QStringLiteral("Project Diagnostics & Code Issues"), this);
    mainLayout->addWidget(m_titleLabel);

    // Tree Widget for problems list
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels({QStringLiteral("Severity"), QStringLiteral("Category"), QStringLiteral("Description")});
    m_treeWidget->header()->resizeSection(0, 110);
    m_treeWidget->header()->resizeSection(1, 130);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &ProblemsDialog::onItemDoubleClicked);

    mainLayout->addWidget(m_treeWidget, 1);

    // Bottom close button
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch(1);
    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(bottomLayout);

    updateTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateTheme();
    });
}

void ProblemsDialog::updateTheme()
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

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(isDark
            ? QStringLiteral("font-size: 14px; font-weight: 700; color: #8be9fd; background: transparent;")
            : QStringLiteral("font-size: 14px; font-weight: 700; color: #4f46e5; background: transparent;"));
    }

    if (m_treeWidget) {
        m_treeWidget->setStyleSheet(isDark ? QStringLiteral(
            "QTreeWidget {"
            "  background-color: #1a1b23;"
            "  color: #f8f8f2;"
            "  border: 1px solid #282a36;"
            "  border-radius: 6px;"
            "  font-size: 12px;"
            "}"
            "QTreeWidget::item {"
            "  padding: 6px 4px;"
            "  border-bottom: 1px solid #21222c;"
            "}"
            "QTreeWidget::item:hover {"
            "  background-color: #282a36;"
            "}"
            "QTreeWidget::item:selected {"
            "  background-color: #3b335a;"
            "  color: #bd93f9;"
            "  font-weight: 600;"
            "}"
            "QHeaderView::section {"
            "  background-color: #21222c;"
            "  color: #94a3b8;"
            "  padding: 6px 8px;"
            "  border: none;"
            "  border-right: 1px solid #282a36;"
            "  border-bottom: 1px solid #282a36;"
            "  font-weight: 700;"
            "}"
        ) : QStringLiteral(
            "QTreeWidget {"
            "  background-color: #ffffff;"
            "  color: #0f172a;"
            "  border: 1px solid #cbd5e1;"
            "  border-radius: 6px;"
            "  font-size: 12px;"
            "}"
            "QTreeWidget::item {"
            "  padding: 6px 4px;"
            "  border-bottom: 1px solid #f1f5f9;"
            "}"
            "QTreeWidget::item:hover {"
            "  background-color: #f8fafc;"
            "}"
            "QTreeWidget::item:selected {"
            "  background-color: #e0e7ff;"
            "  color: #4338ca;"
            "  font-weight: 600;"
            "}"
            "QHeaderView::section {"
            "  background-color: #f1f5f9;"
            "  color: #475569;"
            "  padding: 6px 8px;"
            "  border: none;"
            "  border-right: 1px solid #cbd5e1;"
            "  border-bottom: 1px solid #cbd5e1;"
            "  font-weight: 700;"
            "}"
        ));
    }

    if (m_closeBtn) {
        m_closeBtn->setStyleSheet(isDark ? QStringLiteral(
            "QPushButton {"
            "  background-color: #7f5af0;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 7px 20px;"
            "  font-weight: 700;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #6c47d9;"
            "}"
        ) : QStringLiteral(
            "QPushButton {"
            "  background-color: #6366f1;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 7px 20px;"
            "  font-weight: 700;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #4f46e5;"
            "}"
        ));
    }

    // Refresh problem item foreground colors if already populated
    if (!m_problems.isEmpty()) {
        setProblems(m_problems);
    }
}

void ProblemsDialog::setProblems(const QList<ProblemItem> &problems)
{
    m_problems = problems;
    m_treeWidget->clear();

    const bool isDark = ThemeManager::instance().isDarkMode();
    const QColor errColor = isDark ? QColor(255, 85, 85) : QColor(220, 38, 38);
    const QColor warnColor = isDark ? QColor(255, 184, 108) : QColor(217, 119, 6);
    const QColor textCol = isDark ? QColor(248, 248, 242) : QColor(15, 23, 42);

    for (const auto &item : m_problems) {
        auto *treeItem = new QTreeWidgetItem(m_treeWidget);
        if (item.severity == ProblemItem::Error) {
            treeItem->setText(0, QStringLiteral("⊗ Error"));
            treeItem->setForeground(0, errColor);
        } else {
            treeItem->setText(0, QStringLiteral("⚠️ Warning"));
            treeItem->setForeground(0, warnColor);
        }

        treeItem->setText(1, item.category);
        treeItem->setForeground(1, textCol);

        treeItem->setText(2, item.description);
        treeItem->setForeground(2, textCol);

        treeItem->setData(0, Qt::UserRole, item.chapterIndex);
    }
}

void ProblemsDialog::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;

    const int chapIdx = item->data(0, Qt::UserRole).toInt();
    if (chapIdx >= 0) {
        emit chapterSelected(chapIdx);
        accept();
    } else {
        emit cssTabRequested();
        accept();
    }
}
