#include "ThemeManager.h"
#include "debug/debug.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QStyleFactory>
#include <QUrl>

static QString resolveWallpaperImageFile(const QString &rawPath)
{
    if (rawPath.isEmpty()) return QString();

    QFileInfo info(rawPath);
    if (!info.exists()) return QString();

    // If it's directly an image file, return it
    if (info.isFile()) return rawPath;

    // If it's a directory (e.g. KDE wallpaper package /usr/share/wallpapers/Cluster/):
    if (info.isDir()) {
        const QString dirPath = rawPath.endsWith(QLatin1Char('/')) ? rawPath : (rawPath + QLatin1Char('/'));

        // 1. Check for quick screenshot/thumbnail
        const QStringList screenshots = {
            dirPath + QStringLiteral("contents/screenshot.png"),
            dirPath + QStringLiteral("contents/screenshot.jpg"),
            dirPath + QStringLiteral("screenshot.png"),
            dirPath + QStringLiteral("screenshot.jpg")
        };
        for (const QString &s : screenshots) {
            if (QFileInfo::exists(s)) return s;
        }

        // 2. Check contents/images_dark or contents/images
        const QStringList candidateDirs = {
            dirPath + QStringLiteral("contents/images_dark"),
            dirPath + QStringLiteral("contents/images"),
            dirPath
        };

        const QStringList nameFilters = {
            QStringLiteral("*.png"), QStringLiteral("*.jpg"),
            QStringLiteral("*.jpeg"), QStringLiteral("*.webp"),
            QStringLiteral("*.avif")
        };

        for (const QString &subDir : candidateDirs) {
            QDir d(subDir);
            if (d.exists()) {
                QFileInfoList files = d.entryInfoList(nameFilters, QDir::Files, QDir::Size);
                if (!files.isEmpty()) {
                    return files.first().absoluteFilePath();
                }
            }
        }
    }

    return QString();
}

static QColor extractDominantColor(const QImage &img)
{
    if (img.isNull()) return QColor();

    QImage small = img.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    struct Bucket {
        quint64 sumR = 0, sumG = 0, sumB = 0;
        quint64 score = 0;
        int count = 0;
    };
    Bucket buckets[12];
    quint64 totalR = 0, totalG = 0, totalB = 0;
    int totalCount = 0;

    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            QColor c = small.pixelColor(x, y);
            const int s = c.saturation();
            const int v = c.value();

            if (v > 35 && v < 245) {
                totalR += c.red();
                totalG += c.green();
                totalB += c.blue();
                totalCount++;

                if (s >= 30) {
                    int h = c.hue();
                    if (h < 0) h = 0;
                    int bucketIdx = (h / 30) % 12;
                    buckets[bucketIdx].sumR += c.red();
                    buckets[bucketIdx].sumG += c.green();
                    buckets[bucketIdx].sumB += c.blue();
                    buckets[bucketIdx].score += static_cast<quint64>(s * v);
                    buckets[bucketIdx].count++;
                }
            }
        }
    }

    int bestIdx = -1;
    quint64 maxScore = 0;
    for (int i = 0; i < 12; ++i) {
        if (buckets[i].count > 0 && buckets[i].score > maxScore) {
            maxScore = buckets[i].score;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0 && buckets[bestIdx].count > 0) {
        int r = static_cast<int>(buckets[bestIdx].sumR / buckets[bestIdx].count);
        int g = static_cast<int>(buckets[bestIdx].sumG / buckets[bestIdx].count);
        int b = static_cast<int>(buckets[bestIdx].sumB / buckets[bestIdx].count);
        QColor res(r, g, b);
        int h = res.hue();
        int s = qMax(res.saturation(), 110);
        int v = qBound(120, res.value(), 215);
        return QColor::fromHsv(h, s, v);
    }

    if (totalCount > 0) {
        return QColor(totalR / totalCount, totalG / totalCount, totalB / totalCount);
    }

    return QColor();
}

ThemeManager &ThemeManager::instance()
{
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    loadSettings();
}

