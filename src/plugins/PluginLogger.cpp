#include "PluginLogger.h"
#include "utils/AppPaths.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace EnsemblePlugin {
namespace PluginLogger {

static QMutex s_logMutex;

QString logFilePath() {
    return QDir(AppPaths::pluginsDir()).filePath(QStringLiteral("plugin_activity.log"));
}

void log(const QString &publicId,
         const QString &action,
         const QString &details,
         bool allowed)
{
    QMutexLocker locker(&s_logMutex);
    QDir().mkpath(AppPaths::pluginsDir());

    QFile file(logFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        QString statusStr = allowed ? QStringLiteral("ALLOWED") : QStringLiteral("BLOCKED");
        QString pid = publicId.isEmpty() ? QStringLiteral("system") : publicId;
        out << QStringLiteral("[%1] [%2] [%3] [%4] %5\n")
                   .arg(timestamp, pid, action, statusStr, details);
    }
}

QStringList readRecentLogs(int maxLines) {
    QMutexLocker locker(&s_logMutex);
    QFile file(logFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    if (lines.size() > maxLines) {
        return lines.mid(lines.size() - maxLines);
    }
    return lines;
}

void clearLogs() {
    QMutexLocker locker(&s_logMutex);
    QFile file(logFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.close();
    }
}

} // namespace PluginLogger
} // namespace EnsemblePlugin
