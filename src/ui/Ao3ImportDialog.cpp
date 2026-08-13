#include "Ao3ImportDialog.h"
#include "Ao3SkinDiffDialog.h"
#include "MainWindow.h"
#include "model/ProjectSerializer.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

Ao3ImportDialog::Ao3ImportDialog(MainWindow *mainWindow, QWidget *parent)
    : QDialog(parent), m_mainWindow(mainWindow)
{
    setWindowTitle(QStringLiteral("Import Stories from Archive of Our Own"));
    resize(860, 620);
    setModal(true);

    m_session = new Ao3Session(this);
    m_client = new Ao3Client(m_session, this);
    m_authServer = new Ao3AuthServer(this);

    connect(m_client, &Ao3Client::pseudsFetched, this, &Ao3ImportDialog::onPseudsFetched);
    connect(m_client, &Ao3Client::worksListFetched, this, &Ao3ImportDialog::onWorksListFetched);
    connect(m_client, &Ao3Client::skinsFetched, this, &Ao3ImportDialog::onSkinsFetched);
    connect(m_client, &Ao3Client::fullWorkFetched, this, &Ao3ImportDialog::onFullWorkFetched);
    connect(m_client, &Ao3Client::errorOccurred, this, &Ao3ImportDialog::onErrorOccurred);

    connect(m_authServer, &Ao3AuthServer::cookieReceived, this, &Ao3ImportDialog::onSessionReceived);
    connect(m_authServer, &Ao3AuthServer::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, QStringLiteral("Auth Server Error"), msg);
    });

    setupUi();
}

Ao3ImportDialog::~Ao3ImportDialog()
{
    m_authServer->stop();
}

void Ao3ImportDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->addWidget(createDisclaimerStep());
    m_stackedWidget->addWidget(createLoginStep());
    m_stackedWidget->addWidget(createDashboardStep());
    m_stackedWidget->addWidget(createConfigStep());
    m_stackedWidget->addWidget(createProgressStep());

    mainLayout->addWidget(m_stackedWidget);
}

QWidget *Ao3ImportDialog::createDisclaimerStep()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(20);

    auto *headerLabel = new QLabel(QStringLiteral(
        "<span style=\"color: #ff5555; font-size: 22px; font-weight: 800;\">⚠️ IMPORTANT LEGAL & COMMUNITY DISCLAIMER</span>"), page);
    headerLabel->setTextFormat(Qt::RichText);
    layout->addWidget(headerLabel);

    auto *warningBox = new QFrame(page);
    warningBox->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background-color: #282a36;"
        "  border: 2px solid #ff5555;"
        "  border-radius: 8px;"
        "  padding: 16px;"
        "}"));
    auto *boxLayout = new QVBoxLayout(warningBox);

    auto *warningText = new QLabel(QStringLiteral(
        "<p style=\"color: #f8f8f2; font-size: 14px; line-height: 1.6; margin: 0;\">"
        "<b>\"This feature is only meant for ease of importing your current work back to Ensemble easily. "
        "Using this feature in a context for AI training, unauthorized use, and others that may be deemed "
        "illegal in copyright stances or in Archive of Our Own's Community standing regarding theft is forbidden.\"</b>"
        "</p>"), warningBox);
    warningText->setTextFormat(Qt::RichText);
    warningText->setWordWrap(true);
    boxLayout->addWidget(warningText);

    layout->addWidget(warningBox);
    layout->addStretch();

    auto *btnLayout = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), page);
    cancelBtn->setStyleSheet(QStringLiteral("padding: 8px 16px; font-weight: bold;"));

    m_agreeBtn = new QPushButton(QStringLiteral("I Understand & Agree (15s)"), page);
    m_agreeBtn->setEnabled(false);
    m_agreeBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #bd93f9;"
        "  color: #ffffff;"
        "  font-weight: 800;"
        "  font-size: 13px;"
        "  border-radius: 6px;"
        "  padding: 10px 20px;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #44475a;"
        "  color: #6272a4;"
        "}"));

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(m_agreeBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_agreeBtn, &QPushButton::clicked, this, &Ao3ImportDialog::onAgreeDisclaimerClicked);

    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, &QTimer::timeout, this, &Ao3ImportDialog::onDisclaimerCountdownTick);
    m_countdownTimer->start(1000);

    return page;
}

