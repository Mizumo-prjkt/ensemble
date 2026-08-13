#pragma once

#include "Ao3Project.h"

#include <QString>

class ProjectSerializer
{
public:
    static bool load(const QString &path, Ao3Project &project, QString *error = nullptr);
    static bool save(const QString &path, const Ao3Project &project, QString *error = nullptr);

    static bool loadLegacyJson(const QString &path, Ao3Project &project, QString *error = nullptr);
    static bool loadZstdArchive(const QString &path, Ao3Project &project, QString *error = nullptr);
};
