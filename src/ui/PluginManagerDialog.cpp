#include "PluginManagerDialog.h"
#include "plugins/PluginLogger.h"
#include "plugins/PluginManager.h"
#include "utils/AppPaths.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString formatBytes(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("0 B");
    }
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    }
    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 2));
    }
    double gb = mb / 1024.0;
    return QStringLiteral("%1 GB").arg(QString::number(gb, 'f', 2));
}
} // namespace

namespace EnsemblePlugin {

PluginManagerDialog::PluginManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("[Beta] Scripting Plugins Manager"));
    resize(840, 560);
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setupUi();
    refreshPluginList();
    refreshAuditLogs();

    connect(&PluginManager::instance(), &PluginManager::pluginListChanged,
            this, &PluginManagerDialog::refreshPluginList);
}

void PluginManagerDialog::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto *headerLabel = new QLabel(
        tr("<h3>[Beta] Sandboxed Scripting Plugins (.er)</h3>"
           "<p style='color: gray; margin-top: -4px;'>"
           "Extend Ensemble using sandboxed Python-like DSL plugins (.er). "
           "All plugins run on isolated background threads with strict memory limits (8 MB), "
           "permission boundaries, and logged network mediation."
           "</p>"), this);
    mainLayout->addWidget(headerLabel);

    auto *splitLayout = new QHBoxLayout();
    splitLayout->setSpacing(12);

    // Left table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("ID"), tr("Version"), tr("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PluginManagerDialog::onSelectionChanged);
    splitLayout->addWidget(m_table, 3);

    // Right Tab Widget
    m_tabWidget = new QTabWidget(this);

    // ----------------------------------------------------
    // Tab 1: Overview & Controls
    // ----------------------------------------------------
    auto *overviewTab = new QWidget(this);
    auto *overviewLayout = new QVBoxLayout(overviewTab);
    overviewLayout->setSpacing(8);

    m_nameLabel = new QLabel(tr("<b>Name:</b> -"), this);
    m_idLabel = new QLabel(tr("<b>ID:</b> -"), this);
    m_versionLabel = new QLabel(tr("<b>Version:</b> -"), this);
    m_statusBadge = new QLabel(tr("<b>Status:</b> -"), this);
    m_pathLabel = new QLabel(tr("<b>Path:</b> -"), this);
    m_pathLabel->setWordWrap(true);
    m_resourceUsageLabel = new QLabel(tr("<b>Resource Usage:</b> -"), this);

    overviewLayout->addWidget(m_nameLabel);
    overviewLayout->addWidget(m_idLabel);
    overviewLayout->addWidget(m_versionLabel);
    overviewLayout->addWidget(m_statusBadge);
    overviewLayout->addWidget(m_resourceUsageLabel);
    overviewLayout->addWidget(m_pathLabel);

    m_errorBox = new QTextEdit(this);
    m_errorBox->setReadOnly(true);
    m_errorBox->setPlaceholderText(tr("No faults or errors reported."));
    m_errorBox->setMaximumHeight(80);
    overviewLayout->addWidget(new QLabel(tr("Fault Log:"), this));
    overviewLayout->addWidget(m_errorBox);

    auto *actionLayout = new QHBoxLayout();
    m_actionCombo = new QComboBox(this);
    m_runActionBtn = new QPushButton(tr("▶ Run Action"), this);
    m_runActionBtn->setEnabled(false);
    connect(m_runActionBtn, &QPushButton::clicked, this, &PluginManagerDialog::onRunActionClicked);
    actionLayout->addWidget(m_actionCombo);
    actionLayout->addWidget(m_runActionBtn);
    overviewLayout->addLayout(actionLayout);

    auto *btnCtrlLayout = new QHBoxLayout();
    m_toggleActiveBtn = new QPushButton(tr("Disable Plugin"), this);
    m_toggleActiveBtn->setEnabled(false);
    connect(m_toggleActiveBtn, &QPushButton::clicked, this, &PluginManagerDialog::onToggleActiveClicked);
    btnCtrlLayout->addWidget(m_toggleActiveBtn);

    m_uninstallBtn = new QPushButton(tr("🗑️ Uninstall / Delete"), this);
    m_uninstallBtn->setEnabled(false);
    m_uninstallBtn->setStyleSheet(QStringLiteral("color: #d9534f;"));
    connect(m_uninstallBtn, &QPushButton::clicked, this, &PluginManagerDialog::onUninstallClicked);
    btnCtrlLayout->addWidget(m_uninstallBtn);
    overviewLayout->addLayout(btnCtrlLayout);

    overviewLayout->addStretch();
    m_tabWidget->addTab(overviewTab, tr("Overview"));

    // ----------------------------------------------------
    // Tab 2: Permissions & Domains
    // ----------------------------------------------------
    auto *permsTab = new QWidget(this);
    auto *permsLayout = new QVBoxLayout(permsTab);
    permsLayout->setSpacing(8);

    auto *capsGroup = new QGroupBox(tr("Ensemble Access Permissions"), this);
    auto *capsLayout = new QVBoxLayout(capsGroup);
    m_chkEditorRead = new QCheckBox(tr("Editor Read (Inspect text, titles & chapters)"), this);
    m_chkEditorWrite = new QCheckBox(tr("Editor Write (Modify text, insert, replace selection)"), this);
    m_chkStorage = new QCheckBox(tr("Local Storage (Private 5 MB key-value store)"), this);
    m_chkUi = new QCheckBox(tr("UI Windows (Create dialog windows & compositing)"), this);
    m_chkNetwork = new QCheckBox(tr("Internet Access (Outbound HTTP requests via Ensemble)"), this);

    capsLayout->addWidget(m_chkEditorRead);
    capsLayout->addWidget(m_chkEditorWrite);
    capsLayout->addWidget(m_chkStorage);
    capsLayout->addWidget(m_chkUi);
    capsLayout->addWidget(m_chkNetwork);
    permsLayout->addWidget(capsGroup);

    m_detectedSummaryLabel = new QLabel(tr("<b>Static Analysis:</b> -"), this);
    m_detectedSummaryLabel->setWordWrap(true);
    permsLayout->addWidget(m_detectedSummaryLabel);

    auto *domainGroup = new QGroupBox(tr("Allowed Web Domains (Whitelist)"), this);
    auto *domainLayout = new QVBoxLayout(domainGroup);

    m_detectedDomainsLabel = new QLabel(tr("<b>Detected Endpoints:</b> None"), this);
    m_detectedDomainsLabel->setWordWrap(true);
    domainLayout->addWidget(m_detectedDomainsLabel);

    m_allowedDomainsList = new QListWidget(this);
    m_allowedDomainsList->setMaximumHeight(90);
    domainLayout->addWidget(m_allowedDomainsList);

    auto *domainBtnRow = new QHBoxLayout();
    m_allowDetectedBtn = new QPushButton(tr("Allow Detected"), this);
    connect(m_allowDetectedBtn, &QPushButton::clicked, this, &PluginManagerDialog::onAllowAllDetectedDomainsClicked);
    domainBtnRow->addWidget(m_allowDetectedBtn);

    m_removeDomainBtn = new QPushButton(tr("Remove"), this);
    connect(m_removeDomainBtn, &QPushButton::clicked, this, &PluginManagerDialog::onRemoveDomainClicked);
    domainBtnRow->addWidget(m_removeDomainBtn);
    domainLayout->addLayout(domainBtnRow);

    permsLayout->addWidget(domainGroup);

    m_savePermsBtn = new QPushButton(tr("💾 Apply & Save Permissions"), this);
    m_savePermsBtn->setEnabled(false);
    connect(m_savePermsBtn, &QPushButton::clicked, this, &PluginManagerDialog::onSavePermissionsClicked);
    permsLayout->addWidget(m_savePermsBtn);

    m_tabWidget->addTab(permsTab, tr("Permissions"));

    // ----------------------------------------------------
    // Tab 3: Audit Log
    // ----------------------------------------------------
    auto *logTab = new QWidget(this);
    auto *logLayout = new QVBoxLayout(logTab);
    logLayout->setSpacing(6);

    auto *logInfoLabel = new QLabel(tr("All plugin actions (network requests, UI creations, storage operations, and blocked attempts) are audited here:"), this);
    logInfoLabel->setWordWrap(true);
    logLayout->addWidget(logInfoLabel);

    m_auditLogText = new QTextEdit(this);
    m_auditLogText->setReadOnly(true);
    m_auditLogText->setFontFamily(QStringLiteral("monospace"));
    logLayout->addWidget(m_auditLogText);

    auto *logBtnRow = new QHBoxLayout();
    m_refreshLogBtn = new QPushButton(tr("🔄 Refresh Log"), this);
    connect(m_refreshLogBtn, &QPushButton::clicked, this, &PluginManagerDialog::onRefreshLogsClicked);
    logBtnRow->addWidget(m_refreshLogBtn);

    m_clearLogBtn = new QPushButton(tr("🗑️ Clear Log"), this);
    connect(m_clearLogBtn, &QPushButton::clicked, this, &PluginManagerDialog::onClearLogsClicked);
    logBtnRow->addWidget(m_clearLogBtn);
    logBtnRow->addStretch();
    logLayout->addLayout(logBtnRow);

    m_tabWidget->addTab(logTab, tr("Audit Log"));

    splitLayout->addWidget(m_tabWidget, 3);
    mainLayout->addLayout(splitLayout);

    // Bottom action buttons
    auto *btnRow = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("📁 Load Plugin File (.er)..."), this);
    connect(m_loadBtn, &QPushButton::clicked, this, &PluginManagerDialog::onLoadPluginClicked);
    btnRow->addWidget(m_loadBtn);

    m_openFolderBtn = new QPushButton(tr("📂 Open Plugins Folder"), this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &PluginManagerDialog::onOpenPluginsFolderClicked);
    btnRow->addWidget(m_openFolderBtn);

    m_reloadBtn = new QPushButton(tr("🔄 Reload All"), this);
    connect(m_reloadBtn, &QPushButton::clicked, this, &PluginManagerDialog::onReloadClicked);
    btnRow->addWidget(m_reloadBtn);

    m_createSampleBtn = new QPushButton(tr("✨ Create Sample Plugin (.er)"), this);
    connect(m_createSampleBtn, &QPushButton::clicked, this, &PluginManagerDialog::onCreateSamplePluginClicked);
    btnRow->addWidget(m_createSampleBtn);

    btnRow->addStretch();

    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    btnRow->addWidget(closeBtn);

    mainLayout->addLayout(btnRow);
}

