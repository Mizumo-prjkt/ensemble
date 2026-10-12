#include "LoreGraph.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace {

// 8 unique magic bytes distinguishing .grph format
static const char kGrphMagicData[8] = {'\x89', 'G', 'R', 'P', 'H', '\x01', '\r', '\n'};

// Helper to extract key="value" or key=value attributes from [ ... ] bracket string
QMap<QString, QString> parseAttributes(const QString &bracketContent)
{
    QMap<QString, QString> attrs;
    // Match: key="value", key=val, pos(x, y), size(w, h)
    QRegularExpression rePos(QString::fromUtf8(R"(pos\s*\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\))"));
    auto posMatch = rePos.match(bracketContent);
    if (posMatch.hasMatch()) {
        attrs[QStringLiteral("x")] = posMatch.captured(1);
        attrs[QStringLiteral("y")] = posMatch.captured(2);
    }

    QRegularExpression reSize(QString::fromUtf8(R"(size\s*\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\))"));
    auto sizeMatch = reSize.match(bracketContent);
    if (sizeMatch.hasMatch()) {
        attrs[QStringLiteral("w")] = sizeMatch.captured(1);
        attrs[QStringLiteral("h")] = sizeMatch.captured(2);
    }

    // Match generic key="value" or key=value
    QRegularExpression reAttr(QString::fromUtf8(R"raw(([a-zA-Z0-9_]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s,\]]+)))raw"));
    auto it = reAttr.globalMatch(bracketContent);
    while (it.hasNext()) {
        auto m = it.next();
        QString key = m.captured(1);
        QString val;
        if (!m.captured(2).isNull()) {
            val = m.captured(2);
        } else if (!m.captured(3).isNull()) {
            val = m.captured(3);
        } else {
            val = m.captured(4);
        }
        if (key != QStringLiteral("pos") && key != QStringLiteral("size")) {
            attrs[key] = val;
        }
    }

    return attrs;
}

} // namespace

LoreGraph::LoreGraph()
{
}

const QByteArray &LoreGraph::magicBytes()
{
    static const QByteArray magic(kGrphMagicData, 8);
    return magic;
}

bool LoreGraph::hasValidMagic(const QByteArray &bytes)
{
    if (bytes.size() < 8) return false;
    return memcmp(bytes.constData(), kGrphMagicData, 8) == 0;
}

bool LoreGraph::isValidHexOrNamedColor(const QString &c)
{
    if (c.isEmpty()) return true;
    if (c.startsWith(QLatin1Char('#'))) {
        static const QRegularExpression hexRe(QStringLiteral("^#([0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
        return hexRe.match(c).hasMatch();
    }
    return QColor::fromString(c).isValid();
}

bool LoreGraph::isValidNodeType(const QString &t)
{
    static const QSet<QString> validTypes = {
        QStringLiteral("character"), QStringLiteral("location"),
        QStringLiteral("event"), QStringLiteral("item"),
        QStringLiteral("faction"), QStringLiteral("concept"),
        QStringLiteral("plot_point")
    };
    return validTypes.contains(t.toLower());
}

bool LoreGraph::isValidNodeShape(const QString &s)
{
    static const QSet<QString> validShapes = {
        QStringLiteral("box"), QStringLiteral("rounded"),
        QStringLiteral("diamond"), QStringLiteral("ellipse"),
        QStringLiteral("cylinder"), QStringLiteral("hexagon")
    };
    return validShapes.contains(s.toLower());
}

bool LoreGraph::isValidConnectorStyle(const QString &s)
{
    static const QSet<QString> validStyles = {
        QStringLiteral("solid"), QStringLiteral("dashed"),
        QStringLiteral("dotted"), QStringLiteral("thick")
    };
    return validStyles.contains(s.toLower());
}

bool LoreGraph::isValidArrowType(const QString &a)
{
    static const QSet<QString> validArrows = {
        QStringLiteral("->"), QStringLiteral("<->"),
        QStringLiteral("--"), QStringLiteral("-x")
    };
    return validArrows.contains(a);
}

void LoreGraph::addOrUpdateNode(const LoreGraphNode &node)
{
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].id == node.id) {
            m_nodes[i] = node;
            return;
        }
    }
    m_nodes.append(node);
}

