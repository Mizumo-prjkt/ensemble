#include "DiffUtil.h"

#include <QDateTime>
#include <QTimeZone>
#include <QSysInfo>
#include <QRegularExpression>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <algorithm>

namespace DiffUtil {

static QStringList splitLines(const QString &text)
{
    if (text.isEmpty()) {
        return QStringList();
    }
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.split(QLatin1Char('\n'));
}

static std::vector<Edit> computeEdits(const QStringList &a, const QStringList &b)
{
    std::vector<Edit> edits;
    const int n = a.size();
    const int m = b.size();

    // 1. Common prefix optimization
    int start = 0;
    while (start < n && start < m && a.at(start) == b.at(start)) {
        Edit e;
        e.type = Edit::Type::Equal;
        e.line = a.at(start);
        e.oldLineNo = start + 1;
        e.newLineNo = start + 1;
        edits.push_back(e);
        ++start;
    }

    // 2. Common suffix optimization
    int endA = n - 1;
    int endB = m - 1;
    std::vector<Edit> suffixEdits;
    while (endA >= start && endB >= start && a.at(endA) == b.at(endB)) {
        Edit e;
        e.type = Edit::Type::Equal;
        e.line = a.at(endA);
        e.oldLineNo = endA + 1;
        e.newLineNo = endB + 1;
        suffixEdits.push_back(e);
        --endA;
        --endB;
    }
    std::reverse(suffixEdits.begin(), suffixEdits.end());

    // 3. Middle diff computation using LCS dynamic programming
    const int midLenA = endA - start + 1;
    const int midLenB = endB - start + 1;

    if (midLenA > 0 && midLenB == 0) {
        for (int i = 0; i < midLenA; ++i) {
            Edit e;
            e.type = Edit::Type::Delete;
            e.line = a.at(start + i);
            e.oldLineNo = start + i + 1;
            e.newLineNo = start + 1;
            edits.push_back(e);
        }
    } else if (midLenA == 0 && midLenB > 0) {
        for (int j = 0; j < midLenB; ++j) {
            Edit e;
            e.type = Edit::Type::Insert;
            e.line = b.at(start + j);
            e.oldLineNo = start + 1;
            e.newLineNo = start + j + 1;
            edits.push_back(e);
        }
    } else if (midLenA > 0 && midLenB > 0) {
        // Standard DP matrix on the middle slice
        std::vector<std::vector<int>> dp(midLenA + 1, std::vector<int>(midLenB + 1, 0));
        for (int i = 1; i <= midLenA; ++i) {
            for (int j = 1; j <= midLenB; ++j) {
                if (a.at(start + i - 1) == b.at(start + j - 1)) {
                    dp[i][j] = dp[i - 1][j - 1] + 1;
                } else {
                    dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
                }
            }
        }

        // Backtrack
        std::vector<Edit> midEdits;
        int i = midLenA;
        int j = midLenB;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && a.at(start + i - 1) == b.at(start + j - 1)) {
                Edit e;
                e.type = Edit::Type::Equal;
                e.line = a.at(start + i - 1);
                e.oldLineNo = start + i;
                e.newLineNo = start + j;
                midEdits.push_back(e);
                --i;
                --j;
            } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
                Edit e;
                e.type = Edit::Type::Insert;
                e.line = b.at(start + j - 1);
                e.oldLineNo = start + i;
                e.newLineNo = start + j;
                midEdits.push_back(e);
                --j;
            } else if (i > 0 && (j == 0 || dp[i][j - 1] < dp[i - 1][j])) {
                Edit e;
                e.type = Edit::Type::Delete;
                e.line = a.at(start + i - 1);
                e.oldLineNo = start + i;
                e.newLineNo = start + j;
                midEdits.push_back(e);
                --i;
            }
        }
        std::reverse(midEdits.begin(), midEdits.end());
        edits.insert(edits.end(), midEdits.begin(), midEdits.end());
    }

    edits.insert(edits.end(), suffixEdits.begin(), suffixEdits.end());
    return edits;
}