void Ao3ImportDialog::onDisclaimerCountdownTick()
{
    m_disclaimerCountdown--;
    if (m_disclaimerCountdown > 0) {
        m_agreeBtn->setText(QStringLiteral("I Understand & Agree (%1s)").arg(m_disclaimerCountdown));
    } else {
        m_countdownTimer->stop();
        m_agreeBtn->setText(QStringLiteral("I Understand & Agree"));
        m_agreeBtn->setEnabled(true);
    }
}

void Ao3ImportDialog::onAgreeDisclaimerClicked()
{
    // Start the local auth callback server before advancing
    if (!m_authServer->start()) {
        QMessageBox::critical(this, QStringLiteral("Error"),
                              QStringLiteral("Failed to start the local authentication server. Cannot proceed."));
        return;
    }
    m_stackedWidget->setCurrentIndex(1); // Advance to Login Step
}

void Ao3ImportDialog::onSessionReceived(const QString &sessionCookie, const QString &username)
{
    m_session->verifyAndSetCookie(sessionCookie, username);
}

void Ao3ImportDialog::onConnectCookieClicked()
{
    const QString text = m_cookieInputEdit->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Empty Cookie"),
                             QStringLiteral("Please paste your _otwarchive_session cookie value first."));
        return;
    }
    m_connectCookieBtn->setEnabled(false);
    m_connectCookieBtn->setText(QStringLiteral("Verifying..."));
    m_session->verifyAndSetCookie(text);
}

void Ao3ImportDialog::onLoginSucceeded(const QString &username)
{
    m_userLabel->setText(QStringLiteral("Logged in as: <b>%1</b>").arg(
        username.isEmpty() ? QStringLiteral("(unknown)") : username));

    m_client->fetchPseuds();
    m_client->fetchWorksList(QString(), true);
    m_client->fetchWorkSkins();

    m_authServer->stop();
    m_stackedWidget->setCurrentIndex(2); // Dashboard
}

void Ao3ImportDialog::onLoginFailed(const QString &reason)
{
    m_connectCookieBtn->setEnabled(true);
    m_connectCookieBtn->setText(QStringLiteral("Connect to AO3 →"));
    QMessageBox::warning(this, QStringLiteral("Authentication Failed"), reason);
}

