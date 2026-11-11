// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

struct SystemDiscoveryResults {
    bool uefiEnvironmentDetected = false;
    QString imagePath;
    QString imageFormat;
    QStringList languages;
    QStringList locales;
    QStringList timeZones;
    QStringList keyboardLayouts;
    QVariantList keyboardLayoutOptions;
    QVariantMap keyboardVariantOptionsByLayout;
    QVariantList keyboardModelOptions;
    QVariantList keyboardOptionOptions;
    QStringList targetDisks;
    QString defaultLanguage;
    QString defaultLocale;
    QString defaultTimeZone;
    QString defaultKeyboardLayout;
};

struct StoragePreflightResult {
    bool valid = false;
    QStringList errors;
    QVariantList layout;
    quint64 diskBytes = 0;
    quint64 logicalBlockSize = 0;
    quint64 imageRootBytes = 0;
    quint64 imageHomeBytes = 0;
    quint64 imageVarLibBytes = 0;
};

class SystemDiscovery final
{
public:
    static SystemDiscoveryResults discover();
    static bool isSuitableTargetDisk(const QString& devicePath, const QString& imagePath = {});
    static QVariantMap inspectTargetDisk(const QString& devicePath, const QString& imagePath = {});
    static StoragePreflightResult preflightStorage(const QString& devicePath,
                                                   const QString& imagePath,
                                                   const QString& imageFormat,
                                                   const QString& swapChoice,
                                                   bool encryptRoot,
                                                   bool encryptHome,
                                                   bool encryptVarLib,
                                                   bool encryptSwap,
                                                   const QString& partitioningMode,
                                                   const QVariantMap& manualAssignments,
                                                   const QVariantList& manualPartitionLayout,
                                                   const QVariantMap& partitionLabels,
                                                   const QVariantMap& partitionSizesMiB,
                                                   quint64 sharedSpaceMiB,
                                                   quint64 sharedSpaceStartSector,
                                                   const QString& sharedResizeDevice);
};
