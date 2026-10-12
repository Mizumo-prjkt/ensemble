#include "AgenticData.h"
#include "utils/AesCipher.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

AgenticData::AgenticData()
{
    clear();
}

void AgenticData::clear()
{
    m_sharedFiles.clear();
    m_agentFiles.clear();
    m_historyEntries.clear();
    m_lastChangesByModel.clear();
    m_createdDate = QDateTime::currentDateTimeUtc();
    m_modifiedDate = m_createdDate;
    m_projectIdentifier = QUuid::createUuid().toString(QUuid::WithoutBraces);

    ensureDefaultAgentifiers();
    ensureDefaultHistory();
}

void AgenticData::ensureDefaultAgentifiers()
{
    m_agentifiers = QJsonObject();
    m_agentifiers[QStringLiteral("format")] = QStringLiteral("ensemble-agentifiers-v1");
    m_agentifiers[QStringLiteral("projectIdentifier")] = m_projectIdentifier;
    m_agentifiers[QStringLiteral("created")] = m_createdDate.toString(Qt::ISODate);
    m_agentifiers[QStringLiteral("modified")] = m_modifiedDate.toString(Qt::ISODate);
    m_agentifiers[QStringLiteral("agents")] = QJsonObject();
}

void AgenticData::ensureDefaultHistory()
{
    if (m_historyEntries.isEmpty()) {
        AgentModelHistoryEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.timestamp = m_createdDate;
        entry.agentName = QStringLiteral("system");
        entry.model = QStringLiteral("ensemble-core");
        entry.role = QStringLiteral("orchestrator");
        entry.action = QStringLiteral("init");
        entry.summary = QStringLiteral("Agentic data repository initialized for project.");
        m_historyEntries.append(entry);
        m_lastChangesByModel[entry.agentName.toLower()] = entry;
    }
}

QString AgenticData::sanitizePath(const QString &path)
{
    QString clean = path;
    clean.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Strip leading slashes and drive letters
    while (clean.startsWith(QLatin1Char('/'))) {
        clean.remove(0, 1);
    }

    // Eliminate relative traversal segments
    QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList resolved;
    for (const QString &p : parts) {
        if (p == QStringLiteral(".")) continue;
        if (p == QStringLiteral("..")) {
            if (!resolved.isEmpty()) resolved.removeLast();
        } else {
            resolved.append(p);
        }
    }
    return resolved.join(QLatin1Char('/'));
}

QString AgenticData::sanitizeAgentName(const QString &agent)
{
    QString name = agent.trimmed().toLower();
    name.remove(QRegularExpression(QStringLiteral("[^a-z0-9_\\-]")));
    if (name.isEmpty()) {
        name = QStringLiteral("default_agent");
    }
    return name;
}

qint64 AgenticData::totalSharedBytes() const
{
    qint64 total = 0;
    for (auto it = m_sharedFiles.constBegin(); it != m_sharedFiles.constEnd(); ++it) {
        total += it.value().size();
    }
    return total;
}

qint64 AgenticData::totalLocalizedBytes() const
{
    qint64 total = 0;
    for (auto agentIt = m_agentFiles.constBegin(); agentIt != m_agentFiles.constEnd(); ++agentIt) {
        for (auto fileIt = agentIt.value().constBegin(); fileIt != agentIt.value().constEnd(); ++fileIt) {
            total += fileIt.value().size();
        }
    }
    return total;
}

qint64 AgenticData::agentBytes(const QString &agentName) const
{
    const QString agent = sanitizeAgentName(agentName);
    if (!m_agentFiles.contains(agent)) return 0;
    qint64 total = 0;
    const auto &files = m_agentFiles.value(agent);
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        total += it.value().size();
    }
    return total;
}

