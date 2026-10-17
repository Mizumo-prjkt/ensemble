#include "ChapterManagerDialog.h"

#include "model/Ao3Project.h"
#include "model/Chapter.h"
#include "themes/ThemeManager.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextDocument>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

ChapterManagerDialog::ChapterManagerDialog(Ao3Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(QStringLiteral("Chapter Manager — Ensemble"));
    resize(780, 500);

    setupUi();
    updateTheme();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](AppTheme) {
        updateTheme();
    });

    refreshList();
}

void ChapterManagerDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Header info banner
    auto *headerLabel = new QLabel(
        QStringLiteral("<b>Manage, reorder, and review story chapters</b><br/>"
                       "<span style=\"color: gray;\">Drag rows to resort, or type a 1-based position below. Double-click any title to edit inline.</span>"),
        this);
    mainLayout->addWidget(headerLabel);

    // Tree Widget (Table-like multi-column list)
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({
        QStringLiteral("Chapter #"),
        QStringLiteral("Title"),
        QStringLiteral("Words"),
        QStringLiteral("Lines"),
        QStringLiteral("HTML Syntaxes Used")
    });

    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragEnabled(true);
    m_tree->viewport()->setAcceptDrops(true);

    auto *header = m_tree->header();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 95);
    m_tree->setColumnWidth(1, 210);
    m_tree->setColumnWidth(2, 85);
    m_tree->setColumnWidth(3, 75);

    mainLayout->addWidget(m_tree, 1);

    // Controls Row 1: Actions & Reordering
    auto *controlsRow = new QHBoxLayout;
    controlsRow->setSpacing(8);

    m_addBtn = new QPushButton(QStringLiteral("➕ Add Chapter"), this);
    m_renameBtn = new QPushButton(QStringLiteral("✏️ Rename Title"), this);
    m_deleteBtn = new QPushButton(QStringLiteral("🗑️ Delete Chapter"), this);

    controlsRow->addWidget(m_addBtn);
    controlsRow->addWidget(m_renameBtn);
    controlsRow->addWidget(m_deleteBtn);

    controlsRow->addStretch();

    auto *posLabel = new QLabel(QStringLiteral("Move to position:"), this);
    m_positionSpin = new QSpinBox(this);
    // Explicit 1-based indexing: Chapter 0 does NOT exist! Chapter 1 must stay Chapter 1.
    m_positionSpin->setMinimum(1);
    m_positionSpin->setMaximum(1);
    m_positionSpin->setToolTip(QStringLiteral("Target chapter position (1 to N). Chapter 0 is not permitted."));

    m_moveBtn = new QPushButton(QStringLiteral("Move"), this);
    m_upBtn = new QPushButton(QStringLiteral("▲ Up"), this);
    m_downBtn = new QPushButton(QStringLiteral("▼ Down"), this);

    controlsRow->addWidget(posLabel);
    controlsRow->addWidget(m_positionSpin);
    controlsRow->addWidget(m_moveBtn);
    controlsRow->addWidget(m_upBtn);
    controlsRow->addWidget(m_downBtn);

    mainLayout->addLayout(controlsRow);

    // Bottom Summary & Close Row
    auto *bottomRow = new QHBoxLayout;
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet(QStringLiteral("font-weight: 500; color: #666;"));
    bottomRow->addWidget(m_summaryLabel, 1);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setDefault(true);
    bottomRow->addWidget(closeBtn);

    mainLayout->addLayout(bottomRow);

    // Connections
    connect(m_addBtn, &QPushButton::clicked, this, &ChapterManagerDialog::onAddChapter);
    connect(m_renameBtn, &QPushButton::clicked, this, &ChapterManagerDialog::onRenameChapter);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ChapterManagerDialog::onDeleteChapter);
    connect(m_moveBtn, &QPushButton::clicked, this, &ChapterManagerDialog::onMoveToPosition);
    connect(m_upBtn, &QPushButton::clicked, this, &ChapterManagerDialog::onMoveUp);
    connect(m_downBtn, &QPushButton::clicked, this, &ChapterManagerDialog::onMoveDown);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ChapterManagerDialog::onSelectionChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &ChapterManagerDialog::onItemChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ChapterManagerDialog::onItemDoubleClicked);
    connect(m_tree->model(), &QAbstractItemModel::rowsMoved, this, &ChapterManagerDialog::onRowsMoved);
}

void ChapterManagerDialog::updateTheme()
{
    setStyleSheet(ThemeManager::instance().getDialogStyleSheet());
}

int ChapterManagerDialog::selectedChapterIndex() const
{
    auto items = m_tree->selectedItems();
    if (items.isEmpty())
        return -1;
    return m_tree->indexOfTopLevelItem(items.first());
}

