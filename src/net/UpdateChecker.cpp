#include "UpdateChecker.h"

#include "debug/debug.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_apiUrl(QStringLiteral("https://api.github.com/repos/Mizumo-prjkt/ensemble/releases/latest"))
{
#ifdef ENSEMBLE_APP_VERSION
    m_currentVersion = QStringLiteral(ENSEMBLE_APP_VERSION);
#else
    m_currentVersion = QStringLiteral("1.1.1");
#endif
}

UpdateChecker::~UpdateChecker()
{
    cancel();
}

void UpdateChecker::cancel()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

void UpdateChecker::checkForUpdates()
{
    cancel();

    QNetworkRequest request(m_apiUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Ensemble-Updater/1.0 (Desktop)"));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    emit updateCheckStarted();

    m_currentReply = m_nam->get(request);
    connect(m_currentReply, &QNetworkReply::finished, this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::onReplyFinished()
{
    if (!m_currentReply)
        return;

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        if (statusCode == 404) {
            err = QStringLiteral("No GitHub releases found for Mizumo-prjkt/ensemble.");
        } else if (statusCode == 403) {
            err = QStringLiteral("GitHub API rate limit exceeded or access denied. Please try again later.");
        }
        emit updateCheckError(err);
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    ReleaseInfo info = parseReleaseJson(data, m_currentVersion);
    if (info.tagName.isEmpty()) {
        emit updateCheckError(QStringLiteral("Failed to parse release information from GitHub."));
        return;
    }

    emit updateCheckFinished(info);
}

QList<int> UpdateChecker::parseVersionParts(const QString &verStr)
{
    QList<int> parts;
    static const QRegularExpression re(QStringLiteral(R"((\d+)(?:\.(\d+))?(?:\.(\d+))?)"));
    const QRegularExpressionMatch m = re.match(verStr);
    if (m.hasMatch()) {
        parts << m.captured(1).toInt();
        parts << (!m.captured(2).isEmpty() ? m.captured(2).toInt() : 0);
        parts << (!m.captured(3).isEmpty() ? m.captured(3).toInt() : 0);
    } else {
        parts << 0 << 0 << 0;
    }
    return parts;
}

bool UpdateChecker::isVersionNewer(const QString &remoteVer, const QString &currentVer)
{
    QList<int> remote = parseVersionParts(remoteVer);
    QList<int> current = parseVersionParts(currentVer);

    while (remote.size() < 3) remote.append(0);
    while (current.size() < 3) current.append(0);

    for (int i = 0; i < 3; ++i) {
        if (remote[i] > current[i])
            return true;
        if (remote[i] < current[i])
            return false;
    }

    return false;
}

ReleaseInfo UpdateChecker::parseReleaseJson(const QByteArray &jsonData, const QString &currentVer)
{
    ReleaseInfo info;
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return info;
    }

    const QJsonObject obj = doc.object();
    info.tagName = obj.value(QStringLiteral("tag_name")).toString().trimmed();
    info.name = obj.value(QStringLiteral("name")).toString().trimmed();
    info.htmlUrl = obj.value(QStringLiteral("html_url")).toString().trimmed();
    info.body = obj.value(QStringLiteral("body")).toString();

    const QString pubDateStr = obj.value(QStringLiteral("published_at")).toString();
    if (!pubDateStr.isEmpty()) {
        info.publishedAt = QDateTime::fromString(pubDateStr, Qt::ISODate);
    }

    if (info.htmlUrl.isEmpty() && !info.tagName.isEmpty()) {
        info.htmlUrl = QStringLiteral("https://github.com/Mizumo-prjkt/ensemble/releases/tag/%1").arg(info.tagName);
    } else if (info.htmlUrl.isEmpty()) {
        info.htmlUrl = QStringLiteral("https://github.com/Mizumo-prjkt/ensemble/releases/latest");
    }

    info.version = info.tagName;
    if (info.version.startsWith(QLatin1Char('v')) || info.version.startsWith(QLatin1Char('V'))) {
        info.version = info.version.mid(1);
    }

    info.isUpdateAvailable = isVersionNewer(info.version, currentVer);
    return info;
}
