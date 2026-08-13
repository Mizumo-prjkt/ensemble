#include "Ao3Client.h"
#include "Ao3Parser.h"
#include "Ao3Session.h"

#include <QFile>
#include <QNetworkReply>
#include <QUrlQuery>

Ao3Client::Ao3Client(Ao3Session *session, QObject *parent)
    : QObject(parent), m_session(session)
{
}

void Ao3Client::fetchPseuds()
{
    if (!m_session || !m_session->isAuthenticated()) {
        emit errorOccurred(QStringLiteral("User is not authenticated with AO3."));
        return;
    }

    const QUrl url(QStringLiteral("https://archiveofourown.org/users/%1/pseuds").arg(m_session->username()));
    QNetworkReply *reply = m_session->authenticatedGet(url);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_activeReplies.removeOne(reply);
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray html = reply->readAll();
            const QList<Ao3Pseud> pseuds = Ao3Parser::parsePseudsList(html);
            emit pseudsFetched(pseuds);
        } else {
            emit errorOccurred(QStringLiteral("Failed to fetch pseuds: ") + reply->errorString());
        }
        reply->deleteLater();
    });
}

void Ao3Client::fetchWorksList(const QString &pseud, bool includeDrafts)
{
    if (!m_session || !m_session->isAuthenticated()) {
        qWarning() << "[Ao3Client] fetchWorksList failed: User is not authenticated.";
        emit errorOccurred(QStringLiteral("User is not authenticated with AO3."));
        return;
    }

    QString path;
    if (!pseud.isEmpty()) {
        path = QStringLiteral("https://archiveofourown.org/users/%1/pseuds/%2/works").arg(m_session->username(), pseud);
    } else {
        path = QStringLiteral("https://archiveofourown.org/users/%1/works").arg(m_session->username());
    }

    const QUrl url(path);
    qDebug() << "[Ao3Client] Fetching works list from URL:" << url.toString();
    QNetworkReply *reply = m_session->authenticatedGet(url);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, includeDrafts]() {
        m_activeReplies.removeOne(reply);
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "[Ao3Client] GET works reply finished. HTTP Status:" << statusCode << "Error:" << reply->error();

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray html = reply->readAll();
            qDebug() << "[Ao3Client] Read" << html.size() << "bytes of HTML from works endpoint.";

            QFile dumpFile(QStringLiteral("/tmp/ao3_response.html"));
            if (dumpFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                dumpFile.write(html);
                dumpFile.close();
                qDebug() << "[Ao3Client] Dumped response HTML to /tmp/ao3_response.html";
            }

            QList<Ao3WorkSummary> works = Ao3Parser::parseWorksList(html);
            qDebug() << "[Ao3Client] Parsed" << works.size() << "published works.";

            if (includeDrafts) {
                const QUrl draftsUrl(QStringLiteral("https://archiveofourown.org/users/%1/drafts").arg(m_session->username()));
                qDebug() << "[Ao3Client] Fetching drafts from URL:" << draftsUrl.toString();
                QNetworkReply *draftReply = m_session->authenticatedGet(draftsUrl);
                m_activeReplies.append(draftReply);

                connect(draftReply, &QNetworkReply::finished, this, [this, draftReply, works]() mutable {
                    m_activeReplies.removeOne(draftReply);
                    const int dStatus = draftReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    qDebug() << "[Ao3Client] GET drafts reply finished. HTTP Status:" << dStatus << "Error:" << draftReply->error();

                    if (draftReply->error() == QNetworkReply::NoError) {
                        const QByteArray dHtml = draftReply->readAll();
                        qDebug() << "[Ao3Client] Read" << dHtml.size() << "bytes of HTML from drafts endpoint.";
                        QList<Ao3WorkSummary> drafts = Ao3Parser::parseWorksList(dHtml);
                        qDebug() << "[Ao3Client] Parsed" << drafts.size() << "drafts.";
                        for (auto &d : drafts) {
                            d.isDraft = true;
                            works.append(d);
                        }
                    }
                    qDebug() << "[Ao3Client] Total works emitted:" << works.size();
                    emit worksListFetched(works);
                    draftReply->deleteLater();
                });
            } else {
                qDebug() << "[Ao3Client] Total works emitted (published only):" << works.size();
                emit worksListFetched(works);
            }
        } else {
            qWarning() << "[Ao3Client] Failed to fetch works list:" << reply->errorString();
            emit errorOccurred(QStringLiteral("Failed to fetch works list: ") + reply->errorString());
        }
        reply->deleteLater();
    });
}

void Ao3Client::fetchWorkSkins()
{
    if (!m_session || !m_session->isAuthenticated()) {
        emit errorOccurred(QStringLiteral("User is not authenticated with AO3."));
        return;
    }

    const QUrl url(QStringLiteral("https://archiveofourown.org/users/%1/skins").arg(m_session->username()));
    QNetworkReply *reply = m_session->authenticatedGet(url);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_activeReplies.removeOne(reply);
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray html = reply->readAll();
            const QList<Ao3WorkSkin> skins = Ao3Parser::parseSkinsList(html);
            emit skinsFetched(skins);
        } else {
            emit errorOccurred(QStringLiteral("Failed to fetch work skins: ") + reply->errorString());
        }
        reply->deleteLater();
    });
}

void Ao3Client::fetchSkinCss(int skinId)
{
    if (!m_session || !m_session->isAuthenticated()) {
        emit errorOccurred(QStringLiteral("User is not authenticated with AO3."));
        return;
    }

    const QUrl url(QStringLiteral("https://archiveofourown.org/skins/%1").arg(skinId));
    QNetworkReply *reply = m_session->authenticatedGet(url);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, skinId]() {
        m_activeReplies.removeOne(reply);
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray html = reply->readAll();
            const QString css = Ao3Parser::parseSkinCss(html);
            emit skinCssFetched(skinId, css);
        } else {
            emit errorOccurred(QStringLiteral("Failed to fetch skin CSS: ") + reply->errorString());
        }
        reply->deleteLater();
    });
}

void Ao3Client::fetchFullWork(int workId)
{
    if (!m_session || !m_session->isAuthenticated()) {
        emit errorOccurred(QStringLiteral("User is not authenticated with AO3."));
        return;
    }

    const QUrl url(QStringLiteral("https://archiveofourown.org/works/%1?view_full_work=true").arg(workId));
    QNetworkReply *reply = m_session->authenticatedGet(url);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, workId]() {
        m_activeReplies.removeOne(reply);
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray html = reply->readAll();
            Ao3FullWork fullWork = Ao3Parser::parseFullWork(html);
            fullWork.summary.workId = workId;
            emit fullWorkFetched(fullWork);
        } else {
            emit errorOccurred(QStringLiteral("Failed to fetch full work: ") + reply->errorString());
        }
        reply->deleteLater();
    });
}

void Ao3Client::cancelAll()
{
    for (auto *reply : m_activeReplies) {
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeReplies.clear();
}
