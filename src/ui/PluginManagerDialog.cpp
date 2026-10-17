#include "PluginManagerDialog.h"
#include "plugins/PluginManager.h"
#include "utils/AppPaths.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace EnsemblePlugin {

PluginManagerDialog::PluginManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("[Beta] Scripting Plugins Manager"));
    resize(760, 520);
    setupUi();
    refreshPluginList();

    connect(&PluginManager::instance(), &PluginManager::pluginListChanged,
            this, &PluginManagerDialog::refreshPluginList);
}

void PluginManagerDialog::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto *headerLabel = new QLabel(tr("<h3>[Beta] Sandboxed Scripting Plugins</h3>"
                                      "<p style='color: gray; margin-top: -4px;'>"
                                      "Extend Ensemble using safe, sandboxed Python-like DSL plugins. "
                                      "All scripts run isolated on background threads with strict memory (8 MB) and execution limits."
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

    // Right details pane
    auto *detailsGroup = new QGroupBox(tr("Plugin Details"), this);
    auto *detailsLayout = new QVBoxLayout(detailsGroup);
    detailsLayout->setSpacing(8);

    m_nameLabel = new QLabel(tr("<b>Name:</b> -"), this);
    m_idLabel = new QLabel(tr("<b>ID:</b> -"), this);
    m_versionLabel = new QLabel(tr("<b>Version:</b> -"), this);
    m_statusBadge = new QLabel(tr("<b>Status:</b> -"), this);
    m_pathLabel = new QLabel(tr("<b>Path:</b> -"), this);
    m_pathLabel->setWordWrap(true);

    detailsLayout->addWidget(m_nameLabel);
    detailsLayout->addWidget(m_idLabel);
    detailsLayout->addWidget(m_versionLabel);
    detailsLayout->addWidget(m_statusBadge);
    detailsLayout->addWidget(m_pathLabel);

    m_errorBox = new QTextEdit(this);
    m_errorBox->setReadOnly(true);
    m_errorBox->setPlaceholderText(tr("No faults or errors reported."));
    m_errorBox->setMaximumHeight(100);
    detailsLayout->addWidget(new QLabel(tr("Fault Log:"), this));
    detailsLayout->addWidget(m_errorBox);

    auto *actionLayout = new QHBoxLayout();
    m_actionCombo = new QComboBox(this);
    m_runActionBtn = new QPushButton(tr("▶ Run Action"), this);
    m_runActionBtn->setEnabled(false);
    connect(m_runActionBtn, &QPushButton::clicked, this, &PluginManagerDialog::onRunActionClicked);
    actionLayout->addWidget(m_actionCombo);
    actionLayout->addWidget(m_runActionBtn);
    detailsLayout->addLayout(actionLayout);

    m_toggleActiveBtn = new QPushButton(tr("Disable Plugin"), this);
    m_toggleActiveBtn->setEnabled(false);
    connect(m_toggleActiveBtn, &QPushButton::clicked, this, &PluginManagerDialog::onToggleActiveClicked);
    detailsLayout->addWidget(m_toggleActiveBtn);
    detailsLayout->addStretch();

    splitLayout->addWidget(detailsGroup, 2);
    mainLayout->addLayout(splitLayout);

    // Bottom action buttons
    auto *btnRow = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("📁 Load Plugin File..."), this);
    connect(m_loadBtn, &QPushButton::clicked, this, &PluginManagerDialog::onLoadPluginClicked);
    btnRow->addWidget(m_loadBtn);

    m_openFolderBtn = new QPushButton(tr("📂 Open Plugins Folder"), this);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &PluginManagerDialog::onOpenPluginsFolderClicked);
    btnRow->addWidget(m_openFolderBtn);

    m_reloadBtn = new QPushButton(tr("🔄 Reload All"), this);
    connect(m_reloadBtn, &QPushButton::clicked, this, &PluginManagerDialog::onReloadClicked);
    btnRow->addWidget(m_reloadBtn);

    m_createSampleBtn = new QPushButton(tr("✨ Create Sample Plugin"), this);
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
        if (meta.faulted) {
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
    }
}

void PluginManagerDialog::onSelectionChanged() {
    int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) {
        updateDetailsPane(QString());
        return;
    }
    QTableWidgetItem *idItem = m_table->item(row, 1);
    if (idItem) {
        updateDetailsPane(idItem->text());
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
        return;
    }

    PluginMetadata meta = PluginManager::instance().plugin(publicId);
    m_nameLabel->setText(tr("<b>Name:</b> %1").arg(meta.name));
    m_idLabel->setText(tr("<b>ID:</b> %1").arg(meta.publicId));
    m_versionLabel->setText(tr("<b>Version:</b> %1").arg(meta.version));
    m_pathLabel->setText(tr("<b>Path:</b> %1").arg(meta.filePath.isEmpty() ? tr("<in-memory>") : meta.filePath));

    if (meta.faulted) {
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

void PluginManagerDialog::onLoadPluginClicked() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select Plugin Script"),
                                                AppPaths::pluginsDir(),
                                                tr("Ensemble Plugins (*.py *.ensp);;All Files (*.*)"));
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
}

void PluginManagerDialog::onToggleActiveClicked() {
    int row = m_table->currentRow();
    if (row < 0) return;
    QTableWidgetItem *idItem = m_table->item(row, 1);
    if (!idItem) return;

    QString pid = idItem->text();
    PluginMetadata meta = PluginManager::instance().plugin(pid);
    PluginManager::instance().setPluginActive(pid, !meta.active);
    updateDetailsPane(pid);
}

void PluginManagerDialog::onRunActionClicked() {
    QString action = m_actionCombo->currentText();
    if (!action.isEmpty()) {
        PluginManager::instance().triggerToolbarAction(action);
    }
}

void PluginManagerDialog::onCreateSamplePluginClicked() {
    QString samplePath = QDir(AppPaths::pluginsDir()).filePath(QStringLiteral("dialogue_checker.py"));
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
                                 tr("Created and loaded dialogue_checker.py"));
    }
}

} // namespace EnsemblePlugin
