// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <vector>

enum class ImageFormat {
    DwarFS,
    SquashFS
};

enum class PartitioningMode {
    Automated,
    Manual
};

enum class SwapChoice {
    None,
    Small,
    Suspend,
    File
};

struct StoragePreflightResult;

struct PlannedPartition {
    QString mountPoint;
    QString filesystem;
    int sizePercent = 0;
    bool encrypted = false;
    bool compressed = false;
};

struct InstallationPlan {
    bool debugOnly = false;
    bool uefiEnvironmentDetected = false;
    bool requiresUefi = true;
    bool requiresGpt = true;
    QString imagePath;
    QString liveSessionUser;
    ImageFormat imageFormat = ImageFormat::SquashFS;
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
    QString userPassword;
    QString encryptionPassphrase;
    QString userShell = QStringLiteral("/bin/zsh");
    bool acknowledgeDiskErasure = false;
    QStringList userGroups;
    bool automaticLogin = false;
    bool allowWeakPassword = false;
    QString targetDevice;
    PartitioningMode partitioningMode = PartitioningMode::Automated;
    SwapChoice swapChoice = SwapChoice::Small;
    std::vector<PlannedPartition> partitions;
    QString efiMountPoint = QStringLiteral("/boot/efi");
    int efiRecommendedSizeMiB = 100;
    QVariantList storageLayout;
    quint64 targetDiskBytes = 0;
    quint64 logicalBlockSize = 0;
};

class InstallationPlanController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool debugMode READ debugMode CONSTANT)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY choicesChanged)
    Q_PROPERTY(QStringList availableLocales READ availableLocales NOTIFY choicesChanged)
    Q_PROPERTY(QStringList availableTimeZones READ availableTimeZones NOTIFY choicesChanged)
    Q_PROPERTY(QStringList availableKeyboardLayouts READ availableKeyboardLayouts NOTIFY choicesChanged)
    Q_PROPERTY(QStringList availableTargetDisks READ availableTargetDisks NOTIFY choicesChanged)
    Q_PROPERTY(QString imagePath READ imagePath NOTIFY planChanged)
    Q_PROPERTY(QString imageFormat READ imageFormat NOTIFY planChanged)
    Q_PROPERTY(bool uefiEnvironmentDetected READ uefiEnvironmentDetected NOTIFY planChanged)

    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY planChanged)
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY planChanged)
    Q_PROPERTY(QString timeZone READ timeZone WRITE setTimeZone NOTIFY planChanged)
    Q_PROPERTY(QString keyboardLayout READ keyboardLayout WRITE setKeyboardLayout NOTIFY planChanged)
    Q_PROPERTY(QString keyboardVariant READ keyboardVariant WRITE setKeyboardVariant NOTIFY planChanged)
    Q_PROPERTY(QString keyboardModel READ keyboardModel WRITE setKeyboardModel NOTIFY planChanged)
    Q_PROPERTY(QString keyboardOptions READ keyboardOptions WRITE setKeyboardOptions NOTIFY planChanged)
    Q_PROPERTY(QString fullName READ fullName WRITE setFullName NOTIFY planChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY planChanged)
    Q_PROPERTY(QString hostname READ hostname WRITE setHostname NOTIFY planChanged)
    Q_PROPERTY(QString targetDisk READ targetDisk WRITE setTargetDisk NOTIFY planChanged)
    Q_PROPERTY(QString partitioningMode READ partitioningMode WRITE setPartitioningMode NOTIFY planChanged)
    Q_PROPERTY(QString swapChoice READ swapChoice WRITE setSwapChoice NOTIFY planChanged)
    Q_PROPERTY(bool automaticLogin READ automaticLogin WRITE setAutomaticLogin NOTIFY planChanged)
    Q_PROPERTY(bool allowWeakPassword READ allowWeakPassword WRITE setAllowWeakPassword NOTIFY planChanged)
    Q_PROPERTY(bool encryptHome READ encryptHome WRITE setEncryptHome NOTIFY planChanged)
    Q_PROPERTY(bool encryptVarLib READ encryptVarLib WRITE setEncryptVarLib NOTIFY planChanged)
    Q_PROPERTY(bool acknowledgeDiskErasure READ acknowledgeDiskErasure WRITE setAcknowledgeDiskErasure NOTIFY planChanged)

    Q_PROPERTY(bool planValid READ planValid NOTIFY planChanged)
    Q_PROPERTY(QStringList validationErrors READ validationErrors NOTIFY planChanged)
    Q_PROPERTY(bool validatedPlanAvailable READ validatedPlanAvailable NOTIFY planChanged)
    Q_PROPERTY(bool storagePreflightBusy READ storagePreflightBusy NOTIFY planChanged)
    Q_PROPERTY(bool storagePreflightComplete READ storagePreflightComplete NOTIFY planChanged)
    Q_PROPERTY(QStringList storagePreflightErrors READ storagePreflightErrors NOTIFY planChanged)
    Q_PROPERTY(QVariantList storageLayout READ storageLayout NOTIFY planChanged)
    Q_PROPERTY(qulonglong targetDiskBytes READ targetDiskBytes NOTIFY planChanged)
    Q_PROPERTY(qulonglong logicalBlockSize READ logicalBlockSize NOTIFY planChanged)

