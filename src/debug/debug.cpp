#include "debug.hpp"
#include "utils/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRecursiveMutex>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>
#include <cstdio>
#include <fstream>
#include <string>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(logApp, "app")
Q_LOGGING_CATEGORY(logEditor, "editor")
Q_LOGGING_CATEGORY(logPreview, "preview")
Q_LOGGING_CATEGORY(logNetwork, "network")
Q_LOGGING_CATEGORY(logParser, "parser")
Q_LOGGING_CATEGORY(logModel, "model")
Q_LOGGING_CATEGORY(logMcp, "mcp")

namespace EnsembleDebug {

static QRecursiveMutex s_logMutex;
static QString s_logBuffer;
static QFile *s_logFile = nullptr;
static bool s_initialized = false;
static std::atomic<bool> s_mcpBridgeMode{false};

void setMcpBridgeMode(bool enabled)
{
    s_mcpBridgeMode.store(enabled);
}

bool isMcpBridgeMode()
{
    return s_mcpBridgeMode.load();
}

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
    // In MCP Bridge mode, NEVER write to stdout because stdout is strictly reserved for JSON-RPC messages.
    if (isDebugBuild() || type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        if (s_mcpBridgeMode.load() || type == QtCriticalMsg || type == QtFatalMsg) {
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

    qInstallMessageHandler(ensembleMessageHandler);

    const QString logPath = AppPaths::logFilePath();
    s_logFile = new QFile(logPath);
    if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (isDebugBuild() && !s_mcpBridgeMode.load()) {
            qCDebug(logApp) << "Debug log file initialized at:" << logPath;
        }
    }

    if (isDebugBuild() && !s_mcpBridgeMode.load()) {
        qCInfo(logApp) << "=================================================";
        qCInfo(logApp) << " Ensemble Debug Logging System Initialized";
        qCInfo(logApp) << " Build Type:" << ENSEMBLE_BUILD_TYPE;
        qCInfo(logApp) << " Full Version:" << fullVersion();
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
#if defined(QT_DEBUG) || !defined(NDEBUG)
    return true;
#else
    return false;
#endif
}

QString fullVersion()
{
#ifdef ENSEMBLE_APP_VERSION
    return QStringLiteral("%1 (%2%3)").arg(QStringLiteral(ENSEMBLE_APP_VERSION), QStringLiteral(ENSEMBLE_BUILD_TYPE), QStringLiteral(ENSEMBLE_GIT_DIRTY));
#else
    return QStringLiteral("1.1.1 (%1%2)").arg(QStringLiteral(ENSEMBLE_BUILD_TYPE), QStringLiteral(ENSEMBLE_GIT_DIRTY));
#endif
}

HardwareInfo queryHardwareInfo()
{
    HardwareInfo info;
    info.osString = QStringLiteral("%1 (Kernel %2)").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
    info.cpuArch = QSysInfo::currentCpuArchitecture();
    info.cpuThreads = QThread::idealThreadCount();
    if (qApp) {
        info.qpaPlatform = QGuiApplication::platformName();
    }

#if defined(Q_OS_LINUX)
    // CPU Model from /proc/cpuinfo
    std::ifstream cpuFile("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuFile, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto pos = line.find(':');
            if (pos != std::string::npos && pos + 2 < line.size()) {
                info.cpuModel = QString::fromStdString(line.substr(pos + 2)).trimmed();
            }
            break;
        }
    }

    // RAM from /proc/meminfo
    std::ifstream memFile("/proc/meminfo");
    quint64 totalKb = 0, availKb = 0;
    while (std::getline(memFile, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            const auto pos = line.find(':');
            if (pos != std::string::npos) {
                totalKb = std::strtoull(line.c_str() + pos + 1, nullptr, 10);
            }
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            const auto pos = line.find(':');
            if (pos != std::string::npos) {
                availKb = std::strtoull(line.c_str() + pos + 1, nullptr, 10);
            }
        }
    }
    if (totalKb > 0) {
        info.ramTotal = QStringLiteral("%1 GB").arg(QString::number(totalKb / (1024.0 * 1024.0), 'f', 2));
    }
    if (availKb > 0) {
        info.ramAvailable = QStringLiteral("%1 GB").arg(QString::number(availKb / (1024.0 * 1024.0), 'f', 2));
    }

    // Installed GPUs from /sys/bus/pci/devices
    QDir pciDir(QStringLiteral("/sys/bus/pci/devices"));
    const QStringList devs = pciDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &dev : devs) {
        QFile classFile(QStringLiteral("/sys/bus/pci/devices/%1/class").arg(dev));
        if (classFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString classId = QTextStream(&classFile).readLine().trimmed();
            if (classId.startsWith(QLatin1String("0x03"))) {
                QFile vFile(QStringLiteral("/sys/bus/pci/devices/%1/vendor").arg(dev));
                QFile dFile(QStringLiteral("/sys/bus/pci/devices/%1/device").arg(dev));
                QString vendorId, devId;
                if (vFile.open(QIODevice::ReadOnly | QIODevice::Text))
                    vendorId = QTextStream(&vFile).readLine().trimmed();
                if (dFile.open(QIODevice::ReadOnly | QIODevice::Text))
                    devId = QTextStream(&dFile).readLine().trimmed();

                QString vendorName = QStringLiteral("Unknown Vendor");
                QString gpuType = QStringLiteral("GPU");
                if (vendorId == QLatin1String("0x8086")) {
                    vendorName = QStringLiteral("Intel Corporation");
                    gpuType = QStringLiteral("iGPU");
                } else if (vendorId == QLatin1String("0x10de")) {
                    vendorName = QStringLiteral("NVIDIA Corporation");
                    gpuType = QStringLiteral("dGPU");
                } else if (vendorId == QLatin1String("0x1002")) {
                    vendorName = QStringLiteral("AMD / ATI");
                    gpuType = QStringLiteral("GPU");
                }
                info.installedGpus.append(QStringLiteral("[%1] %2 (Vendor %3, Device %4, PCI %5)")
                                              .arg(gpuType, vendorName, vendorId, devId, dev));
            }
        }
    }
#elif defined(Q_OS_WIN)
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        info.ramTotal = QStringLiteral("%1 GB").arg(QString::number(memStatus.ullTotalPhys / (1024.0 * 1024.0 * 1024.0), 'f', 2));
        info.ramAvailable = QStringLiteral("%1 GB").arg(QString::number(memStatus.ullAvailPhys / (1024.0 * 1024.0 * 1024.0), 'f', 2));
    }
    const QString winCpu = qEnvironmentVariable("PROCESSOR_IDENTIFIER");
    if (!winCpu.isEmpty()) {
        info.cpuModel = winCpu;
    }
