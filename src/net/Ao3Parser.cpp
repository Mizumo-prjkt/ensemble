#include "Ao3Parser.h"

#include <QRegularExpression>
#include <QSet>

QString Ao3Parser::parseAuthenticityToken(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    QRegularExpression re(QStringLiteral("name=[\"']authenticity_token[\"']\\s+value=[\"']([^\"']+)[\"']"));
    auto match = re.match(html);
    if (!match.hasMatch()) {
        QRegularExpression re2(QStringLiteral("value=[\"']([^\"']+)[\"']\\s+name=[\"']authenticity_token[\"']"));
        match = re2.match(html);
    }
    return match.hasMatch() ? match.captured(1) : QString();
}

QString Ao3Parser::parseUsername(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);

    // 1. Match logged-in header greeting: id="greeting" ... href="/users/USERNAME" ... Hi, USERNAME!
    QRegularExpression reGreeting(QStringLiteral("id=[\"']greeting[\"'][\\s\\S]*?href=[\"']/users/([^/\"']+)[\"']"));
    auto matchG = reGreeting.match(html);
    if (matchG.hasMatch()) {
        return matchG.captured(1);
    }

    // 2. Match logged-in user profile/preferences link in navbar dropdown
    QRegularExpression reProfile(QStringLiteral("href=[\"']/users/([^/\"']+)/(?:profile|preferences|subscriptions)[\"']"));
    auto matchP = reProfile.match(html);
    if (matchP.hasMatch()) {
        return matchP.captured(1);
    }

    // 3. Match "Hi, USERNAME!" in text
    QRegularExpression reHi(QStringLiteral("Hi,\\s*([A-Za-z0-9_-]+)!"));
    auto matchH = reHi.match(html);
    if (matchH.hasMatch()) {
        return matchH.captured(1);
    }

    return QString();
}

static QString sanitizeTagText(QString tag)
{
    tag.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    tag.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    tag.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    tag.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    tag.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return tag.trimmed();
}

static QStringList extractTagsFromDd(const QString &html, const QString &ddClass)
{
    QStringList result;
    QRegularExpression reDd(QStringLiteral("<dd[^>]+class=[\"'][^\"']*") + ddClass + QStringLiteral("[^\"']*tags[^\"']*[\"'][^>]*>(.*?)</dd>"),
                            QRegularExpression::DotMatchesEverythingOption);
    const auto match = reDd.match(html);
    if (match.hasMatch()) {
        QRegularExpression reTag(QStringLiteral("<a[^>]+class=[\"']tag[\"'][^>]*>([^<]+)</a>"));
        auto it = reTag.globalMatch(match.captured(1));
        while (it.hasNext()) {
            const QString tag = sanitizeTagText(it.next().captured(1));
            if (!tag.isEmpty() && !result.contains(tag)) {
                result.append(tag);
            }
        }
    }
    return result;
}

static QStringList extractAuthors(const QString &html)
{
    QStringList authors;
    QRegularExpression reByline(QStringLiteral("<h3[^>]+class=[\"'][^\"']*byline[^\"']*heading[^\"']*[\"'][^>]*>(.*?)</h3>"),
                                QRegularExpression::DotMatchesEverythingOption);
    const auto bMatch = reByline.match(html);
    const QString searchArea = bMatch.hasMatch() ? bMatch.captured(1) : html;
    QRegularExpression reAuthor(QStringLiteral("<a[^>]+rel=[\"']author[\"'][^>]*>([^<]+)</a>"));
    auto it = reAuthor.globalMatch(searchArea);
    while (it.hasNext()) {
        const QString author = sanitizeTagText(it.next().captured(1));
        if (!author.isEmpty() && !authors.contains(author)) {
            authors.append(author);
        }
    }
    return authors;
}

QList<Ao3Pseud> Ao3Parser::parsePseudsList(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    QList<Ao3Pseud> pseuds;
    QSet<QString> seen;

    QRegularExpression re(QStringLiteral("<a[^>]+href=[\"']/users/[^/]+/pseuds/([^\"']+)[\"'][^>]*>([^<]+)</a>"));
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString name = match.captured(2).trimmed();
        const QString slug = match.captured(1).trimmed();
        if (!name.isEmpty() && !seen.contains(name) &&
            !name.contains(QStringLiteral("Edit"), Qt::CaseInsensitive) &&
            !name.contains(QStringLiteral("New"), Qt::CaseInsensitive) &&
            !name.contains(QStringLiteral("Delete"), Qt::CaseInsensitive)) {
            seen.insert(name);
            Ao3Pseud p;
            p.name = name;
            p.url = QStringLiteral("/users/%1/pseuds/%2").arg(slug, slug);
            p.isDefault = html.contains(name + QStringLiteral(" (default)"));
            pseuds.append(p);
        }
    }
    return pseuds;
}