bool AgenticData::setSharedFile(const QString &relativePath, const QByteArray &data, QString *error)
{
    const QString cleanPath = sanitizePath(relativePath);
    if (cleanPath.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid or empty relative path for shared data file");
        return false;
    }

    const qint64 oldSize = m_sharedFiles.contains(cleanPath) ? m_sharedFiles.value(cleanPath).size() : 0;
    const qint64 newTotal = totalSharedBytes() - oldSize + data.size();

    if (newTotal > kMaxSharedBytes) {
        if (error) {
            *error = QStringLiteral("Shared agent data quota exceeded: %1 bytes requested (maximum allowed is 5 MB / %2 bytes)")
                         .arg(newTotal)
                         .arg(kMaxSharedBytes);
        }
        return false;
    }

    m_sharedFiles.insert(cleanPath, data);
    m_modifiedDate = QDateTime::currentDateTimeUtc();
    return true;
}

QByteArray AgenticData::sharedFile(const QString &relativePath) const
{
    return m_sharedFiles.value(sanitizePath(relativePath));
}

bool AgenticData::hasSharedFile(const QString &relativePath) const
{
    return m_sharedFiles.contains(sanitizePath(relativePath));
}

bool AgenticData::removeSharedFile(const QString &relativePath)
{
    const QString cleanPath = sanitizePath(relativePath);
    if (m_sharedFiles.remove(cleanPath) > 0) {
        m_modifiedDate = QDateTime::currentDateTimeUtc();
        return true;
    }
    return false;
}

QStringList AgenticData::sharedFileList() const
{
    return m_sharedFiles.keys();
}

bool AgenticData::setAgentFile(const QString &agentName, const QString &relativePath, const QByteArray &data, QString *error)
{
    const QString agent = sanitizeAgentName(agentName);
    const QString cleanPath = sanitizePath(relativePath);

    if (cleanPath.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid relative path for localized agent file");
        return false;
    }

    const qint64 oldSize = (m_agentFiles.contains(agent) && m_agentFiles[agent].contains(cleanPath))
                               ? m_agentFiles[agent].value(cleanPath).size()
                               : 0;

    const qint64 newTotalLocalized = totalLocalizedBytes() - oldSize + data.size();
    if (newTotalLocalized > kMaxLocalizedBytes) {
        if (error) {
            *error = QStringLiteral("Localized agent data quota exceeded: %1 bytes requested (maximum allowed is 20 MB / %2 bytes)")
                         .arg(newTotalLocalized)
                         .arg(kMaxLocalizedBytes);
        }
        return false;
    }

    m_agentFiles[agent].insert(cleanPath, data);
    m_modifiedDate = QDateTime::currentDateTimeUtc();
    return true;
}

QByteArray AgenticData::agentFile(const QString &agentName, const QString &relativePath) const
{
    const QString agent = sanitizeAgentName(agentName);
    if (!m_agentFiles.contains(agent)) return QByteArray();
    return m_agentFiles.value(agent).value(sanitizePath(relativePath));
}

bool AgenticData::hasAgentFile(const QString &agentName, const QString &relativePath) const
{
    const QString agent = sanitizeAgentName(agentName);
    if (!m_agentFiles.contains(agent)) return false;
    return m_agentFiles.value(agent).contains(sanitizePath(relativePath));
}

bool AgenticData::removeAgentFile(const QString &agentName, const QString &relativePath)
{
    const QString agent = sanitizeAgentName(agentName);
    if (!m_agentFiles.contains(agent)) return false;

    const QString cleanPath = sanitizePath(relativePath);
    if (m_agentFiles[agent].remove(cleanPath) > 0) {
        if (m_agentFiles[agent].isEmpty()) {
            m_agentFiles.remove(agent);
        }
        m_modifiedDate = QDateTime::currentDateTimeUtc();
        return true;
    }
    return false;
}

QStringList AgenticData::agentNames() const
{
    return m_agentFiles.keys();
}

QStringList AgenticData::agentFileList(const QString &agentName) const
{
    const QString agent = sanitizeAgentName(agentName);
    if (!m_agentFiles.contains(agent)) return QStringList();
    return m_agentFiles.value(agent).keys();
}

