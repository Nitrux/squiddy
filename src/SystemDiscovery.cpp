// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "SystemDiscovery.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimeZone>
#include <QVariantMap>
#include <QXmlStreamReader>

#include <limits>
#include <sys/stat.h>

namespace {

QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

QStringList supportedLocales()
{
    QStringList locales;
    QFile file(QStringLiteral("/usr/share/i18n/SUPPORTED"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return locales;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const QString locale = fields.isEmpty() ? QString() : fields.first();
        if (!locale.isEmpty() && !locales.contains(locale))
            locales.append(locale);
    }
    locales.sort();
    return locales;
}

QStringList supportedLanguages(const QStringList& locales)
{
    QSet<QString> languages;
    for (const QString& localeName : locales) {
        const QString language = QLocale::languageToString(QLocale(localeName).language());
        if (!language.isEmpty() && language != QStringLiteral("C"))
            languages.insert(language);
    }
    QStringList result = languages.values();
    result.sort();
    return result;
}

QString currentLocale(const QStringList& locales)
{
    QString requested = QString::fromLocal8Bit(qgetenv("LANG")).trimmed();
    if (requested.isEmpty() || requested == QStringLiteral("C")
        || requested.startsWith(QStringLiteral("C."))
        || requested == QStringLiteral("POSIX"))
        requested = QLocale::system().name();

    for (const QString& locale : locales) {
        if (locale.compare(requested, Qt::CaseInsensitive) == 0)
            return locale;
    }

    const QString requestedName = QLocale(requested).name();
    for (const QString& locale : locales) {
        if (QLocale(locale).name().compare(requestedName, Qt::CaseInsensitive) == 0)
            return locale;
    }
    if (locales.contains(QStringLiteral("en_US.UTF-8")))
        return QStringLiteral("en_US.UTF-8");
    return locales.isEmpty() ? QString() : locales.first();
}

QString currentTimeZone(const QStringList& timeZones)
{
    const QString localtimePath = QFileInfo(QStringLiteral("/etc/localtime")).canonicalFilePath();
    const QString zoneInfoPrefix = QStringLiteral("/usr/share/zoneinfo/");
    if (localtimePath.startsWith(zoneInfoPrefix)) {
        const QString zone = localtimePath.mid(zoneInfoPrefix.size());
        if (timeZones.contains(zone))
            return zone;
    }

    const QString systemZone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if (timeZones.contains(systemZone))
        return systemZone;
    if (timeZones.contains(QStringLiteral("UTC")))
        return QStringLiteral("UTC");
    return timeZones.isEmpty() ? QString() : timeZones.first();
}

QStringList supportedTimeZones()
{
    QStringList zones;
    for (const QByteArray& zoneId : QTimeZone::availableTimeZoneIds()) {
        const QString zone = QString::fromUtf8(zoneId);
        if (!zone.startsWith(QStringLiteral("posix/"))
            && !zone.startsWith(QStringLiteral("right/"))
            && !zones.contains(zone))
            zones.append(zone);
    }
    zones.sort();
    return zones;
}

struct KeyboardChoices {
    QMap<QString, QString> layouts;
    QMap<QString, QMap<QString, QString>> variants;
    QMap<QString, QString> models;
    QMap<QString, QString> options;
};

KeyboardChoices keyboardChoicesFromXml(const QString& path)
{
    KeyboardChoices choices;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return choices;

    enum class Item { None, Model, Layout, Variant, Option };
    Item item = Item::None;
    QString currentLayout;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto element = xml.name();
            if (element == QStringLiteral("model"))
                item = Item::Model;
            else if (element == QStringLiteral("layout"))
                item = Item::Layout;
            else if (element == QStringLiteral("variant"))
                item = Item::Variant;
            else if (element == QStringLiteral("option"))
                item = Item::Option;
            else if (element == QStringLiteral("configItem")) {
                QString code;
                QString description;
                while (xml.readNextStartElement()) {
                    if (xml.name() == QStringLiteral("name"))
                        code = xml.readElementText().trimmed();
                    else if (xml.name() == QStringLiteral("description") && description.isEmpty())
                        description = xml.readElementText().trimmed();
                    else
                        xml.skipCurrentElement();
                }
                if (code.isEmpty())
                    continue;
                if (description.isEmpty())
                    description = code;
                switch (item) {
                case Item::Model:
                    choices.models.insert(code, description);
                    break;
                case Item::Layout:
                    currentLayout = code;
                    choices.layouts.insert(code, description);
                    break;
                case Item::Variant:
                    if (!currentLayout.isEmpty())
                        choices.variants[currentLayout].insert(code, description);
                    break;
                case Item::Option:
                    choices.options.insert(code, description);
                    break;
                case Item::None:
                    break;
                }
            }
        } else if (xml.isEndElement()) {
            const auto element = xml.name();
            if (element == QStringLiteral("variant"))
                item = Item::Layout;
            else if (element == QStringLiteral("layout")) {
                currentLayout.clear();
                item = Item::None;
            } else if (element == QStringLiteral("model")
                       || element == QStringLiteral("option")) {
                item = Item::None;
            }
        }
    }

