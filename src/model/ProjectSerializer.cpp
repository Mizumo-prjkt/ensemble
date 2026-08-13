#include "ProjectSerializer.h"
#include "ZstdArchive.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

constexpr int kLegacyFormatVersion = 1;

QJsonObject chapterToJson(const Chapter &chapter)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = chapter.id().toString(QUuid::WithoutBraces);
    obj[QStringLiteral("title")] = chapter.title();
    obj[QStringLiteral("order")] = chapter.order();
    obj[QStringLiteral("html")] = chapter.html();
    return obj;
}

bool chapterFromJson(const QJsonObject &obj, Chapter &chapter, QString *error)
{
    const QString idStr = obj.value(QStringLiteral("id")).toString();
    if (!idStr.isEmpty()) {
        const QUuid id = QUuid::fromString(idStr);
        if (id.isNull()) {
            if (error)
                *error = QStringLiteral("Invalid chapter id: %1").arg(idStr);
            return false;
        }
    }

    chapter.setTitle(obj.value(QStringLiteral("title")).toString(QStringLiteral("Untitled Chapter")));
    chapter.setOrder(obj.value(QStringLiteral("order")).toInt(0));
    chapter.setHtml(obj.value(QStringLiteral("html")).toString());
    return true;
}

} // namespace

bool ProjectSerializer::load(const QString &path, Ao3Project &project, QString *error)
{
    if (ZstdArchive::isZstdArchive(path)) {
        return loadZstdArchive(path, project, error);
    }
    return loadLegacyJson(path, project, error);
}

bool ProjectSerializer::loadLegacyJson(const QString &path, Ao3Project &project, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Could not open file: %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("JSON parse error: %1").arg(parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid project file format.");
        return false;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("formatVersion")).toInt(0);
    if (version != kLegacyFormatVersion) {
        if (error)
            *error = QStringLiteral("Unsupported format version: %1").arg(version);
        return false;
    }

    project.resetToNew();
    project.setTitle(root.value(QStringLiteral("title")).toString(QStringLiteral("Untitled Work")));
    project.setWorkSkinCss(root.value(QStringLiteral("workSkinCss")).toString());

    project.chapters().clear();
    const QJsonArray chapters = root.value(QStringLiteral("chapters")).toArray();
    if (chapters.isEmpty()) {
        project.addChapter(QStringLiteral("Chapter 1"));
    } else {
        for (const QJsonValue &val : chapters) {
            if (!val.isObject()) {
                if (error)
                    *error = QStringLiteral("Invalid chapter entry.");
                return false;
            }
            Chapter chapter;
            if (!chapterFromJson(val.toObject(), chapter, error))
                return false;
            project.chapters().append(chapter);
        }
    }

    project.sortChaptersByOrder();
    project.setActiveChapterIndex(0);
    project.setFilePath(path);
    project.setDirty(false);
    return true;
}