QList<Ao3WorkSummary> Ao3Parser::parseWorksList(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    QList<Ao3WorkSummary> works;

    // Split HTML by work blocks: <li ... id="work_12345" ...>
    QRegularExpression reSplit(QStringLiteral("<li[^>]+id=[\"']work_(\\d+)[\"']"));
    auto itSplit = reSplit.globalMatch(html);

    QList<int> workIds;
    QList<int> matchPositions;
    while (itSplit.hasNext()) {
        const auto match = itSplit.next();
        workIds.append(match.captured(1).toInt());
        matchPositions.append(match.capturedStart());
    }

    for (int i = 0; i < workIds.size(); ++i) {
        const int workId = workIds.at(i);
        const int startPos = matchPositions.at(i);
        const int endPos = (i + 1 < matchPositions.size()) ? matchPositions.at(i + 1) : html.length();
        const QString block = html.mid(startPos, endPos - startPos);

        Ao3WorkSummary w;
        w.workId = workId;

        // Title: <a href="/works/12345">Title</a>
        QRegularExpression reTitle(QStringLiteral("<a[^>]+href=[\"']/works/") + QString::number(workId) + QStringLiteral("[\"'][^>]*>([^<]+)</a>"));
        const auto tMatch = reTitle.match(block);
        w.title = tMatch.hasMatch() ? tMatch.captured(1).trimmed() : QStringLiteral("Work %1").arg(workId);

        // Pseud / Author: rel="author">PseudName</a>
        QStringList authors;
        QRegularExpression reAuthor(QStringLiteral("rel=[\"']author[\"'][^>]*>([^<]+)</a>"));
        auto itA = reAuthor.globalMatch(block);
        while (itA.hasNext()) {
            authors.append(itA.next().captured(1).trimmed());
        }
        w.pseud = authors.join(QStringLiteral(", "));
        if (w.pseud.isEmpty()) {
            w.pseud = QStringLiteral("Anonymous");
        }

        // Fandom: <h5 class="fandoms heading">...<a class="tag">FandomName</a>
        QStringList fandomList;
        QRegularExpression reFandomBlock(QStringLiteral("<h5[^>]+class=[\"'][^\"']*fandoms[^\"']*[\"'][^>]*>(.*?)</h5>"), QRegularExpression::DotMatchesEverythingOption);
        const auto fBlockMatch = reFandomBlock.match(block);
        if (fBlockMatch.hasMatch()) {
            QRegularExpression reFandomTag(QStringLiteral("<a[^>]+class=[\"']tag[\"'][^>]*>([^<]+)</a>"));
            auto itF = reFandomTag.globalMatch(fBlockMatch.captured(1));
            while (itF.hasNext()) {
                fandomList.append(itF.next().captured(1).trimmed());
            }
        }
        w.fandom = fandomList.join(QStringLiteral(", "));
        if (w.fandom.isEmpty()) {
            w.fandom = QStringLiteral("Unspecified");
        }

        // Words: <dd class="words">178,816</dd>
        QRegularExpression reWords(QStringLiteral("<dd[^>]+class=[\"']words[\"'][^>]*>([0-9,]+)</dd>"));
        const auto wMatch = reWords.match(block);
        if (wMatch.hasMatch()) {
            QString numStr = wMatch.captured(1);
            numStr.remove(QLatin1Char(','));
            w.wordCount = numStr.toInt();
        } else {
            w.wordCount = 0;
        }

        // Chapters: <dd class="chapters">...23/?...</dd>
        QRegularExpression reChap(QStringLiteral("<dd[^>]+class=[\"']chapters[\"'][^>]*>(?:<a[^>]*>)?(\\d+)(?:</a>)?/(\\d+|\\?)"));
        const auto cMatch = reChap.match(block);
        if (cMatch.hasMatch()) {
            w.chapterCount = cMatch.captured(1).toInt();
            const QString totStr = cMatch.captured(2);
            w.totalChapters = (totStr == QStringLiteral("?")) ? -1 : totStr.toInt();
        } else {
            w.chapterCount = 1;
            w.totalChapters = 1;
        }

        // Tags: warnings, relationships, characters, freeform
        QRegularExpression reTagsList(QStringLiteral("<li[^>]*class=[\"']([^\"']+)[\"'][^>]*>\\s*<a[^>]*class=[\"']tag[\"'][^>]*>([^<]+)</a>"));
        auto itTag = reTagsList.globalMatch(block);
        while (itTag.hasNext()) {
            const auto m = itTag.next();
            const QString cls = m.captured(1);
            const QString tagText = sanitizeTagText(m.captured(2));
            if (cls.contains(QStringLiteral("warning"))) {
                if (!w.archiveWarnings.contains(tagText)) w.archiveWarnings.append(tagText);
            } else if (cls.contains(QStringLiteral("relationship"))) {
                if (!w.relationships.contains(tagText)) w.relationships.append(tagText);
            } else if (cls.contains(QStringLiteral("character"))) {
                if (!w.characters.contains(tagText)) w.characters.append(tagText);
            } else if (cls.contains(QStringLiteral("freeform"))) {
                if (!w.tags.contains(tagText)) w.tags.append(tagText);
            }
        }

        // Required tags (rating & category)
        QRegularExpression reReqRating(QStringLiteral("<span[^>]+class=[\"'][^\"']*rating-([a-z-]+)[^\"']*[\"'][^>]*title=[\"']([^\"']+)[\"']"));
        const auto rMatch = reReqRating.match(block);
        if (rMatch.hasMatch()) {
            w.rating = rMatch.captured(2).trimmed();
        }

        QRegularExpression reReqCat(QStringLiteral("<span[^>]+class=[\"'][^\"']*category-([a-z-]+)[^\"']*[\"'][^>]*title=[\"']([^\"']+)[\"']"));
        const auto cCatMatch = reReqCat.match(block);
        if (cCatMatch.hasMatch()) {
            const QString catTitle = cCatMatch.captured(2).trimmed();
            for (const QString &c : catTitle.split(QLatin1Char(','))) {
                const QString trimmedCat = c.trimmed();
                if (!trimmedCat.isEmpty() && !w.categories.contains(trimmedCat)) {
                    w.categories.append(trimmedCat);
                }
            }
        }

        works.append(w);
    }

    return works;
}

