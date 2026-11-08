// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "SystemDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <limits>

namespace {

constexpr quint64 MiB = 1024ULL * 1024;
constexpr quint64 GiB = 1024ULL * MiB;
constexpr quint64 Alignment = MiB;

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

quint64 alignUp(quint64 value, quint64 alignment)
{
    if (alignment == 0 || value > std::numeric_limits<quint64>::max() - alignment + 1)
        return 0;
    return ((value + alignment - 1) / alignment) * alignment;
}

quint64 alignDown(quint64 value, quint64 alignment)
{
    return alignment == 0 ? 0 : (value / alignment) * alignment;
}

bool imageUsage(const QString& path, const QString& format,
                quint64* rootBytes, quint64* homeBytes, quint64* varLibBytes,
                QString* error)
{
    QString executable;
    QStringList arguments;
    if (format == QStringLiteral("SquashFS")) {
        executable = QStandardPaths::findExecutable(QStringLiteral("unsquashfs"));
        arguments = {QStringLiteral("-lln"), QStringLiteral("-no-progress"),
                     QStringLiteral("-d"), QString(), path};
    } else if (format == QStringLiteral("DwarFS")) {
        executable = QStandardPaths::findExecutable(QStringLiteral("dwarfsck"));
        arguments = {QStringLiteral("--no-check"), QStringLiteral("--list"),
                     QStringLiteral("--verbose"), path};
    }
    if (executable.isEmpty()) {
        *error = QObject::tr("The read-only image listing tool for %1 is unavailable.").arg(format);
        return false;
    }

    quint64 totals[3] = {};
    quint64 regularFiles[3] = {};
    QByteArray pendingOutput;
    static const QRegularExpression entryPattern(
        QStringLiteral("^(\\S+)\\s+\\S+\\s+(\\d+)\\s+\\S+\\s+\\S+\\s+(.+)$"));
    const auto consumeLine = [&](const QByteArray& rawLine) -> bool {
        const QString line = QString::fromLocal8Bit(rawLine).trimmed();
        const auto match = entryPattern.match(line);
        if (!match.hasMatch() || !match.captured(1).startsWith(QLatin1Char('-')))
            return true;
        bool sizeOk = false;
        const quint64 size = match.captured(2).toULongLong(&sizeOk);
        if (!sizeOk) {
            *error = QObject::tr("The installation image contains an invalid or oversized file entry.");
            return false;
        }
        QString entryPath = match.captured(3).trimmed();
        while (entryPath.startsWith(QStringLiteral("./")) || entryPath.startsWith(QLatin1Char('/')))
            entryPath.remove(0, entryPath.startsWith(QStringLiteral("./")) ? 2 : 1);
        int bucket = 0;
        if (entryPath == QStringLiteral("home") || entryPath.startsWith(QStringLiteral("home/")))
            bucket = 1;
        else if (entryPath == QStringLiteral("var/lib") || entryPath.startsWith(QStringLiteral("var/lib/")))
            bucket = 2;
        if (size > std::numeric_limits<quint64>::max() - totals[bucket]) {
            *error = QObject::tr("The installation image size exceeds supported limits.");
            return false;
        }
        totals[bucket] += size;
        ++regularFiles[bucket];
        return true;
    };
    QProcess process;
    const auto consumeOutput = [&]() -> bool {
        pendingOutput += process.readAllStandardOutput();
        qsizetype newline = -1;
        while ((newline = pendingOutput.indexOf('\n')) >= 0) {
            const QByteArray line = pendingOutput.left(newline);
            pendingOutput.remove(0, newline + 1);
            if (!consumeLine(line))
                return false;
        }
        return true;
    };

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted()) {
        *error = QObject::tr("The installation image could not be inspected.");
        return false;
    }
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        if (!consumeOutput()) {
            process.kill();
            process.waitForFinished();
            return false;
        }
    }
    if (!consumeOutput())
        return false;
    if (!pendingOutput.isEmpty() && !consumeLine(pendingOutput))
        return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QObject::tr("The installation image listing failed: %1")
                     .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return false;
    }
    if (regularFiles[0] + regularFiles[1] + regularFiles[2] == 0) {
        *error = QObject::tr("The installation image listing did not contain any readable file entries.");
        return false;
    }

    // Include per-file allocation overhead and a 10% growth margin. Sparse files and
    // hard links are deliberately counted by logical size, which errs on the safe side.
    for (int index = 0; index < 3; ++index) {
        const quint64 metadataAllowance = regularFiles[index] > std::numeric_limits<quint64>::max() / 4096
            ? std::numeric_limits<quint64>::max() : regularFiles[index] * 4096;
        const quint64 withMetadata = totals[index] > std::numeric_limits<quint64>::max() - metadataAllowance
            ? std::numeric_limits<quint64>::max() : totals[index] + metadataAllowance;
        const quint64 growthAllowance = withMetadata / 10 + (withMetadata % 10 != 0);
        totals[index] = withMetadata > std::numeric_limits<quint64>::max() - growthAllowance
            ? std::numeric_limits<quint64>::max() : withMetadata + growthAllowance;
    }
    *rootBytes = totals[0];
    *homeBytes = totals[1];
    *varLibBytes = totals[2];
    return true;
}

