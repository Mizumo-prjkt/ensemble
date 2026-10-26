#pragma once

#include <QDialog>
#include <QDateTime>
#include <QList>
#include <QString>

class QTabWidget;
class QLabel;
class QPushButton;
class QTableWidget;
class QCheckBox;
class QTimer;
class Ao3Project;

namespace EnsemblePlugin {
struct PluginNetworkStats;
}

class ResourceManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ResourceManagerDialog(Ao3Project *project, QWidget *parent = nullptr);
    ~ResourceManagerDialog() override;

public slots:
    void refreshAll();
    void onTrimChaptersClicked();
    void onAutoRefreshToggled(bool enabled);
    void updateTheme();

private:
    void setupUi();
    QWidget *createOverviewTab();
    QWidget *createDocumentTab();
    QWidget *createPluginsTab();
    QWidget *createNetworkTab();

    void refreshProcessOverview();
    void refreshDocumentMemory();
    void refreshPluginResources();
    void refreshPluginNetwork();

    Ao3Project *m_project = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTimer *m_refreshTimer = nullptr;

    // Overview Tab Widgets
    QLabel *m_pidLabel = nullptr;
    QLabel *m_cpuLabel = nullptr;
    QLabel *m_rssLabel = nullptr;
    QLabel *m_vmSizeLabel = nullptr;
    QLabel *m_threadsLabel = nullptr;
    QLabel *m_diagnosticsWorkerLabel = nullptr;
    QLabel *m_guiWatchdogLabel = nullptr;
    QLabel *m_inMemoryHtmlLabel = nullptr;
    QLabel *m_totalStoryHtmlLabel = nullptr;
    QLabel *m_chapterResidencyLabel = nullptr;
    QLabel *m_cssMemoryLabel = nullptr;
    QLabel *m_loreMemoryLabel = nullptr;
    QPushButton *m_trimOverviewBtn = nullptr;
    QCheckBox *m_autoRefreshCheck = nullptr;

    // Document Memory Tab Widgets
    QTableWidget *m_docTable = nullptr;
    QLabel *m_docSummaryLabel = nullptr;
    QPushButton *m_trimDocBtn = nullptr;
    QPushButton *m_refreshDocBtn = nullptr;

    // Plugins Tab Widgets
    QTableWidget *m_pluginTable = nullptr;
    QLabel *m_pluginSummaryLabel = nullptr;
    QPushButton *m_refreshPluginBtn = nullptr;

    // Network Tab Widgets (Strictly omitted if plugin does not have network links)
    QLabel *m_noNetworkBanner = nullptr;
    QTableWidget *m_networkTable = nullptr;
    QLabel *m_networkSummaryLabel = nullptr;
    QPushButton *m_refreshNetworkBtn = nullptr;
    QPushButton *m_openAuditLogBtn = nullptr;
};
