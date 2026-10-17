#include "PluginManager.h"
#include "PluginSandbox.h"
#include "ScriptEvaluator.h"
#include "utils/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace EnsemblePlugin {

// PluginWorker implementation
PluginWorker::PluginWorker(QObject *parent)
    : QObject(parent)
{
}

PluginWorker::~PluginWorker() = default;

void PluginWorker::initPlugin(const QString &publicId,
                              const QString &name,
                              const QString &version,
                              const QString &scriptSource,
                              const QString &secretUidp,
                              const ResourceLimits &limits)
{
    m_publicId = publicId;
    m_name = name;
    m_version = version;
    m_scriptSource = scriptSource;
    m_active = true;

    m_sandbox = std::make_unique<PluginSandbox>(publicId, name, version, secretUidp);

    EditorCallbacks edCb;
    edCb.getChapterText = [this]() -> QString { return m_chapterText; };
    edCb.getSelection = [this]() -> QString { return m_selection; };
    edCb.replaceSelection = [this](const QString &newText) {
        emit replaceSelectionRequested(newText);
    };
    edCb.getChapterTitle = [this]() -> QString { return m_chapterTitle; };
    m_sandbox->setEditorCallbacks(std::move(edCb));

    StatusCallbacks stCb;
    stCb.onWarn = [this](const QString &pid, const QString &msg, int line) {
        emit statusWarn(pid, msg, line);
    };
    stCb.onError = [this](const QString &pid, const QString &msg, int line) {
        emit statusError(pid, msg, line);
    };
    stCb.onNotify = [this](const QString &pid, const QString &msg) {
        emit statusNotify(pid, msg);
    };
    m_sandbox->setStatusCallbacks(std::move(stCb));

    InterPluginCallbacks ipCb;
    ipCb.listPlugins = []() -> QList<QVariantMap> {
        return PluginManager::instance().publicPluginList();
    };
    ipCb.isActive = [](const QString &tid) -> bool {
        return PluginManager::instance().hasPlugin(tid) && PluginManager::instance().plugin(tid).active;
    };
    ipCb.sendMessage = [this](const QString &sid, const QString &tid, const QString &payload) -> bool {
        emit sendMessageRequested(sid, tid, payload);
        return true;
    };
    m_sandbox->setInterPluginCallbacks(std::move(ipCb));

    m_evaluator = std::make_unique<ScriptEvaluator>(*m_sandbox, limits);

    try {
        m_evaluator->executeScript(m_scriptSource.toStdString());
        emit pluginLoaded(m_publicId, m_evaluator->registeredToolbarActions());
    } catch (const ScriptTrap &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromStdString(e.rawMessage()), e.line());
    } catch (const std::exception &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown runtime error during initialization"), -1);
    }
}

void PluginWorker::handleChapterChange(const QString &title, const QString &text, int chapterIndex) {
    m_chapterTitle = title;
    m_chapterText = text;

    if (!m_active || !m_evaluator) return;

    try {
        m_evaluator->dispatchChapterChange(title, text, chapterIndex);
    } catch (const ScriptTrap &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromStdString(e.rawMessage()), e.line());
    } catch (const std::exception &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during on_chapter_change"), -1);
    }
}

void PluginWorker::handleToolbarClick(const QString &actionName) {
    if (!m_active || !m_evaluator) return;

    try {
        m_evaluator->dispatchToolbarClick(actionName);
    } catch (const ScriptTrap &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromStdString(e.rawMessage()), e.line());
    } catch (const std::exception &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during on_toolbar_click"), -1);
    }
}

void PluginWorker::handleIncomingMessage(const QString &senderId, const QString &payload) {
    if (!m_active || !m_evaluator) return;

    try {
        m_evaluator->dispatchMessage(senderId, payload);
    } catch (const ScriptTrap &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromStdString(e.rawMessage()), e.line());
    } catch (const std::exception &e) {
        m_active = false;
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during on_message"), -1);
    }
}

void PluginWorker::updateEditorSnapshot(const QString &title, const QString &text, const QString &selection) {
    m_chapterTitle = title;
    m_chapterText = text;
    m_selection = selection;
}

void PluginWorker::setActive(bool active) {
    m_active = active;
}

// PluginManager implementation
PluginManager &PluginManager::instance() {
    static PluginManager s_instance;
    return s_instance;
}

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager() {
    shutdownAll();
}

void PluginManager::initialize() {
    loadManifest();
    scanAndLoadPlugins();
}

void PluginManager::shutdownAll() {
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        PluginMetadata &meta = it.value();
        if (meta.thread) {
            meta.thread->quit();
            meta.thread->wait(300);
            delete meta.thread;
            meta.thread = nullptr;
            meta.worker = nullptr;
        }
    }
    m_plugins.clear();
}

void PluginManager::loadManifest() {
    QString manifestPath = QDir(AppPaths::pluginsDir()).filePath(QStringLiteral("manifest.json"));
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            m_secretUidpMap[it.key()] = it.value().toString();
        }
    }
}