void ThemeManager::rescanWallpaperColor()
{
    qCInfo(logApp) << "[Theme] Scanning for active desktop wallpaper and accent color...";
    QString rawPath;
    QString source;

#if defined(Q_OS_WIN)
    // Query Windows Desktop Wallpaper registry key
    QSettings winDesktop(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Desktop"), QSettings::NativeFormat);
    const QString winWallpaper = winDesktop.value(QStringLiteral("Wallpaper")).toString();
    if (!winWallpaper.isEmpty() && QFileInfo::exists(winWallpaper)) {
        rawPath = winWallpaper;
        source = QStringLiteral("Windows Desktop Registry");
        qCDebug(logApp) << "[Theme] Detected wallpaper via Windows registry:" << rawPath;
    }
#elif defined(Q_OS_LINUX)
    // 1. Try querying KDE Plasma shell via DBus evaluateScript
    QProcess dbusProc;
    dbusProc.start(QStringLiteral("qdbus6"), {
        QStringLiteral("org.kde.plasmashell"),
        QStringLiteral("/PlasmaShell"),
        QStringLiteral("org.kde.PlasmaShell.evaluateScript"),
        QStringLiteral("var d = desktops(); for (var i = 0; i < d.length; i++) { d[i].currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General']; var img = d[i].readConfig('Image'); if (img) { print(img); break; } }")
    });
    if (dbusProc.waitForFinished(1000) && dbusProc.exitCode() == 0) {
        QString out = QString::fromUtf8(dbusProc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty() && out != QStringLiteral("undefined")) {
            rawPath = out;
            source = QStringLiteral("KDE Plasma DBus");
            qCDebug(logApp) << "[Theme] Detected wallpaper via KDE DBus script:" << rawPath;
        }
    }

    // 2. Fallback to KDE plasma desktop-appletsrc configuration file
    if (rawPath.isEmpty()) {
        const QString plasmaConfig = QStringLiteral("%1/.config/plasma-org.kde.plasma.desktop-appletsrc").arg(QDir::homePath());
        if (QFileInfo::exists(plasmaConfig)) {
            QSettings plasma(plasmaConfig, QSettings::IniFormat);
            const QStringList keys = plasma.allKeys();
            for (const QString &k : keys) {
                if (k.endsWith(QStringLiteral("/Image")) || k.endsWith(QStringLiteral("/wallpaper"))) {
                    QString val = plasma.value(k).toString();
                    if (!val.isEmpty()) {
                        rawPath = val;
                        source = QStringLiteral("KDE Plasma config file");
                        qCDebug(logApp) << "[Theme] Detected wallpaper via KDE config key" << k << ":" << rawPath;
                        break;
                    }
                }
            }
        }
    }

    // 3. Check GNOME gsettings picture-uri-dark or picture-uri
    if (rawPath.isEmpty()) {
        QProcess proc;
        proc.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.background"), isDarkMode() ? QStringLiteral("picture-uri-dark") : QStringLiteral("picture-uri")});
        if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
            QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!out.isEmpty() && out != QStringLiteral("''") && out != QStringLiteral("\"\"")) {
                rawPath = out;
                source = QStringLiteral("GNOME gsettings");
                qCDebug(logApp) << "[Theme] Detected wallpaper via GNOME gsettings:" << rawPath;
            }
        }
    }

    // 4. Check GNOME picture-uri light fallback
    if (rawPath.isEmpty()) {
        QProcess proc;
        proc.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.background"), QStringLiteral("picture-uri")});
        if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
            QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!out.isEmpty() && out != QStringLiteral("''") && out != QStringLiteral("\"\"")) {
                rawPath = out;
                source = QStringLiteral("GNOME gsettings (light)");
                qCDebug(logApp) << "[Theme] Detected wallpaper via GNOME light fallback:" << rawPath;
            }
        }
    }
#endif

    // Clean up rawPath
    rawPath = rawPath.remove(QLatin1Char('\'')).remove(QLatin1Char('"')).trimmed();
    if (rawPath.startsWith(QLatin1String("file://"))) {
        rawPath = QUrl(rawPath).toLocalFile();
    }
    while (rawPath.endsWith(QLatin1Char('/')) && rawPath.length() > 1) {
        rawPath.chop(1);
    }

    QString imageFile = resolveWallpaperImageFile(rawPath);

    if (!imageFile.isEmpty() && QFileInfo::exists(imageFile)) {
        qCDebug(logApp) << "[Theme] Resolving image file from" << source << "path:" << imageFile;
        QImage img(imageFile);
        if (!img.isNull()) {
            QColor extracted = extractDominantColor(img);
            if (extracted.isValid()) {
                m_materialYouAccentColor = extracted;
                m_wallpaperPath = imageFile;
                m_wallpaperName = QFileInfo(rawPath).isDir() ? QFileInfo(rawPath).fileName() : QFileInfo(imageFile).completeBaseName();
                m_wallpaperStatus = QStringLiteral("Extracted from %1: %2").arg(source, m_wallpaperName);
                qCInfo(logApp) << "[Theme] Extracted Material You dynamic accent color:" << m_materialYouAccentColor.name()
                               << "from" << imageFile << "(" << source << ")";
                return;
            } else {
                qCWarning(logApp) << "[Theme] Failed to extract chromatic accent from image:" << imageFile;
            }
        } else {
            qCWarning(logApp) << "[Theme] QImage failed to decode wallpaper file:" << imageFile;
        }
    } else {
        qCWarning(logApp) << "[Theme] No valid wallpaper image file found. Raw path was:" << rawPath;
    }

    // Fallback: Check desktop environment accent color
#if defined(Q_OS_WIN)
    // Check Windows DWM colorization / accent color
    QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"), QSettings::NativeFormat);
    const QVariant val = dwm.value(QStringLiteral("ColorizationColor"));
    if (val.isValid()) {
        const uint argb = val.toUInt();
        const int r = (argb >> 16) & 0xFF;
        const int g = (argb >> 8) & 0xFF;
        const int b = argb & 0xFF;
        const QColor dwmColor(r, g, b);
        if (dwmColor.isValid() && dwmColor != QColor(0, 0, 0)) {
            m_materialYouAccentColor = dwmColor;
            m_wallpaperPath.clear();
            m_wallpaperName = QStringLiteral("Windows DWM Accent");
            m_wallpaperStatus = QStringLiteral("Windows System Accent (%1)").arg(dwmColor.name());
            qCInfo(logApp) << "[Theme] Using Windows DWM accent color:" << dwmColor.name();
            return;
        }
    }
#elif defined(Q_OS_LINUX)
    QProcess accentProc;
    accentProc.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("accent-color")});
    if (accentProc.waitForFinished(1000) && accentProc.exitCode() == 0) {
        QString acc = QString::fromUtf8(accentProc.readAllStandardOutput()).trimmed().remove(QLatin1Char('\'')).remove(QLatin1Char('"'));
        if (!acc.isEmpty()) {
            QColor c;
            if (acc == QStringLiteral("blue")) c = QColor(0x35, 0x84, 0xe4);
            else if (acc == QStringLiteral("teal")) c = QColor(0x21, 0x90, 0xa4);
            else if (acc == QStringLiteral("green")) c = QColor(0x3a, 0x94, 0x4c);
            else if (acc == QStringLiteral("yellow")) c = QColor(0xe5, 0xa5, 0x0a);
            else if (acc == QStringLiteral("orange")) c = QColor(0xed, 0x5b, 0x00);
            else if (acc == QStringLiteral("red")) c = QColor(0xe0, 0x1b, 0x24);
            else if (acc == QStringLiteral("pink")) c = QColor(0xd5, 0x61, 0x99);
            else if (acc == QStringLiteral("purple")) c = QColor(0x91, 0x41, 0xac);
            else if (acc == QStringLiteral("slate")) c = QColor(0x62, 0x6e, 0x7a);
            if (c.isValid()) {
                m_materialYouAccentColor = c;
                m_wallpaperPath.clear();
                m_wallpaperName = acc;
                m_wallpaperStatus = QStringLiteral("Desktop Accent Color (%1)").arg(acc);
                qCInfo(logApp) << "[Theme] Using desktop accent color fallback:" << acc << m_materialYouAccentColor.name();
                return;
            }
        }
    }