void PluginManagerDialog::refreshPluginList() {
    QString prevSelected = m_selectedPublicId;
    m_table->setRowCount(0);
    const auto list = PluginManager::instance().plugins();

    int targetRow = -1;
    for (int row = 0; row < list.size(); ++row) {
        const auto &meta = list.at(row);
        m_table->insertRow(row);

        auto *nameItem = new QTableWidgetItem(meta.name);
        auto *idItem = new QTableWidgetItem(meta.publicId);
        auto *verItem = new QTableWidgetItem(meta.version);

        QString statusText = tr("Active");
        if (meta.quarantined) {
            statusText = tr("🚨 Quarantined");
        } else if (meta.faulted) {
            statusText = tr("Faulted");
        } else if (!meta.active) {
            statusText = tr("Disabled");
        }
        auto *statusItem = new QTableWidgetItem(statusText);

        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, idItem);
        m_table->setItem(row, 2, verItem);
        m_table->setItem(row, 3, statusItem);

        if (!prevSelected.isEmpty() && meta.publicId == prevSelected) {
            targetRow = row;
        }
    }

    if (m_table->rowCount() > 0) {
        if (targetRow >= 0) {
            m_table->selectRow(targetRow);
        } else {
            m_table->selectRow(0);
        }
    } else {
        updateDetailsPane(QString());
        updatePermissionsPane(QString());
    }
}