bool LoreGraph::removeNode(const QString &id)
{
    bool removed = false;
    for (int i = m_nodes.size() - 1; i >= 0; --i) {
        if (m_nodes[i].id == id) {
            m_nodes.removeAt(i);
            removed = true;
        }
    }

    // Also remove connectors associated with this node
    for (int i = m_connectors.size() - 1; i >= 0; --i) {
        if (m_connectors[i].fromNodeId == id || m_connectors[i].toNodeId == id) {
            m_connectors.removeAt(i);
        }
    }

    // Remove from boxes
    for (auto &box : m_boxes) {
        box.memberNodeIds.removeAll(id);
    }

    // Remove followup
    removeFollowup(id);

    return removed;
}

LoreGraphNode *LoreGraph::findNode(const QString &id)
{
    for (auto &n : m_nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const LoreGraphNode *LoreGraph::findNode(const QString &id) const
{
    for (const auto &n : m_nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

void LoreGraph::updateNodePosition(const QString &id, double x, double y)
{
    if (auto *n = findNode(id)) {
        n->x = x;
        n->y = y;
    }
}

void LoreGraph::addOrUpdateBox(const LoreGraphBox &box)
{
    for (int i = 0; i < m_boxes.size(); ++i) {
        if (m_boxes[i].id == box.id) {
            m_boxes[i] = box;
            return;
        }
    }
    m_boxes.append(box);
}

bool LoreGraph::removeBox(const QString &id)
{
    for (int i = m_boxes.size() - 1; i >= 0; --i) {
        if (m_boxes[i].id == id) {
            m_boxes.removeAt(i);
            return true;
        }
    }
    return false;
}

LoreGraphBox *LoreGraph::findBox(const QString &id)
{
    for (auto &b : m_boxes) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

const LoreGraphBox *LoreGraph::findBox(const QString &id) const
{
    for (const auto &b : m_boxes) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

void LoreGraph::addConnector(const LoreGraphConnector &conn)
{
    // If connector with same id exists, update it
    if (!conn.id.isEmpty()) {
        for (int i = 0; i < m_connectors.size(); ++i) {
            if (m_connectors[i].id == conn.id) {
                m_connectors[i] = conn;
                return;
            }
        }
    }
    m_connectors.append(conn);
}

bool LoreGraph::removeConnector(const QString &id)
{
    for (int i = m_connectors.size() - 1; i >= 0; --i) {
        if (m_connectors[i].id == id) {
            m_connectors.removeAt(i);
            return true;
        }
    }
    return false;
}

void LoreGraph::connectNodes(const QString &fromId, const QString &toId,
                             const QString &arrow, const QString &label,
                             const QString &style, const QString &color)
{
    // Avoid exact duplicate connections
    for (const auto &c : m_connectors) {
        if (c.fromNodeId == fromId && c.toNodeId == toId && c.label == label && c.arrowType == arrow) {
            return;
        }
    }

    LoreGraphConnector conn;
    const QString baseId = QStringLiteral("conn_%1_%2").arg(fromId, toId);
    QString candId = baseId;
    int suffix = 2;
    bool idExists = true;
    while (idExists) {
        idExists = false;
        for (const auto &existing : m_connectors) {
            if (existing.id == candId) {
                idExists = true;
                candId = QStringLiteral("%1_%2").arg(baseId).arg(suffix++);
                break;
            }
        }
    }
    conn.id = candId;
    conn.fromNodeId = fromId;
    conn.toNodeId = toId;
    conn.arrowType = arrow.isEmpty() ? QStringLiteral("->") : arrow;
    conn.label = label;
    conn.style = style.isEmpty() ? QStringLiteral("solid") : style;
    conn.color = color.isEmpty() ? QStringLiteral("#94a3b8") : color;
    m_connectors.append(conn);
}

void LoreGraph::addOrUpdateFollowup(const LoreGraphFollowup &f)
{
    for (int i = 0; i < m_followups.size(); ++i) {
        if (m_followups[i].nodeId == f.nodeId) {
            m_followups[i] = f;
            return;
        }
    }
    m_followups.append(f);
}

bool LoreGraph::removeFollowup(const QString &nodeId)
{
    for (int i = m_followups.size() - 1; i >= 0; --i) {
        if (m_followups[i].nodeId == nodeId) {
            m_followups.removeAt(i);
            return true;
        }
    }
    return false;
}

void LoreGraph::clear()
{
    m_title = QStringLiteral("Story Lore Map");
    m_direction = QStringLiteral("LR");
    m_nodes.clear();
    m_boxes.clear();
    m_connectors.clear();
    m_followups.clear();
}

void LoreGraph::autoLayout()
{
    if (m_nodes.isEmpty()) return;

    // Arrange nodes on a clean grid if coordinates are not set or overlapping
    const double startX = 60.0;
    const double startY = 80.0;
    const double colSpacing = 240.0;
    const double rowSpacing = 130.0;
    const int cols = qMax(1, static_cast<int>(std::ceil(std::sqrt(m_nodes.size()))));

    for (int i = 0; i < m_nodes.size(); ++i) {
        int row = i / cols;
        int col = i % cols;
        m_nodes[i].x = startX + col * colSpacing;
        m_nodes[i].y = startY + row * rowSpacing;
    }
}

QByteArray LoreGraph::toGrphBytes() const
{
    QByteArray bytes;
    bytes.append(magicBytes());
    bytes.append(toDsl().toUtf8());
    return bytes;
}

bool LoreGraph::fromGrphBytes(const QByteArray &bytes, QString *error)
{
    if (!hasValidMagic(bytes)) {
        if (error) {
            *error = QStringLiteral("Invalid .grph magic header. Expected 8 magic bytes: 0x89 'G' 'R' 'P' 'H' 0x01 '\\r' '\\n'.");
        }
        return false;
    }

    const QString dsl = QString::fromUtf8(bytes.mid(8));
    return fromDsl(dsl, error);
}

QString LoreGraph::toDsl() const
{
    QString dsl;
    dsl += QStringLiteral("# Ensemble Lore Graph Definition (.grph)\n");
    dsl += QStringLiteral("# Format Specification: Ensemble Graph Language V1\n\n");

    // Graph declaration
    dsl += QStringLiteral("graph \"%1\" [direction=%2]\n\n")
               .arg(m_title, m_direction);

    // Nodes
    if (!m_nodes.isEmpty()) {
        dsl += QStringLiteral("# Nodes (Story Blocks)\n");
        for (const auto &n : m_nodes) {
            QString line = QStringLiteral("node %1 [label=\"%2\", type=\"%3\", pos(%4, %5), size(%6, %7), color=\"%8\", shape=%9")
                               .arg(n.id,
                                    n.label,
                                    n.type,
                                    QString::number(n.x),
                                    QString::number(n.y),
                                    QString::number(n.width),
                                    QString::number(n.height),
                                    n.color,
                                    n.shape);
            if (!n.loreId.isEmpty()) {
                line += QStringLiteral(", loreId=\"%1\"").arg(n.loreId);
            }
            if (!n.notes.isEmpty()) {
                line += QStringLiteral(", notes=\"%1\"").arg(n.notes);
            }
            line += QStringLiteral("]\n");
            dsl += line;
        }
        dsl += QStringLiteral("\n");
    }

    // Boxes (Containers)
    if (!m_boxes.isEmpty()) {
        dsl += QStringLiteral("# Boxes (Grouping Containers)\n");
        for (const auto &b : m_boxes) {
            dsl += QStringLiteral("box %1 [label=\"%2\", pos(%3, %4), size(%5, %6), color=\"%7\"] {\n")
                       .arg(b.id,
                            b.label,
                            QString::number(b.x),
                            QString::number(b.y),
                            QString::number(b.width),
                            QString::number(b.height),
                            b.color);
            for (const auto &mem : b.memberNodeIds) {
                dsl += QStringLiteral("    %1\n").arg(mem);
            }
            dsl += QStringLiteral("}\n\n");
        }
    }

    // Connectors
    if (!m_connectors.isEmpty()) {
        dsl += QStringLiteral("# Connectors (Relationships & Narrative Flows)\n");
        for (const auto &c : m_connectors) {
            QString line = QStringLiteral("%1 %2 %3 [id=\"%4\", style=%5, color=\"%6\"")
                               .arg(c.fromNodeId, c.arrowType, c.toNodeId, c.id, c.style, c.color);
            if (!c.label.isEmpty()) {
                line += QStringLiteral(", label=\"%1\"").arg(c.label);
            }
            if (!c.relation.isEmpty()) {
                line += QStringLiteral(", relation=\"%1\"").arg(c.relation);
            }
            line += QStringLiteral("]\n");
            dsl += line;
        }
        dsl += QStringLiteral("\n");
    }

    // Follow-ups
    if (!m_followups.isEmpty()) {
        dsl += QStringLiteral("# Narrative Follow-ups & Foreshadowing Targets\n");
        for (const auto &f : m_followups) {
            dsl += QStringLiteral("followup %1 [targetChapter=%2, status=\"%3\", note=\"%4\"]\n")
                       .arg(f.nodeId, QString::number(f.targetChapter), f.status, f.note);
        }
        dsl += QStringLiteral("\n");
    }

    return dsl;
}

bool LoreGraph::fromDsl(const QString &dsl, QString *error)
{
    QString parsedTitle = QStringLiteral("Untitled Graph");
    QString parsedDirection = QStringLiteral("LR");
    QList<LoreGraphNode> parsedNodes;
    QList<LoreGraphBox> parsedBoxes;
    QList<LoreGraphConnector> parsedConnectors;
    QList<LoreGraphFollowup> parsedFollowups;

    const QStringList lines = dsl.split(QLatin1Char('\n'));
    bool inBox = false;
    LoreGraphBox currentBox;

    static const QSet<QString> validDirections = {
        QStringLiteral("LR"), QStringLiteral("RL"),
        QStringLiteral("TD"), QStringLiteral("TB"), QStringLiteral("BT")
    };

    static const QRegularExpression graphRe(
        QString::fromUtf8(R"raw(^graph\s*(?:"([^"]*)"|'([^']*)'|(\S+))(?:\s*\[(.*)\])?\s*$)raw"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression nodeRe(
        QString::fromUtf8(R"(^node\s+([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression boxStartRe(
        QString::fromUtf8(R"(^box\s+([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*\{\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression followupRe(
        QString::fromUtf8(R"(^followup\s+([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression connRe(
        QString::fromUtf8(R"(^([a-zA-Z0-9_\-\.]+)\s*(->|<->|--|-x)\s*([a-zA-Z0-9_\-\.]+)(?:\s*\[(.*)\])?\s*$)"));

    for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
        const QString line = lines.at(lineNum).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("//"))) {
            continue;
        }

        // Inside a box definition
        if (inBox) {
            if (line == QStringLiteral("}") || line.startsWith(QLatin1Char('}'))) {
                parsedBoxes.append(currentBox);
                inBox = false;
                continue;
            } else {
                QString member = line;
                member.remove(QLatin1Char(';'));
                member = member.trimmed();
                static const QRegularExpression validIdRe(QStringLiteral("^[a-zA-Z0-9_\\-\\.]+$"));
                if (!validIdRe.match(member).hasMatch()) {
                    if (error) {
                        *error = QStringLiteral("Line %1: Invalid box member node ID '%2'.").arg(lineNum + 1).arg(member);
                    }
                    return false;
                }
                currentBox.memberNodeIds.append(member);
                continue;
            }
        }

        // 1. graph "..." [direction=LR]
        if (line.startsWith(QStringLiteral("graph"), Qt::CaseInsensitive)) {
            auto m = graphRe.match(line);
            if (!m.hasMatch()) {
                if (error) {
                    *error = QStringLiteral("Line %1: Malformed graph declaration '%2'.").arg(lineNum + 1).arg(line);
                }
                return false;
            }
            if (!m.captured(1).isEmpty()) parsedTitle = m.captured(1);
            else if (!m.captured(2).isEmpty()) parsedTitle = m.captured(2);
            else if (!m.captured(3).isEmpty()) parsedTitle = m.captured(3);

            if (!m.captured(4).isEmpty()) {
                auto attrs = parseAttributes(m.captured(4));
                if (attrs.contains(QStringLiteral("direction"))) {
                    const QString dir = attrs.value(QStringLiteral("direction")).toUpper();
                    if (!validDirections.contains(dir)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid graph direction '%2'. Allowed values are LR, RL, TD, TB, BT.")
                                         .arg(lineNum + 1).arg(dir);
                        }
                        return false;
                    }
                    parsedDirection = dir;
                }
            }
            continue;
        }

        // 2. node <id> [ ... ]
        if (line.startsWith(QStringLiteral("node "), Qt::CaseInsensitive)) {
            auto m = nodeRe.match(line);
            if (!m.hasMatch()) {
                if (error) {
                    *error = QStringLiteral("Line %1: Malformed node declaration '%2'.").arg(lineNum + 1).arg(line);
                }
                return false;
            }
            LoreGraphNode n;
            n.id = m.captured(1);
            n.label = n.id;
            if (!m.captured(2).isEmpty()) {
                auto attrs = parseAttributes(m.captured(2));
                if (attrs.contains(QStringLiteral("label"))) n.label = attrs.value(QStringLiteral("label"));
                if (attrs.contains(QStringLiteral("type"))) {
                    const QString t = attrs.value(QStringLiteral("type")).toLower();
                    if (!isValidNodeType(t)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid node type '%2'. Valid types are character, location, event, item, faction, concept, plot_point.")
                                         .arg(lineNum + 1).arg(t);
                        }
                        return false;
                    }
                    n.type = t;
                }
                if (attrs.contains(QStringLiteral("shape"))) {
                    const QString s = attrs.value(QStringLiteral("shape")).toLower();
                    if (!isValidNodeShape(s)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid node shape '%2'. Valid shapes are box, rounded, diamond, ellipse, cylinder, hexagon.")
                                         .arg(lineNum + 1).arg(s);
                        }
                        return false;
                    }
                    n.shape = s;
                }
                if (attrs.contains(QStringLiteral("color"))) {
                    const QString col = attrs.value(QStringLiteral("color"));
                    if (!isValidHexOrNamedColor(col)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid node color '%2'. Must be a valid hex color or CSS color name.")
                                         .arg(lineNum + 1).arg(col);
                        }
                        return false;
                    }
                    n.color = col;
                }
                if (attrs.contains(QStringLiteral("loreId"))) n.loreId = attrs.value(QStringLiteral("loreId"));
                if (attrs.contains(QStringLiteral("notes"))) n.notes = attrs.value(QStringLiteral("notes"));
                if (attrs.contains(QStringLiteral("x"))) n.x = attrs.value(QStringLiteral("x")).toDouble();
                if (attrs.contains(QStringLiteral("y"))) n.y = attrs.value(QStringLiteral("y")).toDouble();
                if (attrs.contains(QStringLiteral("w"))) n.width = attrs.value(QStringLiteral("w")).toDouble();
                if (attrs.contains(QStringLiteral("h"))) n.height = attrs.value(QStringLiteral("h")).toDouble();
            }
            bool foundNode = false;
            for (int i = 0; i < parsedNodes.size(); ++i) {
                if (parsedNodes[i].id == n.id) {
                    parsedNodes[i] = n;
                    foundNode = true;
                    break;
                }
            }
            if (!foundNode) {
                parsedNodes.append(n);
            }
            continue;
        }

        // 3. box <id> [ ... ] {
        if (line.startsWith(QStringLiteral("box "), Qt::CaseInsensitive)) {
            auto m = boxStartRe.match(line);
            if (!m.hasMatch()) {
                if (error) {
                    *error = QStringLiteral("Line %1: Malformed box declaration '%2'. Expected 'box <id> [attrs] {'.").arg(lineNum + 1).arg(line);
                }
                return false;
            }
            currentBox = LoreGraphBox();
            currentBox.id = m.captured(1);
            currentBox.label = currentBox.id;
            if (!m.captured(2).isEmpty()) {
                auto attrs = parseAttributes(m.captured(2));
                if (attrs.contains(QStringLiteral("label"))) currentBox.label = attrs.value(QStringLiteral("label"));
                if (attrs.contains(QStringLiteral("color"))) {
                    const QString col = attrs.value(QStringLiteral("color"));
                    if (!isValidHexOrNamedColor(col)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid box color '%2'. Must be a valid hex color or CSS color name.")
                                         .arg(lineNum + 1).arg(col);
                        }
                        return false;
                    }
                    currentBox.color = col;
                }
                if (attrs.contains(QStringLiteral("x"))) currentBox.x = attrs.value(QStringLiteral("x")).toDouble();
                if (attrs.contains(QStringLiteral("y"))) currentBox.y = attrs.value(QStringLiteral("y")).toDouble();
                if (attrs.contains(QStringLiteral("w"))) currentBox.width = attrs.value(QStringLiteral("w")).toDouble();
                if (attrs.contains(QStringLiteral("h"))) currentBox.height = attrs.value(QStringLiteral("h")).toDouble();
            }
            inBox = true;
            continue;
        }

        // 4. followup <id> [ ... ]
        if (line.startsWith(QStringLiteral("followup "), Qt::CaseInsensitive)) {
            auto m = followupRe.match(line);
            if (!m.hasMatch()) {
                if (error) {
                    *error = QStringLiteral("Line %1: Malformed followup declaration '%2'.").arg(lineNum + 1).arg(line);
                }
                return false;
            }
            LoreGraphFollowup f;
            f.nodeId = m.captured(1);
            if (!m.captured(2).isEmpty()) {
                auto attrs = parseAttributes(m.captured(2));
                if (attrs.contains(QStringLiteral("targetChapter"))) f.targetChapter = attrs.value(QStringLiteral("targetChapter")).toInt();
                if (attrs.contains(QStringLiteral("status"))) f.status = attrs.value(QStringLiteral("status"));
                if (attrs.contains(QStringLiteral("note"))) f.note = attrs.value(QStringLiteral("note"));
            }
            parsedFollowups.append(f);
            continue;
        }

        // 5. Connectors: <source> <arrow> <target> [ ... ]
        auto connMatch = connRe.match(line);
        if (connMatch.hasMatch()) {
            LoreGraphConnector c;
            c.fromNodeId = connMatch.captured(1);
            c.arrowType = connMatch.captured(2);
            c.toNodeId = connMatch.captured(3);

            if (!isValidArrowType(c.arrowType)) {
                if (error) {
                    *error = QStringLiteral("Line %1: Invalid connector arrow '%2'. Valid arrows are ->, <->, --, -x.")
                                 .arg(lineNum + 1).arg(c.arrowType);
                }
                return false;
            }

            if (!connMatch.captured(4).isEmpty()) {
                auto attrs = parseAttributes(connMatch.captured(4));
                if (attrs.contains(QStringLiteral("id"))) c.id = attrs.value(QStringLiteral("id"));
                if (attrs.contains(QStringLiteral("label"))) c.label = attrs.value(QStringLiteral("label"));
                if (attrs.contains(QStringLiteral("relation"))) c.relation = attrs.value(QStringLiteral("relation"));
                if (attrs.contains(QStringLiteral("style"))) {
                    const QString st = attrs.value(QStringLiteral("style")).toLower();
                    if (!isValidConnectorStyle(st)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid connector style '%2'. Valid styles are solid, dashed, dotted, thick.")
                                         .arg(lineNum + 1).arg(st);
                        }
                        return false;
                    }
                    c.style = st;
                }
                if (attrs.contains(QStringLiteral("color"))) {
                    const QString col = attrs.value(QStringLiteral("color"));
                    if (!isValidHexOrNamedColor(col)) {
                        if (error) {
                            *error = QStringLiteral("Line %1: Invalid connector color '%2'. Must be a valid hex color or CSS color name.")
                                         .arg(lineNum + 1).arg(col);
                        }
                        return false;
                    }
                    c.color = col;
                }
            }
            if (c.id.isEmpty()) {
                const QString baseId = QStringLiteral("conn_%1_%2").arg(c.fromNodeId, c.toNodeId);
                QString candId = baseId;
                int suffix = 2;
                bool idExists = true;
                while (idExists) {
                    idExists = false;
                    for (const auto &existing : parsedConnectors) {
                        if (existing.id == candId) {
                            idExists = true;
                            candId = QStringLiteral("%1_%2").arg(baseId).arg(suffix++);
                            break;
                        }
                    }
                }
                c.id = candId;
            }
            parsedConnectors.append(c);
            continue;
        }

        // If we reach here, the non-empty, non-comment line did not match any valid directive!
        if (error) {
            *error = QStringLiteral("Line %1: Syntax error: Unrecognized statement '%2'.").arg(lineNum + 1).arg(line);
        }
        return false;
    }

    if (inBox) {
        if (error) {
            *error = QStringLiteral("Syntax error: Unclosed box block '%1' (missing '}').").arg(currentBox.id);
        }
        return false;
    }

    if (parsedNodes.isEmpty() && parsedBoxes.isEmpty() && parsedConnectors.isEmpty() && parsedTitle == QStringLiteral("Untitled Graph")) {
        if (error) {
            *error = QStringLiteral("DSL contains no valid graph declarations or nodes.");
        }
        return false;
    }

    // Semantic validation: Ensure all referenced node IDs exist in parsedNodes
    QSet<QString> declaredNodeIds;
    for (const auto &n : parsedNodes) {
        declaredNodeIds.insert(n.id);
    }

    for (const auto &c : parsedConnectors) {
        if (!declaredNodeIds.contains(c.fromNodeId)) {
            if (error) {
                *error = QStringLiteral("Semantic error: Connector references nonexistent source node '%1'.").arg(c.fromNodeId);
            }
            return false;
        }
        if (!declaredNodeIds.contains(c.toNodeId)) {
            if (error) {
                *error = QStringLiteral("Semantic error: Connector references nonexistent target node '%1'.").arg(c.toNodeId);
            }
            return false;
        }
    }

    for (const auto &b : parsedBoxes) {
        for (const QString &mId : b.memberNodeIds) {
            if (!declaredNodeIds.contains(mId)) {
                if (error) {
                    *error = QStringLiteral("Semantic error: Box '%1' contains nonexistent member node '%2'.").arg(b.id, mId);
                }
                return false;
            }
        }
    }

    for (const auto &f : parsedFollowups) {
        if (!declaredNodeIds.contains(f.nodeId)) {
            if (error) {
                *error = QStringLiteral("Semantic error: Followup references nonexistent node '%1'.").arg(f.nodeId);
            }
            return false;
        }
    }

    // Success! Mutate *this now
    clear();
    m_title = parsedTitle;
    m_direction = parsedDirection;
    m_nodes = parsedNodes;
    m_boxes = parsedBoxes;
    m_connectors = parsedConnectors;
    m_followups = parsedFollowups;

    return true;
}

QString LoreGraph::dslGrammarSpecification()
{
    return QStringLiteral(
        "=== Ensemble Lore Graph (.grph) Language Specification ===\n\n"
        "1. File Structure:\n"
        "   - Magic Bytes Header: 8 bytes (0x89 'G' 'R' 'P' 'H' 0x01 '\\r' '\\n')\n"
        "   - Body: UTF-8 plain text DSL describing graph elements.\n\n"
        "2. Directives:\n"
        "   • Graph Declaration:\n"
        "     graph \"<Title>\" [direction=LR|RL|TD|TB|BT]\n\n"
        "   • Nodes (Story Blocks):\n"
        "     node <id> [label=\"<Name>\", type=\"<type>\", pos(x, y), size(w, h), color=\"#hex\", shape=<shape>, loreId=\"<id>\", notes=\"<Author Notes>\"]\n"
        "     - Supported types: character, location, event, item, faction, concept, plot_point\n"
        "     - Supported shapes: box, rounded, diamond, ellipse, cylinder, hexagon\n"
        "     - pos(x, y): Canvas coordinates allowing manual or programmatic block arrangement.\n\n"
        "   • Boxes (Grouping Containers):\n"
        "     box <id> [label=\"<Group Title>\", pos(x, y), size(w, h), color=\"#hex\"] {\n"
        "         <memberNodeId1>\n"
        "         <memberNodeId2>\n"
        "     }\n\n"
        "   • Connectors (Relationships & Narrative Flows):\n"
        "     <sourceNode> <arrow> <targetNode> [id=\"<connId>\", label=\"<Text>\", relation=\"<type>\", style=solid|dashed|dotted|thick, color=\"#hex\"]\n"
        "     - Arrows:\n"
        "       ->   : Directed cause, flow, or action\n"
        "       <->  : Bidirectional alliance or mutual bond\n"
        "       --   : Undirected association\n"
        "       -x   : Conflict, rivalry, opposing faction\n\n"
        "   • Narrative Follow-ups:\n"
        "     followup <nodeId> [targetChapter=<N>, status=\"pending|in_progress|resolved\", note=\"<details>\"]\n"
    );
}

QJsonObject LoreGraph::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("title")] = m_title;
    obj[QStringLiteral("direction")] = m_direction;
    obj[QStringLiteral("nodeCount")] = m_nodes.size();
    obj[QStringLiteral("boxCount")] = m_boxes.size();
    obj[QStringLiteral("connectorCount")] = m_connectors.size();
    obj[QStringLiteral("followupCount")] = m_followups.size();

    QJsonArray nodeArr;
    for (const auto &n : m_nodes) {
        QJsonObject no;
        no[QStringLiteral("id")] = n.id;
        no[QStringLiteral("label")] = n.label;
        no[QStringLiteral("type")] = n.type;
        no[QStringLiteral("x")] = n.x;
        no[QStringLiteral("y")] = n.y;
        no[QStringLiteral("width")] = n.width;
        no[QStringLiteral("height")] = n.height;
        no[QStringLiteral("shape")] = n.shape;
        no[QStringLiteral("color")] = n.color;
        if (!n.loreId.isEmpty()) no[QStringLiteral("loreId")] = n.loreId;
        if (!n.notes.isEmpty()) no[QStringLiteral("notes")] = n.notes;
        nodeArr.append(no);
    }
    obj[QStringLiteral("nodes")] = nodeArr;

    QJsonArray boxArr;
    for (const auto &b : m_boxes) {
        QJsonObject bo;
        bo[QStringLiteral("id")] = b.id;
        bo[QStringLiteral("label")] = b.label;
        bo[QStringLiteral("x")] = b.x;
        bo[QStringLiteral("y")] = b.y;
        bo[QStringLiteral("width")] = b.width;
        bo[QStringLiteral("height")] = b.height;
        bo[QStringLiteral("color")] = b.color;
        QJsonArray mems;
        for (const auto &m : b.memberNodeIds) mems.append(m);
        bo[QStringLiteral("members")] = mems;
        boxArr.append(bo);
    }
    obj[QStringLiteral("boxes")] = boxArr;

    QJsonArray connArr;
    for (const auto &c : m_connectors) {
        QJsonObject co;
        co[QStringLiteral("id")] = c.id;
        co[QStringLiteral("from")] = c.fromNodeId;
        co[QStringLiteral("to")] = c.toNodeId;
        co[QStringLiteral("arrow")] = c.arrowType;
        co[QStringLiteral("label")] = c.label;
        co[QStringLiteral("relation")] = c.relation;
        co[QStringLiteral("style")] = c.style;
        co[QStringLiteral("color")] = c.color;
        connArr.append(co);
    }
    obj[QStringLiteral("connectors")] = connArr;

    QJsonArray fArr;
    for (const auto &f : m_followups) {
        QJsonObject fo;
        fo[QStringLiteral("nodeId")] = f.nodeId;
        fo[QStringLiteral("targetChapter")] = f.targetChapter;
        fo[QStringLiteral("status")] = f.status;
        fo[QStringLiteral("note")] = f.note;
        fArr.append(fo);
    }
    obj[QStringLiteral("followups")] = fArr;

    return obj;
}
