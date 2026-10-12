#include "AgentThoughtsDialog.h"
#include "mcp/McpServer.h"
#include "model/Ao3Project.h"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace {

class AgentActivityTreeWidgetItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 0) {
            // Column 0: Date & Time sorting via QDateTime
            const QDateTime dt1 = data(0, Qt::UserRole + 1).toDateTime();
            const QDateTime dt2 = other.data(0, Qt::UserRole + 1).toDateTime();
            if (dt1.isValid() && dt2.isValid()) {
                if (dt1 != dt2) {
                    return dt1 < dt2;
                }
            }
        } else if (col == 5) {
            // Column 5: Duration sorting via integer milliseconds
            const int d1 = data(5, Qt::UserRole + 2).toInt();
            const int d2 = other.data(5, Qt::UserRole + 2).toInt();
            if (d1 != d2) {
                return d1 < d2;
            }
        }
        return QTreeWidgetItem::operator<(other);
    }
};

class AgentHistoryTreeWidgetItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 0) {
            // Column 0: Date & Time sorting via QDateTime
            const QDateTime dt1 = data(0, Qt::UserRole + 1).toDateTime();
            const QDateTime dt2 = other.data(0, Qt::UserRole + 1).toDateTime();
            if (dt1.isValid() && dt2.isValid()) {
                if (dt1 != dt2) {
                    return dt1 < dt2;
                }
            }
        } else if (col == 4) {
            // Column 4: Word Delta sorting via integer
            const int w1 = data(4, Qt::UserRole + 2).toInt();
            const int w2 = other.data(4, Qt::UserRole + 2).toInt();
            if (w1 != w2) {
                return w1 < w2;
            }
        }
        return QTreeWidgetItem::operator<(other);
    }
};

} // namespace

AgentThoughtsDialog::AgentThoughtsDialog(QWidget *parent)
    : AgentThoughtsDialog(static_cast<McpServer *>(nullptr), parent)
{
}

AgentThoughtsDialog::AgentThoughtsDialog(McpServer *server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setWindowTitle(QStringLiteral("AI Agent Thoughts & Processing Monitor"));
    setMinimumSize(960, 640);
    resize(1020, 680);

    setupUi();
    updateTheme();

    if (m_server) {
        setMcpServer(m_server);
    }
}

const AgentActivityEvent *AgentThoughtsDialog::findEvent(const QString &id) const
{
    if (id.isEmpty()) return nullptr;
    for (const auto &ev : m_allEvents) {
        if (ev.id == id) {
            return &ev;
        }
    }
    return nullptr;
}

void AgentThoughtsDialog::setMcpServer(McpServer *server)
{
    if (m_server == server && server != nullptr) {
        // Already connected to this server, only import any new activities not yet in m_allEvents
        const auto existing = server->recentActivities();
        for (const auto &ev : existing) {
            bool found = false;
            for (const auto &curr : m_allEvents) {
                if (!ev.id.isEmpty() && curr.id == ev.id) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                addActivityEvent(ev);
            }
        }

        const auto sessions = server->activeAgentSessions();
        QStringList names;
        for (const auto &s : sessions) {
            names.append(QStringLiteral("%1 (%2)").arg(s.name, s.role));
        }
        updatePresenceHeader(sessions.size(), names);
        return;
    }

    if (m_server) {
        disconnect(m_server, nullptr, this, nullptr);
    }

    m_server = server;
    if (!m_server) return;

    connect(m_server, &McpServer::agentActivityLogged,
            this, &AgentThoughtsDialog::addActivityEvent, Qt::UniqueConnection);
    connect(m_server, &McpServer::agentPresenceChanged,
            this, &AgentThoughtsDialog::updatePresenceHeader, Qt::UniqueConnection);

    // Pre-populate with recent activities from server buffer without duplicating
    const auto existing = m_server->recentActivities();
    for (const auto &ev : existing) {
        bool found = false;
        for (const auto &curr : m_allEvents) {
            if (!ev.id.isEmpty() && curr.id == ev.id) {
                found = true;
                break;
            }
        }
        if (!found) {
            addActivityEvent(ev);
        }
    }

    const auto sessions = m_server->activeAgentSessions();
    QStringList names;
    for (const auto &s : sessions) {
        names.append(QStringLiteral("%1 (%2)").arg(s.name, s.role));
    }
    updatePresenceHeader(sessions.size(), names);
}

void AgentThoughtsDialog::setProject(Ao3Project *project)
{
    m_project = project;
    refreshModelHistory();
}

void AgentThoughtsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // -------------------------------------------------------------
    // Header Bar
    // -------------------------------------------------------------
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(3);

    m_headerTitle = new QLabel(QStringLiteral("🧠 AI Agent Thoughts & Processing Inspector"), this);
    m_headerTitle->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: bold;"));

    m_presenceStatusLabel = new QLabel(QStringLiteral("No AI agents currently active."), this);
    m_presenceStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #888888;"));

    titleCol->addWidget(m_headerTitle);
    titleCol->addWidget(m_presenceStatusLabel);
    headerLayout->addLayout(titleCol, 1);

    m_copyInputBtn = new QPushButton(QStringLiteral("📋 Copy Selected Input"), this);
    m_copyInputBtn->setToolTip(QStringLiteral("Copy the raw input JSON sent by the AI agent to clipboard"));
    connect(m_copyInputBtn, &QPushButton::clicked, this, &AgentThoughtsDialog::copyCurrentInputData);
    headerLayout->addWidget(m_copyInputBtn);

    m_exportBtn = new QPushButton(QStringLiteral("💾 Export Log…"), this);
    connect(m_exportBtn, &QPushButton::clicked, this, &AgentThoughtsDialog::exportLog);
    headerLayout->addWidget(m_exportBtn);

    m_clearBtn = new QPushButton(QStringLiteral("🗑️ Clear"), this);
    connect(m_clearBtn, &QPushButton::clicked, this, &AgentThoughtsDialog::clearEvents);
    headerLayout->addWidget(m_clearBtn);

    mainLayout->addLayout(headerLayout);

    // -------------------------------------------------------------
    // Model Last Changes Banner
    // -------------------------------------------------------------
    auto *cardsContainer = new QWidget(this);
    auto *cardsContainerLayout = new QVBoxLayout(cardsContainer);
    cardsContainerLayout->setContentsMargins(0, 0, 0, 0);
    cardsContainerLayout->setSpacing(4);

    auto *cardsHeaderLabel = new QLabel(QStringLiteral("<b>Last Model Modifications & Delta:</b>"), cardsContainer);
    cardsHeaderLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #a0aec0;"));
    cardsContainerLayout->addWidget(cardsHeaderLabel);

    auto *scrollArea = new QScrollArea(cardsContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedHeight(68);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_lastChangesCardWidget = new QWidget(scrollArea);
    m_lastChangesLayout = new QHBoxLayout(m_lastChangesCardWidget);
    m_lastChangesLayout->setContentsMargins(0, 0, 0, 0);
    m_lastChangesLayout->setSpacing(8);
    scrollArea->setWidget(m_lastChangesCardWidget);
    cardsContainerLayout->addWidget(scrollArea);

    mainLayout->addWidget(cardsContainer);
    updateLastChangesCards();

    // -------------------------------------------------------------
    // Filter & Search Bar
    // -------------------------------------------------------------
    auto *filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("🔍 Filter thoughts, actions, or input arguments…"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AgentThoughtsDialog::onFilterChanged);
    filterLayout->addWidget(m_searchEdit, 2);

    m_agentFilterCombo = new QComboBox(this);
    m_agentFilterCombo->addItem(QStringLiteral("All Agents"));
    connect(m_agentFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgentThoughtsDialog::onFilterChanged);
    filterLayout->addWidget(m_agentFilterCombo);

    m_typeFilterCombo = new QComboBox(this);
    m_typeFilterCombo->addItem(QStringLiteral("All Activities"));
    m_typeFilterCombo->addItem(QStringLiteral("💭 Thoughts & Critiques"));
    m_typeFilterCombo->addItem(QStringLiteral("📖 Data Inspections"));
    m_typeFilterCombo->addItem(QStringLiteral("✏️ Data Modifications"));
    m_typeFilterCombo->addItem(QStringLiteral("⚙️ Tool Executions"));
    m_typeFilterCombo->addItem(QStringLiteral("👤 Presence Changes"));
    connect(m_typeFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgentThoughtsDialog::onFilterChanged);
    filterLayout->addWidget(m_typeFilterCombo);

    m_autoScrollCheck = new QCheckBox(QStringLiteral("Auto-scroll"), this);
    m_autoScrollCheck->setChecked(true);
    filterLayout->addWidget(m_autoScrollCheck);

    mainLayout->addLayout(filterLayout);

    // -------------------------------------------------------------
    // Splitter: Event Timeline & Detail Inspector
    // -------------------------------------------------------------
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);

    // Tree / List of events
    m_eventTree = new QTreeWidget(splitter);
    m_eventTree->setHeaderLabels({
        QStringLiteral("Date & Time"),
        QStringLiteral("Agent"),
        QStringLiteral("Type"),
        QStringLiteral("Action / Target"),
        QStringLiteral("Model Thought / Analysis"),
        QStringLiteral("Duration")
    });
    m_eventTree->setRootIsDecorated(false);
    m_eventTree->setAlternatingRowColors(true);
    m_eventTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_eventTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_eventTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_eventTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_eventTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_eventTree->header()->resizeSection(3, 230);
    m_eventTree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_eventTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_eventTree->setSortingEnabled(true);
    m_eventTree->sortByColumn(0, Qt::AscendingOrder);
    connect(m_eventTree, &QTreeWidget::itemSelectionChanged, this, &AgentThoughtsDialog::onSelectionChanged);
    splitter->addWidget(m_eventTree);

    // Detail Tabs
    m_detailTabs = new QTabWidget(splitter);

    // Tab 1: Thought & Reasoning
    m_thoughtTextEdit = new QTextEdit(m_detailTabs);
    m_thoughtTextEdit->setReadOnly(true);
    m_thoughtTextEdit->setPlaceholderText(QStringLiteral("Select an event above to inspect the agent's full reasoning, critique, and story analysis…"));
    m_detailTabs->addTab(m_thoughtTextEdit, QStringLiteral("💭 Model Thoughts && Reasoning"));

    // Tab 2: Agent Input Data
    m_inputTextEdit = new QTextEdit(m_detailTabs);
    m_inputTextEdit->setReadOnly(true);
    m_inputTextEdit->setFont(QFont(QStringLiteral("monospace"), 10));
    m_inputTextEdit->setPlaceholderText(QStringLiteral("Exact raw input arguments and payload received from the AI model will be displayed here in formatted JSON…"));
    m_detailTabs->addTab(m_inputTextEdit, QStringLiteral("📥 Agent Input Data"));

    // Tab 3: Response / Output
    m_resultTextEdit = new QTextEdit(m_detailTabs);
    m_resultTextEdit->setReadOnly(true);
    m_resultTextEdit->setFont(QFont(QStringLiteral("monospace"), 10));
    m_resultTextEdit->setPlaceholderText(QStringLiteral("Output and results returned to the AI model…"));
    m_detailTabs->addTab(m_resultTextEdit, QStringLiteral("📤 Response / Output"));

    // Tab 4: Model History & Audit Trail
    m_historyTab = new QWidget(m_detailTabs);
    auto *historyLayout = new QVBoxLayout(m_historyTab);
    historyLayout->setContentsMargins(8, 8, 8, 8);
    historyLayout->setSpacing(8);

    auto *histFilterBar = new QHBoxLayout();
    m_historySearchEdit = new QLineEdit(m_historyTab);
    m_historySearchEdit->setPlaceholderText(QStringLiteral("🔍 Filter model history and audit trail…"));
    m_historySearchEdit->setClearButtonEnabled(true);
    connect(m_historySearchEdit, &QLineEdit::textChanged, this, &AgentThoughtsDialog::onHistoryFilterChanged);
    histFilterBar->addWidget(m_historySearchEdit, 2);

    m_historyAgentCombo = new QComboBox(m_historyTab);
    m_historyAgentCombo->addItem(QStringLiteral("All Models / Agents"));
    connect(m_historyAgentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgentThoughtsDialog::onHistoryFilterChanged);
    histFilterBar->addWidget(m_historyAgentCombo);

    m_refreshHistoryBtn = new QPushButton(QStringLiteral("🔄 Refresh History"), m_historyTab);
    connect(m_refreshHistoryBtn, &QPushButton::clicked, this, &AgentThoughtsDialog::refreshModelHistory);
    histFilterBar->addWidget(m_refreshHistoryBtn);

    historyLayout->addLayout(histFilterBar);

    auto *histSplitter = new QSplitter(Qt::Vertical, m_historyTab);
    histSplitter->setChildrenCollapsible(false);

    m_historyTree = new QTreeWidget(histSplitter);
    m_historyTree->setHeaderLabels({
        QStringLiteral("Date & Time"),
        QStringLiteral("Model / Agent"),
        QStringLiteral("Role"),
        QStringLiteral("Action"),
        QStringLiteral("Word Delta"),
        QStringLiteral("Summary")
    });
    m_historyTree->setRootIsDecorated(false);
    m_historyTree->setAlternatingRowColors(true);
    m_historyTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_historyTree->setSortingEnabled(true);
    m_historyTree->sortByColumn(0, Qt::DescendingOrder);
    connect(m_historyTree, &QTreeWidget::itemSelectionChanged, this, &AgentThoughtsDialog::onHistorySelectionChanged);
    histSplitter->addWidget(m_historyTree);

    m_historyDetailEdit = new QTextEdit(histSplitter);
    m_historyDetailEdit->setReadOnly(true);
    m_historyDetailEdit->setPlaceholderText(QStringLiteral("Select a model history entry above to inspect complete change metrics, XML metadata, and details…"));
    histSplitter->addWidget(m_historyDetailEdit);
    histSplitter->setStretchFactor(0, 3);
    histSplitter->setStretchFactor(1, 2);

    historyLayout->addWidget(histSplitter, 1);
    m_detailTabs->addTab(m_historyTab, QStringLiteral("📜 Model History && Audit Trail"));

    splitter->addWidget(m_detailTabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter, 1);

    // -------------------------------------------------------------
    // Footer Bar
    // -------------------------------------------------------------
    auto *footerLayout = new QHBoxLayout();
    m_statsLabel = new QLabel(QStringLiteral("Total events: 0"), this);
    m_statsLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888888;"));
    footerLayout->addWidget(m_statsLabel, 1);

    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footerLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(footerLayout);
}

