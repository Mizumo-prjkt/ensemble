#include "ResourceManagerDialog.h"
#include "model/Ao3Project.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginPermissions.h"
#include "ui/PluginManagerDialog.h"
#include "diagnostics/ProjectDiagnosticsRunner.h"
#include "diagnostics/MainThreadWatchdog.h"
#include "themes/themes.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

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

struct SysProcInfo {
    qint64 pid = 0;
    qint64 rssBytes = 0;
    qint64 vmSizeBytes = 0;
    int threadsCount = 1;
    double cpuPercent = 0.0;
};

static unsigned long long s_prevProcTicks = 0;
static qint64 s_prevTimeMs = 0;

SysProcInfo readProcessInfo()
{
    SysProcInfo info;
#if defined(Q_OS_LINUX)
    info.pid = static_cast<qint64>(getpid());
    QFile statusFile(QStringLiteral("/proc/self/status"));
    if (statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&statusFile);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(QStringLiteral("VmRSS:"))) {
                QString val = line.section(':', 1).trimmed().split(' ').value(0);
                info.rssBytes = val.toLongLong() * 1024;
            } else if (line.startsWith(QStringLiteral("VmSize:"))) {
                QString val = line.section(':', 1).trimmed().split(' ').value(0);
                info.vmSizeBytes = val.toLongLong() * 1024;
            } else if (line.startsWith(QStringLiteral("Threads:"))) {
                QString val = line.section(':', 1).trimmed();
                info.threadsCount = val.toInt();
            }
        }
    }

    // Read CPU ticks from /proc/self/stat
    QFile statFile(QStringLiteral("/proc/self/stat"));
    if (statFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(statFile.readAll());
        int closeParen = content.lastIndexOf(QLatin1Char(')'));
        if (closeParen != -1) {
            QString rest = content.mid(closeParen + 1).trimmed();
            QStringList tokens = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (tokens.size() > 12) {
                unsigned long long utime = tokens[11].toULongLong();
                unsigned long long stime = tokens[12].toULongLong();
                unsigned long long totalTicks = utime + stime;

                qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (s_prevTimeMs > 0 && nowMs > s_prevTimeMs && totalTicks >= s_prevProcTicks) {
                    double elapsedSec = (nowMs - s_prevTimeMs) / 1000.0;
                    long clkTck = sysconf(_SC_CLK_TCK);
                    if (clkTck <= 0) clkTck = 100;
                    double ticksDiff = totalTicks - s_prevProcTicks;
                    double cpuSec = ticksDiff / static_cast<double>(clkTck);
                    long numCpus = sysconf(_SC_NPROCESSORS_ONLN);
                    if (numCpus <= 0) numCpus = 1;
                    info.cpuPercent = qBound(0.0, (cpuSec / elapsedSec) * 100.0, 100.0 * numCpus);
                }
                s_prevProcTicks = totalTicks;
                s_prevTimeMs = nowMs;
            }
        }
    }
#elif defined(Q_OS_WIN)
    info.pid = static_cast<qint64>(QCoreApplication::applicationPid());
    FILETIME creationTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER k, u;
        k.LowPart = kernelTime.dwLowDateTime;
        k.HighPart = kernelTime.dwHighDateTime;
        u.LowPart = userTime.dwLowDateTime;
        u.HighPart = userTime.dwHighDateTime;
        unsigned long long total100ns = k.QuadPart + u.QuadPart;

        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (s_prevTimeMs > 0 && nowMs > s_prevTimeMs && total100ns >= s_prevProcTicks) {
            double elapsedMs = nowMs - s_prevTimeMs;
            double cpuMs = (total100ns - s_prevProcTicks) / 10000.0;
            info.cpuPercent = qBound(0.0, (cpuMs / elapsedMs) * 100.0, 100.0);
        }
        s_prevProcTicks = total100ns;
        s_prevTimeMs = nowMs;
    }
#else
    info.pid = static_cast<qint64>(QCoreApplication::applicationPid());
#endif
    return info;
}

} // namespace

ResourceManagerDialog::ResourceManagerDialog(Ao3Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(tr("Resource & Task Manager — Ensemble"));
    resize(820, 560);
    setMinimumSize(680, 440);

    setupUi();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &ResourceManagerDialog::refreshAll);
    m_refreshTimer->start();

    refreshAll();
    updateTheme();
}

ResourceManagerDialog::~ResourceManagerDialog()
{
    if (m_refreshTimer && m_refreshTimer->isActive()) {
        m_refreshTimer->stop();
    }
}

void ResourceManagerDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createOverviewTab(), tr("📊 Overview"));
    m_tabs->addTab(createDocumentTab(), tr("📖 Memory"));
    m_tabs->addTab(createPluginsTab(), tr("🧩 Plugins"));
    m_tabs->addTab(createNetworkTab(), tr("🌐 Network"));
    mainLayout->addWidget(m_tabs);

    // Bottom action bar
    auto *bottomBar = new QHBoxLayout();
    m_autoRefreshCheck = new QCheckBox(tr("Auto-refresh every 2 seconds"), this);
    m_autoRefreshCheck->setChecked(true);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, &ResourceManagerDialog::onAutoRefreshToggled);
    bottomBar->addWidget(m_autoRefreshCheck);

    bottomBar->addStretch();

    auto *refreshAllBtn = new QPushButton(tr("🔄 Refresh Now"), this);
    connect(refreshAllBtn, &QPushButton::clicked, this, &ResourceManagerDialog::refreshAll);
    bottomBar->addWidget(refreshAllBtn);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomBar->addWidget(closeBtn);

    mainLayout->addLayout(bottomBar);
}

QWidget *ResourceManagerDialog::createOverviewTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setSpacing(12);

    // 1. Core Process Box
    auto *procBox = new QGroupBox(tr("Ensemble Core Process (OS / Hardware)"), widget);
    auto *procLayout = new QGridLayout(procBox);
    procLayout->setSpacing(8);

    procLayout->addWidget(new QLabel(tr("<b>Process ID (PID):</b>"), procBox), 0, 0);
    m_pidLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_pidLabel, 0, 1);

    procLayout->addWidget(new QLabel(tr("<b>Resident RAM (RSS):</b>"), procBox), 0, 2);
    m_rssLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_rssLabel, 0, 3);

    procLayout->addWidget(new QLabel(tr("<b>Virtual Memory Size:</b>"), procBox), 1, 0);
    m_vmSizeLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_vmSizeLabel, 1, 1);

    procLayout->addWidget(new QLabel(tr("<b>Active OS Threads:</b>"), procBox), 1, 2);
    m_threadsLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_threadsLabel, 1, 3);

    procLayout->addWidget(new QLabel(tr("<b>Process CPU Usage:</b>"), procBox), 2, 0);
    m_cpuLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_cpuLabel, 2, 1);

    procLayout->addWidget(new QLabel(tr("<b>Diagnostics Worker (Core):</b>"), procBox), 2, 2);
    m_diagnosticsWorkerLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_diagnosticsWorkerLabel, 2, 3);

    procLayout->addWidget(new QLabel(tr("<b>GUI Thread Health:</b>"), procBox), 3, 0);
    m_guiWatchdogLabel = new QLabel(QStringLiteral("-"), procBox);
    procLayout->addWidget(m_guiWatchdogLabel, 3, 1, 1, 3);

    layout->addWidget(procBox);

    // 2. Document & Story Memory Box
    auto *docBox = new QGroupBox(tr("Active Document & Story In-Memory Data"), widget);
    auto *docLayout = new QGridLayout(docBox);
    docLayout->setSpacing(8);

    docLayout->addWidget(new QLabel(tr("<b>HTML Data in RAM:</b>"), docBox), 0, 0);
    m_inMemoryHtmlLabel = new QLabel(QStringLiteral("-"), docBox);
    docLayout->addWidget(m_inMemoryHtmlLabel, 0, 1);

    docLayout->addWidget(new QLabel(tr("<b>Total Story Text Size:</b>"), docBox), 0, 2);
    m_totalStoryHtmlLabel = new QLabel(QStringLiteral("-"), docBox);
    docLayout->addWidget(m_totalStoryHtmlLabel, 0, 3);

    docLayout->addWidget(new QLabel(tr("<b>Chapter RAM Residency:</b>"), docBox), 1, 0);
    m_chapterResidencyLabel = new QLabel(QStringLiteral("-"), docBox);
    docLayout->addWidget(m_chapterResidencyLabel, 1, 1);

    docLayout->addWidget(new QLabel(tr("<b>Work Skin CSS Size:</b>"), docBox), 1, 2);
    m_cssMemoryLabel = new QLabel(QStringLiteral("-"), docBox);
    docLayout->addWidget(m_cssMemoryLabel, 1, 3);

    docLayout->addWidget(new QLabel(tr("<b>Lore System Memory:</b>"), docBox), 2, 0);
    m_loreMemoryLabel = new QLabel(QStringLiteral("-"), docBox);
    docLayout->addWidget(m_loreMemoryLabel, 2, 1);

    auto *docBtnRow = new QHBoxLayout();
    m_trimOverviewBtn = new QPushButton(tr("⚡ Trim Inactive Chapters to Session Cache"), docBox);
    m_trimOverviewBtn->setToolTip(tr("Flushes inactive chapters to disk session cache and frees RAM memory."));
    connect(m_trimOverviewBtn, &QPushButton::clicked, this, &ResourceManagerDialog::onTrimChaptersClicked);
    docBtnRow->addWidget(m_trimOverviewBtn);
    docBtnRow->addStretch();
    docLayout->addLayout(docBtnRow, 3, 0, 1, 4);

    layout->addWidget(docBox);

    auto *infoLabel = new QLabel(
        tr("<i>💡 Smart Memory Note: Ensemble keeps only the currently active chapter loaded in full memory. "
           "Inactive chapters are seamlessly persisted to session cache and restored on demand when selected or saved.</i>"),
        widget);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    layout->addStretch();
    return widget;
}

