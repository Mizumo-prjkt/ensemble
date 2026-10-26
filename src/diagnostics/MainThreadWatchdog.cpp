#include "MainThreadWatchdog.h"
#include "debug/debug.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutexLocker>

#if defined(Q_OS_LINUX)
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

QMutex MainThreadWatchdog::s_checkpointMutex;
QString MainThreadWatchdog::s_currentCheckpoint = QStringLiteral("Idle");
QString MainThreadWatchdog::s_currentCheckpointDetail;
qint64 MainThreadWatchdog::s_checkpointSetTimeMs = 0;

MainThreadWatchdog &MainThreadWatchdog::instance()
{
    static MainThreadWatchdog s_inst;
    return s_inst;
}

MainThreadWatchdog::MainThreadWatchdog(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_LINUX)
    m_mainThreadId.store(static_cast<qint64>(getpid()));
#elif defined(Q_OS_WIN)
    m_mainThreadId.store(static_cast<qint64>(GetCurrentThreadId()));
#endif
    m_lastHeartbeatMs.store(QDateTime::currentMSecsSinceEpoch());
}

MainThreadWatchdog::~MainThreadWatchdog()
{
    stop();
}

void MainThreadWatchdog::start(int warningThresholdMs, int freezeThresholdMs)
{
    if (m_thread && m_thread->isRunning()) {
        return;
    }

    m_warningThresholdMs = qMax(500, warningThresholdMs);
    m_freezeThresholdMs = qMax(m_warningThresholdMs + 500, freezeThresholdMs);
    m_lastHeartbeatMs.store(QDateTime::currentMSecsSinceEpoch());

    m_thread = new WatchdogThread(this);
    m_thread->start(QThread::LowPriority);

    qCInfo(logApp) << "[Watchdog] Main GUI Thread Watchdog started (Warning:" << m_warningThresholdMs
                   << "ms, Freeze:" << m_freezeThresholdMs << "ms)";
}

void MainThreadWatchdog::stop()
{
    if (m_thread) {
        m_thread->requestStop();
        m_thread->wait(1000);
        if (m_thread->isRunning()) {
            m_thread->terminate();
            m_thread->wait(500);
        }
        delete m_thread;
        m_thread = nullptr;
        qCInfo(logApp) << "[Watchdog] Main GUI Thread Watchdog stopped.";
    }
}

bool MainThreadWatchdog::isRunning() const
{
    return m_thread && m_thread->isRunning();
}

void MainThreadWatchdog::pingHeartbeat()
{
    m_lastHeartbeatMs.store(QDateTime::currentMSecsSinceEpoch());
}

void MainThreadWatchdog::setCheckpoint(const QString &operation, const QString &detail)
{
    QMutexLocker locker(&s_checkpointMutex);
    s_currentCheckpoint = operation.isEmpty() ? QStringLiteral("Idle") : operation;
    s_currentCheckpointDetail = detail;
    s_checkpointSetTimeMs = QDateTime::currentMSecsSinceEpoch();
}

QString MainThreadWatchdog::currentCheckpoint()
{
    QMutexLocker locker(&s_checkpointMutex);
    return s_currentCheckpoint;
}

QString MainThreadWatchdog::currentCheckpointDetail()
{
    QMutexLocker locker(&s_checkpointMutex);
    return s_currentCheckpointDetail;
}

bool MainThreadWatchdog::isLagging() const
{
    return m_isLagging.load();
}

bool MainThreadWatchdog::isFrozen() const
{
    return m_isFrozen.load();
}

qint64 MainThreadWatchdog::lastHeartbeatAgeMs() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_lastHeartbeatMs.load();
    return qMax<qint64>(0, now - last);
}

qint64 MainThreadWatchdog::maxRecordedHangMs() const
{
    return m_maxRecordedHangMs.load();
}

QString MainThreadWatchdog::statusDescription() const
{
    if (!isRunning()) {
        return QStringLiteral("Watchdog Offline");
    }

    const qint64 age = lastHeartbeatAgeMs();
    const QString cp = currentCheckpoint();

    if (m_isFrozen.load()) {
        return QStringLiteral("🚨 FROZEN (%1 ms) - Checkpoint: %2").arg(QString::number(age), cp);
    }
    if (m_isLagging.load()) {
        return QStringLiteral("⚠️ LAGGING (%1 ms) - Checkpoint: %2").arg(QString::number(age), cp);
    }
    return QStringLiteral("Responsive (Ping: %1 ms, Checkpoint: %2)").arg(QString::number(age), cp);
}