void AgentThoughtsDialog::updatePresenceHeader(int count, const QStringList &names)
{
    if (count <= 0) {
        m_presenceStatusLabel->setText(QStringLiteral("⚪ 0 AI agents connected. Waiting for models to connect…"));
        m_presenceStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #888888;"));
    } else {
        m_presenceStatusLabel->setText(QStringLiteral("🟢 %1 AI agent(s) active: %2")
                                           .arg(QString::number(count), names.join(QStringLiteral(", "))));
        m_presenceStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #50fa7b;"));
    }
}

void AgentThoughtsDialog::addActivityEvent(const AgentActivityEvent &event)
{
    // Deduplicate: If an event with the same non-empty ID already exists, do not re-add
    if (!event.id.isEmpty()) {
        for (const auto &existing : m_allEvents) {
            if (existing.id == event.id) {
                return;
            }
        }
    }

    // Insert into m_allEvents maintaining strict chronological order by event.timestamp
    if (m_allEvents.isEmpty() || event.timestamp >= m_allEvents.last().timestamp) {
        m_allEvents.append(event);
    } else {
        auto it = std::lower_bound(m_allEvents.begin(), m_allEvents.end(), event,
            [](const AgentActivityEvent &a, const AgentActivityEvent &b) {
                return a.timestamp < b.timestamp;
            });
        m_allEvents.insert(it, event);
    }

    // Dynamically add new agent name to combo if not present
    if (!event.agentName.isEmpty()) {
        bool found = false;
        for (int i = 0; i < m_agentFilterCombo->count(); ++i) {
            if (m_agentFilterCombo->itemText(i) == event.agentName) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_agentFilterCombo->addItem(event.agentName);
        }
    }

    if (eventMatchesFilter(event)) {
        auto *item = new AgentActivityTreeWidgetItem();
        item->setText(0, event.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        item->setText(1, QStringLiteral("%1 [%2]").arg(event.agentName, event.role));
        item->setText(2, QStringLiteral("%1 %2").arg(event.iconSymbol(), event.typeString()));
        item->setText(3, event.title);
        item->setText(4, event.thought.simplified());
        item->setText(5, QStringLiteral("%1 ms").arg(event.durationMs));

        if (event.isError) {
            item->setForeground(3, QBrush(QColor(QStringLiteral("#ff5555"))));
        }

        // Store ID in UserRole, timestamp in UserRole+1, duration in UserRole+2
        item->setData(0, Qt::UserRole, event.id);
        item->setData(0, Qt::UserRole + 1, event.timestamp);
        item->setData(5, Qt::UserRole + 2, event.durationMs);

        m_eventTree->addTopLevelItem(item);

        if (m_autoScrollCheck->isChecked()) {
            m_eventTree->scrollToItem(item);
        }
    }

    if (event.type == AgentActivityEvent::Type::DataModification ||
        event.type == AgentActivityEvent::Type::Thought ||
        event.type == AgentActivityEvent::Type::PresenceChange) {
        refreshModelHistory();
    }

    m_statsLabel->setText(QStringLiteral("Total events: %1 | Active agents: %2")
                              .arg(QString::number(m_allEvents.size()))
                              .arg(m_server ? QString::number(m_server->clientCount()) : QStringLiteral("0")));
}

bool AgentThoughtsDialog::eventMatchesFilter(const AgentActivityEvent &event) const
{
    // Agent name filter
    const QString filterAgent = m_agentFilterCombo->currentText();
    if (filterAgent != QStringLiteral("All Agents") && event.agentName != filterAgent) {
        return false;
    }

    // Type filter
    const int typeIdx = m_typeFilterCombo->currentIndex();
    if (typeIdx == 1 && event.type != AgentActivityEvent::Type::Thought) return false;
    if (typeIdx == 2 && event.type != AgentActivityEvent::Type::DataInspection) return false;
    if (typeIdx == 3 && event.type != AgentActivityEvent::Type::DataModification) return false;
    if (typeIdx == 4 && event.type != AgentActivityEvent::Type::ToolExecution) return false;
    if (typeIdx == 5 && event.type != AgentActivityEvent::Type::PresenceChange) return false;

    // Search query filter
    const QString query = m_searchEdit->text().trimmed();
    if (!query.isEmpty()) {
        const QString haystack = QStringLiteral("%1 %2 %3 %4 %5")
                                     .arg(event.agentName, event.title, event.thought,
                                          event.role, QString::fromUtf8(QJsonDocument(event.inputData).toJson()));
        if (!haystack.contains(query, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

void AgentThoughtsDialog::refreshFilteredList()
{
    const bool wasSorting = m_eventTree->isSortingEnabled();
    m_eventTree->setSortingEnabled(false);
    m_eventTree->clear();

    for (const auto &event : m_allEvents) {
        if (eventMatchesFilter(event)) {
            auto *item = new AgentActivityTreeWidgetItem();
            item->setText(0, event.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            item->setText(1, QStringLiteral("%1 [%2]").arg(event.agentName, event.role));
            item->setText(2, QStringLiteral("%1 %2").arg(event.iconSymbol(), event.typeString()));
            item->setText(3, event.title);
            item->setText(4, event.thought.simplified());
            item->setText(5, QStringLiteral("%1 ms").arg(event.durationMs));

            if (event.isError) {
                item->setForeground(3, QBrush(QColor(QStringLiteral("#ff5555"))));
            }

            item->setData(0, Qt::UserRole, event.id);
            item->setData(0, Qt::UserRole + 1, event.timestamp);
            item->setData(5, Qt::UserRole + 2, event.durationMs);

            m_eventTree->addTopLevelItem(item);
        }
    }

    m_eventTree->setSortingEnabled(wasSorting);
    if (wasSorting) {
        int sortCol = m_eventTree->sortColumn();
        Qt::SortOrder sortOrd = m_eventTree->header()->sortIndicatorOrder();
        m_eventTree->sortByColumn(sortCol >= 0 ? sortCol : 0, sortOrd);
    }

    if (m_autoScrollCheck->isChecked()) {
        m_eventTree->scrollToBottom();
    }
}

void AgentThoughtsDialog::onFilterChanged()
{
    refreshFilteredList();
}

void AgentThoughtsDialog::onSelectionChanged()
{
    const auto items = m_eventTree->selectedItems();
    if (items.isEmpty()) {
        m_thoughtTextEdit->clear();
        m_inputTextEdit->clear();
        m_resultTextEdit->clear();
        return;
    }

    const QString id = items.first()->data(0, Qt::UserRole).toString();
    const AgentActivityEvent *foundEv = findEvent(id);
    if (!foundEv) return;

    const AgentActivityEvent &ev = *foundEv;

    // 1. Thought / Reasoning
    QString thoughtHtml = QStringLiteral(
        "<h3>%1 %2</h3>"
        "<p><b>Agent:</b> %3 &nbsp;&nbsp;|&nbsp;&nbsp; <b>Role:</b> %4 &nbsp;&nbsp;|&nbsp;&nbsp; <b>Time:</b> %5 (%6 ms)</p>"
        "<hr/>"
        "<div style='font-size: 13px; line-height: 1.5; white-space: pre-wrap;'>%7</div>"
    ).arg(
        ev.iconSymbol(),
        ev.title.toHtmlEscaped(),
        ev.agentName.toHtmlEscaped(),
        ev.role.toHtmlEscaped(),
        ev.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        QString::number(ev.durationMs),
        ev.thought.isEmpty() ? QStringLiteral("<i>(No explicit thought text provided)</i>") : ev.thought.toHtmlEscaped()
    );
    m_thoughtTextEdit->setHtml(thoughtHtml);

    // 2. Exact Agent Input Data
    const QByteArray inputJson = QJsonDocument(ev.inputData).toJson(QJsonDocument::Indented);
    m_inputTextEdit->setPlainText(QString::fromUtf8(inputJson));

    // 3. Response / Output
    const QByteArray resultJson = QJsonDocument(ev.resultData).toJson(QJsonDocument::Indented);
    m_resultTextEdit->setPlainText(QString::fromUtf8(resultJson));
}

void AgentThoughtsDialog::copyCurrentInputData()
{
    const QString text = m_inputTextEdit->toPlainText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void AgentThoughtsDialog::clearEvents()
{
    m_allEvents.clear();
    m_eventTree->clear();
    m_thoughtTextEdit->clear();
    m_inputTextEdit->clear();
    m_resultTextEdit->clear();
    m_statsLabel->setText(QStringLiteral("Total events: 0"));
}

void AgentThoughtsDialog::exportLog()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Agent Activity Log"),
        QStringLiteral("agent_activity_log.json"),
        QStringLiteral("JSON Files (*.json);;Markdown Files (*.md);;All Files (*)")
    );

    if (path.isEmpty()) return;

    if (path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
        QString md = QStringLiteral("# Ensemble AI Agent Activity & Thoughts Log\n\n");
        md += QStringLiteral("Exported on: %1\n\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        for (const auto &ev : m_allEvents) {
            md += QStringLiteral("### %1 %2\n").arg(ev.iconSymbol(), ev.title);
            md += QStringLiteral("- **Agent**: %1 (%2)\n").arg(ev.agentName, ev.role);
            md += QStringLiteral("- **Timestamp**: %1 (%2 ms)\n").arg(ev.timestamp.toString(Qt::ISODate), QString::number(ev.durationMs));
            md += QStringLiteral("\n**Thought / Reasoning:**\n%1\n\n").arg(ev.thought);
            md += QStringLiteral("**Agent Input Data:**\n```json\n%1\n```\n\n---\n\n")
                      .arg(QString::fromUtf8(QJsonDocument(ev.inputData).toJson(QJsonDocument::Indented)));
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(md.toUtf8());
        }
    } else {
        QJsonArray arr;
        for (const auto &ev : m_allEvents) {
            QJsonObject o;
            o[QStringLiteral("timestamp")] = ev.timestamp.toString(Qt::ISODate);
            o[QStringLiteral("agentName")] = ev.agentName;
            o[QStringLiteral("role")] = ev.role;
            o[QStringLiteral("type")] = ev.typeString();
            o[QStringLiteral("title")] = ev.title;
            o[QStringLiteral("thought")] = ev.thought;
            o[QStringLiteral("durationMs")] = ev.durationMs;
            o[QStringLiteral("inputData")] = ev.inputData;
            o[QStringLiteral("resultData")] = ev.resultData;
            arr.append(o);
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        }
    }
}

void AgentThoughtsDialog::updateTheme()
{
    const AppTheme theme = ThemeManager::instance().currentTheme();
    const QString accent = ThemeManager::instance().materialYouAccentColor().isValid()
        ? ThemeManager::instance().materialYouAccentColor().name()
        : QStringLiteral("#3b82f6");

    if (theme == AppTheme::Darker) {
        // OLED Darker Mode
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #000000; color: #e2e8f0; }"
            "QLabel { color: #e2e8f0; }"
            "QLineEdit, QComboBox { background-color: #0f1015; color: #ffffff; border: 1px solid #2a2b3d; border-radius: 4px; padding: 5px 8px; }"
            "QTreeWidget { background-color: #08080a; color: #e2e8f0; border: 1px solid #1a1c26; border-radius: 4px; alternate-background-color: #0d0e12; }"
            "QTreeWidget::item:selected { background-color: %1; color: #ffffff; }"
            "QHeaderView::section { background-color: #121318; color: #a0aec0; border: none; padding: 4px 6px; font-weight: bold; }"
            "QTabWidget::pane { border: 1px solid #1a1c26; background-color: #08080a; border-radius: 4px; }"
            "QTabBar::tab { background-color: #121318; color: #a0aec0; padding: 6px 12px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
            "QTabBar::tab:selected { background-color: #08080a; color: #ffffff; border-bottom: 2px solid %1; }"
            "QTextEdit { background-color: #08080a; color: #e2e8f0; border: none; padding: 8px; }"
            "QPushButton { background-color: #161822; color: #e2e8f0; border: 1px solid #2a2b3d; border-radius: 4px; padding: 5px 12px; }"
            "QPushButton:hover { background-color: #232736; border-color: %1; }"
        ).arg(accent));
    } else if (theme == AppTheme::Dark) {
        // Standard Dark
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #1e1e2e; color: #cdd6f4; }"
            "QLabel { color: #cdd6f4; }"
            "QLineEdit, QComboBox { background-color: #181825; color: #cdd6f4; border: 1px solid #313244; border-radius: 4px; padding: 5px 8px; }"
            "QTreeWidget { background-color: #181825; color: #cdd6f4; border: 1px solid #313244; border-radius: 4px; alternate-background-color: #1e1e2e; }"
            "QTreeWidget::item:selected { background-color: %1; color: #ffffff; }"
            "QHeaderView::section { background-color: #11111b; color: #a6adc8; border: none; padding: 4px 6px; font-weight: bold; }"
            "QTabWidget::pane { border: 1px solid #313244; background-color: #181825; border-radius: 4px; }"
            "QTabBar::tab { background-color: #11111b; color: #a6adc8; padding: 6px 12px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
            "QTabBar::tab:selected { background-color: #181825; color: #cdd6f4; border-bottom: 2px solid %1; }"
            "QTextEdit { background-color: #181825; color: #cdd6f4; border: none; padding: 8px; }"
            "QPushButton { background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 5px 12px; }"
            "QPushButton:hover { background-color: #45475a; border-color: %1; }"
        ).arg(accent));
    } else {
        // Light Theme
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #f8fafc; color: #0f172a; }"
            "QLabel { color: #0f172a; }"
            "QLineEdit, QComboBox { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 5px 8px; }"
            "QTreeWidget { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; alternate-background-color: #f1f5f9; }"
            "QTreeWidget::item:selected { background-color: %1; color: #ffffff; }"
            "QHeaderView::section { background-color: #e2e8f0; color: #334155; border: none; padding: 4px 6px; font-weight: bold; }"
            "QTabWidget::pane { border: 1px solid #cbd5e1; background-color: #ffffff; border-radius: 4px; }"
            "QTabBar::tab { background-color: #e2e8f0; color: #475569; padding: 6px 12px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
            "QTabBar::tab:selected { background-color: #ffffff; color: #0f172a; border-bottom: 2px solid %1; }"
            "QTextEdit { background-color: #ffffff; color: #0f172a; border: none; padding: 8px; }"
            "QPushButton { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 5px 12px; }"
            "QPushButton:hover { background-color: #f1f5f9; border-color: %1; }"
        ).arg(accent));
    }
}