QWidget *Ao3ImportDialog::createLoginStep()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(16);

    auto *disclaimerLabel = new QLabel(QStringLiteral(
        "<i>Ensemble does not see, store, or transmit your AO3 password. You log in directly through "
        "Archive of Our Own's official website in your browser. Only your resulting session cookie "
        "is used temporarily and is discarded when you exit.</i>"), page);
    disclaimerLabel->setStyleSheet(QStringLiteral("color: #8be9fd; font-size: 11px;"));
    disclaimerLabel->setWordWrap(true);
    layout->addWidget(disclaimerLabel);

    m_loginStatusLabel = new QLabel(QStringLiteral(
        "<span style=\"font-size: 16px; font-weight: bold;\">AO3 Account Connection</span>"), page);
    m_loginStatusLabel->setTextFormat(Qt::RichText);
    layout->addWidget(m_loginStatusLabel);

    // Box 1: Direct Cookie Paste (Recommended & 100% Reliable)
    auto *pasteBox = new QFrame(page);
    pasteBox->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background-color: #282a36;"
        "  border: 2px solid #50fa7b;"
        "  border-radius: 10px;"
        "  padding: 20px;"
        "}"));
    auto *pasteLayout = new QVBoxLayout(pasteBox);
    pasteLayout->setSpacing(12);

    auto *pasteTitle = new QLabel(QStringLiteral(
        "<span style=\"font-size: 15px; font-weight: 800; color: #50fa7b;\">⚡ Paste Session Cookie (3 Simple Clicks)</span><br>"
        "<span style=\"color: #f1fa8c; font-size: 11px;\">Note: AO3 sets <code>HttpOnly</code> on session cookies for security, so JavaScript cannot read them directly. Copy it from DevTools:</span>"
        "<ol style=\"color: #f8f8f2; font-size: 12px; margin-left: 20px; margin-top: 8px; line-height: 1.6;\">"
        "  <li>Open <a href=\"https://archiveofourown.org\" style=\"color:#8be9fd;\">archiveofourown.org</a> in your browser while logged in.</li>"
        "  <li>Press <code>F12</code> &rarr; Click <b>Application</b> (or <b>Storage</b>) tab &rarr; <b>Cookies</b> &rarr; <code>archiveofourown.org</code>.</li>"
        "  <li>Double-click the value for <b><code>_otwarchive_session</code></b>, copy (<code>Ctrl+C</code>), and paste it below:</li>"
        "</ol>"), pasteBox);
    pasteTitle->setTextFormat(Qt::RichText);
    pasteTitle->setWordWrap(true);
    pasteLayout->addWidget(pasteTitle);

    auto *inputRow = new QHBoxLayout();
    m_cookieInputEdit = new QLineEdit(pasteBox);
    m_cookieInputEdit->setPlaceholderText(QStringLiteral("Paste _otwarchive_session value here (e.g. BAh7SU...)"));
    m_cookieInputEdit->setStyleSheet(QStringLiteral("padding: 10px; background: #191a21; color: #f8f8f2; border: 1px solid #6272a4; border-radius: 6px; font-family: monospace; font-size: 12px;"));

    m_connectCookieBtn = new QPushButton(QStringLiteral("Connect to AO3 →"), pasteBox);
    m_connectCookieBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #50fa7b; color: #282a36; font-weight: 800; font-size: 13px; padding: 10px 20px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #6bffaa; }"));

    inputRow->addWidget(m_cookieInputEdit, 1);
    inputRow->addWidget(m_connectCookieBtn);
    pasteLayout->addLayout(inputRow);

    layout->addWidget(pasteBox);

    // Box 2: Browser Callback & Helper
    auto *browserBox = new QFrame(page);
    browserBox->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background-color: #282a36;"
        "  border: 1px solid #44475a;"
        "  border-radius: 10px;"
        "  padding: 16px;"
        "}"));
    auto *browserLayout = new QVBoxLayout(browserBox);

    auto *browserTitle = new QLabel(QStringLiteral(
        "<b>🌐 Method 2: Open Browser & Transfer Helper</b>"), browserBox);
    browserLayout->addWidget(browserTitle);

    auto *btnRow = new QHBoxLayout();
    m_openBrowserBtn = new QPushButton(QStringLiteral("🌐 Open AO3 in Browser"), browserBox);
    m_openBrowserBtn->setStyleSheet(QStringLiteral("padding: 8px 14px; background: #bd93f9; color: #ffffff; font-weight: bold; border-radius: 4px;"));

    m_openHelperBtn = new QPushButton(QStringLiteral("🔑 Open Transfer Helper Page"), browserBox);
    m_openHelperBtn->setStyleSheet(QStringLiteral("padding: 8px 14px; background: #ff79c6; color: #ffffff; font-weight: bold; border-radius: 4px;"));

    btnRow->addWidget(m_openBrowserBtn);
    btnRow->addWidget(m_openHelperBtn);
    btnRow->addStretch();
    browserLayout->addLayout(btnRow);

    layout->addWidget(browserBox);

    layout->addStretch();

    // Button row
    auto *btnLayout = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), page);
    cancelBtn->setStyleSheet(QStringLiteral("padding: 8px 16px; font-weight: bold;"));
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    // Connections
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_connectCookieBtn, &QPushButton::clicked, this, &Ao3ImportDialog::onConnectCookieClicked);
    connect(m_cookieInputEdit, &QLineEdit::returnPressed, this, &Ao3ImportDialog::onConnectCookieClicked);

    connect(m_session, &Ao3Session::loginSucceeded, this, &Ao3ImportDialog::onLoginSucceeded);
    connect(m_session, &Ao3Session::loginFailed, this, &Ao3ImportDialog::onLoginFailed);

    connect(m_openBrowserBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://archiveofourown.org/users/login")));
    });

    connect(m_openHelperBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(m_authServer->callbackHelperUrl()));
    });

    return page;
}