void PluginManagerDialog::onSelectionChanged() {
    int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) {
        m_selectedPublicId.clear();
        updateDetailsPane(QString());
        updatePermissionsPane(QString());
        return;
    }
    QTableWidgetItem *idItem = m_table->item(row, 1);
    if (idItem) {
        m_selectedPublicId = idItem->text();
        updateDetailsPane(m_selectedPublicId);
        updatePermissionsPane(m_selectedPublicId);
    }
}

void PluginManagerDialog::updateDetailsPane(const QString &publicId) {
    if (publicId.isEmpty() || !PluginManager::instance().hasPlugin(publicId)) {
        m_nameLabel->setText(tr("<b>Name:</b> -"));
        m_idLabel->setText(tr("<b>ID:</b> -"));
        m_versionLabel->setText(tr("<b>Version:</b> -"));
        m_statusBadge->setText(tr("<b>Status:</b> -"));
        m_pathLabel->setText(tr("<b>Path:</b> -"));
        m_resourceUsageLabel->setText(tr("<b>Resource Usage:</b> -"));
        m_errorBox->clear();
        m_actionCombo->clear();
        m_runActionBtn->setEnabled(false);
        m_toggleActiveBtn->setEnabled(false);
        m_uninstallBtn->setEnabled(false);
        return;
    }

    PluginMetadata meta = PluginManager::instance().plugin(publicId);
    m_nameLabel->setText(tr("<b>Name:</b> %1").arg(meta.name));
    m_idLabel->setText(tr("<b>ID:</b> %1").arg(meta.publicId));
    m_versionLabel->setText(tr("<b>Version:</b> %1").arg(meta.version));
    m_pathLabel->setText(tr("<b>Path:</b> %1").arg(meta.filePath.isEmpty() ? tr("<in-memory>") : meta.filePath));
    m_resourceUsageLabel->setText(tr("<b>Resource Usage:</b> Heap: %1 / 8.00 MiB | Storage: %2 / 5.00 MiB")
                                      .arg(formatBytes(meta.memoryUsedBytes), formatBytes(meta.storageUsedBytes)));
    m_uninstallBtn->setEnabled(true);

    if (meta.quarantined) {
        m_statusBadge->setText(tr("<b>Status:</b> <span style='color: #c9302c; font-weight: bold;'>🚨 QUARANTINED FOR SECURITY VIOLATION</span>"));
        m_errorBox->setPlainText(meta.faultError);
        m_toggleActiveBtn->setText(tr("Quarantined (Disabled)"));
        m_toggleActiveBtn->setEnabled(false);
    } else if (meta.faulted) {
        m_statusBadge->setText(tr("<b>Status:</b> <span style='color: red;'>Faulted (Line %1)</span>").arg(meta.faultLine));
        QString errorDisplay = meta.faultError;
        if (errorDisplay.contains(QStringLiteral("Editor write permission is disabled"), Qt::CaseInsensitive)) {
            errorDisplay += tr("\n\n💡 Tip: Switch to the 'Permissions' tab, enable 'Editor Write', click 'Save Permissions', and click 'Re-enable Plugin' (or click 'Re-enable Plugin' to grant it automatically).");
        } else if (errorDisplay.contains(QStringLiteral("Internet access is disabled"), Qt::CaseInsensitive) ||
                   errorDisplay.contains(QStringLiteral("Domain not whitelisted"), Qt::CaseInsensitive)) {
            errorDisplay += tr("\n\n💡 Tip: Switch to the 'Permissions' tab, enable 'Allow Internet Access', whitelist the domain, and click 'Save Permissions'.");
        }
        m_errorBox->setPlainText(errorDisplay);
        m_toggleActiveBtn->setText(tr("Re-enable Plugin"));
        m_toggleActiveBtn->setEnabled(true);
    } else if (!meta.active) {
        m_statusBadge->setText(tr("<b>Status:</b> <span style='color: gray;'>Disabled</span>"));
        m_errorBox->clear();
        m_toggleActiveBtn->setText(tr("Enable Plugin"));
        m_toggleActiveBtn->setEnabled(true);
    } else {
        m_statusBadge->setText(tr("<b>Status:</b> <span style='color: green;'>Active</span>"));
        m_errorBox->clear();
        m_toggleActiveBtn->setText(tr("Disable Plugin"));
        m_toggleActiveBtn->setEnabled(true);
    }

    m_actionCombo->clear();
    for (const QString &act : meta.toolbarActions) {
        m_actionCombo->addItem(act);
    }
    m_runActionBtn->setEnabled(meta.active && !meta.faulted && m_actionCombo->count() > 0);
}

