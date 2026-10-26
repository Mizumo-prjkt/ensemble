#include "PluginSandbox.h"
#include "PluginLogger.h"
#include "utils/AppPaths.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace EnsemblePlugin {

static constexpr qint64 MAX_DISK_STORAGE_BYTES = 5 * 1024 * 1024; // 5 MB quota

PluginSandbox::PluginSandbox(QString publicId,
                             QString name,
                             QString version,
                             QString secretUidp)
    : m_publicId(std::move(publicId))
    , m_name(std::move(name))
    , m_version(std::move(version))
    , m_secretUidp(secretUidp.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : std::move(secretUidp))
{
    // Ensure plugin store directory exists
    QDir().mkpath(storageDirPath());
}

QString PluginSandbox::storageDirPath() const {
    return QDir::cleanPath(AppPaths::appDataDir() + QStringLiteral("/plugin_store/") + m_secretUidp);
}

void PluginSandbox::validateKey(const QString &key, int line, int col) const {
    if (key.isEmpty()) {
        throw ScriptTrap("StorageError: storage key cannot be empty", line, col);
    }
    if (key.contains(QLatin1String("..")) || key.contains(QLatin1Char('/')) ||
        key.contains(QLatin1Char('\\')) || key.contains(QLatin1Char(':')) ||
        key.contains(QLatin1Char('\0'))) {
        throw ScriptTrap("SecurityError: path traversal attempt in storage key: '" + key.toStdString() + "'", line, col);
    }

    for (const QChar &ch : key) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('_') && ch != QLatin1Char('-')) {
            throw ScriptTrap("SecurityError: invalid character in storage key: '" + key.toStdString() + "'", line, col);
        }
    }
}

QString PluginSandbox::resolveKeyPath(const QString &key, int line, int col) const {
    validateKey(key, line, col);
    QString baseDir = storageDirPath();
    QString resolved = QDir::cleanPath(baseDir + QLatin1Char('/') + key + QStringLiteral(".dat"));

    if (!resolved.startsWith(baseDir)) {
        throw ScriptTrap("SecurityError: resolved key path traverses outside sandbox: '" + key.toStdString() + "'", line, col);
    }
    return resolved;
}

qint64 PluginSandbox::calculateUsedStorageBytes() const {
    QDir dir(storageDirPath());
    qint64 total = 0;
    const auto entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &fi : entries) {
        total += fi.size();
    }
    return total;
}

QString PluginSandbox::storageRead(const QString &key, int line, int col) {
    if (!m_permissions.allowStorage) {
        PluginLogger::log(m_publicId, QStringLiteral("STORAGE_READ"), QStringLiteral("BLOCKED: storage permission disabled"), false);
        throw ScriptTrap("PermissionDenied: Local storage permission is disabled for this plugin", line, col);
    }

    QString path = resolveKeyPath(key, line, col);
    QFile file(path);
    if (!file.exists()) {
        return QString();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        throw ScriptTrap("StorageError: unable to open key for reading: '" + key.toStdString() + "'", line, col);
    }
    QByteArray data = file.readAll();
    PluginLogger::log(m_publicId, QStringLiteral("STORAGE_READ"), QStringLiteral("key='%1', %2 bytes").arg(key).arg(data.size()), true);
    return QString::fromUtf8(data);
}

bool PluginSandbox::storageWrite(const QString &key, const QString &value, int line, int col) {
    if (!m_permissions.allowStorage) {
        PluginLogger::log(m_publicId, QStringLiteral("STORAGE_WRITE"), QStringLiteral("BLOCKED: storage permission disabled"), false);
        throw ScriptTrap("PermissionDenied: Local storage permission is disabled for this plugin", line, col);
    }

    QString path = resolveKeyPath(key, line, col);
    QByteArray data = value.toUtf8();
    qint64 newSize = data.size();

    qint64 currentUsed = calculateUsedStorageBytes();
    qint64 existingFileSize = 0;
    QFileInfo fi(path);
    if (fi.exists()) {
        existingFileSize = fi.size();
    }

    if ((currentUsed - existingFileSize + newSize) > MAX_DISK_STORAGE_BYTES) {
        throw ScriptTrap("StorageError: 5 MB storage quota exceeded", line, col);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw ScriptTrap("StorageError: unable to open key for writing: '" + key.toStdString() + "'", line, col);
    }
    bool ok = file.write(data) == newSize;
    if (ok) {
        PluginLogger::log(m_publicId, QStringLiteral("STORAGE_WRITE"), QStringLiteral("key='%1', %2 bytes").arg(key).arg(newSize), true);
    }
    return ok;
}

bool PluginSandbox::storageDelete(const QString &key, int line, int col) {
    if (!m_permissions.allowStorage) {
        PluginLogger::log(m_publicId, QStringLiteral("STORAGE_DELETE"), QStringLiteral("BLOCKED: storage permission disabled"), false);
        throw ScriptTrap("PermissionDenied: Local storage permission is disabled for this plugin", line, col);
    }

    QString path = resolveKeyPath(key, line, col);
    QFile file(path);
    if (!file.exists()) {
        return false;
    }
    bool ok = file.remove();
    if (ok) {
        PluginLogger::log(m_publicId, QStringLiteral("STORAGE_DELETE"), QStringLiteral("key='%1'").arg(key), true);
    }
    return ok;
}

