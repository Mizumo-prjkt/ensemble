#include "net/Ao3Session.h"
#include <QCoreApplication>
#include <QDebug>
#include <cassert>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    Ao3Session session;
    assert(!session.isAuthenticated());

    session.setCookieFromString("BAh7SUkiD3Nlc3Npb25faWQGOgZFVEkiRTExM2Y3");
    assert(session.isAuthenticated());

    qDebug() << "Ao3Session Cookie Jar Test Passed!";
    return 0;
}