void PluginManager::saveManifest() {
    QString manifestPath = QDir(AppPaths::pluginsDir()).filePath(QStringLiteral("manifest.json"));
    QFile file(manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;

    QJsonObject obj;
    for (auto it = m_secretUidpMap.begin(); it != m_secretUidpMap.end(); ++it) {
        obj[it.key()] = it.value();
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QString PluginManager::getOrCreateSecretUidp(const QString &publicId) {
    if (m_secretUidpMap.contains(publicId) && !m_secretUidpMap[publicId].isEmpty()) {
        return m_secretUidpMap[publicId];
    }
    QString uidp = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_secretUidpMap[publicId] = uidp;
    saveManifest();
    return uidp;
}

void PluginManager::parsePluginHeader(const QString &source, QString &publicId, QString &name, QString &version) {
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (!line.startsWith(QLatin1Char('#'))) {
            if (!line.isEmpty()) break; // end of header comments
            continue;
        }
        line = line.mid(1).trimmed();
        if (line.startsWith(QStringLiteral("ID:"), Qt::CaseInsensitive)) {
            publicId = line.mid(3).trimmed();
        } else if (line.startsWith(QStringLiteral("Name:"), Qt::CaseInsensitive)) {
            name = line.mid(5).trimmed();
        } else if (line.startsWith(QStringLiteral("Version:"), Qt::CaseInsensitive)) {
            version = line.mid(8).trimmed();
        }
    }
}

void PluginManager::scanAndLoadPlugins() {
    QDir dir(AppPaths::pluginsDir());
    const QStringList entries = dir.entryList(QStringList() << QStringLiteral("*.py") << QStringLiteral("*.ensp"),
                                              QDir::Files | QDir::Readable);
    for (const QString &fileName : entries) {
        loadPluginFromFile(dir.filePath(fileName));
    }
}

bool PluginManager::loadPluginFromFile(const QString &filePath, QString *errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not read plugin file: ") + filePath;
        return false;
    }

    QString source = QString::fromUtf8(file.readAll());
    QString publicId;
    QString name;
    QString version = QStringLiteral("1.0");

    parsePluginHeader(source, publicId, name, version);
    if (publicId.isEmpty()) {
        publicId = QFileInfo(filePath).baseName();
    }
    if (name.isEmpty()) {
        name = publicId;
    }

    return loadPluginFromSource(publicId, name, version, source, filePath, errorMessage);
}

bool PluginManager::loadPluginFromSource(const QString &publicId,
                                        const QString &name,
                                        const QString &version,
                                        const QString &source,
                                        const QString &filePath,
                                        QString *errorMessage)
{
    if (publicId.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Public ID cannot be empty");
        return false;
    }

    if (m_plugins.contains(publicId)) {
        unloadPlugin(publicId);
    }

    QString secretUidp = getOrCreateSecretUidp(publicId);

    auto thread = new QThread();
    auto worker = new PluginWorker();
    worker->moveToThread(thread);

    PluginMetadata meta;
    meta.publicId = publicId;
    meta.name = name;
    meta.version = version;
    meta.filePath = filePath;
    meta.secretUidp = secretUidp;
    meta.active = true;
    meta.faulted = false;
    meta.thread = thread;
    meta.worker = worker;

    // Connect worker signals
    connect(worker, &PluginWorker::replaceSelectionRequested, this, &PluginManager::replaceSelectionRequested, Qt::QueuedConnection);
    connect(worker, &PluginWorker::statusWarn, this, &PluginManager::pluginStatusWarn, Qt::QueuedConnection);
    connect(worker, &PluginWorker::statusError, this, &PluginManager::pluginStatusError, Qt::QueuedConnection);
    connect(worker, &PluginWorker::statusNotify, this, &PluginManager::pluginStatusNotify, Qt::QueuedConnection);
    connect(worker, &PluginWorker::pluginLoaded, this, &PluginManager::onWorkerPluginLoaded, Qt::QueuedConnection);
    connect(worker, &PluginWorker::pluginFaulted, this, &PluginManager::onWorkerPluginFaulted, Qt::QueuedConnection);
    connect(worker, &PluginWorker::sendMessageRequested, this, &PluginManager::routeMessage, Qt::QueuedConnection);

    // Thread cleanup
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);

    thread->start();

    // Initialize worker inside its own thread
    QMetaObject::invokeMethod(worker, "initPlugin", Qt::QueuedConnection,
                              Q_ARG(QString, publicId),
                              Q_ARG(QString, name),
                              Q_ARG(QString, version),
                              Q_ARG(QString, source),
                              Q_ARG(QString, secretUidp),
                              Q_ARG(ResourceLimits, ResourceLimits()));

    // Seed current editor snapshot if available
    if (!m_lastText.isEmpty() || !m_lastTitle.isEmpty()) {
        QMetaObject::invokeMethod(worker, "updateEditorSnapshot", Qt::QueuedConnection,
                                  Q_ARG(QString, m_lastTitle),
                                  Q_ARG(QString, m_lastText),
                                  Q_ARG(QString, m_lastSelection));
    }

    m_plugins[publicId] = meta;
    emit pluginListChanged();
    return true;
}

