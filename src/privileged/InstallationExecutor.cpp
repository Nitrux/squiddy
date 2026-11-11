// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "InstallationExecutor.h"

#include "../SystemDiscovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantMap>

#include <algorithm>
#include <limits>
#include <unistd.h>

namespace {

struct MountRecord {
    QString target;
};

bool runPath(const QString& name, const QStringList& arguments, const QByteArray& input,
             QString* output, QString* error);

struct CryptMapperGuard {
    QStringList opened;

    bool close(QString* error)
    {
        while (!opened.isEmpty()) {
            const QString name = opened.constLast();
            if (!runPath(QStringLiteral("cryptsetup"), {QStringLiteral("close"), name}, {}, nullptr, error))
                return false;
            opened.removeLast();
        }
        return true;
    }

    ~CryptMapperGuard()
    {
        close(nullptr);
    }
};

bool run(const QString& program, const QStringList& arguments, const QByteArray& input,
         QString* output, QString* error)
{
    QString localError;
    if (!error)
        error = &localError;
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted()) {
        *error = QObject::tr("Could not start %1.").arg(QFileInfo(program).fileName());
        return false;
    }
    if (!input.isEmpty()) {
        process.write(input);
        process.closeWriteChannel();
    }
    if (!process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished();
        *error = QObject::tr("%1 did not finish.").arg(QFileInfo(program).fileName());
        return false;
    }
    const QByteArray stdoutData = process.readAllStandardOutput();
    const QByteArray stderrData = process.readAllStandardError();
    if (output)
        *output = QString::fromLocal8Bit(stdoutData).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromLocal8Bit(stderrData).trimmed();
        *error = detail.isEmpty()
            ? QObject::tr("%1 failed with exit code %2.").arg(QFileInfo(program).fileName()).arg(process.exitCode())
            : QObject::tr("%1 failed: %2").arg(QFileInfo(program).fileName(), detail);
        return false;
    }
    return true;
}

bool runPath(const QString& name, const QStringList& arguments, const QByteArray& input,
             QString* output, QString* error)
{
    QString localError;
    if (!error)
        error = &localError;
    const QString executable = QStandardPaths::findExecutable(name);
    if (executable.isEmpty()) {
        *error = QObject::tr("Required program %1 is unavailable.").arg(name);
        return false;
    }
    return run(executable, arguments, input, output, error);
}

bool writeFile(const QString& path, const QByteArray& contents, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QObject::tr("Could not write %1.").arg(path);
        return false;
    }
    const QFileInfo previous(path);
    file.setPermissions(previous.exists() ? previous.permissions()
        : QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    if (file.write(contents) != contents.size() || !file.commit()) {
        *error = QObject::tr("Could not write %1.").arg(path);
        return false;
    }
    return true;
}

bool chrootRun(const QString& root, const QStringList& command,
               const QByteArray& input, QString* error, QString* output = nullptr)
{
    QStringList arguments{root};
    arguments.append(command);
    return runPath(QStringLiteral("chroot"), arguments, input, output, error);
}

QString uuidFor(const QString& device, QString* error)
{
    QString uuid;
    if (!runPath(QStringLiteral("blkid"), {QStringLiteral("-s"), QStringLiteral("UUID"),
                 QStringLiteral("-o"), QStringLiteral("value"), device}, {}, &uuid, error))
        return {};
    if (uuid.isEmpty())
        *error = QObject::tr("No filesystem UUID was found for %1.").arg(device);
    return uuid;
}

bool mountDevice(const QString& device, const QString& target, const QStringList& options,
                 QList<MountRecord>* mounts, QString* error)
{
    if (!QDir().mkpath(target)) {
        *error = QObject::tr("Could not create mount point %1.").arg(target);
        return false;
    }
    QStringList args;
    if (!options.isEmpty())
        args << QStringLiteral("-o") << options.join(QLatin1Char(','));
    args << device << target;
    if (!runPath(QStringLiteral("mount"), args, {}, nullptr, error))
        return false;
    mounts->append({target});
    return true;
}

bool mountBind(const QString& source, const QString& target, QList<MountRecord>* mounts, QString* error)
{
    if (!QDir().mkpath(target)) {
        *error = QObject::tr("Could not create system mount point %1.").arg(target);
        return false;
    }
    if (!runPath(QStringLiteral("mount"), {QStringLiteral("--bind"), source, target}, {}, nullptr, error))
        return false;
    mounts->append({target});
    return true;
}

bool mountTmpfs(const QString& target, QList<MountRecord>* mounts, QString* error)
{
    if (!QDir().mkpath(target)) {
        *error = QObject::tr("Could not create system mount point %1.").arg(target);
        return false;
    }
    if (!runPath(QStringLiteral("mount"), {QStringLiteral("-t"), QStringLiteral("tmpfs"),
            QStringLiteral("tmpfs"), target}, {}, nullptr, error)) return false;
    mounts->append({target});
    return true;
}

bool unmountAll(QList<MountRecord>* mounts, QString* error)
{
    bool success = true;
    for (auto it = mounts->crbegin(); it != mounts->crend(); ++it) {
        QString unmountError;
        if (!runPath(QStringLiteral("umount"), {it->target}, {}, nullptr, &unmountError)) {
            success = false;
            if (error && error->isEmpty()) *error = unmountError;
        }
    }
    mounts->clear();
    return success;
}

QVariantMap rowForRole(const InstallationPlan& plan, const QString& role)
{
    for (const QVariant& value : plan.storageLayout) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("mountPoint")).toString() == role
            && !row.value(QStringLiteral("device")).toString().isEmpty())
            return row;
    }
    return {};
}

QString deviceForRole(const InstallationPlan& plan, const QString& role)
{
    return rowForRole(plan, role).value(QStringLiteral("device")).toString();
}

QString labelForRole(const InstallationPlan& plan, const QString& role)
{
    return rowForRole(plan, role).value(QStringLiteral("label")).toString();
}

bool runAllowE2fsckRepair(const QString& device, QString* error)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("e2fsck"));
    if (executable.isEmpty()) {
        *error = QObject::tr("Required program e2fsck is unavailable.");
        return false;
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-f"), QStringLiteral("-p"), device});
    process.start();
    if (!process.waitForStarted() || !process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished();
        *error = QObject::tr("Could not check the filesystem on %1.").arg(device);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() > 1) {
        const QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        *error = detail.isEmpty() ? QObject::tr("Filesystem check failed for %1.").arg(device) : detail;
        return false;
    }
    return true;
}

bool extFilesystemSizeBytes(const QString& device, quint64* sizeBytes, QString* error)
{
    QString details;
    if (!runPath(QStringLiteral("dumpe2fs"),
            {QStringLiteral("-h"), device}, {}, &details, error))
        return false;
    static const QRegularExpression blockCountPattern(
        QStringLiteral("(?m)^Block count:\\s*(\\d+)\\s*$"));
    static const QRegularExpression blockSizePattern(
        QStringLiteral("(?m)^Block size:\\s*(\\d+)\\s*$"));
    const auto countMatch = blockCountPattern.match(details);
    const auto sizeMatch = blockSizePattern.match(details);
    bool countOk = false;
    bool blockSizeOk = false;
    const quint64 blocks = countMatch.captured(1).toULongLong(&countOk);
    const quint64 blockSize = sizeMatch.captured(1).toULongLong(&blockSizeOk);
    if (!countOk || !blockSizeOk || blocks == 0 || blockSize == 0
        || blocks > std::numeric_limits<quint64>::max() / blockSize) {
        *error = QObject::tr("Could not read the ext filesystem size on %1.").arg(device);
        return false;
    }
    *sizeBytes = blocks * blockSize;
    return true;
}