bool ProjectSerializer::loadZstdArchive(const QString &path, Ao3Project &project, QString *error)
{
    QMap<QString, QByteArray> entries;
    if (!ZstdArchive::decompressArchive(path, entries, error)) {
        return false;
    }

    if (!entries.contains(QStringLiteral("metadata.xml"))) {
        if (error) *error = QStringLiteral("Missing metadata.xml in archive: %1").arg(path);
        return false;
    }

    project.resetToNew();
    project.chapters().clear();

    // 1. Read history.cmt if present
    if (entries.contains(QStringLiteral("history.cmt"))) {
        project.setHistoryLog(QString::fromUtf8(entries.value(QStringLiteral("history.cmt"))));
    }

    // 2. Read stylesheet/workskin.css if present
    if (entries.contains(QStringLiteral("stylesheet/workskin.css"))) {
        project.setWorkSkinCss(QString::fromUtf8(entries.value(QStringLiteral("stylesheet/workskin.css"))));
    }

    // 3. Parse metadata.xml
    QXmlStreamReader xml(entries.value(QStringLiteral("metadata.xml")));

    struct ChapterMeta {
        QString idStr;
        int order = 0;
        QString fileRelPath;
        QString title;
    };
    QList<ChapterMeta> chapMetas;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString tag = xml.name().toString();
            if (tag == QStringLiteral("title")) {
                project.setTitle(xml.readElementText());
            } else if (tag == QStringLiteral("author")) {
                project.setAuthor(xml.readElementText());
            } else if (tag == QStringLiteral("created")) {
                project.setCreatedDate(QDateTime::fromString(xml.readElementText(), Qt::ISODate));
            } else if (tag == QStringLiteral("modified")) {
                project.setModifiedDate(QDateTime::fromString(xml.readElementText(), Qt::ISODate));
            } else if (tag == QStringLiteral("chapter")) {
                ChapterMeta meta;
                meta.idStr = xml.attributes().value(QStringLiteral("id")).toString();
                meta.order = xml.attributes().value(QStringLiteral("order")).toInt();
                meta.fileRelPath = xml.attributes().value(QStringLiteral("file")).toString();
                meta.title = xml.readElementText();
                chapMetas.append(meta);
            }
        }
    }

    if (xml.hasError()) {
        if (error) *error = QStringLiteral("XML error in metadata.xml: %1").arg(xml.errorString());
        return false;
    }

    // 4. Load Chapter Contents
    for (const ChapterMeta &meta : chapMetas) {
        Chapter ch;
        ch.setTitle(meta.title.isEmpty() ? QStringLiteral("Untitled Chapter") : meta.title);
        ch.setOrder(meta.order);

        if (!meta.fileRelPath.isEmpty() && entries.contains(meta.fileRelPath)) {
            ch.setHtml(QString::fromUtf8(entries.value(meta.fileRelPath)));
        } else {
            ch.setHtml(QStringLiteral("<p></p>"));
        }
        project.chapters().append(ch);
    }

    if (project.chapters().isEmpty()) {
        project.addChapter(QStringLiteral("Chapter 1"));
    } else {
        project.sortChaptersByOrder();
    }

    project.setActiveChapterIndex(0);
    project.setFilePath(path);
    project.setDirty(false);
    return true;
}

bool ProjectSerializer::save(const QString &path, const Ao3Project &project, QString *error)
{
    QMap<QString, QByteArray> fileEntries;

    // 1. Build metadata.xml
    QByteArray xmlBytes;
    QXmlStreamWriter xml(&xmlBytes);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("ao3project"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("2.0"));

    xml.writeStartElement(QStringLiteral("metadata"));
    xml.writeTextElement(QStringLiteral("title"), project.title());
    xml.writeTextElement(QStringLiteral("author"), project.author());
    xml.writeTextElement(QStringLiteral("created"), project.createdDate().toString(Qt::ISODate));
    xml.writeTextElement(QStringLiteral("modified"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    xml.writeEndElement(); // metadata

    xml.writeStartElement(QStringLiteral("chapters"));
    int chIdx = 1;
    for (const Chapter &ch : project.chapters()) {
        const QString chFileName = QStringLiteral("chapters/chapter_%1.html").arg(chIdx);
        xml.writeStartElement(QStringLiteral("chapter"));
        xml.writeAttribute(QStringLiteral("id"), ch.id().toString(QUuid::WithoutBraces));
        xml.writeAttribute(QStringLiteral("order"), QString::number(ch.order()));
        xml.writeAttribute(QStringLiteral("file"), chFileName);
        xml.writeCharacters(ch.title());
        xml.writeEndElement(); // chapter

        fileEntries.insert(chFileName, ch.html().toUtf8());
        chIdx++;
    }
    xml.writeEndElement(); // chapters

    xml.writeStartElement(QStringLiteral("stylesheets"));
    xml.writeStartElement(QStringLiteral("stylesheet"));
    xml.writeAttribute(QStringLiteral("file"), QStringLiteral("stylesheet/workskin.css"));
    xml.writeEndElement(); // stylesheet
    xml.writeEndElement(); // stylesheets

    xml.writeEndElement(); // ao3project
    xml.writeEndDocument();

    fileEntries.insert(QStringLiteral("metadata.xml"), xmlBytes);

    // 2. Add history.cmt
    fileEntries.insert(QStringLiteral("history.cmt"), project.historyLog().toUtf8());

    // 3. Add stylesheet/workskin.css
    fileEntries.insert(QStringLiteral("stylesheet/workskin.css"), project.workSkinCss().toUtf8());

    // 4. Package and compress using ZstdArchive
    return ZstdArchive::compressArchive(path, fileEntries, error);
}
