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

namespace EnsembleDebug {

// Initialize the debug system (custom handler, file logger, ANSI color stdout)
void initDebugSystem();

// Get formatted debug log string buffer for UI dialog
QString getLogBuffer();

// Clear log buffer
void clearLogBuffer();

// Check if running in Debug build mode
bool isDebugBuild();

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
