#pragma once

#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

// Logging categories for Ensemble
Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logEditor)
Q_DECLARE_LOGGING_CATEGORY(logPreview)
Q_DECLARE_LOGGING_CATEGORY(logNetwork)
Q_DECLARE_LOGGING_CATEGORY(logParser)
Q_DECLARE_LOGGING_CATEGORY(logModel)
Q_DECLARE_LOGGING_CATEGORY(logMcp)

namespace EnsembleDebug {

// Initialize the debug system (custom handler, file logger, ANSI color stdout)
void initDebugSystem();

// Set MCP bridge mode (redirects all terminal log output to stderr so stdout is reserved for JSON-RPC)
void setMcpBridgeMode(bool enabled);
bool isMcpBridgeMode();

// Get formatted debug log string buffer for UI dialog
QString getLogBuffer();

// Clear log buffer
void clearLogBuffer();

// Check if running in Debug build mode
bool isDebugBuild();

// Full formatted version string (e.g. "1.1.1 (Debug-dirty)")
QString fullVersion();

// Hardware & system diagnostics information
struct HardwareInfo {
    QString osString;
    QString qpaPlatform;
    QString cpuModel;
    QString cpuArch;
    int cpuThreads = 0;
    QString ramTotal;
    QString ramAvailable;
    QStringList installedGpus;
    QString activeGpuRenderer;
    QString activeGpuVendor;
    QString glVersion;
};

// Query system hardware information (CPU, RAM, installed GPUs, active GPU)
HardwareInfo queryHardwareInfo();

// Format hardware info into human-readable multi-line string
QString formatHardwareReport(const HardwareInfo &info);

// Log hardware diagnostics to logApp
void logHardwareDiagnostics();

// Utility RAII scope timer for profiling performance
class ScopeTimer {
public:
    explicit ScopeTimer(const char *name, const QLoggingCategory &category = logApp());
    ~ScopeTimer();

private:
    const char *m_name;
    const QLoggingCategory &m_category;
    QElapsedTimer m_timer;
};

} // namespace EnsembleDebug

#if defined(QT_DEBUG) || !defined(NDEBUG)
#define ENSEMBLE_PROFILE_SCOPE(name) EnsembleDebug::ScopeTimer _scopeTimer##__LINE__(name, logApp())
#define ENSEMBLE_PROFILE_CAT_SCOPE(name, cat) EnsembleDebug::ScopeTimer _scopeTimer##__LINE__(name, cat())
#else
#define ENSEMBLE_PROFILE_SCOPE(name) do {} while(0)
#define ENSEMBLE_PROFILE_CAT_SCOPE(name, cat) do {} while(0)
#endif