QString Ao3Parser::parseNextPageUrl(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    QRegularExpression re(QStringLiteral("<a[^>]+rel=[\"']next[\"'][^>]+href=[\"']([^\"']+)[\"']"));
    const auto match = re.match(html);
    return match.hasMatch() ? match.captured(1) : QString();
}

QList<Ao3WorkSkin> Ao3Parser::parseSkinsList(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    QList<Ao3WorkSkin> skins;

    QRegularExpression reSkin(QStringLiteral("<li[^>]+id=[\"']skin_(\\d+)[\"'][^>]*>(.*?)</li>"),
                              QRegularExpression::DotMatchesEverythingOption);
    auto it = reSkin.globalMatch(html);
    while (it.hasNext()) {
        const auto match = it.next();
        Ao3WorkSkin skin;
        skin.skinId = match.captured(1).toInt();
        const QString block = match.captured(2);

        QRegularExpression reTitle(QStringLiteral("<h4[^>]*>\\s*<a[^>]*>([^<]+)</a>"));
        const auto tMatch = reTitle.match(block);
        skin.name = tMatch.hasMatch() ? tMatch.captured(1).trimmed() : QStringLiteral("Skin %1").arg(skin.skinId);
        skin.isWorkSkin = block.contains(QStringLiteral("WorkSkin"), Qt::CaseInsensitive) || block.contains(QStringLiteral("work skin"), Qt::CaseInsensitive);

        skins.append(skin);
    }
    return skins;
}

QString Ao3Parser::parseSkinCss(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    QRegularExpression re(QStringLiteral("<textarea[^>]+id=[\"']skin_css[\"'][^>]*>(.*?)</textarea>"),
                          QRegularExpression::DotMatchesEverythingOption);
    const auto match = re.match(html);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    QRegularExpression re2(QStringLiteral("<pre[^>]*>(.*?)</pre>"), QRegularExpression::DotMatchesEverythingOption);
    const auto match2 = re2.match(html);
    return match2.hasMatch() ? match2.captured(1).trimmed() : QString();
}

