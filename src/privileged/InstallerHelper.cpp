// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "InstallerHelper.h"

#include "../InstallationPlan.h"
#include "../SystemDiscovery.h"
#include "InstallationExecutor.h"

#include <QDBusError>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QFileInfo>
#include <QMetaType>
#include <QPointer>
#include <QRunnable>
#include <QRegularExpression>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QUuid>

#include <polkit/polkit.h>

#include <utility>
#include <pwd.h>

#include <sys/stat.h>

namespace {

constexpr char actionId[] = "org.nitrux.squiddy.install";

bool hasType(const QVariantMap& map, const QString& key, QMetaType::Type type)
{
    return map.contains(key) && map.value(key).metaType().id() == type;
}

bool readString(const QVariantMap& map, const QString& key, QString* value)
{
    if (!hasType(map, key, QMetaType::QString))
        return false;
    *value = map.value(key).toString();
    return !value->isEmpty() && !value->contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")));
}

bool readBool(const QVariantMap& map, const QString& key, bool* value)
{
    if (!hasType(map, key, QMetaType::Bool))
        return false;
    *value = map.value(key).toBool();
    return true;
}

bool readInt(const QVariantMap& map, const QString& key, int* value)
{
    if (!hasType(map, key, QMetaType::Int))
        return false;
    *value = map.value(key).toInt();
    return true;
}

bool isWholeBlockDevice(const QString& devicePath)
{
    const QFileInfo deviceInfo(devicePath);
    const QString canonicalPath = deviceInfo.canonicalFilePath();
    if (canonicalPath.isEmpty() || !canonicalPath.startsWith(QStringLiteral("/dev/")))
        return false;

    const QByteArray pathBytes = canonicalPath.toLocal8Bit();
    struct stat deviceStatus {};
    if (::stat(pathBytes.constData(), &deviceStatus) != 0 || !S_ISBLK(deviceStatus.st_mode))
        return false;

    const QString sysfsPath = QStringLiteral("/sys/class/block/%1/partition")
        .arg(QFileInfo(canonicalPath).fileName());
    return !QFileInfo::exists(sysfsPath);
}

QString validatePayload(const QVariantMap& payload, InstallationPlan* outputPlan)
{
    static const QStringList requiredKeys = {
        QStringLiteral("debugOnly"), QStringLiteral("uefiEnvironmentDetected"),
        QStringLiteral("requiresUefi"), QStringLiteral("requiresGpt"),
        QStringLiteral("imagePath"), QStringLiteral("imageFormat"),
        QStringLiteral("language"), QStringLiteral("locale"), QStringLiteral("timeZone"),
        QStringLiteral("keyboardLayout"), QStringLiteral("keyboardVariant"),
        QStringLiteral("keyboardModel"), QStringLiteral("keyboardOptions"),
        QStringLiteral("fullName"), QStringLiteral("username"), QStringLiteral("hostname"),
        QStringLiteral("userPassword"), QStringLiteral("passwordConfirmation"),
        QStringLiteral("encryptionPassphrase"), QStringLiteral("encryptionPassphraseConfirmation"),
        QStringLiteral("acknowledgeDiskErasure"),
        QStringLiteral("userShell"), QStringLiteral("userGroups"),
        QStringLiteral("automaticLogin"), QStringLiteral("allowWeakPassword"),
        QStringLiteral("targetDevice"), QStringLiteral("manualAssignments"),
        QStringLiteral("manualPartitionLayout"),
        QStringLiteral("partitionLabels"), QStringLiteral("partitionSizesMiB"),
        QStringLiteral("sharedSpaceMiB"), QStringLiteral("sharedSpaceStartSector"),
        QStringLiteral("sharedResizeDevice"),
        QStringLiteral("partitioningMode"),
        QStringLiteral("swapChoice"), QStringLiteral("encryptSwap"), QStringLiteral("partitions"),
        QStringLiteral("efiMountPoint"), QStringLiteral("efiRecommendedSizeMiB"),
        QStringLiteral("storageLayout"), QStringLiteral("targetDiskBytes"),
        QStringLiteral("logicalBlockSize")
    };
    if (payload.size() != requiredKeys.size())
        return QObject::tr("The plan contains missing or unexpected fields.");
    for (const QString& key : requiredKeys) {
        if (!payload.contains(key))
            return QObject::tr("The plan contains missing or unexpected fields.");
    }

    bool debugOnly = false;
    bool uefiDetected = false;
    bool requiresUefi = false;
    bool requiresGpt = false;
    if (!readBool(payload, QStringLiteral("debugOnly"), &debugOnly)
        || !readBool(payload, QStringLiteral("uefiEnvironmentDetected"), &uefiDetected)
        || !readBool(payload, QStringLiteral("requiresUefi"), &requiresUefi)
        || !readBool(payload, QStringLiteral("requiresGpt"), &requiresGpt))
        return QObject::tr("The plan has invalid firmware flags.");
    if (debugOnly)
        return QObject::tr("Debug-only plans cannot be sent to the privileged helper.");
    if (!uefiDetected || !requiresUefi || !requiresGpt || !QFileInfo(QStringLiteral("/sys/firmware/efi")).isDir())
        return QObject::tr("This helper requires a verified UEFI environment and GPT plan.");

    QString imagePath;
    QString imageFormat;
    if (!readString(payload, QStringLiteral("imagePath"), &imagePath)
        || !readString(payload, QStringLiteral("imageFormat"), &imageFormat)
        || (imageFormat != QStringLiteral("DwarFS") && imageFormat != QStringLiteral("SquashFS")))
        return QObject::tr("The image source is invalid or unsupported.");
    const QFileInfo imageInfo(imagePath);
    if (!imageInfo.isFile() || !imageInfo.isReadable())
        return QObject::tr("The installation image is not a readable file.");

    QString language;
    QString locale;
    QString timeZone;
    QString keyboardLayout;
    QString keyboardVariant;
    QString keyboardModel;
    QString keyboardOptions;
    QString fullName;
    QString username;
    QString hostname;
    QString targetDevice;
    QString partitioningMode;
    QString swapChoice;
    QString userShell;
    QString efiMountPoint;
    if (!readString(payload, QStringLiteral("language"), &language)
        || !readString(payload, QStringLiteral("locale"), &locale)
        || !readString(payload, QStringLiteral("timeZone"), &timeZone)
        || !readString(payload, QStringLiteral("keyboardLayout"), &keyboardLayout)
        || !hasType(payload, QStringLiteral("keyboardVariant"), QMetaType::QString)
        || !hasType(payload, QStringLiteral("keyboardModel"), QMetaType::QString)
        || !hasType(payload, QStringLiteral("keyboardOptions"), QMetaType::QString)
        || !readString(payload, QStringLiteral("fullName"), &fullName)
        || !readString(payload, QStringLiteral("username"), &username)
        || !readString(payload, QStringLiteral("hostname"), &hostname)
        || !readString(payload, QStringLiteral("targetDevice"), &targetDevice)
        || !readString(payload, QStringLiteral("partitioningMode"), &partitioningMode)
        || !readString(payload, QStringLiteral("swapChoice"), &swapChoice)
        || !readString(payload, QStringLiteral("userShell"), &userShell)
        || !readString(payload, QStringLiteral("efiMountPoint"), &efiMountPoint))
        return QObject::tr("The plan contains invalid or empty configuration values.");

    static const QRegularExpression localePattern(QStringLiteral("^[A-Za-z0-9_@.+-]{1,128}$"));
    static const QRegularExpression timeZonePattern(QStringLiteral("^(UTC|[A-Za-z0-9_+-]+(/[A-Za-z0-9_+.-]+)+)$"));
    if (!localePattern.match(locale).hasMatch() || !timeZonePattern.match(timeZone).hasMatch())
        return QObject::tr("The locale or time zone identifier is invalid.");
    static const QRegularExpression xkbValuePattern(QStringLiteral("^[A-Za-z0-9_,:+-]{0,128}$"));
    if (!xkbValuePattern.match(payload.value(QStringLiteral("keyboardVariant")).toString()).hasMatch()
        || !xkbValuePattern.match(payload.value(QStringLiteral("keyboardModel")).toString()).hasMatch()
        || !xkbValuePattern.match(payload.value(QStringLiteral("keyboardOptions")).toString()).hasMatch())
        return QObject::tr("Keyboard settings contain unsupported characters.");

    const SystemDiscoveryResults discovered = SystemDiscovery::discover();
    if (!discovered.uefiEnvironmentDetected
        || discovered.imagePath != imagePath
        || discovered.imageFormat != imageFormat
        || !discovered.languages.contains(language)
        || !discovered.locales.contains(locale)
        || !discovered.timeZones.contains(timeZone)
        || !discovered.keyboardLayouts.contains(keyboardLayout)
        || !discovered.targetDisks.contains(targetDevice))
        return QObject::tr("The plan no longer matches the live system discovery results.");

    if (partitioningMode != QStringLiteral("automated")
        && partitioningMode != QStringLiteral("share")
        && partitioningMode != QStringLiteral("manual"))
        return QObject::tr("The partitioning mode is invalid.");
    if (!hasType(payload, QStringLiteral("manualAssignments"), QMetaType::QVariantMap)
        || !hasType(payload, QStringLiteral("manualPartitionLayout"), QMetaType::QVariantList)
        || !hasType(payload, QStringLiteral("partitionLabels"), QMetaType::QVariantMap)
        || !hasType(payload, QStringLiteral("partitionSizesMiB"), QMetaType::QVariantMap)
        || !hasType(payload, QStringLiteral("sharedSpaceMiB"), QMetaType::ULongLong)
        || !hasType(payload, QStringLiteral("sharedSpaceStartSector"), QMetaType::ULongLong)
        || !hasType(payload, QStringLiteral("sharedResizeDevice"), QMetaType::QString))
        return QObject::tr("The storage selections are malformed.");
    const QVariantMap assignments = payload.value(QStringLiteral("manualAssignments")).toMap();
    const QVariantList manualLayout = payload.value(QStringLiteral("manualPartitionLayout")).toList();
    const QVariantMap labels = payload.value(QStringLiteral("partitionLabels")).toMap();
    const QVariantMap sizes = payload.value(QStringLiteral("partitionSizesMiB")).toMap();
    const quint64 sharedSpaceMiB = payload.value(QStringLiteral("sharedSpaceMiB")).toULongLong();
    const quint64 sharedSpaceStartSector = payload.value(QStringLiteral("sharedSpaceStartSector")).toULongLong();
    const QString sharedResizeDevice = payload.value(QStringLiteral("sharedResizeDevice")).toString();
    static const QStringList roles = {QStringLiteral("/boot/efi"), QStringLiteral("/"),
        QStringLiteral("/home"), QStringLiteral("/var/lib"), QStringLiteral("swap")};
    if (labels.size() != roles.size())
        return QObject::tr("The partition labels are malformed.");
    for (const QString& role : roles) {
        if (!labels.contains(role) || labels.value(role).metaType().id() != QMetaType::QString)
            return QObject::tr("The partition labels are malformed.");
    }
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        if (it.value().metaType().id() != QMetaType::QString || !roles.contains(it.value().toString()))
            return QObject::tr("A manual partition assignment is malformed.");
    }
    for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) {
        if (!roles.contains(it.key()) || it.value().metaType().id() != QMetaType::ULongLong)
            return QObject::tr("A partition size is malformed.");
    }
    if (userShell != QStringLiteral("/bin/zsh") || efiMountPoint != QStringLiteral("/boot/efi"))
        return QObject::tr("The plan does not match the configured Nitrux defaults.");

    int efiSize = 0;
    if (!readInt(payload, QStringLiteral("efiRecommendedSizeMiB"), &efiSize) || efiSize != 100)
        return QObject::tr("The EFI partition recommendation is invalid.");

    bool automaticLogin = false;
    bool allowWeakPassword = false;
    bool acknowledgeDiskErasure = false;
    if (!readBool(payload, QStringLiteral("automaticLogin"), &automaticLogin)
        || !readBool(payload, QStringLiteral("allowWeakPassword"), &allowWeakPassword)
        || !readBool(payload, QStringLiteral("acknowledgeDiskErasure"), &acknowledgeDiskErasure)
        || !acknowledgeDiskErasure)
        return QObject::tr("The account options are invalid.");

    const QVariant passwordValue = payload.value(QStringLiteral("userPassword"));
    const QVariant confirmationValue = payload.value(QStringLiteral("passwordConfirmation"));
    if (passwordValue.metaType().id() != QMetaType::QString
        || confirmationValue.metaType().id() != QMetaType::QString
        || passwordValue.toString() != confirmationValue.toString())
        return QObject::tr("Password confirmation is invalid.");
    const QVariant encryptionValue = payload.value(QStringLiteral("encryptionPassphrase"));
    const QVariant encryptionConfirmation = payload.value(QStringLiteral("encryptionPassphraseConfirmation"));
    if (encryptionValue.metaType().id() != QMetaType::QString
        || encryptionConfirmation.metaType().id() != QMetaType::QString
        || encryptionValue.toString() != encryptionConfirmation.toString())
        return QObject::tr("Encryption passphrase confirmation is invalid.");

    if (!hasType(payload, QStringLiteral("userGroups"), QMetaType::QStringList)
        || payload.value(QStringLiteral("userGroups")).toStringList() != QStringList{
            QStringLiteral("adm"), QStringLiteral("bluetooth"), QStringLiteral("cdrom"),
            QStringLiteral("dip"), QStringLiteral("fuse"), QStringLiteral("gamemode"),
            QStringLiteral("input"), QStringLiteral("lpadmin"), QStringLiteral("pipewire"),
            QStringLiteral("plugdev"), QStringLiteral("sambashare"), QStringLiteral("sudo"),
            QStringLiteral("video"), QStringLiteral("wheel")})
        return QObject::tr("The user group list does not match the configured Nitrux defaults.");

    if (!isWholeBlockDevice(targetDevice)
        || !SystemDiscovery::isSuitableTargetDisk(targetDevice, imagePath))
        return QObject::tr("The selected target failed the read-only disk preflight.");

    bool encryptSwap = false;
    if (!readBool(payload, QStringLiteral("encryptSwap"), &encryptSwap))
        return QObject::tr("The swap encryption setting is malformed.");

    const QVariant partitionsValue = payload.value(QStringLiteral("partitions"));
    if (partitionsValue.metaType().id() != QMetaType::QVariantList)
        return QObject::tr("The partition layout is malformed.");
    const QVariantList partitions = partitionsValue.toList();
    if (partitions.size() != 3)
        return QObject::tr("The partition layout is incomplete.");

    InstallationPlanController validator(false);
    validator.setAvailableLanguages({language});
    validator.setAvailableLocales({locale});
    validator.setAvailableTimeZones({timeZone});
    validator.setAvailableKeyboardLayouts({keyboardLayout});
    validator.setAvailableTargetDisks({targetDevice});
    validator.setDetectedImage(imagePath, imageFormat);
    validator.setUefiEnvironmentDetected(true);
    validator.setLanguage(language);
    validator.setLocale(locale);
    validator.setTimeZone(timeZone);
    validator.setKeyboardLayout(keyboardLayout);
    validator.setKeyboardVariant(payload.value(QStringLiteral("keyboardVariant")).toString());
    validator.setKeyboardModel(payload.value(QStringLiteral("keyboardModel")).toString());
    validator.setKeyboardOptions(payload.value(QStringLiteral("keyboardOptions")).toString());
    validator.setFullName(fullName);
    validator.setUsername(username);
    validator.setHostname(hostname);
    validator.setTargetDisk(targetDevice);
    validator.setPartitioningMode(partitioningMode);
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it)
        validator.setManualAssignment(it.key(), it.value().toString());
    validator.setManualPartitionLayout(manualLayout);
    for (auto it = labels.cbegin(); it != labels.cend(); ++it)
        validator.setPartitionLabel(it.key(), it.value().toString());
    for (auto it = sizes.cbegin(); it != sizes.cend(); ++it)
        validator.setPartitionSizeMiB(it.key(), it.value().toULongLong());
    validator.setSharedSpaceStartSector(sharedSpaceStartSector);
    validator.setSharedSpaceMiB(sharedSpaceMiB);
    validator.setSharedResizeDevice(sharedResizeDevice);
    validator.setSwapChoice(swapChoice);
    validator.setAutomaticLogin(automaticLogin);
    validator.setAllowWeakPassword(allowWeakPassword);
    validator.setUserPassword(passwordValue.toString());
    validator.setPasswordConfirmation(confirmationValue.toString());
    validator.setEncryptionPassphrase(encryptionValue.toString());
    validator.setEncryptionPassphraseConfirmation(encryptionConfirmation.toString());
    validator.setAcknowledgeDiskErasure(acknowledgeDiskErasure);

    const QVariantMap root = partitions.at(0).toMap();
    const QVariantMap home = partitions.at(1).toMap();
    const QVariantMap varLib = partitions.at(2).toMap();
    if (!hasType(root, QStringLiteral("encrypted"), QMetaType::Bool)
        || !hasType(home, QStringLiteral("encrypted"), QMetaType::Bool)
        || !hasType(varLib, QStringLiteral("encrypted"), QMetaType::Bool))
        return QObject::tr("The encryption settings are malformed.");
    if (!hasType(payload, QStringLiteral("storageLayout"), QMetaType::QVariantList)
        || !hasType(payload, QStringLiteral("targetDiskBytes"), QMetaType::ULongLong)
        || !hasType(payload, QStringLiteral("logicalBlockSize"), QMetaType::ULongLong))
        return QObject::tr("The submitted storage preflight result is malformed.");

    const QVariantList submittedStorageLayout = payload.value(QStringLiteral("storageLayout")).toList();
    const StoragePreflightResult storage = SystemDiscovery::preflightStorage(
        targetDevice, imagePath, imageFormat, swapChoice,
        root.value(QStringLiteral("encrypted")).toBool(),
        home.value(QStringLiteral("encrypted")).toBool(),
        varLib.value(QStringLiteral("encrypted")).toBool(),
        encryptSwap, partitioningMode,
        assignments, manualLayout, labels, sizes, sharedSpaceMiB, sharedSpaceStartSector, sharedResizeDevice);
    if (!storage.valid
        || submittedStorageLayout != storage.layout
        || payload.value(QStringLiteral("targetDiskBytes")).toULongLong() != storage.diskBytes
        || payload.value(QStringLiteral("logicalBlockSize")).toULongLong() != storage.logicalBlockSize)
        return QObject::tr("The selected disk or image no longer matches the approved storage preflight.");
    validator.setEncryptRoot(root.value(QStringLiteral("encrypted")).toBool());
    validator.setEncryptHome(home.value(QStringLiteral("encrypted")).toBool());
    validator.setEncryptVarLib(varLib.value(QStringLiteral("encrypted")).toBool());
    validator.setEncryptSwap(encryptSwap);
    validator.acceptStoragePreflightResult(storage);

    if (!validator.validateAndBuildPlan())
        return QObject::tr("The helper rejected one or more plan values.");

    const auto& validated = validator.validatedPlan();
    if (!validated || validated->partitions.size() != 3)
        return QObject::tr("The helper could not construct the partition plan.");

    for (qsizetype index = 0; index < partitions.size(); ++index) {
        if (partitions.at(index).metaType().id() != QMetaType::QVariantMap)
            return QObject::tr("A partition entry is malformed.");
        const QVariantMap submitted = partitions.at(index).toMap();
        static const QStringList partitionKeys = {
            QStringLiteral("mountPoint"), QStringLiteral("filesystem"),
            QStringLiteral("sizePercent"), QStringLiteral("encrypted"),
            QStringLiteral("compressed")
        };
        if (submitted.size() != partitionKeys.size())
            return QObject::tr("A partition entry contains missing or unexpected fields.");
        for (const QString& key : partitionKeys) {
            if (!submitted.contains(key))
                return QObject::tr("A partition entry contains missing or unexpected fields.");
        }
        const PlannedPartition& expected = validated->partitions.at(static_cast<size_t>(index));
        int sizePercent = 0;
        bool encrypted = false;
        bool compressed = false;
        QString mountPoint;
        QString filesystem;
        if (!readString(submitted, QStringLiteral("mountPoint"), &mountPoint)
            || !readString(submitted, QStringLiteral("filesystem"), &filesystem)
            || !readInt(submitted, QStringLiteral("sizePercent"), &sizePercent)
            || !readBool(submitted, QStringLiteral("encrypted"), &encrypted)
            || !readBool(submitted, QStringLiteral("compressed"), &compressed)
            || mountPoint != expected.mountPoint || filesystem != expected.filesystem
            || sizePercent != expected.sizePercent || encrypted != expected.encrypted
            || compressed != expected.compressed)
            return QObject::tr("The partition layout does not match the validated Nitrux layout.");
    }

    if (outputPlan)
        *outputPlan = *validated;
    return {};
}

