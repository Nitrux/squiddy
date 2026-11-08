// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "InstallationPlan.h"

#include "SystemDiscovery.h"

#include <pwquality.h>

#include <QByteArray>
#include <QFileInfo>
#include <QRegularExpression>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <array>
#include <memory>
#include <utility>

namespace {

bool containsValue(const QStringList& choices, const QString& value)
{
    return !value.isEmpty() && choices.contains(value);
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
        m_availableTimeZones = {QStringLiteral("America/Mexico_City"), QStringLiteral("UTC")};
        m_availableKeyboardLayouts = {tr("English (US)"), tr("Spanish (Latin American)")};
        m_availableTargetDisks = {tr("Debug virtual disk (64 GiB)"), tr("Debug virtual disk (128 GiB)")};
        m_imagePath = QStringLiteral("debug://nitrux-base.squashfs");
        m_imageFormat = QStringLiteral("SquashFS");
        m_imageVerified = true;
        m_uefiEnvironmentDetected = true;

        m_language = m_availableLanguages.first();
        m_locale = m_availableLocales.first();
        m_timeZone = m_availableTimeZones.first();
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
QStringList InstallationPlanController::availableKeyboardLayouts() const { return m_availableKeyboardLayouts; }
QStringList InstallationPlanController::availableTargetDisks() const { return m_availableTargetDisks; }

void InstallationPlanController::setAvailableLanguages(const QStringList& choices)
{
    if (m_debugMode || m_availableLanguages == choices) return;
    m_availableLanguages = choices;
    emit choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableLocales(const QStringList& choices)
{
    if (m_debugMode || m_availableLocales == choices) return;
    m_availableLocales = choices;
    emit choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableTimeZones(const QStringList& choices)
{
    if (m_debugMode || m_availableTimeZones == choices) return;
    m_availableTimeZones = choices;
    emit choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableKeyboardLayouts(const QStringList& choices)
{
    if (m_debugMode || m_availableKeyboardLayouts == choices) return;
    m_availableKeyboardLayouts = choices;
    emit choicesChanged();
    invalidatePlan();
}

void InstallationPlanController::setAvailableTargetDisks(const QStringList& choices)
{
    if (m_debugMode || m_availableTargetDisks == choices) return;
    m_availableTargetDisks = choices;
    invalidateStoragePreflight();
    emit choicesChanged();
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
    invalidateStoragePreflight();
    invalidatePlan();
}

QString InstallationPlanController::partitioningMode() const { return m_partitioningMode; }
void InstallationPlanController::setPartitioningMode(const QString& value)
{
    if (m_partitioningMode == value) return;
    m_partitioningMode = value;
    invalidatePlan();
}

QString InstallationPlanController::swapChoice() const { return m_swapChoice; }
void InstallationPlanController::setSwapChoice(const QString& value)
{
    if (m_swapChoice == value) return;
    m_swapChoice = value;
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
    invalidateStoragePreflight();
    invalidatePlan();
}

bool InstallationPlanController::encryptVarLib() const { return m_encryptVarLib; }
void InstallationPlanController::setEncryptVarLib(bool value)
{
    if (m_encryptVarLib == value) return;
    m_encryptVarLib = value;
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
        {QStringLiteral("encryptionPassphrase"), m_encryptHome || m_encryptVarLib
            ? plan.encryptionPassphrase : QString()},
        {QStringLiteral("encryptionPassphraseConfirmation"), m_encryptHome || m_encryptVarLib
            ? m_encryptionPassphraseConfirmation : QString()},
        {QStringLiteral("acknowledgeDiskErasure"), plan.acknowledgeDiskErasure},
        {QStringLiteral("userShell"), plan.userShell},
        {QStringLiteral("userGroups"), plan.userGroups},
        {QStringLiteral("automaticLogin"), plan.automaticLogin},
        {QStringLiteral("allowWeakPassword"), plan.allowWeakPassword},
        {QStringLiteral("targetDevice"), plan.targetDevice},
        {QStringLiteral("partitioningMode"), partitioningMode},
        {QStringLiteral("swapChoice"), swapChoice},
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
        emit planChanged();
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
    plan.encryptionPassphrase = (m_encryptHome || m_encryptVarLib)
        ? m_encryptionPassphrase : QString();
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
        : PartitioningMode::Manual;
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
        {QStringLiteral("/"), QStringLiteral("xfs"), 23, false, false},
        {QStringLiteral("/home"), QStringLiteral("f2fs"), 67, m_encryptHome, true},
        {QStringLiteral("/var/lib"), QStringLiteral("f2fs"), 10, m_encryptVarLib, true}
    };

    m_validatedPlan = std::move(plan);
    emit planChanged();
    return true;
}

QString InstallationPlanController::storagePreflightFingerprint() const
{
    return QStringList{m_targetDisk, m_imagePath, m_imageFormat, m_swapChoice,
                       m_encryptHome ? QStringLiteral("1") : QStringLiteral("0"),
                       m_encryptVarLib ? QStringLiteral("1") : QStringLiteral("0")}
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
    const bool encryptHome = m_encryptHome;
    const bool encryptVarLib = m_encryptVarLib;
    updateValidation();
    emit planChanged();

    const QPointer<InstallationPlanController> guard(this);
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, generation, fingerprint, targetDisk, imagePath, imageFormat,
         swapChoice, encryptHome, encryptVarLib] {
            const StoragePreflightResult result = SystemDiscovery::preflightStorage(
                targetDisk, imagePath, imageFormat, swapChoice, encryptHome, encryptVarLib);
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
                emit guard->planChanged();
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
    emit planChanged();
}

void InstallationPlanController::invalidatePlan()
{
    m_validatedPlan.reset();
    updateValidation();
    emit planChanged();
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
            errors.append(tr("Confirm that the selected disk may be erased."));
        if ((m_encryptHome || m_encryptVarLib)
            && (m_encryptionPassphrase.size() < 12 || m_encryptionPassphrase.size() > 128
                || m_encryptionPassphrase.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")))
                || m_encryptionPassphrase != m_encryptionPassphraseConfirmation))
            errors.append(tr("Enter matching encryption passphrases of 12–128 characters."));
        if (!m_storagePreflightComplete)
            errors.append(tr("Run the read-only storage preflight from the summary before installation."));
        else
            errors.append(m_storagePreflightErrors);
    }

    if (m_partitioningMode != QStringLiteral("automated"))
        errors.append(tr("Manual partition editing is not available yet."));

    static const QStringList swapChoices = {
        QStringLiteral("None"), QStringLiteral("Small"),
        QStringLiteral("Suspend"), QStringLiteral("File")
    };
    if (!swapChoices.contains(m_swapChoice))
        errors.append(tr("Select a valid swap option."));

    m_validationErrors = std::move(errors);
}
