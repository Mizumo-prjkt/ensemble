#pragma once

#include "ui/ProblemsDialog.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <atomic>

struct DiagnosticsChapterSnapshot {
    int index = 0;
    QString title;
    QString html;
};

struct DiagnosticsJob {
    quint64 jobId = 0;
    QString workSkinCss;
    QStringList cssClasses;
    QList<DiagnosticsChapterSnapshot> chapters;
};
Q_DECLARE_METATYPE(DiagnosticsChapterSnapshot)
Q_DECLARE_METATYPE(DiagnosticsJob)

/**
 * @brief Worker living on an isolated background thread.
 * Executes pure XML, regex, and CSS AST checks without touching the GUI thread.
 */
class DiagnosticsWorker : public QObject {
    Q_OBJECT

public:
    explicit DiagnosticsWorker(QObject *parent = nullptr);

    static void executeJob(const DiagnosticsJob &job,
                           QList<ProblemItem> &problems,
                           int &errorCount,
                           int &warningCount);

public slots:
    void processJob(const DiagnosticsJob &job);

signals:
    void jobFinished(quint64 jobId, const QList<ProblemItem> &problems, int errorCount, int warningCount, qint64 elapsedMs);
};

/**
 * @brief Thread-isolated manager for project-wide diagnostics and linting.
 * Includes a strict Watchdog to terminate stuck threads and report core issues.
 */
class ProjectDiagnosticsRunner : public QObject {
    Q_OBJECT

public:
    static ProjectDiagnosticsRunner &instance();

    explicit ProjectDiagnosticsRunner(QObject *parent = nullptr);
    ~ProjectDiagnosticsRunner() override;

    void requestDiagnostics(const QString &workSkinCss,
                            const QStringList &cssClasses,
                            const QList<DiagnosticsChapterSnapshot> &chapters);

    QList<ProblemItem> runDiagnosticsSync(const QString &workSkinCss,
                                          const QStringList &cssClasses,
                                          const QList<DiagnosticsChapterSnapshot> &chapters,
                                          int *outErrorCount = nullptr,
                                          int *outWarningCount = nullptr);

    bool isRunning() const noexcept { return m_running.load(std::memory_order_relaxed); }
    qint64 lastRunMs() const noexcept { return m_lastRunMs.load(std::memory_order_relaxed); }
    qint64 totalRuns() const noexcept { return m_totalRuns.load(std::memory_order_relaxed); }
    int terminationsCount() const noexcept { return m_terminationsCount.load(std::memory_order_relaxed); }

signals:
    void diagnosticsReady(const QList<ProblemItem> &problems, int errorCount, int warningCount);
    void componentTerminated(const QString &componentName, bool isPlugin, const QString &reason);

private slots:
    void onJobFinished(quint64 jobId, const QList<ProblemItem> &problems, int errorCount, int warningCount, qint64 elapsedMs);
    void onWatchdogTimeout();

private:
    void setupWorkerThread();
    void killAndRestartWorkerThread(const QString &reason);

    QThread *m_thread = nullptr;
    DiagnosticsWorker *m_worker = nullptr;
    QTimer *m_watchdogTimer = nullptr;

    quint64 m_nextJobId = 1;
    quint64 m_activeJobId = 0;
    std::atomic<bool> m_running{false};
    std::atomic<qint64> m_lastRunMs{0};
    std::atomic<qint64> m_totalRuns{0};
    std::atomic<int> m_terminationsCount{0};
};