#endif

    m_materialYouAccentColor = QColor(0x79, 0x86, 0xcb);
    m_wallpaperPath.clear();
    m_wallpaperName = QStringLiteral("Default Palette");
    m_wallpaperStatus = QStringLiteral("Default Material 3 fallback (#7986cb)");
    qCWarning(logApp) << "[Theme] Wallpaper extraction fell back to default Material 3 accent:" << m_materialYouAccentColor.name();
}

void ThemeManager::loadSettings()
{
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    const QString themeStr = settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString().toLower();
    if (themeStr == QStringLiteral("darker")) {
        m_currentTheme = AppTheme::Darker;
    } else if (themeStr == QStringLiteral("light")) {
        m_currentTheme = AppTheme::Light;
    } else {
        m_currentTheme = AppTheme::Dark;
    }

    const QString flavorStr = settings.value(QStringLiteral("skinFlavor"), QStringLiteral("ensemble")).toString().toLower();
    if (flavorStr == QStringLiteral("materialyou")) {
        m_skinFlavor = SkinFlavor::MaterialYou;
    } else if (flavorStr == QStringLiteral("qt")) {
        m_skinFlavor = SkinFlavor::StandardQt;
    } else if (flavorStr == QStringLiteral("kde")) {
        m_skinFlavor = SkinFlavor::KdeUi;
    } else if (flavorStr == QStringLiteral("custom")) {
        m_skinFlavor = SkinFlavor::Custom;
    } else {
        m_skinFlavor = SkinFlavor::StandardEnsemble;
    }

    rescanWallpaperColor();
}

void ThemeManager::saveSettings()
{
    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    if (m_currentTheme == AppTheme::Darker) {
        settings.setValue(QStringLiteral("theme"), QStringLiteral("darker"));
    } else if (m_currentTheme == AppTheme::Light) {
        settings.setValue(QStringLiteral("theme"), QStringLiteral("light"));
    } else {
        settings.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
    }

    QString flavorStr = QStringLiteral("ensemble");
    switch (m_skinFlavor) {
    case SkinFlavor::MaterialYou: flavorStr = QStringLiteral("materialyou"); break;
    case SkinFlavor::StandardQt: flavorStr = QStringLiteral("qt"); break;
    case SkinFlavor::KdeUi: flavorStr = QStringLiteral("kde"); break;
    case SkinFlavor::Custom: flavorStr = QStringLiteral("custom"); break;
    default: break;
    }
    settings.setValue(QStringLiteral("skinFlavor"), flavorStr);
}

void ThemeManager::applyCurrentThemeAndSkin()
{
    if (!qApp) return;

    if (m_skinFlavor == SkinFlavor::KdeUi) {
        bool set = false;
        const auto available = QStyleFactory::keys();
        for (const QString &key : available) {
            if (key.compare(QStringLiteral("Breeze"), Qt::CaseInsensitive) == 0) {
                qApp->setStyle(QStyleFactory::create(key));
                set = true;
                break;
            }
        }
        if (!set) {
            qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        }
    } else {
        qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }

    qApp->setPalette(getPalette());
    qApp->setStyleSheet(getApplicationStyleSheet());
}

void ThemeManager::setTheme(AppTheme theme)
{
    m_currentTheme = theme;
    saveSettings();
    applyCurrentThemeAndSkin();
    emit themeChanged(m_currentTheme);
}

void ThemeManager::setSkinFlavor(SkinFlavor flavor)
{
    m_skinFlavor = flavor;
    if (flavor == SkinFlavor::MaterialYou) {
        rescanWallpaperColor();
    }
    saveSettings();
    applyCurrentThemeAndSkin();
    emit skinFlavorChanged(m_skinFlavor);
    emit themeChanged(m_currentTheme);
}

void ThemeManager::toggleTheme()
{
    if (m_currentTheme == AppTheme::Dark) {
        setTheme(AppTheme::Darker);
    } else if (m_currentTheme == AppTheme::Darker) {
        setTheme(AppTheme::Light);
    } else {
        setTheme(AppTheme::Dark);
    }
}

int ThemeManager::buttonRadius() const
{
    switch (m_skinFlavor) {
    case SkinFlavor::MaterialYou: return 20; // Authentic Android M3 pill button
    case SkinFlavor::KdeUi: return 3;       // KDE Breeze subtle 3px radius
    case SkinFlavor::StandardQt: return 4;   // Qt Fusion 4px radius
    case SkinFlavor::StandardEnsemble: return 6;
    default: return 6;
    }
}

int ThemeManager::cardRadius() const
{
    switch (m_skinFlavor) {
    case SkinFlavor::MaterialYou: return 22; // Android M3 rounded card
    case SkinFlavor::KdeUi: return 4;       // KDE Breeze container
    case SkinFlavor::StandardQt: return 6;   // Qt frame
    case SkinFlavor::StandardEnsemble: return 12;
    default: return 8;
    }
}

int ThemeManager::inputRadius() const
{
    switch (m_skinFlavor) {
    case SkinFlavor::MaterialYou: return 14; // Android M3 rounded input field
    case SkinFlavor::KdeUi: return 3;
    case SkinFlavor::StandardQt: return 4;
    case SkinFlavor::StandardEnsemble: return 6;
    default: return 6;
    }
}