void PluginManagerDialog::updatePermissionsPane(const QString &publicId) {
    if (publicId.isEmpty() || !PluginManager::instance().hasPlugin(publicId)) {
        m_chkEditorRead->setChecked(false);
        m_chkEditorWrite->setChecked(false);
        m_chkStorage->setChecked(false);
        m_chkUi->setChecked(false);
        m_chkNetwork->setChecked(false);
        m_chkNetwork->setEnabled(false);
        m_chkNetwork->setText(tr("Allow Internet Access (External Requests)"));
        m_detectedSummaryLabel->setText(tr("<b>Static Analysis:</b> -"));
        m_detectedDomainsLabel->setText(tr("<b>Detected Endpoints:</b> None"));
        m_allowedDomainsList->clear();
        m_savePermsBtn->setEnabled(false);
        m_allowDetectedBtn->setEnabled(false);
        m_removeDomainBtn->setEnabled(false);
        return;
    }

    PluginMetadata meta = PluginManager::instance().plugin(publicId);
    const auto &perms = meta.permissions;

    m_chkEditorRead->setChecked(perms.allowEditorRead);
    m_chkEditorWrite->setChecked(perms.allowEditorWrite);
    m_chkStorage->setChecked(perms.allowStorage);
    m_chkUi->setChecked(perms.allowUi);

    bool hasNetworkRequests = perms.detectedNetwork || !perms.detectedDomains.isEmpty();
    m_chkNetwork->setChecked(perms.allowNetwork);
    if (!hasNetworkRequests) {
        m_chkNetwork->setEnabled(false);
        m_chkNetwork->setText(tr("Internet Access (This plugin does not have URL requests)"));
    } else {
        m_chkNetwork->setEnabled(true);
        m_chkNetwork->setText(tr("Allow Internet Access (External Requests)"));
    }

    QStringList detectedList;
    if (perms.detectedEditorRead) detectedList.append(tr("Editor Read"));
    if (perms.detectedEditorWrite) detectedList.append(tr("Editor Write"));
    if (perms.detectedStorage) detectedList.append(tr("Storage"));
    if (perms.detectedUi) detectedList.append(tr("UI Windows"));
    if (perms.detectedNetwork) detectedList.append(tr("Network"));

    m_detectedSummaryLabel->setText(tr("<b>Static Analysis Detected:</b> %1")
                                       .arg(detectedList.isEmpty() ? tr("No special API calls found") : detectedList.join(QStringLiteral(", "))));

    if (perms.detectedDomains.isEmpty()) {
        m_detectedDomainsLabel->setText(tr("<b>Detected Endpoints:</b> None found in script"));
    } else {
        m_detectedDomainsLabel->setText(tr("<b>Detected Endpoints:</b> <span style='color: #2b78e4;'>%1</span>")
                                           .arg(perms.detectedDomains.join(QStringLiteral(", "))));
    }

    m_allowedDomainsList->clear();
    for (const QString &d : perms.allowedDomains) {
        m_allowedDomainsList->addItem(d);
    }

    m_savePermsBtn->setEnabled(true);
    m_allowDetectedBtn->setEnabled(!perms.detectedDomains.isEmpty());
    m_removeDomainBtn->setEnabled(m_allowedDomainsList->count() > 0);
}

