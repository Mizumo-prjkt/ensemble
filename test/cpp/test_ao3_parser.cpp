#include "net/Ao3Parser.h"

#include <QFile>
#include <QCoreApplication>
#include <QDebug>
#include <cassert>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QFile file(QStringLiteral("/home/miku/Documents/ao3-typewriter/test/fixtures/login_page_sample.html"));
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open login_page_sample.html fixture!";
        return 1;
    }

    const QByteArray html = file.readAll();
    const QString token = Ao3Parser::parseAuthenticityToken(html);
    qDebug() << "Extracted authenticity_token:" << token;

    assert(!token.isEmpty());
    assert(token.length() > 10);

    qDebug() << "All C++ Ao3Parser Unit Tests Passed!";
    return 0;
}
