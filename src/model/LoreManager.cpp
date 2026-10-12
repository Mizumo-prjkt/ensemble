#include "LoreManager.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

LoreManager::LoreManager()
{
    clear();
}

void LoreManager::clear()
{
    m_entities.clear();
    m_historyEvents.clear();
    m_conflicts.clear();
    m_resolvedConflicts.clear();
    m_plans.clear();
    m_references.clear();
    m_defaultGraph.clear();
    m_namedGraphs.clear();
    m_lastUpdated = QDateTime::currentDateTimeUtc();
    m_lastModifiedBy = QStringLiteral("user");
}

void LoreManager::ensureDefaults()
{
    m_lastUpdated = QDateTime::currentDateTimeUtc();

    // Default starter graph in lore/graphs/data.grph if currently empty
    if (m_defaultGraph.nodes().isEmpty()) {
        m_defaultGraph.setTitle(QStringLiteral("Story Lore & Narrative Arc"));
        m_defaultGraph.setDirection(QStringLiteral("LR"));

        LoreGraphNode n1;
        n1.id = QStringLiteral("main_protagonist");
        n1.label = QStringLiteral("Protagonist");
        n1.type = QStringLiteral("character");
        n1.x = 80.0;
        n1.y = 120.0;
        n1.color = QStringLiteral("#3b82f6");
        n1.shape = QStringLiteral("rounded");
        m_defaultGraph.addOrUpdateNode(n1);

        LoreGraphNode n2;
        n2.id = QStringLiteral("inciting_incident");
        n2.label = QStringLiteral("Inciting Incident");
        n2.type = QStringLiteral("event");
        n2.x = 360.0;
        n2.y = 120.0;
        n2.color = QStringLiteral("#f59e0b");
        n2.shape = QStringLiteral("diamond");
        m_defaultGraph.addOrUpdateNode(n2);

        LoreGraphNode n3;
        n3.id = QStringLiteral("key_setting");
        n3.label = QStringLiteral("Primary Setting");
        n3.type = QStringLiteral("location");
        n3.x = 640.0;
        n3.y = 120.0;
        n3.color = QStringLiteral("#10b981");
        n3.shape = QStringLiteral("box");
        m_defaultGraph.addOrUpdateNode(n3);

        m_defaultGraph.connectNodes(QStringLiteral("main_protagonist"), QStringLiteral("inciting_incident"),
                                   QStringLiteral("->"), QStringLiteral("triggers"));
        m_defaultGraph.connectNodes(QStringLiteral("inciting_incident"), QStringLiteral("key_setting"),
                                   QStringLiteral("->"), QStringLiteral("takes place in"));

        LoreGraphFollowup f;
        f.nodeId = QStringLiteral("inciting_incident");
        f.targetChapter = 1;
        f.status = QStringLiteral("pending");
        f.note = QStringLiteral("Establish character stakes and core premise.");
        m_defaultGraph.addOrUpdateFollowup(f);
    }
}

void LoreManager::serializeToArchive(QMap<QString, QByteArray> &fileEntries) const
{
    // 1. lore/lore_data.json
    fileEntries.insert(QStringLiteral("lore/lore_data.json"), serializeEntitiesJson());

    // 2. lore/history.json
    fileEntries.insert(QStringLiteral("lore/history.json"), serializeHistoryJson());

    // 3. lore/conflicts.json
    fileEntries.insert(QStringLiteral("lore/conflicts.json"), serializeConflictsJson());

    // 4. lore/resolved.json
    fileEntries.insert(QStringLiteral("lore/resolved.json"), serializeResolvedJson());

    // 5. lore/plans.json
    fileEntries.insert(QStringLiteral("lore/plans.json"), serializePlansJson());

    // 6. lore/references.json
    fileEntries.insert(QStringLiteral("lore/references.json"), serializeReferencesJson());

    // 7. lore/metadata.xml
    fileEntries.insert(QStringLiteral("lore/metadata.xml"), serializeMetadataXml());

    // 8. lore/graphs/data.grph
    fileEntries.insert(QStringLiteral("lore/graphs/data.grph"), m_defaultGraph.toGrphBytes());

    // Additional named graphs
    for (auto it = m_namedGraphs.constBegin(); it != m_namedGraphs.constEnd(); ++it) {
        fileEntries.insert(QStringLiteral("lore/graphs/%1.grph").arg(it.key()), it.value().toGrphBytes());
    }
}

