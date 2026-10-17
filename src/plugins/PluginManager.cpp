#include "PluginManager.h"
#include "PluginLogger.h"
#include "PluginPermissions.h"
#include "PluginSandbox.h"
#include "ScriptEvaluator.h"
#include "ui/PluginHostDialog.h"
#include "utils/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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
                              const PluginPermissions &permissions,
                              const ResourceLimits &limits)
{
    m_publicId = publicId;
    m_name = name;
    m_version = version;
    m_scriptSource = scriptSource;
    m_active = true;

    m_sandbox = std::make_unique<PluginSandbox>(publicId, name, version, secretUidp);
    m_sandbox->setPermissions(permissions);

    EditorCallbacks edCb;
    edCb.getChapterText = [this]() -> QString { return m_chapterText; };
    edCb.getSelection = [this]() -> QString { return m_selection; };
    edCb.replaceSelection = [this](const QString &newText) {
        emit replaceSelectionRequested(newText);
    };
    edCb.getChapterTitle = [this]() -> QString { return m_chapterTitle; };
    edCb.getChapterCount = [this]() -> int {
        return m_chapters.isEmpty() ? (m_chapterText.isEmpty() ? 0 : 1) : m_chapters.size();
    };
    edCb.getAllChapters = [this]() -> QList<QVariantMap> { return m_chapters; };
    edCb.getChapter = [this](int idx) -> QVariantMap {
        if (idx >= 0 && idx < m_chapters.size()) {
            return m_chapters.at(idx);
        }
        return QVariantMap();
    };
    edCb.insertText = [this](const QString &t) {
        emit insertTextRequested(t);
    };
    edCb.setChapterText = [this](const QString &t) {
        emit setChapterTextRequested(t);
    };
    edCb.getWordCount = [this]() -> int {
        return m_chapterText.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    };
    m_sandbox->setEditorCallbacks(std::move(edCb));

    UiCallbacks uiCb;
    uiCb.createWindow = [this](const QString &winId, const QString &title, int w, int h) {
        emit uiCreateWindowRequested(m_publicId, winId, title, w, h);
    };
    uiCb.addWidget = [this](const QString &winId, const QString &type, const QVariantMap &props) {
        emit uiAddWidgetRequested(winId, type, props);
    };
    uiCb.showWindow = [this](const QString &winId) {
        emit uiShowWindowRequested(winId);
    };
    uiCb.closeWindow = [this](const QString &winId) {
        emit uiCloseWindowRequested(winId);
    };
    m_sandbox->setUiCallbacks(std::move(uiCb));

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
    stCb.onQuarantine = [this](const QString &pid, const QString &reason, int line) {
        m_active = false;
        emit pluginQuarantined(pid, reason, line);
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
        PluginLogger::log(m_publicId, QStringLiteral("LOAD"), QStringLiteral("Plugin loaded successfully"), true);
    } catch (const ScriptTrap &e) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("LOAD"), QStringLiteral("Faulted on init: %1 (line %2)").arg(QString::fromStdString(e.rawMessage())).arg(e.line()), false);
        emit pluginFaulted(m_publicId, QString::fromStdString(e.rawMessage()), e.line());
    } catch (const std::exception &e) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("LOAD"), QStringLiteral("Faulted on init: %1").arg(QString::fromUtf8(e.what())), false);
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("LOAD"), QStringLiteral("Unknown runtime error during initialization"), false);
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown runtime error during initialization"), -1);
    }
}

void PluginWorker::setPermissions(const PluginPermissions &permissions) {
    if (m_sandbox) {
        m_sandbox->setPermissions(permissions);
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
        QString err = QString::fromStdString(e.rawMessage());
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_CHAPTER_CHANGE"), err, false);
        if (err.contains(QStringLiteral("Security Warning:")) || err.contains(QStringLiteral("Security Violation:")) || err.contains(QStringLiteral("quarantined"), Qt::CaseInsensitive)) {
            emit pluginQuarantined(m_publicId, err, e.line());
        } else {
            emit pluginFaulted(m_publicId, err, e.line());
        }
    } catch (const std::exception &e) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_CHAPTER_CHANGE"), QString::fromUtf8(e.what()), false);
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_CHAPTER_CHANGE"), QStringLiteral("Unknown error"), false);
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during on_chapter_change"), -1);
    }
}

