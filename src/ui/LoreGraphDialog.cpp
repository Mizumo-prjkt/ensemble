#include "LoreGraphDialog.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QSplitter>
#include <QStyleOptionGraphicsItem>
#include <QVBoxLayout>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// -----------------------------------------------------------------------------
// LoreGraphNodeItem
// -----------------------------------------------------------------------------

LoreGraphNodeItem::LoreGraphNodeItem(const LoreGraphNode &node, LoreGraphDialog *dialog, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_node(node)
    , m_dialog(dialog)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setPos(node.x, node.y);
    setZValue(10);
}

QRectF LoreGraphNodeItem::boundingRect() const
{
    const qreal w = qMax(120.0, m_node.width);
    const qreal h = qMax(60.0, m_node.height);
    return QRectF(-4, -4, w + 8, h + 8);
}

QPainterPath LoreGraphNodeItem::shape() const
{
    QPainterPath path;
    const qreal w = qMax(120.0, m_node.width);
    const qreal h = qMax(60.0, m_node.height);
    path.addRoundedRect(0, 0, w, h, 8, 8);
    return path;
}

void LoreGraphNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing);

    const qreal w = qMax(120.0, m_node.width);
    const qreal h = qMax(60.0, m_node.height);
    const QRectF rect(0, 0, w, h);

    QColor baseColor(m_node.color.isEmpty() ? QStringLiteral("#3b82f6") : m_node.color);
    if (!baseColor.isValid()) {
        baseColor = QColor(QStringLiteral("#3b82f6"));
    }

    // Shadow
    painter->setBrush(QColor(0, 0, 0, 70));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(rect.translated(3, 3), 8, 8);

    // Node Body
    QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
    QColor topCol = baseColor.lighter(115);
    QColor botCol = baseColor.darker(115);
    grad.setColorAt(0.0, topCol);
    grad.setColorAt(1.0, botCol);

    QPen borderPen;
    if (isSelected()) {
        borderPen = QPen(QColor(QStringLiteral("#38bdf8")), 3, Qt::SolidLine);
    } else {
        borderPen = QPen(baseColor.lighter(140), 1.5, Qt::SolidLine);
    }

    painter->setPen(borderPen);
    painter->setBrush(grad);

    if (m_node.shape == QStringLiteral("diamond")) {
        QPolygonF poly;
        poly << QPointF(w / 2.0, 0) << QPointF(w, h / 2.0)
             << QPointF(w / 2.0, h) << QPointF(0, h / 2.0);
        painter->drawPolygon(poly);
    } else if (m_node.shape == QStringLiteral("ellipse")) {
        painter->drawEllipse(rect);
    } else {
        // default / rounded / box
        const qreal radius = (m_node.shape == QStringLiteral("box")) ? 2.0 : 8.0;
        painter->drawRoundedRect(rect, radius, radius);
    }

    // Type Badge (Header pill)
    QString typeLabel = m_node.type.toUpper();
    if (!typeLabel.isEmpty()) {
        QRectF badgeRect(8, 6, w - 16, 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 90));
        painter->drawRoundedRect(badgeRect, 3, 3);

        painter->setPen(QColor(QStringLiteral("#f1f5f9")));
        QFont badgeFont = painter->font();
        badgeFont.setPointSize(8);
        badgeFont.setBold(true);
        painter->setFont(badgeFont);
        painter->drawText(badgeRect, Qt::AlignCenter, typeLabel);
    }

    // Label Text
    QRectF textRect(6, 26, w - 12, h - 30);
    painter->setPen(Qt::white);
    QFont labelFont = painter->font();
    labelFont.setPointSize(10);
    labelFont.setBold(true);
    painter->setFont(labelFont);
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, m_node.label);

    // Follow-up indicator chip if node has lore link or notes
    if (!m_node.notes.isEmpty() || !m_node.loreId.isEmpty()) {
        QRectF chipRect(w - 18, 4, 14, 14);
        painter->setBrush(QColor(QStringLiteral("#fbbf24")));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(chipRect);
        painter->setPen(Qt::black);
        QFont f = painter->font();
        f.setPointSize(7);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(chipRect, Qt::AlignCenter, QStringLiteral("!"));
    }
}

void LoreGraphNodeItem::updateNodeData(const LoreGraphNode &node)
{
    prepareGeometryChange();
    m_node = node;
    setPos(node.x, node.y);
    update();
}

QVariant LoreGraphNodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && m_dialog) {
        m_dialog->onNodeMoved(m_node.id, pos());
    }
    return QGraphicsItem::itemChange(change, value);
}

void LoreGraphNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);
    if (m_dialog) {
        m_dialog->editNode(m_node.id);
    }
}

// -----------------------------------------------------------------------------
// LoreGraphConnectorItem
// -----------------------------------------------------------------------------

