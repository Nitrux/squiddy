// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "InstallationPlan.h"

#include "SystemDiscovery.h"

#include <pwquality.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QStringView>
#include <QTimeZone>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace {

bool containsValue(const QStringList& choices, const QString& value)
{
    return !value.isEmpty() && choices.contains(value);
}

QString timeZoneRegionForId(const QString& id)
{
    if (id == QStringLiteral("UTC"))
        return QStringLiteral("UTC");
    const QString code = QLocale::territoryToCode(QTimeZone(id.toUtf8()).territory());
    return code.size() == 2 ? code : QStringLiteral("OTHER");
}

QString passwordQualityError(const QString& password, const QString& username)
{
    using Settings = std::unique_ptr<pwquality_settings_t, decltype(&pwquality_free_settings)>;
    Settings settings(pwquality_default_settings(), &pwquality_free_settings);
    if (!settings) {
        return QObject::tr("Password quality validation is unavailable.");
    }

    constexpr std::array options = {
        "minlen=12",
        "minclass=4",
        "maxrepeat=3",
        "maxsequence=3",
        "usersubstr=4",
        "badwords=linux nitrux calamares password 123456"
    };
    for (const auto* option : options) {
        if (pwquality_set_option(settings.get(), option) != 0) {
            return QObject::tr("Password quality policy could not be loaded.");
        }
    }

    const QByteArray passwordUtf8 = password.toUtf8();
    const QByteArray usernameUtf8 = username.toUtf8();
    if (pwquality_check(settings.get(), passwordUtf8.constData(), nullptr,
                        usernameUtf8.constData(), nullptr) < 0) {
        return QObject::tr("Password does not meet the configured quality requirements.");
    }

    return {};
}

} // namespace

InstallationPlanController::InstallationPlanController(bool debugMode, QObject* parent)
    : QObject(parent)
    , m_debugMode(debugMode)
{
    if (m_debugMode) {
        m_availableLanguages = {tr("English (United States)"), tr("Spanish (Mexico)")};
        m_availableLocales = {QStringLiteral("en_US.UTF-8"), QStringLiteral("es_MX.UTF-8")};
        m_availableTimeZones = {QStringLiteral("America/Mexico_City"),
                                QStringLiteral("America/New_York"), QStringLiteral("UTC")};
        m_availableKeyboardLayouts = {tr("English (US)"), tr("Spanish (Latin American)")};
        m_keyboardLayoutOptions = {
            QVariantMap{{QStringLiteral("text"), tr("English (US)")}, {QStringLiteral("value"), m_availableKeyboardLayouts.at(0)}},
            QVariantMap{{QStringLiteral("text"), tr("Spanish (Latin American)")}, {QStringLiteral("value"), m_availableKeyboardLayouts.at(1)}}
        };
        m_keyboardVariantOptionsByLayout.insert(m_availableKeyboardLayouts.at(0), QVariantList{
            QVariantMap{{QStringLiteral("text"), tr("English (US, international)")}, {QStringLiteral("value"), QStringLiteral("intl")}}
        });
        m_keyboardModelOptions = {
            QVariantMap{{QStringLiteral("text"), tr("Generic 105-key PC")}, {QStringLiteral("value"), QStringLiteral("pc105")}}
        };
        m_keyboardOptionOptions = {
            QVariantMap{{QStringLiteral("text"), tr("Right Alt as Compose")}, {QStringLiteral("value"), QStringLiteral("compose:ralt")}}
        };
        m_availableTargetDisks = {tr("Debug virtual disk (64 GiB)"), tr("Debug virtual disk (128 GiB)")};
        m_imagePath = QStringLiteral("debug://nitrux-base.squashfs");
        m_imageFormat = QStringLiteral("SquashFS");
        m_imageVerified = true;
        m_uefiEnvironmentDetected = true;

        m_language = m_availableLanguages.first();
        m_locale = m_availableLocales.first();
        m_timeZone = QStringLiteral("America/New_York");
        m_keyboardLayout = m_availableKeyboardLayouts.first();
        m_fullName = tr("Debug User");
        m_username = QStringLiteral("debuguser");
        m_hostname = QStringLiteral("debuguser-nitrux");
        m_targetDisk = m_availableTargetDisks.first();
        m_storagePreflightComplete = true;
        m_storageLayout = {
            QVariantMap{{QStringLiteral("label"), QStringLiteral("ESP")}, {QStringLiteral("mountPoint"), QStringLiteral("/boot/efi")}, {QStringLiteral("filesystem"), QStringLiteral("vfat (100 MiB)")}},
            QVariantMap{{QStringLiteral("label"), QStringLiteral("NX_ROOT")}, {QStringLiteral("mountPoint"), QStringLiteral("/")}, {QStringLiteral("filesystem"), QStringLiteral("xfs (23%)")}},
            QVariantMap{{QStringLiteral("label"), QStringLiteral("NX_HOME")}, {QStringLiteral("mountPoint"), QStringLiteral("/home")}, {QStringLiteral("filesystem"), QStringLiteral("LUKS2 + F2FS (67%)")}},
            QVariantMap{{QStringLiteral("label"), QStringLiteral("NX_VAR_LIB")}, {QStringLiteral("mountPoint"), QStringLiteral("/var/lib")}, {QStringLiteral("filesystem"), QStringLiteral("LUKS2 + F2FS (10%)")}}
        };
    }

    updateValidation();
}

bool InstallationPlanController::debugMode() const { return m_debugMode; }
QStringList InstallationPlanController::availableLanguages() const { return m_availableLanguages; }
QStringList InstallationPlanController::availableLocales() const { return m_availableLocales; }
QStringList InstallationPlanController::availableTimeZones() const { return m_availableTimeZones; }
QString InstallationPlanController::timeZoneRegion() const { return timeZoneRegionForId(m_timeZone); }
QVariantList InstallationPlanController::timeZoneRegionOptions() const
{
    QMap<QString, QString> labels;
    for (const QString& id : m_availableTimeZones) {
        const QString region = timeZoneRegionForId(id);
        if (labels.contains(region))
            continue;
        const QString text = region == QStringLiteral("UTC") ? tr("UTC")
            : region == QStringLiteral("OTHER") ? tr("Other time zones")
            : QLocale::territoryToString(QLocale::codeToTerritory(QStringView(region)));
        labels.insert(region, text);
    }

    QVariantList options;
    for (auto it = labels.cbegin(); it != labels.cend(); ++it)
        options.append(QVariantMap{{QStringLiteral("text"), it.value()},
                                   {QStringLiteral("value"), it.key()}});
    std::sort(options.begin(), options.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap left = a.toMap();
        const QVariantMap right = b.toMap();
        const QString leftRegion = left.value(QStringLiteral("value")).toString();
        const QString rightRegion = right.value(QStringLiteral("value")).toString();
        if (leftRegion == rightRegion)
            return false;
        if (leftRegion == QStringLiteral("UTC") || rightRegion == QStringLiteral("UTC"))
            return leftRegion == QStringLiteral("UTC");
        if (leftRegion == QStringLiteral("OTHER") || rightRegion == QStringLiteral("OTHER"))
            return rightRegion == QStringLiteral("OTHER");
        return QString::localeAwareCompare(left.value(QStringLiteral("text")).toString(),
                                           right.value(QStringLiteral("text")).toString()) < 0;
    });
    return options;
}

