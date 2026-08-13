#include "Ao3Session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QNetworkCookie>
#include <QThread>

Ao3Session::Ao3Session(QObject *parent) : QObject(parent) {
  m_nam = new QNetworkAccessManager(this);
  m_cookieJar = new QNetworkCookieJar(this);
  m_nam->setCookieJar(m_cookieJar);
}

#include "Ao3Parser.h"
#include <QRegularExpression>

void Ao3Session::setCookieFromWebEngine(const QNetworkCookie &cookie) {
  if (cookie.name() == "_otwarchive_session") {
    QList<QNetworkCookie> cookies = m_cookieJar->cookiesForUrl(
        QUrl(QStringLiteral("https://archiveofourown.org")));
    cookies.append(cookie);
    m_cookieJar->setCookiesFromUrl(
        cookies, QUrl(QStringLiteral("https://archiveofourown.org")));
    emit loginSucceeded(m_username);
  }
}

void Ao3Session::setCookieFromString(const QString &rawCookieHeader) {
  m_rawCookieHeader = rawCookieHeader.trimmed();

  QList<QNetworkCookie> cookies;
  const QStringList pairs = m_rawCookieHeader.split(QLatin1Char(';'));
  for (const QString &pair : pairs) {
    const QString trimmed = pair.trimmed();
    const int eqIdx = trimmed.indexOf(QLatin1Char('='));
    if (eqIdx > 0) {
      const QString name = trimmed.left(eqIdx).trimmed();
      QString value = trimmed.mid(eqIdx + 1).trimmed();
      if (value.startsWith('"') && value.endsWith('"')) {
        value = value.mid(1, value.length() - 2);
      }

      QNetworkCookie cookie;
      cookie.setName(name.toUtf8());
      cookie.setValue(value.toUtf8());
      cookie.setPath(QStringLiteral("/"));
      cookie.setSecure(true);
      cookies.append(cookie);
    } else if (!trimmed.isEmpty()) {
      QNetworkCookie cookie;
      cookie.setName("_otwarchive_session");
      cookie.setValue(trimmed.toUtf8());
      cookie.setPath(QStringLiteral("/"));
      cookie.setSecure(true);
      cookies.append(cookie);
    }
  }

  m_cookieJar->setCookiesFromUrl(
      cookies, QUrl(QStringLiteral("https://archiveofourown.org")));
  m_cookieJar->setCookiesFromUrl(
      cookies, QUrl(QStringLiteral("https://archiveofourown.org/users/login")));
}

void Ao3Session::verifyAndSetCookie(const QString &rawInput,
                                    const QString &providedUser) {
  const QString cleaned = rawInput.trimmed();
  if (cleaned.isEmpty()) {
    emit loginFailed(QStringLiteral("Cookie string cannot be empty."));
    return;
  }

  setCookieFromString(cleaned);

  if (!providedUser.isEmpty()) {
    m_username = providedUser;
    emit loginSucceeded(m_username);
    return;
  }

  // Verify against AO3 homepage to extract username
  QNetworkReply *reply =
      authenticatedGet(QUrl(QStringLiteral("https://archiveofourown.org/")));
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    if (reply->error() == QNetworkReply::NoError) {
      const QByteArray html = reply->readAll();
      const QString htmlStr = QString::fromUtf8(html);

      if (htmlStr.contains(QStringLiteral("Lost Cookie Home"))) {
        qWarning() << "[Ao3Session] AO3 returned Lost Cookie Home page.";
        emit loginFailed(
            QStringLiteral("AO3 rejected session cookie (Lost Cookie Home). "
                           "Please log in to AO3 in your browser and copy your "
                           "fresh _otwarchive_session value."));
      } else {
        const QString user = Ao3Parser::parseUsername(html);
        if (!user.isEmpty()) {
          m_username = user;
          qDebug() << "[Ao3Session] Login verified successfully for user:"
                   << m_username;
          emit loginSucceeded(m_username);
        } else {
          emit loginFailed(
              QStringLiteral("Connected to AO3, but could not detect logged-in "
                             "username. Please check cookie validity."));
        }
      }
    } else {
      emit loginFailed(QStringLiteral("Failed to connect to AO3: ") +
                       reply->errorString());
    }
    reply->deleteLater();
  });
}

QNetworkReply *Ao3Session::authenticatedGet(const QUrl &url) {
  m_lastRequestTimeMs = QDateTime::currentMSecsSinceEpoch();

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setRawHeader(
      "User-Agent",
      "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/120.0.0.0 Safari/537.36 Ensemble/1.1.0");
  request.setRawHeader(
      "Accept",
      "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
  request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
  request.setRawHeader("Accept-Encoding", "identity");

  if (!m_rawCookieHeader.isEmpty()) {
    request.setRawHeader("Cookie", m_rawCookieHeader.toUtf8());
  }

  return m_nam->get(request);
}

void Ao3Session::logout() {
  m_cookieJar->setCookiesFromUrl(
      QList<QNetworkCookie>(),
      QUrl(QStringLiteral("https://archiveofourown.org")));
  m_username.clear();
}