LoreGraphConnectorItem::LoreGraphConnectorItem(const LoreGraphConnector &conn,
                                               LoreGraphNodeItem *fromItem,
                                               LoreGraphNodeItem *toItem,
                                               QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_connector(conn)
    , m_fromItem(fromItem)
    , m_toItem(toItem)
{
    setZValue(5);
    updatePosition();
}

void LoreGraphConnectorItem::updatePosition()
{
    if (!m_fromItem || !m_toItem) return;

    prepareGeometryChange();
    const QRectF fromRect = m_fromItem->boundingRect();
    const QRectF toRect = m_toItem->boundingRect();

    m_fromPoint = m_fromItem->pos() + QPointF(fromRect.width() / 2.0, fromRect.height() / 2.0);
    m_toPoint = m_toItem->pos() + QPointF(toRect.width() / 2.0, toRect.height() / 2.0);
    update();
}

QRectF LoreGraphConnectorItem::boundingRect() const
{
    const qreal extra = 40.0;
    return QRectF(m_fromPoint, m_toPoint).normalized().adjusted(-extra, -extra, extra, extra);
}

void LoreGraphConnectorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (!m_fromItem || !m_toItem) return;

    painter->setRenderHint(QPainter::Antialiasing);

    QColor lineColor(m_connector.color.isEmpty() ? QStringLiteral("#94a3b8") : m_connector.color);
    if (!lineColor.isValid()) lineColor = QColor(QStringLiteral("#94a3b8"));

    Qt::PenStyle penStyle = Qt::SolidLine;
    if (m_connector.style == QStringLiteral("dashed")) {
        penStyle = Qt::DashLine;
    } else if (m_connector.style == QStringLiteral("dotted")) {
        penStyle = Qt::DotLine;
    }

    qreal penWidth = 2.0;
    if (m_connector.arrowType == QStringLiteral("-x")) {
        lineColor = QColor(QStringLiteral("#ef4444")); // Red conflict
        penStyle = Qt::DashLine;
        penWidth = 2.5;
    }

    QPen pen(lineColor, penWidth, penStyle, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);

    // Draw main connector line
    painter->drawLine(m_fromPoint, m_toPoint);

    // Arrow calculations
    const qreal dx = m_toPoint.x() - m_fromPoint.x();
    const qreal dy = m_toPoint.y() - m_fromPoint.y();
    const qreal angle = std::atan2(dy, dx);
    const qreal arrowSize = 12.0;

    // Target arrow
    if (m_connector.arrowType == QStringLiteral("->") || m_connector.arrowType == QStringLiteral("<->")) {
        painter->setBrush(lineColor);
        painter->setPen(lineColor);
        QPointF p1 = m_toPoint - QPointF(std::cos(angle - M_PI / 6.0) * arrowSize, std::sin(angle - M_PI / 6.0) * arrowSize);
        QPointF p2 = m_toPoint - QPointF(std::cos(angle + M_PI / 6.0) * arrowSize, std::sin(angle + M_PI / 6.0) * arrowSize);
        QPolygonF arrowHead;
        arrowHead << m_toPoint << p1 << p2;
        painter->drawPolygon(arrowHead);
    }

    // Source arrow (for <->)
    if (m_connector.arrowType == QStringLiteral("<->")) {
        painter->setBrush(lineColor);
        painter->setPen(lineColor);
        QPointF p1 = m_fromPoint + QPointF(std::cos(angle - M_PI / 6.0) * arrowSize, std::sin(angle - M_PI / 6.0) * arrowSize);
        QPointF p2 = m_fromPoint + QPointF(std::cos(angle + M_PI / 6.0) * arrowSize, std::sin(angle + M_PI / 6.0) * arrowSize);
        QPolygonF arrowHead;
        arrowHead << m_fromPoint << p1 << p2;
        painter->drawPolygon(arrowHead);
    }

    // Conflict marker (-x)
    if (m_connector.arrowType == QStringLiteral("-x")) {
        const QPointF mid = (m_fromPoint + m_toPoint) / 2.0;
        painter->setPen(QPen(QColor(QStringLiteral("#ef4444")), 3.0));
        painter->drawLine(mid + QPointF(-8, -8), mid + QPointF(8, 8));
        painter->drawLine(mid + QPointF(-8, 8), mid + QPointF(8, -8));
    }

    // Relation / Label Pill
    const QString text = m_connector.label.isEmpty() ? m_connector.relation : m_connector.label;
    if (!text.isEmpty()) {
        const QPointF mid = (m_fromPoint + m_toPoint) / 2.0;
        QFont font = painter->font();
        font.setPointSize(8);
        font.setBold(true);
        painter->setFont(font);

        QFontMetrics fm(font);
        const int textW = fm.horizontalAdvance(text) + 12;
        const int textH = fm.height() + 6;
        const QRectF pillRect(mid.x() - textW / 2.0, mid.y() - textH / 2.0, textW, textH);

        painter->setPen(QPen(lineColor, 1.0));
        painter->setBrush(QColor(QStringLiteral("#1e293b")));
        painter->drawRoundedRect(pillRect, 4, 4);

        painter->setPen(QColor(QStringLiteral("#e2e8f0")));
        painter->drawText(pillRect, Qt::AlignCenter, text);
    }
}

// -----------------------------------------------------------------------------
// LoreGraphBoxItem
// -----------------------------------------------------------------------------

LoreGraphBoxItem::LoreGraphBoxItem(const LoreGraphBox &box, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_box(box)
{
    setZValue(1); // Drawn behind nodes
    setPos(box.x, box.y);
}

QRectF LoreGraphBoxItem::boundingRect() const
{
    return QRectF(0, 0, m_box.width, m_box.height);
}

void LoreGraphBoxItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing);

    QColor col(m_box.color.isEmpty() ? QStringLiteral("#6366f1") : m_box.color);
    if (!col.isValid()) col = QColor(QStringLiteral("#6366f1"));

    QRectF rect(0, 0, m_box.width, m_box.height);

    // Translucent container fill
    QColor fill = col;
    fill.setAlpha(30);
    painter->setBrush(fill);
    painter->setPen(QPen(col, 1.5, Qt::DashLine));
    painter->drawRoundedRect(rect, 8, 8);

    // Box Header Label
    if (!m_box.label.isEmpty()) {
        QRectF headerRect(8, 8, m_box.width - 16, 22);
        painter->setBrush(col);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(headerRect, 4, 4);

        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPointSize(9);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(headerRect, Qt::AlignCenter, m_box.label);
    }
}

// -----------------------------------------------------------------------------
// LoreGraphDialog
// -----------------------------------------------------------------------------

LoreGraphDialog::LoreGraphDialog(Ao3Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(QStringLiteral("Lore Graph & Narrative Map"));
    resize(1080, 720);
    setupUi();
    refreshFromProject();
}

void LoreGraphDialog::setProject(Ao3Project *project)
{
    m_project = project;
    refreshFromProject();
}

void LoreGraphDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    m_tabWidget = new QTabWidget(this);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &LoreGraphDialog::onTabChanged);

    // Tab 1: Visual Graph Canvas
    auto *canvasTab = new QWidget(this);
    setupGraphCanvasTab(canvasTab);
    m_tabWidget->addTab(canvasTab, QStringLiteral("🗺️ Visual Lore Graph"));

    // Tab 2: .grph DSL Editor
    auto *dslTab = new QWidget(this);
    setupDslTab(dslTab);
    m_tabWidget->addTab(dslTab, QStringLiteral("📝 .grph DSL Source"));

    // Tab 3: Lore Entities && History
    auto *loreDataTab = new QWidget(this);
    setupLoreDataTab(loreDataTab);
    m_tabWidget->addTab(loreDataTab, QStringLiteral("📚 Lore Entities && History"));

    // Tab 4: Conflicts && Plans
    auto *conflictsTab = new QWidget(this);
    setupConflictsPlansTab(conflictsTab);
    m_tabWidget->addTab(conflictsTab, QStringLiteral("⚔️ Conflicts && Narrative Plans"));

    mainLayout->addWidget(m_tabWidget);

    // Bottom dialog buttons
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    mainLayout->addWidget(btnBox);
}

