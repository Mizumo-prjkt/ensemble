#include "AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace AppPaths {

QString appDataDir()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path;
}

QString appConfigDir()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(path);
    return path;
}

QString logFilePath()
{
    return QDir(appDataDir()).filePath(QStringLiteral("ensemble-debug.log"));
}

QString sessionFilePath()
{
    return QDir(appDataDir()).filePath(QStringLiteral("session.json"));
}

QString autosaveDir()
{
    const QString path = QDir(appDataDir()).filePath(QStringLiteral("autosaves"));
    QDir().mkpath(path);
    return path;
}

QString pluginsDir()
{
    const QString path = QDir(appDataDir()).filePath(QStringLiteral("plugins"));
    QDir().mkpath(path);
    return path;
}

QString pluginStoreDir()
{
    const QString path = QDir(appDataDir()).filePath(QStringLiteral("plugin_store"));
    QDir().mkpath(path);
    return path;
}

void ensureDirectoriesExist()
{
    appDataDir();
    appConfigDir();
    autosaveDir();
    pluginsDir();
    pluginStoreDir();
}

} // namespace AppPaths