QWidget *ResourceManagerDialog::createDocumentTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setSpacing(8);

    m_docSummaryLabel = new QLabel(tr("Active Document Chapters Status:"), widget);
    layout->addWidget(m_docSummaryLabel);

    m_docTable = new QTableWidget(widget);
    m_docTable->setColumnCount(5);
    m_docTable->setHorizontalHeaderLabels({tr("#"), tr("Chapter Title"), tr("RAM Status"), tr("HTML Content Size"), tr("Estimated RAM")});
    m_docTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_docTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_docTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_docTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_docTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_docTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_docTable);

    auto *btnRow = new QHBoxLayout();
    m_trimDocBtn = new QPushButton(tr("⚡ Trim Inactive Chapters Memory"), widget);
    connect(m_trimDocBtn, &QPushButton::clicked, this, &ResourceManagerDialog::onTrimChaptersClicked);
    btnRow->addWidget(m_trimDocBtn);

    m_refreshDocBtn = new QPushButton(tr("🔄 Refresh"), widget);
    connect(m_refreshDocBtn, &QPushButton::clicked, this, &ResourceManagerDialog::refreshDocumentMemory);
    btnRow->addWidget(m_refreshDocBtn);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    return widget;
}

QWidget *ResourceManagerDialog::createPluginsTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setSpacing(8);

    m_pluginSummaryLabel = new QLabel(tr("Installed Plugins & Memory Utilization:"), widget);
    layout->addWidget(m_pluginSummaryLabel);

    m_pluginTable = new QTableWidget(widget);
    m_pluginTable->setColumnCount(6);
    m_pluginTable->setHorizontalHeaderLabels({
        tr("Plugin ID"),
        tr("Name"),
        tr("Status"),
        tr("Heap Memory"),
        tr("Storage Used"),
        tr("Toolbar Actions")
    });
    m_pluginTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pluginTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pluginTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_pluginTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_pluginTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_pluginTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_pluginTable);

    auto *btnRow = new QHBoxLayout();
    m_refreshPluginBtn = new QPushButton(tr("🔄 Refresh Plugins"), widget);
    connect(m_refreshPluginBtn, &QPushButton::clicked, this, &ResourceManagerDialog::refreshPluginResources);
    btnRow->addWidget(m_refreshPluginBtn);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    return widget;
}