int ThemeManager::tabRadius() const
{
    switch (m_skinFlavor) {
    case SkinFlavor::MaterialYou: return 18; // Android M3 pill chip tab
    case SkinFlavor::KdeUi: return 3;
    case SkinFlavor::StandardQt: return 4;
    case SkinFlavor::StandardEnsemble: return 6;
    default: return 6;
    }
}

QString ThemeManager::buttonPadding() const
{
    switch (m_skinFlavor) {
    case SkinFlavor::MaterialYou: return QStringLiteral("8px 22px");
    case SkinFlavor::KdeUi: return QStringLiteral("6px 14px");
    case SkinFlavor::StandardQt: return QStringLiteral("6px 16px");
    case SkinFlavor::StandardEnsemble: return QStringLiteral("7px 16px");
    default: return QStringLiteral("7px 16px");
    }
}

QColor ThemeManager::primaryButtonBgColor() const
{
    return accentColor();
}

QColor ThemeManager::primaryButtonTextColor() const
{
    return QColor(QStringLiteral("#ffffff"));
}

QColor ThemeManager::selectionTextColor() const
{
    const QColor bg = selectionBgColor();
    const double lum = 0.2126 * (bg.redF()) + 0.7152 * (bg.greenF()) + 0.0722 * (bg.blueF());
    return (lum > 0.45) ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#ffffff"));
}

QString ThemeManager::selectionBgCss() const
{
    return selectionBgColor().name();
}

QPalette ThemeManager::getPalette() const
{
    QPalette palette;
    const QColor bg = windowBgColor();
    const QColor surface = surfaceBgColor();
    const QColor card = cardBgColor();
    const QColor text = primaryTextColor();
    const QColor muted = mutedTextColor();
    const QColor accent = accentColor();
    const QColor highlight = selectionBgColor();

    if (isDarkMode()) {
        palette.setColor(QPalette::Window, bg);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, surface);
        palette.setColor(QPalette::AlternateBase, card);
        palette.setColor(QPalette::ToolTipBase, card);
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, card);
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::BrightText, Qt::white);
        palette.setColor(QPalette::Link, accent);
        palette.setColor(QPalette::Highlight, highlight);
        palette.setColor(QPalette::HighlightedText, primaryButtonTextColor());
        palette.setColor(QPalette::PlaceholderText, muted);
        palette.setColor(QPalette::Disabled, QPalette::Text, muted);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    } else {
        palette.setColor(QPalette::Window, bg);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, surface);
        palette.setColor(QPalette::AlternateBase, card);
        palette.setColor(QPalette::ToolTipBase, surface);
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, card);
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::BrightText, Qt::white);
        palette.setColor(QPalette::Link, accent);
        palette.setColor(QPalette::Highlight, highlight);
        palette.setColor(QPalette::HighlightedText, primaryButtonTextColor());
        palette.setColor(QPalette::PlaceholderText, muted);
        palette.setColor(QPalette::Disabled, QPalette::Text, muted);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    }

    return palette;
}