void LoreGraphDialog::setupGraphCanvasTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(4, 4, 4, 4);

    // Toolbar
    auto *tb = new QToolBar(this);
    tb->setStyleSheet(QStringLiteral("QToolBar { spacing: 6px; }"));

    auto *addNodeBtn = tb->addAction(QStringLiteral("➕ Add Node"));
    connect(addNodeBtn, &QAction::triggered, this, &LoreGraphDialog::onAddNodeClicked);

    auto *connectBtn = tb->addAction(QStringLiteral("🔗 Connect Nodes"));
    connect(connectBtn, &QAction::triggered, this, &LoreGraphDialog::onConnectNodesClicked);

    auto *deleteBtn = tb->addAction(QStringLiteral("🗑️ Delete Selected"));
    connect(deleteBtn, &QAction::triggered, this, &LoreGraphDialog::onDeleteSelectedClicked);

    tb->addSeparator();

    auto *autoLayoutBtn = tb->addAction(QStringLiteral("🪄 Auto-Layout"));
    connect(autoLayoutBtn, &QAction::triggered, this, &LoreGraphDialog::onAutoLayoutClicked);

    tb->addSeparator();

    auto *zoomInBtn = tb->addAction(QStringLiteral("🔍+ Zoom In"));
    connect(zoomInBtn, &QAction::triggered, this, [this]() {
        if (m_view) m_view->scale(1.2, 1.2);
    });

    auto *zoomOutBtn = tb->addAction(QStringLiteral("🔍- Zoom Out"));
    connect(zoomOutBtn, &QAction::triggered, this, [this]() {
        if (m_view) m_view->scale(0.833, 0.833);
    });

    auto *resetZoomBtn = tb->addAction(QStringLiteral("🔄 Reset Zoom"));
    connect(resetZoomBtn, &QAction::triggered, this, [this]() {
        if (m_view) m_view->resetTransform();
    });

    layout->addWidget(tb);

    // Canvas scene and view
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-1000, -1000, 3000, 3000);
    m_scene->setBackgroundBrush(QColor(QStringLiteral("#0f172a"))); // Slate dark canvas

    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    m_view->setDragMode(QGraphicsView::RubberBandDrag);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    layout->addWidget(m_view);

    m_statusLabel = new QLabel(QStringLiteral("Tip: Drag nodes to position them. Double-click a node to edit. Coordinates sync live to .ao3proj."), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px;"));
    layout->addWidget(m_statusLabel);
}

void LoreGraphDialog::setupDslTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *topRow = new QHBoxLayout();
    auto *applyBtn = new QPushButton(QStringLiteral("💾 Apply Changes to Graph"), this);
    applyBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #3b82f6; color: white; font-weight: bold; padding: 6px 14px; border-radius: 4px; } QPushButton:hover { background-color: #2563eb; }"));
    connect(applyBtn, &QPushButton::clicked, this, &LoreGraphDialog::onApplyDslClicked);
    topRow->addWidget(applyBtn);

    auto *resetBtn = new QPushButton(QStringLiteral("🔄 Reload from Current Graph"), this);
    connect(resetBtn, &QPushButton::clicked, this, &LoreGraphDialog::onResetDslClicked);
    topRow->addWidget(resetBtn);

    auto *helpBtn = new QPushButton(QStringLiteral("❓ .grph Language Specification"), this);
    connect(helpBtn, &QPushButton::clicked, this, &LoreGraphDialog::onDslHelpClicked);
    topRow->addWidget(helpBtn);

    topRow->addStretch();
    layout->addLayout(topRow);

    m_dslEditor = new QPlainTextEdit(this);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    m_dslEditor->setFont(monoFont);
    m_dslEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_dslEditor->setStyleSheet(QStringLiteral("QPlainTextEdit { background-color: #0b0f19; color: #e2e8f0; border: 1px solid #1e293b; padding: 8px; }"));
    layout->addWidget(m_dslEditor);

    m_dslStatusLabel = new QLabel(QStringLiteral("Editing .grph DSL. Unique Magic: \\x89GRPH\\x01\\r\\n"), this);
    m_dslStatusLabel->setStyleSheet(QStringLiteral("color: #38bdf8; font-size: 11px;"));
    layout->addWidget(m_dslStatusLabel);
}

void LoreGraphDialog::setupLoreDataTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    // Entities Panel (lore/lore_data.json)
    auto *entWidget = new QWidget(this);
    auto *entLayout = new QVBoxLayout(entWidget);
    entLayout->setContentsMargins(0, 0, 0, 0);

    auto *entHeader = new QHBoxLayout();
    entHeader->addWidget(new QLabel(QStringLiteral("<b>Lore Entities (lore/lore_data.json)</b>"), this));
    entHeader->addStretch();

    auto *addEntBtn = new QPushButton(QStringLiteral("➕ Add Entity"), this);
    connect(addEntBtn, &QPushButton::clicked, this, &LoreGraphDialog::onAddEntityClicked);
    entHeader->addWidget(addEntBtn);

    auto *editEntBtn = new QPushButton(QStringLiteral("✏️ Edit Selected"), this);
    connect(editEntBtn, &QPushButton::clicked, this, &LoreGraphDialog::onEditEntityClicked);
    entHeader->addWidget(editEntBtn);

    auto *delEntBtn = new QPushButton(QStringLiteral("🗑️ Delete Selected"), this);
    connect(delEntBtn, &QPushButton::clicked, this, &LoreGraphDialog::onDeleteEntityClicked);
    entHeader->addWidget(delEntBtn);

    entLayout->addLayout(entHeader);

    m_entitiesTable = new QTableWidget(this);
    m_entitiesTable->setColumnCount(5);
    m_entitiesTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Name"), QStringLiteral("Category"), QStringLiteral("Tags"), QStringLiteral("Description")});
    m_entitiesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_entitiesTable->horizontalHeader()->setStretchLastSection(true);
    m_entitiesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entitiesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    entLayout->addWidget(m_entitiesTable);

    splitter->addWidget(entWidget);

    // History Panel (lore/history.json)
    auto *histWidget = new QWidget(this);
    auto *histLayout = new QVBoxLayout(histWidget);
    histLayout->setContentsMargins(0, 0, 0, 0);

    auto *histHeader = new QHBoxLayout();
    histHeader->addWidget(new QLabel(QStringLiteral("<b>Backstory & Timeline Events (lore/history.json)</b>"), this));
    histHeader->addStretch();

    auto *addHistBtn = new QPushButton(QStringLiteral("➕ Add Timeline Event"), this);
    connect(addHistBtn, &QPushButton::clicked, this, &LoreGraphDialog::onAddHistoryClicked);
    histHeader->addWidget(addHistBtn);

    auto *delHistBtn = new QPushButton(QStringLiteral("🗑️ Delete Selected"), this);
    connect(delHistBtn, &QPushButton::clicked, this, &LoreGraphDialog::onDeleteHistoryClicked);
    histHeader->addWidget(delHistBtn);

    histLayout->addLayout(histHeader);

    m_historyTable = new QTableWidget(this);
    m_historyTable->setColumnCount(5);
    m_historyTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Date / Era"), QStringLiteral("Title"), QStringLiteral("Story Chapter"), QStringLiteral("Summary")});
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_historyTable->horizontalHeader()->setStretchLastSection(true);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    histLayout->addWidget(m_historyTable);

    splitter->addWidget(histWidget);

    layout->addWidget(splitter);
}

