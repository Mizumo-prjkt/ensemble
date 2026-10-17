#pragma once

#include "ResourceGuard.h"
#include "ScriptValue.h"
#include <QList>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace EnsemblePlugin {

struct EditorCallbacks {
    std::function<QString()> getChapterText;
    std::function<QString()> getSelection;
    std::function<void(const QString &)> replaceSelection;
    std::function<QString()> getChapterTitle;
};

struct StatusCallbacks {
    std::function<void(const QString &pluginId, const QString &msg, int line)> onWarn;
    std::function<void(const QString &pluginId, const QString &msg, int line)> onError;
    std::function<void(const QString &pluginId, const QString &msg)> onNotify;
};

struct InterPluginCallbacks {
    std::function<QList<QVariantMap>()> listPlugins;
    std::function<bool(const QString &targetId)> isActive;
    std::function<bool(const QString &senderId, const QString &targetId, const QString &payload)> sendMessage;
};

/**
 * @brief Manages plugin identity (public_id vs secret_uidp), storage sandboxing,
 * and binding host connectors to the DSL scripting environment.
 */
class PluginSandbox {
public:
    PluginSandbox(QString publicId,
                  QString name,
                  QString version,
                  QString secretUidp = QString());

    const QString &publicId() const noexcept { return m_publicId; }
    const QString &name() const noexcept { return m_name; }
    const QString &version() const noexcept { return m_version; }

    // Strictly internal to C++ host! NEVER exposed to script environment.
    const QString &secretUidp() const noexcept { return m_secretUidp; }

    void setEditorCallbacks(EditorCallbacks callbacks) { m_editorCallbacks = std::move(callbacks); }
    void setStatusCallbacks(StatusCallbacks callbacks) { m_statusCallbacks = std::move(callbacks); }
    void setInterPluginCallbacks(InterPluginCallbacks callbacks) { m_pluginCallbacks = std::move(callbacks); }

    // Host connectors exposed to DSL
    ScriptValue createEditorNamespace(ResourceGuard &guard);
    ScriptValue createStorageNamespace(ResourceGuard &guard);
    ScriptValue createStatusNamespace(ResourceGuard &guard);
    ScriptValue createPluginsNamespace(ResourceGuard &guard);

    // Direct storage operations with strict path-traversal & quota checks
    QString storageRead(const QString &key, int line = -1, int col = -1);
    bool storageWrite(const QString &key, const QString &value, int line = -1, int col = -1);
    bool storageDelete(const QString &key, int line = -1, int col = -1);
    QStringList storageListKeys(int line = -1, int col = -1);

    qint64 calculateUsedStorageBytes() const;
    QString storageDirPath() const;

private:
    void validateKey(const QString &key, int line, int col) const;
    QString resolveKeyPath(const QString &key, int line, int col) const;

    QString m_publicId;
    QString m_name;
    QString m_version;
    QString m_secretUidp; // Strictly private UUID token

    EditorCallbacks m_editorCallbacks;
    StatusCallbacks m_statusCallbacks;
    InterPluginCallbacks m_pluginCallbacks;
};

} // namespace EnsemblePlugin
