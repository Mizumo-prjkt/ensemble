#include "net/Ao3Parser.h"
#include "net/Ao3Session.h"
#include "net/Ao3Client.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qDebug() << "Usage: ./test_ao3_fetch <_otwarchive_session_cookie_value>";
        return 1;
    }

    const QString cookieVal = QString::fromUtf8(argv[1]);
    qDebug() << "Testing session cookie:" << cookieVal.left(20) << "...";

    Ao3Session session;
    session.verifyAndSetCookie(cookieVal);

    Ao3Client client(&session);

    QObject::connect(&session, &Ao3Session::loginSucceeded, [&]() {
        qDebug() << "Login Succeeded! Username:" << session.username();

        QObject::connect(&client, &Ao3Client::worksListFetched, [&](const QList<Ao3WorkSummary> &works) {
            qDebug() << "Works List Fetched! Count:" << works.size();
            for (const auto &w : works) {
                qDebug() << "  ID:" << w.workId << "Title:" << w.title << "Pseud:" << w.pseud << "Words:" << w.wordCount << "Chapters:" << w.chapterCount;
            }
            app.quit();
        });

        QObject::connect(&client, &Ao3Client::errorOccurred, [&](const QString &msg) {
            qCritical() << "Error occurred:" << msg;
            app.quit();
        });

        client.fetchWorksList(QString(), true);
    });

    QObject::connect(&session, &Ao3Session::loginFailed, [&](const QString &reason) {
        qCritical() << "Login Failed:" << reason;
        app.quit();
    });

    // Timeout safety
    QTimer::singleShot(15000, &app, [&]() {
        qCritical() << "Test timed out after 15 seconds!";
        app.quit();
    });

    return app.exec();
}
