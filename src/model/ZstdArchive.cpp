#include "ZstdArchive.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

// ZSTD Frame Magic Header: 0xFD2FB528 (Little-endian bytes: 0x28, 0xB5, 0x2F, 0xFD)
static const QByteArray kZstdMagic = QByteArray::fromHex("28b52ffd");
static const QByteArray kZipMagic = QByteArray::fromHex("504b0304");

} // namespace

bool ZstdArchive::isZstdArchive(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(64);
    if (header.isEmpty())
        return false;

    // Check if starts with Zip magic (0x504B0304) or ZSTD magic (0xFD2FB528)
    if (header.startsWith(kZipMagic) || header.startsWith(kZstdMagic))
        return true;

    // If file starts with '{' or '[', it's a legacy JSON file
    const char firstChar = header.trimmed().at(0);
    if (firstChar == '{' || firstChar == '[')
        return false;

    // Any non-JSON container file (.ao3proj archive format)
    return true;
}

bool ZstdArchive::compressArchive(const QString &destPath, const QMap<QString, QByteArray> &fileEntries, QString *error)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) *error = QStringLiteral("Failed to create temporary directory.");
        return false;
    }

    // Write file entries to temporary directory structure
    for (auto it = fileEntries.constBegin(); it != fileEntries.constEnd(); ++it) {
        const QString relPath = it.key();
        const QByteArray data = it.value();

        const QString fullPath = QDir(tempDir.path()).filePath(relPath);
        QFileInfo info(fullPath);
        QDir().mkpath(info.absolutePath());

        QFile f(fullPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.close();
        }
    }

    // Remove existing destination file if present
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    // 1. Primary: Use python3 zipfile module to write standard Zip archive container
    QProcess pyProc;
    const QString script = QStringLiteral(
        "import zipfile, os, sys\n"
        "src_dir = sys.argv[1]\n"
        "out_file = sys.argv[2]\n"
        "with zipfile.ZipFile(out_file, 'w', zipfile.ZIP_DEFLATED) as z:\n"
        "    for root, dirs, files in os.walk(src_dir):\n"
        "        for f in files:\n"
        "            full = os.path.join(root, f)\n"
        "            rel = os.path.relpath(full, src_dir)\n"
        "            z.write(full, rel)\n"
    );

    pyProc.start(QStringLiteral("python3"), {QStringLiteral("-c"), script, tempDir.path(), destPath});
    if (pyProc.waitForStarted(1000) && pyProc.waitForFinished(4000) && pyProc.exitCode() == 0 && QFile::exists(destPath)) {
        return true;
    }

    // 2. Fallback: 7z a -tzip destPath tempDir/*
    QProcess process7z;
    process7z.start(QStringLiteral("7z"), {
        QStringLiteral("a"),
        QStringLiteral("-tzip"),
        destPath,
        QDir(tempDir.path()).filePath(QStringLiteral("metadata.xml")),
        QDir(tempDir.path()).filePath(QStringLiteral("history.cmt")),
        QDir(tempDir.path()).filePath(QStringLiteral("chapters")),
        QDir(tempDir.path()).filePath(QStringLiteral("stylesheet"))
    });

    if (process7z.waitForStarted(1000) && process7z.waitForFinished(4000) && process7z.exitCode() == 0 && QFile::exists(destPath)) {
        return true;
    }

    if (error) *error = QStringLiteral("Could not write archive file: %1").arg(destPath);
    return false;
}

bool ZstdArchive::decompressArchive(const QString &srcPath, QMap<QString, QByteArray> &outEntries, QString *error)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) *error = QStringLiteral("Failed to create temporary directory.");
        return false;
    }

    // 1. Try extracting using python3 zipfile module
    QProcess pyProc;
    const QString script = QStringLiteral(
        "import zipfile, sys\n"
        "in_file = sys.argv[1]\n"
        "out_dir = sys.argv[2]\n"
        "with zipfile.ZipFile(in_file, 'r') as z:\n"
        "    z.extractall(out_dir)\n"
    );

    pyProc.start(QStringLiteral("python3"), {QStringLiteral("-c"), script, srcPath, tempDir.path()});
    if (pyProc.waitForStarted(1000) && pyProc.waitForFinished(4000) && pyProc.exitCode() == 0) {
        QDirIterator it(tempDir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fullPath = it.next();
            QString relPath = QDir(tempDir.path()).relativeFilePath(fullPath);
            if (relPath.startsWith(QLatin1String("./")))
                relPath = relPath.mid(2);

            QFile f(fullPath);
            if (f.open(QIODevice::ReadOnly)) {
                outEntries.insert(relPath, f.readAll());
            }
        }
        if (!outEntries.isEmpty())
            return true;
    }

    // 2. Try extracting using unzip
    QProcess unzipProc;
    unzipProc.start(QStringLiteral("unzip"), {QStringLiteral("-q"), QStringLiteral("-o"), srcPath, QStringLiteral("-d"), tempDir.path()});
    if (unzipProc.waitForStarted(1000) && unzipProc.waitForFinished(4000) && unzipProc.exitCode() == 0) {
        QDirIterator it(tempDir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fullPath = it.next();
            QString relPath = QDir(tempDir.path()).relativeFilePath(fullPath);
            if (relPath.startsWith(QLatin1String("./")))
                relPath = relPath.mid(2);

            QFile f(fullPath);
            if (f.open(QIODevice::ReadOnly)) {
                outEntries.insert(relPath, f.readAll());
            }
        }
        if (!outEntries.isEmpty())
            return true;
    }

    // 3. Fallback: try tar for legacy tar archives
    QProcess tarProc;
    tarProc.start(QStringLiteral("tar"), {QStringLiteral("-xf"), srcPath, QStringLiteral("-C"), tempDir.path()});
    if (tarProc.waitForStarted(1000) && tarProc.waitForFinished(4000) && tarProc.exitCode() == 0) {
        QDirIterator it(tempDir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fullPath = it.next();
            QString relPath = QDir(tempDir.path()).relativeFilePath(fullPath);
            if (relPath.startsWith(QLatin1String("./")))
                relPath = relPath.mid(2);

            QFile f(fullPath);
            if (f.open(QIODevice::ReadOnly)) {
                outEntries.insert(relPath, f.readAll());
            }
        }
        if (!outEntries.isEmpty())
            return true;
    }

    if (error) *error = QStringLiteral("Could not extract project archive: %1").arg(srcPath);
    return false;
}