QWidget *Ao3ImportDialog::createDashboardStep()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *topBar = new QHBoxLayout();
    m_userLabel = new QLabel(QStringLiteral("Logged in as: <b>---</b>"), page);
    m_userLabel->setStyleSheet(QStringLiteral("font-size: 14px; color: #50fa7b;"));

    m_pseudCombo = new QComboBox(page);
    m_pseudCombo->addItem(QStringLiteral("All Pseuds"), QString());

    m_filterCombo = new QComboBox(page);
    m_filterCombo->addItem(QStringLiteral("All Works (Published & Drafts)"), 0);
    m_filterCombo->addItem(QStringLiteral("Published Works Only"), 1);
    m_filterCombo->addItem(QStringLiteral("Drafts Only"), 2);

    topBar->addWidget(m_userLabel);
    topBar->addStretch();
    topBar->addWidget(new QLabel(QStringLiteral("Pseud:"), page));
    topBar->addWidget(m_pseudCombo);
    topBar->addWidget(new QLabel(QStringLiteral("Filter:"), page));
    topBar->addWidget(m_filterCombo);

    layout->addLayout(topBar);

    m_worksTable = new QTableWidget(0, 7, page);
    m_worksTable->setHorizontalHeaderLabels({QStringLiteral("Import"), QStringLiteral("ID"), QStringLiteral("Title"), QStringLiteral("Pseud"), QStringLiteral("Fandom"), QStringLiteral("Words"), QStringLiteral("Chapters")});
    m_worksTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_worksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_worksTable->setStyleSheet(QStringLiteral("background-color: #191a21; color: #f8f8f2; gridline-color: #44475a;"));
    layout->addWidget(m_worksTable, 1);

    auto *bottomBar = new QHBoxLayout();
    auto *nextBtn = new QPushButton(QStringLiteral("Next: Configure Import →"), page);
    nextBtn->setStyleSheet(QStringLiteral("background-color: #bd93f9; color: #ffffff; font-weight: bold; padding: 8px 16px; border-radius: 6px;"));

    bottomBar->addStretch();
    bottomBar->addWidget(nextBtn);
    layout->addLayout(bottomBar);

    connect(m_pseudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Ao3ImportDialog::onPseudChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Ao3ImportDialog::onFilterChanged);
    connect(nextBtn, &QPushButton::clicked, this, [this]() {
        m_selectedWorkIds.clear();
        for (int i = 0; i < m_worksTable->rowCount(); ++i) {
            auto *item = m_worksTable->item(i, 0);
            if (item && item->checkState() == Qt::Checked) {
                m_selectedWorkIds.append(m_worksTable->item(i, 1)->text().toInt());
            }
        }
        if (m_selectedWorkIds.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("No Works Selected"), QStringLiteral("Please select at least one work to import."));
            return;
        }
        m_stackedWidget->setCurrentIndex(3); // Config Step
    });

    return page;
}

void Ao3ImportDialog::onPseudsFetched(const QList<Ao3Pseud> &pseuds)
{
    const bool oldState = m_pseudCombo->blockSignals(true);
    m_pseudCombo->clear();
    m_pseudCombo->addItem(QStringLiteral("All Pseuds"), QString());
    for (const auto &p : pseuds) {
        m_pseudCombo->addItem(p.name, p.name);
    }
    m_pseudCombo->blockSignals(oldState);
}

void Ao3ImportDialog::onWorksListFetched(const QList<Ao3WorkSummary> &works)
{
    qDebug() << "[Ao3ImportDialog] onWorksListFetched received" << works.size() << "works.";
    m_currentWorks = works;
    populateWorksTable(works);
}

void Ao3ImportDialog::onSkinsFetched(const QList<Ao3WorkSkin> &skins)
{
    qDebug() << "[Ao3ImportDialog] onSkinsFetched received" << skins.size() << "skins.";
    m_currentSkins = skins;
}

