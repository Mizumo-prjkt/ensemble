#pragma once

#include <QWidget>

class Ao3Project;
class QListWidget;
class QPushButton;

class ChapterSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit ChapterSidebar(QWidget *parent = nullptr);

    void bindProject(Ao3Project *project);
    void refresh();

public slots:
    void onOpenChapterManager();

signals:
    void chapterSelected(int index);
    void chaptersReordered();

private slots:
    void onAddChapter();
    void onRenameChapter();
    void onDeleteChapter();
    void onCurrentRowChanged(int row);
    void onRowsMoved();
    void showContextMenu(const QPoint &pos);

private:
    void rebuildList();

    Ao3Project *m_project = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_managerBtn = nullptr;
    bool m_blockSignals = false;
};
