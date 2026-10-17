#pragma once

#include <QDialog>

class Ao3Project;
class QTreeWidget;
class QTreeWidgetItem;
class QSpinBox;
class QPushButton;
class QLabel;

class ChapterManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChapterManagerDialog(Ao3Project *project, QWidget *parent = nullptr);

    int selectedChapterIndex() const;

signals:
    void chapterSelected(int index);
    void chaptersReordered();

private slots:
    void onAddChapter();
    void onRenameChapter();
    void onDeleteChapter();
    void onMoveToPosition();
    void onMoveUp();
    void onMoveDown();
    void onSelectionChanged();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onRowsMoved();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void updateTheme();

private:
    void setupUi();
    void refreshList();
    void updateSummary();
    void updatePositionControls();

    static QString computeSyntaxSummary(const QString &html);
    static int computeWordCount(const QString &html);
    static int computeLineCount(const QString &html);

    Ao3Project *m_project = nullptr;

    QTreeWidget *m_tree = nullptr;
    QSpinBox *m_positionSpin = nullptr;
    QPushButton *m_moveBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_renameBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QLabel *m_summaryLabel = nullptr;

    bool m_blockSignals = false;
};