void LoreGraphDialog::setupConflictsPlansTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    // Conflicts Panel (lore/conflicts.json & lore/resolved.json)
    auto *confWidget = new QWidget(this);
    auto *confLayout = new QVBoxLayout(confWidget);
    confLayout->setContentsMargins(0, 0, 0, 0);

    auto *confHeader = new QHBoxLayout();
    confHeader->addWidget(new QLabel(QStringLiteral("<b>Narrative Conflicts & Plot Discrepancies (lore/conflicts.json)</b>"), this));
    confHeader->addStretch();

    auto *addConfBtn = new QPushButton(QStringLiteral("➕ Add Conflict"), this);
    connect(addConfBtn, &QPushButton::clicked, this, &LoreGraphDialog::onAddConflictClicked);
    confHeader->addWidget(addConfBtn);

    auto *resolveConfBtn = new QPushButton(QStringLiteral("✅ Resolve Selected Conflict"), this);
    resolveConfBtn->setStyleSheet(QStringLiteral("font-weight: bold; color: #10b981;"));
    connect(resolveConfBtn, &QPushButton::clicked, this, &LoreGraphDialog::onResolveConflictClicked);
    confHeader->addWidget(resolveConfBtn);

    confLayout->addLayout(confHeader);

    m_conflictsTable = new QTableWidget(this);
    m_conflictsTable->setColumnCount(6);
    m_conflictsTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Title"), QStringLiteral("Severity"), QStringLiteral("Status"), QStringLiteral("Chapters"), QStringLiteral("Description")});
    m_conflictsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_conflictsTable->horizontalHeader()->setStretchLastSection(true);
    m_conflictsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conflictsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    confLayout->addWidget(m_conflictsTable);

    splitter->addWidget(confWidget);

    // Plans Panel (lore/plans.json)
    auto *planWidget = new QWidget(this);
    auto *planLayout = new QVBoxLayout(planWidget);
    planLayout->setContentsMargins(0, 0, 0, 0);

    auto *planHeader = new QHBoxLayout();
    planHeader->addWidget(new QLabel(QStringLiteral("<b>Narrative Plans & Lore Follow-ups (lore/plans.json)</b>"), this));
    planHeader->addStretch();

    auto *addPlanBtn = new QPushButton(QStringLiteral("➕ Add Narrative Plan"), this);
    connect(addPlanBtn, &QPushButton::clicked, this, &LoreGraphDialog::onAddPlanClicked);
    planHeader->addWidget(addPlanBtn);

    auto *delPlanBtn = new QPushButton(QStringLiteral("🗑️ Delete Selected"), this);
    connect(delPlanBtn, &QPushButton::clicked, this, &LoreGraphDialog::onDeletePlanClicked);
    planHeader->addWidget(delPlanBtn);

    planLayout->addLayout(planHeader);

    m_plansTable = new QTableWidget(this);
    m_plansTable->setColumnCount(5);
    m_plansTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Target Chapter"), QStringLiteral("Status"), QStringLiteral("Title"), QStringLiteral("Description")});
    m_plansTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_plansTable->horizontalHeader()->setStretchLastSection(true);
    m_plansTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_plansTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    planLayout->addWidget(m_plansTable);

    splitter->addWidget(planWidget);

    layout->addWidget(splitter);
}

void LoreGraphDialog::refreshFromProject()
{
    if (!m_project) return;
    m_project->lore().ensureDefaults();

    populateScene();
    populateDslEditor();
    populateLoreEntitiesTable();
    populateHistoryTable();
    populateConflictsTable();
    populatePlansTable();
}

void LoreGraphDialog::populateScene()
{
    if (!m_project || !m_scene) return;

    m_scene->clear();
    m_nodeItems.clear();
    m_connectorItems.clear();
    m_boxItems.clear();

    const LoreGraph &graph = m_project->lore().defaultGraph();

    // 1. Boxes (drawn lowest)
    for (const LoreGraphBox &box : graph.boxes()) {
        auto *boxItem = new LoreGraphBoxItem(box);
        m_scene->addItem(boxItem);
        m_boxItems.append(boxItem);
    }

    // 2. Nodes
    for (const LoreGraphNode &node : graph.nodes()) {
        auto *nodeItem = new LoreGraphNodeItem(node, this);
        m_scene->addItem(nodeItem);
        m_nodeItems.insert(node.id, nodeItem);
    }

    // 3. Connectors
    for (const LoreGraphConnector &conn : graph.connectors()) {
        LoreGraphNodeItem *fromItem = m_nodeItems.value(conn.fromNodeId, nullptr);
        LoreGraphNodeItem *toItem = m_nodeItems.value(conn.toNodeId, nullptr);
        if (fromItem && toItem) {
            auto *connItem = new LoreGraphConnectorItem(conn, fromItem, toItem);
            m_scene->addItem(connItem);
            m_connectorItems.append(connItem);
        }
    }
}

void LoreGraphDialog::populateDslEditor()
{
    if (!m_project || !m_dslEditor) return;
    const LoreGraph &graph = m_project->lore().defaultGraph();
    m_dslEditor->setPlainText(graph.toDsl());
}

