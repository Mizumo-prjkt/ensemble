#pragma once

#include <QDialog>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class Ao3Project;

struct ProblemItem {
    enum Severity { Warning, Error };
    Severity severity;
    QString category;
    QString description;
    int chapterIndex = -1; // -1 for CSS or project level
};

class ProblemsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProblemsDialog(QWidget *parent = nullptr);
    ~ProblemsDialog() override = default;

    void setProblems(const QList<ProblemItem> &problems);

signals:
    void chapterSelected(int chapterIndex);
    void cssTabRequested();

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_treeWidget = nullptr;
    QList<ProblemItem> m_problems;
};