quint64 ramBytes()
{
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    QTextStream stream(&meminfo);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (!line.startsWith(QStringLiteral("MemTotal:")))
            continue;
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        bool ok = false;
        const quint64 kibibytes = fields.size() > 1 ? fields.at(1).toULongLong(&ok) : 0;
        return ok && kibibytes <= std::numeric_limits<quint64>::max() / 1024
            ? kibibytes * 1024 : 0;
    }
    return 0;
}

bool kernelModuleAvailable(const QString& moduleName)
{
    QString sysfsModuleName = moduleName;
    sysfsModuleName.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (QFileInfo::exists(QStringLiteral("/sys/module/%1").arg(sysfsModuleName)))
        return true;
    const QString moduleRoot = QStringLiteral("/lib/modules/%1/kernel")
                                   .arg(readText(QStringLiteral("/proc/sys/kernel/osrelease")));
    if (!QDir(moduleRoot).exists())
        return false;
    QDirIterator it(moduleRoot, {moduleName + QStringLiteral(".ko"),
                                 moduleName + QStringLiteral(".ko.*")},
                    QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

bool kernelSupports(const QString& filesystem, const QString& moduleName)
{
    const QStringList lines = readText(QStringLiteral("/proc/filesystems"))
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (line.section(QLatin1Char(' '), -1) == filesystem)
            return true;
    }
    return kernelModuleAvailable(moduleName);
}

bool f2fsZstdCompressionAvailable()
{
    const QString release = readText(QStringLiteral("/proc/sys/kernel/osrelease"));
    const QStringList configPaths = {
        QStringLiteral("/boot/config-%1").arg(release),
        QStringLiteral("/usr/lib/modules/%1/config").arg(release)
    };
    const QRegularExpression enabled(QStringLiteral("^CONFIG_F2FS_FS_ZSTD=(y|m)$"));
    for (const QString& path : configPaths) {
        QFile config(path);
        if (!config.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream stream(&config);
        while (!stream.atEnd()) {
            if (enabled.match(stream.readLine().trimmed()).hasMatch())
                return true;
        }
        return false;
    }

    const QString gzip = QStandardPaths::findExecutable(QStringLiteral("gzip"));
    if (gzip.isEmpty() || !QFileInfo::exists(QStringLiteral("/proc/config.gz")))
        return false;
    QProcess process;
    process.setProgram(gzip);
    process.setArguments({QStringLiteral("-dc"), QStringLiteral("/proc/config.gz")});
    process.start();
    if (!process.waitForStarted() || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return false;
    const QString config = QString::fromLocal8Bit(process.readAllStandardOutput());
    for (const QString& line : config.split(QLatin1Char('\n'))) {
        if (enabled.match(line.trimmed()).hasMatch())
            return true;
    }
    return false;
}

void addMissingTool(QStringList* errors, const QString& tool, const QString& purpose)
{
    if (QStandardPaths::findExecutable(tool).isEmpty())
        errors->append(QObject::tr("Required tool %1 (%2) is unavailable.").arg(tool, purpose));
}

QVariantMap partitionRow(int partitionNumber, const QString& typeGuid,
                         const QString& label, const QString& mountPoint,
                         const QString& filesystem, quint64 startBytes,
                         quint64 sizeBytes, quint64 logicalBlockSize,
                         quint64 estimatedImageBytes, quint64 minimumBytes,
                         bool encrypted, bool compressed, const QString& note = {})
{
    const quint64 firstSector = startBytes / logicalBlockSize;
    const quint64 sectorCount = sizeBytes / logicalBlockSize;
    return {
        {QStringLiteral("partitionNumber"), partitionNumber},
        {QStringLiteral("typeGuid"), typeGuid},
        {QStringLiteral("label"), label},
        {QStringLiteral("mountPoint"), mountPoint},
        {QStringLiteral("filesystem"), filesystem},
        {QStringLiteral("startSector"), QVariant::fromValue<qulonglong>(firstSector)},
        {QStringLiteral("sectorCount"), QVariant::fromValue<qulonglong>(sectorCount)},
        {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(sizeBytes)},
        {QStringLiteral("estimatedImageBytes"), QVariant::fromValue<qulonglong>(estimatedImageBytes)},
        {QStringLiteral("minimumBytes"), QVariant::fromValue<qulonglong>(minimumBytes)},
        {QStringLiteral("encrypted"), encrypted},
        {QStringLiteral("compressed"), compressed},
        {QStringLiteral("note"), note}
    };
}

} // namespace

StoragePreflightResult SystemDiscovery::preflightStorage(const QString& devicePath,
                                                         const QString& imagePath,
                                                         const QString& imageFormat,
                                                         const QString& swapChoice,
                                                         bool encryptHome,
                                                         bool encryptVarLib)
{
    StoragePreflightResult result;
    QString imageError;
    if (!imageUsage(imagePath, imageFormat, &result.imageRootBytes,
                    &result.imageHomeBytes, &result.imageVarLibBytes, &imageError)) {
        result.errors.append(imageError);
        return result;
    }

    if (!isSuitableTargetDisk(devicePath, imagePath)) {
        result.errors.append(QObject::tr("The selected disk is no longer a writable, unmounted whole disk."));
        return result;
    }

    const QString deviceName = QFileInfo(QFileInfo(devicePath).canonicalFilePath()).fileName();
    const QString blockPath = QStringLiteral("/sys/class/block/%1").arg(deviceName);
    bool sectorsOk = false;
    bool logicalOk = false;
    const quint64 sectors512 = readText(QStringLiteral("%1/size").arg(blockPath)).toULongLong(&sectorsOk);
    result.logicalBlockSize = readText(QStringLiteral("%1/queue/logical_block_size").arg(blockPath))
                                  .toULongLong(&logicalOk);
    if (!sectorsOk || !logicalOk || result.logicalBlockSize < 512
        || result.logicalBlockSize > 1024 * 1024 || sectors512 > std::numeric_limits<quint64>::max() / 512) {
        result.errors.append(QObject::tr("The selected disk geometry could not be read safely."));
        return result;
    }
    result.diskBytes = sectors512 * 512;
    if (result.diskBytes % result.logicalBlockSize != 0) {
        result.errors.append(QObject::tr("The disk capacity is not aligned to its logical sector size."));
        return result;
    }

    addMissingTool(&result.errors, QStringLiteral("sfdisk"), QObject::tr("GPT partitioning"));
    addMissingTool(&result.errors, QStringLiteral("mount"), QObject::tr("filesystem mounting"));
    addMissingTool(&result.errors, QStringLiteral("umount"), QObject::tr("filesystem unmounting"));
    addMissingTool(&result.errors, QStringLiteral("chroot"), QObject::tr("installed-system configuration"));
    addMissingTool(&result.errors, QStringLiteral("blockdev"), QObject::tr("partition table activation"));
    addMissingTool(&result.errors, QStringLiteral("udevadm"), QObject::tr("partition device discovery"));
    addMissingTool(&result.errors, QStringLiteral("blkid"), QObject::tr("filesystem identification"));
    addMissingTool(&result.errors, QStringLiteral("efibootmgr"), QObject::tr("UEFI boot entry creation"));
    if (imageFormat == QStringLiteral("DwarFS"))
        addMissingTool(&result.errors, QStringLiteral("dwarfsextract"), QObject::tr("DwarFS image installation"));
    else
        addMissingTool(&result.errors, QStringLiteral("unsquashfs"), QObject::tr("SquashFS image installation"));
    if (QStandardPaths::findExecutable(QStringLiteral("mkfs.fat")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("mkfs.vfat")).isEmpty())
        result.errors.append(QObject::tr("Neither mkfs.fat nor mkfs.vfat is available for EFI filesystem creation."));
    if (swapChoice == QStringLiteral("File"))
        addMissingTool(&result.errors, QStringLiteral("fallocate"), QObject::tr("swap file creation"));
    addMissingTool(&result.errors, QStringLiteral("mkfs.xfs"), QObject::tr("XFS creation"));
    addMissingTool(&result.errors, QStringLiteral("mkfs.f2fs"), QObject::tr("F2FS creation"));
    if (encryptHome || encryptVarLib)
        addMissingTool(&result.errors, QStringLiteral("cryptsetup"), QObject::tr("LUKS2 encryption"));
    if (swapChoice == QStringLiteral("Small") || swapChoice == QStringLiteral("Suspend")
        || swapChoice == QStringLiteral("File")) {
        addMissingTool(&result.errors, QStringLiteral("mkswap"), QObject::tr("swap creation"));
        addMissingTool(&result.errors, QStringLiteral("swapon"), QObject::tr("swap activation"));
    }
    if (!kernelSupports(QStringLiteral("vfat"), QStringLiteral("vfat"))
        || !kernelSupports(QStringLiteral("xfs"), QStringLiteral("xfs"))
        || !kernelSupports(QStringLiteral("f2fs"), QStringLiteral("f2fs")))
        result.errors.append(QObject::tr("The live kernel lacks support for one or more required filesystems (VFAT, XFS, F2FS)."));
    if ((encryptHome || encryptVarLib) && !kernelModuleAvailable(QStringLiteral("dm-crypt")))
        result.errors.append(QObject::tr("The live kernel does not provide device-mapper encryption support."));
    if ((encryptHome || encryptVarLib)
        && !QFileInfo::exists(QStringLiteral("/dev/mapper/control")))
        result.errors.append(QObject::tr("The device-mapper control interface is unavailable."));
    const bool zstdCompressionAvailable = f2fsZstdCompressionAvailable();
    const bool kernelAdvertisesCompression = QFileInfo::exists(
        QStringLiteral("/sys/fs/f2fs/features/compression"));
    if (!kernelAdvertisesCompression && !zstdCompressionAvailable)
        result.errors.append(QObject::tr("The live kernel's F2FS compression support could not be verified."));
    else if (!zstdCompressionAvailable)
        result.errors.append(QObject::tr("The live kernel's Zstandard support for F2FS compression could not be verified."));

    const quint64 sector = result.logicalBlockSize;
    const quint64 gptReservation = sector * 34;
    const quint64 usableEnd = alignDown(result.diskBytes - gptReservation, Alignment);
    const quint64 efiStart = alignUp(2 * MiB, Alignment);
    const quint64 efiSize = 100 * MiB;
    const quint64 dataStart = alignUp(efiStart + efiSize, Alignment);
    quint64 swapSize = 0;
    if (swapChoice == QStringLiteral("Small") || swapChoice == QStringLiteral("Suspend")) {
        const quint64 memory = ramBytes();
        if (memory == 0) {
            result.errors.append(QObject::tr("Available memory could not be read to size swap safely."));
        } else if (swapChoice == QStringLiteral("Small")) {
            const quint64 suggested = memory <= 4 * GiB ? memory * 2
                : (memory <= 8 * GiB ? 8 * GiB : memory);
            const quint64 tenPercentCapacity = usableEnd > dataStart
                ? (usableEnd - dataStart) / 10 : 0;
            swapSize = alignDown(std::min({suggested, 8 * GiB, tenPercentCapacity}), Alignment);
        } else {
            swapSize = alignUp(memory + memory / 10, Alignment);
        }
    }
    if (swapChoice != QStringLiteral("None") && swapChoice != QStringLiteral("Small")
        && swapChoice != QStringLiteral("Suspend") && swapChoice != QStringLiteral("File"))
        result.errors.append(QObject::tr("The selected swap option is invalid."));

    const quint64 swapStart = usableEnd >= swapSize ? alignDown(usableEnd - swapSize, Alignment) : 0;
    const quint64 dataEnd = (swapChoice == QStringLiteral("Small") || swapChoice == QStringLiteral("Suspend"))
        ? swapStart : usableEnd;
    if (dataEnd <= dataStart) {
        result.errors.append(QObject::tr("The selected disk has no usable space for the proposed layout."));
        return result;
    }

    const quint64 dataBytes = dataEnd - dataStart;
    const quint64 rootShare = (dataBytes / 100) * 23 + (dataBytes % 100) * 23 / 100;
    const quint64 homeShare = (dataBytes / 100) * 67 + (dataBytes % 100) * 67 / 100;
    const quint64 rootSize = alignDown(rootShare, Alignment);
    const quint64 homeSize = alignDown(homeShare, Alignment);
    const quint64 varLibSize = dataBytes - rootSize - homeSize;
    const quint64 configuredRootMinimum = quint64(15) * GiB + quint64(21) * (GiB / 100);
    const quint64 imageRootMinimum = result.imageRootBytes > std::numeric_limits<quint64>::max() - 600 * MiB
        ? std::numeric_limits<quint64>::max() : result.imageRootBytes + 600 * MiB;
    const quint64 rootRequired = std::max(configuredRootMinimum, imageRootMinimum);
    const quint64 homeRequired = result.imageHomeBytes > std::numeric_limits<quint64>::max() - GiB
        ? std::numeric_limits<quint64>::max() : result.imageHomeBytes + GiB;
    const quint64 varLibRequired = result.imageVarLibBytes > std::numeric_limits<quint64>::max() - GiB
        ? std::numeric_limits<quint64>::max() : result.imageVarLibBytes + GiB;
    if (rootSize < rootRequired)
        result.errors.append(QObject::tr("The proposed root partition is smaller than the image's estimated requirement (%1 GiB needed).")
                                 .arg(double(rootRequired) / GiB, 0, 'f', 2));
    if (homeSize < homeRequired)
        result.errors.append(QObject::tr("The proposed /home partition is smaller than the image's estimated requirement (%1 GiB needed).")
                                 .arg(double(homeRequired) / GiB, 0, 'f', 2));
    if (varLibSize < varLibRequired)
        result.errors.append(QObject::tr("The proposed /var/lib partition is smaller than the image's estimated requirement (%1 GiB needed).")
                                 .arg(double(varLibRequired) / GiB, 0, 'f', 2));
    if (swapChoice == QStringLiteral("File")) {
        const quint64 fileSwapRequired = rootRequired > std::numeric_limits<quint64>::max() - 512 * MiB
            ? std::numeric_limits<quint64>::max() : rootRequired + 512 * MiB;
        if (rootSize < fileSwapRequired)
            result.errors.append(QObject::tr("The root partition lacks the configured 512 MiB swap-file allowance."));
    }
    if (swapChoice == QStringLiteral("Small") && swapSize < Alignment)
        result.errors.append(QObject::tr("The selected disk is too small to allocate the configured small swap partition."));

    if (result.logicalBlockSize > Alignment || Alignment % sector != 0
        || dataStart % sector != 0 || rootSize % sector != 0 || homeSize % sector != 0
        || varLibSize % sector != 0 || efiStart % sector != 0 || efiSize % sector != 0)
        result.errors.append(QObject::tr("The proposed partition boundaries do not align to the disk's logical sectors."));

    const QString linuxFilesystemGuid = QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4");
    result.layout.append(partitionRow(1, QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b"),
                                      QStringLiteral("ESP"), QStringLiteral("/boot/efi"),
                                      QStringLiteral("vfat"), efiStart, efiSize, sector,
                                      0, 0, false, false));
    result.layout.append(partitionRow(2, linuxFilesystemGuid, QStringLiteral("NX_ROOT"),
                                      QStringLiteral("/"), QStringLiteral("xfs"),
                                      dataStart, rootSize, sector, result.imageRootBytes,
                                      rootRequired, false, false));
    const QString luksGuid = QStringLiteral("ca7d7ccb-63ed-4c53-861c-1742536059cc");
    result.layout.append(partitionRow(3, encryptHome ? luksGuid : linuxFilesystemGuid, QStringLiteral("NX_HOME"),
                                      QStringLiteral("/home"),
                                      encryptHome ? QStringLiteral("LUKS2 + F2FS") : QStringLiteral("f2fs"),
                                      dataStart + rootSize, homeSize, sector, result.imageHomeBytes,
                                      homeRequired, encryptHome, true));
    result.layout.append(partitionRow(4, encryptVarLib ? luksGuid : linuxFilesystemGuid, QStringLiteral("NX_VAR_LIB"),
                                      QStringLiteral("/var/lib"),
                                      encryptVarLib ? QStringLiteral("LUKS2 + F2FS") : QStringLiteral("f2fs"),
                                      dataStart + rootSize + homeSize, varLibSize, sector, result.imageVarLibBytes,
                                      varLibRequired, encryptVarLib, true));
    if (swapSize > 0)
        result.layout.append(partitionRow(5, QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"),
                                          QStringLiteral("SWAP"), QStringLiteral("swap"),
                                          QStringLiteral("swap"), swapStart, swapSize, sector,
                                          0, 0, false, false));
    if (swapChoice == QStringLiteral("File")) {
        result.layout.append(QVariantMap{
            {QStringLiteral("partitionNumber"), 0},
            {QStringLiteral("typeGuid"), QString()},
            {QStringLiteral("label"), QStringLiteral("swapfile")},
            {QStringLiteral("mountPoint"), QStringLiteral("swap")},
            {QStringLiteral("filesystem"), QStringLiteral("file (512 MiB)")},
            {QStringLiteral("startSector"), QVariant::fromValue<qulonglong>(0)},
            {QStringLiteral("sectorCount"), QVariant::fromValue<qulonglong>(0)},
            {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(512 * MiB)},
            {QStringLiteral("estimatedImageBytes"), QVariant::fromValue<qulonglong>(0)},
            {QStringLiteral("minimumBytes"), QVariant::fromValue<qulonglong>(0)},
            {QStringLiteral("encrypted"), false},
            {QStringLiteral("compressed"), false},
            {QStringLiteral("note"), QObject::tr("Created within the root filesystem.")}
        });
    }

    result.valid = result.errors.isEmpty();
    return result;
}
