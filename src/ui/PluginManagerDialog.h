#pragma once

#include <QDialog>
#include <QStringList>

class QTableWidget;
class QLabel;
class QPushButton;
class QTextEdit;
class QComboBox;
class QCheckBox;
class QListWidget;
class QTabWidget;

namespace EnsemblePlugin {

class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget *parent = nullptr);
    ~PluginManagerDialog() override = default;

public slots:
    void refreshPluginList();

private slots:
    void onSelectionChanged();
    void onLoadPluginClicked();
    void onOpenPluginsFolderClicked();
    void onReloadClicked();
    void onToggleActiveClicked();
    void onUninstallClicked();
    void onRunActionClicked();
    void onCreateSamplePluginClicked();

    // Permissions slots
    void onSavePermissionsClicked();
    void onAllowAllDetectedDomainsClicked();
    void onAddDomainClicked();
    void onRemoveDomainClicked();

    // Audit log slots
    void onRefreshLogsClicked();
    void onClearLogsClicked();

private:
    void setupUi();
    void updateDetailsPane(const QString &publicId);
    void updatePermissionsPane(const QString &publicId);
    void refreshAuditLogs();

    QTableWidget *m_table = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    // Overview Tab
    QLabel *m_idLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_pathLabel = nullptr;
    QTextEdit *m_errorBox = nullptr;
    QComboBox *m_actionCombo = nullptr;
    QPushButton *m_runActionBtn = nullptr;
    QPushButton *m_toggleActiveBtn = nullptr;
    QPushButton *m_uninstallBtn = nullptr;

    // Permissions Tab
    QCheckBox *m_chkEditorRead = nullptr;
    QCheckBox *m_chkEditorWrite = nullptr;
    QCheckBox *m_chkStorage = nullptr;
    QCheckBox *m_chkUi = nullptr;
    QCheckBox *m_chkNetwork = nullptr;
    QLabel *m_detectedSummaryLabel = nullptr;
    QLabel *m_detectedDomainsLabel = nullptr;
    QListWidget *m_allowedDomainsList = nullptr;
    QPushButton *m_allowDetectedBtn = nullptr;
    QPushButton *m_addDomainBtn = nullptr;
    QPushButton *m_removeDomainBtn = nullptr;
    QPushButton *m_savePermsBtn = nullptr;

    // Audit Log Tab
    QTextEdit *m_auditLogText = nullptr;
    QPushButton *m_refreshLogBtn = nullptr;
    QPushButton *m_clearLogBtn = nullptr;

    // Bottom buttons
    QPushButton *m_loadBtn = nullptr;
    QPushButton *m_openFolderBtn = nullptr;
    QPushButton *m_reloadBtn = nullptr;
    QPushButton *m_createSampleBtn = nullptr;

    QString m_selectedPublicId;
};

} // namespace EnsemblePlugin