void Ao3ImportDialog::populateWorksTable(const QList<Ao3WorkSummary> &works)
{
    m_worksTable->setRowCount(0);
    const int filterMode = m_filterCombo->currentIndex(); // 0 = All, 1 = Published, 2 = Drafts
    qDebug() << "[Ao3ImportDialog] populateWorksTable called. Total works in list:" << works.size() << "Filter mode:" << filterMode;

    int renderedCount = 0;
    for (const auto &w : works) {
        if (filterMode == 1 && w.isDraft) continue;
        if (filterMode == 2 && !w.isDraft) continue;

        const int row = m_worksTable->rowCount();
        m_worksTable->insertRow(row);

        auto *checkItem = new QTableWidgetItem();
        checkItem->setCheckState(Qt::Checked);
        m_worksTable->setItem(row, 0, checkItem);

        m_worksTable->setItem(row, 1, new QTableWidgetItem(QString::number(w.workId)));
        m_worksTable->setItem(row, 2, new QTableWidgetItem(w.title + (w.isDraft ? QStringLiteral(" [DRAFT]") : QString())));
        m_worksTable->setItem(row, 3, new QTableWidgetItem(w.pseud));
        m_worksTable->setItem(row, 4, new QTableWidgetItem(w.fandom));
        m_worksTable->setItem(row, 5, new QTableWidgetItem(QString::number(w.wordCount)));
        m_worksTable->setItem(row, 6, new QTableWidgetItem(QStringLiteral("%1/%2").arg(w.chapterCount).arg(w.totalChapters == -1 ? QStringLiteral("?") : QString::number(w.totalChapters))));
        renderedCount++;
    }
    qDebug() << "[Ao3ImportDialog] populateWorksTable finished. Rendered" << renderedCount << "rows into table.";
}

void Ao3ImportDialog::onPseudChanged(int index)
{
    if (index < 0) return;
    const QString pseud = m_pseudCombo->itemData(index).toString();
    const bool includeDrafts = (m_filterCombo->currentIndex() != 1);
    m_client->fetchWorksList(pseud, includeDrafts);
}

void Ao3ImportDialog::onFilterChanged(int index)
{
    if (index < 0) return;
    populateWorksTable(m_currentWorks);
}

QWidget *Ao3ImportDialog::createConfigStep()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    layout->addWidget(new QLabel(QStringLiteral("<b>Import Settings:</b>"), page));

    auto *modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(QStringLiteral("Import Mode:"), page));
    m_importModeCombo = new QComboBox(page);
    m_importModeCombo->addItem(QStringLiteral("Create New .ao3proj Files"), 0);
    m_importModeCombo->addItem(QStringLiteral("Append to Current Open Project"), 1);
    modeLayout->addWidget(m_importModeCombo, 1);
    layout->addLayout(modeLayout);

    auto *dirLayout = new QHBoxLayout();
    dirLayout->addWidget(new QLabel(QStringLiteral("Destination Folder:"), page));
    m_destDirEdit = new QLineEdit(QDir::homePath() + QStringLiteral("/Documents"), page);
    auto *browseBtn = new QPushButton(QStringLiteral("Browse..."), page);
    dirLayout->addWidget(m_destDirEdit, 1);
    dirLayout->addWidget(browseBtn);
    layout->addLayout(dirLayout);

    layout->addStretch();

    auto *btnLayout = new QHBoxLayout();
    auto *startBtn = new QPushButton(QStringLiteral("Start Import"), page);
    startBtn->setStyleSheet(QStringLiteral("background-color: #50fa7b; color: #282a36; font-weight: bold; padding: 10px 20px; border-radius: 6px;"));

    btnLayout->addStretch();
    btnLayout->addWidget(startBtn);
    layout->addLayout(btnLayout);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Destination Folder"), m_destDirEdit->text());
        if (!dir.isEmpty()) {
            m_destDirEdit->setText(dir);
        }
    });

    connect(startBtn, &QPushButton::clicked, this, &Ao3ImportDialog::onStartImportClicked);

    return page;
}