bool checkPolkitAuthorization(const QString& caller)
{
    const QByteArray callerBytes = caller.toUtf8();
    GError* error = nullptr;
    PolkitAuthority* authority = polkit_authority_get_sync(nullptr, &error);
    if (!authority) {
        if (error)
            g_error_free(error);
        return false;
    }

    PolkitSubject* subject = polkit_system_bus_name_new(callerBytes.constData());
    if (!subject) {
        g_object_unref(authority);
        return false;
    }

    PolkitAuthorizationResult* result = polkit_authority_check_authorization_sync(
        authority,
        subject,
        actionId,
        nullptr,
        POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
        nullptr,
        &error);

    const bool authorized = result && polkit_authorization_result_get_is_authorized(result);
    if (result)
        g_object_unref(result);
    if (error)
        g_error_free(error);
    g_object_unref(subject);
    g_object_unref(authority);
    return authorized;
}

} // namespace

InstallerHelper::InstallerHelper(QObject* parent)
    : QObject(parent)
{
}

QString InstallerHelper::AuthorizeInstall()
{
    if (!calledFromDBus())
        return {};

    const QDBusMessage request = message();
    const QDBusConnection bus = connection();
    const QString caller = request.service();
    if (caller.isEmpty())
        return {};

    setDelayedReply(true);
    QPointer<InstallerHelper> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, bus, request, caller] {
        const bool authorized = checkPolkitAuthorization(caller);
        if (!self)
            return;

        QMetaObject::invokeMethod(self.data(), [self, bus, request, caller, authorized] {
            if (!self)
                return;

            QString token;
            if (authorized) {
                const QDateTime now = QDateTime::currentDateTimeUtc();
                for (auto it = self->m_authorizationGrants.begin(); it != self->m_authorizationGrants.end();) {
                    if (it->expiresAt < now)
                        it = self->m_authorizationGrants.erase(it);
                    else
                        ++it;
                }
                token = QUuid::createUuid().toString(QUuid::WithoutBraces);
                self->m_authorizationGrants.insert(token, {caller, now.addSecs(120)});
            }
            bus.send(request.createReply(QVariant(token)));
        }, Qt::QueuedConnection);
    }));

    return {};
}