void AgentThoughtsDialog::updateLastChangesCards()
{
    if (!m_lastChangesLayout) return;

    // Clear existing cards
    QLayoutItem *child = nullptr;
    while ((child = m_lastChangesLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }

    if (!m_project) {
        auto *lbl = new QLabel(QStringLiteral("<i>No project loaded.</i>"), m_lastChangesCardWidget);
        lbl->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
        m_lastChangesLayout->addWidget(lbl);
        m_lastChangesLayout->addStretch(1);
        return;
    }

    const auto lastChanges = m_project->agenticData().lastChangesByModel();
    if (lastChanges.isEmpty()) {
        auto *lbl = new QLabel(QStringLiteral("<i>No AI model modifications recorded yet in this project session.</i>"), m_lastChangesCardWidget);
        lbl->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
        m_lastChangesLayout->addWidget(lbl);
        m_lastChangesLayout->addStretch(1);
        return;
    }

    for (auto it = lastChanges.constBegin(); it != lastChanges.constEnd(); ++it) {
        const AgentModelHistoryEntry &entry = it.value();
        auto *card = new QFrame(m_lastChangesCardWidget);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background-color: rgba(255, 255, 255, 0.05); border: 1px solid rgba(128, 128, 128, 0.25); border-radius: 6px; padding: 3px 8px; }"
        ));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(6, 3, 6, 3);
        cardLayout->setSpacing(2);

        QString deltaBadge;
        if (entry.wordCountDelta > 0) {
            deltaBadge = QStringLiteral("<span style='color: #50fa7b; font-weight: bold;'>+%1 w</span>").arg(entry.wordCountDelta);
        } else if (entry.wordCountDelta < 0) {
            deltaBadge = QStringLiteral("<span style='color: #ff5555; font-weight: bold;'>%1 w</span>").arg(entry.wordCountDelta);
        }

        auto *title = new QLabel(
            QStringLiteral("<b>🤖 %1</b> <span style='font-size: 10px; color: #888888;'>(%2)</span> &nbsp; %3")
                .arg(entry.agentName.toHtmlEscaped(), entry.model.toHtmlEscaped(), deltaBadge),
            card
        );
        title->setStyleSheet(QStringLiteral("font-size: 11px;"));

        QString summaryText = entry.summary;
        if (summaryText.length() > 40) {
            summaryText = summaryText.left(37) + QStringLiteral("…");
        }

        auto *sub = new QLabel(
            QStringLiteral("<span style='color: #aaa;'>%1 • %2</span> — <span style='font-size: 10px;'>%3</span>")
                .arg(entry.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), entry.action, summaryText.toHtmlEscaped()),
            card
        );
        sub->setStyleSheet(QStringLiteral("font-size: 10px; color: #bbbbbb;"));

        cardLayout->addWidget(title);
        cardLayout->addWidget(sub);
        m_lastChangesLayout->addWidget(card);
    }

    m_lastChangesLayout->addStretch(1);
}