void LoreGraphDialog::populateLoreEntitiesTable()
{
    if (!m_project || !m_entitiesTable) return;
    const auto &entities = m_project->lore().entities();

    m_entitiesTable->setRowCount(entities.size());
    for (int i = 0; i < entities.size(); ++i) {
        const auto &e = entities[i];
        m_entitiesTable->setItem(i, 0, new QTableWidgetItem(e.id));
        m_entitiesTable->setItem(i, 1, new QTableWidgetItem(e.name));
        m_entitiesTable->setItem(i, 2, new QTableWidgetItem(e.category));
        m_entitiesTable->setItem(i, 3, new QTableWidgetItem(e.tags.join(QStringLiteral(", "))));
        m_entitiesTable->setItem(i, 4, new QTableWidgetItem(e.description));
    }
}

void LoreGraphDialog::populateHistoryTable()
{
    if (!m_project || !m_historyTable) return;
    const auto &history = m_project->lore().historyEvents();

    m_historyTable->setRowCount(history.size());
    for (int i = 0; i < history.size(); ++i) {
        const auto &h = history[i];
        m_historyTable->setItem(i, 0, new QTableWidgetItem(h.id));
        m_historyTable->setItem(i, 1, new QTableWidgetItem(h.eraOrDate));
        m_historyTable->setItem(i, 2, new QTableWidgetItem(h.title));
        m_historyTable->setItem(i, 3, new QTableWidgetItem(h.storyChapter >= 0 ? QString::number(h.storyChapter) : QStringLiteral("Pre-canon")));
        m_historyTable->setItem(i, 4, new QTableWidgetItem(h.summary));
    }
}

void LoreGraphDialog::populateConflictsTable()
{
    if (!m_project || !m_conflictsTable) return;
    const auto &conflicts = m_project->lore().conflicts();

    m_conflictsTable->setRowCount(conflicts.size());
    for (int i = 0; i < conflicts.size(); ++i) {
        const auto &c = conflicts[i];
        m_conflictsTable->setItem(i, 0, new QTableWidgetItem(c.id));
        m_conflictsTable->setItem(i, 1, new QTableWidgetItem(c.title));
        m_conflictsTable->setItem(i, 2, new QTableWidgetItem(c.severity));
        m_conflictsTable->setItem(i, 3, new QTableWidgetItem(c.status));
        m_conflictsTable->setItem(i, 4, new QTableWidgetItem(QStringLiteral("Ch %1 vs Ch %2").arg(c.chapterA).arg(c.chapterB)));
        m_conflictsTable->setItem(i, 5, new QTableWidgetItem(c.description));
    }
}

void LoreGraphDialog::populatePlansTable()
{
    if (!m_project || !m_plansTable) return;
    const auto &plans = m_project->lore().plans();

    m_plansTable->setRowCount(plans.size());
    for (int i = 0; i < plans.size(); ++i) {
        const auto &p = plans[i];
        m_plansTable->setItem(i, 0, new QTableWidgetItem(p.id));
        m_plansTable->setItem(i, 1, new QTableWidgetItem(QStringLiteral("Ch %1").arg(p.targetChapter)));
        m_plansTable->setItem(i, 2, new QTableWidgetItem(p.status));
        m_plansTable->setItem(i, 3, new QTableWidgetItem(p.title));
        m_plansTable->setItem(i, 4, new QTableWidgetItem(p.description));
    }
}

void LoreGraphDialog::onNodeMoved(const QString &nodeId, const QPointF &newPos)
{
    if (!m_project) return;
    m_project->lore().defaultGraph().updateNodePosition(nodeId, newPos.x(), newPos.y());
    m_project->markDirtyBy(QStringLiteral("user"));

    // Update connector visual geometries
    for (auto *connItem : m_connectorItems) {
        if (connItem && (connItem->connector().fromNodeId == nodeId || connItem->connector().toNodeId == nodeId)) {
            connItem->updatePosition();
        }
    }
}

void LoreGraphDialog::editNode(const QString &nodeId)
{
    if (!m_project) return;
    LoreGraphNode *node = m_project->lore().defaultGraph().findNode(nodeId);
    if (!node) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Lore Node: %1").arg(nodeId));
    auto *form = new QFormLayout(&dlg);

    auto *labelEdit = new QLineEdit(node->label, &dlg);
    form->addRow(QStringLiteral("Label:"), labelEdit);

    auto *typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({QStringLiteral("character"), QStringLiteral("location"), QStringLiteral("event"),
                         QStringLiteral("item"), QStringLiteral("faction"), QStringLiteral("concept"), QStringLiteral("plot_point")});
    typeCombo->setCurrentText(node->type);
    form->addRow(QStringLiteral("Type:"), typeCombo);

    auto *shapeCombo = new QComboBox(&dlg);
    shapeCombo->addItems({QStringLiteral("rounded"), QStringLiteral("box"), QStringLiteral("diamond"),
                          QStringLiteral("ellipse"), QStringLiteral("cylinder"), QStringLiteral("hexagon")});
    shapeCombo->setCurrentText(node->shape);
    form->addRow(QStringLiteral("Shape:"), shapeCombo);

    auto *colorEdit = new QLineEdit(node->color, &dlg);
    form->addRow(QStringLiteral("Color Hex:"), colorEdit);

    auto *loreIdEdit = new QLineEdit(node->loreId, &dlg);
    form->addRow(QStringLiteral("Lore Entity Link ID:"), loreIdEdit);

    auto *notesEdit = new QLineEdit(node->notes, &dlg);
    form->addRow(QStringLiteral("Notes:"), notesEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        node->label = labelEdit->text().trimmed();
        node->type = typeCombo->currentText();
        node->shape = shapeCombo->currentText();
        node->color = colorEdit->text().trimmed();
        node->loreId = loreIdEdit->text().trimmed();
        node->notes = notesEdit->text().trimmed();
        m_project->markDirtyBy(QStringLiteral("user"));

        if (auto *item = m_nodeItems.value(nodeId, nullptr)) {
            item->updateNodeData(*node);
        }
    }
}

