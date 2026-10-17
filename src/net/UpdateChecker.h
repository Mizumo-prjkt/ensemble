#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

struct ReleaseInfo
{
    QString tagName;
    QString version;
    QString name;
    QString htmlUrl;
    QString body;
    QDateTime publishedAt;
    bool isUpdateAvailable = false;
};

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    void checkForUpdates();
    void cancel();

    QUrl apiUrl() const { return m_apiUrl; }
    void setApiUrl(const QUrl &url) { m_apiUrl = url; }

    QString currentVersion() const { return m_currentVersion; }
    void setCurrentVersion(const QString &version) { m_currentVersion = version; }

    static bool isVersionNewer(const QString &remoteVer, const QString &currentVer);
    static QList<int> parseVersionParts(const QString &verStr);
    static ReleaseInfo parseReleaseJson(const QByteArray &jsonData, const QString &currentVer);

signals:
    void updateCheckStarted();
    void updateCheckFinished(const ReleaseInfo &info);
    void updateCheckError(const QString &errorMessage);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QUrl m_apiUrl;
    QString m_currentVersion;
};