bool shrinkSharedFilesystem(const QVariantMap& resizeRow, QString* error)
{
    const QString device = resizeRow.value(QStringLiteral("device")).toString();
    const QString filesystem = resizeRow.value(QStringLiteral("filesystem")).toString();
    const quint64 newSizeBytes = resizeRow.value(QStringLiteral("sizeBytes")).toULongLong();
    constexpr quint64 filesystemMargin = 64ULL * 1024 * 1024;
    if (newSizeBytes <= filesystemMargin) {
        *error = QObject::tr("The resized partition is too small for filesystem metadata.");
        return false;
    }
    const quint64 targetFilesystemMiB = (newSizeBytes - filesystemMargin) / (1024ULL * 1024);
    if (filesystem == QStringLiteral("ext2") || filesystem == QStringLiteral("ext3")
        || filesystem == QStringLiteral("ext4")) {
        if (!runAllowE2fsckRepair(device, error))
            return false;
        quint64 currentFilesystemBytes = 0;
        if (!extFilesystemSizeBytes(device, &currentFilesystemBytes, error))
            return false;
        if (currentFilesystemBytes > targetFilesystemMiB * (1024ULL * 1024)
            && !runPath(QStringLiteral("resize2fs"),
                {device, QStringLiteral("%1M").arg(targetFilesystemMiB)}, {}, nullptr, error))
            return false;
        return runAllowE2fsckRepair(device, error);
    }
    if (filesystem == QStringLiteral("ntfs")) {
        const QString size = QStringLiteral("%1Mi").arg(targetFilesystemMiB);
        const QStringList checkArguments{QStringLiteral("--no-action"), QStringLiteral("--size"), size, device};
        if (!runPath(QStringLiteral("ntfsresize"), checkArguments, {}, nullptr, error))
            return false;
        return runPath(QStringLiteral("ntfsresize"),
            {QStringLiteral("--size"), size, device}, {}, nullptr, error);
    }
    if (filesystem == QStringLiteral("vfat")) {
        if (targetFilesystemMiB < 512) {
            *error = QObject::tr("FAT32 partitions cannot be shrunk below 512 MiB.");
            return false;
        }
        return runPath(QStringLiteral("fatresize"),
            {QStringLiteral("--size"), QStringLiteral("%1Mi").arg(targetFilesystemMiB), device},
            {}, nullptr, error);
    }
    if (filesystem == QStringLiteral("swap"))
        return true;
    if (filesystem != QStringLiteral("btrfs")) {
        *error = QObject::tr("This existing filesystem cannot be resized by Squiddy.");
        return false;
    }

    QTemporaryDir mountPoint(QStringLiteral("/run/squiddy-resize-XXXXXX"));
    if (!mountPoint.isValid()) {
        *error = QObject::tr("Could not create a temporary filesystem mount point.");
        return false;
    }
    if (!runPath(QStringLiteral("mount"), {device, mountPoint.path()}, {}, nullptr, error))
        return false;
    QString showOutput;
    bool success = runPath(QStringLiteral("btrfs"),
        {QStringLiteral("filesystem"), QStringLiteral("show"), QStringLiteral("--raw"), mountPoint.path()},
        {}, &showOutput, error);
    static const QRegularExpression singleDevice(QStringLiteral("(?m)^\\s*Total devices\\s+1\\s*$"));
    static const QRegularExpression deviceSizePattern(
        QStringLiteral("(?m)^\\s*devid\\s+1\\s+size\\s+(\\d+)\\s+used\\s+"));
    const auto deviceSizeMatch = deviceSizePattern.match(showOutput);
    bool deviceSizeOk = false;
    const quint64 deviceSize = deviceSizeMatch.captured(1).toULongLong(&deviceSizeOk);
    if (success && (!singleDevice.match(showOutput).hasMatch() || !deviceSizeOk)) {
        *error = QObject::tr("Only a verified single-device Btrfs filesystem can be shrunk by Squiddy.");
        success = false;
    }
    if (success && deviceSize > targetFilesystemMiB * (1024ULL * 1024)) {
        success = runPath(QStringLiteral("btrfs"),
            {QStringLiteral("filesystem"), QStringLiteral("resize"),
             QStringLiteral("%1M").arg(targetFilesystemMiB), mountPoint.path()}, {}, nullptr, error);
    }
    QString unmountError;
    if (!runPath(QStringLiteral("umount"), {mountPoint.path()}, {}, nullptr, &unmountError)) {
        if (success)
            *error = unmountError;
        success = false;
    }
    return success;
}

bool growManualFilesystem(const QVariantMap& resizeRow, QString* error)
{
    const QString device = resizeRow.value(QStringLiteral("device")).toString();
    const QString filesystem = resizeRow.value(QStringLiteral("originalFilesystem")).toString();
    const quint64 sizeBytes = resizeRow.value(QStringLiteral("sizeBytes")).toULongLong();
    if (sizeBytes <= 64ULL * 1024 * 1024) {
        *error = QObject::tr("The resized partition is too small for filesystem metadata.");
        return false;
    }
    const quint64 targetMiB = (sizeBytes - 64ULL * 1024 * 1024) / (1024ULL * 1024);
    if (filesystem == QStringLiteral("ext2") || filesystem == QStringLiteral("ext3")
        || filesystem == QStringLiteral("ext4")) {
        return runAllowE2fsckRepair(device, error)
            && runPath(QStringLiteral("resize2fs"), {device}, {}, nullptr, error)
            && runAllowE2fsckRepair(device, error);
    }
    if (filesystem == QStringLiteral("ntfs")) {
        const QString size = QStringLiteral("%1Mi").arg(targetMiB);
        return runPath(QStringLiteral("ntfsresize"),
            {QStringLiteral("--no-action"), QStringLiteral("--size"), size, device}, {}, nullptr, error)
            && runPath(QStringLiteral("ntfsresize"),
                {QStringLiteral("--size"), size, device}, {}, nullptr, error);
    }
    if (filesystem == QStringLiteral("vfat"))
        return runPath(QStringLiteral("fatresize"),
            {QStringLiteral("--size"), QStringLiteral("%1Mi").arg(targetMiB), device}, {}, nullptr, error);
    if (filesystem == QStringLiteral("swap"))
        return true;
    if (filesystem != QStringLiteral("btrfs")) {
        *error = QObject::tr("Squiddy cannot grow the existing %1 filesystem.").arg(filesystem);
        return false;
    }
    QTemporaryDir mountPoint(QStringLiteral("/run/squiddy-grow-XXXXXX"));
    if (!mountPoint.isValid()) {
        *error = QObject::tr("Could not create a temporary filesystem mount point.");
        return false;
    }
    if (!runPath(QStringLiteral("mount"), {device, mountPoint.path()}, {}, nullptr, error))
        return false;
    QString output;
    bool success = runPath(QStringLiteral("btrfs"),
        {QStringLiteral("filesystem"), QStringLiteral("show"), QStringLiteral("--raw"), mountPoint.path()},
        {}, &output, error);
    static const QRegularExpression singleDevice(QStringLiteral("(?m)^\\s*Total devices\\s+1\\s*$"));
    if (success && !singleDevice.match(output).hasMatch()) {
        *error = QObject::tr("Only a single-device Btrfs filesystem can be grown by Squiddy.");
        success = false;
    }
    if (success)
        success = runPath(QStringLiteral("btrfs"),
            {QStringLiteral("filesystem"), QStringLiteral("resize"), QStringLiteral("max"), mountPoint.path()},
            {}, nullptr, error);
    QString unmountError;
    if (!runPath(QStringLiteral("umount"), {mountPoint.path()}, {}, nullptr, &unmountError)) {
        if (success) *error = unmountError;
        success = false;
    }
    return success;
}

