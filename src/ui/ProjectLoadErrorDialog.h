#pragma once

#include "model/ProjectSerializer.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QPlainTextEdit;
class QFrame;

class ProjectLoadErrorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProjectLoadErrorDialog(const ProjectLoadReport &report, QWidget *parent = nullptr);
    ~ProjectLoadErrorDialog() override = default;

private slots:
    void toggleDetails();
    void copyExplanation();

private:
    ProjectLoadReport m_report;
    QPushButton *m_detailsToggleBtn = nullptr;
    QPlainTextEdit *m_detailsEdit = nullptr;
    QPushButton *m_copyBtn = nullptr;
    bool m_detailsVisible = false;
};