QVariantList InstallationPlanController::timeZoneOptionsForRegion(const QString& region) const
{
    QVariantList options;
    for (const QString& id : m_availableTimeZones) {
        if (timeZoneRegionForId(id) != region)
            continue;
        QString text = id == QStringLiteral("UTC") ? tr("UTC") : id.section(QLatin1Char('/'), 1);
        if (text.isEmpty())
            text = id;
        text.replace(QLatin1Char('_'), QLatin1Char(' '));
        text.replace(QLatin1Char('/'), QStringLiteral(" / "));
        options.append(QVariantMap{{QStringLiteral("text"), text},
                                   {QStringLiteral("value"), id}});
    }

    QString preferred = timeZoneRegionForId(m_timeZone) == region ? m_timeZone : QString();
    if (preferred.isEmpty() && region.size() == 2) {
        QFile zoneTable(QStringLiteral("/usr/share/zoneinfo/zone.tab"));
        if (zoneTable.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!zoneTable.atEnd()) {
                const QList<QByteArray> fields = zoneTable.readLine().trimmed().split('\t');
                if (fields.size() >= 3 && fields.at(0) == region.toUtf8()) {
                    const QString id = QString::fromUtf8(fields.at(2));
                    if (m_availableTimeZones.contains(id)) {
                        preferred = id;
                        break;
                    }
                }
            }
        }
    }
    std::sort(options.begin(), options.end(), [&preferred](const QVariant& a, const QVariant& b) {
        const QVariantMap left = a.toMap();
        const QVariantMap right = b.toMap();
        const QString leftId = left.value(QStringLiteral("value")).toString();
        const QString rightId = right.value(QStringLiteral("value")).toString();
        if (leftId == rightId)
            return false;
        if (leftId == preferred || rightId == preferred)
            return leftId == preferred;
        const int order = QString::localeAwareCompare(left.value(QStringLiteral("text")).toString(),
                                                      right.value(QStringLiteral("text")).toString());
        return order == 0 ? leftId < rightId : order < 0;
    });
    return options;
}

QStringList InstallationPlanController::availableKeyboardLayouts() const { return m_availableKeyboardLayouts; }
QVariantList InstallationPlanController::keyboardLayoutOptions() const { return m_keyboardLayoutOptions; }
QVariantList InstallationPlanController::keyboardVariantOptions() const
{
    return m_keyboardVariantOptionsByLayout.value(m_keyboardLayout).toList();
}
QVariantList InstallationPlanController::keyboardModelOptions() const { return m_keyboardModelOptions; }
QVariantList InstallationPlanController::keyboardOptionOptions() const { return m_keyboardOptionOptions; }
QStringList InstallationPlanController::availableTargetDisks() const { return m_availableTargetDisks; }
QString InstallationPlanController::diskDisplayName(const QString& devicePath) const
{
    const QString diskName = QFileInfo(devicePath).fileName();
    const QString basePath = QStringLiteral("/sys/class/block/") + diskName + QStringLiteral("/device/");
    const auto readName = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly | QIODevice::Text)
            ? QString::fromUtf8(file.readAll()).simplified() : QString();
    };
    QString model = readName(basePath + QStringLiteral("model"));
    QString vendor = readName(basePath + QStringLiteral("vendor"));
    if (model.isEmpty()) {
        const QString numbers = readName(QStringLiteral("/sys/class/block/") + diskName
            + QStringLiteral("/dev"));
        QFile udev(QStringLiteral("/run/udev/data/b") + numbers);
        if (!numbers.isEmpty() && udev.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines = QString::fromUtf8(udev.readAll()).split(QLatin1Char('\n'));
            for (const QString& line : lines) {
                if (line.startsWith(QStringLiteral("E:ID_MODEL_FROM_DATABASE=")))
                    model = line.mid(25).simplified();
                else if (line.startsWith(QStringLiteral("E:ID_MODEL=")) && model.isEmpty())
                    model = line.mid(11).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
                else if (line.startsWith(QStringLiteral("E:ID_VENDOR_FROM_DATABASE=")) && vendor.isEmpty())
                    vendor = line.mid(26).simplified();
            }
        }
    }
    if (!model.isEmpty())
        return vendor.isEmpty() || model.startsWith(vendor, Qt::CaseInsensitive)
            ? model : QStringLiteral("%1 %2").arg(vendor, model);
    return vendor.isEmpty() ? diskName : QStringLiteral("%1 %2").arg(vendor, diskName);
}


