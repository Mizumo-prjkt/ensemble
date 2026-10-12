#pragma once

#include "LoreGraph.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct LoreEntity
{
    QString id;
    QString name;
    QString category; // Character, Location, Faction, Artifact, Concept, Rule, Fact
    QString description;
    QStringList tags;
    QStringList associatedCharacters;
    QList<int> chaptersReferenced;
    QJsonObject attributes;
    QDateTime created;
    QDateTime modified;
};

struct LoreHistoryEvent
{
    QString id;
    QString title;
    QString eraOrDate;
    QString summary;
    QString impact;
    QStringList involvedEntities;
    int storyChapter = -1; // -1 if backstory/pre-canon
};

struct LoreConflict
{
    QString id;
    QString title;
    QString description;
    QString severity = QStringLiteral("medium"); // low, medium, high, critical
    QString status = QStringLiteral("open");     // open, investigating, resolved
    int chapterA = 0;
    int chapterB = 0;
    QString detectedBy = QStringLiteral("user");
    QDateTime detectedAt;
};

struct LoreResolvedConflict
{
    QString id;
    QString title;
    QString resolution;
    QString canonRuling;
    QString resolvedBy = QStringLiteral("user");
    QDateTime resolvedAt;
};

struct LorePlan
{
    QString id;
    QString title;
    QString description;
    int targetChapter = 0;
    QString status = QStringLiteral("planned"); // planned, in_progress, fulfilled, abandoned
    QString loreThread;
    QStringList neededFollowUps; // Lore threads to follow up with the story
    QString notes;
};

struct LoreReference
{
    QString id;
    QString title;
    QString source; // e.g. "Canon S2E4", "AO3 Tag Guide"
    QString citation;
    QString applicability;
    QString notes;
};

class LoreManager
{
public:
    LoreManager();

    void clear();
    void ensureDefaults();

    // Archive serialization for .ao3proj (lore/ directory structure)
    void serializeToArchive(QMap<QString, QByteArray> &fileEntries) const;
    bool deserializeFromArchive(const QMap<QString, QByteArray> &entries, QString *error = nullptr);

    // Entity operations (lore/lore_data.json)
    const QList<LoreEntity> &entities() const { return m_entities; }
    void addOrUpdateEntity(const LoreEntity &entity);
    bool removeEntity(const QString &id);
    const LoreEntity *findEntity(const QString &id) const;

    // History operations (lore/history.json)
    const QList<LoreHistoryEvent> &historyEvents() const { return m_historyEvents; }
    void addOrUpdateHistoryEvent(const LoreHistoryEvent &event);
    bool removeHistoryEvent(const QString &id);

    // Conflicts operations (lore/conflicts.json & lore/resolved.json)
    const QList<LoreConflict> &conflicts() const { return m_conflicts; }
    void addOrUpdateConflict(const LoreConflict &conflict);
    bool resolveConflict(const QString &conflictId, const QString &resolution,
                         const QString &canonRuling = QString(),
                         const QString &resolvedBy = QStringLiteral("user"));
    const QList<LoreResolvedConflict> &resolvedConflicts() const { return m_resolvedConflicts; }

    // Plans operations (lore/plans.json)
    const QList<LorePlan> &plans() const { return m_plans; }
    void addOrUpdatePlan(const LorePlan &plan);
    bool removePlan(const QString &id);

    // References operations (lore/references.json)
    const QList<LoreReference> &references() const { return m_references; }
    void addOrUpdateReference(const LoreReference &ref);
    bool removeReference(const QString &id);

    // Lore Graph operations (lore/graphs/data.grph)
    LoreGraph &defaultGraph() { return m_defaultGraph; }
    const LoreGraph &defaultGraph() const { return m_defaultGraph; }
    LoreGraph *graph(const QString &name = QStringLiteral("data"));
    const LoreGraph *graph(const QString &name = QStringLiteral("data")) const;
    void setNamedGraph(const QString &name, const LoreGraph &graph);
    QStringList graphNames() const;

    // Metadata XML (lore/metadata.xml)
    QByteArray serializeMetadataXml() const;
    bool deserializeMetadataXml(const QByteArray &xmlBytes);

    // Overall summary for MCP and diagnostics
    QJsonObject toJsonSummary() const;

private:
    QByteArray serializeEntitiesJson() const;
    bool deserializeEntitiesJson(const QByteArray &jsonBytes);

    QByteArray serializeHistoryJson() const;
    bool deserializeHistoryJson(const QByteArray &jsonBytes);

    QByteArray serializeConflictsJson() const;
    bool deserializeConflictsJson(const QByteArray &jsonBytes);

    QByteArray serializeResolvedJson() const;
    bool deserializeResolvedJson(const QByteArray &jsonBytes);

    QByteArray serializePlansJson() const;
    bool deserializePlansJson(const QByteArray &jsonBytes);

    QByteArray serializeReferencesJson() const;
    bool deserializeReferencesJson(const QByteArray &jsonBytes);

    QList<LoreEntity> m_entities;
    QList<LoreHistoryEvent> m_historyEvents;
    QList<LoreConflict> m_conflicts;
    QList<LoreResolvedConflict> m_resolvedConflicts;
    QList<LorePlan> m_plans;
    QList<LoreReference> m_references;

    LoreGraph m_defaultGraph;
    QMap<QString, LoreGraph> m_namedGraphs;

    QDateTime m_lastUpdated;
    QString m_lastModifiedBy = QStringLiteral("user");
};
