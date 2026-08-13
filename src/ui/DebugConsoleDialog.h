#pragma once

#include <QDialog>

class QTextEdit;
class QComboBox;
class QTimer;

class DebugConsoleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DebugConsoleDialog(QWidget *parent = nullptr);
    ~DebugConsoleDialog() override = default;

public slots:
    void refreshLogs();
    void clearLogs();
    void copyLogs();

private:
    QTextEdit *m_logTextEdit = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;
};