void PluginManager::unloadPlugin(const QString &publicId) {
    if (!m_plugins.contains(publicId)) return;
    PluginMetadata meta = m_plugins.take(publicId);
    if (meta.thread) {
        meta.thread->quit();
        meta.thread->wait(300);
        delete meta.thread;
    }
    emit pluginListChanged();
    emit toolbarActionsChanged();
}

void PluginManager::reloadPlugin(const QString &publicId) {
    if (!m_plugins.contains(publicId)) return;
    QString path = m_plugins[publicId].filePath;
    if (!path.isEmpty() && QFile::exists(path)) {
        loadPluginFromFile(path);
    }
}

void PluginManager::setPluginActive(const QString &publicId, bool active) {
    if (!m_plugins.contains(publicId)) return;
    m_plugins[publicId].active = active;
    if (m_plugins[publicId].worker) {
        QMetaObject::invokeMethod(m_plugins[publicId].worker, "setActive", Qt::QueuedConnection,
                                  Q_ARG(bool, active));
    }
    emit pluginListChanged();
}

QList<PluginMetadata> PluginManager::plugins() const {
    return m_plugins.values();
}

PluginMetadata PluginManager::plugin(const QString &publicId) const {
    return m_plugins.value(publicId);
}

bool PluginManager::hasPlugin(const QString &publicId) const {
    return m_plugins.contains(publicId);
}

QStringList PluginManager::allToolbarActions() const {
    QStringList actions;
    for (const auto &p : m_plugins) {
        if (p.active && !p.faulted) {
            actions.append(p.toolbarActions);
        }
    }
    actions.removeDuplicates();
    return actions;
}

QList<QVariantMap> PluginManager::publicPluginList() const {
    QList<QVariantMap> list;
    for (const auto &p : m_plugins) {
        QVariantMap map;
        map[QStringLiteral("id")] = p.publicId;
        map[QStringLiteral("name")] = p.name;
        map[QStringLiteral("version")] = p.version;
        // Strictly exclude secretUidp!
        list.append(map);
    }
    return list;
}

void PluginManager::notifyChapterChanged(const QString &title, const QString &text, int chapterIndex) {
    m_lastTitle = title;
    m_lastText = text;
    m_lastChapterIndex = chapterIndex;

    for (const auto &p : m_plugins) {
        if (p.active && !p.faulted && p.worker) {
            QMetaObject::invokeMethod(p.worker, "handleChapterChange", Qt::QueuedConnection,
                                      Q_ARG(QString, title),
                                      Q_ARG(QString, text),
                                      Q_ARG(int, chapterIndex));
        }
    }
}

void PluginManager::notifyEditorSnapshot(const QString &title, const QString &text, const QString &selection) {
    m_lastTitle = title;
    m_lastText = text;
    m_lastSelection = selection;

    for (const auto &p : m_plugins) {
        if (p.active && !p.faulted && p.worker) {
            QMetaObject::invokeMethod(p.worker, "updateEditorSnapshot", Qt::QueuedConnection,
                                      Q_ARG(QString, title),
                                      Q_ARG(QString, text),
                                      Q_ARG(QString, selection));
        }
    }
}

void PluginManager::triggerToolbarAction(const QString &actionName) {
    for (const auto &p : m_plugins) {
        if (p.active && !p.faulted && p.worker) {
            QMetaObject::invokeMethod(p.worker, "handleToolbarClick", Qt::QueuedConnection,
                                      Q_ARG(QString, actionName));
        }
    }
}

void PluginManager::routeMessage(const QString &senderId, const QString &targetId, const QString &payload) {
    if (m_plugins.contains(targetId)) {
        const auto &target = m_plugins[targetId];
        if (target.active && !target.faulted && target.worker) {
            QMetaObject::invokeMethod(target.worker, "handleIncomingMessage", Qt::QueuedConnection,
                                      Q_ARG(QString, senderId),
                                      Q_ARG(QString, payload));
        }
    }
}

void PluginManager::onWorkerPluginLoaded(const QString &publicId, const QStringList &toolbarActions) {
    if (m_plugins.contains(publicId)) {
        m_plugins[publicId].toolbarActions = toolbarActions;
        m_plugins[publicId].faulted = false;
        m_plugins[publicId].faultError.clear();
        emit toolbarActionsChanged();
        emit pluginListChanged();
    }
}

void PluginManager::onWorkerPluginFaulted(const QString &publicId, const QString &errorMessage, int line) {
    if (m_plugins.contains(publicId)) {
        m_plugins[publicId].faulted = true;
        m_plugins[publicId].faultError = errorMessage;
        m_plugins[publicId].faultLine = line;
        emit pluginFaulted(publicId, errorMessage, line);
        emit pluginListChanged();
        emit toolbarActionsChanged();
    }
}

} // namespace EnsemblePlugin