bool LoreManager::deserializeFromArchive(const QMap<QString, QByteArray> &entries, QString *error)
{
    clear();

    bool hasAnyLore = false;

    // 1. lore/lore_data.json
    if (entries.contains(QStringLiteral("lore/lore_data.json"))) {
        deserializeEntitiesJson(entries.value(QStringLiteral("lore/lore_data.json")));
        hasAnyLore = true;
    }

    // 2. lore/history.json
    if (entries.contains(QStringLiteral("lore/history.json"))) {
        deserializeHistoryJson(entries.value(QStringLiteral("lore/history.json")));
        hasAnyLore = true;
    }

    // 3. lore/conflicts.json
    if (entries.contains(QStringLiteral("lore/conflicts.json"))) {
        deserializeConflictsJson(entries.value(QStringLiteral("lore/conflicts.json")));
        hasAnyLore = true;
    }

    // 4. lore/resolved.json
    if (entries.contains(QStringLiteral("lore/resolved.json"))) {
        deserializeResolvedJson(entries.value(QStringLiteral("lore/resolved.json")));
        hasAnyLore = true;
    }

    // 5. lore/plans.json
    if (entries.contains(QStringLiteral("lore/plans.json"))) {
        deserializePlansJson(entries.value(QStringLiteral("lore/plans.json")));
        hasAnyLore = true;
    }

    // 6. lore/references.json
    if (entries.contains(QStringLiteral("lore/references.json"))) {
        deserializeReferencesJson(entries.value(QStringLiteral("lore/references.json")));
        hasAnyLore = true;
    }

    // 7. lore/metadata.xml
    if (entries.contains(QStringLiteral("lore/metadata.xml"))) {
        deserializeMetadataXml(entries.value(QStringLiteral("lore/metadata.xml")));
        hasAnyLore = true;
    }

    // 8. lore/graphs/data.grph and other graphs
    if (entries.contains(QStringLiteral("lore/graphs/data.grph"))) {
        m_defaultGraph.fromGrphBytes(entries.value(QStringLiteral("lore/graphs/data.grph")), error);
        hasAnyLore = true;
    }

    // Scan for any other .grph files in lore/graphs/
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString &key = it.key();
        if (key.startsWith(QStringLiteral("lore/graphs/")) && key.endsWith(QStringLiteral(".grph"))) {
            QString baseName = key.mid(12); // after "lore/graphs/"
            baseName.chop(5); // remove ".grph"
            if (baseName != QStringLiteral("data")) {
                LoreGraph g;
                g.fromGrphBytes(it.value());
                m_namedGraphs.insert(baseName, g);
            }
        }
    }

    if (!hasAnyLore) {
        ensureDefaults();
    }

    return true;
}

// -------------------------------------------------------------
// Entity CRUD
// -------------------------------------------------------------
void LoreManager::addOrUpdateEntity(const LoreEntity &entity)
{
    for (int i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i].id == entity.id) {
            m_entities[i] = entity;
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return;
        }
    }
    m_entities.append(entity);
    m_lastUpdated = QDateTime::currentDateTimeUtc();
}

bool LoreManager::removeEntity(const QString &id)
{
    for (int i = m_entities.size() - 1; i >= 0; --i) {
        if (m_entities[i].id == id) {
            m_entities.removeAt(i);
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return true;
        }
    }
    return false;
}