Ao3FullWork Ao3Parser::parseFullWork(const QByteArray &htmlData)
{
    const QString html = QString::fromUtf8(htmlData);
    Ao3FullWork fullWork;

    // Extract Work Title
    QRegularExpression reTitleHeader(QStringLiteral("<h2[^>]*class=[\"'][^\"']*title[^\"']*[\"'][^>]*>\\s*(.*?)\\s*</h2>"),
                                      QRegularExpression::DotMatchesEverythingOption);
    const auto tHeaderMatch = reTitleHeader.match(html);
    if (tHeaderMatch.hasMatch()) {
        fullWork.summary.title = tHeaderMatch.captured(1).remove(QRegularExpression(QStringLiteral("<[^>]+>"))).trimmed();
    }

    // Extract Work Author / Pseud
    const QStringList authors = extractAuthors(html);
    if (!authors.isEmpty()) {
        fullWork.summary.pseud = authors.join(QStringLiteral(", "));
    }

    // Extract Rating
    const QStringList ratings = extractTagsFromDd(html, QStringLiteral("rating"));
    if (!ratings.isEmpty()) {
        fullWork.summary.rating = ratings.first();
    } else {
        QRegularExpression reRate(QStringLiteral("<dd[^>]+class=[\"'][^\"']*rating[^\"']*tags[^\"']*[\"'][^>]*>(.*?)</dd>"),
                                  QRegularExpression::DotMatchesEverythingOption);
        const auto rMatch = reRate.match(html);
        if (rMatch.hasMatch()) {
            fullWork.summary.rating = sanitizeTagText(rMatch.captured(1).remove(QRegularExpression(QStringLiteral("<[^>]+>"))));
        }
    }
    if (fullWork.summary.rating.isEmpty()) {
        fullWork.summary.rating = QStringLiteral("Not Rated");
    }

    // Extract Archive Warnings
    fullWork.summary.archiveWarnings = extractTagsFromDd(html, QStringLiteral("warning"));

    // Extract Categories
    fullWork.summary.categories = extractTagsFromDd(html, QStringLiteral("category"));

    // Extract Fandoms
    fullWork.summary.fandoms = extractTagsFromDd(html, QStringLiteral("fandom"));
    if (!fullWork.summary.fandoms.isEmpty()) {
        fullWork.summary.fandom = fullWork.summary.fandoms.join(QStringLiteral(", "));
    }

    // Extract Relationships
    fullWork.summary.relationships = extractTagsFromDd(html, QStringLiteral("relationship"));

    // Extract Characters
    fullWork.summary.characters = extractTagsFromDd(html, QStringLiteral("character"));

    // Extract Freeform Additional Tags
    fullWork.summary.tags = extractTagsFromDd(html, QStringLiteral("freeform"));

    // Extract Summary Block
    QRegularExpression reSummary(QStringLiteral("<div[^>]+class=[\"'][^\"']*summary[^\"']*module[^\"']*[\"'][^>]*>\\s*<blockquote[^>]*class=[\"']userstuff[\"'][^>]*>(.*?)</blockquote>"),
                                 QRegularExpression::DotMatchesEverythingOption);
    const auto sumMatch = reSummary.match(html);
    if (sumMatch.hasMatch()) {
        fullWork.summary.summaryText = sumMatch.captured(1).remove(QRegularExpression(QStringLiteral("<[^>]+>"))).trimmed();
    }

    // Extract Work Skin CSS
    QRegularExpression reSkin(QStringLiteral("<style[^>]*id=[\"']workskin[\"'][^>]*>(.*?)</style>"),
                              QRegularExpression::DotMatchesEverythingOption);
    auto sMatch = reSkin.match(html);
    if (!sMatch.hasMatch()) {
        reSkin = QRegularExpression(QStringLiteral("<style[^>]*>(.*?)</style>"),
                                    QRegularExpression::DotMatchesEverythingOption);
        sMatch = reSkin.match(html);
    }
    if (sMatch.hasMatch()) {
        fullWork.workSkinCss = sMatch.captured(1).trimmed();
    }

    // Multi-Chapter Splitting
    QRegularExpression reChapSplit(QStringLiteral("<div[^>]+id=[\"']chapter-\\d+[\"'][^>]*>"));
    const QStringList blocks = html.split(reChapSplit);

    if (blocks.size() > 1) {
        for (int i = 1; i < blocks.size(); ++i) {
            const QString &block = blocks.at(i);
            Ao3ChapterContent chap;
            chap.chapterNumber = i;

            // Extract Chapter Title
            QRegularExpression reTitle(QStringLiteral("<h3[^>]*class=[\"']title[\"'][^>]*>\\s*<a[^>]*>[^<]+</a>:?\\s*(.*?)\\s*</h3>"),
                                       QRegularExpression::DotMatchesEverythingOption);
            const auto tMatch = reTitle.match(block);
            if (tMatch.hasMatch()) {
                chap.title = tMatch.captured(1).remove(QRegularExpression(QStringLiteral("<[^>]+>"))).trimmed();
            }
            if (chap.title.isEmpty()) {
                chap.title = QStringLiteral("Chapter %1").arg(i);
            }

            // Extract Chapter Body
            int startIdx = block.indexOf(QStringLiteral("role=\"article\""));
            if (startIdx == -1) startIdx = block.indexOf(QStringLiteral("class=\"userstuff"));
            if (startIdx != -1) {
                int bodyStart = block.indexOf(QLatin1Char('>'), startIdx);
                if (bodyStart != -1) {
                    bodyStart += 1;
                    int bodyEnd = block.length();
                    static const QStringList endTerms = {
                        QStringLiteral("<div class=\"chapter preface"),
                        QStringLiteral("<div id=\"work_endnotes\""),
                        QStringLiteral("<div id=\"feedback\""),
                        QStringLiteral("<!--chapter end-->"),
                        QStringLiteral("<!--/chapter-->")
                    };
                    for (const auto &term : endTerms) {
                        int pos = block.indexOf(term, bodyStart);
                        if (pos != -1 && pos < bodyEnd) {
                            bodyEnd = pos;
                        }
                    }
                    QString body = block.mid(bodyStart, bodyEnd - bodyStart).trimmed();
                    body.remove(QRegularExpression(QStringLiteral("^\\s*<h3[^>]*class=[\"']landmark[^\"'\\w]*heading[\"'][^>]*>.*?</h3>"),
                                                  QRegularExpression::DotMatchesEverythingOption));
                    body.remove(QRegularExpression(QStringLiteral(R"(<!--[\s\S]*?-->)")));
                    chap.bodyHtml = body.trimmed();
                }
            }

            fullWork.chapters.append(chap);
        }
    } else {
        // Fallback for single chapter work
        Ao3ChapterContent chap;
        chap.chapterNumber = 1;
        chap.title = fullWork.summary.title.isEmpty() ? QStringLiteral("Chapter 1") : fullWork.summary.title;

        QRegularExpression reBody(QStringLiteral("<div[^>]+class=[\"']userstuff[^\"']*[\"'][^>]*>(.*?)</div>"),
                                  QRegularExpression::DotMatchesEverythingOption);
        const auto bMatch = reBody.match(html);
        QString body = bMatch.hasMatch() ? bMatch.captured(1).trimmed() : QString();
        body.remove(QRegularExpression(QStringLiteral(R"(<!--[\s\S]*?-->)")));
        chap.bodyHtml = body.trimmed();
        fullWork.chapters.append(chap);
    }

    // Scan all chapters for embedded <img> tags
    static const QRegularExpression reImg(
        QStringLiteral(R"(<img\b([^>]*?)\/?>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSrc(
        QStringLiteral(R"(src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reAlt(
        QStringLiteral(R"(alt\s*=\s*["']([^"']*)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTitle(
        QStringLiteral(R"(title\s*=\s*["']([^"']*)["'])"),
        QRegularExpression::CaseInsensitiveOption);

    for (const auto &chap : fullWork.chapters) {
        auto itImg = reImg.globalMatch(chap.bodyHtml);
        while (itImg.hasNext()) {
            const QString tagAttrs = itImg.next().captured(1);
            const auto mSrc = reSrc.match(tagAttrs);
            if (mSrc.hasMatch()) {
                Ao3ImageEmbed embed;
                embed.url = mSrc.captured(1).trimmed();
                const auto mAlt = reAlt.match(tagAttrs);
                if (mAlt.hasMatch()) {
                    embed.alt = sanitizeTagText(mAlt.captured(1));
                }
                const auto mTit = reTitle.match(tagAttrs);
                if (mTit.hasMatch()) {
                    embed.title = sanitizeTagText(mTit.captured(1));
                }
                embed.chapterNumber = chap.chapterNumber;
                fullWork.embeddedImages.append(embed);
            }
        }
    }

    return fullWork;
}