void LoreGraphDialog::onAddNodeClicked()
{
    if (!m_project) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add New Lore Node"));
    auto *form = new QFormLayout(&dlg);

    auto *idEdit = new QLineEdit(&dlg);
    idEdit->setPlaceholderText(QStringLiteral("e.g. secret_society"));
    form->addRow(QStringLiteral("Node ID:"), idEdit);

    auto *labelEdit = new QLineEdit(&dlg);
    labelEdit->setPlaceholderText(QStringLiteral("e.g. Order of the Crimson Sun"));
    form->addRow(QStringLiteral("Display Label:"), labelEdit);

    auto *typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({QStringLiteral("character"), QStringLiteral("location"), QStringLiteral("event"),
                         QStringLiteral("item"), QStringLiteral("faction"), QStringLiteral("concept"), QStringLiteral("plot_point")});
    form->addRow(QStringLiteral("Type:"), typeCombo);

    auto *shapeCombo = new QComboBox(&dlg);
    shapeCombo->addItems({QStringLiteral("rounded"), QStringLiteral("box"), QStringLiteral("diamond"),
                          QStringLiteral("ellipse"), QStringLiteral("cylinder"), QStringLiteral("hexagon")});
    form->addRow(QStringLiteral("Shape:"), shapeCombo);

    auto *colorEdit = new QLineEdit(QStringLiteral("#3b82f6"), &dlg);
    form->addRow(QStringLiteral("Color Hex:"), colorEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        QString id = idEdit->text().trimmed();
        if (id.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Invalid ID"), QStringLiteral("Node ID cannot be empty."));
            return;
        }

        LoreGraphNode node;
        node.id = id;
        node.label = labelEdit->text().trimmed().isEmpty() ? id : labelEdit->text().trimmed();
        node.type = typeCombo->currentText();
        node.shape = shapeCombo->currentText();
        node.color = colorEdit->text().trimmed();
        node.x = 200.0 + (m_nodeItems.size() * 40.0);
        node.y = 200.0;

        m_project->lore().defaultGraph().addOrUpdateNode(node);
        m_project->markDirtyBy(QStringLiteral("user"));

        populateScene();
        populateDslEditor();
    }
}