const LoreEntity *LoreManager::findEntity(const QString &id) const
{
    for (const auto &e : m_entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

// -------------------------------------------------------------
// History CRUD
// -------------------------------------------------------------
void LoreManager::addOrUpdateHistoryEvent(const LoreHistoryEvent &event)
{
    for (int i = 0; i < m_historyEvents.size(); ++i) {
        if (m_historyEvents[i].id == event.id) {
            m_historyEvents[i] = event;
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return;
        }
    }
    m_historyEvents.append(event);
    m_lastUpdated = QDateTime::currentDateTimeUtc();
}

bool LoreManager::removeHistoryEvent(const QString &id)
{
    for (int i = m_historyEvents.size() - 1; i >= 0; --i) {
        if (m_historyEvents[i].id == id) {
            m_historyEvents.removeAt(i);
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------
// Conflict & Resolution CRUD
// -------------------------------------------------------------
void LoreManager::addOrUpdateConflict(const LoreConflict &conflict)
{
    for (int i = 0; i < m_conflicts.size(); ++i) {
        if (m_conflicts[i].id == conflict.id) {
            m_conflicts[i] = conflict;
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return;
        }
    }
    m_conflicts.append(conflict);
    m_lastUpdated = QDateTime::currentDateTimeUtc();
}

bool LoreManager::resolveConflict(const QString &conflictId, const QString &resolution,
                                   const QString &canonRuling, const QString &resolvedBy)
{
    for (int i = 0; i < m_conflicts.size(); ++i) {
        if (m_conflicts[i].id == conflictId) {
            LoreConflict c = m_conflicts.takeAt(i);

            LoreResolvedConflict res;
            res.id = c.id;
            res.title = c.title;
            res.resolution = resolution;
            res.canonRuling = canonRuling;
            res.resolvedBy = resolvedBy;
            res.resolvedAt = QDateTime::currentDateTimeUtc();
            m_resolvedConflicts.append(res);

            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------
// Plans CRUD
// -------------------------------------------------------------
void LoreManager::addOrUpdatePlan(const LorePlan &plan)
{
    for (int i = 0; i < m_plans.size(); ++i) {
        if (m_plans[i].id == plan.id) {
            m_plans[i] = plan;
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return;
        }
    }
    m_plans.append(plan);
    m_lastUpdated = QDateTime::currentDateTimeUtc();
}

bool LoreManager::removePlan(const QString &id)
{
    for (int i = m_plans.size() - 1; i >= 0; --i) {
        if (m_plans[i].id == id) {
            m_plans.removeAt(i);
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------
// References CRUD
// -------------------------------------------------------------
void LoreManager::addOrUpdateReference(const LoreReference &ref)
{
    for (int i = 0; i < m_references.size(); ++i) {
        if (m_references[i].id == ref.id) {
            m_references[i] = ref;
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return;
        }
    }
    m_references.append(ref);
    m_lastUpdated = QDateTime::currentDateTimeUtc();
}

bool LoreManager::removeReference(const QString &id)
{
    for (int i = m_references.size() - 1; i >= 0; --i) {
        if (m_references[i].id == id) {
            m_references.removeAt(i);
            m_lastUpdated = QDateTime::currentDateTimeUtc();
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------
// Named Graph Access
// -------------------------------------------------------------
LoreGraph *LoreManager::graph(const QString &name)
{
    if (name.isEmpty() || name == QStringLiteral("data")) {
        return &m_defaultGraph;
    }
    if (m_namedGraphs.contains(name)) {
        return &m_namedGraphs[name];
    }
    return nullptr;
}

const LoreGraph *LoreManager::graph(const QString &name) const
{
    if (name.isEmpty() || name == QStringLiteral("data")) {
        return &m_defaultGraph;
    }
    auto it = m_namedGraphs.constFind(name);
    if (it != m_namedGraphs.constEnd()) {
        return &it.value();
    }
    return nullptr;
}

void LoreManager::setNamedGraph(const QString &name, const LoreGraph &g)
{
    if (name.isEmpty() || name == QStringLiteral("data")) {
        m_defaultGraph = g;
    } else {
        m_namedGraphs.insert(name, g);
    }
    m_lastUpdated = QDateTime::currentDateTimeUtc();
}

QStringList LoreManager::graphNames() const
{
    QStringList names;
    names.append(QStringLiteral("data"));
    names.append(m_namedGraphs.keys());
    return names;
}

// -------------------------------------------------------------
// JSON Serialization Helpers
// -------------------------------------------------------------
QByteArray LoreManager::serializeEntitiesJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ensemble-lore-data-v1");
    root[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    QJsonArray arr;
    for (const auto &e : m_entities) {
        QJsonObject o;
        o[QStringLiteral("id")] = e.id;
        o[QStringLiteral("name")] = e.name;
        o[QStringLiteral("category")] = e.category;
        o[QStringLiteral("description")] = e.description;

        QJsonArray tagArr;
        for (const auto &t : e.tags) tagArr.append(t);
        o[QStringLiteral("tags")] = tagArr;

        QJsonArray charArr;
        for (const auto &c : e.associatedCharacters) charArr.append(c);
        o[QStringLiteral("associatedCharacters")] = charArr;

        QJsonArray chRefArr;
        for (int ch : e.chaptersReferenced) chRefArr.append(ch);
        o[QStringLiteral("chaptersReferenced")] = chRefArr;

        o[QStringLiteral("attributes")] = e.attributes;
        if (e.created.isValid()) o[QStringLiteral("created")] = e.created.toString(Qt::ISODate);
        if (e.modified.isValid()) o[QStringLiteral("modified")] = e.modified.toString(Qt::ISODate);
        arr.append(o);
    }
    root[QStringLiteral("entities")] = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool LoreManager::deserializeEntitiesJson(const QByteArray &jsonBytes)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (!doc.isObject()) return false;

    m_entities.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("entities")).toArray();
    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();

        LoreEntity e;
        e.id = o.value(QStringLiteral("id")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.category = o.value(QStringLiteral("category")).toString(QStringLiteral("Concept"));
        e.description = o.value(QStringLiteral("description")).toString();

        for (const auto &t : o.value(QStringLiteral("tags")).toArray()) {
            e.tags.append(t.toString());
        }
        for (const auto &c : o.value(QStringLiteral("associatedCharacters")).toArray()) {
            e.associatedCharacters.append(c.toString());
        }
        for (const auto &ch : o.value(QStringLiteral("chaptersReferenced")).toArray()) {
            e.chaptersReferenced.append(ch.toInt());
        }
        e.attributes = o.value(QStringLiteral("attributes")).toObject();
        e.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
        e.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(), Qt::ISODate);

        m_entities.append(e);
    }
    return true;
}

QByteArray LoreManager::serializeHistoryJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ensemble-lore-history-v1");
    root[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    QJsonArray arr;
    for (const auto &h : m_historyEvents) {
        QJsonObject o;
        o[QStringLiteral("id")] = h.id;
        o[QStringLiteral("title")] = h.title;
        o[QStringLiteral("eraOrDate")] = h.eraOrDate;
        o[QStringLiteral("summary")] = h.summary;
        o[QStringLiteral("impact")] = h.impact;
        o[QStringLiteral("storyChapter")] = h.storyChapter;

        QJsonArray invArr;
        for (const auto &inv : h.involvedEntities) invArr.append(inv);
        o[QStringLiteral("involvedEntities")] = invArr;

        arr.append(o);
    }
    root[QStringLiteral("events")] = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool LoreManager::deserializeHistoryJson(const QByteArray &jsonBytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) return false;

    m_historyEvents.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("events")).toArray();
    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();

        LoreHistoryEvent h;
        h.id = o.value(QStringLiteral("id")).toString();
        h.title = o.value(QStringLiteral("title")).toString();
        h.eraOrDate = o.value(QStringLiteral("eraOrDate")).toString();
        h.summary = o.value(QStringLiteral("summary")).toString();
        h.impact = o.value(QStringLiteral("impact")).toString();
        h.storyChapter = o.value(QStringLiteral("storyChapter")).toInt(-1);

        for (const auto &inv : o.value(QStringLiteral("involvedEntities")).toArray()) {
            h.involvedEntities.append(inv.toString());
        }
        m_historyEvents.append(h);
    }
    return true;
}

QByteArray LoreManager::serializeConflictsJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ensemble-lore-conflicts-v1");
    root[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    QJsonArray arr;
    for (const auto &c : m_conflicts) {
        QJsonObject o;
        o[QStringLiteral("id")] = c.id;
        o[QStringLiteral("title")] = c.title;
        o[QStringLiteral("description")] = c.description;
        o[QStringLiteral("severity")] = c.severity;
        o[QStringLiteral("status")] = c.status;
        o[QStringLiteral("chapterA")] = c.chapterA;
        o[QStringLiteral("chapterB")] = c.chapterB;
        o[QStringLiteral("detectedBy")] = c.detectedBy;
        if (c.detectedAt.isValid()) o[QStringLiteral("detectedAt")] = c.detectedAt.toString(Qt::ISODate);
        arr.append(o);
    }
    root[QStringLiteral("conflicts")] = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool LoreManager::deserializeConflictsJson(const QByteArray &jsonBytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) return false;

    m_conflicts.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("conflicts")).toArray();
    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();

        LoreConflict c;
        c.id = o.value(QStringLiteral("id")).toString();
        c.title = o.value(QStringLiteral("title")).toString();
        c.description = o.value(QStringLiteral("description")).toString();
        c.severity = o.value(QStringLiteral("severity")).toString(QStringLiteral("medium"));
        c.status = o.value(QStringLiteral("status")).toString(QStringLiteral("open"));
        c.chapterA = o.value(QStringLiteral("chapterA")).toInt(0);
        c.chapterB = o.value(QStringLiteral("chapterB")).toInt(0);
        c.detectedBy = o.value(QStringLiteral("detectedBy")).toString(QStringLiteral("user"));
        c.detectedAt = QDateTime::fromString(o.value(QStringLiteral("detectedAt")).toString(), Qt::ISODate);
        m_conflicts.append(c);
    }
    return true;
}

QByteArray LoreManager::serializeResolvedJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ensemble-lore-resolved-v1");
    root[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    QJsonArray arr;
    for (const auto &r : m_resolvedConflicts) {
        QJsonObject o;
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("title")] = r.title;
        o[QStringLiteral("resolution")] = r.resolution;
        o[QStringLiteral("canonRuling")] = r.canonRuling;
        o[QStringLiteral("resolvedBy")] = r.resolvedBy;
        if (r.resolvedAt.isValid()) o[QStringLiteral("resolvedAt")] = r.resolvedAt.toString(Qt::ISODate);
        arr.append(o);
    }
    root[QStringLiteral("resolved")] = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool LoreManager::deserializeResolvedJson(const QByteArray &jsonBytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) return false;

    m_resolvedConflicts.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("resolved")).toArray();
    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();

        LoreResolvedConflict r;
        r.id = o.value(QStringLiteral("id")).toString();
        r.title = o.value(QStringLiteral("title")).toString();
        r.resolution = o.value(QStringLiteral("resolution")).toString();
        r.canonRuling = o.value(QStringLiteral("canonRuling")).toString();
        r.resolvedBy = o.value(QStringLiteral("resolvedBy")).toString(QStringLiteral("user"));
        r.resolvedAt = QDateTime::fromString(o.value(QStringLiteral("resolvedAt")).toString(), Qt::ISODate);
        m_resolvedConflicts.append(r);
    }
    return true;
}

QByteArray LoreManager::serializePlansJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ensemble-lore-plans-v1");
    root[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    QJsonArray arr;
    for (const auto &p : m_plans) {
        QJsonObject o;
        o[QStringLiteral("id")] = p.id;
        o[QStringLiteral("title")] = p.title;
        o[QStringLiteral("description")] = p.description;
        o[QStringLiteral("targetChapter")] = p.targetChapter;
        o[QStringLiteral("status")] = p.status;
        o[QStringLiteral("loreThread")] = p.loreThread;

        QJsonArray reqArr;
        for (const auto &f : p.neededFollowUps) reqArr.append(f);
        o[QStringLiteral("neededFollowUps")] = reqArr;

        o[QStringLiteral("notes")] = p.notes;
        arr.append(o);
    }
    root[QStringLiteral("plans")] = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool LoreManager::deserializePlansJson(const QByteArray &jsonBytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) return false;

    m_plans.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("plans")).toArray();
    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();

        LorePlan p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.title = o.value(QStringLiteral("title")).toString();
        p.description = o.value(QStringLiteral("description")).toString();
        p.targetChapter = o.value(QStringLiteral("targetChapter")).toInt(0);
        p.status = o.value(QStringLiteral("status")).toString(QStringLiteral("planned"));
        p.loreThread = o.value(QStringLiteral("loreThread")).toString();

        for (const auto &f : o.value(QStringLiteral("neededFollowUps")).toArray()) {
            p.neededFollowUps.append(f.toString());
        }
        p.notes = o.value(QStringLiteral("notes")).toString();
        m_plans.append(p);
    }
    return true;
}

QByteArray LoreManager::serializeReferencesJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ensemble-lore-references-v1");
    root[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    QJsonArray arr;
    for (const auto &r : m_references) {
        QJsonObject o;
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("title")] = r.title;
        o[QStringLiteral("source")] = r.source;
        o[QStringLiteral("citation")] = r.citation;
        o[QStringLiteral("applicability")] = r.applicability;
        o[QStringLiteral("notes")] = r.notes;
        arr.append(o);
    }
    root[QStringLiteral("references")] = arr;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool LoreManager::deserializeReferencesJson(const QByteArray &jsonBytes)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    if (!doc.isObject()) return false;

    m_references.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("references")).toArray();
    for (const auto &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();

        LoreReference r;
        r.id = o.value(QStringLiteral("id")).toString();
        r.title = o.value(QStringLiteral("title")).toString();
        r.source = o.value(QStringLiteral("source")).toString();
        r.citation = o.value(QStringLiteral("citation")).toString();
        r.applicability = o.value(QStringLiteral("applicability")).toString();
        r.notes = o.value(QStringLiteral("notes")).toString();
        m_references.append(r);
    }
    return true;
}

// -------------------------------------------------------------
// Metadata XML (lore/metadata.xml)
// -------------------------------------------------------------
QByteArray LoreManager::serializeMetadataXml() const
{
    QByteArray xmlBytes;
    QXmlStreamWriter xml(&xmlBytes);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("lore-metadata"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));

    xml.writeStartElement(QStringLiteral("stats"));
    xml.writeTextElement(QStringLiteral("entityCount"), QString::number(m_entities.size()));
    xml.writeTextElement(QStringLiteral("historyCount"), QString::number(m_historyEvents.size()));
    xml.writeTextElement(QStringLiteral("conflictCount"), QString::number(m_conflicts.size()));
    xml.writeTextElement(QStringLiteral("resolvedCount"), QString::number(m_resolvedConflicts.size()));
    xml.writeTextElement(QStringLiteral("planCount"), QString::number(m_plans.size()));
    xml.writeTextElement(QStringLiteral("referenceCount"), QString::number(m_references.size()));
    xml.writeTextElement(QStringLiteral("graphCount"), QString::number(1 + m_namedGraphs.size()));
    xml.writeTextElement(QStringLiteral("nodeCount"), QString::number(m_defaultGraph.nodes().size()));
    xml.writeTextElement(QStringLiteral("connectorCount"), QString::number(m_defaultGraph.connectors().size()));
    xml.writeEndElement(); // stats

    xml.writeTextElement(QStringLiteral("defaultGraph"), QStringLiteral("lore/graphs/data.grph"));
    xml.writeTextElement(QStringLiteral("lastUpdated"), m_lastUpdated.toString(Qt::ISODate));
    xml.writeTextElement(QStringLiteral("lastModifiedBy"), m_lastModifiedBy);

    xml.writeEndElement(); // lore-metadata
    xml.writeEndDocument();
    return xmlBytes;
}

