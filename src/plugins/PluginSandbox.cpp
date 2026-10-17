#include "PluginSandbox.h"
#include "utils/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    QString path = resolveKeyPath(key, line, col);
    QFile file(path);
    if (!file.exists()) {
        return QString();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        throw ScriptTrap("StorageError: unable to open key for reading: '" + key.toStdString() + "'", line, col);
    }
    return QString::fromUtf8(file.readAll());
}

bool PluginSandbox::storageWrite(const QString &key, const QString &value, int line, int col) {
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
    return file.write(data) == newSize;
}

bool PluginSandbox::storageDelete(const QString &key, int line, int col) {
    QString path = resolveKeyPath(key, line, col);
    QFile file(path);
    if (!file.exists()) {
        return false;
    }
    return file.remove();
}

QStringList PluginSandbox::storageListKeys(int line, int col) {
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
        QString text = m_editorCallbacks.getChapterText ? m_editorCallbacks.getChapterText() : QString();
        std::string s = text.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "editor.get_chapter_text");

    // editor.get_selection() -> String
    (*dict)["get_selection"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        QString sel = m_editorCallbacks.getSelection ? m_editorCallbacks.getSelection() : QString();
        std::string s = sel.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "editor.get_selection");

    // editor.replace_selection(new_text: String) -> None
    (*dict)["replace_selection"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("replace_selection() requires 1 argument", l, c);
        QString newText = QString::fromStdString(args.at(0).toString());
        if (m_editorCallbacks.replaceSelection) {
            m_editorCallbacks.replaceSelection(newText);
        }
        return ScriptValue();
    }, "editor.replace_selection");

    // editor.get_chapter_title() -> String
    (*dict)["get_chapter_title"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        QString title = m_editorCallbacks.getChapterTitle ? m_editorCallbacks.getChapterTitle() : QString();
        std::string s = title.toStdString();
        g.alloc(s.capacity() + sizeof(std::string), l, c);
        return ScriptValue(std::move(s));
    }, "editor.get_chapter_title");

    guard.alloc(sizeof(ScriptDict) + 4 * 64, -1, -1);
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
        if (m_statusCallbacks.onError) {
            m_statusCallbacks.onError(m_publicId, msg, lineNum);
        }
        return ScriptValue();
    }, "status.error");

    // status.notify(message: String) -> None
    (*dict)["notify"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("status.notify() requires message argument", l, c);
        QString msg = QString::fromStdString(args.at(0).toString());
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

    // plugins.list() -> List<Dict> (Returns [{"id": public_id, "name": name, "version": version}] ONLY)
    (*dict)["list"] = ScriptValue([this](const std::vector<ScriptValue> &, ResourceGuard &g, int l, int c) -> ScriptValue {
        auto list = std::make_shared<ScriptList>();
        g.alloc(sizeof(ScriptList), l, c);
        if (m_pluginCallbacks.listPlugins) {
            QList<QVariantMap> pluginInfos = m_pluginCallbacks.listPlugins();
            for (const auto &map : pluginInfos) {
                auto pdict = std::make_shared<ScriptDict>();
                // Strictly exclude any secret_uidp!
                (*pdict)["id"] = ScriptValue(map.value(QStringLiteral("id")).toString().toStdString());
                (*pdict)["name"] = ScriptValue(map.value(QStringLiteral("name")).toString().toStdString());
                (*pdict)["version"] = ScriptValue(map.value(QStringLiteral("version")).toString().toStdString());
                g.alloc(sizeof(ScriptDict) + 3 * 32, l, c);
                list->push_back(ScriptValue(pdict));
            }
        }
        return ScriptValue(list);
    }, "plugins.list");

    // plugins.is_active(public_id: String) -> Bool
    (*dict)["is_active"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.empty()) throw ScriptTrap("plugins.is_active() requires target public_id", l, c);
        QString targetId = QString::fromStdString(args.at(0).toString());
        bool active = m_pluginCallbacks.isActive ? m_pluginCallbacks.isActive(targetId) : false;
        return ScriptValue(active);
    }, "plugins.is_active");

    // plugins.send_message(target_public_id: String, payload: String) -> Bool
    (*dict)["send_message"] = ScriptValue([this](const std::vector<ScriptValue> &args, ResourceGuard &, int l, int c) -> ScriptValue {
        if (args.size() < 2) throw ScriptTrap("plugins.send_message() requires target public_id and payload", l, c);
        QString targetId = QString::fromStdString(args.at(0).toString());
        QString payload = QString::fromStdString(args.at(1).toString());
        bool ok = m_pluginCallbacks.sendMessage ? m_pluginCallbacks.sendMessage(m_publicId, targetId, payload) : false;
        return ScriptValue(ok);
    }, "plugins.send_message");

    guard.alloc(sizeof(ScriptDict) + 3 * 64, -1, -1);
    return ScriptValue(dict);
}

} // namespace EnsemblePlugin
