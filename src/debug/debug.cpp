#include "debug.hpp"
#include "utils/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <cstdio>

Q_LOGGING_CATEGORY(logApp, "app")
Q_LOGGING_CATEGORY(logEditor, "editor")
Q_LOGGING_CATEGORY(logPreview, "preview")
Q_LOGGING_CATEGORY(logNetwork, "network")
Q_LOGGING_CATEGORY(logParser, "parser")
Q_LOGGING_CATEGORY(logModel, "model")

namespace EnsembleDebug {

static QRecursiveMutex s_logMutex;
static QString s_logBuffer;
static QFile *s_logFile = nullptr;
static bool s_initialized = false;

static void ensembleMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // In Release builds, mute debug messages completely
    if (type == QtDebugMsg && !isDebugBuild()) {
        return;
    }

    QMutexLocker locker(&s_logMutex);

    const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    const QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("app");

    QString levelStr;
    QString ansiColor;
    switch (type) {
    case QtDebugMsg:
        levelStr = QStringLiteral("DEBUG");
        ansiColor = QStringLiteral("\033[36m"); // Cyan
        break;
    case QtInfoMsg:
        levelStr = QStringLiteral("INFO ");
        ansiColor = QStringLiteral("\033[32m"); // Green
        break;
    case QtWarningMsg:
        levelStr = QStringLiteral("WARN ");
        ansiColor = QStringLiteral("\033[33m"); // Yellow
        break;
    case QtCriticalMsg:
        levelStr = QStringLiteral("CRIT ");
        ansiColor = QStringLiteral("\033[31m"); // Red
        break;
    case QtFatalMsg:
        levelStr = QStringLiteral("FATAL");
        ansiColor = QStringLiteral("\033[35m"); // Magenta
        break;
    }

    const QString formatted = QStringLiteral("[%1] [%2] [%3] %4")
                                  .arg(timeStr, levelStr, category, msg);

    const QString coloredTerminal = QStringLiteral("%1[%2] [%3] [%4]\033[0m %5")
                                        .arg(ansiColor, timeStr, levelStr, category, msg);

    // Output to stdout/stderr (only for Debug builds or non-debug messages)
    if (isDebugBuild() || type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        if (type == QtCriticalMsg || type == QtFatalMsg) {
            std::fprintf(stderr, "%s\n", coloredTerminal.toLocal8Bit().constData());
            std::fflush(stderr);
        } else {
            std::printf("%s\n", coloredTerminal.toLocal8Bit().constData());
            std::fflush(stdout);
        }
    }

    // Append to memory buffer for UI viewer
    s_logBuffer.append(formatted);
    s_logBuffer.append(QLatin1Char('\n'));
    if (s_logBuffer.length() > 200000) {
        s_logBuffer.remove(0, 50000);
    }

    // Write to log file if available
    if (s_logFile && s_logFile->isOpen()) {
        QTextStream stream(s_logFile);
        stream << formatted << "\n";
        stream.flush();
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

void initDebugSystem()
{
    QMutexLocker locker(&s_logMutex);
    if (s_initialized)
        return;

    s_initialized = true;

    // Filter rules: enable verbose categories in debug builds, mute debug in release builds
    if (isDebugBuild()) {
        QLoggingCategory::setFilterRules(QStringLiteral(
            "app.debug=true\n"
            "editor.debug=true\n"
            "preview.debug=true\n"
            "network.debug=true\n"
            "parser.debug=true\n"
            "model.debug=true\n"
        ));
    } else {
        QLoggingCategory::setFilterRules(QStringLiteral(
            "app.debug=false\n"
            "editor.debug=false\n"
            "preview.debug=false\n"
            "network.debug=false\n"
            "parser.debug=false\n"
            "model.debug=false\n"
        ));
    }

    const QString logPath = AppPaths::logFilePath();
    s_logFile = new QFile(logPath);
    if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (isDebugBuild()) {
            qCDebug(logApp) << "Debug log file initialized at:" << logPath;
        }
    }

    qInstallMessageHandler(ensembleMessageHandler);

    if (isDebugBuild()) {
        qCInfo(logApp) << "=================================================";
        qCInfo(logApp) << " Ensemble Debug Logging System Initialized";
        qCInfo(logApp) << " Build Type:" << ENSEMBLE_BUILD_TYPE;
        qCInfo(logApp) << " Full Version:" << ENSEMBLE_FULL_VERSION;
        qCInfo(logApp) << " Thread ID:" << QThread::currentThread();
        qCInfo(logApp) << "=================================================";
    }
}

QString getLogBuffer()
{
    QMutexLocker locker(&s_logMutex);
    return s_logBuffer;
}

void clearLogBuffer()
{
    QMutexLocker locker(&s_logMutex);
    s_logBuffer.clear();
}

bool isDebugBuild()
{
#ifdef QT_DEBUG
    return true;
#else
    return QStringLiteral(ENSEMBLE_BUILD_TYPE).contains(QStringLiteral("Debug"), Qt::CaseInsensitive);
#endif
}

ScopeTimer::ScopeTimer(const char *name, const QLoggingCategory &category)
    : m_name(name), m_category(category)
{
    m_timer.start();
    qCDebug(m_category) << QStringLiteral("⏱️ [START] %1").arg(m_name);
}

ScopeTimer::~ScopeTimer()
{
    const qint64 elapsedMs = m_timer.elapsed();
    qCDebug(m_category) << QStringLiteral("⏱️ [DONE]  %1 completed in %2 ms").arg(m_name).arg(elapsedMs);
}

} // namespace EnsembleDebug
