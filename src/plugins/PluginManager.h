#pragma once

#include "ResourceGuard.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantMap>
#include <memory>

namespace EnsemblePlugin {

class PluginSandbox;
class ScriptEvaluator;

/**
 * @brief Worker living in a dedicated QThread for a single plugin instance.
 * Completely isolated from GUI pointers.
 */
class PluginWorker : public QObject {
    Q_OBJECT

public:
    explicit PluginWorker(QObject *parent = nullptr);
    ~PluginWorker() override;

    bool isActive() const noexcept { return m_active; }

public slots:
    void initPlugin(const QString &publicId,
                    const QString &name,
                    const QString &version,
                    const QString &scriptSource,
                    const QString &secretUidp,
                    const ResourceLimits &limits = ResourceLimits());

    void handleChapterChange(const QString &title, const QString &text, int chapterIndex);
    void handleToolbarClick(const QString &actionName);
    void handleIncomingMessage(const QString &senderId, const QString &payload);
    void updateEditorSnapshot(const QString &title, const QString &text, const QString &selection);
    void setActive(bool active);

signals:
    void replaceSelectionRequested(const QString &newText);
    void statusWarn(const QString &publicId, const QString &message, int line);
    void statusError(const QString &publicId, const QString &message, int line);
    void statusNotify(const QString &publicId, const QString &message);
    void pluginFaulted(const QString &publicId, const QString &errorMessage, int line);
    void pluginLoaded(const QString &publicId, const QStringList &toolbarActions);
    void sendMessageRequested(const QString &senderId, const QString &targetId, const QString &payload);

private:
    QString m_publicId;
    QString m_name;
    QString m_version;
    QString m_scriptSource;
    bool m_active = true;

    QString m_chapterText;
    QString m_selection;
    QString m_chapterTitle;

    std::unique_ptr<PluginSandbox> m_sandbox;
    std::unique_ptr<ScriptEvaluator> m_evaluator;
};

struct PluginMetadata {
    QString publicId;
    QString name;
    QString version;
    QString filePath;
    QString secretUidp;
    bool active = true;
    bool faulted = false;
    QString faultError;
    int faultLine = -1;
    QStringList toolbarActions;
    QThread *thread = nullptr;
    PluginWorker *worker = nullptr;
};

/**
 * @brief Manages QThread instances per plugin, bridges queued signals to the GUI,
 * and maintains inter-plugin message routing and lifecycle.
 */
class PluginManager : public QObject {
    Q_OBJECT

public:
    static PluginManager &instance();

    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    void initialize();
    void shutdownAll();

    void scanAndLoadPlugins();
    bool loadPluginFromFile(const QString &filePath, QString *errorMessage = nullptr);
    bool loadPluginFromSource(const QString &publicId,
                              const QString &name,
                              const QString &version,
                              const QString &source,
                              const QString &filePath = QString(),
                              QString *errorMessage = nullptr);

    void unloadPlugin(const QString &publicId);
    void reloadPlugin(const QString &publicId);
    void setPluginActive(const QString &publicId, bool active);

    QList<PluginMetadata> plugins() const;
    PluginMetadata plugin(const QString &publicId) const;
    bool hasPlugin(const QString &publicId) const;

    QStringList allToolbarActions() const;

    // Direct public metadata query (excludes secret_uidp)
    QList<QVariantMap> publicPluginList() const;

public slots:
    void notifyChapterChanged(const QString &title, const QString &text, int chapterIndex);
    void notifyEditorSnapshot(const QString &title, const QString &text, const QString &selection);
    void triggerToolbarAction(const QString &actionName);
    void routeMessage(const QString &senderId, const QString &targetId, const QString &payload);

signals:
    void pluginListChanged();
    void pluginStatusWarn(const QString &publicId, const QString &message, int line);
    void pluginStatusError(const QString &publicId, const QString &message, int line);
    void pluginStatusNotify(const QString &publicId, const QString &message);
    void pluginFaulted(const QString &publicId, const QString &errorMessage, int line);
    void replaceSelectionRequested(const QString &newText);
    void toolbarActionsChanged();

private slots:
    void onWorkerPluginLoaded(const QString &publicId, const QStringList &toolbarActions);
    void onWorkerPluginFaulted(const QString &publicId, const QString &errorMessage, int line);

private:
    void loadManifest();
    void saveManifest();
    QString getOrCreateSecretUidp(const QString &publicId);
    static void parsePluginHeader(const QString &source, QString &publicId, QString &name, QString &version);

    QMap<QString, PluginMetadata> m_plugins;
    QMap<QString, QString> m_secretUidpMap; // publicId -> secretUidp
    QString m_lastTitle;
    QString m_lastText;
    QString m_lastSelection;
    int m_lastChapterIndex = -1;
};

} // namespace EnsemblePlugin