void LoreGraphDialog::onConnectNodesClicked()
{
    if (!m_project) return;
    const auto &nodes = m_project->lore().defaultGraph().nodes();
    if (nodes.size() < 2) {
        QMessageBox::information(this, QStringLiteral("Connect Nodes"), QStringLiteral("At least 2 nodes are required to form a connection."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Connect Lore Nodes"));
    auto *form = new QFormLayout(&dlg);

    auto *fromCombo = new QComboBox(&dlg);
    auto *toCombo = new QComboBox(&dlg);
    for (const auto &n : nodes) {
        fromCombo->addItem(QStringLiteral("%1 (%2)").arg(n.label, n.id), n.id);
        toCombo->addItem(QStringLiteral("%1 (%2)").arg(n.label, n.id), n.id);
    }
    if (toCombo->count() > 1) toCombo->setCurrentIndex(1);

    form->addRow(QStringLiteral("From Node:"), fromCombo);
    form->addRow(QStringLiteral("To Node:"), toCombo);

    auto *arrowCombo = new QComboBox(&dlg);
    arrowCombo->addItem(QStringLiteral("-> (Directed Relationship)"), QStringLiteral("->"));
    arrowCombo->addItem(QStringLiteral("<-> (Bidirectional)"), QStringLiteral("<->"));
    arrowCombo->addItem(QStringLiteral("-- (Undirected Link)"), QStringLiteral("--"));
    arrowCombo->addItem(QStringLiteral("-x (Conflict / Antagonism)"), QStringLiteral("-x"));
    form->addRow(QStringLiteral("Arrow Style:"), arrowCombo);

    auto *labelEdit = new QLineEdit(&dlg);
    labelEdit->setPlaceholderText(QStringLiteral("e.g. ally of, causes, located in, opposes"));
    form->addRow(QStringLiteral("Relationship Label:"), labelEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        const QString fromId = fromCombo->currentData().toString();
        const QString toId = toCombo->currentData().toString();
        const QString arrow = arrowCombo->currentData().toString();
        const QString label = labelEdit->text().trimmed();

        m_project->lore().defaultGraph().connectNodes(fromId, toId, arrow, label);
        m_project->markDirtyBy(QStringLiteral("user"));

        populateScene();
        populateDslEditor();
    }
}

void LoreGraphDialog::onDeleteSelectedClicked()
{
    if (!m_project || !m_scene) return;
    const auto selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Delete"), QStringLiteral("Please select a node or item to delete."));
        return;
    }

    for (auto *item : selected) {
        if (auto *nodeItem = dynamic_cast<LoreGraphNodeItem *>(item)) {
            m_project->lore().defaultGraph().removeNode(nodeItem->nodeId());
        }
    }

    m_project->markDirtyBy(QStringLiteral("user"));
    populateScene();
    populateDslEditor();
}

void LoreGraphDialog::onAutoLayoutClicked()
{
    if (!m_project) return;
    m_project->lore().defaultGraph().autoLayout();
    m_project->markDirtyBy(QStringLiteral("user"));
    populateScene();
    populateDslEditor();
}

void LoreGraphDialog::onApplyDslClicked()
{
    if (!m_project || !m_dslEditor) return;
    const QString dsl = m_dslEditor->toPlainText();

    QString err;
    if (!m_project->lore().defaultGraph().fromDsl(dsl, &err)) {
        m_dslStatusLabel->setText(QStringLiteral("❌ Parse Error: %1").arg(err));
        m_dslStatusLabel->setStyleSheet(QStringLiteral("color: #ef4444; font-weight: bold;"));
        QMessageBox::warning(this, QStringLiteral(".grph Syntax Error"), QStringLiteral("Failed to parse .grph DSL:\n\n%1").arg(err));
        return;
    }

    m_dslStatusLabel->setText(QStringLiteral("✓ Applied successfully. Nodes: %1, Connectors: %2")
                                  .arg(m_project->lore().defaultGraph().nodes().size())
                                  .arg(m_project->lore().defaultGraph().connectors().size()));
    m_dslStatusLabel->setStyleSheet(QStringLiteral("color: #10b981; font-weight: bold;"));
    m_project->markDirtyBy(QStringLiteral("user"));

    populateScene();
}

void LoreGraphDialog::onResetDslClicked()
{
    populateDslEditor();
    m_dslStatusLabel->setText(QStringLiteral("Reloaded DSL from active graph model."));
    m_dslStatusLabel->setStyleSheet(QStringLiteral("color: #38bdf8;"));
}

void LoreGraphDialog::onDslHelpClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral(".grph Language Specification & MCP Grammar"));
    dlg.resize(700, 500);

    auto *layout = new QVBoxLayout(&dlg);
    auto *textEdit = new QPlainTextEdit(&dlg);
    textEdit->setReadOnly(true);
    QFont f(QStringLiteral("monospace"));
    f.setPointSize(9);
    textEdit->setFont(f);
    textEdit->setPlainText(LoreGraph::dslGrammarSpecification());
    layout->addWidget(textEdit);

    auto *btn = new QPushButton(QStringLiteral("Close"), &dlg);
    connect(btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(btn);

    dlg.exec();
}

void LoreGraphDialog::onTabChanged(int index)
{
    if (index == 0) {
        // Switching to visual canvas: ensure populated
        populateScene();
    } else if (index == 1) {
        // Switching to DSL tab: sync text
        populateDslEditor();
    }
}

// -----------------------------------------------------------------------------
// Lore Entities & History Slots
// -----------------------------------------------------------------------------

void LoreGraphDialog::onAddEntityClicked()
{
    if (!m_project) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add Lore Entity (lore/lore_data.json)"));
    auto *form = new QFormLayout(&dlg);

    auto *idEdit = new QLineEdit(&dlg);
    idEdit->setPlaceholderText(QStringLiteral("e.g. king_alden"));
    form->addRow(QStringLiteral("ID:"), idEdit);

    auto *nameEdit = new QLineEdit(&dlg);
    form->addRow(QStringLiteral("Name:"), nameEdit);

    auto *catCombo = new QComboBox(&dlg);
    catCombo->addItems({QStringLiteral("Character"), QStringLiteral("Location"), QStringLiteral("Faction"),
                        QStringLiteral("Artifact"), QStringLiteral("Concept"), QStringLiteral("Rule"), QStringLiteral("Fact")});
    form->addRow(QStringLiteral("Category:"), catCombo);

    auto *tagsEdit = new QLineEdit(&dlg);
    tagsEdit->setPlaceholderText(QStringLiteral("e.g. royalty, deceased, canon"));
    form->addRow(QStringLiteral("Tags (comma separated):"), tagsEdit);

    auto *descEdit = new QPlainTextEdit(&dlg);
    form->addRow(QStringLiteral("Description:"), descEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        QString id = idEdit->text().trimmed();
        if (id.isEmpty()) return;

        LoreEntity ent;
        ent.id = id;
        ent.name = nameEdit->text().trimmed().isEmpty() ? id : nameEdit->text().trimmed();
        ent.category = catCombo->currentText();
        for (const QString &t : tagsEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            ent.tags.append(t.trimmed());
        }
        ent.description = descEdit->toPlainText().trimmed();
        ent.created = QDateTime::currentDateTimeUtc();
        ent.modified = ent.created;

        m_project->lore().addOrUpdateEntity(ent);
        m_project->markDirtyBy(QStringLiteral("user"));
        populateLoreEntitiesTable();
    }
}

void LoreGraphDialog::onEditEntityClicked()
{
    if (!m_project || !m_entitiesTable) return;
    int row = m_entitiesTable->currentRow();
    if (row < 0) return;

    QString id = m_entitiesTable->item(row, 0)->text();
    const LoreEntity *orig = m_project->lore().findEntity(id);
    if (!orig) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Lore Entity: %1").arg(id));
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(orig->name, &dlg);
    form->addRow(QStringLiteral("Name:"), nameEdit);

    auto *catCombo = new QComboBox(&dlg);
    catCombo->addItems({QStringLiteral("Character"), QStringLiteral("Location"), QStringLiteral("Faction"),
                        QStringLiteral("Artifact"), QStringLiteral("Concept"), QStringLiteral("Rule"), QStringLiteral("Fact")});
    catCombo->setCurrentText(orig->category);
    form->addRow(QStringLiteral("Category:"), catCombo);

    auto *tagsEdit = new QLineEdit(orig->tags.join(QStringLiteral(", ")), &dlg);
    form->addRow(QStringLiteral("Tags:"), tagsEdit);

    auto *descEdit = new QPlainTextEdit(orig->description, &dlg);
    form->addRow(QStringLiteral("Description:"), descEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        LoreEntity updated = *orig;
        updated.name = nameEdit->text().trimmed();
        updated.category = catCombo->currentText();
        updated.tags.clear();
        for (const QString &t : tagsEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            updated.tags.append(t.trimmed());
        }
        updated.description = descEdit->toPlainText().trimmed();
        updated.modified = QDateTime::currentDateTimeUtc();

        m_project->lore().addOrUpdateEntity(updated);
        m_project->markDirtyBy(QStringLiteral("user"));
        populateLoreEntitiesTable();
    }
}

void LoreGraphDialog::onDeleteEntityClicked()
{
    if (!m_project || !m_entitiesTable) return;
    int row = m_entitiesTable->currentRow();
    if (row < 0) return;

    QString id = m_entitiesTable->item(row, 0)->text();
    m_project->lore().removeEntity(id);
    m_project->markDirtyBy(QStringLiteral("user"));
    populateLoreEntitiesTable();
}

void LoreGraphDialog::onAddHistoryClicked()
{
    if (!m_project) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add Backstory Timeline Event (lore/history.json)"));
    auto *form = new QFormLayout(&dlg);

    auto *idEdit = new QLineEdit(&dlg);
    idEdit->setPlaceholderText(QStringLiteral("e.g. event_great_war"));
    form->addRow(QStringLiteral("ID:"), idEdit);

    auto *dateEdit = new QLineEdit(&dlg);
    dateEdit->setPlaceholderText(QStringLiteral("e.g. 10 years ago, Year 842"));
    form->addRow(QStringLiteral("Date / Era:"), dateEdit);

    auto *titleEdit = new QLineEdit(&dlg);
    form->addRow(QStringLiteral("Title:"), titleEdit);

    auto *chapterEdit = new QLineEdit(QStringLiteral("-1"), &dlg);
    chapterEdit->setPlaceholderText(QStringLiteral("-1 for backstory, or 1..N for story chapter"));
    form->addRow(QStringLiteral("Story Chapter:"), chapterEdit);

    auto *summaryEdit = new QPlainTextEdit(&dlg);
    form->addRow(QStringLiteral("Summary:"), summaryEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        QString id = idEdit->text().trimmed();
        if (id.isEmpty()) return;

        LoreHistoryEvent ev;
        ev.id = id;
        ev.eraOrDate = dateEdit->text().trimmed();
        ev.title = titleEdit->text().trimmed();
        ev.storyChapter = chapterEdit->text().toInt();
        ev.summary = summaryEdit->toPlainText().trimmed();

        m_project->lore().addOrUpdateHistoryEvent(ev);
        m_project->markDirtyBy(QStringLiteral("user"));
        populateHistoryTable();
    }
}

void LoreGraphDialog::onDeleteHistoryClicked()
{
    if (!m_project || !m_historyTable) return;
    int row = m_historyTable->currentRow();
    if (row < 0) return;

    QString id = m_historyTable->item(row, 0)->text();
    m_project->lore().removeHistoryEvent(id);
    m_project->markDirtyBy(QStringLiteral("user"));
    populateHistoryTable();
}

// -----------------------------------------------------------------------------
// Conflicts & Plans Slots
// -----------------------------------------------------------------------------

void LoreGraphDialog::onAddConflictClicked()
{
    if (!m_project) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Log Lore / Canon Conflict (lore/conflicts.json)"));
    auto *form = new QFormLayout(&dlg);

    auto *idEdit = new QLineEdit(&dlg);
    idEdit->setPlaceholderText(QStringLiteral("e.g. conflict_alden_death_year"));
    form->addRow(QStringLiteral("ID:"), idEdit);

    auto *titleEdit = new QLineEdit(&dlg);
    form->addRow(QStringLiteral("Title:"), titleEdit);

    auto *sevCombo = new QComboBox(&dlg);
    sevCombo->addItems({QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("critical")});
    sevCombo->setCurrentText(QStringLiteral("medium"));
    form->addRow(QStringLiteral("Severity:"), sevCombo);

    auto *chAEdit = new QLineEdit(QStringLiteral("1"), &dlg);
    auto *chBEdit = new QLineEdit(QStringLiteral("2"), &dlg);
    form->addRow(QStringLiteral("Chapter A:"), chAEdit);
    form->addRow(QStringLiteral("Chapter B:"), chBEdit);

    auto *descEdit = new QPlainTextEdit(&dlg);
    descEdit->setPlaceholderText(QStringLiteral("Explain the discrepancy between chapters or canon..."));
    form->addRow(QStringLiteral("Description:"), descEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        QString id = idEdit->text().trimmed();
        if (id.isEmpty()) return;

        LoreConflict conf;
        conf.id = id;
        conf.title = titleEdit->text().trimmed();
        conf.severity = sevCombo->currentText();
        conf.status = QStringLiteral("open");
        conf.chapterA = chAEdit->text().toInt();
        conf.chapterB = chBEdit->text().toInt();
        conf.description = descEdit->toPlainText().trimmed();
        conf.detectedAt = QDateTime::currentDateTimeUtc();
        conf.detectedBy = QStringLiteral("user");

        m_project->lore().addOrUpdateConflict(conf);
        m_project->markDirtyBy(QStringLiteral("user"));
        populateConflictsTable();
    }
}

void LoreGraphDialog::onResolveConflictClicked()
{
    if (!m_project || !m_conflictsTable) return;
    int row = m_conflictsTable->currentRow();
    if (row < 0) return;

    QString id = m_conflictsTable->item(row, 0)->text();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Resolve Conflict: %1").arg(id));
    auto *form = new QFormLayout(&dlg);

    auto *resEdit = new QPlainTextEdit(&dlg);
    resEdit->setPlaceholderText(QStringLiteral("Describe how this plot discrepancy was resolved..."));
    form->addRow(QStringLiteral("Resolution:"), resEdit);

    auto *canonEdit = new QLineEdit(&dlg);
    canonEdit->setPlaceholderText(QStringLiteral("Definitive canon rule adopted for subsequent chapters"));
    form->addRow(QStringLiteral("Canon Ruling:"), canonEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        m_project->lore().resolveConflict(id, resEdit->toPlainText().trimmed(), canonEdit->text().trimmed(), QStringLiteral("user"));
        m_project->markDirtyBy(QStringLiteral("user"));
        populateConflictsTable();
        QMessageBox::information(this, QStringLiteral("Conflict Resolved"),
                                 QStringLiteral("Conflict has been resolved and moved to lore/resolved.json."));
    }
}

void LoreGraphDialog::onAddPlanClicked()
{
    if (!m_project) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add Narrative Plan / Follow-up (lore/plans.json)"));
    auto *form = new QFormLayout(&dlg);

    auto *idEdit = new QLineEdit(&dlg);
    idEdit->setPlaceholderText(QStringLiteral("e.g. reveal_traitor"));
    form->addRow(QStringLiteral("ID:"), idEdit);

    auto *titleEdit = new QLineEdit(&dlg);
    form->addRow(QStringLiteral("Title:"), titleEdit);

    auto *chEdit = new QLineEdit(QStringLiteral("3"), &dlg);
    form->addRow(QStringLiteral("Target Chapter:"), chEdit);

    auto *statCombo = new QComboBox(&dlg);
    statCombo->addItems({QStringLiteral("planned"), QStringLiteral("in_progress"), QStringLiteral("fulfilled"), QStringLiteral("abandoned")});
    form->addRow(QStringLiteral("Status:"), statCombo);

    auto *descEdit = new QPlainTextEdit(&dlg);
    form->addRow(QStringLiteral("Description & Lore to Follow Up:"), descEdit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    if (dlg.exec() == QDialog::Accepted) {
        QString id = idEdit->text().trimmed();
        if (id.isEmpty()) return;

        LorePlan plan;
        plan.id = id;
        plan.title = titleEdit->text().trimmed();
        plan.targetChapter = chEdit->text().toInt();
        plan.status = statCombo->currentText();
        plan.description = descEdit->toPlainText().trimmed();

        m_project->lore().addOrUpdatePlan(plan);
        m_project->markDirtyBy(QStringLiteral("user"));
        populatePlansTable();
    }
}

void LoreGraphDialog::onDeletePlanClicked()
{
    if (!m_project || !m_plansTable) return;
    int row = m_plansTable->currentRow();
    if (row < 0) return;

    QString id = m_plansTable->item(row, 0)->text();
    m_project->lore().removePlan(id);
    m_project->markDirtyBy(QStringLiteral("user"));
    populatePlansTable();
}