    return xml.hasError() ? KeyboardChoices{} : choices;
}

KeyboardChoices keyboardChoicesFromList(const QString& path)
{
    KeyboardChoices choices;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return choices;

    enum class Section { None, Model, Layout, Variant, Option };
    Section section = Section::None;
    QTextStream stream(&file);
    static const QRegularExpression choiceLine(QStringLiteral("^\\s*([^\\s]+)\\s+(.+)$"));
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('!'))) {
            const QString heading = trimmed.mid(1).trimmed();
            section = heading == QStringLiteral("model") ? Section::Model
                : heading == QStringLiteral("layout") ? Section::Layout
                : heading == QStringLiteral("variant") ? Section::Variant
                : heading == QStringLiteral("option") ? Section::Option
                : Section::None;
            continue;
        }
        const auto match = choiceLine.match(line);
        if (!match.hasMatch())
            continue;
        const QString code = match.captured(1);
        const QString description = match.captured(2).trimmed();
        switch (section) {
        case Section::Model:
            choices.models.insert(code, description);
            break;
        case Section::Layout:
            choices.layouts.insert(code, description);
            break;
        case Section::Variant: {
            const qsizetype separator = description.indexOf(QLatin1Char(':'));
            if (separator > 0) {
                const QString layout = description.left(separator).trimmed();
                const QString variant = description.mid(separator + 1).trimmed();
                if (!layout.isEmpty() && !variant.isEmpty())
                    choices.variants[layout].insert(code, variant);
            }
            break;
        }
        case Section::Option:
            choices.options.insert(code, description);
            break;
        case Section::None:
            break;
        }
    }
    return choices;
}

QVariantList keyboardOptions(const QMap<QString, QString>& choices)
{
    QVariantList options;
    for (auto it = choices.cbegin(); it != choices.cend(); ++it) {
        options.append(QVariantMap{
            {QStringLiteral("text"), it.value()},
            {QStringLiteral("value"), it.key()}
        });
    }
    return options;
}

