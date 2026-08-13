#pragma once

#include <QWidget>

class Ao3Project;
class QListWidget;

class ChapterSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit ChapterSidebar(QWidget *parent = nullptr);

    void bindProject(Ao3Project *project);
    void refresh();

signals:
    void chapterSelected(int index);
    void chaptersReordered();

private slots:
    void onAddChapter();
    void onRenameChapter();
    void onDeleteChapter();
    void onMoveUp();
    void onMoveDown();
    void onCurrentRowChanged(int row);
    void onRowsMoved();

private:
    void rebuildList();

    Ao3Project *m_project = nullptr;
    QListWidget *m_list = nullptr;
    bool m_blockSignals = false;
};