void InstallationPlanController::setAvailableLanguages(const QStringList& choices)
{
    if (m_debugMode || m_availableLanguages == choices) return;
    m_availableLanguages = choices;
    Q_EMIT choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableLocales(const QStringList& choices)
{
    if (m_debugMode || m_availableLocales == choices) return;
    m_availableLocales = choices;
    Q_EMIT choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableTimeZones(const QStringList& choices)
{
    if (m_debugMode || m_availableTimeZones == choices) return;
    m_availableTimeZones = choices;
    Q_EMIT choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableKeyboardLayouts(const QStringList& choices)
{
    if (m_debugMode || m_availableKeyboardLayouts == choices) return;
    m_availableKeyboardLayouts = choices;
    Q_EMIT choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setKeyboardLayoutOptions(const QVariantList& options)
{
    if (m_debugMode || m_keyboardLayoutOptions == options) return;
    m_keyboardLayoutOptions = options;
    Q_EMIT choicesChanged();
}

void InstallationPlanController::setKeyboardAdvancedOptions(const QVariantMap& variantsByLayout,
                                                           const QVariantList& models,
                                                           const QVariantList& options)
{
    if (m_debugMode || (m_keyboardVariantOptionsByLayout == variantsByLayout
        && m_keyboardModelOptions == models && m_keyboardOptionOptions == options)) return;
    m_keyboardVariantOptionsByLayout = variantsByLayout;
    m_keyboardModelOptions = models;
    m_keyboardOptionOptions = options;
    Q_EMIT choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableTargetDisks(const QStringList& choices)
{
    if (m_debugMode || m_availableTargetDisks == choices) return;
    m_availableTargetDisks = choices;
    invalidateStoragePreflight();
    Q_EMIT choicesChanged();
    invalidatePlan();
}

QString InstallationPlanController::imagePath() const { return m_imagePath; }
QString InstallationPlanController::imageFormat() const { return m_imageFormat; }
void InstallationPlanController::setDetectedImage(const QString& path, const QString& format)
{
    if (m_debugMode) return;
    const QFileInfo imageInfo(path);
    const bool imageVerified = imageInfo.isFile() && imageInfo.isReadable()
        && (format == QStringLiteral("DwarFS") || format == QStringLiteral("SquashFS"));
    if (m_imagePath == path && m_imageFormat == format && m_imageVerified == imageVerified) return;
    m_imagePath = path;
    m_imageFormat = format;
    m_imageVerified = imageVerified;
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::uefiEnvironmentDetected() const
{
    return m_uefiEnvironmentDetected;
}

void InstallationPlanController::setUefiEnvironmentDetected(bool detected)
{
    if (m_debugMode || m_uefiEnvironmentDetected == detected) return;
    m_uefiEnvironmentDetected = detected;
    invalidatePlan();
}

QString InstallationPlanController::language() const { return m_language; }
void InstallationPlanController::setLanguage(const QString& value)
{
    if (m_language == value) return;
    m_language = value;
    invalidatePlan();
}

void InstallationPlanController::selectLanguage(const QString& value, const QString& preferredLocale)
{
    markRegionalSettingsChanged();
    applyLanguageDefaults(value, preferredLocale);
}

void InstallationPlanController::markRegionalSettingsChanged()
{
    m_regionalSettingsChangedByUser = true;
}

bool InstallationPlanController::regionalSettingsChangedByUser() const
{
    return m_regionalSettingsChangedByUser;
}

void InstallationPlanController::applyLanguageDefaults(const QString& value, const QString& preferredLocale)
{
    if (!m_availableLanguages.contains(value))
        return;

    QLocale::Language selectedLanguage = QLocale::AnyLanguage;
    auto matchesLanguage = [&](const QString& localeName) {
        const QLocale::Language language = QLocale(localeName).language();
        const QString name = QLocale::languageToString(language);
        if (value == name || value.startsWith(name + QStringLiteral(" ("))) {
            selectedLanguage = language;
            return true;
        }
        return false;
    };

    QString selectedLocale;
    if (m_availableLocales.contains(preferredLocale) && matchesLanguage(preferredLocale))
        selectedLocale = preferredLocale;
    if (selectedLocale.isEmpty() && m_availableLocales.contains(m_locale)
        && m_locale.contains(QStringLiteral(".UTF-8"), Qt::CaseInsensitive)
        && matchesLanguage(m_locale))
        selectedLocale = m_locale;
    if (selectedLocale.isEmpty()) {
        for (const QString& candidate : m_availableLocales) {
            if (candidate.contains(QStringLiteral(".UTF-8"), Qt::CaseInsensitive)
                && matchesLanguage(candidate)) {
                selectedLanguage = QLocale(candidate).language();
                break;
            }
        }
        if (selectedLanguage != QLocale::AnyLanguage) {
            const QLocale::Territory likelyTerritory = QLocale(selectedLanguage).territory();
            for (const QString& candidate : m_availableLocales) {
                const QLocale locale(candidate);
                if (candidate.contains(QStringLiteral(".UTF-8"), Qt::CaseInsensitive)
                    && locale.language() == selectedLanguage
                    && locale.territory() == likelyTerritory) {
                    selectedLocale = candidate;
                    break;
                }
            }
            if (selectedLocale.isEmpty()) {
                for (const QString& candidate : m_availableLocales) {
                    if (candidate.contains(QStringLiteral(".UTF-8"), Qt::CaseInsensitive)
                        && QLocale(candidate).language() == selectedLanguage) {
                        selectedLocale = candidate;
                        break;
                    }
                }
            }
        }
    }

    setLanguage(value);
    if (selectedLocale.isEmpty())
        return;
    setLocale(selectedLocale);

    const QLocale locale(selectedLocale);
    const QLocale::Territory territory = locale.territory();
    if (territory == QLocale::AnyTerritory)
        return;
    const QString countryCode = QLocale::territoryToCode(territory);

    QStringList layoutCandidates;
    if (locale.language() == QLocale::Spanish && territory != QLocale::Spain)
        layoutCandidates.append(QStringLiteral("latam"));
    layoutCandidates.append(countryCode.toLower());
    layoutCandidates.append(QLocale::languageToCode(locale.language(), QLocale::ISO639Part1));
    layoutCandidates.append(QLocale::languageToCode(locale.language(), QLocale::ISO639Alpha3));
    for (const QString& candidate : layoutCandidates) {
        if (m_availableKeyboardLayouts.contains(candidate)) {
            setKeyboardLayout(candidate);
            break;
        }
    }
    if (m_debugMode && !m_availableKeyboardLayouts.isEmpty()) {
        if (locale.language() == QLocale::English)
            setKeyboardLayout(m_availableKeyboardLayouts.first());
        else if (locale.language() == QLocale::Spanish && m_availableKeyboardLayouts.size() > 1)
            setKeyboardLayout(m_availableKeyboardLayouts.at(1));
    }
    if (m_debugMode) {
        const QString zone = territory == QLocale::Mexico
            ? QStringLiteral("America/Mexico_City") : QStringLiteral("America/New_York");
        if (m_availableTimeZones.contains(zone))
            setTimeZone(zone);
        return;
    }

    if (QTimeZone(m_timeZone.toUtf8()).territory() == territory)
        return;
    QFile zoneTable(QStringLiteral("/usr/share/zoneinfo/zone.tab"));
    if (zoneTable.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!zoneTable.atEnd()) {
            const QList<QByteArray> fields = zoneTable.readLine().trimmed().split('\t');
            if (fields.size() >= 3 && fields.at(0) == countryCode.toUtf8()) {
                const QString zone = QString::fromUtf8(fields.at(2));
                if (m_availableTimeZones.contains(zone)) {
                    setTimeZone(zone);
                    return;
                }
            }
        }
    }
    for (const QByteArray& zoneId : QTimeZone::availableTimeZoneIds(territory)) {
        const QString zone = QString::fromUtf8(zoneId);
        if (m_availableTimeZones.contains(zone)) {
            setTimeZone(zone);
            return;
        }
    }
}

QString InstallationPlanController::locale() const { return m_locale; }
void InstallationPlanController::setLocale(const QString& value)
{
    if (m_locale == value) return;
    m_locale = value;
    invalidatePlan();
}

QString InstallationPlanController::timeZone() const { return m_timeZone; }
void InstallationPlanController::setTimeZone(const QString& value)
{
    if (m_timeZone == value) return;
    m_timeZone = value;
    invalidatePlan();
}

QString InstallationPlanController::keyboardLayout() const { return m_keyboardLayout; }
void InstallationPlanController::setKeyboardLayout(const QString& value)
{
    if (m_keyboardLayout == value) return;
    m_keyboardLayout = value;
    m_keyboardVariant.clear();
    invalidatePlan();
}

QString InstallationPlanController::keyboardVariant() const { return m_keyboardVariant; }
void InstallationPlanController::setKeyboardVariant(const QString& value)
{
    if (m_keyboardVariant == value) return;
    m_keyboardVariant = value;
    invalidatePlan();
}

QString InstallationPlanController::keyboardModel() const { return m_keyboardModel; }
void InstallationPlanController::setKeyboardModel(const QString& value)
{
    if (m_keyboardModel == value) return;
    m_keyboardModel = value;
    invalidatePlan();
}

QString InstallationPlanController::keyboardOptions() const { return m_keyboardOptions; }
void InstallationPlanController::setKeyboardOptions(const QString& value)
{
    if (m_keyboardOptions == value) return;
    m_keyboardOptions = value;
    invalidatePlan();
}

QString InstallationPlanController::fullName() const { return m_fullName; }
void InstallationPlanController::setFullName(const QString& value)
{
    if (m_fullName == value) return;
    m_fullName = value;
    invalidatePlan();
}

QString InstallationPlanController::username() const { return m_username; }
void InstallationPlanController::setUsername(const QString& value)
{
    if (m_username == value) return;
    const QString previousDefaultHostname = m_username.isEmpty()
        ? QString()
        : m_username + QStringLiteral("-nitrux");
    const bool followsUsername = m_hostname.isEmpty() || m_hostname == previousDefaultHostname;
    m_username = value;
    if (followsUsername)
        m_hostname = value.isEmpty() ? QString() : value + QStringLiteral("-nitrux");
    invalidatePlan();
}

QString InstallationPlanController::hostname() const { return m_hostname; }
void InstallationPlanController::setHostname(const QString& value)
{
    if (m_hostname == value) return;
    m_hostname = value;
    invalidatePlan();
}

QString InstallationPlanController::targetDisk() const { return m_targetDisk; }
void InstallationPlanController::setTargetDisk(const QString& value)
{
    if (m_targetDisk == value) return;
    m_targetDisk = value;
    m_acknowledgeDiskErasure = false;
    m_targetDiskInfo = SystemDiscovery::inspectTargetDisk(value, m_imagePath);
    m_manualAssignments.clear();
    m_manualPartitionLayout.clear();
    m_sharedSpaceMiB = 0;
    m_sharedSpaceStartSector = 0;
    m_sharedResizeDevice.clear();
    if (m_partitioningMode != QStringLiteral("automated")
        && !m_targetDiskInfo.value(QStringLiteral("gpt")).toBool())
        m_partitioningMode = QStringLiteral("automated");
    if (m_partitioningMode == QStringLiteral("automated") && m_swapChoice == QStringLiteral("None"))
        m_swapChoice = QStringLiteral("Small");
    invalidateStoragePreflight();
    invalidatePlan();
}

QVariantMap InstallationPlanController::targetDiskInfo() const { return m_targetDiskInfo; }
QVariantMap InstallationPlanController::manualAssignments() const { return m_manualAssignments; }
QVariantList InstallationPlanController::manualPartitionLayout() const { return m_manualPartitionLayout; }
void InstallationPlanController::setManualPartitionLayout(const QVariantList& layout)
{
    if (m_manualPartitionLayout == layout) return;
    m_manualPartitionLayout = layout;
    m_manualAssignments.clear();
    m_encryptRoot = false;
    m_encryptHome = false;
    m_encryptVarLib = false;
    m_encryptSwap = false;
    for (const QVariant& value : layout) {
        const QVariantMap row = value.toMap();
        const QString role = row.value(QStringLiteral("mountPoint")).toString();
        const QString device = row.value(QStringLiteral("device")).toString();
        if (role.isEmpty() || device.isEmpty() || row.value(QStringLiteral("action")).toString() == QStringLiteral("delete"))
            continue;
        m_manualAssignments.insert(device, role);
        if (role == QStringLiteral("/")) m_encryptRoot = row.value(QStringLiteral("encrypted")).toBool();
        else if (role == QStringLiteral("/home")) m_encryptHome = row.value(QStringLiteral("encrypted")).toBool();
        else if (role == QStringLiteral("/var/lib")) m_encryptVarLib = row.value(QStringLiteral("encrypted")).toBool();
        else if (role == QStringLiteral("swap")) m_encryptSwap = row.value(QStringLiteral("encrypted")).toBool();
        m_partitionLabels.insert(role, row.value(QStringLiteral("filesystemLabel")).toString());
    }
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
QVariantMap InstallationPlanController::partitionLabels() const { return m_partitionLabels; }
QVariantMap InstallationPlanController::partitionSizesMiB() const { return m_partitionSizesMiB; }
qulonglong InstallationPlanController::sharedSpaceMiB() const { return m_sharedSpaceMiB; }
void InstallationPlanController::setSharedSpaceMiB(qulonglong value)
{
    if (m_sharedSpaceMiB == value) return;
    m_sharedSpaceMiB = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
qulonglong InstallationPlanController::sharedSpaceStartSector() const { return m_sharedSpaceStartSector; }
void InstallationPlanController::setSharedSpaceStartSector(qulonglong value)
{
    if (m_sharedSpaceStartSector == value) return;
    m_sharedSpaceStartSector = value;
    m_sharedSpaceMiB = 0;
    m_sharedResizeDevice.clear();
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
QString InstallationPlanController::sharedResizeDevice() const { return m_sharedResizeDevice; }
void InstallationPlanController::setSharedResizeDevice(const QString& value)
{
    if (m_sharedResizeDevice == value) return;
    m_sharedResizeDevice = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
void InstallationPlanController::setManualAssignment(const QString& device, const QString& role)
{
    if (m_manualAssignments.value(device).toString() == role) return;
    if (role.isEmpty()) m_manualAssignments.remove(device);
    else m_manualAssignments.insert(device, role);
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
void InstallationPlanController::setPartitionLabel(const QString& role, const QString& label)
{
    if (m_partitionLabels.value(role).toString() == label) return;
    m_partitionLabels.insert(role, label);
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
void InstallationPlanController::setPartitionSizeMiB(const QString& role, qulonglong size)
{
    if (m_partitionSizesMiB.value(role).toULongLong() == size) return;
    m_partitionSizesMiB.insert(role, QVariant::fromValue<qulonglong>(size));
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

QString InstallationPlanController::partitioningMode() const { return m_partitioningMode; }
void InstallationPlanController::setPartitioningMode(const QString& value)
{
    if (m_partitioningMode == value) return;
    m_partitioningMode = value;
    if (value != QStringLiteral("share"))
        m_sharedResizeDevice.clear();
    if (value == QStringLiteral("manual"))
        m_swapChoice = QStringLiteral("None");
    else if (m_swapChoice == QStringLiteral("None"))
        m_swapChoice = QStringLiteral("Small");
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

QString InstallationPlanController::swapChoice() const { return m_swapChoice; }
void InstallationPlanController::setSwapChoice(const QString& value)
{
    if (m_swapChoice == value) return;
    m_swapChoice = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::automaticLogin() const { return m_automaticLogin; }
void InstallationPlanController::setAutomaticLogin(bool value)
{
    if (m_automaticLogin == value) return;
    m_automaticLogin = value;
    invalidatePlan();
}

bool InstallationPlanController::allowWeakPassword() const { return m_allowWeakPassword; }
void InstallationPlanController::setAllowWeakPassword(bool value)
{
    if (m_allowWeakPassword == value) return;
    m_allowWeakPassword = value;
    invalidatePlan();
}

bool InstallationPlanController::encryptHome() const { return m_encryptHome; }
void InstallationPlanController::setEncryptHome(bool value)
{
    if (m_encryptHome == value) return;
    m_encryptHome = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::encryptVarLib() const { return m_encryptVarLib; }
void InstallationPlanController::setEncryptVarLib(bool value)
{
    if (m_encryptVarLib == value) return;
    m_encryptVarLib = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}
bool InstallationPlanController::encryptRoot() const { return m_encryptRoot; }
void InstallationPlanController::setEncryptRoot(bool value)
{
    if (m_encryptRoot == value) return;
    m_encryptRoot = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::encryptSwap() const { return m_encryptSwap; }
void InstallationPlanController::setEncryptSwap(bool value)
{
    if (m_encryptSwap == value) return;
    m_encryptSwap = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::encryptInstallation() const { return m_encryptInstallation; }
void InstallationPlanController::setEncryptInstallation(bool value)
{
    if (m_encryptInstallation == value) return;
    m_encryptInstallation = value;
    m_acknowledgeDiskErasure = false;
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::acknowledgeDiskErasure() const { return m_acknowledgeDiskErasure; }
void InstallationPlanController::setAcknowledgeDiskErasure(bool value)
{
    if (m_acknowledgeDiskErasure == value) return;
    m_acknowledgeDiskErasure = value;
    invalidatePlan();
}

bool InstallationPlanController::planValid() const { return m_validationErrors.isEmpty(); }
QStringList InstallationPlanController::validationErrors() const { return m_validationErrors; }
bool InstallationPlanController::validatedPlanAvailable() const { return m_validatedPlan.has_value(); }
bool InstallationPlanController::storagePreflightBusy() const { return m_storagePreflightBusy; }
bool InstallationPlanController::storagePreflightComplete() const { return m_storagePreflightComplete; }
QStringList InstallationPlanController::storagePreflightErrors() const { return m_storagePreflightErrors; }
QVariantList InstallationPlanController::storageLayout() const { return m_storageLayout; }
qulonglong InstallationPlanController::targetDiskBytes() const { return m_targetDiskBytes; }
qulonglong InstallationPlanController::logicalBlockSize() const { return m_logicalBlockSize; }
const std::optional<InstallationPlan>& InstallationPlanController::validatedPlan() const
{
    return m_validatedPlan;
}

QVariantMap InstallationPlanController::validatedPlanMap() const
{
    if (!m_validatedPlan)
        return {};

    const InstallationPlan& plan = *m_validatedPlan;
    QVariantList partitions;
    for (const PlannedPartition& partition : plan.partitions) {
        partitions.append(QVariantMap{
            {QStringLiteral("mountPoint"), partition.mountPoint},
            {QStringLiteral("filesystem"), partition.filesystem},
            {QStringLiteral("sizePercent"), partition.sizePercent},
            {QStringLiteral("encrypted"), partition.encrypted},
            {QStringLiteral("compressed"), partition.compressed}
        });
    }

    QString imageFormat;
    switch (plan.imageFormat) {
    case ImageFormat::DwarFS:
        imageFormat = QStringLiteral("DwarFS");
        break;
    case ImageFormat::SquashFS:
        imageFormat = QStringLiteral("SquashFS");
        break;
    }

    QString partitioningMode;
    switch (plan.partitioningMode) {
    case PartitioningMode::Automated:
        partitioningMode = QStringLiteral("automated");
        break;
    case PartitioningMode::Shared:
        partitioningMode = QStringLiteral("share");
        break;
    case PartitioningMode::Manual:
        partitioningMode = QStringLiteral("manual");
        break;
    }

    QString swapChoice;
    switch (plan.swapChoice) {
    case SwapChoice::None:
        swapChoice = QStringLiteral("None");
        break;
    case SwapChoice::Small:
        swapChoice = QStringLiteral("Small");
        break;
    case SwapChoice::Suspend:
        swapChoice = QStringLiteral("Suspend");
        break;
    case SwapChoice::File:
        swapChoice = QStringLiteral("File");
        break;
    }

    return {
        {QStringLiteral("debugOnly"), plan.debugOnly},
        {QStringLiteral("uefiEnvironmentDetected"), plan.uefiEnvironmentDetected},
        {QStringLiteral("requiresUefi"), plan.requiresUefi},
        {QStringLiteral("requiresGpt"), plan.requiresGpt},
        {QStringLiteral("imagePath"), plan.imagePath},
        {QStringLiteral("imageFormat"), imageFormat},
        {QStringLiteral("language"), plan.language},
        {QStringLiteral("locale"), plan.locale},
        {QStringLiteral("timeZone"), plan.timeZone},
        {QStringLiteral("keyboardLayout"), plan.keyboardLayout},
        {QStringLiteral("keyboardVariant"), plan.keyboardVariant},
        {QStringLiteral("keyboardModel"), plan.keyboardModel},
        {QStringLiteral("keyboardOptions"), plan.keyboardOptions},
        {QStringLiteral("fullName"), plan.fullName},
        {QStringLiteral("username"), plan.username},
        {QStringLiteral("hostname"), plan.hostname},
        {QStringLiteral("userPassword"), plan.userPassword},
        {QStringLiteral("passwordConfirmation"), m_passwordConfirmation},
        {QStringLiteral("encryptionPassphrase"), plan.partitions.at(0).encrypted
                || plan.partitions.at(1).encrypted || plan.partitions.at(2).encrypted || plan.encryptSwap
            ? plan.encryptionPassphrase : QString()},
        {QStringLiteral("encryptionPassphraseConfirmation"), plan.partitions.at(0).encrypted
                || plan.partitions.at(1).encrypted || plan.partitions.at(2).encrypted || plan.encryptSwap
            ? m_encryptionPassphraseConfirmation : QString()},
        {QStringLiteral("acknowledgeDiskErasure"), plan.acknowledgeDiskErasure},
        {QStringLiteral("userShell"), plan.userShell},
        {QStringLiteral("userGroups"), plan.userGroups},
        {QStringLiteral("automaticLogin"), plan.automaticLogin},
        {QStringLiteral("allowWeakPassword"), plan.allowWeakPassword},
        {QStringLiteral("targetDevice"), plan.targetDevice},
        {QStringLiteral("manualAssignments"), plan.manualAssignments},
        {QStringLiteral("manualPartitionLayout"), plan.manualPartitionLayout},
        {QStringLiteral("partitionLabels"), plan.partitionLabels},
        {QStringLiteral("partitionSizesMiB"), plan.partitionSizesMiB},
        {QStringLiteral("sharedSpaceMiB"), QVariant::fromValue<qulonglong>(plan.sharedSpaceMiB)},
        {QStringLiteral("sharedSpaceStartSector"), QVariant::fromValue<qulonglong>(plan.sharedSpaceStartSector)},
        {QStringLiteral("sharedResizeDevice"), plan.sharedResizeDevice},
        {QStringLiteral("partitioningMode"), partitioningMode},
        {QStringLiteral("swapChoice"), swapChoice},
        {QStringLiteral("encryptSwap"), plan.encryptSwap},
        {QStringLiteral("partitions"), partitions},
        {QStringLiteral("efiMountPoint"), plan.efiMountPoint},
        {QStringLiteral("efiRecommendedSizeMiB"), plan.efiRecommendedSizeMiB},
        {QStringLiteral("storageLayout"), plan.storageLayout},
        {QStringLiteral("targetDiskBytes"), QVariant::fromValue<qulonglong>(plan.targetDiskBytes)},
        {QStringLiteral("logicalBlockSize"), QVariant::fromValue<qulonglong>(plan.logicalBlockSize)}
    };
}

void InstallationPlanController::setUserPassword(const QString& value)
{
    if (m_userPassword == value) return;
    m_userPassword = value;
    invalidatePlan();
}

void InstallationPlanController::setPasswordConfirmation(const QString& value)
{
    if (m_passwordConfirmation == value) return;
    m_passwordConfirmation = value;
    invalidatePlan();
}

void InstallationPlanController::setEncryptionPassphrase(const QString& value)
{
    if (m_encryptionPassphrase == value) return;
    m_encryptionPassphrase = value;
    invalidatePlan();
}

void InstallationPlanController::setEncryptionPassphraseConfirmation(const QString& value)
{
    if (m_encryptionPassphraseConfirmation == value) return;
    m_encryptionPassphraseConfirmation = value;
    invalidatePlan();
}

bool InstallationPlanController::validateAndBuildPlan()
{
    m_validatedPlan.reset();
    if (!m_debugMode) {
        const QFileInfo imageInfo(m_imagePath);
        m_imageVerified = imageInfo.isFile() && imageInfo.isReadable()
            && (m_imageFormat == QStringLiteral("DwarFS")
                || m_imageFormat == QStringLiteral("SquashFS"));
    }
    updateValidation();
    if (!planValid()) {
        Q_EMIT planChanged();
        return false;
    }

    InstallationPlan plan;
    plan.debugOnly = m_debugMode;
    plan.uefiEnvironmentDetected = m_uefiEnvironmentDetected;
    plan.imagePath = m_imagePath;
    plan.imageFormat = m_imageFormat == QStringLiteral("DwarFS")
        ? ImageFormat::DwarFS
        : ImageFormat::SquashFS;
    plan.language = m_language;
    plan.locale = m_locale;
    plan.timeZone = m_timeZone;
    plan.keyboardLayout = m_keyboardLayout;
    plan.keyboardVariant = m_keyboardVariant;
    plan.keyboardModel = m_keyboardModel;
    plan.keyboardOptions = m_keyboardOptions;
    plan.fullName = m_fullName.trimmed();
    plan.username = m_username;
    plan.hostname = m_hostname;
    plan.userPassword = m_userPassword;
    const bool manualMode = m_partitioningMode == QStringLiteral("manual");
    const bool encryptRoot = manualMode
        ? m_encryptRoot && m_manualAssignments.values().contains(QVariant(QStringLiteral("/"))) : m_encryptInstallation;
    const bool encryptHome = manualMode
        ? m_encryptHome && m_manualAssignments.values().contains(QVariant(QStringLiteral("/home"))) : m_encryptInstallation;
    const bool encryptVarLib = manualMode
        ? m_encryptVarLib && m_manualAssignments.values().contains(QVariant(QStringLiteral("/var/lib"))) : m_encryptInstallation;
    const bool encryptSwap = manualMode
        ? m_encryptSwap && m_manualAssignments.values().contains(QVariant(QStringLiteral("swap"))) : m_encryptInstallation;
    plan.encryptionPassphrase = (encryptRoot || encryptHome || encryptVarLib || encryptSwap)
        ? m_encryptionPassphrase : QString();
    plan.encryptSwap = encryptSwap;
    plan.acknowledgeDiskErasure = m_acknowledgeDiskErasure;
    plan.userGroups = {
        QStringLiteral("adm"), QStringLiteral("bluetooth"), QStringLiteral("cdrom"),
        QStringLiteral("dip"), QStringLiteral("fuse"), QStringLiteral("gamemode"),
        QStringLiteral("input"), QStringLiteral("lpadmin"), QStringLiteral("pipewire"),
        QStringLiteral("plugdev"), QStringLiteral("sambashare"), QStringLiteral("sudo"),
        QStringLiteral("video"), QStringLiteral("wheel")
    };
    plan.automaticLogin = m_automaticLogin;
    plan.allowWeakPassword = m_allowWeakPassword;
    plan.targetDevice = m_targetDisk;
    plan.partitioningMode = m_partitioningMode == QStringLiteral("automated")
        ? PartitioningMode::Automated
        : (m_partitioningMode == QStringLiteral("share")
            ? PartitioningMode::Shared : PartitioningMode::Manual);
    plan.manualAssignments = m_manualAssignments;
    plan.manualPartitionLayout = m_manualPartitionLayout;
    plan.partitionLabels = m_partitionLabels;
    plan.partitionSizesMiB = m_partitionSizesMiB;
    plan.sharedSpaceMiB = m_sharedSpaceMiB;
    plan.sharedSpaceStartSector = m_sharedSpaceStartSector;
    plan.sharedResizeDevice = m_sharedResizeDevice;
    if (m_swapChoice == QStringLiteral("None"))
        plan.swapChoice = SwapChoice::None;
    else if (m_swapChoice == QStringLiteral("Suspend"))
        plan.swapChoice = SwapChoice::Suspend;
    else if (m_swapChoice == QStringLiteral("File"))
        plan.swapChoice = SwapChoice::File;
    else
        plan.swapChoice = SwapChoice::Small;

    plan.storageLayout = m_storageLayout;
    plan.targetDiskBytes = m_targetDiskBytes;
    plan.logicalBlockSize = m_logicalBlockSize;
    plan.partitions = {
        {QStringLiteral("/"), QStringLiteral("xfs"), 23, encryptRoot, false},
        {QStringLiteral("/home"), QStringLiteral("f2fs"), 67, encryptHome, true},
        {QStringLiteral("/var/lib"), QStringLiteral("f2fs"), 10, encryptVarLib, true}
    };

    m_validatedPlan = std::move(plan);
    Q_EMIT planChanged();
    return true;
}

QString InstallationPlanController::storagePreflightFingerprint() const
{
    return QStringList{m_targetDisk, m_imagePath, m_imageFormat, m_swapChoice,
                       m_partitioningMode, QString::number(m_sharedSpaceMiB),
                       QString::number(m_sharedSpaceStartSector), m_sharedResizeDevice,
                       QString::fromUtf8(QJsonDocument::fromVariant(m_manualAssignments).toJson(QJsonDocument::Compact)),
                       QString::fromUtf8(QJsonDocument::fromVariant(m_manualPartitionLayout).toJson(QJsonDocument::Compact)),
                       QString::fromUtf8(QJsonDocument::fromVariant(m_partitionLabels).toJson(QJsonDocument::Compact)),
                       QString::fromUtf8(QJsonDocument::fromVariant(m_partitionSizesMiB).toJson(QJsonDocument::Compact)),
                       m_encryptHome ? QStringLiteral("1") : QStringLiteral("0"),
                       m_encryptVarLib ? QStringLiteral("1") : QStringLiteral("0"),
                       m_encryptRoot ? QStringLiteral("1") : QStringLiteral("0"),
                       m_encryptSwap ? QStringLiteral("1") : QStringLiteral("0"),
                       m_encryptInstallation ? QStringLiteral("1") : QStringLiteral("0")}
        .join(QChar(0x1f));
}

void InstallationPlanController::invalidateStoragePreflight()
{
    ++m_preflightGeneration;
    m_storagePreflightBusy = false;
    m_storagePreflightComplete = m_debugMode;
    m_storagePreflightErrors.clear();
    m_storageLayout.clear();
    m_targetDiskBytes = 0;
    m_logicalBlockSize = 0;
}

void InstallationPlanController::runStoragePreflight()
{
    if (m_debugMode || m_storagePreflightBusy)
        return;
    invalidateStoragePreflight();
    m_storagePreflightBusy = true;
    const quint64 generation = ++m_preflightGeneration;
    const QString fingerprint = storagePreflightFingerprint();
    const QString targetDisk = m_targetDisk;
    const QString imagePath = m_imagePath;
    const QString imageFormat = m_imageFormat;
    const QString swapChoice = m_swapChoice;
    const bool manualMode = m_partitioningMode == QStringLiteral("manual");
    const bool encryptRoot = manualMode
        ? m_encryptRoot && m_manualAssignments.values().contains(QVariant(QStringLiteral("/"))) : m_encryptInstallation;
    const bool encryptHome = manualMode
        ? m_encryptHome && m_manualAssignments.values().contains(QVariant(QStringLiteral("/home"))) : m_encryptInstallation;
    const bool encryptVarLib = manualMode
        ? m_encryptVarLib && m_manualAssignments.values().contains(QVariant(QStringLiteral("/var/lib"))) : m_encryptInstallation;
    const bool encryptSwap = manualMode
        ? m_encryptSwap && m_manualAssignments.values().contains(QVariant(QStringLiteral("swap"))) : m_encryptInstallation;
    const QString partitioningMode = m_partitioningMode;
    const QVariantMap manualAssignments = m_manualAssignments;
    const QVariantList manualPartitionLayout = m_manualPartitionLayout;
    const QVariantMap partitionLabels = m_partitionLabels;
    const QVariantMap partitionSizesMiB = m_partitionSizesMiB;
    const quint64 sharedSpaceMiB = m_sharedSpaceMiB;
    const quint64 sharedSpaceStartSector = m_sharedSpaceStartSector;
    const QString sharedResizeDevice = m_sharedResizeDevice;
    updateValidation();
    Q_EMIT planChanged();

    const QPointer<InstallationPlanController> guard(this);
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, generation, fingerprint, targetDisk, imagePath, imageFormat,
         swapChoice, encryptRoot, encryptHome, encryptVarLib, encryptSwap, partitioningMode, manualAssignments,
         manualPartitionLayout, partitionLabels, partitionSizesMiB, sharedSpaceMiB, sharedSpaceStartSector, sharedResizeDevice] {
            const StoragePreflightResult result = SystemDiscovery::preflightStorage(
                targetDisk, imagePath, imageFormat, swapChoice, encryptRoot, encryptHome, encryptVarLib,
                encryptSwap, partitioningMode, manualAssignments, manualPartitionLayout, partitionLabels, partitionSizesMiB, sharedSpaceMiB,
                sharedSpaceStartSector, sharedResizeDevice);
            if (!guard)
                return;
            QMetaObject::invokeMethod(guard.data(), [guard, generation, fingerprint, result] {
                if (!guard || guard->m_preflightGeneration != generation
                    || guard->storagePreflightFingerprint() != fingerprint)
                    return;
                guard->m_storagePreflightBusy = false;
                guard->m_storagePreflightComplete = true;
                guard->m_storagePreflightErrors = result.errors;
                guard->m_storageLayout = result.layout;
                guard->m_targetDiskBytes = result.diskBytes;
                guard->m_logicalBlockSize = result.logicalBlockSize;
                guard->updateValidation();
                Q_EMIT guard->planChanged();
            }, Qt::QueuedConnection);
        }));
}

void InstallationPlanController::acceptStoragePreflightResult(const StoragePreflightResult& result)
{
    if (m_debugMode)
        return;
    m_storagePreflightBusy = false;
    m_storagePreflightComplete = true;
    m_storagePreflightErrors = result.errors;
    m_storageLayout = result.layout;
    m_targetDiskBytes = result.diskBytes;
    m_logicalBlockSize = result.logicalBlockSize;
    updateValidation();
    Q_EMIT planChanged();
}

void InstallationPlanController::invalidatePlan()
{
    m_validatedPlan.reset();
    updateValidation();
    Q_EMIT planChanged();
}

void InstallationPlanController::updateValidation()
{
    QStringList errors;

    const auto requireChoice = [&errors](const QStringList& choices,
                                         const QString& value,
                                         const QString& label) {
        if (choices.isEmpty())
            errors.append(QObject::tr("%1 choices are not available yet.").arg(label));
        else if (!containsValue(choices, value))
            errors.append(QObject::tr("Select a valid %1.").arg(label.toLower()));
    };

    if (m_imagePath.isEmpty())
        errors.append(tr("A supported installation image has not been detected."));
    else if (m_imageFormat != QStringLiteral("DwarFS")
             && m_imageFormat != QStringLiteral("SquashFS"))
        errors.append(tr("The detected installation image format is not supported."));
    else if (!m_imageVerified)
        errors.append(tr("The installation image is not a readable file."));

    if (!m_uefiEnvironmentDetected)
        errors.append(tr("A UEFI boot environment has not been verified."));

    requireChoice(m_availableLanguages, m_language, tr("Language"));
    requireChoice(m_availableLocales, m_locale, tr("Locale"));
    requireChoice(m_availableTimeZones, m_timeZone, tr("Time zone"));
    requireChoice(m_availableKeyboardLayouts, m_keyboardLayout, tr("Keyboard layout"));
    requireChoice(m_availableTargetDisks, m_targetDisk, tr("Target disk"));
    if (containsValue(m_availableTargetDisks, m_targetDisk)
        && !SystemDiscovery::isSuitableTargetDisk(m_targetDisk, m_imagePath))
        errors.append(tr("The selected disk is no longer suitable: it must be writable, unmounted, large enough for the image and EFI partition, and a whole disk."));

    if (m_fullName.trimmed().isEmpty() || m_fullName.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))))
        errors.append(tr("Enter a valid full name."));

    static const QRegularExpression usernamePattern(QStringLiteral("^[a-z_][a-z0-9_-]{0,30}\\$?$"));
    if (!usernamePattern.match(m_username).hasMatch())
        errors.append(tr("Username must use lowercase letters, digits, underscores, or hyphens, with an optional trailing $."));
    else if (m_username == QStringLiteral("root") || m_username == QStringLiteral("nitrux"))
        errors.append(tr("That username is reserved."));

    static const QRegularExpression hostnamePattern(QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9_-]{0,61}[A-Za-z0-9])?$"));
    if (m_hostname.size() > 63 || !hostnamePattern.match(m_hostname).hasMatch())
        errors.append(tr("Enter a valid computer name (1–63 letters, digits, hyphens, or underscores)."));
    else if (m_hostname.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        errors.append(tr("localhost cannot be used as the computer name."));

    if (m_userPassword.size() < 12 || m_userPassword.size() > 64)
        errors.append(tr("Password must be 12–64 characters."));
    else if (m_userPassword.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))))
        errors.append(tr("Password cannot contain control characters."));
    else if (m_userPassword != m_passwordConfirmation)
        errors.append(tr("Passwords do not match."));
    else if (!m_allowWeakPassword) {
        const QString qualityError = passwordQualityError(m_userPassword, m_username);
        if (!qualityError.isEmpty())
            errors.append(qualityError);
    }

    if (!m_debugMode) {
        if (!m_acknowledgeDiskErasure)
            errors.append(tr("Confirm the selected storage changes."));
        const bool needsEncryption = (m_partitioningMode != QStringLiteral("manual") && m_encryptInstallation)
            || (m_encryptRoot && m_manualAssignments.values().contains(QVariant(QStringLiteral("/"))))
            || (m_encryptHome && m_manualAssignments.values().contains(QVariant(QStringLiteral("/home"))))
            || (m_encryptVarLib && m_manualAssignments.values().contains(QVariant(QStringLiteral("/var/lib"))))
            || (m_encryptSwap && m_manualAssignments.values().contains(QVariant(QStringLiteral("swap"))));
        if (needsEncryption
            && (m_encryptionPassphrase.size() < 12 || m_encryptionPassphrase.size() > 128
                || m_encryptionPassphrase.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")))
                || m_encryptionPassphrase != m_encryptionPassphraseConfirmation))
            errors.append(tr("Enter matching encryption passphrases of 12–128 characters."));
        if (!m_storagePreflightComplete)
            errors.append(tr("Run the read-only storage preflight from the summary before installation."));
        else
            errors.append(m_storagePreflightErrors);
    }

    if (m_partitioningMode != QStringLiteral("automated")
        && m_partitioningMode != QStringLiteral("share")
        && m_partitioningMode != QStringLiteral("manual"))
        errors.append(tr("Select a valid partitioning method."));

    static const QStringList swapChoices = {
        QStringLiteral("None"), QStringLiteral("Small"),
        QStringLiteral("Suspend"), QStringLiteral("File")
    };
    if (!swapChoices.contains(m_swapChoice))
        errors.append(tr("Select a valid swap option."));

    m_validationErrors = std::move(errors);
}
