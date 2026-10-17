#pragma once

#include <QString>
#include <QStringList>

namespace EnsemblePlugin {

namespace PluginLogger {

/**
 * @brief Thread-safe audit logger for plugin events, network requests,
 * permission checks, and behavior.
 */
void log(const QString &publicId,
         const QString &action,
         const QString &details,
         bool allowed = true);

QString logFilePath();
QStringList readRecentLogs(int maxLines = 200);
void clearLogs();

} // namespace PluginLogger

} // namespace EnsemblePlugin
