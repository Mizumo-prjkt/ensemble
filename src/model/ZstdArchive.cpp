#include "ZstdArchive.h"
#include "utils/miniz.h"

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
    // 1. Primary: In-process C/C++ miniz ZIP archive builder (zero external dependencies, 100% portable)
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    if (mz_zip_writer_init_heap(&zip, 0, 65536)) {
        bool addSuccess = true;
        for (auto it = fileEntries.constBegin(); it != fileEntries.constEnd(); ++it) {
            const QByteArray relName = it.key().toUtf8();
            const QByteArray fileData = it.value();

            if (!mz_zip_writer_add_mem(&zip, relName.constData(), fileData.constData(), static_cast<size_t>(fileData.size()), MZ_DEFAULT_COMPRESSION)) {
                addSuccess = false;
                break;
            }
        }

        if (addSuccess) {
            void *pZipBuf = nullptr;
            size_t zipSize = 0;
            if (mz_zip_writer_finalize_heap_archive(&zip, &pZipBuf, &zipSize) && pZipBuf && zipSize > 0) {
                // Atomic save via temp file or direct QFile write
                QFile outFile(destPath);
                if (outFile.open(QIODevice::WriteOnly)) {
                    qint64 written = outFile.write(reinterpret_cast<const char *>(pZipBuf), static_cast<qint64>(zipSize));
                    outFile.flush();
                    outFile.close();
                    if (written == static_cast<qint64>(zipSize)) {
                        mz_zip_writer_end(&zip);
                        return true;
                    }
                }
            }
        }
        mz_zip_writer_end(&zip);
    }

    // 2. Fallback: Temporary directory with external tooling
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

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    const QString nativeDestPath = QDir::toNativeSeparators(destPath);
    const QString nativeTempDir = QDir::toNativeSeparators(tempDir.path());

    // 2a. Python zipfile
    const QString pyScript = QStringLiteral(
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

    const QStringList pyCmds = {QStringLiteral("python3"), QStringLiteral("python")};
    for (const QString &pyCmd : pyCmds) {
        QProcess pyProc;
        pyProc.start(pyCmd, {QStringLiteral("-c"), pyScript, tempDir.path(), destPath});
        if (pyProc.waitForStarted(1000) && pyProc.waitForFinished(5000) && pyProc.exitCode() == 0 && QFile::exists(destPath)) {
            return true;
        }
    }

    // 2b. Windows PowerShell
#if defined(Q_OS_WIN) || defined(_WIN32)
    QProcess psProc;
    const QString psCmd = QStringLiteral("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
                              .arg(nativeTempDir, nativeDestPath);
    psProc.start(QStringLiteral("powershell"), {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), psCmd});
    if (psProc.waitForStarted(1000) && psProc.waitForFinished(8000) && psProc.exitCode() == 0 && QFile::exists(destPath)) {
        return true;
    }
#endif

    // 2c. Native tar
    QProcess tarProc;
    tarProc.start(QStringLiteral("tar"), {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"), destPath, QStringLiteral("-C"), tempDir.path(), QStringLiteral(".")});
    if (tarProc.waitForStarted(1000) && tarProc.waitForFinished(5000) && tarProc.exitCode() == 0 && QFile::exists(destPath)) {
        return true;
    }

    if (error) *error = QStringLiteral("Could not write project archive: %1").arg(destPath);
    return false;
}

bool ZstdArchive::decompressArchive(const QString &srcPath, QMap<QString, QByteArray> &outEntries, QString *error)
{
    // 1. Primary: In-process C/C++ miniz ZIP archive extractor
    QFile inFile(srcPath);
    if (inFile.open(QIODevice::ReadOnly)) {
        const QByteArray archiveBytes = inFile.readAll();
        inFile.close();

        if (!archiveBytes.isEmpty()) {
            mz_zip_archive zip;
            mz_zip_zero_struct(&zip);

            if (mz_zip_reader_init_mem(&zip, archiveBytes.constData(), static_cast<size_t>(archiveBytes.size()), 0)) {
                mz_uint totalFiles = mz_zip_reader_get_num_files(&zip);
                for (mz_uint i = 0; i < totalFiles; ++i) {
                    mz_zip_archive_file_stat stat;
                    if (!mz_zip_reader_file_stat(&zip, i, &stat))
                        continue;
                    if (stat.m_is_directory)
                        continue;

                    size_t uncompSize = 0;
                    void *pData = mz_zip_reader_extract_to_heap(&zip, i, &uncompSize, 0);
                    if (pData) {
                        QString relPath = QString::fromUtf8(stat.m_filename);
                        if (relPath.startsWith(QLatin1String("./")))
                            relPath = relPath.mid(2);
                        outEntries.insert(relPath, QByteArray(reinterpret_cast<const char *>(pData), static_cast<int>(uncompSize)));
                        mz_free(pData);
                    }
                }
                mz_zip_reader_end(&zip);

                if (!outEntries.isEmpty()) {
                    return true;
                }
            }
        }
    }

    // 2. Fallback: External process extraction
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) *error = QStringLiteral("Failed to create temporary directory.");
        return false;
    }

    auto readEntriesFromTempDir = [&tempDir, &outEntries]() -> bool {
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
        return !outEntries.isEmpty();
    };

    const QString nativeSrcPath = QDir::toNativeSeparators(srcPath);
    const QString nativeTempDir = QDir::toNativeSeparators(tempDir.path());

    // 2a. Python
    const QString pyScript = QStringLiteral(
        "import zipfile, sys\n"
        "in_file = sys.argv[1]\n"
        "out_dir = sys.argv[2]\n"
        "with zipfile.ZipFile(in_file, 'r') as z:\n"
        "    z.extractall(out_dir)\n"
    );

    const QStringList pyCmds = {QStringLiteral("python3"), QStringLiteral("python")};
    for (const QString &pyCmd : pyCmds) {
        QProcess pyProc;
        pyProc.start(pyCmd, {QStringLiteral("-c"), pyScript, srcPath, tempDir.path()});
        if (pyProc.waitForStarted(1000) && pyProc.waitForFinished(5000) && pyProc.exitCode() == 0) {
            if (readEntriesFromTempDir())
                return true;
        }
    }

    // 2b. PowerShell
#if defined(Q_OS_WIN) || defined(_WIN32)
    QProcess psProc;
    const QString psCmd = QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                              .arg(nativeSrcPath, nativeTempDir);
    psProc.start(QStringLiteral("powershell"), {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), psCmd});
    if (psProc.waitForStarted(1000) && psProc.waitForFinished(8000) && psProc.exitCode() == 0) {
        if (readEntriesFromTempDir())
            return true;
    }
#endif

    // 2c. tar
    QProcess tarProc;
    tarProc.start(QStringLiteral("tar"), {QStringLiteral("-xf"), srcPath, QStringLiteral("-C"), tempDir.path()});
    if (tarProc.waitForStarted(1000) && tarProc.waitForFinished(5000) && tarProc.exitCode() == 0) {
        if (readEntriesFromTempDir())
            return true;
    }

    if (error) *error = QStringLiteral("Could not extract project archive: %1").arg(srcPath);
    return false;
}