QColor ThemeManager::windowBgColor() const
{
    if (isOledMode())
        return QColor(QStringLiteral("#000000"));
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return isDarkMode() ? QColor(QStringLiteral("#232629")) : QColor(QStringLiteral("#eff0f1"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#2b2b2b")) : QColor(QStringLiteral("#f0f0f0"));
    if (m_skinFlavor == SkinFlavor::MaterialYou) {
        if (isDarkMode()) return QColor(QStringLiteral("#13151b"));
        return QColor(QStringLiteral("#f7f8fc"));
    }
    return isDarkMode() ? QColor(QStringLiteral("#14151a")) : QColor(QStringLiteral("#f8fafc"));
}

QColor ThemeManager::surfaceBgColor() const
{
    if (isOledMode())
        return QColor(QStringLiteral("#08080a"));
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return isDarkMode() ? QColor(QStringLiteral("#2a2e32")) : QColor(QStringLiteral("#f7f7f7"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#323232")) : QColor(QStringLiteral("#e6e6e6"));
    if (m_skinFlavor == SkinFlavor::MaterialYou) {
        if (isDarkMode()) return QColor(QStringLiteral("#1c1e27"));
        return QColor(QStringLiteral("#eef1f7"));
    }
    return isDarkMode() ? QColor(QStringLiteral("#1a1b23")) : QColor(QStringLiteral("#f1f5f9"));
}

QColor ThemeManager::cardBgColor() const
{
    if (isOledMode())
        return QColor(QStringLiteral("#101014"));
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return isDarkMode() ? QColor(QStringLiteral("#31363b")) : QColor(QStringLiteral("#ffffff"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#3c3c3c")) : QColor(QStringLiteral("#ffffff"));
    if (m_skinFlavor == SkinFlavor::MaterialYou) {
        if (isDarkMode()) return QColor(QStringLiteral("#242733"));
        return QColor(QStringLiteral("#ffffff"));
    }
    return isDarkMode() ? QColor(QStringLiteral("#21222c")) : QColor(QStringLiteral("#ffffff"));
}

QColor ThemeManager::primaryTextColor() const
{
    if (isOledMode())
        return QColor(QStringLiteral("#ffffff"));
    return isDarkMode() ? QColor(QStringLiteral("#f8f8f2")) : QColor(QStringLiteral("#0f172a"));
}

QColor ThemeManager::mutedTextColor() const
{
    if (isOledMode())
        return QColor(QStringLiteral("#a1a1aa"));
    return isDarkMode() ? QColor(QStringLiteral("#94a3b8")) : QColor(QStringLiteral("#334155"));
}

QColor ThemeManager::accentColor() const
{
    if (m_skinFlavor == SkinFlavor::MaterialYou && m_materialYouAccentColor.isValid())
        return m_materialYouAccentColor;
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return QColor(QStringLiteral("#3daee9"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#4a90e2")) : QColor(QStringLiteral("#2980b9"));

    return isDarkMode() ? QColor(QStringLiteral("#7f5af0")) : QColor(QStringLiteral("#6366f1"));
}

QColor ThemeManager::accentHoverColor() const
{
    if (m_skinFlavor == SkinFlavor::MaterialYou && m_materialYouAccentColor.isValid())
        return isDarkMode() ? m_materialYouAccentColor.lighter(118) : m_materialYouAccentColor.darker(115);
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return QColor(QStringLiteral("#2980b9"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#6ba4e8")) : QColor(QStringLiteral("#3498db"));

    return isDarkMode() ? QColor(QStringLiteral("#6c47d9")) : QColor(QStringLiteral("#4f46e5"));
}

QColor ThemeManager::borderColor() const
{
    if (isOledMode())
        return QColor(QStringLiteral("#202026"));
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return isDarkMode() ? QColor(QStringLiteral("#31363b")) : QColor(QStringLiteral("#bdc3c7"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#444444")) : QColor(QStringLiteral("#cccccc"));

    return isDarkMode() ? QColor(QStringLiteral("#2e2f3e")) : QColor(QStringLiteral("#cbd5e1"));
}

QColor ThemeManager::selectionBgColor() const
{
    if (m_skinFlavor == SkinFlavor::MaterialYou && m_materialYouAccentColor.isValid()) {
        QColor sel = m_materialYouAccentColor;
        return isDarkMode() ? sel : (sel.value() > 180 ? sel.darker(120) : sel);
    }
    if (m_skinFlavor == SkinFlavor::KdeUi)
        return isDarkMode() ? QColor(QStringLiteral("#2d5c7f")) : QColor(QStringLiteral("#2980b9"));
    if (m_skinFlavor == SkinFlavor::StandardQt)
        return isDarkMode() ? QColor(QStringLiteral("#264f78")) : QColor(QStringLiteral("#2980b9"));

    return isDarkMode() ? QColor(QStringLiteral("#44475a")) : QColor(QStringLiteral("#4338ca"));
}

QString ThemeManager::getEditorPaneStyleSheet() const
{
    const QString bg = isOledMode() ? QStringLiteral("#050505") : (isDarkMode() ? QStringLiteral("#181920") : QStringLiteral("#ffffff"));
    const QString text = primaryTextColor().name();
    const QString border = borderColor().name();
    const QString selBg = selectionBgCss();
    const QString selText = selectionTextColor().name();

    return QStringLiteral(
        "QTextEdit {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  selection-background-color: %5;"
        "  selection-color: %6;"
        "  padding: 14px;"
        "}"
    ).arg(bg, text, border).arg(cardRadius()).arg(selBg, selText);
}

QString ThemeManager::getCodeEditorStyleSheet() const
{
    const QString bg = isOledMode() ? QStringLiteral("#000000") : (isDarkMode() ? QStringLiteral("#14151a") : QStringLiteral("#ffffff"));
    const QString text = primaryTextColor().name();
    const QString border = borderColor().name();
    const QString selBg = selectionBgCss();
    const QString selText = selectionTextColor().name();

    return QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  selection-background-color: %5;"
        "  selection-color: %6;"
        "  font-family: 'Consolas', 'Monospace', monospace;"
        "}"
    ).arg(bg, text, border).arg(inputRadius()).arg(selBg, selText);
}

// Syntax highlighting colors
QColor ThemeManager::syntaxTagColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#569CD6")) : QColor(QStringLiteral("#0969da"));
}

QColor ThemeManager::syntaxAttributeColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#9CDCFE")) : QColor(QStringLiteral("#953800"));
}

QColor ThemeManager::syntaxValueColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#CE9178")) : QColor(QStringLiteral("#0a3069"));
}

QColor ThemeManager::syntaxCommentColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#6A9955")) : QColor(QStringLiteral("#6e7781"));
}

QColor ThemeManager::syntaxEntityColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#DCDCAA")) : QColor(QStringLiteral("#8250df"));
}

QColor ThemeManager::syntaxSelectorColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#D7BA7D")) : QColor(QStringLiteral("#116329"));
}

QColor ThemeManager::syntaxPropertyColor() const
{
    return isDarkMode() ? QColor(QStringLiteral("#9CDCFE")) : QColor(QStringLiteral("#0550ae"));
}

QString ThemeManager::getApplicationStyleSheet() const
{
    const QString bg = windowBgColor().name();
    const QString surface = surfaceBgColor().name();
    const QString card = cardBgColor().name();
    const QString text = primaryTextColor().name();
    const QString muted = mutedTextColor().name();
    const QString border = borderColor().name();
    const QString accent = accentColor().name();
    const QString accentHover = accentHoverColor().name();
    const QString selBg = selectionBgCss();
    const QString selText = selectionTextColor().name();
    const QString hoverBg = isDarkMode() ? QStringLiteral("rgba(255, 255, 255, 0.08)") : QStringLiteral("rgba(0, 0, 0, 0.05)");
    const int btnRad = buttonRadius();
    const int cRad = cardRadius();
    const int inRad = inputRadius();
    const int tabRad = tabRadius();
    const QString btnPad = buttonPadding();

    const bool isM3 = (m_skinFlavor == SkinFlavor::MaterialYou);
    const bool isKde = (m_skinFlavor == SkinFlavor::KdeUi);

    QString tabStyle;
    if (isM3) {
        tabStyle = QStringLiteral(
            "QTabWidget::pane {"
            "  border: 1px solid %1;"
            "  background-color: %2;"
            "  border-radius: %3px;"
            "  padding: 12px;"
            "}"
            "QTabBar::tab {"
            "  background-color: transparent;"
            "  color: %4;"
            "  border: none;"
            "  border-radius: %5px;"
            "  padding: 8px 18px;"
            "  margin: 3px 4px;"
            "  font-weight: 600;"
            "  font-size: 12px;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: %6;"
            "  color: #ffffff;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background-color: %7;"
            "  color: %8;"
            "}"
        ).arg(border, card).arg(cRad).arg(muted).arg(tabRad).arg(accent, selBg, text);
    } else if (isKde) {
        tabStyle = QStringLiteral(
            "QTabWidget::pane {"
            "  border: 1px solid %1;"
            "  background-color: %2;"
            "  border-radius: %3px;"
            "}"
            "QTabBar::tab {"
            "  background-color: %4;"
            "  color: %5;"
            "  border: 1px solid %1;"
            "  border-bottom: none;"
            "  border-top: 2px solid transparent;"
            "  border-radius: 3px;"
            "  padding: 6px 14px;"
            "  margin-right: 2px;"
            "  font-weight: 500;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: %2;"
            "  color: %6;"
            "  border-top: 2px solid %6;"
            "  font-weight: bold;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background-color: %2;"
            "  color: %7;"
            "}"
        ).arg(border, card).arg(cRad).arg(surface, muted, accent, text);
    } else {
        tabStyle = QStringLiteral(
            "QTabWidget::pane {"
            "  border: 1px solid %1;"
            "  background-color: %2;"
            "  border-radius: %3px;"
            "}"
            "QTabBar::tab {"
            "  background-color: %4;"
            "  color: %5;"
            "  border: 1px solid %1;"
            "  border-bottom: none;"
            "  border-top-left-radius: %6px;"
            "  border-top-right-radius: %6px;"
            "  padding: 7px 16px;"
            "  margin-right: 2px;"
            "  font-weight: 600;"
            "  font-size: 12px;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: %2;"
            "  color: %7;"
            "  border-top: 2px solid %7;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background-color: %1;"
            "  color: %8;"
            "}"
        ).arg(border, card).arg(cRad).arg(surface, muted).arg(tabRad).arg(accent, text);
    }

    return QStringLiteral(
        "QMainWindow, QDialog, QMessageBox, QWidget#centralWidget {"
        "  background-color: %1;"
        "  color: %2;"
        "  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "}"
        "* {"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QMessageBox QLabel, QDialog QLabel {"
        "  color: %2;"
        "}"
        "QPushButton {"
        "  background-color: %5;"
        "  color: %2;"
        "  border: 1px solid %6;"
        "  border-radius: %7px;"
        "  padding: %8;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background-color: %9;"
        "  border-color: %10;"
        "  color: %10;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %10;"
        "  color: #ffffff;"
        "}"
        "QPushButton:disabled {"
        "  color: %11;"
        "  border-color: %6;"
        "  background-color: transparent;"
        "}"
        "QPushButton:default, QPushButton#closeBtn, QPushButton#startWritingBtn {"
        "  background-color: %10;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %7px;"
        "  padding: 9px 24px;"
        "  font-weight: bold;"
        "  font-size: 13px;"
        "}"
        "QPushButton:default:hover, QPushButton#closeBtn:hover, QPushButton#startWritingBtn:hover {"
        "  background-color: %12;"
        "  color: #ffffff;"
        "}"
        "QPushButton:default:pressed, QPushButton#closeBtn:pressed, QPushButton#startWritingBtn:pressed {"
        "  background-color: %10;"
        "  color: #ffffff;"
        "}"
        "QLineEdit, QComboBox, QSpinBox {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %6;"
        "  border-radius: %13px;"
        "  padding: 6px 10px;"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus {"
        "  border-color: %10;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %6;"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QListView, QTreeView, QListWidget, QTreeWidget {"
        "  background-color: %14;"
        "  color: %2;"
        "  border: 1px solid %6;"
        "  border-radius: %13px;"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QListView::item:selected, QTreeView::item:selected, QListWidget::item:selected, QTreeWidget::item:selected {"
        "  background-color: %3;"
        "  color: %4;"
        "  font-weight: 600;"
        "}"
        "QListView::item:selected:!active, QTreeView::item:selected:!active, QListWidget::item:selected:!active, QTreeWidget::item:selected:!active {"
        "  background-color: %3;"
        "  color: %4;"
        "}"
        "QListView::item:hover:!selected, QTreeView::item:hover:!selected, QListWidget::item:hover:!selected, QTreeWidget::item:hover:!selected {"
        "  background-color: %16;"
        "  color: %2;"
        "}"
        "QMenuBar {"
        "  background-color: %14;"
        "  color: %2;"
        "  border-bottom: 1px solid %6;"
        "  padding: 2px;"
        "}"
        "QMenuBar::item {"
        "  background: transparent;"
        "  padding: 4px 10px;"
        "  border-radius: %7px;"
        "}"
        "QMenuBar::item:selected {"
        "  background-color: %3;"
        "  color: %4;"
        "}"
        "QMenu {"
        "  background-color: %14;"
        "  color: %2;"
        "  border: 1px solid %6;"
        "  border-radius: %13px;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  padding: 6px 24px 6px 12px;"
        "  border-radius: %7px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: %3;"
        "  color: %4;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background-color: %6;"
        "  margin: 4px 6px;"
        "}"
        "QToolBar {"
        "  background-color: %14;"
        "  border-bottom: 1px solid %6;"
        "  spacing: 4px;"
        "  padding: 2px 4px;"
        "}"
        "QToolButton {"
        "  background-color: transparent;"
        "  color: %2;"
        "  border: 1px solid transparent;"
        "  border-radius: %7px;"
        "  padding: 4px 6px;"
        "}"
        "QToolButton:hover {"
        "  background-color: %3;"
        "  border-color: %10;"
        "}"
        "QToolButton:checked {"
        "  background-color: %10;"
        "  color: #ffffff;"
        "}"
        "%15"
    ).arg(bg)             // 1
     .arg(text)           // 2
     .arg(selBg)          // 3
     .arg(selText)        // 4
     .arg(card)           // 5
     .arg(border)         // 6
     .arg(btnRad)         // 7
     .arg(btnPad)         // 8
     .arg(surface)        // 9
     .arg(accent)         // 10
     .arg(muted)          // 11
     .arg(accentHover)    // 12
     .arg(inRad)          // 13
     .arg(surface)        // 14
     .arg(tabStyle)       // 15
     .arg(hoverBg);       // 16
}

QString ThemeManager::getMainMenuStyleSheet() const
{
    const QString bg = windowBgColor().name();
    const QString surface = surfaceBgColor().name();
    const QString card = cardBgColor().name();
    const QString text = primaryTextColor().name();
    const QString border = borderColor().name();
    const QString accent = accentColor().name();
    const QString accentHover = accentHoverColor().name();
    const QString selBg = selectionBgCss();
    const int cRad = cardRadius();
    const int btnRad = buttonRadius();
    const int inRad = inputRadius();
    const QString muted = mutedTextColor().name();

    return QStringLiteral(
        "MainMenuWidget {"
        "  background-color: %1;"
        "  color: %2;"
        "  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "}"
        "QScrollArea#menuScrollArea {"
        "  background-color: transparent;"
        "  border: none;"
        "}"
        "QFrame#centerCard {"
        "  background-color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: %5px;"
        "}"
        "QFrame#infoBox {"
        "  background-color: %6;"
        "  border: 1px solid %4;"
        "  border-radius: %7px;"
        "}"
        "QPushButton[class=\"actionBtn\"] {"
        "  background-color: %6;"
        "  color: %2;"
        "  border: 1px solid %4;"
        "  border-radius: %8px;"
        "  padding: 0px;"
        "  text-align: left;"
        "}"
        "QPushButton[class=\"actionBtn\"]:hover {"
        "  background-color: %9;"
        "  border-color: %10;"
        "}"
        "QPushButton[class=\"actionBtn\"]:hover QLabel#actionBtnTitle {"
        "  color: %10;"
        "}"
        "QPushButton[class=\"actionBtn\"]:pressed {"
        "  background-color: %10;"
        "}"
        "QPushButton[class=\"actionBtn\"]:disabled {"
        "  background-color: %6;"
        "  border: 1px dashed %4;"
        "  opacity: 0.5;"
        "}"
        "QPushButton[class=\"actionBtn\"]:disabled QLabel#actionBtnTitle {"
        "  color: %12;"
        "}"
        "QPushButton[class=\"actionBtn\"]:disabled QLabel#actionBtnSubtext {"
        "  color: %12;"
        "  opacity: 0.7;"
        "}"
        "QPushButton#startWritingBtn {"
        "  background-color: %10;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %8px;"
        "  padding: 10px 24px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#startWritingBtn:hover {"
        "  background-color: %11;"
        "  color: #ffffff;"
        "}"
        "QPushButton#themeToggleBtn {"
        "  border-radius: %8px;"
        "  padding: 6px;"
        "}"
    ).arg(bg, text, card, border).arg(cRad).arg(surface).arg(inRad).arg(btnRad).arg(selBg, accent, accentHover, muted);
}

QString ThemeManager::getStatusBarStyleSheet() const
{
    const QString bg = surfaceBgColor().name();
    const QString text = mutedTextColor().name();
    const QString border = borderColor().name();

    return QStringLiteral(
        "QStatusBar {"
        "  background-color: %1;"
        "  color: %2;"
        "  border-top: 1px solid %3;"
        "  font-size: 11px;"
        "  padding: 2px 8px;"
        "}"
    ).arg(bg, text, border);
}

QString ThemeManager::getSidebarStyleSheet() const
{
    const QString surface = surfaceBgColor().name();
    const QString card = cardBgColor().name();
    const QString text = primaryTextColor().name();
    const QString border = borderColor().name();
    const QString accent = accentColor().name();
    const QString selBg = selectionBgCss();
    const QString hoverBg = isDarkMode() ? QStringLiteral("rgba(255, 255, 255, 0.08)") : QStringLiteral("rgba(0, 0, 0, 0.05)");
    const int btnRad = buttonRadius();
    const int inRad = inputRadius();

    return QStringLiteral(
        "QListWidget {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %4px;"
        "  padding: 4px;"
        "}"
        "QListWidget::item {"
        "  padding: 8px 10px;"
        "  border-radius: %4px;"
        "  margin-bottom: 2px;"
        "}"
        "QListWidget::item:hover:!selected {"
        "  background-color: %9;"
        "  color: %2;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: %6;"
        "  color: #ffffff;"
        "  font-weight: bold;"
        "}"
        "QListWidget::item:selected:!active {"
        "  background-color: %5;"
        "  color: #ffffff;"
        "}"
        "QPushButton {"
        "  background-color: %7;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: %8px;"
        "  padding: 6px 12px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background-color: %5;"
        "  border-color: %6;"
        "  color: %6;"
        "}"
    ).arg(surface, text, border).arg(inRad).arg(selBg, accent, card).arg(btnRad).arg(hoverBg);
}

QString ThemeManager::getDialogStyleSheet() const
{
    const QString bg = windowBgColor().name();
    const QString surface = surfaceBgColor().name();
    const QString card = cardBgColor().name();
    const QString border = borderColor().name();
    const QString accent = accentColor().name();
    const QString accentHover = accentHoverColor().name();
    const QString text = primaryTextColor().name();
    const QString muted = mutedTextColor().name();
    const QString selBg = selectionBgCss();
    const QString selText = selectionTextColor().name();
    const int btnRad = buttonRadius();
    const int cRad = cardRadius();
    const int inRad = inputRadius();
    const int tabRad = tabRadius();
    const QString btnPad = buttonPadding();

    const bool isM3 = (m_skinFlavor == SkinFlavor::MaterialYou);
    const bool isKde = (m_skinFlavor == SkinFlavor::KdeUi);

    QString tabStyle;
    if (isM3) {
        tabStyle = QStringLiteral(
            "QTabWidget::pane {"
            "  border: 1px solid %1;"
            "  border-radius: %2px;"
            "  background-color: %3;"
            "  padding: 14px;"
            "}"
            "QTabBar::tab {"
            "  background-color: transparent;"
            "  color: %4;"
            "  border: none;"
            "  border-radius: %5px;"
            "  padding: 8px 18px;"
            "  margin: 3px 4px;"
            "  font-weight: 600;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: %6;"
            "  color: #ffffff;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background-color: %7;"
            "  color: %8;"
            "}"
        ).arg(border).arg(cRad).arg(card, muted).arg(tabRad).arg(accent, selBg, text);
    } else if (isKde) {
        tabStyle = QStringLiteral(
            "QTabWidget::pane {"
            "  border: 1px solid %1;"
            "  border-radius: 4px;"
            "  background-color: %2;"
            "  padding: 12px;"
            "}"
            "QTabBar::tab {"
            "  background-color: %3;"
            "  color: %4;"
            "  border: 1px solid %1;"
            "  border-bottom: none;"
            "  border-top: 2px solid transparent;"
            "  border-radius: 3px;"
            "  padding: 6px 14px;"
            "  margin-right: 2px;"
            "  font-weight: 500;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: %2;"
            "  color: %5;"
            "  border-top: 2px solid %5;"
            "  font-weight: bold;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background-color: %2;"
            "  color: %6;"
            "}"
        ).arg(border, card, surface, muted, accent, text);
    } else {
        tabStyle = QStringLiteral(
            "QTabWidget::pane {"
            "  border: 1px solid %1;"
            "  border-radius: %2px;"
            "  background-color: %3;"
            "  padding: 12px;"
            "}"
            "QTabBar::tab {"
            "  background-color: %4;"
            "  color: %5;"
            "  border: 1px solid %1;"
            "  border-bottom: none;"
            "  border-top-left-radius: %6px;"
            "  border-top-right-radius: %6px;"
            "  padding: 8px 16px;"
            "  margin-right: 4px;"
            "  font-weight: 500;"
            "}"
            "QTabBar::tab:selected {"
            "  background-color: %3;"
            "  color: %7;"
            "  border-color: %7;"
            "  font-weight: bold;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background-color: %8;"
            "  color: %9;"
            "}"
        ).arg(border).arg(cRad).arg(card, surface, muted).arg(tabRad).arg(accent, selBg, text);
    }

    return QStringLiteral(
        "QDialog {"
        "  background-color: %1;"
        "  color: %2;"
        "}"
        "* {"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QGroupBox {"
        "  font-weight: bold;"
        "  border: 1px solid %5;"
        "  border-radius: %6px;"
        "  margin-top: 14px;"
        "  padding-top: 18px;"
        "  padding-bottom: 10px;"
        "  padding-left: 12px;"
        "  padding-right: 12px;"
        "  background-color: %7;"
        "  color: %2;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 14px;"
        "  padding: 0 6px;"
        "  color: %8;"
        "}"
        "QLabel {"
        "  color: %2;"
        "}"
        "QLabel[class=\"muted\"] {"
        "  color: %9;"
        "  font-size: 11px;"
        "}"
        "QCheckBox, QRadioButton {"
        "  color: %2;"
        "  spacing: 8px;"
        "}"
        "QCheckBox:disabled, QRadioButton:disabled, QLabel:disabled {"
        "  color: %9;"
        "}"
        "QComboBox, QLineEdit {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %5;"
        "  border-radius: %10px;"
        "  padding: 6px 10px;"
        "  min-height: 22px;"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QComboBox:focus, QLineEdit:focus {"
        "  border: 2px solid %8;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %5;"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "}"
        "QPushButton {"
        "  background-color: %7;"
        "  color: %2;"
        "  border: 1px solid %5;"
        "  border-radius: %11px;"
        "  padding: %12;"
        "  font-weight: 600;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: %13;"
        "  border-color: %8;"
        "  color: %8;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %8;"
        "  color: #ffffff;"
        "}"
        "QPushButton:disabled {"
        "  color: %9;"
        "  border-color: %5;"
        "  background-color: transparent;"
        "}"
        "/* Close Button - Material You Pill Button */"
        "QPushButton#closeBtn, QPushButton:default {"
        "  background-color: %8;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %11px;"
        "  padding: 9px 24px;"
        "  font-weight: bold;"
        "  font-size: 13px;"
        "}"
        "QPushButton#closeBtn:hover, QPushButton:default:hover {"
        "  background-color: %14;"
        "  color: #ffffff;"
        "}"
        "QPushButton#closeBtn:pressed, QPushButton:default:pressed {"
        "  background-color: %8;"
        "  color: #ffffff;"
        "}"
        "%15"
    ).arg(bg)             // 1
     .arg(text)           // 2
     .arg(selBg)          // 3
     .arg(selText)        // 4
     .arg(border)         // 5
     .arg(cRad)           // 6
     .arg(card)           // 7
     .arg(accent)         // 8
     .arg(muted)          // 9
     .arg(inRad)          // 10
     .arg(btnRad)         // 11
     .arg(btnPad)         // 12
     .arg(surface)        // 13
     .arg(accentHover)    // 14
     .arg(tabStyle);      // 15
}

QString ThemeManager::getProblemsDialogStyleSheet() const
{
    return getDialogStyleSheet();
}
