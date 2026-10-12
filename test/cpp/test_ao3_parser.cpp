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

    // Test parseFullWork metadata tags extraction
    const QByteArray sampleWorkHtml =
        "<h2 class=\"title heading\">The Secret of the Crystal</h2>\n"
        "<h3 class=\"byline heading\"><a rel=\"author\" href=\"/users/miku/pseuds/miku\">miku</a></h3>\n"
        "<dl class=\"work meta group\">\n"
        "  <dd class=\"rating tags\"><a class=\"tag\">Mature</a></dd>\n"
        "  <dd class=\"warning tags\"><a class=\"tag\">Graphic Depictions Of Violence</a></dd>\n"
        "  <dd class=\"category tags\"><a class=\"tag\">F/M</a>, <a class=\"tag\">Multi</a></dd>\n"
        "  <dd class=\"fandom tags\"><a class=\"tag\">Vocaloid &amp; Friends</a></dd>\n"
        "  <dd class=\"relationship tags\"><a class=\"tag\">Miku/Kaito</a></dd>\n"
        "  <dd class=\"character tags\"><a class=\"tag\">Hatsune Miku</a>, <a class=\"tag\">Kaito</a></dd>\n"
        "  <dd class=\"freeform tags\"><a class=\"tag\">Alternate Universe - Sci-Fi</a>, <a class=\"tag\">Time Travel</a></dd>\n"
        "</dl>\n"
        "<div class=\"userstuff\"><p>Chapter one body text.</p></div>\n";

    Ao3FullWork parsedWork = Ao3Parser::parseFullWork(sampleWorkHtml);
    assert(parsedWork.summary.title == "The Secret of the Crystal");
    assert(parsedWork.summary.pseud == "miku");
    assert(parsedWork.summary.rating == "Mature");
    assert(parsedWork.summary.archiveWarnings.contains("Graphic Depictions Of Violence"));
    assert(parsedWork.summary.categories.contains("F/M"));
    assert(parsedWork.summary.fandoms.contains("Vocaloid & Friends"));
    assert(parsedWork.summary.relationships.contains("Miku/Kaito"));
    assert(parsedWork.summary.characters.contains("Hatsune Miku"));
    assert(parsedWork.summary.tags.contains("Alternate Universe - Sci-Fi"));
    assert(parsedWork.summary.tags.contains("Time Travel"));
    qDebug() << "✓ parseFullWork AO3 metadata and tags extraction verified!";

    // Test parseFullWork embedded <img> extraction
    const QByteArray sampleWorkWithImages =
        "<h2 class=\"title heading\">Story with Art</h2>\n"
        "<div class=\"userstuff\">\n"
        "  <p align=\"center\"><a href=\"https://images.example.org/full.png\"><img src=\"https://images.example.org/cover.png\" alt=\"Cover Art\" title=\"Cover Illustration\" width=\"800\" height=\"600\" /></a></p>\n"
        "  <p>Some story prose...</p>\n"
        "  <p><img src=\"https://images.example.org/scene1.jpg\" alt=\"Battle Scene\" /></p>\n"
        "</div>\n";

    Ao3FullWork parsedWorkImages = Ao3Parser::parseFullWork(sampleWorkWithImages);
    assert(parsedWorkImages.embeddedImages.size() == 2);
    assert(parsedWorkImages.embeddedImages.at(0).url == "https://images.example.org/cover.png");
    assert(parsedWorkImages.embeddedImages.at(0).alt == "Cover Art");
    assert(parsedWorkImages.embeddedImages.at(0).title == "Cover Illustration");
    assert(parsedWorkImages.embeddedImages.at(0).chapterNumber == 1);
    assert(parsedWorkImages.embeddedImages.at(1).url == "https://images.example.org/scene1.jpg");
    assert(parsedWorkImages.embeddedImages.at(1).alt == "Battle Scene");
    qDebug() << "✓ parseFullWork embedded <img> URLs extraction verified!";

    qDebug() << "All C++ Ao3Parser Unit Tests Passed!";
    return 0;
}
