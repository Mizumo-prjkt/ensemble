#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <QUrl>

enum class RateLimitMode
{
    SingleRequest = 10, // 10s minimum delay
    BulkExtraction = 60 // 60s minimum delay
};

class Ao3Session : public QObject
{
    Q_OBJECT
public:
    explicit Ao3Session(QObject *parent = nullptr);
    ~Ao3Session() override = default;

    bool isAuthenticated() const { return !m_rawCookieHeader.isEmpty(); }
    QString username() const { return m_username; }
    void setUsername(const QString &user) { m_username = user; }
    QString rawCookieHeader() const { return m_rawCookieHeader; }

    void setCookieFromWebEngine(const QNetworkCookie &cookie);
    void setCookieFromString(const QString &cookieValue);
    void verifyAndSetCookie(const QString &rawInput, const QString &providedUser = QString());
    QNetworkReply *authenticatedGet(const QUrl &url);

    void setRateLimitMode(RateLimitMode mode) { m_rateLimitMode = mode; }
    RateLimitMode rateLimitMode() const { return m_rateLimitMode; }

    void logout();

signals:
    void loginSucceeded(const QString &username);
    void loginFailed(const QString &reason);

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkCookieJar *m_cookieJar = nullptr;
    QString m_username;
    QString m_rawCookieHeader;
    RateLimitMode m_rateLimitMode = RateLimitMode::SingleRequest;
    qint64 m_lastRequestTimeMs = 0;
};