QStringList PluginSandbox::storageListKeys(int line, int col) {
    if (!m_permissions.allowStorage) {
        PluginLogger::log(m_publicId, QStringLiteral("STORAGE_LIST"), QStringLiteral("BLOCKED: storage permission disabled"), false);
        throw ScriptTrap("PermissionDenied: Local storage permission is disabled for this plugin", line, col);
    }

    Q_UNUSED(line);
    Q_UNUSED(col);
    QDir dir(storageDirPath());
    QStringList keys;
    const auto entries = dir.entryInfoList(QStringList() << QStringLiteral("*.dat"), QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &fi : entries) {
        QString base = fi.fileName();
        if (base.endsWith(QStringLiteral(".dat"))) {
            base.chop(4);
            keys.append(base);
        }
    }
    return keys;
}

ScriptValue PluginSandbox::createEditorNamespace(ResourceGuard &guard) {
    auto dict = std::make_shared<ScriptDict>();

    // editor.get_chapter_text() -> String
    (*dict)["get_chapter_text"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_chapter_text"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        QString text = m_editorCallbacks.getChapterText ? m_editorCallbacks.getChapterText() : QString();
        std::string s = text.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "editor.get_chapter_text");

    // editor.get_selection() -> String
    (*dict)["get_selection"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_selection"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        QString sel = m_editorCallbacks.getSelection ? m_editorCallbacks.getSelection() : QString();
        std::string s = sel.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "editor.get_selection");

    // editor.get_chapter_title() -> String
    (*dict)["get_chapter_title"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_chapter_title"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        QString title = m_editorCallbacks.getChapterTitle ? m_editorCallbacks.getChapterTitle() : QString();
        std::string s = title.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "editor.get_chapter_title");

    // editor.get_chapter_count() -> Number
    (*dict)["get_chapter_count"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_chapter_count"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        int count = m_editorCallbacks.getChapterCount ? m_editorCallbacks.getChapterCount() : 0;
        return ScriptValue(static_cast<int64_t>(count));
    }, "editor.get_chapter_count");

    // editor.get_all_chapters() -> List<Dict>
    (*dict)["get_all_chapters"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_all_chapters"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        auto list = std::make_shared<ScriptList>();
        g.alloc(sizeof(ScriptList), l, c);
        if (m_editorCallbacks.getAllChapters) {
            const auto chapters = m_editorCallbacks.getAllChapters();
            for (const auto &chapMap : chapters) {
                auto chapDict = std::make_shared<ScriptDict>();
                g.alloc(sizeof(ScriptDict), l, c);
                (*chapDict)["index"] = ScriptValue(static_cast<int64_t>(chapMap.value(QStringLiteral("index")).toInt()));
                std::string t = chapMap.value(QStringLiteral("title")).toString().toStdString();
                std::string txt = chapMap.value(QStringLiteral("text")).toString().toStdString();
                g.alloc(t.capacity() + txt.capacity() + 64, l, c);
                (*chapDict)["title"] = ScriptValue(std::move(t));
                (*chapDict)["text"] = ScriptValue(std::move(txt));
                list->push_back(ScriptValue(chapDict));
            }
        }
        return ScriptValue(list);
    }, "editor.get_all_chapters");

    // editor.get_chapter(index: Number) -> Dict
    (*dict)["get_chapter"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_chapter"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        if (args.empty() || !args.at(0).isNumber()) {
            throw ScriptTrap("editor.get_chapter() requires a chapter index argument", l, c);
        }
        int idx = static_cast<int>(args.at(0).asNumber());
        auto chapDict = std::make_shared<ScriptDict>();
        g.alloc(sizeof(ScriptDict), l, c);
        if (m_editorCallbacks.getChapter) {
            QVariantMap chapMap = m_editorCallbacks.getChapter(idx);
            (*chapDict)["index"] = ScriptValue(static_cast<int64_t>(idx));
            std::string t = chapMap.value(QStringLiteral("title")).toString().toStdString();
            std::string txt = chapMap.value(QStringLiteral("text")).toString().toStdString();
            g.alloc(t.capacity() + txt.capacity() + 64, l, c);
            (*chapDict)["title"] = ScriptValue(std::move(t));
            (*chapDict)["text"] = ScriptValue(std::move(txt));
        }
        return ScriptValue(chapDict);
    }, "editor.get_chapter");

    // editor.get_word_count() -> Number
    (*dict)["get_word_count"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorRead) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_READ"), QStringLiteral("BLOCKED: get_word_count"), false);
            throw ScriptTrap("PermissionDenied: Editor read permission is disabled for this plugin", l, c);
        }
        int count = m_editorCallbacks.getWordCount ? m_editorCallbacks.getWordCount() : 0;
        return ScriptValue(static_cast<int64_t>(count));
    }, "editor.get_word_count");

    // editor.replace_selection(new_text: String) -> None
    (*dict)["replace_selection"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorWrite) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_WRITE"), QStringLiteral("BLOCKED: replace_selection"), false);
            throw ScriptTrap("PermissionDenied: Editor write permission is disabled for this plugin", l, c);
        }
        if (args.empty()) throw ScriptTrap("replace_selection() requires 1 argument", l, c);
        QString newText = QString::fromStdString(args.at(0).toString());
        PluginLogger::log(m_publicId, QStringLiteral("EDITOR_WRITE"), QStringLiteral("replace_selection: length=%1").arg(newText.length()), true);
        if (m_editorCallbacks.replaceSelection) {
            m_editorCallbacks.replaceSelection(newText);
        }
        return ScriptValue();
    }, "editor.replace_selection");

    // editor.insert_text(text: String) -> None
    (*dict)["insert_text"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorWrite) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_WRITE"), QStringLiteral("BLOCKED: insert_text"), false);
            throw ScriptTrap("PermissionDenied: Editor write permission is disabled for this plugin", l, c);
        }
        if (args.empty()) throw ScriptTrap("insert_text() requires 1 argument", l, c);
        QString text = QString::fromStdString(args.at(0).toString());
        PluginLogger::log(m_publicId, QStringLiteral("EDITOR_WRITE"), QStringLiteral("insert_text: length=%1").arg(text.length()), true);
        if (m_editorCallbacks.insertText) {
            m_editorCallbacks.insertText(text);
        } else if (m_editorCallbacks.replaceSelection) {
            m_editorCallbacks.replaceSelection(text);
        }
        return ScriptValue();
    }, "editor.insert_text");

    // editor.set_chapter_text(text: String) -> None
    (*dict)["set_chapter_text"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (!m_permissions.allowEditorWrite) {
            PluginLogger::log(m_publicId, QStringLiteral("EDITOR_WRITE"), QStringLiteral("BLOCKED: set_chapter_text"), false);
            throw ScriptTrap("PermissionDenied: Editor write permission is disabled for this plugin", l, c);
        }
        if (args.empty()) throw ScriptTrap("set_chapter_text() requires 1 argument", l, c);
        QString text = QString::fromStdString(args.at(0).toString());
        PluginLogger::log(m_publicId, QStringLiteral("EDITOR_WRITE"), QStringLiteral("set_chapter_text: length=%1").arg(text.length()), true);
        if (m_editorCallbacks.setChapterText) {
            m_editorCallbacks.setChapterText(text);
        }
        return ScriptValue();
    }, "editor.set_chapter_text");

    guard.alloc(sizeof(ScriptDict) + 10 * 64, -1, -1);
    return ScriptValue(dict);
}

