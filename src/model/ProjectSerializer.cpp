#include "ProjectSerializer.h"
#include "ZstdArchive.h"
#include "debug/debug.hpp"

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

QString ProjectSerializer::formatHexDump(const QByteArray &bytes)
{
    QString hex;
    const int count = qMin(bytes.size(), 64);
    for (int i = 0; i < count; ++i) {
        if (i > 0) hex += QLatin1Char(' ');
        hex += QString::asprintf("%02X", static_cast<unsigned char>(bytes.at(i)));
    }
    if (bytes.size() > 64) hex += QStringLiteral(" ...");
    return hex;
}

QString ProjectSerializer::formatAsciiPreview(const QByteArray &bytes)
{
    QString ascii;
    const int count = qMin(bytes.size(), 64);
    for (int i = 0; i < count; ++i) {
        const char c = bytes.at(i);
        if (c >= 32 && c <= 126) {
            ascii += QLatin1Char(c);
        } else {
            ascii += QLatin1Char('.');
        }
    }
    if (bytes.size() > 64) ascii += QStringLiteral("...");
    return ascii;
}

ProjectLoadReport ProjectSerializer::loadWithValidation(const QString &path, Ao3Project &project)
{
    ENSEMBLE_PROFILE_CAT_SCOPE("ProjectSerializer::loadWithValidation", logModel);
    ProjectLoadReport report;
    report.filePath = path;

    // -------------------------------------------------------------
    // STAGE 0 & 1: File Access and Magic Bytes Check
    // -------------------------------------------------------------
    if (!QFile::exists(path)) {
        report.failedStage = ValidationStage::FileAccess;
        report.summaryError = QStringLiteral("Project file not found: %1").arg(path);
        report.detailedExplanation = QStringLiteral(
            "============================================================\n"
            " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
            "============================================================\n"
            "File Path: %1\n"
            "Status: FILE_NOT_FOUND\n\n"
            "[Stage 0: File Access Verification]\n"
            "Result: FAILED\n"
            "Details: The operating system reported that no file exists at this path.\n\n"
            "Root Cause:\n"
            "The file path is incorrect, or the file was deleted, moved, or is located on an unmounted drive.\n\n"
            "Troubleshooting:\n"
            "1. Verify that the file path and file name are correct.\n"
            "2. Confirm that external storage or network drives are mounted.\n"
        ).arg(path);
        report.remediationHint = QStringLiteral("Please verify the file path and confirm that the file exists.");
        return report;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report.failedStage = ValidationStage::FileAccess;
        report.summaryError = QStringLiteral("Cannot open project file: %1").arg(file.errorString());
        report.detailedExplanation = QStringLiteral(
            "============================================================\n"
            " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
            "============================================================\n"
            "File Path: %1\n"
            "Status: ACCESS_DENIED / OPEN_FAILED\n\n"
            "[Stage 0: File Access Verification]\n"
            "Result: FAILED\n"
            "OS Error: %2\n\n"
            "Root Cause:\n"
            "The operating system denied read access. The file may have restricted permissions or be locked exclusively by another program.\n\n"
            "Troubleshooting:\n"
            "1. Check file permissions and confirm read access.\n"
            "2. Ensure no other application holds an exclusive file lock.\n"
        ).arg(path, file.errorString());
        report.remediationHint = QStringLiteral("Check file read permissions or close conflicting programs.");
        return report;
    }

    report.fileSize = file.size();
    if (report.fileSize == 0) {
        file.close();
        report.failedStage = ValidationStage::MagicBytesCheck;
        report.summaryError = QStringLiteral("Project file is empty (0 bytes).");
        report.detailedExplanation = QStringLiteral(
            "============================================================\n"
            " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
            "============================================================\n"
            "File Path: %1\n"
            "File Size: 0 bytes\n"
            "Status: EMPTY_FILE\n\n"
            "[Stage 1: Magic Bytes Verification]\n"
            "Result: FAILED\n"
            "Details: The file contains 0 bytes. A valid project file requires at least an archive header.\n\n"
            "Root Cause:\n"
            "The file was created empty or truncated during an interrupted save or transfer.\n\n"
            "Troubleshooting:\n"
            "1. Restore the project from a backup copy.\n"
            "2. Check if disk storage ran out during a previous save operation.\n"
        ).arg(path);
        report.remediationHint = QStringLiteral("The file contains 0 bytes and cannot be loaded.");
        return report;
    }

    report.rawHeaderBytes = file.read(64);
    file.close();

    report.headerHex = formatHexDump(report.rawHeaderBytes);
    report.headerAscii = formatAsciiPreview(report.rawHeaderBytes);

    static const QByteArray kZstdMagic = QByteArray::fromHex("28b52ffd");
    static const QByteArray kZipMagic = QByteArray::fromHex("504b0304");
    static const QByteArray kZipEmptyMagic = QByteArray::fromHex("504b0506");
    static const QByteArray kZipSpannedMagic = QByteArray::fromHex("504b0708");

    if (report.rawHeaderBytes.startsWith(kZstdMagic)) {
        report.formatType = ProjectFormatType::ModernArchiveZstd;
        report.isLegacy = false;
    } else if (report.rawHeaderBytes.startsWith(kZipMagic) ||
               report.rawHeaderBytes.startsWith(kZipEmptyMagic) ||
               report.rawHeaderBytes.startsWith(kZipSpannedMagic)) {
        report.formatType = ProjectFormatType::ModernArchiveZip;
        report.isLegacy = false;
    } else {
        QByteArray trimmedHeader = report.rawHeaderBytes;
        if (trimmedHeader.startsWith("\xEF\xBB\xBF")) { // UTF-8 BOM
            trimmedHeader = trimmedHeader.mid(3);
        }
        trimmedHeader = trimmedHeader.trimmed();
        if (!trimmedHeader.isEmpty() && (trimmedHeader.at(0) == '{' || trimmedHeader.at(0) == '[')) {
            report.formatType = ProjectFormatType::LegacyJsonV1;
            report.isLegacy = true;
        }
    }

    if (report.formatType == ProjectFormatType::Unknown) {
        report.failedStage = ValidationStage::MagicBytesCheck;
        report.summaryError = QStringLiteral("Unrecognized file format: Magic Bytes check failed.");
        report.detailedExplanation = QStringLiteral(
            "============================================================\n"
            " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
            "============================================================\n"
            "File Path: %1\n"
            "File Size: %2 bytes\n"
            "Status: INVALID_MAGIC_BYTES\n\n"
            "[Stage 1: Magic Bytes Verification]\n"
            "Result: FAILED\n\n"
            "Detected File Header:\n"
            "  Hex:   %3\n"
            "  ASCII: %4\n\n"
            "Expected Signatures:\n"
            "  • Modern Zstandard Archive (.ao3proj):  28 B5 2F FD\n"
            "  • Modern ZIP Container (.ao3proj):       50 4B 03 04 ('PK\\x03\\x04')\n"
            "  • Legacy JSON Project (.ao3proj/.json):  7B ('{')\n\n"
            "Root Cause:\n"
            "The magic header bytes do not match any recognized Ensemble project signature.\n"
            "The file may be a different format (such as DOCX, PDF, image, or text) or corrupted.\n\n"
            "Troubleshooting:\n"
            "1. Verify that the file is an authentic Ensemble .ao3proj archive.\n"
            "2. If importing story content directly from Archive of Our Own, use 'Import from AO3'.\n"
        ).arg(path, QString::number(report.fileSize), report.headerHex, report.headerAscii);
        report.remediationHint = QStringLiteral("The file does not match any Ensemble project signature.");
        return report;
    }

    // -------------------------------------------------------------
    // STAGE 2: Structural Integrity Check
    // -------------------------------------------------------------
    if (report.formatType == ProjectFormatType::ModernArchiveZstd ||
        report.formatType == ProjectFormatType::ModernArchiveZip) {

        QMap<QString, QByteArray> entries;
        QString decompressErr;
        if (!ZstdArchive::decompressArchive(path, entries, &decompressErr)) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Archive extraction failed: corrupted or truncated container.");
            report.detailedExplanation = QStringLiteral(
                "============================================================\n"
                " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
                "============================================================\n"
                "File Path: %1\n"
                "Format: Archive Container (%2)\n"
                "Status: DECOMPRESSION_FAILED\n\n"
                "[Stage 1: Magic Bytes Verification]\n"
                "Result: PASSED (Signature: %3)\n\n"
                "[Stage 2: Structural Integrity Check]\n"
                "Result: FAILED\n"
                "Extraction Details: %4\n\n"
                "Root Cause:\n"
                "The archive header was recognized, but the compressed data or central directory is corrupted or truncated.\n\n"
                "Troubleshooting:\n"
                "1. Check if the file download or copy completed without interruption.\n"
                "2. Try restoring from an automated backup or recent snapshot.\n"
            ).arg(path,
                 report.formatType == ProjectFormatType::ModernArchiveZstd ? QStringLiteral("Zstandard") : QStringLiteral("ZIP"),
                 report.headerHex.left(11),
                 decompressErr.isEmpty() ? QStringLiteral("Corrupt or invalid compressed data stream") : decompressErr);
            report.remediationHint = QStringLiteral("The project archive data is corrupted or truncated.");
            return report;
        }

        if (!entries.contains(QStringLiteral("metadata.xml"))) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Archive is missing required 'metadata.xml' descriptor.");
            QString foundEntriesList;
            for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
                foundEntriesList += QStringLiteral("  • %1 (%2 bytes)\n").arg(it.key(), QString::number(it.value().size()));
            }
            if (foundEntriesList.isEmpty()) {
                foundEntriesList = QStringLiteral("  (Archive is completely empty)\n");
            }
            report.detailedExplanation = QStringLiteral(
                "============================================================\n"
                " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
                "============================================================\n"
                "File Path: %1\n"
                "Status: MISSING_MANIFEST\n\n"
                "[Stage 1: Magic Bytes Verification]\n"
                "Result: PASSED\n\n"
                "[Stage 2: Structural Integrity Check]\n"
                "Result: FAILED\n"
                "Missing Manifest: metadata.xml\n\n"
                "Archive Contents Found:\n%2\n"
                "Root Cause:\n"
                "A valid Ensemble project archive must contain 'metadata.xml' at its root.\n\n"
                "Troubleshooting:\n"
                "1. If this archive was created manually, ensure 'metadata.xml' is located at root level.\n"
            ).arg(path, foundEntriesList);
            report.remediationHint = QStringLiteral("The archive is missing the metadata.xml project descriptor.");
            return report;
        }

        QXmlStreamReader xml(entries.value(QStringLiteral("metadata.xml")));
        if (!xml.readNextStartElement()) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Invalid XML in metadata.xml: %1").arg(xml.errorString());
            report.detailedExplanation = QStringLiteral(
                "============================================================\n"
                " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
                "============================================================\n"
                "File Path: %1\n"
                "Status: XML_PARSE_ERROR\n\n"
                "[Stage 2: Structural Integrity Check]\n"
                "Result: FAILED\n"
                "XML Error: %2 (Line %3, Column %4)\n"
            ).arg(path, xml.errorString(), QString::number(xml.lineNumber()), QString::number(xml.columnNumber()));
            report.remediationHint = QStringLiteral("metadata.xml contains invalid or corrupt XML syntax.");
            return report;
        }

        if (xml.name() != QStringLiteral("ao3project") && xml.name() != QStringLiteral("project")) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Invalid manifest root element: <%1> (expected <ao3project> or <project>).").arg(xml.name().toString());
            report.detailedExplanation = QStringLiteral(
                "============================================================\n"
                " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
                "============================================================\n"
                "File Path: %1\n"
                "Status: INVALID_SCHEMA_ROOT\n\n"
                "[Stage 2: Structural Integrity Check]\n"
                "Result: FAILED\n"
                "Expected root tag: <ao3project>\n"
                "Encountered root tag: <%2>\n"
            ).arg(path, xml.name().toString());
            report.remediationHint = QStringLiteral("metadata.xml does not have a valid <ao3project> root element.");
            return report;
        }

        // -------------------------------------------------------------
        // STAGE 3: Render / Populate Project Model
        // -------------------------------------------------------------
        project.resetToNew();
        project.chapters().clear();

        // Agentic Data Quota & Integrity Verification
        QString agenticErr;
        if (!project.agenticData().deserializeFromArchive(entries, &agenticErr)) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Agentic data validation failed: %1").arg(agenticErr);
            report.detailedExplanation = QStringLiteral(
                "============================================================\n"
                " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
                "============================================================\n"
                "File Path: %1\n"
                "Status: AGENTIC_DATA_QUOTA_EXCEEDED\n\n"
                "[Stage 2: Structural Integrity Check]\n"
                "Result: FAILED\n"
                "Details: %2\n\n"
                "Quota Rules:\n"
                "  • Shared Agent Data: Max 5 MB (5,242,880 bytes)\n"
                "  • Localized Agent Data: Max 20 MB (20,971,520 bytes)\n"
            ).arg(path, agenticErr);
            report.remediationHint = QStringLiteral("Project contains agent data exceeding maximum allowed caps (5 MB shared, 20 MB localized).");
            return report;
        }

        if (entries.contains(QStringLiteral("history.cmt"))) {
            project.setHistoryLog(QString::fromUtf8(entries.value(QStringLiteral("history.cmt"))));
        } else {
            project.setHistoryLog(QString());
        }

        // Deserialize history/*.diff snapshot diff files
        project.snapshotDiffs().clear();
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            const QString &key = it.key();
            if (key.startsWith(QStringLiteral("history/")) && key.endsWith(QStringLiteral(".diff"))) {
                const QString hash = key.mid(8, key.length() - 8 - 5);
                if (!hash.isEmpty()) {
                    project.setSnapshotDiff(hash, QString::fromUtf8(it.value()));
                }
            }
        }

        if (entries.contains(QStringLiteral("stylesheet/workskin.css"))) {
            project.setWorkSkinCss(QString::fromUtf8(entries.value(QStringLiteral("stylesheet/workskin.css"))));
        }

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
                } else if (tag == QStringLiteral("rating")) {
                    project.setRating(xml.readElementText());
                } else if (tag == QStringLiteral("warning")) {
                    project.archiveWarnings().append(xml.readElementText());
                } else if (tag == QStringLiteral("category")) {
                    project.categories().append(xml.readElementText());
                } else if (tag == QStringLiteral("fandom")) {
                    project.addFandom(xml.readElementText());
                } else if (tag == QStringLiteral("relationship")) {
                    project.addRelationship(xml.readElementText());
                } else if (tag == QStringLiteral("character")) {
                    project.addCharacter(xml.readElementText());
                } else if (tag == QStringLiteral("tag")) {
                    project.addTag(xml.readElementText());
                } else if (tag == QStringLiteral("created")) {
                    project.setCreatedDate(QDateTime::fromString(xml.readElementText(), Qt::ISODate));
                } else if (tag == QStringLiteral("modified")) {
                    project.setModifiedDate(QDateTime::fromString(xml.readElementText(), Qt::ISODate));
                } else if (tag == QStringLiteral("lastModifiedBy")) {
                    project.setLastModifiedBy(xml.readElementText());
                } else if (tag == QStringLiteral("lastModifiedAt")) {
                    project.setLastModifiedAt(QDateTime::fromString(xml.readElementText(), Qt::ISODate));
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

        // Deserialize Lore System & Lore Graph
        QString loreErr;
        project.lore().deserializeFromArchive(entries, &loreErr);

        if (xml.hasError()) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("XML error in metadata.xml: %1").arg(xml.errorString());
            report.detailedExplanation = QStringLiteral(
                "[Stage 2: Structural Integrity Check]\n"
                "XML parsing error in metadata.xml: %1\n"
            ).arg(xml.errorString());
            report.remediationHint = QStringLiteral("metadata.xml parsing encountered an error.");
            return report;
        }

        bool hasNonZero = false;
        QSet<int> seenOrders;
        bool hasDuplicateOrders = false;
        for (const ChapterMeta &meta : chapMetas) {
            if (meta.order != 0) {
                hasNonZero = true;
            }
            if (seenOrders.contains(meta.order)) {
                hasDuplicateOrders = true;
            }
            seenOrders.insert(meta.order);
        }
        const bool needsReorder = (!hasNonZero && chapMetas.size() > 1) || (hasDuplicateOrders && chapMetas.size() > 1);

        int fallbackOrder = 0;
        for (const ChapterMeta &meta : chapMetas) {
            Chapter ch;
            ch.setTitle(meta.title.isEmpty() ? QStringLiteral("Untitled Chapter") : meta.title);
            ch.setOrder(needsReorder ? fallbackOrder++ : meta.order);
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
            if (needsReorder) {
                for (int i = 0; i < project.chapters().size(); ++i) {
                    project.chapters()[i].setOrder(i);
                }
            }
        }

        if (project.historyLog().trimmed().isEmpty()) {
            project.appendHistoryCommit(
                project.author().isEmpty() ? QStringLiteral("Anonymous") : project.author(),
                QStringLiteral("Initial project import / baseline commit")
            );
        }
        if (project.lastModifiedBy().isEmpty()) {
            project.setLastModifiedBy(project.author().isEmpty() ? QStringLiteral("Author") : project.author());
        }
        if (!project.lastModifiedAt().isValid()) {
            project.setLastModifiedAt(project.modifiedDate().isValid() ? project.modifiedDate() : QDateTime::currentDateTimeUtc());
        }
        project.agenticData().ensureDefaultHistory();

        project.setActiveChapterIndex(0);
        project.setFilePath(path);
        project.setDirty(false);
        project.setLegacyFormat(false);
        project.setLastSnapshotState(project.currentProjectState());
        report.success = true;
        return report;
    }

    // -------------------------------------------------------------
    // STAGE 2 & 3 FOR LEGACY JSON
    // -------------------------------------------------------------
    if (report.formatType == ProjectFormatType::LegacyJsonV1) {
        QFile jsonFile(path);
        if (!jsonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            report.failedStage = ValidationStage::FileAccess;
            report.summaryError = QStringLiteral("Could not read legacy JSON file.");
            report.detailedExplanation = QStringLiteral("Failed to open %1 for reading.").arg(path);
            return report;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
        jsonFile.close();

        if (parseError.error != QJsonParseError::NoError) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Legacy JSON syntax error: %1").arg(parseError.errorString());
            report.detailedExplanation = QStringLiteral(
                "============================================================\n"
                " ENSEMBLE PROJECT VERIFICATION AUDIT\n"
                "============================================================\n"
                "File Path: %1\n"
                "Format: Legacy JSON Format\n"
                "Status: JSON_SYNTAX_ERROR\n\n"
                "[Stage 2: Structural Integrity Check]\n"
                "Result: FAILED\n"
                "JSON Parse Error: %2 at offset %3\n"
            ).arg(path, parseError.errorString(), QString::number(parseError.offset));
            report.remediationHint = QStringLiteral("The legacy JSON file contains invalid JSON syntax.");
            return report;
        }

        if (!doc.isObject()) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Legacy project JSON root must be an object.");
            report.detailedExplanation = QStringLiteral("The document does not contain a JSON object at its root.");
            return report;
        }

        const QJsonObject root = doc.object();
        const int version = root.value(QStringLiteral("formatVersion")).toInt(0);
        if (version != kLegacyFormatVersion) {
            report.failedStage = ValidationStage::StructureCheck;
            report.summaryError = QStringLiteral("Unsupported legacy format version: %1 (expected %2).").arg(version).arg(kLegacyFormatVersion);
            report.detailedExplanation = QStringLiteral(
                "[Stage 2: Structural Integrity Check]\n"
                "Unsupported legacy format version: %1\n"
                "Ensemble legacy loader expects formatVersion: %2\n"
            ).arg(QString::number(version), QString::number(kLegacyFormatVersion));
            report.remediationHint = QStringLiteral("The project file uses an unknown or unsupported format version.");
            return report;
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
                    report.failedStage = ValidationStage::StructureCheck;
                    report.summaryError = QStringLiteral("Invalid chapter entry in legacy JSON.");
                    return report;
                }
                Chapter chapter;
                QString chErr;
                if (!chapterFromJson(val.toObject(), chapter, &chErr)) {
                    report.failedStage = ValidationStage::StructureCheck;
                    report.summaryError = chErr;
                    return report;
                }
                project.chapters().append(chapter);
            }
        }

        project.sortChaptersByOrder();
        project.setActiveChapterIndex(0);
        project.setFilePath(path);
        project.setDirty(false);
        project.setLegacyFormat(true);
        report.success = true;
        report.isLegacy = true;
        return report;
    }

    return report;
}