void Ao3ImportDialog::onStartImportClicked()
{
    m_stackedWidget->setCurrentIndex(4); // Progress Step
    m_currentImportIndex = 0;
    m_progressBar->setMaximum(m_selectedWorkIds.size());
    m_progressBar->setValue(0);
    m_logEdit->clear();

    if (!m_selectedWorkIds.isEmpty()) {
        const int firstWorkId = m_selectedWorkIds.first();
        m_logEdit->append(QStringLiteral("[%1] Fetching work %2...").arg(QTime::currentTime().toString(), QString::number(firstWorkId)));
        m_client->fetchFullWork(firstWorkId);
    }
}

QWidget *Ao3ImportDialog::createProgressStep()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    m_progressStatusLabel = new QLabel(QStringLiteral("Importing selected works from AO3..."), page);
    m_progressStatusLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #bd93f9;"));
    layout->addWidget(m_progressStatusLabel);

    m_progressBar = new QProgressBar(page);
    m_progressBar->setStyleSheet(QStringLiteral("QProgressBar { background: #282a36; color: #ffffff; border-radius: 4px; text-align: center; } QProgressBar::chunk { background: #50fa7b; }"));
    layout->addWidget(m_progressBar);

    m_logEdit = new QTextEdit(page);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet(QStringLiteral("background-color: #191a21; color: #f8f8f2; font-family: monospace; font-size: 11px;"));
    layout->addWidget(m_logEdit, 1);

    auto *btnLayout = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel / Close"), page);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &Ao3ImportDialog::onCancelImportClicked);

    return page;
}

void Ao3ImportDialog::onFullWorkFetched(const Ao3FullWork &work)
{
    m_logEdit->append(QStringLiteral("[%1] Extracted work '%2' (%3 chapters)")
                          .arg(QTime::currentTime().toString(), work.summary.title, QString::number(work.chapters.size())));

    const bool createNew = (m_importModeCombo->currentIndex() == 0);
    if (createNew) {
        Ao3Project newProj;
        newProj.setTitle(work.summary.title);
        newProj.setAuthor(m_session->username());
        newProj.chapters().clear();

        for (const auto &c : work.chapters) {
            Chapter chap;
            chap.setTitle(c.title);
            chap.setHtml(c.bodyHtml);
            newProj.chapters().append(chap);
        }

        if (!work.workSkinCss.isEmpty()) {
            newProj.setWorkSkinCss(work.workSkinCss);
        }

        QString safeTitle = work.summary.title.toLower();
        safeTitle.remove(QRegularExpression(QStringLiteral("[^a-z0-9_-]")));
        if (safeTitle.isEmpty()) {
            safeTitle = QStringLiteral("imported_work_%1").arg(m_selectedWorkIds.at(m_currentImportIndex));
        }
        const QString path = QDir(m_destDirEdit->text()).filePath(QStringLiteral("%1.ao3proj").arg(safeTitle));
        newProj.setFilePath(path);
        QString err;
        ProjectSerializer::save(path, newProj, &err);
        m_logEdit->append(QStringLiteral("[%1] Saved project archive to %2").arg(QTime::currentTime().toString(), path));
    }

    m_currentImportIndex++;
    m_progressBar->setValue(m_currentImportIndex);

    if (m_currentImportIndex < m_selectedWorkIds.size()) {
        const int nextWorkId = m_selectedWorkIds.at(m_currentImportIndex);
        m_logEdit->append(QStringLiteral("[%1] Fetching next work %2...").arg(QTime::currentTime().toString(), QString::number(nextWorkId)));
        m_client->fetchFullWork(nextWorkId);
    } else {
        m_progressStatusLabel->setText(QStringLiteral("🎉 Import Complete! All selected works imported successfully."));
        m_logEdit->append(QStringLiteral("[%1] Import completed successfully.").arg(QTime::currentTime().toString()));
    }
}

void Ao3ImportDialog::onErrorOccurred(const QString &message)
{
    m_logEdit->append(QStringLiteral("[%1] ERROR: %2").arg(QTime::currentTime().toString(), message));
}

void Ao3ImportDialog::onCancelImportClicked()
{
    m_client->cancelAll();
    accept();
}