QJsonObject AgentModelHistoryEntry::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("timestamp")] = timestamp.toString(Qt::ISODate);
    obj[QStringLiteral("agentName")] = agentName;
    obj[QStringLiteral("model")] = model;
    obj[QStringLiteral("role")] = role;
    obj[QStringLiteral("action")] = action;
    obj[QStringLiteral("summary")] = summary;
    if (chapterIndex >= 0) {
        obj[QStringLiteral("chapterIndex")] = chapterIndex;
    }
    obj[QStringLiteral("wordCountDelta")] = wordCountDelta;
    if (!details.isEmpty()) {
        obj[QStringLiteral("details")] = details;
    }
    return obj;
}

void AgenticData::appendHistoryEntry(const QString &agentName, const QString &model,
                                     const QString &role, const QString &action,
                                     const QString &summary, int chapterIndex,
                                     int wordCountDelta, const QString &details)
{
    AgentModelHistoryEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.timestamp = QDateTime::currentDateTimeUtc();
    entry.agentName = agentName.isEmpty() ? QStringLiteral("anonymous") : agentName;
    entry.model = model.isEmpty() ? QStringLiteral("unknown-model") : model;
    entry.role = role.isEmpty() ? QStringLiteral("contributor") : role;
    entry.action = action.isEmpty() ? QStringLiteral("update") : action;
    entry.summary = summary;
    entry.chapterIndex = chapterIndex;
    entry.wordCountDelta = wordCountDelta;
    entry.details = details;

    m_historyEntries.append(entry);
    m_lastChangesByModel[entry.agentName.toLower()] = entry;
    m_modifiedDate = entry.timestamp;
}

QList<AgentModelHistoryEntry> AgenticData::historyEntries() const
{
    return m_historyEntries;
}

QMap<QString, AgentModelHistoryEntry> AgenticData::lastChangesByModel() const
{
    return m_lastChangesByModel;
}

QJsonObject AgenticData::lastChangesByModelJson() const
{
    QJsonObject obj;
    for (auto it = m_lastChangesByModel.constBegin(); it != m_lastChangesByModel.constEnd(); ++it) {
        obj[it.key()] = it.value().toJson();
    }
    return obj;
}

