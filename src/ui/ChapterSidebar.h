#pragma once

#include <QWidget>

class Ao3Project;
class QLineEdit;
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
    void onSearchFilterChanged(const QString &text);

private:
    void rebuildList();
    void updateIcons();

    Ao3Project *m_project = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_quickAddBtn = nullptr;
    QPushButton *m_managerBtn = nullptr;
    bool m_blockSignals = false;
};