public:
    explicit InstallationPlanController(bool debugMode, QObject* parent = nullptr);

    bool debugMode() const;
    QStringList availableLanguages() const;
    QStringList availableLocales() const;
    QStringList availableTimeZones() const;
    QStringList availableKeyboardLayouts() const;
    QStringList availableTargetDisks() const;
    void setAvailableLanguages(const QStringList& choices);
    void setAvailableLocales(const QStringList& choices);
    void setAvailableTimeZones(const QStringList& choices);
    void setAvailableKeyboardLayouts(const QStringList& choices);
    // The discovery provider supplies only disks that passed GPT suitability checks.
    void setAvailableTargetDisks(const QStringList& choices);
    QString imagePath() const;
    QString imageFormat() const;
    void setDetectedImage(const QString& path, const QString& format);
    bool uefiEnvironmentDetected() const;
    void setUefiEnvironmentDetected(bool detected);

    QString language() const;
    void setLanguage(const QString& value);
    QString locale() const;
    void setLocale(const QString& value);
    QString timeZone() const;
    void setTimeZone(const QString& value);
    QString keyboardLayout() const;
    void setKeyboardLayout(const QString& value);
    QString keyboardVariant() const;
    void setKeyboardVariant(const QString& value);
    QString keyboardModel() const;
    void setKeyboardModel(const QString& value);
    QString keyboardOptions() const;
    void setKeyboardOptions(const QString& value);
    QString fullName() const;
    void setFullName(const QString& value);
    QString username() const;
    void setUsername(const QString& value);
    QString hostname() const;
    void setHostname(const QString& value);
    QString targetDisk() const;
    void setTargetDisk(const QString& value);
    QString partitioningMode() const;
    void setPartitioningMode(const QString& value);
    QString swapChoice() const;
    void setSwapChoice(const QString& value);
    bool automaticLogin() const;
    void setAutomaticLogin(bool value);
    bool allowWeakPassword() const;
    void setAllowWeakPassword(bool value);
    bool encryptHome() const;
    void setEncryptHome(bool value);
    bool encryptVarLib() const;
    void setEncryptVarLib(bool value);
    bool acknowledgeDiskErasure() const;
    void setAcknowledgeDiskErasure(bool value);

    bool planValid() const;
    QStringList validationErrors() const;
    bool validatedPlanAvailable() const;
    bool storagePreflightBusy() const;
    bool storagePreflightComplete() const;
    QStringList storagePreflightErrors() const;
    QVariantList storageLayout() const;
    qulonglong targetDiskBytes() const;
    qulonglong logicalBlockSize() const;
    const std::optional<InstallationPlan>& validatedPlan() const;
    QVariantMap validatedPlanMap() const;

    Q_INVOKABLE void setUserPassword(const QString& value);
    Q_INVOKABLE void setPasswordConfirmation(const QString& value);
    Q_INVOKABLE void setEncryptionPassphrase(const QString& value);
    Q_INVOKABLE void setEncryptionPassphraseConfirmation(const QString& value);
    Q_INVOKABLE bool validateAndBuildPlan();
    Q_INVOKABLE void runStoragePreflight();
    void acceptStoragePreflightResult(const StoragePreflightResult& result);

signals:
    void planChanged();
    void choicesChanged();

private:
    void updateValidation();
    void invalidatePlan();
    void invalidateStoragePreflight();
    QString storagePreflightFingerprint() const;

    bool m_debugMode = false;
    QStringList m_availableLanguages;
    QStringList m_availableLocales;
    QStringList m_availableTimeZones;
    QStringList m_availableKeyboardLayouts;
    QStringList m_availableTargetDisks;
    QString m_imagePath;
    QString m_imageFormat;
    bool m_imageVerified = false;
    bool m_uefiEnvironmentDetected = false;

    QString m_language;
    QString m_locale;
    QString m_timeZone;
    QString m_keyboardLayout;
    QString m_keyboardVariant;
    QString m_keyboardModel;
    QString m_keyboardOptions;
    QString m_fullName;
    QString m_username;
    QString m_hostname;
    QString m_targetDisk;
    QString m_partitioningMode = QStringLiteral("automated");
    QString m_swapChoice = QStringLiteral("Small");
    QString m_userPassword;
    QString m_passwordConfirmation;
    QString m_encryptionPassphrase;
    QString m_encryptionPassphraseConfirmation;
    bool m_acknowledgeDiskErasure = false;
    bool m_automaticLogin = false;
    bool m_allowWeakPassword = false;
    bool m_encryptHome = true;
    bool m_encryptVarLib = true;
    QStringList m_validationErrors;
    bool m_storagePreflightBusy = false;
    bool m_storagePreflightComplete = false;
    QStringList m_storagePreflightErrors;
    QVariantList m_storageLayout;
    quint64 m_targetDiskBytes = 0;
    quint64 m_logicalBlockSize = 0;
    quint64 m_preflightGeneration = 0;
    std::optional<InstallationPlan> m_validatedPlan;
};