void PluginWorker::handleToolbarClick(const QString &actionName) {
    if (!m_active || !m_evaluator) return;

    try {
        m_evaluator->dispatchToolbarClick(actionName);
    } catch (const ScriptTrap &e) {
        m_active = false;
        QString err = QString::fromStdString(e.rawMessage());
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_TOOLBAR_CLICK"), err, false);
        if (err.contains(QStringLiteral("Security Warning:")) || err.contains(QStringLiteral("Security Violation:")) || err.contains(QStringLiteral("quarantined"), Qt::CaseInsensitive)) {
            emit pluginQuarantined(m_publicId, err, e.line());
        } else {
            emit pluginFaulted(m_publicId, err, e.line());
        }
    } catch (const std::exception &e) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_TOOLBAR_CLICK"), QString::fromUtf8(e.what()), false);
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_TOOLBAR_CLICK"), QStringLiteral("Unknown error"), false);
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during on_toolbar_click"), -1);
    }
}

void PluginWorker::handleWindowAction(const QString &winId, const QString &actionName, const QVariantMap &inputs) {
    if (!m_active || !m_evaluator) return;

    try {
        m_evaluator->dispatchWindowAction(winId, actionName, inputs);
    } catch (const ScriptTrap &e) {
        m_active = false;
        QString err = QString::fromStdString(e.rawMessage());
        PluginLogger::log(m_publicId, QStringLiteral("WINDOW_ACTION"), err, false);
        if (err.contains(QStringLiteral("Security Warning:")) || err.contains(QStringLiteral("Security Violation:")) || err.contains(QStringLiteral("quarantined"), Qt::CaseInsensitive)) {
            emit pluginQuarantined(m_publicId, err, e.line());
        } else {
            emit pluginFaulted(m_publicId, err, e.line());
        }
    } catch (const std::exception &e) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("WINDOW_ACTION"), QString::fromUtf8(e.what()), false);
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("WINDOW_ACTION"), QStringLiteral("Unknown error"), false);
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during window action"), -1);
    }
}

void PluginWorker::handleIncomingMessage(const QString &senderId, const QString &payload) {
    if (!m_active || !m_evaluator) return;

    try {
        m_evaluator->dispatchMessage(senderId, payload);
    } catch (const ScriptTrap &e) {
        m_active = false;
        QString err = QString::fromStdString(e.rawMessage());
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_MESSAGE"), err, false);
        if (err.contains(QStringLiteral("Security Warning:")) || err.contains(QStringLiteral("Security Violation:")) || err.contains(QStringLiteral("quarantined"), Qt::CaseInsensitive)) {
            emit pluginQuarantined(m_publicId, err, e.line());
        } else {
            emit pluginFaulted(m_publicId, err, e.line());
        }
    } catch (const std::exception &e) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_MESSAGE"), QString::fromUtf8(e.what()), false);
        emit pluginFaulted(m_publicId, QString::fromUtf8(e.what()), -1);
    } catch (...) {
        m_active = false;
        PluginLogger::log(m_publicId, QStringLiteral("HOOK_MESSAGE"), QStringLiteral("Unknown error"), false);
        emit pluginFaulted(m_publicId, QStringLiteral("Unknown error during on_message"), -1);
    }
}

void PluginWorker::updateEditorSnapshot(const QString &title, const QString &text, const QString &selection) {
    m_chapterTitle = title;
    m_chapterText = text;
    m_selection = selection;
}

void PluginWorker::updateChaptersSnapshot(const QList<QVariantMap> &chapters) {
    m_chapters = chapters;
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
    // Close open host dialogs
    for (auto *dlg : m_openWindows) {
        if (dlg) {
            dlg->close();
            delete dlg;
        }
    }
    m_openWindows.clear();

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
        QJsonObject root = doc.object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            QString pid = it.key();
            if (it.value().isString()) {
                // Legacy format: string secret_uidp
                m_secretUidpMap[pid] = it.value().toString();
            } else if (it.value().isObject()) {
                QJsonObject itemObj = it.value().toObject();
                m_secretUidpMap[pid] = itemObj.value(QStringLiteral("secretUidp")).toString();
                if (itemObj.contains(QStringLiteral("active"))) {
                    m_savedActiveMap[pid] = itemObj.value(QStringLiteral("active")).toBool(true);
                }
                if (itemObj.contains(QStringLiteral("permissions"))) {
                    m_savedPermissionsMap[pid] = PluginPermissions::fromJson(itemObj.value(QStringLiteral("permissions")).toObject());
                }
            }
        }
    }
}