QString currentKeyboardLayout()
{
    QFile file(QStringLiteral("/etc/default/keyboard"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QTextStream stream(&file);
    static const QRegularExpression layoutSetting(
        QStringLiteral("^\\s*XKBLAYOUT\\s*=\\s*['\\\"]?([^'\\\"#]+)"));
    while (!stream.atEnd()) {
        const auto match = layoutSetting.match(stream.readLine());
        if (match.hasMatch())
            return match.captured(1).section(QLatin1Char(','), 0, 0).trimmed();
    }
    return {};
}

QString detectedImagePath(QString* format)
{
    QStringList imagePaths;
    const QStringList directories = {
        QStringLiteral("/cdrom/casper"),
        QStringLiteral("/run/live/medium/casper"),
        QStringLiteral("/run/initramfs/live/casper")
    };
    for (const QString& directory : directories) {
        const QDir dir(directory);
        if (!dir.exists())
            continue;
        const QStringList images = dir.entryList(
            {QStringLiteral("*.squashfs"), QStringLiteral("*.dwarfs")},
            QDir::Files | QDir::Readable,
            QDir::Name);
        for (const QString& image : images) {
            const QString path = QFileInfo(dir.absoluteFilePath(image)).canonicalFilePath();
            if (!path.isEmpty() && !imagePaths.contains(path))
                imagePaths.append(path);
        }
    }

    const QString configuredSquashFs = QStringLiteral("/cdrom/casper/filesystem.squashfs");
    if (QFileInfo(configuredSquashFs).isFile()) {
        *format = QStringLiteral("SquashFS");
        return configuredSquashFs;
    }
    if (imagePaths.size() != 1)
        return {};

    const QString path = imagePaths.first();
    if (path.endsWith(QStringLiteral(".squashfs"), Qt::CaseInsensitive))
        *format = QStringLiteral("SquashFS");
    else if (path.endsWith(QStringLiteral(".dwarfs"), Qt::CaseInsensitive))
        *format = QStringLiteral("DwarFS");
    return path;
}

QString sysfsDevicePath(const QString& majorMinor)
{
    return QFileInfo(QStringLiteral("/sys/dev/block/%1").arg(majorMinor)).canonicalFilePath();
}

QString sysfsDevicePathFromNode(const QString& devicePath)
{
    const QString canonicalPath = QFileInfo(devicePath).canonicalFilePath();
    if (canonicalPath.isEmpty() || !canonicalPath.startsWith(QStringLiteral("/dev/")))
        return {};
    struct stat deviceStatus {};
    const QByteArray pathBytes = canonicalPath.toLocal8Bit();
    if (::stat(pathBytes.constData(), &deviceStatus) != 0 || !S_ISBLK(deviceStatus.st_mode))
        return {};
    const QString name = QFileInfo(canonicalPath).fileName();
    return QFileInfo(QStringLiteral("/sys/class/block/%1").arg(name)).canonicalFilePath();
}

void appendUnderlyingDevices(const QString& deviceSysfsPath, QSet<QString>* devices)
{
    if (deviceSysfsPath.isEmpty() || devices->contains(deviceSysfsPath))
        return;
    devices->insert(deviceSysfsPath);

    const QDir slaves(QStringLiteral("%1/slaves").arg(deviceSysfsPath));
    const QFileInfoList entries = slaves.entryInfoList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot,
                                                       QDir::Name);
    for (const QFileInfo& entry : entries)
        appendUnderlyingDevices(entry.canonicalFilePath(), devices);
}

bool pathBelongsToDisk(const QString& sysfsPath, const QString& diskSysfsPath)
{
    return !sysfsPath.isEmpty()
        && (sysfsPath == diskSysfsPath || sysfsPath.startsWith(diskSysfsPath + QLatin1Char('/')));
}

bool hasMountedDescendant(const QString& diskSysfsPath)
{
    QFile mountInfo(QStringLiteral("/proc/self/mountinfo"));
    if (!mountInfo.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;

    QTextStream stream(&mountInfo);
    while (!stream.atEnd()) {
        const QStringList fields = stream.readLine().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 5)
            continue;
        QSet<QString> mountedDevices;
        appendUnderlyingDevices(sysfsDevicePath(fields.at(2)), &mountedDevices);
        for (const QString& mountedSysfsPath : mountedDevices) {
            if (pathBelongsToDisk(mountedSysfsPath, diskSysfsPath))
                return true;
        }
    }
    return false;
}

bool hasActiveSwapDescendant(const QString& diskSysfsPath)
{
    QFile swaps(QStringLiteral("/proc/swaps"));
    if (!swaps.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;

    QTextStream stream(&swaps);
    if (!stream.atEnd())
        stream.readLine();
    while (!stream.atEnd()) {
        const QStringList fields = stream.readLine().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.isEmpty())
            continue;
        QSet<QString> swapDevices;
        appendUnderlyingDevices(sysfsDevicePathFromNode(fields.first()), &swapDevices);
        for (const QString& swapSysfsPath : swapDevices) {
            if (pathBelongsToDisk(swapSysfsPath, diskSysfsPath))
                return true;
        }
    }
    return false;
}

bool isWholeBlockDevice(const QString& devicePath, QString* sysfsPath)
{
    const QFileInfo deviceInfo(devicePath);
    const QString canonicalPath = deviceInfo.canonicalFilePath();
    if (canonicalPath.isEmpty() || !canonicalPath.startsWith(QStringLiteral("/dev/")))
        return false;

    struct stat deviceStatus {};
    const QByteArray pathBytes = canonicalPath.toLocal8Bit();
    if (::stat(pathBytes.constData(), &deviceStatus) != 0 || !S_ISBLK(deviceStatus.st_mode))
        return false;

    const QString name = QFileInfo(canonicalPath).fileName();
    const QString blockPath = QStringLiteral("/sys/class/block/%1").arg(name);
    const QString canonicalBlockPath = QFileInfo(blockPath).canonicalFilePath();
    if (canonicalBlockPath.isEmpty() || QFileInfo::exists(blockPath + QStringLiteral("/partition")))
        return false;
    *sysfsPath = canonicalBlockPath;
    return true;
}

bool isSuitableTargetDiskPath(const QString& devicePath, const QString& imagePath)
{
    QString diskSysfsPath;
    if (!isWholeBlockDevice(devicePath, &diskSysfsPath))
        return false;

    const QString name = QFileInfo(diskSysfsPath).fileName();
    if (name.startsWith(QStringLiteral("loop"))
        || name.startsWith(QStringLiteral("ram"))
        || name.startsWith(QStringLiteral("zram"))
        || name.startsWith(QStringLiteral("dm-"))
        || name.startsWith(QStringLiteral("md")))
        return false;

    const QString blockPath = QStringLiteral("/sys/class/block/%1").arg(name);
    bool sectorsOk = false;
    const qulonglong sectors = readTextFile(blockPath + QStringLiteral("/size")).toULongLong(&sectorsOk);
    if (!sectorsOk || sectors < 68 || sectors > std::numeric_limits<quint64>::max() / 512)
        return false;
    const quint64 capacityBytes = sectors * 512;
    quint64 requiredBytes = 100ULL * 1024 * 1024 + 68ULL * 512;
    const QFileInfo imageInfo(imagePath);
    if (imageInfo.isFile() && imageInfo.size() >= 0)
        requiredBytes += static_cast<quint64>(imageInfo.size());
    if (capacityBytes < requiredBytes)
        return false;
    if (readTextFile(blockPath + QStringLiteral("/ro")) != QStringLiteral("0"))
        return false;
    if (hasMountedDescendant(diskSysfsPath) || hasActiveSwapDescendant(diskSysfsPath))
        return false;

    return true;
}

} // namespace