void ChapterManagerDialog::refreshList()
{
    if (!m_project)
        return;

    m_blockSignals = true;
    m_tree->clear();

    const int count = m_project->chapters().size();
    m_positionSpin->setMaximum(qMax(1, count));

    for (int i = 0; i < count; ++i) {
        const Chapter &ch = m_project->chapters().at(i);
        const int words = computeWordCount(ch.html());
        const int lines = computeLineCount(ch.html());
        const QString syntaxes = computeSyntaxSummary(ch.html());

        auto *item = new QTreeWidgetItem(m_tree);
        // Column 0: Chapter # (1-indexed: Chapter 1 to N)
        item->setText(0, QStringLiteral("Chapter %1").arg(i + 1));
        item->setTextAlignment(0, Qt::AlignCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        // Column 1: Title (Editable)
        item->setText(1, ch.title());
        item->setFlags(item->flags() | Qt::ItemIsEditable);

        // Column 2: Words
        item->setText(2, QLocale().toString(words));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        // Column 3: Lines
        item->setText(3, QLocale().toString(lines));
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        // Column 4: HTML Syntaxes Used
        item->setText(4, syntaxes);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        item->setData(0, Qt::UserRole, ch.id().toString());
    }

    const int activeIndex = m_project->activeChapterIndex();
    if (activeIndex >= 0 && activeIndex < m_tree->topLevelItemCount()) {
        m_tree->setCurrentItem(m_tree->topLevelItem(activeIndex));
    } else if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }

    m_blockSignals = false;
    updateSummary();
    updatePositionControls();
}

void ChapterManagerDialog::updateSummary()
{
    if (!m_project)
        return;

    const int count = m_project->chapters().size();
    int totalWords = 0;
    int totalLines = 0;
    for (const Chapter &ch : m_project->chapters()) {
        totalWords += computeWordCount(ch.html());
        totalLines += computeLineCount(ch.html());
    }

    m_summaryLabel->setText(
        QStringLiteral("Total Chapters: %1  |  Total Words: %2  |  Total Lines: %3")
            .arg(count)
            .arg(QLocale().toString(totalWords))
            .arg(QLocale().toString(totalLines)));
}

void ChapterManagerDialog::updatePositionControls()
{
    const int count = m_project ? m_project->chapters().size() : 0;
    const int selected = selectedChapterIndex();

    m_positionSpin->setMaximum(qMax(1, count));
    if (selected >= 0) {
        m_positionSpin->setValue(selected + 1); // 1-based display
        m_moveBtn->setEnabled(count > 1);
        m_upBtn->setEnabled(selected > 0);
        m_downBtn->setEnabled(selected < count - 1);
        m_renameBtn->setEnabled(true);
        m_deleteBtn->setEnabled(count > 1);
    } else {
        m_moveBtn->setEnabled(false);
        m_upBtn->setEnabled(false);
        m_downBtn->setEnabled(false);
        m_renameBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
    }
}

void ChapterManagerDialog::onSelectionChanged()
{
    updatePositionControls();
    const int index = selectedChapterIndex();
    if (index >= 0 && m_project) {
        emit chapterSelected(index);
    }
}

void ChapterManagerDialog::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_blockSignals || !m_project || !item || column != 1)
        return;

    const int row = m_tree->indexOfTopLevelItem(item);
    if (row < 0 || row >= m_project->chapters().size())
        return;

    const QString newTitle = item->text(1).trimmed();
    if (!newTitle.isEmpty() && newTitle != m_project->chapters()[row].title()) {
        m_project->chapters()[row].setTitle(newTitle);
        m_project->setDirty(true);
        emit chaptersReordered();
    }
}

void ChapterManagerDialog::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    if (item && column != 1) {
        // Switch into edit mode for column 1
        m_tree->editItem(item, 1);
    }
}