QString createUnifiedDiff(const QString &oldPath, const QString &oldText,
                          const QString &newPath, const QString &newText,
                          int contextLines)
{
    if (oldText == newText) {
        return QString();
    }

    const QStringList a = splitLines(oldText);
    const QStringList b = splitLines(newText);
    const std::vector<Edit> edits = computeEdits(a, b);

    // Identify indices of non-equal edits
    std::vector<int> changeIndices;
    for (size_t k = 0; k < edits.size(); ++k) {
        if (edits[k].type != Edit::Type::Equal) {
            changeIndices.push_back(static_cast<int>(k));
        }
    }

    if (changeIndices.empty()) {
        return QString();
    }

    // Group changes into hunks with contextLines
    struct HunkRange {
        int startIdx;
        int endIdx;
    };
    std::vector<HunkRange> ranges;
    int currentStart = std::max(0, changeIndices.front() - contextLines);
    int currentEnd = std::min(static_cast<int>(edits.size()) - 1, changeIndices.front() + contextLines);

    for (size_t k = 1; k < changeIndices.size(); ++k) {
        int idx = changeIndices[k];
        int nextStart = std::max(0, idx - contextLines);
        int nextEnd = std::min(static_cast<int>(edits.size()) - 1, idx + contextLines);

        if (nextStart <= currentEnd + 1) {
            // Overlapping or adjacent, merge
            currentEnd = std::max(currentEnd, nextEnd);
        } else {
            ranges.push_back({currentStart, currentEnd});
            currentStart = nextStart;
            currentEnd = nextEnd;
        }
    }
    ranges.push_back({currentStart, currentEnd});

    QString output;
    output += QStringLiteral("--- %1\n").arg(oldPath);
    output += QStringLiteral("+++ %1\n").arg(newPath);

    for (const auto &r : ranges) {
        int oldStart = 0;
        int oldCount = 0;
        int newStart = 0;
        int newCount = 0;
        QStringList hunkLines;

        bool foundFirstOld = false;
        bool foundFirstNew = false;

        for (int idx = r.startIdx; idx <= r.endIdx; ++idx) {
            const auto &e = edits[idx];
            if (e.type == Edit::Type::Equal) {
                if (!foundFirstOld) { oldStart = e.oldLineNo; foundFirstOld = true; }
                if (!foundFirstNew) { newStart = e.newLineNo; foundFirstNew = true; }
                ++oldCount;
                ++newCount;
                hunkLines.append(QStringLiteral(" ") + e.line);
            } else if (e.type == Edit::Type::Delete) {
                if (!foundFirstOld) { oldStart = e.oldLineNo; foundFirstOld = true; }
                ++oldCount;
                hunkLines.append(QStringLiteral("-") + e.line);
            } else if (e.type == Edit::Type::Insert) {
                if (!foundFirstNew) { newStart = e.newLineNo; foundFirstNew = true; }
                ++newCount;
                hunkLines.append(QStringLiteral("+") + e.line);
            }
        }

        if (oldStart == 0) oldStart = (oldText.isEmpty() ? 0 : 1);
        if (newStart == 0) newStart = (newText.isEmpty() ? 0 : 1);

        output += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n")
                      .arg(oldStart).arg(oldCount)
                      .arg(newStart).arg(newCount);
        for (const QString &hl : hunkLines) {
            output += hl + QLatin1Char('\n');
        }
    }

    return output;
}

QString createProjectDiff(const QMap<QString, QString> &oldState,
                          const QMap<QString, QString> &newState,
                          int contextLines)
{
    QString diffResult;

    // Collect all sorted file keys
    QSet<QString> keySet;
    for (auto it = oldState.begin(); it != oldState.end(); ++it) keySet.insert(it.key());
    for (auto it = newState.begin(); it != newState.end(); ++it) keySet.insert(it.key());

    QStringList keys = keySet.values();
    std::sort(keys.begin(), keys.end());

    int diffCount = 0;
    for (const QString &key : keys) {
        const bool inOld = oldState.contains(key);
        const bool inNew = newState.contains(key);

        if (inOld && inNew) {
            const QString oldContent = oldState.value(key);
            const QString newContent = newState.value(key);
            if (oldContent != newContent) {
                const QString d = createUnifiedDiff(QStringLiteral("a/") + key, oldContent,
                                                   QStringLiteral("b/") + key, newContent,
                                                   contextLines);
                if (!d.isEmpty()) {
                    diffResult += d + QLatin1Char('\n');
                    ++diffCount;
                }
            }
        } else if (inOld && !inNew) {
            // Deleted file
            const QString d = createUnifiedDiff(QStringLiteral("a/") + key, oldState.value(key),
                                               QStringLiteral("/dev/null"), QString(),
                                               contextLines);
            if (!d.isEmpty()) {
                diffResult += d + QLatin1Char('\n');
                ++diffCount;
            }
        } else if (!inOld && inNew) {
            // New file
            const QString d = createUnifiedDiff(QStringLiteral("/dev/null"), QString(),
                                               QStringLiteral("b/") + key, newState.value(key),
                                               contextLines);
            if (!d.isEmpty()) {
                diffResult += d + QLatin1Char('\n');
                ++diffCount;
            }
        }
    }

    if (diffCount == 0) {
        return QStringLiteral("# No file modifications detected in this snapshot.\n");
    }

    return diffResult.trimmed() + QLatin1Char('\n');
}

namespace {
struct CssLookupCache {
    QMutex mutex;
    QHash<QString, QString> declarations;
    QHash<QString, QColor> colors;
};
static CssLookupCache s_cssCache;
} // namespace