bool shrinkSharedPartition(const InstallationPlan& plan, const QVariantMap& resizeRow, QString* error)
{
    const QString disk = plan.targetDevice;
    const QString device = resizeRow.value(QStringLiteral("device")).toString();
    const QVariantMap info = SystemDiscovery::inspectTargetDisk(disk, plan.imagePath);
    QVariantMap currentPartition;
    for (const QVariant& value : info.value(QStringLiteral("partitions")).toList()) {
        const QVariantMap partition = value.toMap();
        if (partition.value(QStringLiteral("device")).toString() == device) {
            currentPartition = partition;
            break;
        }
    }
    if (currentPartition.isEmpty()
        || currentPartition.value(QStringLiteral("sizeBytes")).toULongLong()
            != resizeRow.value(QStringLiteral("originalSizeBytes")).toULongLong()
        || currentPartition.value(QStringLiteral("startSector")).toULongLong()
            != resizeRow.value(QStringLiteral("startSector")).toULongLong()
        || currentPartition.value(QStringLiteral("filesystem")).toString()
            != resizeRow.value(QStringLiteral("filesystem")).toString()) {
        *error = QObject::tr("The selected partition changed before it could be resized.");
        return false;
    }
    const quint64 reclaimBytes = resizeRow.value(QStringLiteral("reclaimBytes")).toULongLong();
    if (reclaimBytes == 0)
        return true;
    if (!shrinkSharedFilesystem(resizeRow, error))
        return false;

    const int partitionNumber = resizeRow.value(QStringLiteral("partitionNumber")).toInt();
    const quint64 sectors = resizeRow.value(QStringLiteral("sectorCount")).toULongLong();
    const QByteArray script = "size=" + QByteArray::number(sectors) + "\n";
    if (!runPath(QStringLiteral("sfdisk"),
            {disk, QStringLiteral("--no-reread"), QStringLiteral("--force"),
             QStringLiteral("--partno"), QString::number(partitionNumber)},
            script, nullptr, error))
        return false;
    return runPath(QStringLiteral("blockdev"), {QStringLiteral("--rereadpt"), disk}, {}, nullptr, error)
        && runPath(QStringLiteral("udevadm"), {QStringLiteral("settle")}, {}, nullptr, error);
}

bool applyManualPartitionLayout(const InstallationPlan& plan, QString* error)
{
    const QVariantMap current = SystemDiscovery::inspectTargetDisk(plan.targetDevice, plan.imagePath);
    if (plan.manualPartitionLayout.isEmpty()) {
        *error = QObject::tr("The staged manual partition table is unavailable.");
        return false;
    }
    const QString tableId = current.value(QStringLiteral("tableId")).toString();
    if (!current.value(QStringLiteral("gpt")).toBool()
        && std::any_of(plan.manualPartitionLayout.cbegin(), plan.manualPartitionLayout.cend(), [](const QVariant& value) {
            return value.toMap().value(QStringLiteral("action")).toString() != QStringLiteral("create");
        })) {
        *error = QObject::tr("Only new partitions can be staged for a new GPT partition table.");
        return false;
    }
    for (const QVariant& value : plan.manualPartitionLayout) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("tableId")).toString() != tableId) {
            *error = QObject::tr("The target partition table changed after the editor was opened.");
            return false;
        }
        if (row.value(QStringLiteral("action")).toString() == QStringLiteral("resize")
            && row.value(QStringLiteral("sectorCount")).toULongLong()
                < row.value(QStringLiteral("originalSectorCount")).toULongLong()
            && !row.value(QStringLiteral("format")).toBool()) {
            QVariantMap resizeRow = row;
            resizeRow.insert(QStringLiteral("filesystem"), row.value(QStringLiteral("originalFilesystem")));
            resizeRow.insert(QStringLiteral("sizeBytes"), row.value(QStringLiteral("sectorCount")).toULongLong()
                * plan.logicalBlockSize);
            if (!shrinkSharedFilesystem(resizeRow, error))
                return false;
        }
    }

    QByteArray table = "label: gpt\n";
    if (!tableId.isEmpty())
        table += "label-id: " + tableId.toLatin1() + "\n";
    table += "unit: sectors\n\n";
    for (const QVariant& value : plan.manualPartitionLayout) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("action")).toString() == QStringLiteral("delete"))
            continue;
        const QString device = row.value(QStringLiteral("device")).toString();
        const quint64 start = row.value(QStringLiteral("startSector")).toULongLong();
        const quint64 count = row.value(QStringLiteral("sectorCount")).toULongLong();
        const QString type = row.value(QStringLiteral("plannedTypeGuid"),
            row.value(QStringLiteral("typeGuid")).toString()).toString();
        const QString name = row.value(QStringLiteral("label")).toString();
        table += device.toLatin1() + " : start=" + QByteArray::number(start)
            + ", size=" + QByteArray::number(count) + ", type=" + type.toLatin1();
        const QString uuid = row.value(QStringLiteral("uuid")).toString();
        if (!uuid.isEmpty()) table += ", uuid=" + uuid.toLatin1();
        const QString attributes = row.value(QStringLiteral("attributes")).toString();
        if (!attributes.isEmpty()) table += ", attrs=" + attributes.toLatin1();
        table += ", name=\"" + name.toUtf8() + "\"\n";
    }
    if (!runPath(QStringLiteral("sfdisk"),
            {QStringLiteral("--no-reread"), QStringLiteral("--force"), plan.targetDevice},
            table, nullptr, error)
        || !runPath(QStringLiteral("blockdev"), {QStringLiteral("--rereadpt"), plan.targetDevice}, {}, nullptr, error)
        || !runPath(QStringLiteral("udevadm"), {QStringLiteral("settle")}, {}, nullptr, error))
        return false;
    for (const QVariant& value : plan.manualPartitionLayout) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("action")).toString() == QStringLiteral("create")
            && !runPath(QStringLiteral("wipefs"), {QStringLiteral("--all"), QStringLiteral("--force"),
                row.value(QStringLiteral("device")).toString()}, {}, nullptr, error))
            return false;
        if (row.value(QStringLiteral("action")).toString() == QStringLiteral("resize")
            && row.value(QStringLiteral("sectorCount")).toULongLong()
                > row.value(QStringLiteral("originalSectorCount")).toULongLong()
            && !row.value(QStringLiteral("format")).toBool()
            && !growManualFilesystem(row, error))
            return false;
    }
    return true;
}

bool createPartitions(const InstallationPlan& plan, QString* error)
{
    if (!plan.acknowledgeDiskErasure || !SystemDiscovery::isSuitableTargetDisk(plan.targetDevice, plan.imagePath)) {
        *error = QObject::tr("The approved target disk is no longer suitable for installation.");
        return false;
    }
    const StoragePreflightResult current = SystemDiscovery::preflightStorage(
        plan.targetDevice, plan.imagePath,
        plan.imageFormat == ImageFormat::DwarFS ? QStringLiteral("DwarFS") : QStringLiteral("SquashFS"),
        plan.swapChoice == SwapChoice::None ? QStringLiteral("None")
            : plan.swapChoice == SwapChoice::Suspend ? QStringLiteral("Suspend")
            : plan.swapChoice == SwapChoice::File ? QStringLiteral("File") : QStringLiteral("Small"),
        plan.partitions.at(0).encrypted, plan.partitions.at(1).encrypted,
        plan.partitions.at(2).encrypted, plan.encryptSwap,
        plan.partitioningMode == PartitioningMode::Automated ? QStringLiteral("automated")
            : (plan.partitioningMode == PartitioningMode::Shared ? QStringLiteral("share")
                : QStringLiteral("manual")),
        plan.manualAssignments, plan.manualPartitionLayout, plan.partitionLabels, plan.partitionSizesMiB,
        plan.sharedSpaceMiB, plan.sharedSpaceStartSector, plan.sharedResizeDevice);
    if (!current.valid || current.layout != plan.storageLayout
        || current.diskBytes != plan.targetDiskBytes || current.logicalBlockSize != plan.logicalBlockSize) {
        *error = QObject::tr("The selected disk no longer matches the approved storage preflight.");
        return false;
    }

    if (plan.partitioningMode == PartitioningMode::Manual)
        return applyManualPartitionLayout(plan, error);
    if (plan.partitioningMode == PartitioningMode::Shared && !plan.sharedResizeDevice.isEmpty()) {
        for (const QVariant& value : plan.storageLayout) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("action")).toString() == QStringLiteral("resize")
                && row.value(QStringLiteral("device")).toString() == plan.sharedResizeDevice
                && !shrinkSharedPartition(plan, row, error))
                return false;
        }
    }
    QByteArray table = plan.partitioningMode == PartitioningMode::Automated
        ? QByteArray("label: gpt\nunit: sectors\n\n") : QByteArray("unit: sectors\n\n");
    for (const QVariant& rowValue : plan.storageLayout) {
        const QVariantMap row = rowValue.toMap();
        const int number = row.value(QStringLiteral("partitionNumber")).toInt();
        if (number <= 0 || row.value(QStringLiteral("action")).toString() != QStringLiteral("create"))
            continue;
        if (plan.partitioningMode == PartitioningMode::Shared)
            table += row.value(QStringLiteral("device")).toString().toLatin1() + " : ";
        table += "start=" + row.value(QStringLiteral("startSector")).toString().toLatin1();
        table += ", size=" + row.value(QStringLiteral("sectorCount")).toString().toLatin1();
        table += ", type=" + row.value(QStringLiteral("typeGuid")).toString().toLatin1();
        table += ", name=\"" + row.value(QStringLiteral("label")).toString().toLatin1() + "\"\n";
    }
    const QStringList arguments = plan.partitioningMode == PartitioningMode::Shared
        ? QStringList{QStringLiteral("--append"), QStringLiteral("--wipe"), QStringLiteral("never"),
                      QStringLiteral("--wipe-partitions"), QStringLiteral("always"), plan.targetDevice}
        : QStringList{QStringLiteral("--wipe"), QStringLiteral("always"),
                      QStringLiteral("--wipe-partitions"), QStringLiteral("always"), plan.targetDevice};
    return runPath(QStringLiteral("sfdisk"), arguments, table, nullptr, error);
}

