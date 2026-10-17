#include "ChapterSidebar.h"

#include "ChapterManagerDialog.h"
#include "ToolbarIcons.h"
#include "model/Ao3Project.h"
#include "themes/ThemeManager.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

ChapterSidebar::ChapterSidebar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    setStyleSheet(ThemeManager::instance().getSidebarStyleSheet());
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        setStyleSheet(ThemeManager::instance().getSidebarStyleSheet());
        updateIcons();
    });

    auto *headerLayout = new QHBoxLayout;
    auto *label = new QLabel(QStringLiteral("<b>Chapters</b>"), this);
    m_quickAddBtn = new QPushButton(this);
    m_quickAddBtn->setObjectName(QStringLiteral("quickAddBtn"));
    m_quickAddBtn->setToolTip(QStringLiteral("Add new chapter"));
    m_quickAddBtn->setFixedSize(26, 26);
    m_quickAddBtn->setIconSize(QSize(16, 16));
    m_quickAddBtn->setCursor(Qt::PointingHandCursor);
    updateIcons();

    headerLayout->addWidget(label);
    headerLayout->addStretch();
    headerLayout->addWidget(m_quickAddBtn);
    layout->addLayout(headerLayout);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("chapterSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("🔍 Filter chapters…"));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    // Dedicated Chapter Manager Button replacing old cluttered bottom buttons
    m_managerBtn = new QPushButton(QStringLiteral("📚 Chapter Manager…"), this);
    m_managerBtn->setObjectName(QStringLiteral("managerBtn"));
    m_managerBtn->setToolTip(QStringLiteral("Open Chapter Manager window to view words, lines, HTML syntaxes, reorder, or edit titles"));
    m_managerBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_managerBtn);

    connect(m_quickAddBtn, &QPushButton::clicked, this, &ChapterSidebar::onAddChapter);
    connect(m_managerBtn, &QPushButton::clicked, this, &ChapterSidebar::onOpenChapterManager);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ChapterSidebar::onSearchFilterChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        for (int i = 0; i < m_list->count(); ++i) {
            if (!m_list->item(i)->isHidden()) {
                m_list->setCurrentRow(i);
                m_list->setFocus();
                break;
            }
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, &ChapterSidebar::onCurrentRowChanged);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (m_blockSignals || !m_project || !item)
            return;
        const int row = m_list->row(item);
        if (row >= 0) {
            emit chapterSelected(row);
        }
    });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &ChapterSidebar::onRowsMoved);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ChapterSidebar::showContextMenu);
}

void ChapterSidebar::updateIcons()
{
    const bool isDark = ThemeManager::instance().isDarkMode();
    const QColor normalColor = isDark ? QColor(QStringLiteral("#cbd5e1")) : QColor(QStringLiteral("#334155"));
    const QColor activeColor = QColor(QStringLiteral("#ffffff"));
    if (m_quickAddBtn) {
        m_quickAddBtn->setIcon(ToolbarIcons::plus(normalColor, activeColor));
    }
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
    if (!m_project)
        return;

    int idx = 1;
    for (const Chapter &chapter : m_project->chapters()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1. %2").arg(idx++).arg(chapter.title()));
        item->setData(Qt::UserRole, chapter.id().toString());
        m_list->addItem(item);
    }

    if (m_searchEdit) {
        onSearchFilterChanged(m_searchEdit->text());
    }
}

void ChapterSidebar::onSearchFilterChanged(const QString &text)
{
    const QString filter = text.trimmed();
    const bool filtering = !filter.isEmpty();

    // Disable drag reordering while filtering so chapters don't get accidentally misplaced
    m_list->setDragEnabled(!filtering);

    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (!item)
            continue;
        if (!filtering) {
            item->setHidden(false);
        } else {
            const bool matches = item->text().contains(filter, Qt::CaseInsensitive);
            item->setHidden(!matches);
        }
    }
}

void ChapterSidebar::onOpenChapterManager()
{
    if (!m_project)
        return;

    ChapterManagerDialog dlg(m_project, this);
    connect(&dlg, &ChapterManagerDialog::chapterSelected, this, &ChapterSidebar::chapterSelected);
    connect(&dlg, &ChapterManagerDialog::chaptersReordered, this, &ChapterSidebar::chaptersReordered);
    dlg.exec();

    refresh();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterSidebar::onAddChapter()
{
    if (!m_project)
        return;

    m_project->addChapter();
    refresh();
    emit chapterSelected(m_project->activeChapterIndex());
    emit chaptersReordered();
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
    if (newTitle.trimmed().isEmpty() || newTitle.trimmed() == chapter.title())
        return;

    chapter.setTitle(newTitle.trimmed());
    m_project->setDirty(true);
    refresh();
    emit chaptersReordered();
}

void ChapterSidebar::onDeleteChapter()
{
    if (!m_project || m_project->chapters().size() <= 1)
        return;

    const int row = m_list->currentRow();
    if (row < 0 || row >= m_project->chapters().size())
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

        m_blockSignals = true;
        for (int i = 0; i < m_list->count(); ++i) {
            m_list->item(i)->setText(QStringLiteral("%1. %2").arg(i + 1).arg(m_project->chapters()[i].title()));
        }
        m_blockSignals = false;

        emit chaptersReordered();
    }
}

void ChapterSidebar::showContextMenu(const QPoint &pos)
{
    auto *menu = new QMenu(this);
    menu->setStyleSheet(ThemeManager::instance().getDialogStyleSheet());

    auto *manageAct = menu->addAction(QStringLiteral("📚 Chapter Manager…"));
    connect(manageAct, &QAction::triggered, this, &ChapterSidebar::onOpenChapterManager);

    menu->addSeparator();

    auto *addAct = menu->addAction(QStringLiteral("➕ Add Chapter"));
    connect(addAct, &QAction::triggered, this, &ChapterSidebar::onAddChapter);

    const int row = m_list->currentRow();
    if (row >= 0 && m_project) {
        auto *renameAct = menu->addAction(QStringLiteral("✏️ Rename…"));
        connect(renameAct, &QAction::triggered, this, &ChapterSidebar::onRenameChapter);

        if (m_project->chapters().size() > 1) {
            auto *delAct = menu->addAction(QStringLiteral("🗑️ Delete Chapter"));
            connect(delAct, &QAction::triggered, this, &ChapterSidebar::onDeleteChapter);
        }
    }

    menu->exec(m_list->viewport()->mapToGlobal(pos));
    menu->deleteLater();
}
