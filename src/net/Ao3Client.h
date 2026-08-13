#pragma once

#include "Ao3Parser.h"
#include "Ao3Session.h"
#include "Ao3Types.h"

#include <QList>
#include <QObject>
#include <QUrl>

class Ao3Client : public QObject
{
    Q_OBJECT
public:
    explicit Ao3Client(Ao3Session *session, QObject *parent = nullptr);
    ~Ao3Client() override = default;

    void fetchPseuds();
    void fetchWorksList(const QString &pseud = QString(), bool includeDrafts = false);
    void fetchWorkSkins();
    void fetchSkinCss(int skinId);
    void fetchFullWork(int workId);
    void cancelAll();

signals:
    void pseudsFetched(const QList<Ao3Pseud> &pseuds);
    void worksListFetched(const QList<Ao3WorkSummary> &works);
    void skinsFetched(const QList<Ao3WorkSkin> &skins);
    void skinCssFetched(int skinId, const QString &css);
    void fullWorkFetched(const Ao3FullWork &work);
    void errorOccurred(const QString &message);

private:
    Ao3Session *m_session = nullptr;
    QList<QNetworkReply *> m_activeReplies;
};