QWidget *ResourceManagerDialog::createNetworkTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setSpacing(8);

    m_networkSummaryLabel = new QLabel(
        tr("Active Plugin Network Connections & Bandwidth (HTTPS only, mediated by Ensemble):"),
        widget);
    layout->addWidget(m_networkSummaryLabel);

    m_noNetworkBanner = new QLabel(
        tr("<div style='padding: 24px; text-align: center; color: #888;'>"
           "<h3>No Plugins with Network Access</h3>"
           "<p>None of the currently loaded plugins have network capabilities or requested external URLs.<br/>"
           "Plugins without external URL requests are omitted from this activity monitor.</p>"
           "</div>"),
        widget);
    m_noNetworkBanner->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_noNetworkBanner);

    m_networkTable = new QTableWidget(widget);
    m_networkTable->setColumnCount(8);
    m_networkTable->setHorizontalHeaderLabels({
        tr("Plugin ID"),
        tr("Plugin Name"),
        tr("Permission"),
        tr("Allowed Domains"),
        tr("Requests"),
        tr("Data Sent"),
        tr("Data Received"),
        tr("Security Status")
    });
    m_networkTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_networkTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_networkTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_networkTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_networkTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_networkTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_networkTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_networkTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_networkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_networkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_networkTable);

    auto *btnRow = new QHBoxLayout();
    m_refreshNetworkBtn = new QPushButton(tr("🔄 Refresh Network"), widget);
    connect(m_refreshNetworkBtn, &QPushButton::clicked, this, &ResourceManagerDialog::refreshPluginNetwork);
    btnRow->addWidget(m_refreshNetworkBtn);

    m_openAuditLogBtn = new QPushButton(tr("📋 Open Security Audit Log..."), widget);
    connect(m_openAuditLogBtn, &QPushButton::clicked, this, [this]() {
        auto *dlg = new EnsemblePlugin::PluginManagerDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
        refreshAll();
    });
    btnRow->addWidget(m_openAuditLogBtn);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    return widget;
}

void ResourceManagerDialog::refreshAll()
{
    refreshProcessOverview();
    refreshDocumentMemory();
    refreshPluginResources();
    refreshPluginNetwork();
}

void ResourceManagerDialog::refreshProcessOverview()
{
    SysProcInfo proc = readProcessInfo();
    m_pidLabel->setText(QString::number(proc.pid));
    m_cpuLabel->setText(QStringLiteral("%1%").arg(QString::number(proc.cpuPercent, 'f', 1)));
    m_rssLabel->setText(proc.rssBytes > 0 ? formatBytes(proc.rssBytes) : tr("N/A"));
    m_vmSizeLabel->setText(proc.vmSizeBytes > 0 ? formatBytes(proc.vmSizeBytes) : tr("N/A"));
    m_threadsLabel->setText(QString::number(proc.threadsCount));

    auto &diag = ProjectDiagnosticsRunner::instance();
    QString diagText;
    if (diag.isRunning()) {
        diagText = tr("🟢 Running (Active Job)");
    } else {
        diagText = tr("⚪ Idle (Last run: %1 ms, Total: %2)")
                       .arg(diag.lastRunMs())
                       .arg(diag.totalRuns());
    }
    if (diag.terminationsCount() > 0) {
        diagText += tr(" ⚠️ %1 watchdog termination(s)").arg(diag.terminationsCount());
    }
    m_diagnosticsWorkerLabel->setText(diagText);

    if (m_guiWatchdogLabel) {
        m_guiWatchdogLabel->setText(MainThreadWatchdog::instance().statusDescription());
    }

    if (m_project) {
        qint64 inMemHtml = m_project->inMemoryHtmlBytes();
        qint64 totalStoryHtml = m_project->totalStoryHtmlBytes();
        int loadedChaps = m_project->loadedChaptersCount();
        int totalChaps = m_project->chapters().size();

        m_inMemoryHtmlLabel->setText(QStringLiteral("<b>%1</b> (%2 loaded)")
                                         .arg(formatBytes(inMemHtml))
                                         .arg(loadedChaps == 1 ? tr("1 chapter") : tr("%1 chapters").arg(loadedChaps)));

        m_totalStoryHtmlLabel->setText(formatBytes(totalStoryHtml));

        m_chapterResidencyLabel->setText(
            tr("%1 of %2 loaded in RAM (%3 cached to session storage)")
                .arg(loadedChaps)
                .arg(totalChaps)
                .arg(qMax(0, totalChaps - loadedChaps)));

        m_cssMemoryLabel->setText(formatBytes(m_project->workSkinMemoryBytes()));
        m_loreMemoryLabel->setText(formatBytes(m_project->loreMemoryBytes()));
    } else {
        m_inMemoryHtmlLabel->setText(tr("No active project"));
        m_totalStoryHtmlLabel->setText(QStringLiteral("-"));
        m_chapterResidencyLabel->setText(QStringLiteral("-"));
        m_cssMemoryLabel->setText(QStringLiteral("-"));
        m_loreMemoryLabel->setText(QStringLiteral("-"));
    }
}