void PluginManagerDialog::onLoadPluginClicked() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select Plugin Script"),
                                                AppPaths::pluginsDir(),
                                                tr("Ensemble Runner (*.er);;Legacy Plugins (*.py *.ensp);;All Files (*.*)"));
    if (path.isEmpty()) return;

    QString errorMsg;
    if (!PluginManager::instance().loadPluginFromFile(path, &errorMsg)) {
        QMessageBox::warning(this, tr("Failed to Load Plugin"), errorMsg);
    }
}

void PluginManagerDialog::onOpenPluginsFolderClicked() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::pluginsDir()));
}

void PluginManagerDialog::onReloadClicked() {
    PluginManager::instance().scanAndLoadPlugins();
    refreshPluginList();
    if (!m_selectedPublicId.isEmpty()) {
        updateDetailsPane(m_selectedPublicId);
        updatePermissionsPane(m_selectedPublicId);
    }
    refreshAuditLogs();
}

void PluginManagerDialog::onToggleActiveClicked() {
    if (m_selectedPublicId.isEmpty()) return;
    PluginMetadata meta = PluginManager::instance().plugin(m_selectedPublicId);

    if (meta.faulted) {
        // If the plugin faulted due to missing permissions, offer to grant the permission directly
        if (meta.faultError.contains(QStringLiteral("Editor write permission is disabled"), Qt::CaseInsensitive)) {
            auto ans = QMessageBox::question(
                this, tr("Grant Editor Write Permission?"),
                tr("Plugin '%1' faulted because <b>Editor Write</b> permission is disabled.\n\n"
                   "Would you like to grant Editor Write permission and re-enable the plugin?")
                    .arg(meta.name),
                QMessageBox::Yes | QMessageBox::No
            );
            if (ans == QMessageBox::Yes) {
                auto perms = meta.permissions;
                perms.allowEditorWrite = true;
                PluginManager::instance().setPluginPermissions(m_selectedPublicId, perms);
            }
        } else if (meta.faultError.contains(QStringLiteral("Internet access is disabled"), Qt::CaseInsensitive) ||
                   meta.faultError.contains(QStringLiteral("Domain not whitelisted"), Qt::CaseInsensitive)) {
            auto ans = QMessageBox::question(
                this, tr("Grant Internet Access Permission?"),
                tr("Plugin '%1' faulted because <b>Internet Access</b> is disabled or endpoints are not whitelisted.\n\n"
                   "Would you like to allow Internet Access for detected domains and re-enable the plugin?")
                    .arg(meta.name),
                QMessageBox::Yes | QMessageBox::No
            );
            if (ans == QMessageBox::Yes) {
                auto perms = meta.permissions;
                perms.allowNetwork = true;
                for (const QString &d : perms.detectedDomains) {
                    if (!perms.allowedDomains.contains(d, Qt::CaseInsensitive)) {
                        perms.allowedDomains.append(d);
                    }
                }
                PluginManager::instance().setPluginPermissions(m_selectedPublicId, perms);
            }
        }

        // Re-enabling a faulted plugin: reload/re-initialize the plugin cleanly
        PluginManager::instance().reloadPlugin(m_selectedPublicId);
    } else {
        PluginManager::instance().setPluginActive(m_selectedPublicId, !meta.active);
    }

    refreshPluginList();
    updateDetailsPane(m_selectedPublicId);
    updatePermissionsPane(m_selectedPublicId);
    refreshAuditLogs();
}

