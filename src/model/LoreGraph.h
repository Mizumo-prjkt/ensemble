#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct LoreGraphNode
{
    QString id;
    QString label;
    QString type = QStringLiteral("concept"); // character, location, event, item, faction, concept, plot_point
    double x = 0.0;
    double y = 0.0;
    double width = 170.0;
    double height = 75.0;
    QString shape = QStringLiteral("rounded"); // box, rounded, diamond, ellipse, cylinder, hexagon
    QString color = QStringLiteral("#3b82f6");
    QString loreId; // Optional link to entity in lore_data.json
    QString notes;
    QJsonObject customData;
};

struct LoreGraphBox
{
    QString id;
    QString label;
    double x = 0.0;
    double y = 0.0;
    double width = 360.0;
    double height = 240.0;
    QString color = QStringLiteral("#6366f1");
    QStringList memberNodeIds;
};

struct LoreGraphConnector
{
    QString id;
    QString fromNodeId;
    QString toNodeId;
    QString arrowType = QStringLiteral("->"); // ->, <->, --, -x
    QString label;
    QString relation = QStringLiteral("related");
    QString style = QStringLiteral("solid"); // solid, dashed, dotted, thick
    QString color = QStringLiteral("#94a3b8");
};

struct LoreGraphFollowup
{
    QString nodeId;
    int targetChapter = 0;
    QString status = QStringLiteral("pending"); // pending, in_progress, resolved
    QString note;
};

class LoreGraph
{
public:
    LoreGraph();

    // Magic bytes: 8 bytes = \x89 G R P H \x01 \r \n (0x89, 'G', 'R', 'P', 'H', 0x01, 0x0D, 0x0A)
    static const QByteArray &magicBytes();
    static bool hasValidMagic(const QByteArray &bytes);

    QString title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    QString direction() const { return m_direction; }
    void setDirection(const QString &dir) { m_direction = dir; }

    const QList<LoreGraphNode> &nodes() const { return m_nodes; }
    QList<LoreGraphNode> &nodes() { return m_nodes; }

    const QList<LoreGraphBox> &boxes() const { return m_boxes; }
    QList<LoreGraphBox> &boxes() { return m_boxes; }

    const QList<LoreGraphConnector> &connectors() const { return m_connectors; }
    QList<LoreGraphConnector> &connectors() { return m_connectors; }

    const QList<LoreGraphFollowup> &followups() const { return m_followups; }
    QList<LoreGraphFollowup> &followups() { return m_followups; }

    // Node operations
    void addOrUpdateNode(const LoreGraphNode &node);
    bool removeNode(const QString &id);
    LoreGraphNode *findNode(const QString &id);
    const LoreGraphNode *findNode(const QString &id) const;
    void updateNodePosition(const QString &id, double x, double y);

    // Box operations
    void addOrUpdateBox(const LoreGraphBox &box);
    bool removeBox(const QString &id);
    LoreGraphBox *findBox(const QString &id);
    const LoreGraphBox *findBox(const QString &id) const;

    // Connector operations
    void addConnector(const LoreGraphConnector &conn);
    bool removeConnector(const QString &id);
    void connectNodes(const QString &fromId, const QString &toId,
                      const QString &arrow = QStringLiteral("->"),
                      const QString &label = QString(),
                      const QString &style = QStringLiteral("solid"),
                      const QString &color = QStringLiteral("#94a3b8"));

    // Followup operations
    void addOrUpdateFollowup(const LoreGraphFollowup &f);
    bool removeFollowup(const QString &nodeId);

    void clear();
    void autoLayout();

    // Serialization: .grph file bytes (magic header + UTF-8 DSL)
    QByteArray toGrphBytes() const;
    bool fromGrphBytes(const QByteArray &bytes, QString *error = nullptr);

    // Human and Agent readable DSL string
    QString toDsl() const;
    bool fromDsl(const QString &dsl, QString *error = nullptr);

    // Complete language grammar specification (for MCP agents and documentation)
    static QString dslGrammarSpecification();

    // Semantic validation helpers
    static bool isValidHexOrNamedColor(const QString &c);
    static bool isValidNodeType(const QString &t);
    static bool isValidNodeShape(const QString &s);
    static bool isValidConnectorStyle(const QString &s);
    static bool isValidArrowType(const QString &a);

    // Summary JSON for MCP and logging
    QJsonObject toJson() const;

private:
    QString m_title = QStringLiteral("Story Lore Map");
    QString m_direction = QStringLiteral("LR");
    QList<LoreGraphNode> m_nodes;
    QList<LoreGraphBox> m_boxes;
    QList<LoreGraphConnector> m_connectors;
    QList<LoreGraphFollowup> m_followups;
};