void ResourceManagerDialog::refreshDocumentMemory()
{
    if (!m_project) {
        m_docTable->setRowCount(0);
        m_docSummaryLabel->setText(tr("No project currently opened."));
        return;
    }

    const auto &chaps = m_project->chapters();
    int activeIdx = m_project->activeChapterIndex();
    int loadedCount = m_project->loadedChaptersCount();
    int totalCount = chaps.size();

    m_docSummaryLabel->setText(
        tr("Document Chapters: %1 total, %2 loaded in RAM, %3 cached to session storage. Active Chapter: #%4")
            .arg(totalCount)
            .arg(loadedCount)
            .arg(qMax(0, totalCount - loadedCount))
            .arg(activeIdx + 1));

    m_docTable->setRowCount(totalCount);
    for (int i = 0; i < totalCount; ++i) {
        const Chapter &ch = chaps.at(i);
        bool isLoaded = ch.isLoaded();
        bool isActive = (i == activeIdx);

        // Col 0: Index
        auto *idxItem = new QTableWidgetItem(QString::number(i + 1));
        idxItem->setTextAlignment(Qt::AlignCenter);
        m_docTable->setItem(i, 0, idxItem);

        // Col 1: Title
        QString titleStr = ch.title().isEmpty() ? tr("Untitled Chapter") : ch.title();
        if (isActive) titleStr += tr(" (Active)");
        auto *titleItem = new QTableWidgetItem(titleStr);
        if (isActive) {
            QFont f = titleItem->font();
            f.setBold(true);
            titleItem->setFont(f);
        }
        m_docTable->setItem(i, 1, titleItem);

        // Col 2: RAM Status
        QString statusStr = isLoaded ? tr("🟢 Loaded in RAM") : tr("⚪ Session Cache");
        auto *statusItem = new QTableWidgetItem(statusStr);
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_docTable->setItem(i, 2, statusItem);

        // Col 3: HTML Content Size
        qint64 contentSize = ch.htmlByteSize();
        auto *sizeItem = new QTableWidgetItem(formatBytes(contentSize));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_docTable->setItem(i, 3, sizeItem);

        // Col 4: Memory Usage
        qint64 memSize = ch.memoryUsageBytes();
        auto *memItem = new QTableWidgetItem(formatBytes(memSize));
        memItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_docTable->setItem(i, 4, memItem);
    }
}

void ResourceManagerDialog::refreshPluginResources()
{
    const auto plugins = EnsemblePlugin::PluginManager::instance().plugins();
    m_pluginTable->setRowCount(plugins.size());
    m_pluginSummaryLabel->setText(tr("Total Installed Plugins: %1").arg(plugins.size()));

    for (int i = 0; i < plugins.size(); ++i) {
        const auto &p = plugins.at(i);

        // Col 0: ID
        auto *idItem = new QTableWidgetItem(p.publicId);
        m_pluginTable->setItem(i, 0, idItem);

        // Col 1: Name & Version
        auto *nameItem = new QTableWidgetItem(QStringLiteral("%1 (v%2)").arg(p.name, p.version));
        m_pluginTable->setItem(i, 1, nameItem);

        // Col 2: Status
        QString statusStr;
        if (p.quarantined) {
            statusStr = tr("🚨 Quarantined");
        } else if (p.faulted) {
            statusStr = tr("⚠️ Faulted");
        } else if (p.active) {
            statusStr = tr("🟢 Active");
        } else {
            statusStr = tr("⚪ Disabled");
        }
        auto *statusItem = new QTableWidgetItem(statusStr);
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_pluginTable->setItem(i, 2, statusItem);

        // Col 3: Heap Memory (Quota: 8.00 MiB)
        QString memText;
        if (!p.active) {
            memText = tr("%1 / 8.00 MiB (Disabled)").arg(formatBytes(p.memoryUsedBytes));
        } else if (p.faulted) {
            memText = tr("%1 / 8.00 MiB (Faulted)").arg(formatBytes(p.memoryUsedBytes));
        } else {
            memText = QStringLiteral("%1 / 8.00 MiB").arg(formatBytes(p.memoryUsedBytes));
        }
        auto *memItem = new QTableWidgetItem(memText);
        memItem->setTextAlignment(Qt::AlignCenter);
        m_pluginTable->setItem(i, 3, memItem);

        // Col 4: Storage Used (Quota: 5.00 MiB)
        QString storText = QStringLiteral("%1 / 5.00 MiB").arg(formatBytes(p.storageUsedBytes));
        auto *storItem = new QTableWidgetItem(storText);
        storItem->setTextAlignment(Qt::AlignCenter);
        m_pluginTable->setItem(i, 4, storItem);

        // Col 5: Actions
        auto *actItem = new QTableWidgetItem(QString::number(p.toolbarActions.size()));
        actItem->setTextAlignment(Qt::AlignCenter);
        m_pluginTable->setItem(i, 5, actItem);
    }
}

