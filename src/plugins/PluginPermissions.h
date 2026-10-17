#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace EnsemblePlugin {

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