QString AgenticData::rawModelHistoryXml() const
{
    QByteArray xmlBytes;
    QXmlStreamWriter writer(&xmlBytes);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("agent-model-history"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writer.writeAttribute(QStringLiteral("projectIdentifier"), m_projectIdentifier);

    for (const AgentModelHistoryEntry &entry : m_historyEntries) {
        writer.writeStartElement(QStringLiteral("entry"));
        writer.writeAttribute(QStringLiteral("id"), entry.id);
        writer.writeAttribute(QStringLiteral("timestamp"), entry.timestamp.toString(Qt::ISODate));
        writer.writeAttribute(QStringLiteral("agent"), entry.agentName);
        writer.writeAttribute(QStringLiteral("model"), entry.model);
        writer.writeAttribute(QStringLiteral("role"), entry.role);
        writer.writeAttribute(QStringLiteral("action"), entry.action);
        if (entry.chapterIndex >= 0) {
            writer.writeAttribute(QStringLiteral("chapterIndex"), QString::number(entry.chapterIndex));
        }
        if (entry.wordCountDelta != 0) {
            writer.writeAttribute(QStringLiteral("wordCountDelta"), QString::number(entry.wordCountDelta));
        }
        writer.writeTextElement(QStringLiteral("summary"), entry.summary);
        if (!entry.details.isEmpty()) {
            writer.writeTextElement(QStringLiteral("details"), entry.details);
        }
        writer.writeEndElement(); // entry
    }

    writer.writeEndElement(); // agent-model-history
    writer.writeEndDocument();

    return QString::fromUtf8(xmlBytes);
}

void AgenticData::setRawModelHistoryXml(const QString &xml)
{
    m_historyEntries.clear();
    m_lastChangesByModel.clear();
    QXmlStreamReader reader(xml);

    while (!reader.atEnd() && !reader.hasError()) {
        QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (reader.name() == QStringLiteral("entry")) {
                AgentModelHistoryEntry entry;
                entry.id = reader.attributes().value(QStringLiteral("id")).toString();
                entry.timestamp = QDateTime::fromString(reader.attributes().value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
                entry.agentName = reader.attributes().value(QStringLiteral("agent")).toString();
                entry.model = reader.attributes().value(QStringLiteral("model")).toString();
                entry.role = reader.attributes().value(QStringLiteral("role")).toString();
                entry.action = reader.attributes().value(QStringLiteral("action")).toString();
                if (reader.attributes().hasAttribute(QStringLiteral("chapterIndex"))) {
                    entry.chapterIndex = reader.attributes().value(QStringLiteral("chapterIndex")).toInt();
                }
                if (reader.attributes().hasAttribute(QStringLiteral("wordCountDelta"))) {
                    entry.wordCountDelta = reader.attributes().value(QStringLiteral("wordCountDelta")).toInt();
                }

                while (!(reader.tokenType() == QXmlStreamReader::EndElement && reader.name() == QStringLiteral("entry")) && !reader.atEnd()) {
                    reader.readNext();
                    if (reader.tokenType() == QXmlStreamReader::StartElement) {
                        if (reader.name() == QStringLiteral("summary")) {
                            entry.summary = reader.readElementText();
                        } else if (reader.name() == QStringLiteral("details")) {
                            entry.details = reader.readElementText();
                        }
                    }
                }
                m_historyEntries.append(entry);
                m_lastChangesByModel[entry.agentName.toLower()] = entry;
            }
        }
    }

    ensureDefaultHistory();
}

QJsonObject AgenticData::agentifiersJson() const
{
    return m_agentifiers;
}

void AgenticData::setAgentifiersJson(const QJsonObject &json)
{
    m_agentifiers = json;
    m_modifiedDate = QDateTime::currentDateTimeUtc();
}

void AgenticData::registerAgentIdentifier(const QString &agentName, const QString &modelFamily,
                                         const QString &role, const QString &notes)
{
    const QString agent = sanitizeAgentName(agentName);
    QJsonObject agents = m_agentifiers.value(QStringLiteral("agents")).toObject();

    QJsonObject agentObj;
    agentObj[QStringLiteral("agentId")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    agentObj[QStringLiteral("registeredAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    agentObj[QStringLiteral("modelFamily")] = modelFamily.isEmpty() ? QStringLiteral("generic") : modelFamily;
    agentObj[QStringLiteral("role")] = role.isEmpty() ? QStringLiteral("co-author") : role;
    if (!notes.isEmpty()) {
        agentObj[QStringLiteral("notes")] = notes;
    }

    agents[agent] = agentObj;
    m_agentifiers[QStringLiteral("agents")] = agents;
    m_agentifiers[QStringLiteral("modified")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QByteArray AgenticData::exportEncryptedAgentifiers(const QByteArray &passphrase) const
{
    const QByteArray clearJson = QJsonDocument(m_agentifiers).toJson(QJsonDocument::Compact);
    return AesCipher::encryptEnvelope(clearJson, passphrase);
}

bool AgenticData::importEncryptedAgentifiers(const QByteArray &encryptedEnvelope, const QByteArray &passphrase, QString *error)
{
    const QByteArray clearJson = AesCipher::decryptEnvelope(encryptedEnvelope, passphrase, error);
    if (clearJson.isEmpty()) {
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(clearJson, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Decrypted agentifiers content is not valid JSON: %1").arg(parseErr.errorString());
        return false;
    }

    m_agentifiers = doc.object();
    return true;
}

QJsonObject AgenticData::generateIndexJson() const
{
    QJsonObject index;
    index[QStringLiteral("version")] = QStringLiteral("1.0");
    index[QStringLiteral("projectIdentifier")] = m_projectIdentifier;
    index[QStringLiteral("lastUpdated")] = m_modifiedDate.toString(Qt::ISODate);

    // Caps metadata
    QJsonObject limits;
    limits[QStringLiteral("maxSharedBytes")] = kMaxSharedBytes;
    limits[QStringLiteral("maxLocalizedBytes")] = kMaxLocalizedBytes;
    index[QStringLiteral("limits")] = limits;

    // Current storage usage
    QJsonObject usage;
    usage[QStringLiteral("sharedBytes")] = totalSharedBytes();
    usage[QStringLiteral("localizedBytes")] = totalLocalizedBytes();
    usage[QStringLiteral("totalBytes")] = totalSharedBytes() + totalLocalizedBytes();
    index[QStringLiteral("usage")] = usage;

    // Shared files listing
    QJsonArray sharedArr;
    for (auto it = m_sharedFiles.constBegin(); it != m_sharedFiles.constEnd(); ++it) {
        QJsonObject f;
        f[QStringLiteral("path")] = it.key();
        f[QStringLiteral("size")] = it.value().size();
        sharedArr.append(f);
    }
    index[QStringLiteral("sharedFiles")] = sharedArr;

    // Localized agent files listing
    QJsonObject agentsObj;
    for (auto it = m_agentFiles.constBegin(); it != m_agentFiles.constEnd(); ++it) {
        QJsonObject a;
        a[QStringLiteral("bytes")] = agentBytes(it.key());
        QJsonArray filesArr;
        for (auto fIt = it.value().constBegin(); fIt != it.value().constEnd(); ++fIt) {
            QJsonObject f;
            f[QStringLiteral("path")] = fIt.key();
            f[QStringLiteral("size")] = fIt.value().size();
            filesArr.append(f);
        }
        a[QStringLiteral("files")] = filesArr;
        agentsObj[it.key()] = a;
    }
    index[QStringLiteral("agents")] = agentsObj;

    return index;
}

void AgenticData::loadIndexJson(const QJsonObject &indexObj)
{
    if (indexObj.contains(QStringLiteral("projectIdentifier"))) {
        m_projectIdentifier = indexObj.value(QStringLiteral("projectIdentifier")).toString();
    }
}

void AgenticData::serializeToArchive(QMap<QString, QByteArray> &archiveEntries) const
{
    // 1. agentic-datas/index.json
    const QByteArray indexBytes = QJsonDocument(generateIndexJson()).toJson(QJsonDocument::Indented);
    archiveEntries.insert(QStringLiteral("agentic-datas/index.json"), indexBytes);

    // 2. agentic-datas/agent_model_history.xml
    archiveEntries.insert(QStringLiteral("agentic-datas/agent_model_history.xml"), rawModelHistoryXml().toUtf8());

    // 3. agentic-datas/agentifiers.json (encrypted identifier registry)
    archiveEntries.insert(QStringLiteral("agentic-datas/agentifiers.json"), exportEncryptedAgentifiers());

    // 4. agentic-datas/shared/...
    for (auto it = m_sharedFiles.constBegin(); it != m_sharedFiles.constEnd(); ++it) {
        const QString archivePath = QStringLiteral("agentic-datas/shared/%1").arg(it.key());
        archiveEntries.insert(archivePath, it.value());
    }

    // 5. agentic-datas/<agent>/...
    for (auto agentIt = m_agentFiles.constBegin(); agentIt != m_agentFiles.constEnd(); ++agentIt) {
        for (auto fileIt = agentIt.value().constBegin(); fileIt != agentIt.value().constEnd(); ++fileIt) {
            const QString archivePath = QStringLiteral("agentic-datas/%1/%2").arg(agentIt.key(), fileIt.key());
            archiveEntries.insert(archivePath, fileIt.value());
        }
    }
}

bool AgenticData::deserializeFromArchive(const QMap<QString, QByteArray> &archiveEntries, QString *error)
{
    clear();

    qint64 incomingSharedBytes = 0;
    qint64 incomingLocalizedBytes = 0;

    // Check sizes first to strictly enforce limits
    for (auto it = archiveEntries.constBegin(); it != archiveEntries.constEnd(); ++it) {
        const QString &key = it.key();
        if (key.startsWith(QStringLiteral("agentic-datas/shared/"))) {
            incomingSharedBytes += it.value().size();
        } else if (key.startsWith(QStringLiteral("agentic-datas/")) &&
                   !key.startsWith(QStringLiteral("agentic-datas/index.json")) &&
                   !key.startsWith(QStringLiteral("agentic-datas/agent_model_history.xml")) &&
                   !key.startsWith(QStringLiteral("agentic-datas/agentifiers.json"))) {
            incomingLocalizedBytes += it.value().size();
        }
    }

    if (incomingSharedBytes > kMaxSharedBytes) {
        if (error) {
            *error = QStringLiteral("Archive shared agent data quota exceeded: %1 bytes (max allowed is 5 MB / %2 bytes)")
                         .arg(incomingSharedBytes)
                         .arg(kMaxSharedBytes);
        }
        return false;
    }

    if (incomingLocalizedBytes > kMaxLocalizedBytes) {
        if (error) {
            *error = QStringLiteral("Archive localized agent data quota exceeded: %1 bytes (max allowed is 20 MB / %2 bytes)")
                         .arg(incomingLocalizedBytes)
                         .arg(kMaxLocalizedBytes);
        }
        return false;
    }

    // Process index.json
    if (archiveEntries.contains(QStringLiteral("agentic-datas/index.json"))) {
        QJsonDocument doc = QJsonDocument::fromJson(archiveEntries.value(QStringLiteral("agentic-datas/index.json")));
        if (doc.isObject()) {
            loadIndexJson(doc.object());
        }
    }

    // Process agent_model_history.xml
    if (archiveEntries.contains(QStringLiteral("agentic-datas/agent_model_history.xml"))) {
        setRawModelHistoryXml(QString::fromUtf8(archiveEntries.value(QStringLiteral("agentic-datas/agent_model_history.xml"))));
    }

    // Process encrypted agentifiers.json
    if (archiveEntries.contains(QStringLiteral("agentic-datas/agentifiers.json"))) {
        QString decryptErr;
        if (!importEncryptedAgentifiers(archiveEntries.value(QStringLiteral("agentic-datas/agentifiers.json")), QByteArray(), &decryptErr)) {
            // Note: If decryption fails due to corrupted key or file, maintain fallback default agentifiers and report warning
            if (error) {
                *error = QStringLiteral("Warning: could not decrypt agentifiers.json (%1)").arg(decryptErr);
            }
        }
    }

    // Process shared/ files
    const QString sharedPrefix = QStringLiteral("agentic-datas/shared/");
    for (auto it = archiveEntries.constBegin(); it != archiveEntries.constEnd(); ++it) {
        if (it.key().startsWith(sharedPrefix)) {
            const QString relPath = it.key().mid(sharedPrefix.length());
            m_sharedFiles.insert(sanitizePath(relPath), it.value());
        }
    }

    // Process <agent>/ files
    const QString agenticPrefix = QStringLiteral("agentic-datas/");
    for (auto it = archiveEntries.constBegin(); it != archiveEntries.constEnd(); ++it) {
        if (it.key().startsWith(agenticPrefix) &&
            !it.key().startsWith(sharedPrefix) &&
            it.key() != QStringLiteral("agentic-datas/index.json") &&
            it.key() != QStringLiteral("agentic-datas/agent_model_history.xml") &&
            it.key() != QStringLiteral("agentic-datas/agentifiers.json")) {

            const QString sub = it.key().mid(agenticPrefix.length());
            const int slashIdx = sub.indexOf(QLatin1Char('/'));
            if (slashIdx > 0) {
                const QString agentName = sanitizeAgentName(sub.left(slashIdx));
                const QString relPath = sanitizePath(sub.mid(slashIdx + 1));
                if (!relPath.isEmpty()) {
                    m_agentFiles[agentName].insert(relPath, it.value());
                }
            }
        }
    }

    return true;
}
