#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QDateTime>
#include <QString>
#include <atomic>

class MainThreadWatchdog : public QObject
{
    Q_OBJECT

public:
    static MainThreadWatchdog &instance();

    // Start / stop monitoring
    void start(int warningThresholdMs = 2000, int freezeThresholdMs = 5000);
    void stop();
    bool isRunning() const;

    // Called periodically by the main GUI thread to signal responsiveness
    void pingHeartbeat();

    // Checkpoint tracking (thread-safe, callable from any thread)
    static void setCheckpoint(const QString &operation, const QString &detail = QString());
    static QString currentCheckpoint();
    static QString currentCheckpointDetail();

    // Scoped RAII checkpoint helper
    struct ScopedCheckpoint {
        explicit ScopedCheckpoint(const QString &op, const QString &detail = QString()) {
            MainThreadWatchdog::setCheckpoint(op, detail);
        }
        ~ScopedCheckpoint() {
            MainThreadWatchdog::setCheckpoint(QStringLiteral("Idle"));
        }
    };

    // Diagnostic query
    bool isLagging() const;
    bool isFrozen() const;
    qint64 lastHeartbeatAgeMs() const;
    qint64 maxRecordedHangMs() const;
    QString statusDescription() const;

    // Diagnose main thread OS state (stat, wchan, CPU ticks)
    QString diagnoseMainThread() const;

signals:
    void mainThreadLagging(qint64 durationMs, const QString &checkpoint, const QString &detail);
    void mainThreadFrozen(qint64 durationMs, const QString &checkpoint, const QString &detail);
    void mainThreadRecovered(qint64 durationMs, const QString &culpritCheckpoint);

private:
    explicit MainThreadWatchdog(QObject *parent = nullptr);
    ~MainThreadWatchdog() override;

    class WatchdogThread : public QThread {
    public:
        explicit WatchdogThread(MainThreadWatchdog *owner) : m_owner(owner) {}
        void run() override;
        void requestStop() { m_stopRequested.store(true); }
    private:
        MainThreadWatchdog *m_owner;
        std::atomic<bool> m_stopRequested{false};
    };

    friend class WatchdogThread;

    std::atomic<qint64> m_lastHeartbeatMs{0};
    std::atomic<bool> m_isLagging{false};
    std::atomic<bool> m_isFrozen{false};
    std::atomic<qint64> m_freezeStartMs{0};
    std::atomic<qint64> m_maxRecordedHangMs{0};
    std::atomic<qint64> m_mainThreadId{0};

    int m_warningThresholdMs = 2000;
    int m_freezeThresholdMs = 5000;

    WatchdogThread *m_thread = nullptr;

    static QMutex s_checkpointMutex;
    static QString s_currentCheckpoint;
    static QString s_currentCheckpointDetail;
    static qint64 s_checkpointSetTimeMs;
};