void copyNetworkProfiles(const InstallationPlan& plan, const QString& root)
{
    if (plan.liveSessionUser.isEmpty())
        return;
    const QString sourceDirectory = QStringLiteral("/etc/NetworkManager/system-connections");
    const QString targetDirectory = root + sourceDirectory;
    if (!QFileInfo(sourceDirectory).isDir() || !QFileInfo(targetDirectory).isDir())
        return;

    QDirIterator files(sourceDirectory, QDir::Files | QDir::NoDotAndDotDot);
    while (files.hasNext()) {
        const QString sourcePath = files.next();
        const QFileInfo sourceInfo = files.fileInfo();
        if (sourceInfo.isSymLink() || sourceInfo.fileName() == QStringLiteral("LTSP"))
            continue;
        const QString targetPath = targetDirectory + QLatin1Char('/') + sourceInfo.fileName();
        if (QFileInfo::exists(targetPath))
            continue;
        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly))
            continue;
        QByteArray contents = source.readAll();
        const QByteArray oldPermission = "permissions=user:" + plan.liveSessionUser.toUtf8() + ":;";
        const QByteArray newPermission = "permissions=user:" + plan.username.toUtf8() + ":;";
        contents.replace(oldPermission, newPermission);
        QSaveFile target(targetPath);
        if (!target.open(QIODevice::WriteOnly | QIODevice::Text))
            continue;
        target.setPermissions(sourceInfo.permissions());
        if (target.write(contents) != contents.size()) {
            target.cancelWriting();
            continue;
        }
        target.commit();
    }
}