bool ProjectSerializer::load(const QString &path, Ao3Project &project, QString *error)
{
    const ProjectLoadReport report = loadWithValidation(path, project);
    if (!report.success) {
        if (error) {
            *error = report.summaryError;
        }
        return false;
    }
    return true;
}

bool ProjectSerializer::loadLegacyJson(const QString &path, Ao3Project &project, QString *error)
{
    return load(path, project, error);
}

bool ProjectSerializer::loadZstdArchive(const QString &path, Ao3Project &project, QString *error)
{
    return load(path, project, error);
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
    xml.writeTextElement(QStringLiteral("rating"), project.rating().isEmpty() ? QStringLiteral("Not Rated") : project.rating());

    if (!project.archiveWarnings().isEmpty()) {
        xml.writeStartElement(QStringLiteral("archiveWarnings"));
        for (const QString &w : project.archiveWarnings()) {
            xml.writeTextElement(QStringLiteral("warning"), w);
        }
        xml.writeEndElement(); // archiveWarnings
    }
    if (!project.categories().isEmpty()) {
        xml.writeStartElement(QStringLiteral("categories"));
        for (const QString &c : project.categories()) {
            xml.writeTextElement(QStringLiteral("category"), c);
        }
        xml.writeEndElement(); // categories
    }
    if (!project.fandoms().isEmpty()) {
        xml.writeStartElement(QStringLiteral("fandoms"));
        for (const QString &f : project.fandoms()) {
            xml.writeTextElement(QStringLiteral("fandom"), f);
        }
        xml.writeEndElement(); // fandoms
    }
    if (!project.relationships().isEmpty()) {
        xml.writeStartElement(QStringLiteral("relationships"));
        for (const QString &r : project.relationships()) {
            xml.writeTextElement(QStringLiteral("relationship"), r);
        }
        xml.writeEndElement(); // relationships
    }
    if (!project.characters().isEmpty()) {
        xml.writeStartElement(QStringLiteral("characters"));
        for (const QString &ch : project.characters()) {
            xml.writeTextElement(QStringLiteral("character"), ch);
        }
        xml.writeEndElement(); // characters
    }
    if (!project.tags().isEmpty()) {
        xml.writeStartElement(QStringLiteral("tags"));
        for (const QString &t : project.tags()) {
            xml.writeTextElement(QStringLiteral("tag"), t);
        }
        xml.writeEndElement(); // tags
    }

    xml.writeTextElement(QStringLiteral("created"), project.createdDate().toString(Qt::ISODate));
    xml.writeTextElement(QStringLiteral("modified"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!project.lastModifiedBy().isEmpty()) {
        xml.writeTextElement(QStringLiteral("lastModifiedBy"), project.lastModifiedBy());
    }
    if (project.lastModifiedAt().isValid()) {
        xml.writeTextElement(QStringLiteral("lastModifiedAt"), project.lastModifiedAt().toString(Qt::ISODate));
    }
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

    // Agentic data manifest attribute & metrics
    xml.writeStartElement(QStringLiteral("agentic-datas"));
    xml.writeAttribute(QStringLiteral("enabled"), QStringLiteral("true"));
    xml.writeAttribute(QStringLiteral("index"), QStringLiteral("agentic-datas/index.json"));
    xml.writeAttribute(QStringLiteral("history"), QStringLiteral("agentic-datas/agent_model_history.xml"));
    xml.writeAttribute(QStringLiteral("agentifiers"), QStringLiteral("agentic-datas/agentifiers.json"));
    xml.writeAttribute(QStringLiteral("sharedBytes"), QString::number(project.agenticData().totalSharedBytes()));
    xml.writeAttribute(QStringLiteral("localizedBytes"), QString::number(project.agenticData().totalLocalizedBytes()));
    xml.writeAttribute(QStringLiteral("maxSharedBytes"), QString::number(AgenticData::kMaxSharedBytes));
    xml.writeAttribute(QStringLiteral("maxLocalizedBytes"), QString::number(AgenticData::kMaxLocalizedBytes));
    xml.writeEndElement(); // agentic-datas

    // Lore manifest attribute & metrics
    xml.writeStartElement(QStringLiteral("lore"));
    xml.writeAttribute(QStringLiteral("enabled"), QStringLiteral("true"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeAttribute(QStringLiteral("defaultGraph"), QStringLiteral("lore/graphs/data.grph"));
    xml.writeAttribute(QStringLiteral("metadata"), QStringLiteral("lore/metadata.xml"));
    xml.writeAttribute(QStringLiteral("entityCount"), QString::number(project.lore().entities().size()));
    xml.writeAttribute(QStringLiteral("conflictCount"), QString::number(project.lore().conflicts().size()));
    xml.writeAttribute(QStringLiteral("planCount"), QString::number(project.lore().plans().size()));
    xml.writeEndElement(); // lore

    xml.writeEndElement(); // ao3project
    xml.writeEndDocument();

    fileEntries.insert(QStringLiteral("metadata.xml"), xmlBytes);

    // 2. Add history.cmt and history/*.diff snapshots
    fileEntries.insert(QStringLiteral("history.cmt"), project.historyLog().toUtf8());
    for (auto it = project.snapshotDiffs().constBegin(); it != project.snapshotDiffs().constEnd(); ++it) {
        fileEntries.insert(QStringLiteral("history/%1.diff").arg(it.key()), it.value().toUtf8());
    }

    // 3. Add stylesheet/workskin.css
    fileEntries.insert(QStringLiteral("stylesheet/workskin.css"), project.workSkinCss().toUtf8());

    // 4. Add agentic-datas container entries
    project.agenticData().serializeToArchive(fileEntries);

    // 5. Add lore container entries (lore/lore_data.json, history.json, conflicts.json, resolved.json, plans.json, references.json, metadata.xml, graphs/data.grph)
    project.lore().serializeToArchive(fileEntries);

    // 5. Package and compress using ZstdArchive (outputs standard .ao3proj container)
    return ZstdArchive::compressArchive(path, fileEntries, error);
}