QString MainThreadWatchdog::diagnoseMainThread() const
{
    QString diag;
#if defined(Q_OS_LINUX)
    const qint64 tid = m_mainThreadId.load();
    const QString statPath = QStringLiteral("/proc/self/task/%1/stat").arg(tid);
    QFile statFile(statPath);
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString line = QString::fromUtf8(statFile.readLine());
        statFile.close();
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 15) {
            const QString stateChar = parts.at(2);
            const QString stateName = (stateChar == QStringLiteral("R")) ? QStringLiteral("Running (CPU Active)")
                                    : (stateChar == QStringLiteral("S")) ? QStringLiteral("Sleeping / Waiting on event")
                                    : (stateChar == QStringLiteral("D")) ? QStringLiteral("Disk Sleep / Uninterruptible")
                                    : (stateChar == QStringLiteral("T")) ? QStringLiteral("Stopped / Traced") : stateChar;
            diag += QStringLiteral("State: %1, utime: %2 ticks, stime: %3 ticks").arg(stateName, parts.at(13), parts.at(14));
        }
    }

    const QString wchanPath = QStringLiteral("/proc/self/task/%1/wchan").arg(tid);
    QFile wchanFile(wchanPath);
    if (wchanFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString wchan = QString::fromUtf8(wchanFile.readAll()).trimmed();
        wchanFile.close();
        if (!wchan.isEmpty()) {
            diag += QStringLiteral(" | Wait Channel: %1").arg(wchan);
        }
    }
#elif defined(Q_OS_WIN)
    diag = QStringLiteral("Windows Main Thread ID: %1").arg(m_mainThreadId.load());
#endif
    return diag.isEmpty() ? QStringLiteral("No OS diagnostics available") : diag;
}

void MainThreadWatchdog::WatchdogThread::run()
{
#if defined(Q_OS_LINUX)
    pthread_setname_np(pthread_self(), "ui-watchdog");
    setpriority(PRIO_PROCESS, 0, 10);
#elif defined(Q_OS_WIN)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    while (!m_stopRequested.load()) {
        msleep(150);

        if (m_stopRequested.load()) {
            break;
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 lastHeartbeat = m_owner->m_lastHeartbeatMs.load();
        const qint64 elapsed = qMax<qint64>(0, now - lastHeartbeat);

        const QString cp = MainThreadWatchdog::currentCheckpoint();
        const QString cpDetail = MainThreadWatchdog::currentCheckpointDetail();

        if (elapsed >= m_owner->m_freezeThresholdMs) {
            if (!m_owner->m_isFrozen.load()) {
                m_owner->m_isFrozen.store(true);
                const QString osDiag = m_owner->diagnoseMainThread();
                qCCritical(logApp) << "[Watchdog] 🚨 CRITICAL: Main GUI Thread FROZEN for" << elapsed
                                   << "ms! Checkpoint:" << cp << "[" << cpDetail << "] | Diagnostics:" << osDiag;
                emit m_owner->mainThreadFrozen(elapsed, cp, cpDetail);
            }
        } else if (elapsed >= m_owner->m_warningThresholdMs) {
            if (!m_owner->m_isLagging.load()) {
                m_owner->m_isLagging.store(true);
                m_owner->m_freezeStartMs.store(now - elapsed);
                const QString osDiag = m_owner->diagnoseMainThread();
                qCWarning(logApp) << "[Watchdog] ⚠️ Main GUI Thread unresponsive for" << elapsed
                                  << "ms. Checkpoint:" << cp << "[" << cpDetail << "] | Diagnostics:" << osDiag;
                emit m_owner->mainThreadLagging(elapsed, cp, cpDetail);
            }
        } else {
            // Heartbeat is responsive
            if (m_owner->m_isLagging.load() || m_owner->m_isFrozen.load()) {
                const qint64 freezeStart = m_owner->m_freezeStartMs.load();
                const qint64 totalHang = (freezeStart > 0) ? (now - freezeStart) : elapsed;

                if (totalHang > m_owner->m_maxRecordedHangMs.load()) {
                    m_owner->m_maxRecordedHangMs.store(totalHang);
                }

                qCInfo(logApp) << "[Watchdog] ✅ Main GUI Thread RECOVERED after" << totalHang
                               << "ms freeze. Culprit checkpoint was:" << cp << "[" << cpDetail << "]";

                m_owner->m_isLagging.store(false);
                m_owner->m_isFrozen.store(false);
                m_owner->m_freezeStartMs.store(0);

                emit m_owner->mainThreadRecovered(totalHang, cp);
            }
        }
    }
}
