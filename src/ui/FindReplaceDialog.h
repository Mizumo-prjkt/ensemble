#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QWidget;

class FindReplaceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindReplaceDialog(QWidget *parent = nullptr);

    void setTargetEditor(QWidget *editor);
    void showFind();
    void showReplace();

private slots:
    void onFindNext();
    void onReplace();
    void onReplaceAll();

private:
    QWidget *m_targetEditor = nullptr;

    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QCheckBox *m_caseCheck = nullptr;

    QPushButton *m_findNextBtn = nullptr;
    QPushButton *m_replaceBtn = nullptr;
    QPushButton *m_replaceAllBtn = nullptr;
};