void AgentThoughtsDialog::refreshModelHistory()
{
    updateLastChangesCards();

    if (!m_historyTree) return;

    const bool wasSorting = m_historyTree->isSortingEnabled();
    m_historyTree->setSortingEnabled(false);
    m_historyTree->clear();
    if (m_historyDetailEdit) {
        m_historyDetailEdit->clear();
    }

    if (!m_project) {
        m_historyTree->setSortingEnabled(wasSorting);
        return;
    }

    const auto &entries = m_project->agenticData().historyEntries();

    // Populate or update agent filter combo
    if (m_historyAgentCombo) {
        QStringList existing;
        for (int i = 1; i < m_historyAgentCombo->count(); ++i) {
            existing.append(m_historyAgentCombo->itemText(i));
        }
        for (const auto &e : entries) {
            if (!existing.contains(e.agentName, Qt::CaseInsensitive)) {
                existing.append(e.agentName);
                m_historyAgentCombo->addItem(e.agentName);
            }
        }
    }

    const QString search = m_historySearchEdit ? m_historySearchEdit->text().trimmed() : QString();
    const QString filterAgent = m_historyAgentCombo ? m_historyAgentCombo->currentText() : QStringLiteral("All Models / Agents");

    // Display newest first
    for (int i = entries.size() - 1; i >= 0; --i) {
        const auto &e = entries.at(i);

        if (filterAgent != QStringLiteral("All Models / Agents") &&
            e.agentName.compare(filterAgent, Qt::CaseInsensitive) != 0) {
            continue;
        }

        if (!search.isEmpty()) {
            const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6")
                                         .arg(e.agentName, e.model, e.role, e.action, e.summary, e.details);
            if (!haystack.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
        }

        auto *item = new AgentHistoryTreeWidgetItem();
        item->setText(0, e.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        item->setText(1, QStringLiteral("%1 (%2)").arg(e.agentName, e.model));
        item->setText(2, e.role);
        item->setText(3, e.action);

        if (e.wordCountDelta > 0) {
            item->setText(4, QStringLiteral("+%1 words").arg(e.wordCountDelta));
            item->setForeground(4, QBrush(QColor(QStringLiteral("#50fa7b"))));
        } else if (e.wordCountDelta < 0) {
            item->setText(4, QStringLiteral("%1 words").arg(e.wordCountDelta));
            item->setForeground(4, QBrush(QColor(QStringLiteral("#ff5555"))));
        } else {
            item->setText(4, QStringLiteral("0"));
            item->setForeground(4, QBrush(QColor(QStringLiteral("#888888"))));
        }

        item->setText(5, e.summary);
        item->setData(0, Qt::UserRole, i);
        item->setData(0, Qt::UserRole + 1, e.timestamp);
        item->setData(4, Qt::UserRole + 2, e.wordCountDelta);
        m_historyTree->addTopLevelItem(item);
    }

    m_historyTree->setSortingEnabled(wasSorting);
    if (wasSorting) {
        int sortCol = m_historyTree->sortColumn();
        Qt::SortOrder sortOrd = m_historyTree->header()->sortIndicatorOrder();
        m_historyTree->sortByColumn(sortCol >= 0 ? sortCol : 0, sortOrd);
    }
}

void AgentThoughtsDialog::onHistoryFilterChanged()
{
    refreshModelHistory();
}

void AgentThoughtsDialog::onHistorySelectionChanged()
{
    if (!m_historyTree || !m_historyDetailEdit || !m_project) return;
    const auto items = m_historyTree->selectedItems();
    if (items.isEmpty()) {
        m_historyDetailEdit->clear();
        return;
    }

    const int idx = items.first()->data(0, Qt::UserRole).toInt();
    const auto &entries = m_project->agenticData().historyEntries();
    if (idx < 0 || idx >= entries.size()) return;

    const auto &e = entries.at(idx);

    QString deltaColor = QStringLiteral("#888888");
    QString deltaStr = QStringLiteral("0 words");
    if (e.wordCountDelta > 0) {
        deltaColor = QStringLiteral("#50fa7b");
        deltaStr = QStringLiteral("+%1 words").arg(e.wordCountDelta);
    } else if (e.wordCountDelta < 0) {
        deltaColor = QStringLiteral("#ff5555");
        deltaStr = QStringLiteral("%1 words").arg(e.wordCountDelta);
    }

    QString html = QStringLiteral(
        "<h3>📜 %1: %2</h3>"
        "<table style='font-size: 12px; margin-bottom: 8px;' cellpadding='4'>"
        "<tr><td><b>Agent:</b></td><td>%3</td><td><b>Model:</b></td><td>%4</td></tr>"
        "<tr><td><b>Role:</b></td><td>%5</td><td><b>Timestamp:</b></td><td>%6</td></tr>"
        "<tr><td><b>Chapter:</b></td><td>%7</td><td><b>Word Delta:</b></td><td><span style='color: %8; font-weight: bold;'>%9</span></td></tr>"
        "</table>"
        "<hr/>"
        "<p><b>Summary:</b></p>"
        "<div style='font-size: 13px; line-height: 1.5; padding: 6px; background-color: rgba(255,255,255,0.03); border-radius: 4px;'>%10</div>"
    ).arg(
        e.agentName.toHtmlEscaped(),
        e.action.toHtmlEscaped(),
        e.agentName.toHtmlEscaped(),
        e.model.toHtmlEscaped(),
        e.role.toHtmlEscaped(),
        e.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
        (e.chapterIndex >= 0 ? QStringLiteral("Chapter %1").arg(e.chapterIndex + 1) : QStringLiteral("N/A (Work-level)")),
        deltaColor,
        deltaStr,
        e.summary.toHtmlEscaped()
    );

    if (!e.details.isEmpty()) {
        html += QStringLiteral(
            "<p style='margin-top: 10px;'><b>Details / Context:</b></p>"
            "<pre style='font-size: 11px; white-space: pre-wrap; padding: 6px; background-color: rgba(0,0,0,0.2); border-radius: 4px;'>%1</pre>"
        ).arg(e.details.toHtmlEscaped());
    }

    m_historyDetailEdit->setHtml(html);
}