bool applyConfig(const InstallationPlan& plan, const QString& root, const QString& espDevice,
                 const QString& rootDevice, const QString& rootLuks,
                 const QString& homeLuks, const QString& varLuks, const QString& swapLuks,
                 QString* error)
{
    const QString espUuid = uuidFor(espDevice, error);
    const QString rootUuid = uuidFor(rootDevice, error);
    if (espUuid.isEmpty() || rootUuid.isEmpty()) return false;

    QString rootLuksUuid, homeUuid, varUuid, swapLuksUuid;
    if (!rootLuks.isEmpty()) {
        rootLuksUuid = uuidFor(rootLuks, error);
        if (rootLuksUuid.isEmpty()) return false;
    }
    if (!homeLuks.isEmpty()) {
        homeUuid = uuidFor(homeLuks, error);
        if (homeUuid.isEmpty()) return false;
    }
    if (!varLuks.isEmpty()) {
        varUuid = uuidFor(varLuks, error);
        if (varUuid.isEmpty()) return false;
    }
    if (!swapLuks.isEmpty()) {
        swapLuksUuid = uuidFor(swapLuks, error);
        if (swapLuksUuid.isEmpty()) return false;
    }

    QByteArray fstab;
    fstab += "UUID=" + espUuid.toLatin1() + " /boot/efi vfat umask=0077 0 1\n";
    const QByteArray rootSource = rootLuks.isEmpty()
        ? QByteArray("UUID=") + rootUuid.toLatin1()
        : QByteArray("/dev/mapper/nx_root");
    fstab += rootSource + " / xfs defaults,inode64 0 1\n";

    const QString homePartition = deviceForRole(plan, QStringLiteral("/home"));
    const QString varPartition = deviceForRole(plan, QStringLiteral("/var/lib"));
    const QString homeFilesystemUuid = !homePartition.isEmpty() && homeLuks.isEmpty()
        ? uuidFor(homePartition, error) : QString();
    const QString varFilesystemUuid = !varPartition.isEmpty() && varLuks.isEmpty()
        ? uuidFor(varPartition, error) : QString();
    if ((!homePartition.isEmpty() && homeLuks.isEmpty() && homeFilesystemUuid.isEmpty())
        || (!varPartition.isEmpty() && varLuks.isEmpty() && varFilesystemUuid.isEmpty())) return false;
    if (!homePartition.isEmpty()) {
        const QByteArray source = homeLuks.isEmpty()
            ? QByteArray("UUID=") + homeFilesystemUuid.toLatin1()
            : QByteArray("/dev/mapper/nx_home");
        fstab += source + " /home f2fs defaults,compress_algorithm=zstd:6,compress_chksum,atgc,gc_merge,lazytime 0 2\n";
    }
    if (!varPartition.isEmpty()) {
        const QByteArray source = varLuks.isEmpty()
            ? QByteArray("UUID=") + varFilesystemUuid.toLatin1()
            : QByteArray("/dev/mapper/nx_var_lib");
        fstab += source + " /var/lib f2fs defaults,compress_algorithm=zstd:6,compress_chksum,atgc,gc_merge,lazytime 0 2\n";
    }

    const QString swapPartition = deviceForRole(plan, QStringLiteral("swap"));
    QString swapUuid;
    if (plan.swapChoice == SwapChoice::File) {
        fstab += "/swapfile none swap defaults 0 0\n";
    } else if (!swapPartition.isEmpty()) {
        if (swapLuks.isEmpty()) {
            swapUuid = uuidFor(swapPartition, error);
            if (swapUuid.isEmpty()) return false;
            fstab += "UUID=" + swapUuid.toLatin1() + " none swap sw,discard 0 0\n";
        } else {
            fstab += "/dev/mapper/nx_swap none swap sw,discard 0 0\n";
        }
    }
    if (!error->isEmpty() || !writeFile(root + QStringLiteral("/etc/fstab"), fstab, error)) return false;

    QByteArray crypttab;
    if (!rootLuks.isEmpty())
        crypttab += "nx_root UUID=" + rootLuksUuid.toLatin1() + " none luks,initramfs\n";
    if (!homeLuks.isEmpty()) crypttab += "nx_home UUID=" + homeUuid.toLatin1() + " none luks\n";
    if (!varLuks.isEmpty()) crypttab += "nx_var_lib UUID=" + varUuid.toLatin1() + " none luks\n";
    if (!swapLuks.isEmpty())
        crypttab += "nx_swap UUID=" + swapLuksUuid.toLatin1() + " none luks,initramfs\n";
    if (!writeFile(root + QStringLiteral("/etc/crypttab"), crypttab, error)) return false;

    QByteArray dmcrypt;
    if (!homeLuks.isEmpty())
        dmcrypt += "target=nx_home\nsource=/dev/disk/by-uuid/" + homeUuid.toLatin1() + "\n\n";
    if (!varLuks.isEmpty())
        dmcrypt += "target=nx_var_lib\nsource=/dev/disk/by-uuid/" + varUuid.toLatin1() + "\n\n";
    if (!swapLuks.isEmpty())
        dmcrypt += "target=nx_swap\nsource=/dev/disk/by-uuid/" + swapLuksUuid.toLatin1() + "\n\n";
    if (!dmcrypt.isEmpty()) {
        const QString dmcryptPath = root + QStringLiteral("/etc/conf.d/dmcrypt");
        QFile currentDmcrypt(dmcryptPath);
        QByteArray existing;
        if (currentDmcrypt.open(QIODevice::ReadOnly | QIODevice::Text))
            existing = currentDmcrypt.readAll();
        if (!writeFile(dmcryptPath, existing + dmcrypt, error)) return false;
    }

    if (plan.swapChoice == SwapChoice::Suspend) {
        QByteArray resume;
        if (!swapLuks.isEmpty())
            resume = "RESUME=/dev/mapper/nx_swap\n";
        else if (!swapUuid.isEmpty())
            resume = "RESUME=UUID=" + swapUuid.toLatin1() + "\n";
        else
            resume = "RESUME=none\n";
        if (!writeFile(root + QStringLiteral("/etc/initramfs-tools/conf.d/resume"), resume, error))
            return false;
    }

    const QString localeGen = root + QStringLiteral("/etc/locale.gen");
    QFile localeFile(localeGen);
    if (!localeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QObject::tr("Could not read the target locale configuration.");
        return false;
    }
    QByteArray lines = localeFile.readAll();
    const QByteArray selectedLocale = plan.locale.toUtf8();
    QByteArray selectedLocalePrefix = selectedLocale;
    selectedLocalePrefix.append(' ');
    if (!lines.contains(selectedLocalePrefix))
        lines += selectedLocale + " UTF-8\n";
    else {
        const QList<QByteArray> sourceLines = lines.split('\n');
        QByteArray updated;
        for (QByteArray line : sourceLines) {
            if (line.trimmed().startsWith('#') && line.contains(selectedLocale))
                line = line.mid(1).trimmed();
            updated += line + '\n';
        }
        lines = updated;
    }
    const QByteArray localeConfiguration = "LANG=" + selectedLocale + "\n";
    if (!writeFile(localeGen, lines, error)
        || !writeFile(root + QStringLiteral("/etc/locale.conf"), localeConfiguration, error)) return false;
    const QString defaultLocale = root + QStringLiteral("/etc/default");
    if (QFileInfo(defaultLocale).isDir()
        && !writeFile(defaultLocale + QStringLiteral("/locale"), localeConfiguration, error)) return false;
    if (!chrootRun(root, {QStringLiteral("/usr/sbin/locale-gen")}, {}, error)) return false;

    const QString zoneTarget = root + QStringLiteral("/usr/share/zoneinfo/") + plan.timeZone;
    if (!QFileInfo(zoneTarget).isFile()) {
        *error = QObject::tr("The selected time zone is not present in the installed image.");
        return false;
    }
    const QString localtime = root + QStringLiteral("/etc/localtime");
    QFile::remove(localtime);
    const QByteArray zoneLink = (QStringLiteral("/usr/share/zoneinfo/") + plan.timeZone).toLocal8Bit();
    if (::symlink(zoneLink.constData(), QFile::encodeName(localtime).constData()) != 0) {
        *error = QObject::tr("Could not set the selected time zone.");
        return false;
    }
    copyNetworkProfiles(plan, root);
    if (!writeFile(root + QStringLiteral("/etc/hostname"), plan.hostname.toUtf8() + '\n', error)
        || !writeFile(root + QStringLiteral("/etc/adjtime"), "0.0 0 0.0\n0\nUTC\n", error)
        || !writeFile(root + QStringLiteral("/etc/hosts"),
            "127.0.0.1 localhost\n127.0.1.1 " + plan.hostname.toUtf8() + "\n::1 localhost ip6-localhost ip6-loopback\n", error)) return false;

    QByteArray xorg = "Section \"InputClass\"\n    Identifier \"system-keyboard\"\n    MatchIsKeyboard \"on\"\n    Option \"XkbLayout\" \"" + plan.keyboardLayout.toUtf8() + "\"\n";
    if (!plan.keyboardVariant.isEmpty()) xorg += "    Option \"XkbVariant\" \"" + plan.keyboardVariant.toUtf8() + "\"\n";
    if (!plan.keyboardModel.isEmpty()) xorg += "    Option \"XkbModel\" \"" + plan.keyboardModel.toUtf8() + "\"\n";
    if (!plan.keyboardOptions.isEmpty()) xorg += "    Option \"XkbOptions\" \"" + plan.keyboardOptions.toUtf8() + "\"\n";
    xorg += "EndSection\n";
    if (!QDir().mkpath(root + QStringLiteral("/etc/X11/xorg.conf.d"))
        || !writeFile(root + QStringLiteral("/etc/X11/xorg.conf.d/00-keyboard.conf"), xorg, error)) return false;

    QStringList groups = plan.userGroups;
    groups.removeDuplicates();
    if (!chrootRun(root, {QStringLiteral("/usr/sbin/useradd"), QStringLiteral("--create-home"),
            QStringLiteral("--shell"), plan.userShell, QStringLiteral("--groups"), groups.join(QLatin1Char(',')),
            QStringLiteral("--comment"), plan.fullName, plan.username}, {}, error)) return false;
    if (!chrootRun(root, {QStringLiteral("/usr/sbin/chpasswd")},
                   plan.username.toUtf8() + ':' + plan.userPassword.toUtf8() + '\n', error)) return false;

    const QString greetd = root + QStringLiteral("/etc/greetd/config.toml");
    QFile greetdFile(greetd);
    if (!greetdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QObject::tr("Could not read greetd configuration.");
        return false;
    }
    QString config = QString::fromUtf8(greetdFile.readAll());
    const auto setTomlValue = [&config](const QString& section, const QString& key, const QString& value) {
        const QString expression = QStringLiteral("(?ms)(^\\[%1\\]\\s*.*?^\\s*%2\\s*=\\s*)[^\\n]*(?=\\n|$)")
            .arg(QRegularExpression::escape(section), QRegularExpression::escape(key));
        const QRegularExpression re(expression);
        if (re.match(config).hasMatch())
            config.replace(re, QStringLiteral("\\1%1").arg(value));
    };
    setTomlValue(QStringLiteral("terminal"), QStringLiteral("vt"), QStringLiteral("7"));
    setTomlValue(QStringLiteral("default_session"), QStringLiteral("command"),
        QStringLiteral("\"greetd-hyprland --config /var/lib/greetd/.config/hypr/hyprland.lua\""));
    const QRegularExpression initialSession(QStringLiteral("(?ms)^\\[initial_session\\]\\s*.*?(?=^\\[|\\z)"));
    config.remove(initialSession);
    if (plan.automaticLogin) {
        config += QStringLiteral("\n[initial_session]\ncommand = \"/usr/bin/env NWSM_REQUIRED_VARS=HYPRLAND_INSTANCE_SIGNATURE /usr/bin/nwsm -- /usr/bin/start-hyprland > /dev/null 2>&1\"\nuser = \"%1\"\n").arg(plan.username);
    }
    if (!writeFile(greetd, config.toUtf8(), error)) return false;

    return true;
}

} // namespace

