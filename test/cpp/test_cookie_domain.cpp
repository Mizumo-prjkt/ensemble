#include <QCoreApplication>
#include <QDebug>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QUrl>
#include <cassert>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QNetworkCookieJar jar;
    const QUrl url(QStringLiteral("https://archiveofourown.org/users/carrisa_lyna/works"));

    // Case 1: Leading dot (.archiveofourown.org)
    QNetworkCookie c1;
    c1.setName("_otwarchive_session");
    c1.setValue("VALUE_1");
    c1.setDomain(QStringLiteral(".archiveofourown.org"));
    c1.setPath(QStringLiteral("/"));
    jar.setCookiesFromUrl({c1}, url);

    QList<QNetworkCookie> res1 = jar.cookiesForUrl(url);
    qDebug() << "With leading dot (.archiveofourown.org):" << res1.size() << "cookies found.";

    // Case 2: Without leading dot (archiveofourown.org)
    QNetworkCookie c2;
    c2.setName("_otwarchive_session");
    c2.setValue("VALUE_2");
    c2.setDomain(QStringLiteral("archiveofourown.org"));
    c2.setPath(QStringLiteral("/"));
    jar.setCookiesFromUrl({c2}, url);

    QList<QNetworkCookie> res2 = jar.cookiesForUrl(url);
    qDebug() << "Without leading dot (archiveofourown.org):" << res2.size() << "cookies found.";

    // Case 3: Empty domain (defaults to host)
    QNetworkCookie c3;
    c3.setName("_otwarchive_session");
    c3.setValue("VALUE_3");
    c3.setPath(QStringLiteral("/"));
    jar.setCookiesFromUrl({c3}, url);

    QList<QNetworkCookie> res3 = jar.cookiesForUrl(url);
    qDebug() << "With empty domain:" << res3.size() << "cookies found.";

    return 0;
}