SystemDiscoveryResults SystemDiscovery::discover()
{
    SystemDiscoveryResults result;
    result.uefiEnvironmentDetected = QFileInfo(QStringLiteral("/sys/firmware/efi")).isDir();
    result.imagePath = detectedImagePath(&result.imageFormat);

    result.locales = supportedLocales();
    result.languages = supportedLanguages(result.locales);
    result.defaultLocale = currentLocale(result.locales);
    const QString defaultLanguage = QLocale::languageToString(
        QLocale(result.defaultLocale).language());
    if (result.languages.contains(defaultLanguage))
        result.defaultLanguage = defaultLanguage;
    else if (!result.languages.isEmpty())
        result.defaultLanguage = result.languages.first();

    result.timeZones = supportedTimeZones();
    result.defaultTimeZone = currentTimeZone(result.timeZones);

    KeyboardChoices keyboard = keyboardChoicesFromXml(
        QStringLiteral("/usr/share/X11/xkb/rules/evdev.xml"));
    if (keyboard.layouts.isEmpty() || keyboard.models.isEmpty()
        || keyboard.options.isEmpty() || keyboard.variants.isEmpty()) {
        KeyboardChoices fallback = keyboardChoicesFromList(
            QStringLiteral("/usr/share/X11/xkb/rules/evdev.lst"));
        if (fallback.layouts.isEmpty())
            fallback = keyboardChoicesFromList(
                QStringLiteral("/usr/share/X11/xkb/rules/base.lst"));
        if (keyboard.layouts.isEmpty()) keyboard.layouts = fallback.layouts;
        if (keyboard.models.isEmpty()) keyboard.models = fallback.models;
        if (keyboard.options.isEmpty()) keyboard.options = fallback.options;
        if (keyboard.variants.isEmpty()) keyboard.variants = fallback.variants;
    }
    result.keyboardLayouts = keyboard.layouts.keys();
    result.keyboardLayoutOptions = keyboardOptions(keyboard.layouts);
    result.keyboardModelOptions = keyboardOptions(keyboard.models);
    result.keyboardOptionOptions = keyboardOptions(keyboard.options);
    for (auto it = keyboard.variants.cbegin(); it != keyboard.variants.cend(); ++it)
        result.keyboardVariantOptionsByLayout.insert(it.key(), QVariant::fromValue(keyboardOptions(it.value())));
    const QString defaultLayout = currentKeyboardLayout();
    if (result.keyboardLayouts.contains(defaultLayout))
        result.defaultKeyboardLayout = defaultLayout;
    else if (result.keyboardLayouts.contains(QStringLiteral("us")))
        result.defaultKeyboardLayout = QStringLiteral("us");
    else if (!result.keyboardLayouts.isEmpty())
        result.defaultKeyboardLayout = result.keyboardLayouts.first();

    const QDir blockDevices(QStringLiteral("/sys/class/block"));
    const QFileInfoList entries = blockDevices.entryInfoList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot,
                                                              QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString devicePath = QStringLiteral("/dev/%1").arg(entry.fileName());
        if (isSuitableTargetDiskPath(devicePath, result.imagePath))
            result.targetDisks.append(QFileInfo(devicePath).canonicalFilePath());
    }
    result.targetDisks.removeDuplicates();
    result.targetDisks.sort();
    return result;
}

bool SystemDiscovery::isSuitableTargetDisk(const QString& devicePath, const QString& imagePath)
{
    return isSuitableTargetDiskPath(devicePath, imagePath);
}