void PluginManagerDialog::onUninstallClicked() {
    if (m_selectedPublicId.isEmpty()) return;
    PluginMetadata meta = PluginManager::instance().plugin(m_selectedPublicId);

    auto reply = QMessageBox::question(
        this, tr("Uninstall Plugin"),
        tr("Are you sure you want to uninstall and delete '%1' (%2)?\n\n"
           "This will permanently delete the plugin script and its private local storage.")
            .arg(meta.name, meta.publicId),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QString err;
        if (!PluginManager::instance().uninstallPlugin(m_selectedPublicId, true, &err)) {
            QMessageBox::warning(this, tr("Uninstall Failed"), err);
        } else {
            refreshPluginList();
            refreshAuditLogs();
            QMessageBox::information(this, tr("Plugin Uninstalled"),
                                     tr("Successfully uninstalled '%1'.").arg(meta.name));
        }
    }
}

void PluginManagerDialog::onRunActionClicked() {
    QString action = m_actionCombo->currentText();
    if (!action.isEmpty()) {
        PluginManager::instance().triggerToolbarAction(action);
        refreshAuditLogs();
    }
}

void PluginManagerDialog::onSavePermissionsClicked() {
    if (m_selectedPublicId.isEmpty()) return;
    PluginMetadata meta = PluginManager::instance().plugin(m_selectedPublicId);

    PluginPermissions perms = meta.permissions;
    perms.allowEditorRead = m_chkEditorRead->isChecked();
    perms.allowEditorWrite = m_chkEditorWrite->isChecked();
    perms.allowStorage = m_chkStorage->isChecked();
    perms.allowUi = m_chkUi->isChecked();
    perms.allowNetwork = m_chkNetwork->isChecked();

    QStringList doms;
    for (int i = 0; i < m_allowedDomainsList->count(); ++i) {
        QString d = m_allowedDomainsList->item(i)->text().trimmed();
        if (!d.isEmpty() && !doms.contains(d, Qt::CaseInsensitive)) {
            doms.append(d);
        }
    }
    perms.allowedDomains = doms;

    PluginManager::instance().setPluginPermissions(m_selectedPublicId, perms);
    if (meta.faulted) {
        PluginManager::instance().reloadPlugin(m_selectedPublicId);
    }
    refreshPluginList();
    updateDetailsPane(m_selectedPublicId);
    updatePermissionsPane(m_selectedPublicId);
    refreshAuditLogs();

    QMessageBox::information(this, tr("Permissions Updated"),
                             tr("Permissions successfully updated for '%1'.").arg(meta.name));
}

