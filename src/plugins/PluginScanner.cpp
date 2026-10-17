#include "PluginPermissions.h"
#include <QRegularExpression>
#include <QUrl>

namespace EnsemblePlugin {

void PluginScanner::scan(const QString &source, PluginPermissions &perms) {
    perms.detectedEditorRead = false;
    perms.detectedEditorWrite = false;
    perms.detectedStorage = false;
    perms.detectedUi = false;
    perms.detectedNetwork = false;
    perms.detectedDomains.clear();

    // 1. Scan for URLs / domains
    static const QRegularExpression urlRegex(
        QStringLiteral(R"((?:https?://)([a-zA-Z0-9.-]+(?::[0-9]+)?))"),
        QRegularExpression::CaseInsensitiveOption);

    auto matchIterator = urlRegex.globalMatch(source);
    while (matchIterator.hasNext()) {
        auto match = matchIterator.next();
        QString rawHost = match.captured(1).trimmed().toLower();
        // Remove port if present to standardize domain
        int colonIdx = rawHost.indexOf(QLatin1Char(':'));
        QString host = (colonIdx > 0) ? rawHost.left(colonIdx) : rawHost;
        if (!host.isEmpty() && !perms.detectedDomains.contains(host, Qt::CaseInsensitive)) {
            perms.detectedDomains.append(host);
        }
    }

    // Also scan for explicit string domain calls like http.get("domain.com/...")
    static const QRegularExpression httpCallRegex(
        QStringLiteral(R"(http\.(?:get|post)\s*\(\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);

    auto httpMatches = httpCallRegex.globalMatch(source);
    while (httpMatches.hasNext()) {
        auto match = httpMatches.next();
        QString target = match.captured(1).trimmed();
        QUrl parsed(target);
        QString host = parsed.host().toLower();
        if (host.isEmpty() && !target.contains(QLatin1String("://"))) {
            // Might be a domain without scheme: "api.example.com/endpoint"
            int slash = target.indexOf(QLatin1Char('/'));
            host = (slash > 0 ? target.left(slash) : target).toLower();
        }
        if (!host.isEmpty() && !perms.detectedDomains.contains(host, Qt::CaseInsensitive)) {
            perms.detectedDomains.append(host);
        }
    }

    perms.detectedDomains.sort();

    // 2. Scan for Network calls
    if (source.contains(QStringLiteral("http."), Qt::CaseInsensitive) || !perms.detectedDomains.isEmpty()) {
        perms.detectedNetwork = true;
    }

    // 3. Scan for Editor Read calls
    if (source.contains(QStringLiteral("editor.get_chapter_text")) ||
        source.contains(QStringLiteral("editor.get_selection")) ||
        source.contains(QStringLiteral("editor.get_chapter_title")) ||
        source.contains(QStringLiteral("editor.get_chapter_count")) ||
        source.contains(QStringLiteral("editor.get_all_chapters")) ||
        source.contains(QStringLiteral("editor.get_chapter")) ||
        source.contains(QStringLiteral("editor.get_word_count"))) {
        perms.detectedEditorRead = true;
    }

    // 4. Scan for Editor Write calls
    if (source.contains(QStringLiteral("editor.replace_selection")) ||
        source.contains(QStringLiteral("editor.insert_text")) ||
        source.contains(QStringLiteral("editor.set_chapter_text"))) {
        perms.detectedEditorWrite = true;
    }

    // 5. Scan for Storage calls
    if (source.contains(QStringLiteral("storage."))) {
        perms.detectedStorage = true;
    }

    // 6. Scan for UI Window calls
    if (source.contains(QStringLiteral("ui."))) {
        perms.detectedUi = true;
    }
}

} // namespace EnsemblePlugin