ScriptValue PluginSandbox::createStorageNamespace(ResourceGuard &guard) {
    auto dict = std::make_shared<ScriptDict>();

    // storage.read(key: String) -> String | None
    (*dict)["read"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("storage.read() requires key argument", l, c);
        QString key = QString::fromStdString(args.at(0).toString());
        QString val = storageRead(key, l, c);
        if (val.isNull()) {
            return ScriptValue();
        }
        std::string s = val.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "storage.read");

    // storage.write(key: String, value: String) -> Bool
    (*dict)["write"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.size() < 2) throw ScriptTrap("storage.write() requires key and value arguments", l, c);
        QString key = QString::fromStdString(args.at(0).toString());
        QString value = QString::fromStdString(args.at(1).toString());
        bool ok = storageWrite(key, value, l, c);
        return ScriptValue(ok);
    }, "storage.write");

    // storage.delete(key: String) -> Bool
    (*dict)["delete"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("storage.delete() requires key argument", l, c);
        QString key = QString::fromStdString(args.at(0).toString());
        bool ok = storageDelete(key, l, c);
        return ScriptValue(ok);
    }, "storage.delete");

    // storage.list_keys() -> List<String>
    (*dict)["list_keys"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        QStringList keys = storageListKeys(l, c);
        auto list = std::make_shared<ScriptList>();
        g.alloc(sizeof(ScriptList), l, c);
        for (const QString &k : keys) {
            std::string s = k.toStdString();
            g.alloc(s.capacity() + sizeof(ScriptValue), l, c);
            list->push_back(ScriptValue(std::move(s)));
        }
        return ScriptValue(list);
    }, "storage.list_keys");

    guard.alloc(sizeof(ScriptDict) + 4 * 64, -1, -1);
    return ScriptValue(dict);
}

