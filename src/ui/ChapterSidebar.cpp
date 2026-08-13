#include "ChapterSidebar.h"

#include "model/Ao3Project.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QUuid>
#include <QPushButton>
#include <QVBoxLayout>

ChapterSidebar::ChapterSidebar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *label = new QLabel(QStringLiteral("Chapters"), this);
    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);

    auto *addBtn = new QPushButton(QStringLiteral("+ Add"), this);
    auto *renameBtn = new QPushButton(QStringLiteral("Rename"), this);
    auto *deleteBtn = new QPushButton(QStringLiteral("Delete"), this);
    auto *upBtn = new QPushButton(QStringLiteral("↑"), this);
    auto *downBtn = new QPushButton(QStringLiteral("↓"), this);

    layout->addWidget(label);
    layout->addWidget(m_list, 1);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(addBtn);
    row1->addWidget(renameBtn);
    row1->addWidget(deleteBtn);
    layout->addLayout(row1);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(upBtn);
    row2->addWidget(downBtn);
    row2->addStretch();
    layout->addLayout(row2);

    connect(addBtn, &QPushButton::clicked, this, &ChapterSidebar::onAddChapter);
    connect(renameBtn, &QPushButton::clicked, this, &ChapterSidebar::onRenameChapter);
    connect(deleteBtn, &QPushButton::clicked, this, &ChapterSidebar::onDeleteChapter);
    connect(upBtn, &QPushButton::clicked, this, &ChapterSidebar::onMoveUp);
    connect(downBtn, &QPushButton::clicked, this, &ChapterSidebar::onMoveDown);
    connect(m_list, &QListWidget::currentRowChanged, this, &ChapterSidebar::onCurrentRowChanged);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &ChapterSidebar::onRowsMoved);
}

void ChapterSidebar::bindProject(Ao3Project *project)
{
    m_project = project;
    refresh();
}

void ChapterSidebar::refresh()
{
    if (!m_project)
        return;

    m_blockSignals = true;
    rebuildList();
    m_list->setCurrentRow(m_project->activeChapterIndex());
    m_blockSignals = false;
}

void ChapterSidebar::rebuildList()
{
    m_list->clear();
    for (const Chapter &chapter : m_project->chapters()) {
        auto *item = new QListWidgetItem(chapter.title());
        item->setData(Qt::UserRole, chapter.id().toString());
        m_list->addItem(item);
    }
}

void ChapterSidebar::onAddChapter()
{
    if (!m_project)
        return;

    m_project->addChapter();
    refresh();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterSidebar::onRenameChapter()
{
    if (!m_project)
        return;

    const int row = m_list->currentRow();
    if (row < 0 || row >= m_project->chapters().size())
        return;

    Chapter &chapter = m_project->chapters()[row];
    const QString newTitle = QInputDialog::getText(this, QStringLiteral("Rename Chapter"),
                                                   QStringLiteral("Title:"), QLineEdit::Normal,
                                                   chapter.title());
    if (newTitle.isEmpty() || newTitle == chapter.title())
        return;

    chapter.setTitle(newTitle);
    m_project->setDirty(true);
    if (QListWidgetItem *item = m_list->item(row))
        item->setText(newTitle);
}

void ChapterSidebar::onDeleteChapter()
{
    if (!m_project || m_project->chapters().size() <= 1)
        return;

    const int row = m_list->currentRow();
    if (row < 0)
        return;

    const auto reply = QMessageBox::question(this, QStringLiteral("Delete Chapter"),
                                             QStringLiteral("Delete \"%1\"?")
                                                 .arg(m_project->chapters()[row].title()));
    if (reply != QMessageBox::Yes)
        return;

    m_project->removeChapter(row);
    refresh();
    emit chapterSelected(m_project->activeChapterIndex());
    emit chaptersReordered();
}

void ChapterSidebar::onMoveUp()
{
    if (!m_project)
        return;
    const int row = m_list->currentRow();
    if (row <= 0)
        return;
    m_project->moveChapter(row, row - 1);
    refresh();
    m_list->setCurrentRow(row - 1);
    emit chaptersReordered();
}

void ChapterSidebar::onMoveDown()
{
    if (!m_project)
        return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_project->chapters().size() - 1)
        return;
    m_project->moveChapter(row, row + 1);
    refresh();
    m_list->setCurrentRow(row + 1);
    emit chaptersReordered();
}

void ChapterSidebar::onCurrentRowChanged(int row)
{
    if (m_blockSignals || !m_project || row < 0)
        return;
    emit chapterSelected(row);
}

void ChapterSidebar::onRowsMoved()
{
    if (!m_project)
        return;

    QList<Chapter> reordered;
    reordered.reserve(m_list->count());

    for (int i = 0; i < m_list->count(); ++i) {
        const QUuid id = QUuid::fromString(m_list->item(i)->data(Qt::UserRole).toString());
        for (const Chapter &chapter : m_project->chapters()) {
            if (chapter.id() == id) {
                Chapter copy = chapter;
                copy.setOrder(i);
                reordered.append(copy);
                break;
            }
        }
    }

    if (reordered.size() == m_project->chapters().size()) {
        m_project->chapters() = reordered;
        m_project->setDirty(true);
        emit chaptersReordered();
    }
}
