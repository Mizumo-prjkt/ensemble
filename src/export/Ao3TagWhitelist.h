#pragma once

#include <QHash>
#include <QSet>
#include <QString>

namespace Ao3TagWhitelist {

inline QSet<QString> allowedTags()
{
    return {
        QStringLiteral("a"), QStringLiteral("abbr"), QStringLiteral("acronym"),
        QStringLiteral("address"), QStringLiteral("b"), QStringLiteral("big"),
        QStringLiteral("blockquote"), QStringLiteral("br"), QStringLiteral("caption"),
        QStringLiteral("center"), QStringLiteral("cite"), QStringLiteral("code"),
        QStringLiteral("col"), QStringLiteral("colgroup"), QStringLiteral("dd"),
        QStringLiteral("del"), QStringLiteral("details"), QStringLiteral("dfn"),
        QStringLiteral("div"), QStringLiteral("dl"), QStringLiteral("dt"),
        QStringLiteral("em"), QStringLiteral("figcaption"), QStringLiteral("figure"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("hr"), QStringLiteral("i"), QStringLiteral("img"),
        QStringLiteral("ins"), QStringLiteral("kbd"), QStringLiteral("li"),
        QStringLiteral("ol"), QStringLiteral("p"), QStringLiteral("pre"),
        QStringLiteral("q"), QStringLiteral("rp"), QStringLiteral("rt"),
        QStringLiteral("ruby"), QStringLiteral("s"), QStringLiteral("samp"),
        QStringLiteral("small"), QStringLiteral("span"), QStringLiteral("strike"),
        QStringLiteral("strong"), QStringLiteral("sub"), QStringLiteral("summary"),
        QStringLiteral("sup"), QStringLiteral("table"), QStringLiteral("tbody"),
        QStringLiteral("td"), QStringLiteral("tfoot"), QStringLiteral("th"),
        QStringLiteral("thead"), QStringLiteral("title"), QStringLiteral("tr"),
        QStringLiteral("tt"), QStringLiteral("u"), QStringLiteral("ul"),
        QStringLiteral("var"),
    };
}

inline QSet<QString> allowedAttributes()
{
    return {
        QStringLiteral("align"), QStringLiteral("alt"), QStringLiteral("axis"),
        QStringLiteral("class"), QStringLiteral("height"), QStringLiteral("href"),
        QStringLiteral("name"), QStringLiteral("src"), QStringLiteral("title"),
        QStringLiteral("width"),
    };
}

inline QHash<QString, QString> tagNormalizations()
{
    return {
        {QStringLiteral("b"), QStringLiteral("strong")},
        {QStringLiteral("i"), QStringLiteral("em")},
        {QStringLiteral("strike"), QStringLiteral("s")},
    };
}

} // namespace Ao3TagWhitelist