ScriptValue PluginSandbox::createStatusNamespace(ResourceGuard &guard) {
    auto dict = std::make_shared<ScriptDict>();

    // status.warn(message: String, line: Number) -> None
    (*dict)["warn"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("status.warn() requires message argument", l, c);
        QString msg = QString::fromStdString(args.at(0).toString());
        int lineNum = -1;
        if (args.size() > 1 && args.at(1).isNumber()) {
            lineNum = static_cast<int>(args.at(1).asNumber());
        }
        PluginLogger::log(m_publicId, QStringLiteral("STATUS_WARN"), QStringLiteral("%1 (line %2)").arg(msg).arg(lineNum), true);
        if (m_statusCallbacks.onWarn) {
            m_statusCallbacks.onWarn(m_publicId, msg, lineNum);
        }
        return ScriptValue();
    }, "status.warn");

    // status.error(message: String, line: Number) -> None
    (*dict)["error"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("status.error() requires message argument", l, c);
        QString msg = QString::fromStdString(args.at(0).toString());
        int lineNum = -1;
        if (args.size() > 1 && args.at(1).isNumber()) {
            lineNum = static_cast<int>(args.at(1).asNumber());
        }
        PluginLogger::log(m_publicId, QStringLiteral("STATUS_ERROR"), QStringLiteral("%1 (line %2)").arg(msg).arg(lineNum), true);
        if (m_statusCallbacks.onError) {
            m_statusCallbacks.onError(m_publicId, msg, lineNum);
        }
        return ScriptValue();
    }, "status.error");

    // status.notify(message: String) -> None
    (*dict)["notify"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("status.notify() requires message argument", l, c);
        QString msg = QString::fromStdString(args.at(0).toString());
        PluginLogger::log(m_publicId, QStringLiteral("STATUS_NOTIFY"), msg, true);
        if (m_statusCallbacks.onNotify) {
            m_statusCallbacks.onNotify(m_publicId, msg);
        }
        return ScriptValue();
    }, "status.notify");

    guard.alloc(sizeof(ScriptDict) + 3 * 64, -1, -1);
    return ScriptValue(dict);
}

ScriptValue PluginSandbox::createPluginsNamespace(ResourceGuard &guard) {
    auto dict = std::make_shared<ScriptDict>();

    // plugins.list() -> List<Dict>
    (*dict)["list"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        auto list = std::make_shared<ScriptList>();
        g.alloc(sizeof(ScriptList), l, c);

        if (m_pluginCallbacks.listPlugins) {
            const auto pluginList = m_pluginCallbacks.listPlugins();
            for (const auto &item : pluginList) {
                auto pdict = std::make_shared<ScriptDict>();
                g.alloc(sizeof(ScriptDict), l, c);
                for (auto it = item.begin(); it != item.end(); ++it) {
                    std::string k = it.key().toStdString();
                    std::string v = it.value().toString().toStdString();
                    g.alloc(k.capacity() + v.capacity() + sizeof(ScriptValue) + 32, l, c);
                    (*pdict)[k] = ScriptValue(std::move(v));
                }
                list->push_back(ScriptValue(pdict));
            }
        }
        return ScriptValue(list);
    }, "plugins.list");

    // plugins.is_active(target_id: String) -> Bool
    (*dict)["is_active"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("plugins.is_active() requires target_id argument", l, c);
        QString tid = QString::fromStdString(args.at(0).toString());
        bool active = m_pluginCallbacks.isActive ? m_pluginCallbacks.isActive(tid) : false;
        return ScriptValue(active);
    }, "plugins.is_active");

    // plugins.send_message(target_id: String, payload: String) -> Bool
    (*dict)["send_message"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.size() < 2) throw ScriptTrap("plugins.send_message() requires target_id and payload arguments", l, c);
        QString tid = QString::fromStdString(args.at(0).toString());
        QString payload = QString::fromStdString(args.at(1).toString());
        PluginLogger::log(m_publicId, QStringLiteral("PLUGIN_MSG"), QStringLiteral("to='%1', length=%2").arg(tid).arg(payload.length()), true);
        bool ok = m_pluginCallbacks.sendMessage ? m_pluginCallbacks.sendMessage(m_publicId, tid, payload) : false;
        return ScriptValue(ok);
    }, "plugins.send_message");

    guard.alloc(sizeof(ScriptDict) + 3 * 64, -1, -1);
    return ScriptValue(dict);
}

