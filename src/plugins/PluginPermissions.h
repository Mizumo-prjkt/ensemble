#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <utility>

namespace EnsemblePlugin {

struct PluginNetworkStats {
    QString publicId;
    QString pluginName;
    bool hasNetworkCapabilities = false;
    bool allowNetwork = false;
    QStringList allowedDomains;
    QStringList detectedDomains;
    qint64 requestsCount = 0;
    qint64 bytesSent = 0;
    qint64 bytesReceived = 0;
    QDateTime lastRequestTime;
    QString lastUrl;
    QString lastStatus;
};

// Character lookalike and script detection for strict URL security
inline std::pair<QString, QChar> identifyHomographLookalike(QChar ch) {
    uint cp = ch.unicode();
    QChar lookalike;
    switch (cp) {
        // Cyrillic lookalikes
        case 0x0430: lookalike = QLatin1Char('a'); break;
        case 0x0410: lookalike = QLatin1Char('A'); break;
        case 0x0432: lookalike = QLatin1Char('b'); break;
        case 0x0412: lookalike = QLatin1Char('B'); break;
        case 0x0441: lookalike = QLatin1Char('c'); break;
        case 0x0421: lookalike = QLatin1Char('C'); break;
        case 0x0435: lookalike = QLatin1Char('e'); break;
        case 0x0415: lookalike = QLatin1Char('E'); break;
        case 0x0454: lookalike = QLatin1Char('e'); break;
        case 0x0456: lookalike = QLatin1Char('i'); break;
        case 0x0406: lookalike = QLatin1Char('I'); break;
        case 0x0458: lookalike = QLatin1Char('j'); break;
        case 0x0408: lookalike = QLatin1Char('J'); break;
        case 0x043A: lookalike = QLatin1Char('k'); break;
        case 0x041A: lookalike = QLatin1Char('K'); break;
        case 0x043C: lookalike = QLatin1Char('m'); break;
        case 0x041C: lookalike = QLatin1Char('M'); break;
        case 0x043D: lookalike = QLatin1Char('h'); break;
        case 0x041D: lookalike = QLatin1Char('H'); break;
        case 0x043E: lookalike = QLatin1Char('o'); break;
        case 0x041E: lookalike = QLatin1Char('O'); break;
        case 0x0440: lookalike = QLatin1Char('p'); break;
        case 0x0420: lookalike = QLatin1Char('P'); break;
        case 0x0455: lookalike = QLatin1Char('s'); break;
        case 0x0405: lookalike = QLatin1Char('S'); break;
        case 0x0442: lookalike = QLatin1Char('t'); break;
        case 0x0422: lookalike = QLatin1Char('T'); break;
        case 0x0443: lookalike = QLatin1Char('y'); break;
        case 0x0423: lookalike = QLatin1Char('Y'); break;
        case 0x0445: lookalike = QLatin1Char('x'); break;
        case 0x0425: lookalike = QLatin1Char('X'); break;
        case 0x04BB: lookalike = QLatin1Char('h'); break;
        // Greek lookalikes
        case 0x03B1: lookalike = QLatin1Char('a'); break;
        case 0x0391: lookalike = QLatin1Char('A'); break;
        case 0x0392: lookalike = QLatin1Char('B'); break;
        case 0x0395: lookalike = QLatin1Char('E'); break;
        case 0x03B5: lookalike = QLatin1Char('e'); break;
        case 0x0397: lookalike = QLatin1Char('H'); break;
        case 0x0399: lookalike = QLatin1Char('I'); break;
        case 0x03B9: lookalike = QLatin1Char('i'); break;
        case 0x039A: lookalike = QLatin1Char('K'); break;
        case 0x03BA: lookalike = QLatin1Char('k'); break;
        case 0x039C: lookalike = QLatin1Char('M'); break;
        case 0x039D: lookalike = QLatin1Char('N'); break;
        case 0x03BD: lookalike = QLatin1Char('v'); break;
        case 0x039F: lookalike = QLatin1Char('O'); break;
        case 0x03BF: lookalike = QLatin1Char('o'); break;
        case 0x03A1: lookalike = QLatin1Char('P'); break;
        case 0x03C1: lookalike = QLatin1Char('p'); break;
        case 0x03A4: lookalike = QLatin1Char('T'); break;
        case 0x03C4: lookalike = QLatin1Char('t'); break;
        case 0x03A5: lookalike = QLatin1Char('Y'); break;
        case 0x03C5: lookalike = QLatin1Char('u'); break;
        case 0x03A7: lookalike = QLatin1Char('X'); break;
        case 0x03C7: lookalike = QLatin1Char('x'); break;
        case 0x0396: lookalike = QLatin1Char('Z'); break;
        default: break;
    }

    QString script;
    if ((cp >= 0x0400 && cp <= 0x052F) || (cp >= 0x2DE0 && cp <= 0x2DFF) || (cp >= 0xA640 && cp <= 0xA69F)) {
        script = QStringLiteral("Cyrillic");
    } else if ((cp >= 0x0370 && cp <= 0x03FF) || (cp >= 0x1F00 && cp <= 0x1FFF)) {
        script = QStringLiteral("Greek");
    } else if (cp >= 0x0530 && cp <= 0x058F) {
        script = QStringLiteral("Armenian");
    } else if (cp >= 0x0590 && cp <= 0x05FF) {
        script = QStringLiteral("Hebrew");
    } else if (cp >= 0x0600 && cp <= 0x06FF) {
        script = QStringLiteral("Arabic");
    } else if (cp >= 0x13A0 && cp <= 0x13FF) {
        script = QStringLiteral("Cherokee");
    } else if (cp >= 0x0080 && cp <= 0x024F) {
        script = QStringLiteral("Latin Extended");
    } else if ((cp >= 0x2000 && cp <= 0x206F) || cp == 0xFEFF) {
        script = QStringLiteral("Zero-Width / Formatting Control");
    } else if (cp >= 0xFF00 && cp <= 0xFFEF) {
        script = QStringLiteral("Fullwidth / Halfwidth Form");
    } else if (cp <= 0x0020 || cp == 0x007F) {
        script = QStringLiteral("Control / Whitespace");
    } else {
        script = QStringLiteral("Non-ASCII Unicode");
    }

    return {script, lookalike};
}

inline QString formatLinkAsCharacterIds(const QString &rawUrl) {
    QString out;
    out.reserve(rawUrl.size() * 4);
    for (int i = 0; i < rawUrl.size(); ++i) {
        QChar ch = rawUrl.at(i);
        if (ch.toLatin1() >= 33 && ch.toLatin1() <= 126) {
            out.append(ch);
        } else {
            auto [script, lookalike] = identifyHomographLookalike(ch);
            if (!lookalike.isNull()) {
                out.append(QStringLiteral("[CHAR_ID:U+%1 (%2 lookalike for ASCII '%3' U+%4)]")
                    .arg(QString::number(ch.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0')))
                    .arg(script)
                    .arg(lookalike)
                    .arg(QString::number(lookalike.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0'))));
            } else {
                out.append(QStringLiteral("[CHAR_ID:U+%1 (%2)]")
                    .arg(QString::number(ch.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0')))
                    .arg(script));
            }
        }
    }
    return out;
}

inline bool detectHomographOrDeceptiveChars(const QString &rawUrl, QString *outDetails, QString *outCharIds) {
    QString trimmed = rawUrl.trimmed();
    int protoIdx = trimmed.indexOf(QStringLiteral("://"));
    QString rest = (protoIdx >= 0) ? trimmed.mid(protoIdx + 3) : trimmed;
    int slashIdx = rest.indexOf(QLatin1Char('/'));
    int qIdx = rest.indexOf(QLatin1Char('?'));
    int hashIdx = rest.indexOf(QLatin1Char('#'));
    int endIdx = rest.size();
    if (slashIdx >= 0) endIdx = qMin(endIdx, slashIdx);
    if (qIdx >= 0) endIdx = qMin(endIdx, qIdx);
    if (hashIdx >= 0) endIdx = qMin(endIdx, hashIdx);

    QString authority = rest.left(endIdx);
    int atIdx = authority.lastIndexOf(QLatin1Char('@'));
    if (atIdx >= 0) authority = authority.mid(atIdx + 1);
    if (!authority.startsWith(QLatin1Char('['))) {
        int colonIdx = authority.indexOf(QLatin1Char(':'));
        if (colonIdx >= 0) authority = authority.left(colonIdx);
    }

    bool hasSuspicious = false;
    QStringList violations;
    for (int i = 0; i < authority.size(); ++i) {
        QChar ch = authority.at(i);
        if (ch.toLatin1() < 33 || ch.toLatin1() > 126 || ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            hasSuspicious = true;
            auto [script, lookalike] = identifyHomographLookalike(ch);
            QString hexCode = QStringLiteral("U+%1").arg(QString::number(ch.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0')));
            if (!lookalike.isNull()) {
                violations.append(QStringLiteral("Character ID %1 (decimal %2, %3 script) disguising as ASCII '%4' (U+%5) at host pos %6")
                    .arg(hexCode)
                    .arg(ch.unicode())
                    .arg(script)
                    .arg(lookalike)
                    .arg(QString::number(lookalike.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0')))
                    .arg(i));
            } else {
                violations.append(QStringLiteral("Character ID %1 (decimal %2, %3 script) at host pos %4")
                    .arg(hexCode)
                    .arg(ch.unicode())
                    .arg(script)
                    .arg(i));
            }
        }
    }

    if (authority.contains(QStringLiteral("xn--"), Qt::CaseInsensitive)) {
        hasSuspicious = true;
        violations.append(QStringLiteral("Punycode IDN domain prefix ('xn--') detected in host '%1'").arg(authority));
    }

    if (hasSuspicious) {
        if (outDetails) *outDetails = violations.join(QStringLiteral("; "));
        if (outCharIds) *outCharIds = formatLinkAsCharacterIds(rawUrl);
        return true;
    }
    return false;
}

struct PluginPermissions {
    bool allowEditorRead = true;
    bool allowEditorWrite = false;
    bool allowStorage = true;
    bool allowUi = true;
    bool allowNetwork = false;
    QStringList allowedDomains; // specific domains whitelisted, e.g. "api.example.com"

    // Detected capabilities via static code analysis of the script:
    bool detectedEditorRead = false;
    bool detectedEditorWrite = false;
    bool detectedStorage = false;
    bool detectedUi = false;
    bool detectedNetwork = false;
    QStringList detectedDomains; // all domains found in code

    QJsonObject toJson() const {
        QJsonObject obj;
        obj[QStringLiteral("allowEditorRead")] = allowEditorRead;
        obj[QStringLiteral("allowEditorWrite")] = allowEditorWrite;
        obj[QStringLiteral("allowStorage")] = allowStorage;
        obj[QStringLiteral("allowUi")] = allowUi;
        obj[QStringLiteral("allowNetwork")] = allowNetwork;
        QJsonArray domArr;
        for (const QString &d : allowedDomains) {
            domArr.append(d);
        }
        obj[QStringLiteral("allowedDomains")] = domArr;
        return obj;
    }

    static PluginPermissions fromJson(const QJsonObject &obj) {
        PluginPermissions p;
        if (obj.contains(QStringLiteral("allowEditorRead")))
            p.allowEditorRead = obj[QStringLiteral("allowEditorRead")].toBool(true);
        if (obj.contains(QStringLiteral("allowEditorWrite")))
            p.allowEditorWrite = obj[QStringLiteral("allowEditorWrite")].toBool(false);
        if (obj.contains(QStringLiteral("allowStorage")))
            p.allowStorage = obj[QStringLiteral("allowStorage")].toBool(true);
        if (obj.contains(QStringLiteral("allowUi")))
            p.allowUi = obj[QStringLiteral("allowUi")].toBool(true);
        if (obj.contains(QStringLiteral("allowNetwork")))
            p.allowNetwork = obj[QStringLiteral("allowNetwork")].toBool(false);
        if (obj.contains(QStringLiteral("allowedDomains"))) {
            const QJsonArray arr = obj[QStringLiteral("allowedDomains")].toArray();
            for (const auto &val : arr) {
                QString d = val.toString().trimmed();
                if (!d.isEmpty() && !p.allowedDomains.contains(d, Qt::CaseInsensitive)) {
                    p.allowedDomains.append(d);
                }
            }
        }
        return p;
    }

    bool isDomainAllowed(const QString &host) const {
        if (!allowNetwork) return false;
        QString cleanHost = host.trimmed().toLower();
        if (cleanHost.isEmpty()) return false;
        for (const QString &d : allowedDomains) {
            QString cleanAllowed = d.trimmed().toLower();
            if (cleanAllowed == QStringLiteral("*")) return true;
            if (cleanAllowed == cleanHost) return true;
            if (cleanAllowed.startsWith(QStringLiteral("*.")) &&
                cleanHost.endsWith(cleanAllowed.mid(1))) {
                return true;
            }
        }
        return false;
    }
};

class PluginScanner {
public:
    static void scan(const QString &source, PluginPermissions &perms);
};

} // namespace EnsemblePlugin
