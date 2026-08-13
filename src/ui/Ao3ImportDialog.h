#pragma once

#include "net/Ao3AuthServer.h"
#include "net/Ao3Client.h"
#include "net/Ao3Session.h"
#include "net/Ao3Types.h"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>

class MainWindow;

class Ao3ImportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit Ao3ImportDialog(MainWindow *mainWindow, QWidget *parent = nullptr);
    ~Ao3ImportDialog() override;

private slots:
    void onDisclaimerCountdownTick();
    void onAgreeDisclaimerClicked();
    void onSessionReceived(const QString &sessionCookie, const QString &username);
    void onConnectCookieClicked();
    void onLoginSucceeded(const QString &username);
    void onLoginFailed(const QString &reason);

    void onPseudsFetched(const QList<Ao3Pseud> &pseuds);
    void onWorksListFetched(const QList<Ao3WorkSummary> &works);
    void onSkinsFetched(const QList<Ao3WorkSkin> &skins);
    void onFullWorkFetched(const Ao3FullWork &work);
    void onErrorOccurred(const QString &message);

    void onPseudChanged(int index);
    void onFilterChanged(int index);
    void onStartImportClicked();
    void onCancelImportClicked();

private:
    void setupUi();
    QWidget *createDisclaimerStep();
    QWidget *createLoginStep();
    QWidget *createDashboardStep();
    QWidget *createConfigStep();
    QWidget *createProgressStep();

    void populateWorksTable(const QList<Ao3WorkSummary> &works);

    MainWindow *m_mainWindow = nullptr;
    Ao3Session *m_session = nullptr;
    Ao3Client *m_client = nullptr;
    Ao3AuthServer *m_authServer = nullptr;

    QStackedWidget *m_stackedWidget = nullptr;

    // Step 0: Disclaimer
    QPushButton *m_agreeBtn = nullptr;
    QTimer *m_countdownTimer = nullptr;
    int m_disclaimerCountdown = 15;

    // Step 1: Login (browser-based auth & cookie paste)
    QLabel *m_loginStatusLabel = nullptr;
    QPushButton *m_openBrowserBtn = nullptr;
    QPushButton *m_openHelperBtn = nullptr;
    QLineEdit *m_cookieInputEdit = nullptr;
    QPushButton *m_connectCookieBtn = nullptr;

    // Step 2: Dashboard
    QLabel *m_userLabel = nullptr;
    QComboBox *m_pseudCombo = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QTableWidget *m_worksTable = nullptr;
    QList<Ao3WorkSummary> m_currentWorks;
    QList<Ao3WorkSkin> m_currentSkins;

    // Step 3: Config
    QLineEdit *m_destDirEdit = nullptr;
    QComboBox *m_importModeCombo = nullptr;

    // Step 4: Progress
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressStatusLabel = nullptr;
    QTextEdit *m_logEdit = nullptr;

    QList<int> m_selectedWorkIds;
    int m_currentImportIndex = 0;
};
