#pragma once

#include <QDialog>

class QTextEdit;
class QComboBox;
class QTimer;
class QLabel;
class QPushButton;

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
    void updateTheme();

private:
    QTextEdit *m_logTextEdit = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;
    QLabel *m_filterLabel = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_hwDiagBtn = nullptr;
};