#endif

    if (info.cpuModel.isEmpty()) {
        info.cpuModel = QStringLiteral("%1 (%2 logical cores)").arg(info.cpuArch, QString::number(info.cpuThreads));
    }

    // Active GPU via QOpenGLContext (if QGuiApplication is running)
    if (qApp) {
        QOffscreenSurface surf;
        surf.create();
        QOpenGLContext ctx;
        if (ctx.create() && ctx.makeCurrent(&surf)) {
            QOpenGLFunctions *f = ctx.functions();
            const char *ren = reinterpret_cast<const char *>(f->glGetString(GL_RENDERER));
            const char *ven = reinterpret_cast<const char *>(f->glGetString(GL_VENDOR));
            const char *ver = reinterpret_cast<const char *>(f->glGetString(GL_VERSION));
            if (ren) info.activeGpuRenderer = QString::fromUtf8(ren);
            if (ven) info.activeGpuVendor = QString::fromUtf8(ven);
            if (ver) info.glVersion = QString::fromUtf8(ver);
            ctx.doneCurrent();
        }
    }

    return info;
}

QString formatHardwareReport(const HardwareInfo &info)
{
    QString report;
    QTextStream out(&report);
    out << "--- System & Hardware Diagnostics ---\n";
    out << "OS: " << info.osString << "\n";
    out << "Platform (QPA): " << (info.qpaPlatform.isEmpty() ? QStringLiteral("N/A") : info.qpaPlatform) << "\n";
    out << "CPU: " << info.cpuModel << " [" << info.cpuArch << "] (" << info.cpuThreads << " threads)\n";
    out << "RAM: " << (info.ramTotal.isEmpty() ? QStringLiteral("Unknown") : info.ramTotal);
    if (!info.ramAvailable.isEmpty()) {
        out << " Total (" << info.ramAvailable << " Available)";
    }
    out << "\n";

    if (!info.installedGpus.isEmpty()) {
        out << "Installed GPUs:\n";
        for (const QString &gpu : info.installedGpus) {
            out << "  • " << gpu << "\n";
        }
    }

    if (!info.activeGpuRenderer.isEmpty()) {
        out << "Active GPU: " << info.activeGpuRenderer;
        if (!info.activeGpuVendor.isEmpty()) {
            out << " (" << info.activeGpuVendor << ")";
        }
        out << "\n";
    }
    if (!info.glVersion.isEmpty()) {
        out << "OpenGL Driver: " << info.glVersion << "\n";
    }
    out << "-------------------------------------";
    return report;
}

void logHardwareDiagnostics()
{
    const HardwareInfo info = queryHardwareInfo();
    qCInfo(logApp) << "=================================================";
    qCInfo(logApp) << " System & Hardware Diagnostics";
    qCInfo(logApp) << " OS:" << info.osString;
    qCInfo(logApp) << " Windowing Platform (QPA):" << (info.qpaPlatform.isEmpty() ? QStringLiteral("N/A") : info.qpaPlatform);
    qCInfo(logApp) << " CPU:" << info.cpuModel << QStringLiteral("[%1, %2 threads]").arg(info.cpuArch).arg(info.cpuThreads);
    qCInfo(logApp) << " RAM:" << QStringLiteral("%1 Total (%2 Available)").arg(info.ramTotal, info.ramAvailable);
    for (const auto &gpu : info.installedGpus) {
        qCInfo(logApp) << " Installed GPU:" << gpu;
    }
    if (!info.activeGpuRenderer.isEmpty()) {
        qCInfo(logApp) << " Active GPU:" << info.activeGpuRenderer << QStringLiteral("(%1)").arg(info.activeGpuVendor);
    }
    if (!info.glVersion.isEmpty()) {
        qCInfo(logApp) << " OpenGL Version:" << info.glVersion;
    }
    qCInfo(logApp) << "=================================================";
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
