#pragma once

#include "PluginPermissions.h"
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
    std::function<int()> getChapterCount;
    std::function<QList<QVariantMap>()> getAllChapters;
    std::function<QVariantMap(int index)> getChapter;
    std::function<void(const QString &)> insertText;
    std::function<void(const QString &)> setChapterText;
    std::function<int()> getWordCount;
};

struct StatusCallbacks {
    std::function<void(const QString &pluginId, const QString &msg, int line)> onWarn;
    std::function<void(const QString &pluginId, const QString &msg, int line)> onError;
    std::function<void(const QString &pluginId, const QString &msg)> onNotify;
    std::function<void(const QString &pluginId, const QString &reason, int line)> onQuarantine;
};

struct InterPluginCallbacks {
    std::function<QList<QVariantMap>()> listPlugins;
    std::function<bool(const QString &targetId)> isActive;
    std::function<bool(const QString &senderId, const QString &targetId, const QString &payload)> sendMessage;
};

struct UiCallbacks {
    std::function<void(const QString &winId, const QString &title, int w, int h)> createWindow;
    std::function<void(const QString &winId, const QString &type, const QVariantMap &props)> addWidget;
    std::function<void(const QString &winId)> showWindow;
    std::function<void(const QString &winId)> closeWindow;
    std::function<void(const QString &winId)> clearWindow;
    std::function<void(const QString &title, const QString &text)> showMessageBox;
};

struct NetworkCallbacks {
    std::function<void(const QString &pluginId, qint64 reqDelta, qint64 bytesSent, qint64 bytesRecv, const QString &url, const QString &status)> onActivity;
};

/**
 * @brief Manages plugin identity (public_id vs secret_uidp), storage sandboxing,
 * permissions enforcement, audit logging, and binding host connectors to DSL.
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

    bool isQuarantined() const noexcept { return m_quarantined; }
    void setQuarantined(bool q) noexcept { m_quarantined = q; }

    const PluginPermissions &permissions() const noexcept { return m_permissions; }
    void setPermissions(const PluginPermissions &perms) { m_permissions = perms; }

    void setEditorCallbacks(EditorCallbacks callbacks) { m_editorCallbacks = std::move(callbacks); }
    void setStatusCallbacks(StatusCallbacks callbacks) { m_statusCallbacks = std::move(callbacks); }
    void setInterPluginCallbacks(InterPluginCallbacks callbacks) { m_pluginCallbacks = std::move(callbacks); }
    void setUiCallbacks(UiCallbacks callbacks) { m_uiCallbacks = std::move(callbacks); }
    void setNetworkCallbacks(NetworkCallbacks callbacks) { m_networkCallbacks = std::move(callbacks); }

    // Host connectors exposed to DSL
    ScriptValue createEditorNamespace(ResourceGuard &guard);
    ScriptValue createStorageNamespace(ResourceGuard &guard);
    ScriptValue createStatusNamespace(ResourceGuard &guard);
    ScriptValue createPluginsNamespace(ResourceGuard &guard);
    ScriptValue createHttpNamespace(ResourceGuard &guard);
    ScriptValue createUiNamespace(ResourceGuard &guard);

    // Direct storage operations with strict path-traversal & quota checks
    QString storageRead(const QString &key, int line = -1, int col = -1);
    bool storageWrite(const QString &key, const QString &value, int line = -1, int col = -1);
    bool storageDelete(const QString &key, int line = -1, int col = -1);
    QStringList storageListKeys(int line = -1, int col = -1);

    qint64 calculateUsedStorageBytes() const;
    QString storageDirPath() const;

    const UiCallbacks &uiCallbacks() const noexcept { return m_uiCallbacks; }
    const QString &lastCreatedWindowId() const noexcept { return m_lastCreatedWindowId; }

private:
    void validateKey(const QString &key, int line, int col) const;
    QString resolveKeyPath(const QString &key, int line, int col) const;

    QString m_publicId;
    QString m_name;
    QString m_version;
    QString m_secretUidp; // Strictly private UUID token

    PluginPermissions m_permissions;
    bool m_quarantined = false;

    QString m_lastCreatedWindowId;

    EditorCallbacks m_editorCallbacks;
    StatusCallbacks m_statusCallbacks;
    InterPluginCallbacks m_pluginCallbacks;
    UiCallbacks m_uiCallbacks;
    NetworkCallbacks m_networkCallbacks;
};

} // namespace EnsemblePlugin