void PluginManagerDialog::onAllowAllDetectedDomainsClicked() {
    if (m_selectedPublicId.isEmpty()) return;
    PluginMetadata meta = PluginManager::instance().plugin(m_selectedPublicId);

    for (const QString &d : meta.permissions.detectedDomains) {
        bool exists = false;
        for (int i = 0; i < m_allowedDomainsList->count(); ++i) {
            if (m_allowedDomainsList->item(i)->text().compare(d, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_allowedDomainsList->addItem(d);
        }
    }
}

void PluginManagerDialog::onRemoveDomainClicked() {
    auto *item = m_allowedDomainsList->currentItem();
    if (item) {
        delete m_allowedDomainsList->takeItem(m_allowedDomainsList->row(item));
    }
}

void PluginManagerDialog::refreshAuditLogs() {
    const QStringList logs = PluginLogger::readRecentLogs(300);
    m_auditLogText->setPlainText(logs.join(QLatin1Char('\n')));
    m_auditLogText->moveCursor(QTextCursor::End);
}

void PluginManagerDialog::onRefreshLogsClicked() {
    refreshAuditLogs();
}

void PluginManagerDialog::onClearLogsClicked() {
    PluginLogger::clearLogs();
    refreshAuditLogs();
}

void PluginManagerDialog::onCreateSamplePluginClicked() {
    struct SampleTemplate {
        QString name;
        QString fileName;
        QString code;
    };

    QList<SampleTemplate> templates = {
        {
            tr("Dialogue Checker"),
            QStringLiteral("dialogue_checker.er"),
            QStringLiteral(
                "# ID: author.dialogue-checker\n"
                "# Name: Dialogue Checker\n"
                "# Version: 1.1\n\n"
                "@on_toolbar_click(\"Check Dialogue\")\n"
                "def check_dialogue():\n"
                "    text = editor.get_chapter_text()\n"
                "    lines = text.split(\"\\n\")\n"
                "    unbalanced = 0\n"
                "    total_quotes = 0\n"
                "    for line in lines:\n"
                "        c = line.count('\"')\n"
                "        total_quotes += c\n"
                "        if c % 2 != 0:\n"
                "            unbalanced += 1\n"
                "    win = ui.create_window(\"Dialogue Analysis\", 460, 320)\n"
                "    win.add_label(\"Chapter Dialogue & Quotation Analysis:\")\n"
                "    win.add_text(\"Total quotes: \" + str(total_quotes) + \"\\nLines with unmatched pairs: \" + str(unbalanced))\n"
                "    win.add_button(\"Close\", \"close_window\")\n"
                "    win.show()\n\n"
                "@on_chapter_change\n"
                "def on_load(title, text, index):\n"
                "    status.notify(\"Inspecting chapter: \" + title)\n"
            )
        },
        {
            tr("Word & Reading Stats"),
            QStringLiteral("word_counter.er"),
            QStringLiteral(
                "# ID: author.reading-stats\n"
                "# Name: Word & Reading Stats\n"
                "# Version: 1.0\n\n"
                "@on_toolbar_click(\"Reading Stats\")\n"
                "def show_stats():\n"
                "    title = editor.get_chapter_title()\n"
                "    words = editor.get_word_count()\n"
                "    text = editor.get_chapter_text()\n"
                "    chars = len(text)\n"
                "    reading_min = int(words / 225)\n"
                "    if reading_min == 0:\n"
                "        reading_min = 1\n"
                "    win = ui.create_window(\"Reading Statistics\", 460, 340)\n"
                "    win.add_label(\"Statistics for: \" + title)\n"
                "    win.add_text(\"Word Count: \" + str(words) + \"\\nCharacter Count: \" + str(chars) + \"\\nEstimated Reading Time: ~\" + str(reading_min) + \" min\")\n"
                "    win.add_button(\"Close\", \"close_window\")\n"
                "    win.show()\n"
            )
        },
        {
            tr("Smart Quotes Formatter"),
            QStringLiteral("smart_quotes.er"),
            QStringLiteral(
                "# ID: author.smart-quotes\n"
                "# Name: Smart Quotes Formatter\n"
                "# Version: 1.0\n\n"
                "@on_toolbar_click(\"Format Smart Quotes\")\n"
                "def format_quotes():\n"
                "    sel = editor.get_selection()\n"
                "    target_text = sel\n"
                "    is_selection = True\n"
                "    if len(target_text) == 0:\n"
                "        target_text = editor.get_chapter_text()\n"
                "        is_selection = False\n"
                "        if len(target_text) == 0:\n"
                "            status.warn(\"No text to format\", 1)\n"
                "            return\n"
                "    in_quote = False\n"
                "    result = \"\"\n"
                "    for ch in target_text:\n"
                "        if ch == '\"':\n"
                "            if in_quote:\n"
                "                result += chr(8221)\n"
                "                in_quote = False\n"
                "            else:\n"
                "                result += chr(8220)\n"
                "                in_quote = True\n"
                "        else:\n"
                "            result += ch\n"
                "    if is_selection:\n"
                "        editor.replace_selection(result)\n"
                "        status.notify(\"Converted selection to smart quotes\")\n"
                "    else:\n"
                "        editor.set_chapter_text(result)\n"
                "        status.notify(\"Converted chapter to smart quotes\")\n"
            )
        },
        {
            tr("Chapter Scratchpad"),
            QStringLiteral("chapter_scratchpad.er"),
            QStringLiteral(
                "# ID: author.scratchpad\n"
                "# Name: Chapter Scratchpad\n"
                "# Version: 1.0\n\n"
                "@on_toolbar_click(\"Open Scratchpad\")\n"
                "def open_scratchpad():\n"
                "    title = editor.get_chapter_title()\n"
                "    key = \"note_\" + title\n"
                "    saved_note = storage.read(key)\n"
                "    win = ui.create_window(\"Author Notes — \" + title, 480, 360)\n"
                "    win.add_label(\"Persistent notes for: \" + title)\n"
                "    win.add_input(\"Enter notes...\", saved_note)\n"
                "    win.add_button(\"Save Note\", \"on_save\")\n"
                "    win.add_button(\"Close\", \"close_window\")\n"
                "    win.show()\n\n"
                "@on_button_click(\"on_save\")\n"
                "def handle_save(inputs, wid):\n"
                "    title = editor.get_chapter_title()\n"
                "    key = \"note_\" + title\n"
                "    note_text = inputs.get(\"text\", \"\")\n"
                "    storage.write(key, note_text)\n"
                "    status.notify(\"Saved note for: \" + title)\n"
                "    close_window(wid)\n"
            )
        },
        {
            tr("Web Dictionary Lookup"),
            QStringLiteral("web_thesaurus.er"),
            QStringLiteral(
                "# ID: author.web-lookup\n"
                "# Name: Web Dictionary Lookup\n"
                "# Version: 1.0\n\n"
                "@on_toolbar_click(\"Lookup Word\")\n"
                "def lookup_word():\n"
                "    sel = editor.get_selection().strip()\n"
                "    if len(sel) == 0:\n"
                "        status.warn(\"Please select a word in the editor to look up\", 1)\n"
                "        return\n"
                "    word = sel.lower()\n"
                "    url = \"https://api.dictionaryapi.dev/api/v2/entries/en/\" + word\n"
                "    resp = http.get(url)\n"
                "    status_code = resp.get(\"status\", 0)\n"
                "    if status_code == 200:\n"
                "        body = resp.get(\"body\", \"\")\n"
                "        win = ui.create_window(\"Definition: \" + word, 500, 380)\n"
                "        win.add_label(\"Dictionary definition for '\" + word + \"':\")\n"
                "        win.add_text(body)\n"
                "        win.add_button(\"Close\", \"close_window\")\n"
                "        win.show()\n"
                "    else:\n"
                "        status.warn(\"Definition lookup failed (HTTP \" + str(status_code) + \")\", 1)\n"
            )
        }
    };

    QStringList names;
    for (const auto &tmpl : templates) {
        names.append(tmpl.name);
    }

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, tr("Create Sample Plugin"),
                                          tr("Choose a sample plugin template:"),
                                          names, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    const SampleTemplate *selected = nullptr;
    for (const auto &tmpl : templates) {
        if (tmpl.name == chosen) {
            selected = &tmpl;
            break;
        }
    }
    if (!selected) return;

    QString samplePath = QDir(AppPaths::pluginsDir()).filePath(selected->fileName);
    if (QFile::exists(samplePath)) {
        auto ans = QMessageBox::question(this, tr("Sample Plugin Exists"),
                                         tr("Plugin '%1' already exists. Overwrite?").arg(selected->fileName),
                                         QMessageBox::Yes | QMessageBox::No);
        if (ans != QMessageBox::Yes) return;
    }

    QFile file(samplePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(selected->code.toUtf8());
        file.close();

        // Pre-configure appropriate permissions for built-in sample plugins so they run immediately
        PluginPermissions samplePerms;
        PluginScanner::scan(selected->code, samplePerms);
        if (samplePerms.detectedEditorWrite) {
            samplePerms.allowEditorWrite = true;
        }
        if (samplePerms.detectedNetwork) {
            samplePerms.allowNetwork = true;
            samplePerms.allowedDomains = samplePerms.detectedDomains;
        }

        QString publicId, name, version;
        PluginManager::parsePluginHeader(selected->code, publicId, name, version);
        if (publicId.isEmpty()) publicId = QFileInfo(samplePath).baseName();
        if (name.isEmpty()) name = publicId;

        PluginManager::instance().loadPluginFromSource(publicId, name, version, selected->code, samplePath, &samplePerms);
        refreshPluginList();
        updateDetailsPane(publicId);
        updatePermissionsPane(publicId);
        QMessageBox::information(this, tr("Sample Plugin Created"),
                                 tr("Created and loaded %1").arg(selected->fileName));
    }
}

} // namespace EnsemblePlugin
