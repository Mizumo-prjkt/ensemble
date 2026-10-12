#pragma once

#include "mcp/AgentActivity.h"
#include <QDialog>
#include <QList>

class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTabWidget;
class QHBoxLayout;
class McpServer;
class Ao3Project;

class AgentThoughtsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AgentThoughtsDialog(QWidget *parent = nullptr);
    explicit AgentThoughtsDialog(McpServer *server, QWidget *parent = nullptr);
    ~AgentThoughtsDialog() override = default;

    void setMcpServer(McpServer *server);
    void setProject(Ao3Project *project);

public slots:
    void addActivityEvent(const AgentActivityEvent &event);
    void updatePresenceHeader(int count, const QStringList &names);
    void updateTheme();
    void clearEvents();
    void exportLog();
    void copyCurrentInputData();
    void refreshModelHistory();

private slots:
    void onSelectionChanged();
    void onFilterChanged();
    void onHistorySelectionChanged();
    void onHistoryFilterChanged();

private:
    void setupUi();
    void refreshFilteredList();
    bool eventMatchesFilter(const AgentActivityEvent &event) const;
    void updateLastChangesCards();
    const AgentActivityEvent *findEvent(const QString &id) const;

    McpServer *m_server = nullptr;
    Ao3Project *m_project = nullptr;
    QList<AgentActivityEvent> m_allEvents;

    QLabel *m_headerTitle = nullptr;
    QLabel *m_presenceStatusLabel = nullptr;
    QWidget *m_lastChangesCardWidget = nullptr;
    QHBoxLayout *m_lastChangesLayout = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_agentFilterCombo = nullptr;
    QComboBox *m_typeFilterCombo = nullptr;
    QCheckBox *m_autoScrollCheck = nullptr;

    QTreeWidget *m_eventTree = nullptr;
    QTabWidget *m_detailTabs = nullptr;
    QTextEdit *m_thoughtTextEdit = nullptr;
    QTextEdit *m_inputTextEdit = nullptr;
    QTextEdit *m_resultTextEdit = nullptr;

    // 4th Tab: Model History & Audit Trail
    QWidget *m_historyTab = nullptr;
    QLineEdit *m_historySearchEdit = nullptr;
    QComboBox *m_historyAgentCombo = nullptr;
    QTreeWidget *m_historyTree = nullptr;
    QTextEdit *m_historyDetailEdit = nullptr;
    QPushButton *m_refreshHistoryBtn = nullptr;

    QLabel *m_statsLabel = nullptr;
    QPushButton *m_copyInputBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