bool InstallationExecutor::execute(const InstallationPlan& plan, const ProgressCallback& progress,
                                   QString* error)
{
    const auto report = [&progress](int percent, const QString& phase, const QString& detail) {
        if (progress) progress({percent, phase, detail});
    };
    if (!plan.acknowledgeDiskErasure || plan.debugOnly || plan.partitions.size() != 3) {
        *error = QObject::tr("The plan is not eligible for real installation.");
        return false;
    }
    const QString disk = QFileInfo(plan.targetDevice).canonicalFilePath();
    if (disk.isEmpty() || disk != plan.targetDevice) {
        *error = QObject::tr("The target disk path changed after authorization.");
        return false;
    }

    const QVariantMap originalDiskInfo = plan.partitioningMode == PartitioningMode::Shared
        ? SystemDiscovery::inspectTargetDisk(disk, plan.imagePath) : QVariantMap{};
    if (plan.partitioningMode == PartitioningMode::Shared
        && !originalDiskInfo.value(QStringLiteral("gpt")).toBool()) {
        *error = QObject::tr("The existing GPT partition table could not be verified.");
        return false;
    }
    const QVariantList originalPartitions = originalDiskInfo.value(QStringLiteral("partitions")).toList();
    report(2, QObject::tr("Preparing storage"), QObject::tr("Applying the approved storage plan…"));
    if (!createPartitions(plan, error)) return false;
    if (plan.partitioningMode != PartitioningMode::Manual) {
        if (!runPath(QStringLiteral("blockdev"), {QStringLiteral("--rereadpt"), disk}, {}, nullptr, error)
            || !runPath(QStringLiteral("udevadm"), {QStringLiteral("settle")}, {}, nullptr, error)) return false;
    }

    const QString esp = deviceForRole(plan, QStringLiteral("/boot/efi"));
    const QString rootPartition = deviceForRole(plan, QStringLiteral("/"));
    const bool encryptRoot = rowForRole(plan, QStringLiteral("/"))
        .value(QStringLiteral("encrypted")).toBool();
    const QString rootDevice = encryptRoot ? QStringLiteral("/dev/mapper/nx_root") : rootPartition;
    const QString homePartition = deviceForRole(plan, QStringLiteral("/home"));
    const QString varPartition = deviceForRole(plan, QStringLiteral("/var/lib"));
    const QString swapPartition = deviceForRole(plan, QStringLiteral("swap"));
    if (esp.isEmpty() || rootDevice.isEmpty()
        || (plan.partitioningMode != PartitioningMode::Manual
            && (homePartition.isEmpty() || varPartition.isEmpty()))) {
        *error = QObject::tr("The approved storage plan is missing a required partition.");
        return false;
    }
    if (plan.partitioningMode == PartitioningMode::Shared) {
        const QVariantMap diskInfo = SystemDiscovery::inspectTargetDisk(disk, plan.imagePath);
        if (!diskInfo.value(QStringLiteral("gpt")).toBool()
            || diskInfo.value(QStringLiteral("tableId")) != originalDiskInfo.value(QStringLiteral("tableId"))) {
            *error = QObject::tr("The existing GPT partition table changed while adding Nitrux partitions.");
            return false;
        }
        const QVariantList actual = diskInfo.value(QStringLiteral("partitions")).toList();
        QVariantMap resizeRow;
        int createdPartitionCount = 0;
        for (const QVariant& value : plan.storageLayout) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("action")).toString() == QStringLiteral("resize"))
                resizeRow = row;
            else if (row.value(QStringLiteral("action")).toString() == QStringLiteral("create"))
                ++createdPartitionCount;
        }
        if (actual.size() != originalPartitions.size() + createdPartitionCount) {
            *error = QObject::tr("The target partition table has unexpected changes.");
            return false;
        }
        for (const QVariant& originalValue : originalPartitions) {
            const QVariantMap original = originalValue.toMap();
            const QString originalDevice = original.value(QStringLiteral("device")).toString();
            QVariantMap observed;
            for (const QVariant& actualValue : actual) {
                const QVariantMap candidate = actualValue.toMap();
                if (candidate.value(QStringLiteral("device")).toString() == originalDevice) {
                    observed = candidate;
                    break;
                }
            }
            bool preserved = !observed.isEmpty()
                && observed.value(QStringLiteral("startSector")) == original.value(QStringLiteral("startSector"))
                && observed.value(QStringLiteral("sectorCount")) == original.value(QStringLiteral("sectorCount"))
                && observed.value(QStringLiteral("typeGuid")).toString().compare(
                    original.value(QStringLiteral("typeGuid")).toString(), Qt::CaseInsensitive) == 0
                && observed.value(QStringLiteral("uuid")) == original.value(QStringLiteral("uuid"))
                && observed.value(QStringLiteral("attributes")) == original.value(QStringLiteral("attributes"))
                && observed.value(QStringLiteral("label")) == original.value(QStringLiteral("label"))
                && observed.value(QStringLiteral("filesystem")) == original.value(QStringLiteral("filesystem"));
            if (!resizeRow.isEmpty() && originalDevice == resizeRow.value(QStringLiteral("device")).toString()) {
                preserved = !observed.isEmpty()
                    && observed.value(QStringLiteral("startSector")) == resizeRow.value(QStringLiteral("startSector"))
                    && observed.value(QStringLiteral("sectorCount")) == resizeRow.value(QStringLiteral("sectorCount"))
                    && observed.value(QStringLiteral("typeGuid")).toString().compare(
                        resizeRow.value(QStringLiteral("typeGuid")).toString(), Qt::CaseInsensitive) == 0
                    && observed.value(QStringLiteral("uuid")) == original.value(QStringLiteral("uuid"))
                    && observed.value(QStringLiteral("attributes")) == original.value(QStringLiteral("attributes"))
                    && observed.value(QStringLiteral("label")) == original.value(QStringLiteral("label"))
                    && observed.value(QStringLiteral("filesystem")) == original.value(QStringLiteral("filesystem"));
            }
            if (!preserved) {
                *error = QObject::tr("An existing partition changed beyond the approved resize operation.");
                return false;
            }
        }
        for (const QVariant& value : plan.storageLayout) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("action")).toString() != QStringLiteral("create"))
                continue;
            bool found = false;
            for (const QVariant& entry : actual) {
                const QVariantMap observed = entry.toMap();
                if (observed.value(QStringLiteral("device")) == row.value(QStringLiteral("device"))
                    && observed.value(QStringLiteral("startSector")) == row.value(QStringLiteral("startSector"))
                    && observed.value(QStringLiteral("sectorCount")) == row.value(QStringLiteral("sectorCount"))
                    && observed.value(QStringLiteral("typeGuid")).toString().compare(
                        row.value(QStringLiteral("typeGuid")).toString(), Qt::CaseInsensitive) == 0
                    && observed.value(QStringLiteral("label")) == row.value(QStringLiteral("label"))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                *error = QObject::tr("The created partitions do not match the approved layout.");
                return false;
            }
        }
    } else if (plan.partitioningMode == PartitioningMode::Manual) {
        const QVariantMap diskInfo = SystemDiscovery::inspectTargetDisk(disk, plan.imagePath);
        const QVariantList actual = diskInfo.value(QStringLiteral("partitions")).toList();
        int expectedCount = 0;
        for (const QVariant& value : plan.manualPartitionLayout) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("action")).toString() == QStringLiteral("delete"))
                continue;
            ++expectedCount;
            const QString device = row.value(QStringLiteral("device")).toString();
            QVariantMap observed;
            for (const QVariant& actualValue : actual) {
                const QVariantMap candidate = actualValue.toMap();
                if (candidate.value(QStringLiteral("device")).toString() == device) {
                    observed = candidate;
                    break;
                }
            }
            const QString expectedType = row.value(QStringLiteral("plannedTypeGuid"),
                row.value(QStringLiteral("typeGuid")).toString()).toString();
            if (observed.isEmpty()
                || observed.value(QStringLiteral("startSector")).toULongLong()
                    != row.value(QStringLiteral("startSector")).toULongLong()
                || observed.value(QStringLiteral("sectorCount")).toULongLong()
                    != row.value(QStringLiteral("sectorCount")).toULongLong()
                || observed.value(QStringLiteral("typeGuid")).toString().compare(expectedType, Qt::CaseInsensitive) != 0
                || observed.value(QStringLiteral("label")).toString() != row.value(QStringLiteral("label")).toString()
                || (!row.value(QStringLiteral("uuid")).toString().isEmpty()
                    && observed.value(QStringLiteral("uuid")).toString() != row.value(QStringLiteral("uuid")).toString())
                || (!row.value(QStringLiteral("uuid")).toString().isEmpty()
                    && observed.value(QStringLiteral("attributes")) != row.value(QStringLiteral("attributes")))) {
                *error = QObject::tr("The applied partition table does not match the reviewed manual layout.");
                return false;
            }
        }
        if (actual.size() != expectedCount) {
            *error = QObject::tr("The applied partition count does not match the reviewed manual layout.");
            return false;
        }
    }
    const bool encryptHome = rowForRole(plan, QStringLiteral("/home"))
        .value(QStringLiteral("encrypted")).toBool();
    const bool encryptVarLib = rowForRole(plan, QStringLiteral("/var/lib"))
        .value(QStringLiteral("encrypted")).toBool();
    const bool encryptSwap = plan.encryptSwap && !swapPartition.isEmpty();
    const QString homeDevice = encryptHome ? QStringLiteral("/dev/mapper/nx_home") : homePartition;
    const QString varDevice = encryptVarLib ? QStringLiteral("/dev/mapper/nx_var_lib") : varPartition;
    const QString swapDevice = encryptSwap ? QStringLiteral("/dev/mapper/nx_swap") : swapPartition;

    report(8, QObject::tr("Formatting filesystems"), QObject::tr("Creating the EFI and encrypted filesystems…"));
    const QString fatFormatter = QStandardPaths::findExecutable(QStringLiteral("mkfs.fat"));
    const QString vfatFormatter = fatFormatter.isEmpty()
        ? QStandardPaths::findExecutable(QStringLiteral("mkfs.vfat")) : fatFormatter;
    if (vfatFormatter.isEmpty()
        || !run(vfatFormatter, {QStringLiteral("-F"), QStringLiteral("32"),
            QStringLiteral("-n"), labelForRole(plan, QStringLiteral("/boot/efi")), esp}, {}, nullptr, error))
        return false;

    const QString passphrase = plan.encryptionPassphrase;
    CryptMapperGuard mapperGuard;
    const auto openEncrypted = [&](bool encrypted, const QString& partition, const QString& mapper) -> bool {
        if (!encrypted)
            return true;
        const QString pbkdf = partition == rootPartition
            ? QStringLiteral("pbkdf2") : QStringLiteral("argon2id");
        if (!runPath(QStringLiteral("cryptsetup"), {QStringLiteral("luksFormat"), QStringLiteral("--type"),
                QStringLiteral("luks2"), QStringLiteral("--pbkdf"), pbkdf,
                QStringLiteral("--batch-mode"), QStringLiteral("--key-file"), QStringLiteral("-"), partition},
                passphrase.toUtf8(), nullptr, error)) return false;
        if (!runPath(QStringLiteral("cryptsetup"), {QStringLiteral("open"), QStringLiteral("--key-file"),
                QStringLiteral("-"), partition, QFileInfo(mapper).fileName()},
                passphrase.toUtf8(), nullptr, error)) return false;
        mapperGuard.opened.append(QFileInfo(mapper).fileName());
        return true;
    };
    if (!openEncrypted(encryptRoot, rootPartition, rootDevice)
        || !runPath(QStringLiteral("mkfs.xfs"), {QStringLiteral("-f"), QStringLiteral("-L"),
            labelForRole(plan, QStringLiteral("/")), rootDevice}, {}, nullptr, error))
        return false;

    const auto setupF2fs = [&](bool encrypted, const QString& partition, const QString& mapper,
                               const QString& label) -> bool {
        return openEncrypted(encrypted, partition, mapper)
            && runPath(QStringLiteral("mkfs.f2fs"), {QStringLiteral("-f"), QStringLiteral("-O"),
                QStringLiteral("compression"), QStringLiteral("-l"), label,
                encrypted ? mapper : partition}, {}, nullptr, error);
    };
    if ((!homePartition.isEmpty()
            && !setupF2fs(encryptHome, homePartition, homeDevice, labelForRole(plan, QStringLiteral("/home"))))
        || (!varPartition.isEmpty()
            && !setupF2fs(encryptVarLib, varPartition, varDevice, labelForRole(plan, QStringLiteral("/var/lib"))))
        || (encryptSwap && !openEncrypted(true, swapPartition, swapDevice)))
        return false;
    if (!swapPartition.isEmpty()
        && !runPath(QStringLiteral("mkswap"), {QStringLiteral("-L"),
            labelForRole(plan, QStringLiteral("swap")), swapDevice}, {}, nullptr, error))
        return false;

    QTemporaryDir target(QStringLiteral("/run/squiddy-target-XXXXXX"));
    target.setAutoRemove(false);
    if (!target.isValid()) {
        *error = QObject::tr("Could not create a temporary installation mount directory.");
        return false;
    }
    const QString root = target.path();
    QList<MountRecord> mounts;
    bool success = false;
    do {
        report(15, QObject::tr("Mounting target"), QObject::tr("Mounting the new filesystems…"));
        if (!mountDevice(rootDevice, root, {QStringLiteral("defaults,inode64")}, &mounts, error)) break;
        if (!QDir().mkpath(root + QStringLiteral("/home")) || !QDir().mkpath(root + QStringLiteral("/var/lib"))) {
            *error = QObject::tr("Could not create filesystem mount points."); break;
        }
        if ((!homeDevice.isEmpty() && !mountDevice(homeDevice, root + QStringLiteral("/home"),
                {QStringLiteral("defaults,compress_algorithm=zstd:6,compress_chksum,atgc,gc_merge,lazytime")}, &mounts, error))
            || (!varDevice.isEmpty() && !mountDevice(varDevice, root + QStringLiteral("/var/lib"),
                {QStringLiteral("defaults,compress_algorithm=zstd:6,compress_chksum,atgc,gc_merge,lazytime")}, &mounts, error))
            || !mountDevice(esp, root + QStringLiteral("/boot/efi"), {QStringLiteral("umask=0077")}, &mounts, error)) break;

        report(22, QObject::tr("Installing base system"), QObject::tr("Extracting the Nitrux image…"));
        bool extracted = false;
        if (plan.imageFormat == ImageFormat::SquashFS)
            extracted = runPath(QStringLiteral("unsquashfs"), {QStringLiteral("-f"), QStringLiteral("-d"), root, plan.imagePath}, {}, nullptr, error);
        else
            extracted = runPath(QStringLiteral("dwarfsextract"), {QStringLiteral("-i"), plan.imagePath,
                        QStringLiteral("-o"), root}, {}, nullptr, error);
        if (!extracted) break;
        if (plan.swapChoice == SwapChoice::File) {
            if (QFileInfo::exists(root + QStringLiteral("/swapfile"))) {
                *error = QObject::tr("The image already contains /swapfile; refusing to overwrite it.");
                break;
            }
            if (!runPath(QStringLiteral("fallocate"), {QStringLiteral("-l"), QStringLiteral("512M"), root + QStringLiteral("/swapfile")}, {}, nullptr, error)
                || !runPath(QStringLiteral("chmod"), {QStringLiteral("600"), root + QStringLiteral("/swapfile")}, {}, nullptr, error)
                || !runPath(QStringLiteral("mkswap"), {root + QStringLiteral("/swapfile")}, {}, nullptr, error)) break;
        }

        if (!mountBind(QStringLiteral("/dev"), root + QStringLiteral("/dev"), &mounts, error)
            || !mountBind(QStringLiteral("/dev/pts"), root + QStringLiteral("/dev/pts"), &mounts, error)
            || !mountBind(QStringLiteral("/proc"), root + QStringLiteral("/proc"), &mounts, error)
            || !mountBind(QStringLiteral("/sys"), root + QStringLiteral("/sys"), &mounts, error)
            || !mountTmpfs(root + QStringLiteral("/run"), &mounts, error)
            || !mountBind(QStringLiteral("/run/udev"), root + QStringLiteral("/run/udev"), &mounts, error)) break;
        if (QFileInfo::exists(QStringLiteral("/sys/firmware/efi/efivars"))) {
            QDir().mkpath(root + QStringLiteral("/sys/firmware/efi/efivars"));
            if (!runPath(QStringLiteral("mount"), {QStringLiteral("-t"), QStringLiteral("efivarfs"),
                    QStringLiteral("efivarfs"), root + QStringLiteral("/sys/firmware/efi/efivars")}, {}, nullptr, error)) break;
            mounts.append({root + QStringLiteral("/sys/firmware/efi/efivars")});
        }

        report(42, QObject::tr("Configuring installed system"), QObject::tr("Writing locale, keyboard, storage, and account settings…"));
        if (!applyConfig(plan, root, esp, rootDevice,
                encryptRoot ? rootPartition : QString(),
                encryptHome ? homePartition : QString(),
                encryptVarLib ? varPartition : QString(),
                encryptSwap ? swapPartition : QString(), error)) break;
        QString hwclockError;
        if (!chrootRun(root, {QStringLiteral("/usr/sbin/hwclock"), QStringLiteral("--systohc"),
                QStringLiteral("--utc")}, {}, &hwclockError))
            chrootRun(root, {QStringLiteral("/usr/sbin/hwclock"), QStringLiteral("--systohc"),
                QStringLiteral("--utc"), QStringLiteral("--directisa")}, {}, &hwclockError);
        report(61, QObject::tr("Configuring Nitrux"), QObject::tr("Selecting the kernel and generating initramfs images…"));
        if (!chrootRun(root, {QStringLiteral("/usr/bin/linker")}, {}, error)
            || !chrootRun(root, {QStringLiteral("/usr/bin/select-kernel")}, {}, error)
            || !writeFile(root + QStringLiteral("/etc/initramfs-tools/conf.d/nitrux-initramfs.conf"),
                          "MODULES=dep\n", error)
            || !chrootRun(root, {QStringLiteral("/usr/sbin/update-initramfs"), QStringLiteral("-u"), QStringLiteral("-k"), QStringLiteral("all")}, {}, error)) break;
        if (QFileInfo::exists(root + QStringLiteral("/etc/init.d/gpu-vendor-check"))
            && QFileInfo::exists(root + QStringLiteral("/etc/runlevels/default/gpu-vendor-check"))) {
            QString serviceError;
            chrootRun(root, {QStringLiteral("/sbin/rc-update"), QStringLiteral("del"),
                QStringLiteral("gpu-vendor-check"), QStringLiteral("default")}, {}, &serviceError);
        }

        report(72, QObject::tr("Configuring UEFI boot"), QObject::tr("Installing Nitrux signed boot files…"));
        if (!chrootRun(root, {QStringLiteral("/usr/bin/install-nitrux-secure-boot")}, {}, error)) break;
        const QString grubDefaults = root + QStringLiteral("/etc/default/grub");
        QFile grubFile(grubDefaults);
        if (!grubFile.open(QIODevice::ReadOnly | QIODevice::Text)) { *error = QObject::tr("Could not read GRUB defaults."); break; }
        QString grub = QString::fromUtf8(grubFile.readAll());
        const auto setGrubValue = [&grub](const QString& key, const QString& value) {
            const QRegularExpression re(QStringLiteral("(?m)^#?\\s*") + QRegularExpression::escape(key) + QStringLiteral("=.*$"));
            if (re.match(grub).hasMatch())
                grub.replace(re, key + QLatin1Char('=') + value);
            else
                grub += key + QLatin1Char('=') + value + QLatin1Char('\n');
        };
        setGrubValue(QStringLiteral("GRUB_DEFAULT"), QStringLiteral("\"saved\""));
        setGrubValue(QStringLiteral("GRUB_DISABLE_OS_PROBER"), QStringLiteral("false"));
        setGrubValue(QStringLiteral("GRUB_DISABLE_RECOVERY"), QStringLiteral("true"));
        setGrubValue(QStringLiteral("GRUB_DISABLE_SUBMENU"), QStringLiteral("true"));
        setGrubValue(QStringLiteral("GRUB_ENABLE_CRYPTODISK"), QStringLiteral("true"));
        setGrubValue(QStringLiteral("GRUB_RECORDFAIL_TIMEOUT"), QStringLiteral("0"));
        setGrubValue(QStringLiteral("GRUB_TERMINAL_OUTPUT"), QStringLiteral("\"console\""));
        setGrubValue(QStringLiteral("GRUB_TIMEOUT"), QStringLiteral("0"));
        setGrubValue(QStringLiteral("GRUB_TIMEOUT_STYLE"), QStringLiteral("\"countdown\""));
        setGrubValue(QStringLiteral("GRUB_CMDLINE_LINUX_DEFAULT"), QStringLiteral("\"amd_prefcore=enabled amd_pstate=active amdgpu.cik_support=1 amdgpu.ppfeaturemask=0xffffffff amdgpu.si_support=1 amdgpu.vm_update_mode=3 apparmor=1 audit=0 fsck.mode=skip hardened_usercopy=1 init_on_free=1 intel_iommu=on intel_pstate=enable kfence=off libahci.ignore_sss=1 lsm=capability,yama,bpf,apparmor,landlock modprobe.blacklist=iTCO_wdt,sp5100_tco nosgx nvidia_drm.modeset=1 nvme_core.default_ps_max_latency_us=0 nvme_core.multipath=Y overlayroot=tmpfs:swap=1,recurse=0 page_alloc.shuffle=1 pti=on quiet radeon.cik_support=0 radeon.si_support=0 randomize_kstack_offset=1 rcu_nocbs=all rcupdate.rcu_expedited=1 slab_nomerge usbcore.autosuspend=-1 vdso32=0 vsyscall=none vt.global_cursor_default=0 xhci_hcd.quirks=270336 zswap.compressor=zstd zswap.enabled=1 zswap.max_pool_percent=20 zswap.zpool=zsmalloc\""));
        if (!writeFile(grubDefaults, grub.toUtf8(), error)
            || !chrootRun(root, {QStringLiteral("/usr/sbin/grub-mkconfig"), QStringLiteral("-o"),
                QStringLiteral("/boot/efi/EFI/Nitrux/grub.cfg")}, {}, error)) break;
        if (!runPath(QStringLiteral("efibootmgr"), {QStringLiteral("--create"), QStringLiteral("--write-signature"),
                QStringLiteral("--disk"), disk, QStringLiteral("--part"),
                QString::number(rowForRole(plan, QStringLiteral("/boot/efi"))
                    .value(QStringLiteral("partitionNumber")).toInt()),
                QStringLiteral("--label"), QStringLiteral("Nitrux"), QStringLiteral("--loader"),
                QStringLiteral("\\EFI\\Nitrux\\shimx64.efi")}, {}, nullptr, error)) break;

        report(85, QObject::tr("Finalizing"), QObject::tr("Removing live installer packages…"));
        if (!chrootRun(root, {QStringLiteral("/usr/bin/apt-get"), QStringLiteral("purge"), QStringLiteral("-y"),
                QStringLiteral("calamares"), QStringLiteral("calamares-settings-nitrux"), QStringLiteral("casper"),
                QStringLiteral("nitrux-live-user"), QStringLiteral("rsync"), QStringLiteral("python3-toml")}, {}, error)) break;
        if (!chrootRun(root, {QStringLiteral("/usr/bin/undebianize")}, {}, error)) break;
        success = true;
    } while (false);

    report(success ? 96 : 0, success ? QObject::tr("Finishing") : QObject::tr("Installation stopped"),
           success ? QObject::tr("Unmounting the target filesystems…") : *error);
    const bool unmounted = unmountAll(&mounts, error);
    const bool mappingsClosed = unmounted && mapperGuard.close(error);
    if (!success || !unmounted || !mappingsClosed) {
        if (unmounted)
            QDir(root).removeRecursively();
        return false;
    }
    QDir(root).removeRecursively();
    report(100, QObject::tr("Complete"), QObject::tr("Nitrux was installed successfully."));
    return true;
}
