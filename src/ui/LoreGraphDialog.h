#pragma once

#include "model/Ao3Project.h"
#include "model/LoreGraph.h"
#include "model/LoreManager.h"

#include <QDialog>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

class LoreGraphDialog;

class LoreGraphNodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };

    LoreGraphNodeItem(const LoreGraphNode &node, LoreGraphDialog *dialog, QGraphicsItem *parent = nullptr);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString nodeId() const { return m_node.id; }
    const LoreGraphNode &node() const { return m_node; }
    void updateNodeData(const LoreGraphNode &node);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    LoreGraphNode m_node;
    LoreGraphDialog *m_dialog = nullptr;
};

class LoreGraphConnectorItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    LoreGraphConnectorItem(const LoreGraphConnector &conn,
                           LoreGraphNodeItem *fromItem,
                           LoreGraphNodeItem *toItem,
                           QGraphicsItem *parent = nullptr);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString connectorId() const { return m_connector.id; }
    const LoreGraphConnector &connector() const { return m_connector; }
    void updatePosition();

private:
    LoreGraphConnector m_connector;
    LoreGraphNodeItem *m_fromItem = nullptr;
    LoreGraphNodeItem *m_toItem = nullptr;
    QPointF m_fromPoint;
    QPointF m_toPoint;
};

class LoreGraphBoxItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 3 };

    LoreGraphBoxItem(const LoreGraphBox &box, QGraphicsItem *parent = nullptr);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString boxId() const { return m_box.id; }
    const LoreGraphBox &box() const { return m_box; }

private:
    LoreGraphBox m_box;
};

class LoreGraphDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoreGraphDialog(Ao3Project *project, QWidget *parent = nullptr);
    ~LoreGraphDialog() override = default;

    void setProject(Ao3Project *project);
    void refreshFromProject();
    void refreshAll() { refreshFromProject(); }
    void onNodeMoved(const QString &nodeId, const QPointF &newPos);
    void editNode(const QString &nodeId);

private slots:
    void onAddNodeClicked();
    void onConnectNodesClicked();
    void onDeleteSelectedClicked();
    void onAutoLayoutClicked();
    void onApplyDslClicked();
    void onResetDslClicked();
    void onDslHelpClicked();
    void onTabChanged(int index);

    // Lore Entities slots
    void onAddEntityClicked();
    void onEditEntityClicked();
    void onDeleteEntityClicked();

    // History slots
    void onAddHistoryClicked();
    void onDeleteHistoryClicked();

    // Conflicts & Plans slots
    void onAddConflictClicked();
    void onResolveConflictClicked();
    void onAddPlanClicked();
    void onDeletePlanClicked();

private:
    void setupUi();
    void setupGraphCanvasTab(QWidget *tab);
    void setupDslTab(QWidget *tab);
    void setupLoreDataTab(QWidget *tab);
    void setupConflictsPlansTab(QWidget *tab);

    void populateScene();
    void populateDslEditor();
    void populateLoreEntitiesTable();
    void populateHistoryTable();
    void populateConflictsTable();
    void populatePlansTable();

    Ao3Project *m_project = nullptr;

    QTabWidget *m_tabWidget = nullptr;

    // Visual Graph Canvas
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QMap<QString, LoreGraphNodeItem *> m_nodeItems;
    QList<LoreGraphConnectorItem *> m_connectorItems;
    QList<LoreGraphBoxItem *> m_boxItems;
    QLabel *m_statusLabel = nullptr;

    // DSL Editor
    QPlainTextEdit *m_dslEditor = nullptr;
    QLabel *m_dslStatusLabel = nullptr;

    // Lore Entities & History
    QTableWidget *m_entitiesTable = nullptr;
    QTableWidget *m_historyTable = nullptr;

    // Conflicts & Plans
    QTableWidget *m_conflictsTable = nullptr;
    QTableWidget *m_plansTable = nullptr;
};