void PluginManager::saveManifest() {
    QString manifestPath = QDir(AppPaths::pluginsDir()).filePath(QStringLiteral("manifest.json"));
    QDir().mkpath(AppPaths::pluginsDir());

    QFile file(manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;

    QJsonObject root;
    for (auto it = m_secretUidpMap.begin(); it != m_secretUidpMap.end(); ++it) {
        QString pid = it.key();
        QJsonObject itemObj;
        itemObj[QStringLiteral("secretUidp")] = it.value();

        bool active = m_savedActiveMap.value(pid, true);
        if (m_plugins.contains(pid)) {
            active = m_plugins[pid].active;
        }
        itemObj[QStringLiteral("active")] = active;

        PluginPermissions perms;
        if (m_plugins.contains(pid)) {
            perms = m_plugins[pid].permissions;
        } else if (m_savedPermissionsMap.contains(pid)) {
            perms = m_savedPermissionsMap[pid];
        }
        itemObj[QStringLiteral("permissions")] = perms.toJson();

        root[pid] = itemObj;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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
    // Prioritize .er files (Ensemble Runner), with legacy .py and .ensp fallback
    const QStringList erEntries = dir.entryList(QStringList() << QStringLiteral("*.er"),
                                                QDir::Files | QDir::Readable);
    QSet<QString> loadedBases;
    for (const QString &fileName : erEntries) {
        loadedBases.insert(QFileInfo(fileName).completeBaseName());
        loadPluginFromFile(dir.filePath(fileName));
    }

    const QStringList legacyEntries = dir.entryList(QStringList() << QStringLiteral("*.py") << QStringLiteral("*.ensp"),
                                                   QDir::Files | QDir::Readable);
    for (const QString &fileName : legacyEntries) {
        QString base = QFileInfo(fileName).completeBaseName();
        if (!loadedBases.contains(base)) {
            loadPluginFromFile(dir.filePath(fileName));
        }
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

    return loadPluginFromSource(publicId, name, version, source, filePath, nullptr, errorMessage);
}

bool PluginManager::loadPluginFromSource(const QString &publicId,
                                        const QString &name,
                                        const QString &version,
                                        const QString &source,
                                        const QString &filePath,
                                        const PluginPermissions *perms,
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

    // Static analysis of capabilities and domains
    PluginPermissions effectivePerms;
    PluginScanner::scan(source, effectivePerms);

    if (perms) {
        effectivePerms.allowEditorRead = perms->allowEditorRead;
        effectivePerms.allowEditorWrite = perms->allowEditorWrite;
        effectivePerms.allowStorage = perms->allowStorage;
        effectivePerms.allowUi = perms->allowUi;
        effectivePerms.allowNetwork = perms->allowNetwork;
        effectivePerms.allowedDomains = perms->allowedDomains;
    } else if (m_savedPermissionsMap.contains(publicId)) {
        const auto &saved = m_savedPermissionsMap[publicId];
        effectivePerms.allowEditorRead = saved.allowEditorRead;
        effectivePerms.allowEditorWrite = saved.allowEditorWrite;
        effectivePerms.allowStorage = saved.allowStorage;
        effectivePerms.allowUi = saved.allowUi;
        effectivePerms.allowNetwork = saved.allowNetwork;
        effectivePerms.allowedDomains = saved.allowedDomains;
    }

    bool active = m_savedActiveMap.value(publicId, true);

    auto thread = new QThread();
    auto worker = new PluginWorker();
    worker->moveToThread(thread);

    PluginMetadata meta;
    meta.publicId = publicId;
    meta.name = name;
    meta.version = version;
    meta.filePath = filePath;
    meta.secretUidp = secretUidp;
    meta.active = active;
    meta.faulted = false;
    meta.permissions = effectivePerms;
    meta.thread = thread;
    meta.worker = worker;

    // Connect worker signals
    connect(worker, &PluginWorker::replaceSelectionRequested, this, &PluginManager::replaceSelectionRequested, Qt::QueuedConnection);
    connect(worker, &PluginWorker::insertTextRequested, this, &PluginManager::insertTextRequested, Qt::QueuedConnection);
    connect(worker, &PluginWorker::setChapterTextRequested, this, &PluginManager::setChapterTextRequested, Qt::QueuedConnection);

    // Declarative UI connections
    connect(worker, &PluginWorker::uiCreateWindowRequested, this, &PluginManager::onUiCreateWindow, Qt::QueuedConnection);
    connect(worker, &PluginWorker::uiAddWidgetRequested, this, &PluginManager::onUiAddWidget, Qt::QueuedConnection);
    connect(worker, &PluginWorker::uiShowWindowRequested, this, &PluginManager::onUiShowWindow, Qt::QueuedConnection);
    connect(worker, &PluginWorker::uiCloseWindowRequested, this, &PluginManager::onUiCloseWindow, Qt::QueuedConnection);

    connect(worker, &PluginWorker::statusWarn, this, &PluginManager::pluginStatusWarn, Qt::QueuedConnection);
    connect(worker, &PluginWorker::statusError, this, &PluginManager::pluginStatusError, Qt::QueuedConnection);
    connect(worker, &PluginWorker::statusNotify, this, &PluginManager::pluginStatusNotify, Qt::QueuedConnection);
    connect(worker, &PluginWorker::pluginLoaded, this, &PluginManager::onWorkerPluginLoaded, Qt::QueuedConnection);
    connect(worker, &PluginWorker::pluginFaulted, this, &PluginManager::onWorkerPluginFaulted, Qt::QueuedConnection);
    connect(worker, &PluginWorker::pluginQuarantined, this, &PluginManager::onWorkerPluginQuarantined, Qt::QueuedConnection);
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
                              Q_ARG(PluginPermissions, effectivePerms),
                              Q_ARG(ResourceLimits, ResourceLimits()));

    // Seed current editor snapshot if available
    if (!m_lastText.isEmpty() || !m_lastTitle.isEmpty()) {
        QMetaObject::invokeMethod(worker, "updateEditorSnapshot", Qt::QueuedConnection,
                                  Q_ARG(QString, m_lastTitle),
                                  Q_ARG(QString, m_lastText),
                                  Q_ARG(QString, m_lastSelection));
    }
    if (!m_lastChapters.isEmpty()) {
        QMetaObject::invokeMethod(worker, "updateChaptersSnapshot", Qt::QueuedConnection,
                                  Q_ARG(QList<QVariantMap>, m_lastChapters));
    }

    m_plugins[publicId] = meta;
    m_savedPermissionsMap[publicId] = effectivePerms;
    m_savedActiveMap[publicId] = active;
    saveManifest();

    emit pluginListChanged();
    return true;
}

void PluginManager::unloadPlugin(const QString &publicId) {
    if (!m_plugins.contains(publicId)) return;

    // Close any windows belonging to this plugin
    QStringList winsToClose;
    for (auto it = m_openWindows.begin(); it != m_openWindows.end(); ++it) {
        if (it.value() && it.value()->publicId() == publicId) {
            winsToClose.append(it.key());
        }
    }
    for (const QString &wid : winsToClose) {
        onUiCloseWindow(wid);
    }

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
    m_savedActiveMap[publicId] = active;
    saveManifest();

    if (m_plugins[publicId].worker) {
        QMetaObject::invokeMethod(m_plugins[publicId].worker, "setActive", Qt::QueuedConnection,
                                  Q_ARG(bool, active));
    }
    emit pluginListChanged();
}

void PluginManager::setPluginPermissions(const QString &publicId, const PluginPermissions &permissions) {
    if (!m_plugins.contains(publicId)) return;
    m_plugins[publicId].permissions = permissions;
    m_savedPermissionsMap[publicId] = permissions;
    saveManifest();

    if (m_plugins[publicId].worker) {
        QMetaObject::invokeMethod(m_plugins[publicId].worker, "setPermissions", Qt::QueuedConnection,
                                  Q_ARG(PluginPermissions, permissions));
    }
    emit pluginListChanged();
}

bool PluginManager::uninstallPlugin(const QString &publicId, bool deleteStorage, QString *errorMessage) {
    if (!m_plugins.contains(publicId)) {
        if (errorMessage) *errorMessage = QStringLiteral("Plugin '%1' not found").arg(publicId);
        return false;
    }

    PluginMetadata meta = m_plugins[publicId];
    QString filePath = meta.filePath;
    QString uidp = meta.secretUidp;

    unloadPlugin(publicId);

    // Delete script file
    if (!filePath.isEmpty() && QFile::exists(filePath)) {
        if (!QFile::remove(filePath)) {
            if (errorMessage) *errorMessage = QStringLiteral("Could not delete plugin script file: ") + filePath;
        }
    }

    // Delete private storage directory
    if (deleteStorage && !uidp.isEmpty()) {
        QString storeDir = QDir::cleanPath(AppPaths::appDataDir() + QStringLiteral("/plugin_store/") + uidp);
        QDir(storeDir).removeRecursively();
    }

    m_secretUidpMap.remove(publicId);
    m_savedPermissionsMap.remove(publicId);
    m_savedActiveMap.remove(publicId);
    saveManifest();

    PluginLogger::log(publicId, QStringLiteral("UNINSTALL"),
                      QStringLiteral("Plugin uninstalled and deleted from disk"), true);

    emit pluginListChanged();
    return true;
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

void PluginManager::notifyChaptersUpdated(const QList<QVariantMap> &chapters) {
    m_lastChapters = chapters;

    for (const auto &p : m_plugins) {
        if (p.active && !p.faulted && p.worker) {
            QMetaObject::invokeMethod(p.worker, "updateChaptersSnapshot", Qt::QueuedConnection,
                                      Q_ARG(QList<QVariantMap>, chapters));
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

void PluginManager::onWorkerPluginQuarantined(const QString &publicId, const QString &reason, int line) {
    if (m_plugins.contains(publicId)) {
        m_plugins[publicId].quarantined = true;
        m_plugins[publicId].active = false;
        m_plugins[publicId].faulted = true;
        m_plugins[publicId].faultError = reason;
        m_plugins[publicId].faultLine = line;

        // Immediately close any open UI windows created by this plugin
        QStringList winsToClose;
        for (auto it = m_openWindows.begin(); it != m_openWindows.end(); ++it) {
            if (it.value() && it.value()->publicId() == publicId) {
                winsToClose.append(it.key());
            }
        }
        for (const QString &wid : winsToClose) {
            onUiCloseWindow(wid);
        }

        saveManifest();
        emit pluginStatusError(publicId, reason, line);
        emit pluginFaulted(publicId, reason, line);
        emit pluginQuarantined(publicId, reason);
        emit pluginListChanged();
        emit toolbarActionsChanged();
    }
}

// Declarative UI hosting implementation
void PluginManager::onUiCreateWindow(const QString &publicId, const QString &winId, const QString &title, int w, int h) {
    if (m_openWindows.contains(winId)) {
        onUiCloseWindow(winId);
    }

    auto *dialog = new PluginHostDialog(publicId, winId, title, w, h);
    connect(dialog, &PluginHostDialog::actionTriggered, this, &PluginManager::onWindowActionTriggered);
    connect(dialog, &PluginHostDialog::dialogClosed, this, [this](const QString &closedWinId) {
        m_openWindows.remove(closedWinId);
    });

    m_openWindows[winId] = dialog;
}

void PluginManager::onUiAddWidget(const QString &winId, const QString &type, const QVariantMap &props) {
    if (!m_openWindows.contains(winId)) return;
    auto *dialog = m_openWindows[winId];

    if (type == QStringLiteral("label")) {
        dialog->addLabel(props.value(QStringLiteral("text")).toString());
    } else if (type == QStringLiteral("text")) {
        dialog->addText(props.value(QStringLiteral("content")).toString(),
                        props.value(QStringLiteral("readOnly"), true).toBool());
    } else if (type == QStringLiteral("input")) {
        dialog->addInput(props.value(QStringLiteral("placeholder")).toString(),
                         props.value(QStringLiteral("defaultText")).toString());
    } else if (type == QStringLiteral("button")) {
        dialog->addButton(props.value(QStringLiteral("label")).toString(),
                          props.value(QStringLiteral("action")).toString());
    }
}

void PluginManager::onUiShowWindow(const QString &winId) {
    if (m_openWindows.contains(winId)) {
        m_openWindows[winId]->show();
        m_openWindows[winId]->raise();
        m_openWindows[winId]->activateWindow();
    }
}

void PluginManager::onUiCloseWindow(const QString &winId) {
    if (m_openWindows.contains(winId)) {
        auto *dlg = m_openWindows.take(winId);
        if (dlg) {
            dlg->close();
            dlg->deleteLater();
        }
    }
}

void PluginManager::onWindowActionTriggered(const QString &publicId, const QString &winId, const QString &actionName, const QVariantMap &inputs) {
    if (m_plugins.contains(publicId)) {
        const auto &meta = m_plugins[publicId];
        if (meta.active && !meta.faulted && meta.worker) {
            QMetaObject::invokeMethod(meta.worker, "handleWindowAction", Qt::QueuedConnection,
                                      Q_ARG(QString, winId),
                                      Q_ARG(QString, actionName),
                                      Q_ARG(QVariantMap, inputs));
        }
    }
}

} // namespace EnsemblePlugin