QString InstallerHelper::SubmitPlan(const QString& token, const QVariantMap& payload)
{
    if (!calledFromDBus())
        return tr("The request did not originate on D-Bus.");

    const QString caller = message().service();
    auto grant = m_authorizationGrants.find(token);
    if (grant == m_authorizationGrants.end()
        || grant->caller != caller
        || grant->expiresAt < QDateTime::currentDateTimeUtc()) {
        if (grant != m_authorizationGrants.end())
            m_authorizationGrants.erase(grant);
        sendErrorReply(QDBusError::AccessDenied, tr("A fresh Polkit authorization is required."));
        return {};
    }
    m_authorizationGrants.erase(grant);

    QString liveSessionUser;
    if (connection().interface()) {
        const QDBusReply<uint> uidReply = connection().interface()->serviceUid(caller);
        if (uidReply.isValid()) {
            if (const struct passwd* entry = ::getpwuid(uidReply.value()))
                liveSessionUser = QString::fromLocal8Bit(entry->pw_name);
        }
    }

    InstallationPlan plan;
    const QString validationError = validatePayload(payload, &plan);
    if (!validationError.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, validationError);
        return {};
    }
    static const QRegularExpression liveUserPattern(QStringLiteral("^[a-z_][a-z0-9_-]{0,30}\\$?$"));
    if (liveUserPattern.match(liveSessionUser).hasMatch())
        plan.liveSessionUser = liveSessionUser;

    if (m_installationActive) {
        sendErrorReply(QDBusError::LimitsExceeded, tr("An installation is already running."));
        return {};
    }
    m_installationActive = true;
    QPointer<InstallerHelper> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, plan = std::move(plan)] {
        if (!self)
            return;
        QString error;
        const bool success = InstallationExecutor::execute(plan, [self](const InstallationProgressUpdate& update) {
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, update] {
                if (self)
                    Q_EMIT self->InstallationProgressChanged(update.percent, update.phase, update.detail, false, false);
            }, Qt::QueuedConnection);
        }, &error);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, success, error] {
            if (self) {
                self->m_installationActive = false;
                Q_EMIT self->InstallationProgressChanged(success ? 100 : 0,
                    success ? QObject::tr("Complete") : QObject::tr("Failed"),
                    success ? QObject::tr("Nitrux was installed successfully.") : error, true, success);
            }
        }, Qt::QueuedConnection);
    }));
    return tr("The installation has started.");
}