bool LoreManager::deserializeMetadataXml(const QByteArray &xmlBytes)
{
    QXmlStreamReader xml(xmlBytes);
    if (!xml.readNextStartElement()) return false;
    if (xml.name() != QStringLiteral("lore-metadata")) return false;

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString tag = xml.name().toString();
            if (tag == QStringLiteral("lastUpdated")) {
                m_lastUpdated = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
            } else if (tag == QStringLiteral("lastModifiedBy")) {
                m_lastModifiedBy = xml.readElementText();
            }
        }
    }
    return true;
}

QJsonObject LoreManager::toJsonSummary() const
{
    QJsonObject summary;
    summary[QStringLiteral("entityCount")] = m_entities.size();
    summary[QStringLiteral("historyCount")] = m_historyEvents.size();
    summary[QStringLiteral("openConflictCount")] = m_conflicts.size();
    summary[QStringLiteral("resolvedConflictCount")] = m_resolvedConflicts.size();
    summary[QStringLiteral("planCount")] = m_plans.size();
    summary[QStringLiteral("referenceCount")] = m_references.size();
    summary[QStringLiteral("graphNodesCount")] = m_defaultGraph.nodes().size();
    summary[QStringLiteral("graphBoxesCount")] = m_defaultGraph.boxes().size();
    summary[QStringLiteral("graphConnectorsCount")] = m_defaultGraph.connectors().size();
    summary[QStringLiteral("lastUpdated")] = m_lastUpdated.toString(Qt::ISODate);

    // Categories breakdown
    QMap<QString, int> catCounts;
    for (const auto &e : m_entities) {
        catCounts[e.category]++;
    }
    QJsonObject cats;
    for (auto it = catCounts.constBegin(); it != catCounts.constEnd(); ++it) {
        cats[it.key()] = it.value();
    }
    summary[QStringLiteral("entityCategories")] = cats;

    return summary;
}
