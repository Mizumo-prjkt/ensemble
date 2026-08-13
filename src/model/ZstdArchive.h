#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

class ZstdArchive
{
public:
    static bool isZstdArchive(const QString &filePath);
    static bool compressArchive(const QString &destPath, const QMap<QString, QByteArray> &fileEntries, QString *error = nullptr);
    static bool decompressArchive(const QString &srcPath, QMap<QString, QByteArray> &outEntries, QString *error = nullptr);
};
