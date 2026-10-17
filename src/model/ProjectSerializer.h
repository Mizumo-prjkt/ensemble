#pragma once

#include "Ao3Project.h"

#include <QByteArray>
#include <QString>
#include <functional>

enum class ProjectFormatType {
    Unknown,
    ModernArchiveZstd, // Zstandard frame (0x28B52FFD)
    ModernArchiveZip,  // Standard PK ZIP archive (0x504B0304)
    LegacyJsonV1       // Legacy JSON format (starts with '{' and has formatVersion: 1)
};

enum class ValidationStage {
    None,
    FileAccess,        // Stage 0: Existence, permissions, non-empty
    MagicBytesCheck,   // Stage 1: File header magic signature check
    StructureCheck,    // Stage 2: Archive decompression, XML manifest, JSON schema
    RenderCheck        // Stage 3: Project instantiation and chapter rendering
};

struct ProjectLoadReport {
    bool success = false;
    bool isLegacy = false;
    ProjectFormatType formatType = ProjectFormatType::Unknown;
    ValidationStage failedStage = ValidationStage::None;

    QString filePath;
    qint64 fileSize = 0;
    QByteArray rawHeaderBytes;
    QString headerHex;
    QString headerAscii;

    QString summaryError;
    QString detailedExplanation;
    QString remediationHint;
};

class ProjectSerializer
{
public:
    using ProgressCallback = std::function<void(int percent, const QString &stageText)>;

    // Comprehensive 3-phase loading pipeline with diagnostics
    static ProjectLoadReport loadWithValidation(const QString &path, Ao3Project &project, ProgressCallback progress = nullptr);

    // Standard load and save (backward-compatible)
    static bool load(const QString &path, Ao3Project &project, QString *error = nullptr);
    static bool save(const QString &path, const Ao3Project &project, QString *error = nullptr);

    // Specific format loaders
    static bool loadLegacyJson(const QString &path, Ao3Project &project, QString *error = nullptr);
    static bool loadZstdArchive(const QString &path, Ao3Project &project, QString *error = nullptr);

    // Formatting utilities for diagnostic reports
    static QString formatHexDump(const QByteArray &bytes);
    static QString formatAsciiPreview(const QByteArray &bytes);
};
