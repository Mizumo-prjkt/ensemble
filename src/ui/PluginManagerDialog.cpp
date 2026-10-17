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

namespace EnsemblePlugin {

PluginManagerDialog::PluginManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("[Beta] Scripting Plugins Manager"));
    resize(840, 560);
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

    overviewLayout->addWidget(m_nameLabel);
    overviewLayout->addWidget(m_idLabel);
    overviewLayout->addWidget(m_versionLabel);
    overviewLayout->addWidget(m_statusBadge);
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

    m_addDomainBtn = new QPushButton(tr("Add Domain..."), this);
    connect(m_addDomainBtn, &QPushButton::clicked, this, &PluginManagerDialog::onAddDomainClicked);
    domainBtnRow->addWidget(m_addDomainBtn);

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
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);

    mainLayout->addLayout(btnRow);
}

void PluginManagerDialog::refreshPluginList() {
    m_table->setRowCount(0);
    const auto list = PluginManager::instance().plugins();

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
    }

    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
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
    m_uninstallBtn->setEnabled(true);

    if (meta.quarantined) {
        m_statusBadge->setText(tr("<b>Status:</b> <span style='color: #c9302c; font-weight: bold;'>🚨 QUARANTINED FOR SECURITY VIOLATION</span>"));
        m_errorBox->setPlainText(meta.faultError);
        m_toggleActiveBtn->setText(tr("Quarantined (Disabled)"));
        m_toggleActiveBtn->setEnabled(false);
    } else if (meta.faulted) {
        m_statusBadge->setText(tr("<b>Status:</b> <span style='color: red;'>Faulted (Line %1)</span>").arg(meta.faultLine));
        m_errorBox->setPlainText(meta.faultError);
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
        m_detectedSummaryLabel->setText(tr("<b>Static Analysis:</b> -"));
        m_detectedDomainsLabel->setText(tr("<b>Detected Endpoints:</b> None"));
        m_allowedDomainsList->clear();
        m_savePermsBtn->setEnabled(false);
        m_allowDetectedBtn->setEnabled(false);
        m_addDomainBtn->setEnabled(false);
        m_removeDomainBtn->setEnabled(false);
        return;
    }

    PluginMetadata meta = PluginManager::instance().plugin(publicId);
    const auto &perms = meta.permissions;

    m_chkEditorRead->setChecked(perms.allowEditorRead);
    m_chkEditorWrite->setChecked(perms.allowEditorWrite);
    m_chkStorage->setChecked(perms.allowStorage);
    m_chkUi->setChecked(perms.allowUi);
    m_chkNetwork->setChecked(perms.allowNetwork);

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
    m_addDomainBtn->setEnabled(true);
    m_removeDomainBtn->setEnabled(true);
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
    refreshAuditLogs();
}

void PluginManagerDialog::onToggleActiveClicked() {
    if (m_selectedPublicId.isEmpty()) return;
    PluginMetadata meta = PluginManager::instance().plugin(m_selectedPublicId);
    PluginManager::instance().setPluginActive(m_selectedPublicId, !meta.active);
    updateDetailsPane(m_selectedPublicId);
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

void PluginManagerDialog::onAddDomainClicked() {
    bool ok = false;
    QString domain = QInputDialog::getText(this, tr("Add Allowed Domain"),
                                           tr("Enter domain or hostname (e.g. api.example.com):"),
                                           QLineEdit::Normal, QString(), &ok);
    if (ok && !domain.trimmed().isEmpty()) {
        QString clean = domain.trimmed().toLower();
        m_allowedDomainsList->addItem(clean);
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
    QString samplePath = QDir(AppPaths::pluginsDir()).filePath(QStringLiteral("dialogue_checker.er"));
    if (QFile::exists(samplePath)) {
        QMessageBox::information(this, tr("Sample Plugin Exists"),
                                 tr("Sample plugin already exists at:\n%1").arg(samplePath));
        return;
    }

    QFile file(samplePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const char *sample =
            "# ID: author.dialogue-checker\n"
            "# Name: Dialogue Checker\n"
            "# Version: 1.0\n"
            "\n"
            "@on_toolbar_click(\"Check Dialogue\")\n"
            "def check_dialogue():\n"
            "    text = editor.get_chapter_text()\n"
            "    lines = text.split(\"\\n\")\n"
            "    unbalanced = 0\n"
            "    for line in lines:\n"
            "        count = 0\n"
            "        for ch in line:\n"
            "            if ch == '\"':\n"
            "                count += 1\n"
            "        if count % 2 != 0:\n"
            "            unbalanced += 1\n"
            "    if unbalanced > 0:\n"
            "        status.warn(\"Dialogue check: found \" + str(unbalanced) + \" lines with unmatched quotes\", 1)\n"
            "    else:\n"
            "        status.notify(\"Dialogue check passed! All quotation marks are paired.\")\n";
        file.write(sample);
        file.close();
        PluginManager::instance().loadPluginFromFile(samplePath);
        QMessageBox::information(this, tr("Sample Plugin Created"),
                                 tr("Created and loaded dialogue_checker.er"));
    }
}

} // namespace EnsemblePlugin
