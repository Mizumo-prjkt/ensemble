#ifndef DIFFUTIL_H
#define DIFFUTIL_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QColor>

namespace DiffUtil {

struct Edit {
    enum class Type { Equal, Insert, Delete };
    Type type;
    QString line;
    int oldLineNo = 0; // 1-based
    int newLineNo = 0; // 1-based
};

struct DiffHunk {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QStringList lines; // Each prefixed with ' ', '+', or '-'
};

// Computes a unified diff between oldText and newText for a single file.
// Returns empty QString if oldText and newText are identical.
QString createUnifiedDiff(const QString &oldPath, const QString &oldText,
                          const QString &newPath, const QString &newText,
                          int contextLines = 3);

// Formats a full project diff between two state maps (relative path -> text content).
// Includes chapter files, stylesheet, and metadata changes.
QString createProjectDiff(const QMap<QString, QString> &oldState,
                          const QMap<QString, QString> &newState,
                          int contextLines = 3);

// Extracts CSS rule declarations for a given class from the provided CSS string.
// Example: class "haruka" -> "color: #2ecc71; font-weight: bold;"
QString extractCssClassDeclarations(const QString &css, const QString &className);

// Attempts to extract the foreground 'color' property from CSS for a given class.
// Returns an invalid QColor if not found.
QColor extractCssColor(const QString &css, const QString &className);

// Returns a system author tag formatted as:
// Windows: <username>@<desktop_id> (e.g. carrisa_lyna@Desktop-HH37G7)
// Linux/macOS: <username>@<hostname> (e.g. miku@workstation)
QString defaultAuthorTag(const QString &customName = QString());

// Formats an ISO 8601 UTC timestamp into a dual display string with adjacent local timezone:
// e.g. "2026-10-12T22:33:07Z (Oct 13, 2026, 06:33 AM +08:00)"
QString formatIsoDateWithLocal(const QString &isoDate);

} // namespace DiffUtil

#endif // DIFFUTIL_H
