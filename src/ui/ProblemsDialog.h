#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class Ao3Project;

struct ProblemItem {
    enum Severity { Warning, Error };
    Severity severity;
    QString category;
    QString description;
    int chapterIndex = -1; // -1 for CSS or project level
    QList<int> affectedChapters; // List of chapter indices affected by this problem
};
Q_DECLARE_METATYPE(ProblemItem)

class ProblemsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProblemsDialog(QWidget *parent = nullptr);
    ~ProblemsDialog() override = default;

    void setProblems(const QList<ProblemItem> &problems);

public slots:
    void updateTheme();

signals:
    void chapterSelected(int chapterIndex);
    void cssTabRequested();

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_treeWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QList<ProblemItem> m_problems;
};
