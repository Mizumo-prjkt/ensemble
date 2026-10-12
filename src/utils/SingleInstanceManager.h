#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>

class QWidget;

enum class SingleInstanceAction {
    ProceedAsPrimary, // Continue starting this process as the primary instance
    ExitApplication   // Existing instance was notified / raised; this process should terminate cleanly
};

class SingleInstanceManager : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceManager(QWidget *targetWindow = nullptr, QObject *parent = nullptr);
    ~SingleInstanceManager() override;

    // Checks if another instance is already running. If so, sends a raise/open command,
    // warns the user, brings existing window to front, and offers to terminate the existing
    // process if it fails to appear or is unresponsive.
    static SingleInstanceAction checkAndHandle(bool disableGuiPrompt = false, const QString &filePathToOpen = QString());

    // Starts listening on local socket for secondary instances.
    bool startListening();

    // Stops listening and unlinks local socket / pid file.
    void stopListening();

    // Unique socket name for current user
    static QString socketName();

    // PID tracking file path
    static QString pidFilePath();

    // Process manager checks and termination
    static bool isProcessRunning(qint64 pid);
    static bool terminateProcess(qint64 pid);

    void setTargetWindow(QWidget *window) { m_targetWindow = window; }
    QWidget *targetWindow() const { return m_targetWindow; }

signals:
    void openFileRequested(const QString &filePath);

public slots:
    void bringWindowToFront();

private slots:
    void onNewConnection();

private:
    QWidget *m_targetWindow = nullptr;
    QLocalServer m_server;
    bool m_isListening = false;
};
