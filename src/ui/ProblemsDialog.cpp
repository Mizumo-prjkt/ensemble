#include "ProblemsDialog.h"
#include "AppIcon.h"

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
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(10);

    // Title header
    auto *titleLabel = new QLabel(QStringLiteral("Project Diagnostics & Code Issues"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: bold; color: #f8f8f2;"));
    mainLayout->addWidget(titleLabel);

    // Tree Widget for problems list
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels({QStringLiteral("Severity"), QStringLiteral("Category"), QStringLiteral("Description")});
    m_treeWidget->header()->resizeSection(0, 110);
    m_treeWidget->header()->resizeSection(1, 130);
    m_treeWidget->setStyleSheet(QStringLiteral(
        "QTreeWidget { background-color: #111116; color: #f8f8f2; border: 1px solid #282a36; border-radius: 6px; font-size: 12px; }"
        "QTreeWidget::item { padding: 4px; border-bottom: 1px solid #1a1b26; }"
        "QTreeWidget::item:selected { background-color: #282a36; color: #50fa7b; }"
        "QHeaderView::section { background-color: #1a1b26; color: #aeb3c6; padding: 4px; border: none; font-weight: bold; }"
    ));
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &ProblemsDialog::onItemDoubleClicked);

    mainLayout->addWidget(m_treeWidget, 1);

    // Bottom close button
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch(1);
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setStyleSheet(QStringLiteral("background-color: #2b2d3c; color: #f8f8f2; padding: 6px 16px; border-radius: 4px; font-weight: bold;"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(closeBtn);

    mainLayout->addLayout(bottomLayout);
}

void ProblemsDialog::setProblems(const QList<ProblemItem> &problems)
{
    m_problems = problems;
    m_treeWidget->clear();

    for (const auto &item : m_problems) {
        auto *treeItem = new QTreeWidgetItem(m_treeWidget);
        if (item.severity == ProblemItem::Error) {
            treeItem->setText(0, QStringLiteral("⊗ Error"));
            treeItem->setForeground(0, QColor(255, 85, 85)); // Bright Red
        } else {
            treeItem->setText(0, QStringLiteral("⚠️ Warning"));
            treeItem->setForeground(0, QColor(255, 184, 108)); // Bright Orange
        }

        treeItem->setText(1, item.category);
        treeItem->setText(2, item.description);
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
