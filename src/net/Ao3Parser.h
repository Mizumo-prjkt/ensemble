#pragma once

#include "Ao3Types.h"

#include <QByteArray>
#include <QList>
#include <QString>

class Ao3Parser
{
public:
    static QString parseAuthenticityToken(const QByteArray &html);
    static QString parseUsername(const QByteArray &html);
    static QList<Ao3Pseud> parsePseudsList(const QByteArray &html);
    static QList<Ao3WorkSummary> parseWorksList(const QByteArray &html);
    static QString parseNextPageUrl(const QByteArray &html);
    static QList<Ao3WorkSkin> parseSkinsList(const QByteArray &html);
    static QString parseSkinCss(const QByteArray &html);
    static Ao3FullWork parseFullWork(const QByteArray &html);
};
