#pragma once

#include <QDialog>

class QLabel;
class QFrame;
class QPushButton;
class QVBoxLayout;
class UpdateChecker;
struct ReleaseInfo;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override;

public slots:
    void updateTheme();
    void checkForUpdates();

private slots:
    void onUpdateCheckStarted();
    void onUpdateCheckFinished(const ReleaseInfo &info);
    void onUpdateCheckError(const QString &errorMessage);
    void onOpenReleaseUrl();

private:
    void setupUi();

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_infoCard = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_versionBadge = nullptr;
    QLabel *m_subTitleLabel = nullptr;
    QLabel *m_aboutText = nullptr;
    QFrame *m_divider = nullptr;
    QLabel *m_creditsText = nullptr;
    QLabel *m_licenseText = nullptr;

    // Updater UI in About section
    QFrame *m_updateCard = nullptr;
    QLabel *m_updateStatusLabel = nullptr;
    QPushButton *m_checkUpdateBtn = nullptr;
    QPushButton *m_getUpdateBtn = nullptr;

    QPushButton *m_closeBtn = nullptr;

    UpdateChecker *m_updater = nullptr;
    QString m_latestReleaseUrl;
};