void ChapterManagerDialog::onAddChapter()
{
    if (!m_project)
        return;

    const int newNumber = m_project->chapters().size() + 1;
    const QString defaultTitle = QStringLiteral("Chapter %1").arg(newNumber);
    bool ok = false;
    const QString title = QInputDialog::getText(
        this,
        QStringLiteral("Add Chapter"),
        QStringLiteral("Enter chapter title:"),
        QLineEdit::Normal,
        defaultTitle,
        &ok);

    if (!ok)
        return;

    m_project->addChapter(title.trimmed().isEmpty() ? defaultTitle : title.trimmed());
    refreshList();
    const int newIdx = m_project->chapters().size() - 1;
    if (newIdx >= 0 && newIdx < m_tree->topLevelItemCount()) {
        m_tree->setCurrentItem(m_tree->topLevelItem(newIdx));
    }
    emit chaptersReordered();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterManagerDialog::onRenameChapter()
{
    if (!m_project)
        return;

    const int row = selectedChapterIndex();
    if (row < 0 || row >= m_project->chapters().size())
        return;

    Chapter &ch = m_project->chapters()[row];
    bool ok = false;
    const QString newTitle = QInputDialog::getText(
        this,
        QStringLiteral("Rename Chapter"),
        QStringLiteral("Chapter title:"),
        QLineEdit::Normal,
        ch.title(),
        &ok);

    if (!ok || newTitle.trimmed().isEmpty() || newTitle == ch.title())
        return;

    ch.setTitle(newTitle.trimmed());
    m_project->setDirty(true);
    refreshList();
    emit chaptersReordered();
}

void ChapterManagerDialog::onDeleteChapter()
{
    if (!m_project || m_project->chapters().size() <= 1) {
        QMessageBox::information(this, QStringLiteral("Cannot Delete"),
                                 QStringLiteral("A story project must contain at least one chapter."));
        return;
    }

    const int row = selectedChapterIndex();
    if (row < 0 || row >= m_project->chapters().size())
        return;

    const QString title = m_project->chapters()[row].title();
    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Delete Chapter"),
        QStringLiteral("Are you sure you want to delete \"%1\"?\nThis action cannot be undone.")
            .arg(title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    m_project->removeChapter(row);
    refreshList();
    emit chaptersReordered();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterManagerDialog::onMoveToPosition()
{
    if (!m_project)
        return;

    const int currentRow = selectedChapterIndex();
    const int count = m_project->chapters().size();
    if (currentRow < 0 || count <= 1)
        return;

    // Target position is 1-based (min: 1, max: count)
    const int targetPos = m_positionSpin->value();
    if (targetPos < 1 || targetPos > count) {
        QMessageBox::warning(this, QStringLiteral("Invalid Position"),
                             QStringLiteral("Target position must be between 1 and %1. Chapter 0 is not permitted.")
                                 .arg(count));
        return;
    }

    const int targetIndex = targetPos - 1;
    if (targetIndex == currentRow)
        return;

    m_project->moveChapter(currentRow, targetIndex);
    refreshList();
    if (targetIndex >= 0 && targetIndex < m_tree->topLevelItemCount()) {
        m_tree->setCurrentItem(m_tree->topLevelItem(targetIndex));
    }
    emit chaptersReordered();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterManagerDialog::onMoveUp()
{
    if (!m_project)
        return;

    const int row = selectedChapterIndex();
    if (row <= 0)
        return;

    m_project->moveChapter(row, row - 1);
    refreshList();
    if (row - 1 >= 0 && row - 1 < m_tree->topLevelItemCount()) {
        m_tree->setCurrentItem(m_tree->topLevelItem(row - 1));
    }
    emit chaptersReordered();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterManagerDialog::onMoveDown()
{
    if (!m_project)
        return;

    const int row = selectedChapterIndex();
    if (row < 0 || row >= m_project->chapters().size() - 1)
        return;

    m_project->moveChapter(row, row + 1);
    refreshList();
    if (row + 1 < m_tree->topLevelItemCount()) {
        m_tree->setCurrentItem(m_tree->topLevelItem(row + 1));
    }
    emit chaptersReordered();
    emit chapterSelected(m_project->activeChapterIndex());
}

void ChapterManagerDialog::onRowsMoved()
{
    if (!m_project)
        return;

    // Reorder m_project->chapters() based on the new order in m_tree
    QList<Chapter> reordered;
    reordered.reserve(m_tree->topLevelItemCount());

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        const QUuid id = QUuid::fromString(item->data(0, Qt::UserRole).toString());
        for (const Chapter &ch : m_project->chapters()) {
            if (ch.id() == id) {
                Chapter copy = ch;
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
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            m_tree->topLevelItem(i)->setText(0, QStringLiteral("Chapter %1").arg(i + 1));
        }
        m_blockSignals = false;

        updatePositionControls();
        emit chaptersReordered();
        emit chapterSelected(m_project->activeChapterIndex());
    }
}

QString ChapterManagerDialog::computeSyntaxSummary(const QString &html)
{
    static const QRegularExpression openTagRe(QStringLiteral(R"(<([a-zA-Z][a-zA-Z0-9]*)\b[^>]*>)"));
    QRegularExpressionMatchIterator it = openTagRe.globalMatch(html);
    QMap<QString, int> counts;
    while (it.hasNext()) {
        const QString t = it.next().captured(1).toLower();
        if (t == QStringLiteral("html") || t == QStringLiteral("body") || t == QStringLiteral("head") || t == QStringLiteral("root"))
            continue;
        counts[t]++;
    }

    if (counts.isEmpty())
        return QStringLiteral("None (plain text)");

    struct TagStat { QString tag; int count; };
    QList<TagStat> stats;
    for (auto itCount = counts.constBegin(); itCount != counts.constEnd(); ++itCount) {
        stats.append({itCount.key(), itCount.value()});
    }
    std::sort(stats.begin(), stats.end(), [](const TagStat &a, const TagStat &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.tag < b.tag;
    });

    QStringList parts;
    for (const TagStat &st : stats) {
        parts << QStringLiteral("<%1> (%2)").arg(st.tag).arg(st.count);
    }
    return parts.join(QStringLiteral(", "));
}

int ChapterManagerDialog::computeWordCount(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    const QString plain = doc.toPlainText();
    return plain.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

int ChapterManagerDialog::computeLineCount(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    const QString plain = doc.toPlainText();
    const QStringList lines = plain.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return qMax(1, lines.size());
}