// Mediated HTTP namespace with per-domain policy and audit logging
ScriptValue PluginSandbox::createHttpNamespace(ResourceGuard &guard) {
    auto dict = std::make_shared<ScriptDict>();

    auto performRequest = [this](const QString &method,
                                 const QString &urlString,
                                 const QString &postData,
                                 const std::shared_ptr<ScriptDict> &headersDict,
                                 ResourceGuard &g,
                                 int l,
                                 int c) -> ScriptValue
    {
        QUrl url(urlString);
        if (!url.isValid() || url.scheme().isEmpty()) {
            throw ScriptTrap("NetworkError: Invalid URL '" + urlString.toStdString() + "'", l, c);
        }

        // Strict Unicode homograph & lookalike spoofing check
        QString deceptiveDetails;
        QString charIds;
        if (detectHomographOrDeceptiveChars(urlString, &deceptiveDetails, &charIds)) {
            QString secMsg = QStringLiteral(
                "Security Violation: Deceptive homograph / lookalike Unicode character detected in URL target ('%1'). "
                "Ensemble strictly forbids disguised URLs or homograph diversion to protect users from malicious deception. "
                "Deceptive Character IDs: %2. Details: %3. "
                "The plugin was forcibly closed and quarantined for a security violation. "
                "Report this immediately to Ensemble developers regarding this incident.")
                .arg(urlString, charIds, deceptiveDetails);
            m_quarantined = true;
            if (m_networkCallbacks.onActivity) {
                m_networkCallbacks.onActivity(m_publicId, 1, 0, 0, urlString, QStringLiteral("Quarantined: Homograph lookalike"));
            }
            PluginLogger::log(m_publicId, QStringLiteral("SECURITY_VIOLATION"), secMsg, false);
            if (m_statusCallbacks.onQuarantine) {
                m_statusCallbacks.onQuarantine(m_publicId, secMsg, l);
            }
            throw ScriptTrap(secMsg.toStdString(), l, c);
        }

        // Strict HTTPS enforcement - no insecure HTTP or protocol downgrade allowed
        if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
            QString secMsg = QStringLiteral(
                "Security Violation: Non-HTTPS communication attempted via '%1' ('%2'). "
                "All communications must strictly use HTTPS. Attempts to degrade communication protections are prohibited. "
                "The plugin was forcibly closed and quarantined for a security violation. "
                "Report this immediately to Ensemble developers regarding this incident.")
                .arg(url.scheme(), urlString);
            m_quarantined = true;
            if (m_networkCallbacks.onActivity) {
                m_networkCallbacks.onActivity(m_publicId, 1, 0, 0, urlString, QStringLiteral("Quarantined: Non-HTTPS"));
            }
            PluginLogger::log(m_publicId, QStringLiteral("SECURITY_VIOLATION"), secMsg, false);
            if (m_statusCallbacks.onQuarantine) {
                m_statusCallbacks.onQuarantine(m_publicId, secMsg, l);
            }
            throw ScriptTrap(secMsg.toStdString(), l, c);
        }

        QString host = url.host().toLower();
        if (host.isEmpty()) {
            throw ScriptTrap("NetworkError: URL host is empty in '" + urlString.toStdString() + "'", l, c);
        }

        // 1. Permission checks
        if (!m_permissions.allowNetwork) {
            if (m_networkCallbacks.onActivity) {
                m_networkCallbacks.onActivity(m_publicId, 1, 0, 0, urlString, QStringLiteral("Blocked: Network Disabled"));
            }
            PluginLogger::log(m_publicId, QStringLiteral("HTTP_REQUEST"),
                              QStringLiteral("BLOCKED: Network access disabled (URL: %1)").arg(urlString), false);
            throw ScriptTrap("PermissionDenied: Network access is disabled for this plugin", l, c);
        }

        if (!m_permissions.isDomainAllowed(host)) {
            if (m_networkCallbacks.onActivity) {
                m_networkCallbacks.onActivity(m_publicId, 1, 0, 0, urlString, QStringLiteral("Blocked: Domain Not Allowed"));
            }
            PluginLogger::log(m_publicId, QStringLiteral("HTTP_REQUEST"),
                              QStringLiteral("BLOCKED: Domain '%1' not in allowed list (URL: %2)").arg(host, urlString), false);
            throw ScriptTrap("PermissionDenied: Access to domain '" + host.toStdString() + "' is not permitted", l, c);
        }

        // 2. Mediated HTTP via Ensemble (QNetworkAccessManager)
        PluginLogger::log(m_publicId, QStringLiteral("HTTP_REQUEST"),
                          QStringLiteral("ALLOWED: %1 %2").arg(method, urlString), true);

        QNetworkAccessManager manager;
        QNetworkRequest req(url);
        // Do not automatically follow redirects; rediversion is forbidden and triggers quarantine
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EnsemblePluginEngine/1.0 (Plugin: %1)").arg(m_publicId));

        if (headersDict) {
            for (const auto &pair : *headersDict) {
                req.setRawHeader(QByteArray::fromStdString(pair.first),
                                 QByteArray::fromStdString(pair.second.toString()));
            }
        }

        QNetworkReply *reply = nullptr;
        if (method == QStringLiteral("POST")) {
            if (!req.hasRawHeader("Content-Type")) {
                req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
            }
            reply = manager.post(req, postData.toUtf8());
        } else {
            reply = manager.get(req);
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            if (reply && reply->isRunning()) {
                reply->abort();
            }
            loop.quit();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(10000); // 10s timeout
        loop.exec();

        if (timer.isActive()) {
            timer.stop();
        }

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // Check for rediversion / redirection attempt
        QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        QString redirectUrl;
        if (!redirectTarget.isNull() && !redirectTarget.toUrl().isEmpty()) {
            redirectUrl = url.resolved(redirectTarget.toUrl()).toString();
        } else if (reply->hasRawHeader("Location")) {
            redirectUrl = url.resolved(QUrl(QString::fromUtf8(reply->rawHeader("Location")))).toString();
        }

        if (!redirectUrl.isEmpty() || (statusCode >= 300 && statusCode < 400 && statusCode != 304)) {
            if (redirectUrl.isEmpty()) {
                redirectUrl = QStringLiteral("<unknown diversion endpoint>");
            }
            QString secWarning = QStringLiteral(
                "Security Warning: The plugin or plugin provider or the link that the plugin used attempted to redivert to a different website (%1). "
                "The plugin was forcibly closed and quarantined for a security violation. "
                "Report this immediately to Ensemble developers regarding this incident.")
                .arg(redirectUrl);
            m_quarantined = true;
            if (m_networkCallbacks.onActivity) {
                m_networkCallbacks.onActivity(m_publicId, 1, postData.toUtf8().size(), 0, redirectUrl, QStringLiteral("Quarantined: Redirection Attempt"));
            }
            PluginLogger::log(m_publicId, QStringLiteral("QUARANTINE_VIOLATION"), secWarning, false);
            if (m_statusCallbacks.onQuarantine) {
                m_statusCallbacks.onQuarantine(m_publicId, secWarning, l);
            }
            reply->deleteLater();
            throw ScriptTrap(secWarning.toStdString(), l, c);
        }

        QByteArray body = reply->readAll();
        reply->deleteLater();

        g.alloc(body.size() + sizeof(ScriptDict) + 128, l, c);

        if (m_networkCallbacks.onActivity) {
            m_networkCallbacks.onActivity(m_publicId, 1, postData.toUtf8().size(), body.size(), urlString, QString::number(statusCode));
        }

        PluginLogger::log(m_publicId, QStringLiteral("HTTP_RESPONSE"),
                          QStringLiteral("Status: %1, size: %2 bytes").arg(statusCode).arg(body.size()), true);

        auto respDict = std::make_shared<ScriptDict>();
        (*respDict)["status"] = ScriptValue(static_cast<int64_t>(statusCode));
        (*respDict)["body"] = ScriptValue(body.toStdString());
        (*respDict)["ok"] = ScriptValue(statusCode >= 200 && statusCode < 300);
        return ScriptValue(respDict);
    };

    // http.get(url: String, headers: Dict = {}) -> Dict
    (*dict)["get"] = ScriptValue([performRequest](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("http.get() requires url argument", l, c);
        QString url = QString::fromStdString(args.at(0).toString());
        std::shared_ptr<ScriptDict> headers;
        if (args.size() > 1 && args.at(1).isDict()) {
            headers = args.at(1).asDict();
        }
        return performRequest(QStringLiteral("GET"), url, QString(), headers, g, l, c);
    }, "http.get");

    // http.post(url: String, data: String = "", headers: Dict = {}) -> Dict
    (*dict)["post"] = ScriptValue([performRequest](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("http.post() requires url argument", l, c);
        QString url = QString::fromStdString(args.at(0).toString());
        QString postData;
        if (args.size() > 1) {
            postData = QString::fromStdString(args.at(1).toString());
        }
        std::shared_ptr<ScriptDict> headers;
        if (args.size() > 2 && args.at(2).isDict()) {
            headers = args.at(2).asDict();
        }
        return performRequest(QStringLiteral("POST"), url, postData, headers, g, l, c);
    }, "http.post");

    guard.alloc(sizeof(ScriptDict) + 2 * 64, -1, -1);
    return ScriptValue(dict);
}