void ResourceManagerDialog::refreshPluginNetwork()
{
    // STRICT REQUIREMENT: Only list plugins that have network links or requested network capabilities!
    const auto allStats = EnsemblePlugin::PluginManager::instance().allNetworkStats();
    QList<EnsemblePlugin::PluginNetworkStats> netPlugins;

    for (const auto &st : allStats) {
        if (st.hasNetworkCapabilities || st.requestsCount > 0) {
            netPlugins.append(st);
        }
    }

    if (netPlugins.isEmpty()) {
        m_noNetworkBanner->setVisible(true);
        m_networkTable->setVisible(false);
        m_networkSummaryLabel->setText(
            tr("No plugins currently installed or active have requested external network capabilities or URLs."));
        return;
    }

    m_noNetworkBanner->setVisible(false);
    m_networkTable->setVisible(true);
    m_networkSummaryLabel->setText(
        tr("Active Network Plugins: %1 plugin(s) with network links.").arg(netPlugins.size()));

    m_networkTable->setRowCount(netPlugins.size());
    for (int i = 0; i < netPlugins.size(); ++i) {
        const auto &st = netPlugins.at(i);

        // Col 0: ID
        m_networkTable->setItem(i, 0, new QTableWidgetItem(st.publicId));

        // Col 1: Name
        m_networkTable->setItem(i, 1, new QTableWidgetItem(st.pluginName.isEmpty() ? st.publicId : st.pluginName));

        // Col 2: Permission
        QString permStr = st.allowNetwork ? tr("🟢 Allowed") : tr("⚪ Blocked / Disabled");
        auto *permItem = new QTableWidgetItem(permStr);
        permItem->setTextAlignment(Qt::AlignCenter);
        m_networkTable->setItem(i, 2, permItem);

        // Col 3: Allowed Domains
        QString domainsStr = st.allowedDomains.isEmpty() ? tr("None") : st.allowedDomains.join(QStringLiteral(", "));
        m_networkTable->setItem(i, 3, new QTableWidgetItem(domainsStr));

        // Col 4: Requests Count
        auto *reqItem = new QTableWidgetItem(QString::number(st.requestsCount));
        reqItem->setTextAlignment(Qt::AlignCenter);
        m_networkTable->setItem(i, 4, reqItem);

        // Col 5: Data Sent
        auto *sentItem = new QTableWidgetItem(formatBytes(st.bytesSent));
        sentItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_networkTable->setItem(i, 5, sentItem);

        // Col 6: Data Received
        auto *recvItem = new QTableWidgetItem(formatBytes(st.bytesReceived));
        recvItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_networkTable->setItem(i, 6, recvItem);

        // Col 7: Security Status
        QString secStatus = st.lastStatus.isEmpty() ? tr("🟢 Safe / Verified") : st.lastStatus;
        auto *secItem = new QTableWidgetItem(secStatus);
        secItem->setTextAlignment(Qt::AlignCenter);
        m_networkTable->setItem(i, 7, secItem);
    }
}

void ResourceManagerDialog::onTrimChaptersClicked()
{
    if (!m_project) return;
    int activeIdx = m_project->activeChapterIndex();
    m_project->trimInactiveChaptersMemory(activeIdx);
    refreshAll();
    QMessageBox::information(
        this,
        tr("Memory Trimmed"),
        tr("Inactive chapters have been unloaded from RAM to session storage.\n\n"
           "Current RAM Residency: %1 of %2 chapters loaded.")
            .arg(m_project->loadedChaptersCount())
            .arg(m_project->chapters().size()));
}

void ResourceManagerDialog::onAutoRefreshToggled(bool enabled)
{
    if (enabled) {
        if (!m_refreshTimer->isActive()) m_refreshTimer->start();
    } else {
        if (m_refreshTimer->isActive()) m_refreshTimer->stop();
    }
}

void ResourceManagerDialog::updateTheme()
{
    // Ensure table styling fits current theme
    if (m_docTable) {
        m_docTable->setAlternatingRowColors(true);
    }
    if (m_pluginTable) {
        m_pluginTable->setAlternatingRowColors(true);
    }
    if (m_networkTable) {
        m_networkTable->setAlternatingRowColors(true);
    }
}