QString extractCssClassDeclarations(const QString &css, const QString &className)
{
    const QString trimmedCls = className.trimmed();
    if (css.trimmed().isEmpty() || trimmedCls.isEmpty()) {
        return QString();
    }

    const QString cacheKey = QString::number(qHash(css)) + QLatin1String("::") + trimmedCls;
    {
        QMutexLocker locker(&s_cssCache.mutex);
        auto it = s_cssCache.declarations.constFind(cacheKey);
        if (it != s_cssCache.declarations.constEnd()) {
            return it.value();
        }
    }

    // Matches e.g. .haruka { ... } or #workskin .haruka, .haruka:hover { ... }
    const QString escaped = QRegularExpression::escape(trimmedCls);
    // Pattern: (?:^|\})[^{}]*\.<escaped>(?![a-zA-Z0-9_-])[^{}]*\{([^}]+)\}
    const QRegularExpression re(QStringLiteral(R"((?:^|\})[^{}]*\.)") + escaped + QStringLiteral(R"((?![a-zA-Z0-9_-])[^{}]*\{([^}]+)\})"));

    QStringList allRules;
    if (re.isValid()) {
        auto it = re.globalMatch(css);
        while (it.hasNext()) {
            const auto match = it.next();
            QString decls = match.captured(1).trimmed();
            // Clean up multi-line whitespace
            decls = decls.simplified();
            if (!decls.isEmpty() && !allRules.contains(decls)) {
                allRules.append(decls);
            }
        }
    }

    const QString result = allRules.join(QStringLiteral(" | "));
    {
        QMutexLocker locker(&s_cssCache.mutex);
        if (s_cssCache.declarations.size() > 4096) {
            s_cssCache.declarations.clear();
        }
        s_cssCache.declarations.insert(cacheKey, result);
    }
    return result;
}

QColor extractCssColor(const QString &css, const QString &className)
{
    const QString trimmedCls = className.trimmed();
    if (css.trimmed().isEmpty() || trimmedCls.isEmpty()) {
        return QColor();
    }

    const QString cacheKey = QString::number(qHash(css)) + QLatin1String("::") + trimmedCls;
    {
        QMutexLocker locker(&s_cssCache.mutex);
        auto it = s_cssCache.colors.constFind(cacheKey);
        if (it != s_cssCache.colors.constEnd()) {
            return it.value();
        }
    }

    const QString decls = extractCssClassDeclarations(css, trimmedCls);
    if (decls.isEmpty()) {
        QMutexLocker locker(&s_cssCache.mutex);
        if (s_cssCache.colors.size() > 4096) {
            s_cssCache.colors.clear();
        }
        s_cssCache.colors.insert(cacheKey, QColor());
        return QColor();
    }

    // Match foreground color: <value>; (not background-color or border-color)
    static const QRegularExpression colorRe(QStringLiteral(R"((?<![-\w])color\s*:\s*([^;!\}]+))"), QRegularExpression::CaseInsensitiveOption);
    const auto match = colorRe.match(decls);
    QColor result;
    if (match.hasMatch()) {
        const QString colorStr = match.captured(1).trimmed();
        QColor col(colorStr);
        if (col.isValid()) {
            result = col;
        }
    }

    {
        QMutexLocker locker(&s_cssCache.mutex);
        if (s_cssCache.colors.size() > 4096) {
            s_cssCache.colors.clear();
        }
        s_cssCache.colors.insert(cacheKey, result);
    }
    return result;
}

QString defaultAuthorTag(const QString &customName)
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = QStringLiteral("user");

    QString host = QSysInfo::machineHostName();
    if (host.isEmpty()) host = qEnvironmentVariable("COMPUTERNAME");
    if (host.isEmpty()) host = qEnvironmentVariable("HOSTNAME");
    if (host.isEmpty()) host = QStringLiteral("localhost");

    const QString tag = QStringLiteral("%1@%2").arg(user, host);

    if (customName.trimmed().isEmpty()) {
        return tag;
    }
    if (customName.contains(QLatin1Char('@'))) {
        return customName.trimmed();
    }
    return QStringLiteral("%1 <%2>").arg(customName.trimmed(), tag);
}

QString formatIsoDateWithLocal(const QString &isoDate)
{
    const QString trimmed = isoDate.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!dt.isValid()) {
        return trimmed;
    }

    const QDateTime localDt = dt.toLocalTime();
    const QString offsetStr = localDt.offsetFromUtc() == 0 ? QStringLiteral("UTC")
        : QStringLiteral("UTC%1%2:%3")
            .arg(localDt.offsetFromUtc() >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
            .arg(std::abs(localDt.offsetFromUtc()) / 3600, 2, 10, QLatin1Char('0'))
            .arg((std::abs(localDt.offsetFromUtc()) % 3600) / 60, 2, 10, QLatin1Char('0'));

    const QString localFormatted = QStringLiteral("%1 %2")
        .arg(localDt.toString(QStringLiteral("yyyy-MM-dd hh:mm AP")), offsetStr);

    return QStringLiteral("%1 (%2)").arg(trimmed, localFormatted);
}

} // namespace DiffUtil