// Declarative UI compositing namespace with thread isolation & audit logging
ScriptValue PluginSandbox::createUiNamespace(ResourceGuard &guard) {
    auto dict = std::make_shared<ScriptDict>();

    // Helper to extract winId from arguments or fall back to last created window
    auto resolveWinId = [this](const std::vector<ScriptValue> &args) -> QString {
        if (!args.empty()) {
            if (args.at(0).isDict()) {
                auto d = args.at(0).asDict();
                if (d->find("_win_id") != d->end()) {
                    return QString::fromStdString((*d)["_win_id"].toString());
                }
            } else if (args.at(0).isString()) {
                return QString::fromStdString(args.at(0).asString());
            }
        }
        return m_lastCreatedWindowId;
    };

    // Helper to generate a window ScriptDict
    auto makeWindowDict = [this](const QString &winId, const QString &title, ResourceGuard &g, int l, int c) -> ScriptValue {
        auto winDict = std::make_shared<ScriptDict>();
        g.alloc(sizeof(ScriptDict) + 12 * 64, l, c);

        (*winDict)["_win_id"] = ScriptValue(winId.toStdString());
        (*winDict)["id"] = ScriptValue(winId.toStdString());
        (*winDict)["win_id"] = ScriptValue(winId.toStdString());
        (*winDict)["title"] = ScriptValue(title.toStdString());

        // win.add_label(text: String)
        (*winDict)["add_label"] = ScriptValue([this, winId](const std::vector<ScriptValue> &args, ResourceGuard &, int line, int col) -> ScriptValue {
            if (args.empty()) throw ScriptTrap("win.add_label() requires text argument", line, col);
            QString text = (args.size() > 1) ? QString::fromStdString(args.at(1).toString()) : QString::fromStdString(args.at(0).toString());
            QVariantMap props;
            props[QStringLiteral("text")] = text;
            if (m_uiCallbacks.addWidget) {
                m_uiCallbacks.addWidget(winId, QStringLiteral("label"), props);
            }
            return ScriptValue();
        }, "win.add_label");

        // win.add_text(content: String, read_only: Bool = True)
        (*winDict)["add_text"] = ScriptValue([this, winId](const std::vector<ScriptValue> &args, ResourceGuard &, int line, int col) -> ScriptValue {
            QString content = args.empty() ? QString() : QString::fromStdString(args.at(0).toString());
            bool readOnly = true;
            if (args.size() > 1) {
                readOnly = args.at(1).toBool();
            }
            QVariantMap props;
            props[QStringLiteral("content")] = content;
            props[QStringLiteral("readOnly")] = readOnly;
            if (m_uiCallbacks.addWidget) {
                m_uiCallbacks.addWidget(winId, QStringLiteral("text"), props);
            }
            return ScriptValue();
        }, "win.add_text");

        // win.add_input(placeholder: String = "", default_text: String = "")
        (*winDict)["add_input"] = ScriptValue([this, winId](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
            QString placeholder = args.empty() ? QString() : QString::fromStdString(args.at(0).toString());
            QString defText = (args.size() > 1 && !args.at(1).isNone()) ? QString::fromStdString(args.at(1).toString()) : QString();
            QVariantMap props;
            props[QStringLiteral("placeholder")] = placeholder;
            props[QStringLiteral("defaultText")] = defText;
            if (m_uiCallbacks.addWidget) {
                m_uiCallbacks.addWidget(winId, QStringLiteral("input"), props);
            }
            return ScriptValue();
        }, "win.add_input");

        // win.add_button(label: String, action_name: String = label)
        (*winDict)["add_button"] = ScriptValue([this, winId](const std::vector<ScriptValue> &args, ResourceGuard &, int line, int col) -> ScriptValue {
            if (args.empty()) throw ScriptTrap("win.add_button() requires label argument", line, col);
            QString label = QString::fromStdString(args.at(0).toString());
            QString actionName = (args.size() > 1) ? QString::fromStdString(args.at(1).toString()) : label;
            if (args.size() >= 3) {
                label = QString::fromStdString(args.at(1).toString());
                actionName = QString::fromStdString(args.at(2).toString());
            }
            QVariantMap props;
            props[QStringLiteral("label")] = label;
            props[QStringLiteral("action")] = actionName;
            if (m_uiCallbacks.addWidget) {
                m_uiCallbacks.addWidget(winId, QStringLiteral("button"), props);
            }
            return ScriptValue();
        }, "win.add_button");

        // win.show()
        auto showFn = ScriptValue([this, winId](const std::vector<ScriptValue> &, ResourceGuard &, int, int) -> ScriptValue {
            if (m_uiCallbacks.showWindow) {
                m_uiCallbacks.showWindow(winId);
            }
            return ScriptValue();
        }, "win.show");
        (*winDict)["show"] = showFn;
        (*winDict)["show_window"] = showFn;

        // win.close()
        auto closeFn = ScriptValue([this, winId](const std::vector<ScriptValue> &, ResourceGuard &, int, int) -> ScriptValue {
            if (m_uiCallbacks.closeWindow) {
                m_uiCallbacks.closeWindow(winId);
            }
            return ScriptValue();
        }, "win.close");
        (*winDict)["close"] = closeFn;
        (*winDict)["close_window"] = closeFn;
        (*winDict)["dismiss"] = closeFn;

        // win.clear()
        auto clearFn = ScriptValue([this, winId](const std::vector<ScriptValue> &, ResourceGuard &, int, int) -> ScriptValue {
            if (m_uiCallbacks.clearWindow) {
                m_uiCallbacks.clearWindow(winId);
            }
            return ScriptValue();
        }, "win.clear");
        (*winDict)["clear"] = clearFn;
        (*winDict)["clear_window"] = clearFn;

        return ScriptValue(winDict);
    };

    // ui.create_window(title: String, width: Number = 450, height: Number = 320) -> Dict
    auto createWindowFn = ScriptValue([this, makeWindowDict](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
        if (!m_permissions.allowUi) {
            PluginLogger::log(m_publicId, QStringLiteral("UI_WINDOW"),
                              QStringLiteral("BLOCKED: UI permission disabled"), false);
            throw ScriptTrap("PermissionDenied: UI window creation is disabled for this plugin", l, c);
        }

        QString title = args.empty() ? QStringLiteral("Plugin Window") : QString::fromStdString(args.at(0).toString());
        int width = 450;
        int height = 320;
        if (args.size() > 1 && args.at(1).isNumber()) {
            width = static_cast<int>(args.at(1).asNumber());
        }
        if (args.size() > 2 && args.at(2).isNumber()) {
            height = static_cast<int>(args.at(2).asNumber());
        }

        QString winId = QStringLiteral("win_%1_%2")
                            .arg(m_publicId, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        m_lastCreatedWindowId = winId;

        PluginLogger::log(m_publicId, QStringLiteral("UI_WINDOW"),
                          QStringLiteral("Created window '%1' (%2x%3)").arg(title).arg(width).arg(height), true);

        if (m_uiCallbacks.createWindow) {
            m_uiCallbacks.createWindow(winId, title, width, height);
        }

        return makeWindowDict(winId, title, g, l, c);
    }, "ui.create_window");

    (*dict)["create_window"] = createWindowFn;
    (*dict)["create_dialog"] = createWindowFn;

    // ui.close_window([win_or_id])
    auto uiCloseWindowFn = ScriptValue([this, resolveWinId](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
        QString winId = resolveWinId(args);
        if (!winId.isEmpty() && m_uiCallbacks.closeWindow) {
            m_uiCallbacks.closeWindow(winId);
        }
        return ScriptValue();
    }, "ui.close_window");
    (*dict)["close_window"] = uiCloseWindowFn;
    (*dict)["close"] = uiCloseWindowFn;

    // ui.show_window([win_or_id])
    auto uiShowWindowFn = ScriptValue([this, resolveWinId](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
        QString winId = resolveWinId(args);
        if (!winId.isEmpty() && m_uiCallbacks.showWindow) {
            m_uiCallbacks.showWindow(winId);
        }
        return ScriptValue();
    }, "ui.show_window");
    (*dict)["show_window"] = uiShowWindowFn;
    (*dict)["show"] = uiShowWindowFn;

    // ui.clear_window([win_or_id])
    auto uiClearWindowFn = ScriptValue([this, resolveWinId](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
        QString winId = resolveWinId(args);
        if (!winId.isEmpty() && m_uiCallbacks.clearWindow) {
            m_uiCallbacks.clearWindow(winId);
        }
        return ScriptValue();
    }, "ui.clear_window");
    (*dict)["clear_window"] = uiClearWindowFn;
    (*dict)["clear"] = uiClearWindowFn;

    // ui.message_box(title, message)
    (*dict)["message_box"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (!m_permissions.allowUi) {
            throw ScriptTrap("PermissionDenied: UI access is disabled for this plugin", l, c);
        }
        if (args.empty()) {
            throw ScriptTrap("ui.message_box(title, [message]) requires at least 1 argument", l, c);
        }
        QString title = QString::fromStdString(args.at(0).toString());
        QString message = (args.size() > 1) ? QString::fromStdString(args.at(1).toString()) : title;
        if (m_uiCallbacks.showMessageBox) {
            m_uiCallbacks.showMessageBox(title, message);
        }
        return ScriptValue();
    }, "ui.message_box");

    // Flat UI helpers on ui directly targeting active window
    (*dict)["add_label"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("ui.add_label() requires text argument", l, c);
        if (m_lastCreatedWindowId.isEmpty()) throw ScriptTrap("RuntimeError: No active window created for ui.add_label()", l, c);
        QString text = (args.size() > 1) ? QString::fromStdString(args.at(1).toString()) : QString::fromStdString(args.at(0).toString());
        QVariantMap props;
        props[QStringLiteral("text")] = text;
        if (m_uiCallbacks.addWidget) {
            m_uiCallbacks.addWidget(m_lastCreatedWindowId, QStringLiteral("label"), props);
        }
        return ScriptValue();
    }, "ui.add_label");

    (*dict)["add_text"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
        if (m_lastCreatedWindowId.isEmpty()) return ScriptValue();
        QString content = args.empty() ? QString() : QString::fromStdString(args.at(0).toString());
        bool readOnly = (args.size() > 1) ? args.at(1).toBool() : true;
        QVariantMap props;
        props[QStringLiteral("content")] = content;
        props[QStringLiteral("readOnly")] = readOnly;
        if (m_uiCallbacks.addWidget) {
            m_uiCallbacks.addWidget(m_lastCreatedWindowId, QStringLiteral("text"), props);
        }
        return ScriptValue();
    }, "ui.add_text");

    (*dict)["add_input"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int, int) -> ScriptValue {
        if (m_lastCreatedWindowId.isEmpty()) return ScriptValue();
        QString ph = args.empty() ? QString() : QString::fromStdString(args.at(0).toString());
        QString def = (args.size() > 1) ? QString::fromStdString(args.at(1).toString()) : QString();
        QVariantMap props;
        props[QStringLiteral("placeholder")] = ph;
        props[QStringLiteral("defaultText")] = def;
        if (m_uiCallbacks.addWidget) {
            m_uiCallbacks.addWidget(m_lastCreatedWindowId, QStringLiteral("input"), props);
        }
        return ScriptValue();
    }, "ui.add_input");

    (*dict)["add_button"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("ui.add_button() requires label argument", l, c);
        if (m_lastCreatedWindowId.isEmpty()) throw ScriptTrap("RuntimeError: No active window created for ui.add_button()", l, c);
        QString label = QString::fromStdString(args.at(0).toString());
        QString actionName = (args.size() > 1) ? QString::fromStdString(args.at(1).toString()) : label;
        if (args.size() >= 3) {
            label = QString::fromStdString(args.at(1).toString());
            actionName = QString::fromStdString(args.at(2).toString());
        }
        QVariantMap props;
        props[QStringLiteral("label")] = label;
        props[QStringLiteral("action")] = actionName;
        if (m_uiCallbacks.addWidget) {
            m_uiCallbacks.addWidget(m_lastCreatedWindowId, QStringLiteral("button"), props);
        }
        return ScriptValue();
    }, "ui.add_button");

    guard.alloc(sizeof(ScriptDict) + 128, -1, -1);
    return ScriptValue(dict);
}

} // namespace EnsemblePlugin
