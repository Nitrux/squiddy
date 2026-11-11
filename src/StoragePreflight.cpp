// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "SystemDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <limits>
#include <vector>

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

struct NitruxPartitionTarget {
    QString label;
    QString mountPoint;
    QString filesystem;
    quint64 percent = 0;
};

QList<NitruxPartitionTarget> nitruxPartitionTargets()
{
    return {
        {QStringLiteral("NX_ROOT"), QStringLiteral("/"), QStringLiteral("xfs"), 23},
        {QStringLiteral("NX_HOME"), QStringLiteral("/home"), QStringLiteral("f2fs"), 67},
        {QStringLiteral("NX_VAR_LIB"), QStringLiteral("/var/lib"), QStringLiteral("f2fs"), 10}
    };
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

QVariantMap SystemDiscovery::inspectTargetDisk(const QString& devicePath, const QString& imagePath)
{
    if (!isSuitableTargetDisk(devicePath, imagePath))
        return {};
    const QString canonical = QFileInfo(devicePath).canonicalFilePath();
    const QString name = QFileInfo(canonical).fileName();
    const QString sysfs = QStringLiteral("/sys/class/block/") + name;
    bool sizeOk = false;
    bool sectorOk = false;
    const quint64 sectors512 = readText(sysfs + QStringLiteral("/size")).toULongLong(&sizeOk);
    const quint64 sector = readText(sysfs + QStringLiteral("/queue/logical_block_size")).toULongLong(&sectorOk);
    if (!sizeOk || !sectorOk || sector < 512 || sector > MiB
        || sectors512 > std::numeric_limits<quint64>::max() / 512)
        return {};
    const quint64 bytes = sectors512 * 512;
    if (bytes % sector != 0)
        return {};

    QVariantMap info{{QStringLiteral("device"), canonical},
                     {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(bytes)},
                     {QStringLiteral("sectorSize"), QVariant::fromValue<qulonglong>(sector)},
                     {QStringLiteral("gpt"), false}};
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("sfdisk"));
    if (executable.isEmpty())
        return info;
    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--json"), canonical});
    process.start();
    if (!process.waitForStarted() || !process.waitForFinished(10000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return info;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
    const QJsonObject table = document.object().value(QStringLiteral("partitiontable")).toObject();
    const QJsonValue jsonSectorSize = table.contains(QStringLiteral("sectorsize"))
        ? table.value(QStringLiteral("sectorsize")) : table.value(QStringLiteral("sector-size"));
    const QString tableLabel = table.value(QStringLiteral("label")).toString();
    const bool isGpt = tableLabel == QStringLiteral("gpt");
    const bool isDos = tableLabel == QStringLiteral("dos") || tableLabel == QStringLiteral("msdos");
    if ((!isGpt && !isDos)
        || table.value(QStringLiteral("device")).toString() != canonical
        || table.value(QStringLiteral("unit")).toString() != QStringLiteral("sectors")
        || (!jsonSectorSize.isUndefined() && jsonSectorSize.toVariant().toULongLong() != sector))
        return info;

    const quint64 firstLba = isGpt
        ? table.value(QStringLiteral("firstlba")).toVariant().toULongLong() : 1;
    const quint64 lastLba = isGpt
        ? table.value(QStringLiteral("lastlba")).toVariant().toULongLong() : bytes / sector - 1;
    if ((isGpt && firstLba < 34) || lastLba <= firstLba || lastLba >= bytes / sector)
        return info;
    QVariantList partitions;
    struct Extent { quint64 begin; quint64 end; };
    std::vector<Extent> used;
    const QJsonArray entries = table.value(QStringLiteral("partitions")).toArray();
    for (const QJsonValue& entry : entries) {
        const QJsonObject row = entry.toObject();
        const QString node = row.value(QStringLiteral("node")).toString();
        const quint64 start = row.value(QStringLiteral("start")).toVariant().toULongLong();
        const quint64 count = row.value(QStringLiteral("size")).toVariant().toULongLong();
        if (!node.startsWith(canonical) || !QFileInfo(node).exists()
            || start < firstLba || count == 0 || start > lastLba
            || count > lastLba - start + 1)
            return info;
        used.push_back({start, start + count});
        QString filesystem;
        const QString blkid = QStandardPaths::findExecutable(QStringLiteral("blkid"));
        if (!blkid.isEmpty()) {
            QProcess probe;
            probe.setProgram(blkid);
            probe.setArguments({QStringLiteral("--probe"), QStringLiteral("--match-tag"),
                QStringLiteral("TYPE"), QStringLiteral("--output"), QStringLiteral("value"), node});
            probe.start();
            if (probe.waitForStarted() && probe.waitForFinished(5000)
                && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0)
                filesystem = QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed().toLower();
        }
        const bool shrinkable = filesystem == QStringLiteral("ext2")
            || filesystem == QStringLiteral("ext3") || filesystem == QStringLiteral("ext4")
            || filesystem == QStringLiteral("btrfs") || filesystem == QStringLiteral("ntfs")
            || filesystem == QStringLiteral("vfat") || filesystem == QStringLiteral("swap");
        partitions.append(QVariantMap{
            {QStringLiteral("device"), node},
            {QStringLiteral("partitionNumber"), [&node, &canonical] {
                QString suffix = node.mid(canonical.size());
                if (suffix.startsWith(QLatin1Char('p'))) suffix.remove(0, 1);
                bool ok = false;
                const int number = suffix.toInt(&ok);
                return ok ? number : 0;
            }()},
            {QStringLiteral("filesystem"), filesystem},
            {QStringLiteral("shrinkable"), shrinkable},
            {QStringLiteral("startSector"), QVariant::fromValue<qulonglong>(start)},
            {QStringLiteral("sectorCount"), QVariant::fromValue<qulonglong>(count)},
            {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(count * sector)},
            {QStringLiteral("typeGuid"), row.value(QStringLiteral("type")).toString()},
            {QStringLiteral("uuid"), row.value(QStringLiteral("uuid")).toString()},
            {QStringLiteral("attributes"), row.value(QStringLiteral("attrs")).toVariant()},
            {QStringLiteral("label"), row.value(QStringLiteral("name")).toString()}
        });
    }
    if (isDos) {
        info.insert(QStringLiteral("partitions"), partitions);
        return info;
    }
    std::sort(used.begin(), used.end(), [](const Extent& a, const Extent& b) { return a.begin < b.begin; });
    quint64 cursor = firstLba;
    quint64 bestStart = 0;
    quint64 bestSize = 0;
    QVariantList freeExtents;
    for (const Extent& extent : used) {
        if (extent.begin < cursor)
            return info;
        const quint64 begin = alignUp(cursor * sector, Alignment) / sector;
        const quint64 end = alignDown(extent.begin * sector, Alignment) / sector;
        if (end > begin) {
            const quint64 count = end - begin;
            freeExtents.append(QVariantMap{
                {QStringLiteral("startSector"), QVariant::fromValue<qulonglong>(begin)},
                {QStringLiteral("sectorCount"), QVariant::fromValue<qulonglong>(count)},
                {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(count * sector)}
            });
            if (count > bestSize) {
                bestStart = begin;
                bestSize = count;
            }
        }
        cursor = extent.end;
    }
    const quint64 begin = alignUp(cursor * sector, Alignment) / sector;
    const quint64 end = alignDown((lastLba + 1) * sector, Alignment) / sector;
    if (end > begin) {
        const quint64 count = end - begin;
        freeExtents.append(QVariantMap{
            {QStringLiteral("startSector"), QVariant::fromValue<qulonglong>(begin)},
            {QStringLiteral("sectorCount"), QVariant::fromValue<qulonglong>(count)},
            {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(count * sector)}
        });
        if (count > bestSize) {
            bestStart = begin;
            bestSize = count;
        }
    }
    for (qsizetype index = 0; index < partitions.size(); ++index) {
        QVariantMap partition = partitions.at(index).toMap();
        if (!partition.value(QStringLiteral("shrinkable")).toBool())
            continue;
        const quint64 partitionEnd = partition.value(QStringLiteral("startSector")).toULongLong()
            + partition.value(QStringLiteral("sectorCount")).toULongLong();
        quint64 nextPartitionStart = lastLba + 1;
        for (const Extent& extent : used) {
            if (extent.begin >= partitionEnd) {
                nextPartitionStart = extent.begin;
                break;
            }
        }
        const quint64 followingFreeStart = alignUp(partitionEnd * sector, Alignment) / sector;
        const quint64 followingFreeEnd = alignDown(nextPartitionStart * sector, Alignment) / sector;
        const quint64 followingFreeBytes = followingFreeEnd > followingFreeStart
            ? (followingFreeEnd - followingFreeStart) * sector : 0;
        partition.insert(QStringLiteral("followingFreeStartSector"),
                         QVariant::fromValue<qulonglong>(followingFreeStart));
        partition.insert(QStringLiteral("followingFreeEndSector"),
                         QVariant::fromValue<qulonglong>(followingFreeEnd));
        partition.insert(QStringLiteral("followingFreeBytes"),
                         QVariant::fromValue<qulonglong>(followingFreeBytes));
        partitions[index] = partition;
    }

    for (const QVariant& value : partitions) {
        const QVariantMap partition = value.toMap();
        if (!partition.value(QStringLiteral("shrinkable")).toBool())
            continue;
        const quint64 partitionEnd = partition.value(QStringLiteral("startSector")).toULongLong()
            + partition.value(QStringLiteral("sectorCount")).toULongLong();
        if (partitionEnd != lastLba + 1 || (partitionEnd * sector) % Alignment != 0)
            continue;
        const bool alreadyListed = std::any_of(freeExtents.cbegin(), freeExtents.cend(),
            [partitionEnd](const QVariant& extentValue) {
                return extentValue.toMap().value(QStringLiteral("startSector")).toULongLong() == partitionEnd;
            });
        if (!alreadyListed) {
            freeExtents.append(QVariantMap{
                {QStringLiteral("startSector"), QVariant::fromValue<qulonglong>(partitionEnd)},
                {QStringLiteral("sectorCount"), QVariant::fromValue<qulonglong>(0)},
                {QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(0)}
            });
        }
        if (bestStart == 0)
            bestStart = partitionEnd;
    }
    info.insert(QStringLiteral("gpt"), true);
    info.insert(QStringLiteral("freeExtents"), freeExtents);
    info.insert(QStringLiteral("tableId"), table.value(QStringLiteral("id")).toString());
    info.insert(QStringLiteral("partitions"), partitions);
    info.insert(QStringLiteral("freeStartSector"), QVariant::fromValue<qulonglong>(bestStart));
    info.insert(QStringLiteral("freeSectorCount"), QVariant::fromValue<qulonglong>(bestSize));
    info.insert(QStringLiteral("freeBytes"), QVariant::fromValue<qulonglong>(bestSize * sector));
    return info;
}

StoragePreflightResult SystemDiscovery::preflightStorage(const QString& devicePath,
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
                                                         const QString& sharedResizeDevice)
{
    Q_UNUSED(partitionSizesMiB);
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
    if (partitioningMode != QStringLiteral("share") && !sharedResizeDevice.isEmpty()) {
        result.errors.append(QObject::tr("Partition shrinking is available only for alongside installation."));
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
    if (encryptRoot || encryptHome || encryptVarLib || encryptSwap)
        addMissingTool(&result.errors, QStringLiteral("cryptsetup"), QObject::tr("LUKS2 encryption"));
    if (swapChoice == QStringLiteral("Small") || swapChoice == QStringLiteral("Suspend")
        || (partitioningMode == QStringLiteral("manual")
            && manualAssignments.values().contains(QVariant(QStringLiteral("swap"))))) {
        addMissingTool(&result.errors, QStringLiteral("mkswap"), QObject::tr("swap creation"));
        addMissingTool(&result.errors, QStringLiteral("swapon"), QObject::tr("swap activation"));
    }
    if (!kernelSupports(QStringLiteral("vfat"), QStringLiteral("vfat"))
        || !kernelSupports(QStringLiteral("xfs"), QStringLiteral("xfs"))
        || !kernelSupports(QStringLiteral("f2fs"), QStringLiteral("f2fs")))
        result.errors.append(QObject::tr("The live kernel lacks support for one or more required filesystems (VFAT, XFS, F2FS)."));
    if ((encryptRoot || encryptHome || encryptVarLib || encryptSwap)
        && !kernelModuleAvailable(QStringLiteral("dm-crypt")))
        result.errors.append(QObject::tr("The live kernel does not provide device-mapper encryption support."));
    if ((encryptRoot || encryptHome || encryptVarLib || encryptSwap)
        && !QFileInfo::exists(QStringLiteral("/dev/mapper/control")))
        result.errors.append(QObject::tr("The device-mapper control interface is unavailable."));
    const bool zstdCompressionAvailable = f2fsZstdCompressionAvailable();
    const bool kernelAdvertisesCompression = QFileInfo::exists(
        QStringLiteral("/sys/fs/f2fs/features/compression"));
    if (!kernelAdvertisesCompression && !zstdCompressionAvailable)
        result.errors.append(QObject::tr("The live kernel's F2FS compression support could not be verified."));
    else if (!zstdCompressionAvailable)
        result.errors.append(QObject::tr("The live kernel's Zstandard support for F2FS compression could not be verified."));

    static const QStringList roles = {QStringLiteral("/boot/efi"), QStringLiteral("/"),
                                      QStringLiteral("/home"), QStringLiteral("/var/lib"),
                                      QStringLiteral("swap")};
    static const QRegularExpression labelPattern(QStringLiteral("^[A-Za-z0-9_]{1,11}$"));
    if (partitioningMode == QStringLiteral("manual")) {
        for (const QString& role : roles) {
            if (manualAssignments.values().contains(QVariant(role))
                && !labelPattern.match(partitionLabels.value(role).toString()).hasMatch())
                result.errors.append(QObject::tr("Enter a label of 1–11 letters, digits, or underscores for %1.").arg(role));
        }
    }
    if (!result.errors.isEmpty())
        return result;

    const quint64 configuredRootMinimum = quint64(15) * GiB + quint64(21) * (GiB / 100);
    const quint64 imageRootMinimum = result.imageRootBytes > std::numeric_limits<quint64>::max() - 600 * MiB
        ? std::numeric_limits<quint64>::max() : result.imageRootBytes + 600 * MiB;
    const quint64 rootRequired = std::max(configuredRootMinimum, imageRootMinimum);
    const quint64 homeRequired = result.imageHomeBytes > std::numeric_limits<quint64>::max() - GiB
        ? std::numeric_limits<quint64>::max() : result.imageHomeBytes + GiB;
    const quint64 varLibRequired = result.imageVarLibBytes > std::numeric_limits<quint64>::max() - GiB
        ? std::numeric_limits<quint64>::max() : result.imageVarLibBytes + GiB;

    const QVariantMap diskInfo = inspectTargetDisk(devicePath, imagePath);
    if ((partitioningMode == QStringLiteral("manual") && swapChoice != QStringLiteral("None"))
        || (partitioningMode != QStringLiteral("manual")
            && swapChoice != QStringLiteral("Small") && swapChoice != QStringLiteral("Suspend"))) {
        result.errors.append(QObject::tr("The suspend setting does not match the partitioning method."));
        return result;
    }
    if (partitioningMode == QStringLiteral("manual")) {
        const bool creatingGptTable = !diskInfo.value(QStringLiteral("gpt")).toBool();
        const QVariantList existing = creatingGptTable ? QVariantList{}
            : diskInfo.value(QStringLiteral("partitions")).toList();
        if (manualPartitionLayout.isEmpty()) {
            result.errors.append(QObject::tr("Open the partition editor and review the disk layout."));
            return result;
        }
        if (creatingGptTable && std::any_of(manualPartitionLayout.cbegin(), manualPartitionLayout.cend(),
                [](const QVariant& value) {
                    return value.toMap().value(QStringLiteral("action")).toString() != QStringLiteral("create");
                })) {
            result.errors.append(QObject::tr("A new GPT table can contain only newly created partitions."));
            return result;
        }
        if (std::any_of(manualPartitionLayout.cbegin(), manualPartitionLayout.cend(), [](const QVariant& value) {
                return value.toMap().value(QStringLiteral("action")).toString() == QStringLiteral("create");
            }))
            addMissingTool(&result.errors, QStringLiteral("wipefs"), QObject::tr("new manual partition preparation"));
        struct PlannedExtent { quint64 start; quint64 end; int number; };
        QList<PlannedExtent> extents;
        QSet<int> partitionNumbers;
        static const QRegularExpression gptLabelPattern(QStringLiteral("^[A-Za-z0-9 _.-]{0,36}$"));
        const QString diskNode = diskInfo.value(QStringLiteral("device")).toString();
        for (const QVariant& value : manualPartitionLayout) {
            if (value.metaType().id() != QMetaType::QVariantMap) {
                result.errors.append(QObject::tr("The manual partition layout contains a malformed entry."));
                return result;
            }
            const QVariantMap row = value.toMap();
            const QString action = row.value(QStringLiteral("action")).toString();
            if (row.value(QStringLiteral("device")).metaType().id() != QMetaType::QString
                || row.value(QStringLiteral("tableId")).metaType().id() != QMetaType::QString
                || row.value(QStringLiteral("label")).metaType().id() != QMetaType::QString
                || row.value(QStringLiteral("mountPoint")).metaType().id() != QMetaType::QString
                || row.value(QStringLiteral("filesystemLabel")).metaType().id() != QMetaType::QString
                || row.value(QStringLiteral("encrypted")).metaType().id() != QMetaType::Bool
                || row.value(QStringLiteral("format")).metaType().id() != QMetaType::Bool) {
                result.errors.append(QObject::tr("A manual partition contains malformed properties."));
                return result;
            }
            if (action != QStringLiteral("keep") && action != QStringLiteral("resize")
                && action != QStringLiteral("create") && action != QStringLiteral("delete")) {
                result.errors.append(QObject::tr("The manual partition layout contains an unsupported operation."));
                return result;
            }
            if (row.value(QStringLiteral("tableId")).toString()
                != diskInfo.value(QStringLiteral("tableId")).toString()) {
                result.errors.append(QObject::tr("The GPT partition table changed after the editor was opened."));
                return result;
            }
            const int number = row.value(QStringLiteral("partitionNumber")).toInt();
            const QString device = row.value(QStringLiteral("device")).toString();
            const QString expectedDevice = diskNode
                + (QRegularExpression(QStringLiteral("[0-9]$")).match(QFileInfo(diskNode).fileName()).hasMatch()
                    ? QStringLiteral("p") : QString()) + QString::number(number);
            if (number < 1 || number > 128 || device != expectedDevice) {
                result.errors.append(QObject::tr("A manual partition has an invalid device number."));
                return result;
            }
            QVariantMap originalRow;
            for (const QVariant& oldValue : existing) {
                const QVariantMap candidate = oldValue.toMap();
                if (candidate.value(QStringLiteral("device")).toString() == device) {
                    originalRow = candidate;
                    break;
                }
            }
            if (action == QStringLiteral("create")) {
                const bool replacingDeletedPartition = std::any_of(manualPartitionLayout.cbegin(),
                    manualPartitionLayout.cend(), [&device](const QVariant& candidateValue) {
                        const QVariantMap candidate = candidateValue.toMap();
                        return candidate.value(QStringLiteral("device")).toString() == device
                            && candidate.value(QStringLiteral("action")).toString() == QStringLiteral("delete");
                    });
                if ((!originalRow.isEmpty() && !replacingDeletedPartition)
                    || !row.value(QStringLiteral("uuid")).toString().isEmpty()) {
                    result.errors.append(QObject::tr("A new partition conflicts with an existing device."));
                    return result;
                }
            } else if (originalRow.isEmpty()
                || row.value(QStringLiteral("originalStartSector")).toULongLong()
                    != originalRow.value(QStringLiteral("startSector")).toULongLong()
                || row.value(QStringLiteral("originalSectorCount")).toULongLong()
                    != originalRow.value(QStringLiteral("sectorCount")).toULongLong()
                || row.value(QStringLiteral("originalUuid")).toString()
                    != originalRow.value(QStringLiteral("uuid")).toString()
                || row.value(QStringLiteral("uuid")).toString()
                    != originalRow.value(QStringLiteral("uuid")).toString()
                || row.value(QStringLiteral("originalTypeGuid")).toString()
                    .compare(originalRow.value(QStringLiteral("typeGuid")).toString(), Qt::CaseInsensitive) != 0
                || row.value(QStringLiteral("typeGuid")).toString()
                    .compare(originalRow.value(QStringLiteral("typeGuid")).toString(), Qt::CaseInsensitive) != 0
                || row.value(QStringLiteral("originalAttributes")) != originalRow.value(QStringLiteral("attributes"))
                || row.value(QStringLiteral("attributes")) != originalRow.value(QStringLiteral("attributes"))
                || row.value(QStringLiteral("originalLabel")).toString()
                    != originalRow.value(QStringLiteral("label")).toString()
                || row.value(QStringLiteral("originalFilesystem")).toString()
                    != originalRow.value(QStringLiteral("filesystem")).toString()
                || row.value(QStringLiteral("startSector")).toULongLong()
                    != originalRow.value(QStringLiteral("startSector")).toULongLong()) {
                result.errors.append(QObject::tr("An existing partition changed after the editor was opened."));
                return result;
            }
            if (action == QStringLiteral("keep")
                && row.value(QStringLiteral("sectorCount")).toULongLong()
                    != originalRow.value(QStringLiteral("sectorCount")).toULongLong()) {
                result.errors.append(QObject::tr("A kept partition cannot change size."));
                return result;
            }
            if (action == QStringLiteral("delete"))
                continue;

            const quint64 start = row.value(QStringLiteral("startSector")).toULongLong();
            const quint64 count = row.value(QStringLiteral("sectorCount")).toULongLong();
            const quint64 diskSectorCount = result.diskBytes / result.logicalBlockSize;
            if (partitionNumbers.contains(number) || count == 0 || start < 2048
                || start >= diskSectorCount || diskSectorCount - start <= 33
                || count > diskSectorCount - start - 33
                || start > std::numeric_limits<quint64>::max() - count) {
                result.errors.append(QObject::tr("A manual partition has invalid GPT geometry."));
                return result;
            }
            partitionNumbers.insert(number);
            extents.append({start, start + count, number});
            if (!gptLabelPattern.match(row.value(QStringLiteral("label")).toString()).hasMatch()) {
                result.errors.append(QObject::tr("Partition labels may contain up to 36 letters, digits, spaces, dots, dashes, or underscores."));
                return result;
            }
            if (action == QStringLiteral("create")) {
                const QString type = row.value(QStringLiteral("plannedTypeGuid"),
                    row.value(QStringLiteral("typeGuid"))).toString().toLower();
                const QString efiGuid = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
                const QString linuxGuid = QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4");
                const QString luksGuid = QStringLiteral("ca7d7ccb-63ed-4c53-861c-1742536059cc");
                const QString swapGuid = QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f");
                if (type != efiGuid && type != linuxGuid && type != luksGuid && type != swapGuid) {
                    result.errors.append(QObject::tr("New partitions must use a supported GPT partition type."));
                    return result;
                }
            }
            if (row.value(QStringLiteral("mountPoint")).toString().isEmpty()
                && (row.value(QStringLiteral("format")).toBool()
                    || row.value(QStringLiteral("encrypted")).toBool())) {
                result.errors.append(QObject::tr("Only partitions assigned to Nitrux can be formatted or encrypted."));
                return result;
            }
            if (action == QStringLiteral("resize") && !row.value(QStringLiteral("format")).toBool()) {
                const QString filesystem = row.value(QStringLiteral("originalFilesystem")).toString();
                if (filesystem == QStringLiteral("swap") && !row.value(QStringLiteral("format")).toBool()) {
                    result.errors.append(QObject::tr("A resized swap partition must be reformatted."));
                    return result;
                }
                if (filesystem != QStringLiteral("ext2") && filesystem != QStringLiteral("ext3")
                    && filesystem != QStringLiteral("ext4") && filesystem != QStringLiteral("btrfs")
                    && filesystem != QStringLiteral("ntfs") && filesystem != QStringLiteral("vfat")
                    && filesystem != QStringLiteral("swap")) {
                    result.errors.append(QObject::tr("Squiddy cannot safely resize the existing %1 filesystem.").arg(filesystem));
                    return result;
                }
                if (filesystem == QStringLiteral("ext2") || filesystem == QStringLiteral("ext3")
                    || filesystem == QStringLiteral("ext4")) {
                    addMissingTool(&result.errors, QStringLiteral("e2fsck"), QObject::tr("ext filesystem resizing"));
                    addMissingTool(&result.errors, QStringLiteral("resize2fs"), QObject::tr("ext filesystem resizing"));
                    addMissingTool(&result.errors, QStringLiteral("dumpe2fs"), QObject::tr("ext filesystem resizing"));
                } else if (filesystem == QStringLiteral("btrfs")) {
                    addMissingTool(&result.errors, QStringLiteral("btrfs"), QObject::tr("Btrfs filesystem resizing"));
                } else if (filesystem == QStringLiteral("ntfs")) {
                    addMissingTool(&result.errors, QStringLiteral("ntfsresize"), QObject::tr("NTFS filesystem resizing"));
                } else if (filesystem == QStringLiteral("vfat")) {
                    addMissingTool(&result.errors, QStringLiteral("fatresize"), QObject::tr("FAT filesystem resizing"));
                }
            }
        }
        for (const QVariant& oldValue : existing) {
            const QString oldDevice = oldValue.toMap().value(QStringLiteral("device")).toString();
            const bool represented = std::any_of(manualPartitionLayout.cbegin(), manualPartitionLayout.cend(),
                [&oldDevice](const QVariant& candidateValue) {
                    return candidateValue.toMap().value(QStringLiteral("device")).toString() == oldDevice
                        && candidateValue.toMap().value(QStringLiteral("action")).toString() != QStringLiteral("create");
                });
            if (!represented) {
                result.errors.append(QObject::tr("Every existing partition must be kept, resized, or explicitly removed."));
                return result;
            }
        }
        std::sort(extents.begin(), extents.end(), [](const PlannedExtent& left, const PlannedExtent& right) {
            return left.start < right.start;
        });
        for (qsizetype index = 1; index < extents.size(); ++index) {
            if (extents.at(index).start < extents.at(index - 1).end) {
                result.errors.append(QObject::tr("Manual partitions overlap."));
                return result;
            }
        }
        QSet<QString> selectedRoles;
        for (auto it = manualAssignments.cbegin(); it != manualAssignments.cend(); ++it) {
            const QString role = it.value().toString();
            if (!roles.contains(role) || selectedRoles.contains(role)) {
                result.errors.append(QObject::tr("Each selected partition must have a unique supported use."));
                return result;
            }
            QVariantMap entry;
            for (const QVariant& value : existing) {
                const QVariantMap candidate = value.toMap();
                if (candidate.value(QStringLiteral("device")).toString() == it.key()) {
                    entry = candidate;
                    break;
                }
            }
            QVariantMap plannedEntry;
            for (const QVariant& value : manualPartitionLayout) {
                const QVariantMap candidate = value.toMap();
                if (candidate.value(QStringLiteral("device")).toString() == it.key()
                    && candidate.value(QStringLiteral("action")).toString() != QStringLiteral("delete")) {
                    plannedEntry = candidate;
                    break;
                }
            }
            if (!plannedEntry.isEmpty()) {
                if (!entry.isEmpty()
                    && (plannedEntry.value(QStringLiteral("originalStartSector")).toULongLong()
                            != entry.value(QStringLiteral("startSector")).toULongLong()
                        || plannedEntry.value(QStringLiteral("originalSectorCount")).toULongLong()
                            != entry.value(QStringLiteral("sectorCount")).toULongLong()
                        || plannedEntry.value(QStringLiteral("originalUuid")).toString()
                            != entry.value(QStringLiteral("uuid")).toString())) {
                    result.errors.append(QObject::tr("Partition %1 changed after the editor was opened.").arg(it.key()));
                    return result;
                }
                entry = plannedEntry;
            }
            if (entry.isEmpty()) {
                result.errors.append(QObject::tr("A selected partition does not belong to the approved layout."));
                return result;
            }
            const QString type = entry.value(QStringLiteral("plannedTypeGuid"),
                entry.value(QStringLiteral("typeGuid"))).toString().toLower();
            const QString efiGuid = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
            const QString linuxGuid = QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4");
            const QString luksGuid = QStringLiteral("ca7d7ccb-63ed-4c53-861c-1742536059cc");
            const QString swapGuid = QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f");
            const QString action = plannedEntry.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("create") && type != efiGuid && type != linuxGuid
                && type != luksGuid && type != swapGuid) {
                result.errors.append(QObject::tr("New partitions must use a supported GPT partition type."));
                return result;
            }
            if ((role == QStringLiteral("/boot/efi") && type != efiGuid)
                || (role == QStringLiteral("swap") && type != swapGuid && type != luksGuid)
                || (role == QStringLiteral("/") && type != linuxGuid && type != luksGuid)
                || ((role == QStringLiteral("/home") || role == QStringLiteral("/var/lib"))
                    && type != linuxGuid && type != luksGuid)) {
                result.errors.append(QObject::tr("The GPT partition type does not match the selected use for %1.").arg(it.key()));
                return result;
            }
            const quint64 size = plannedEntry.isEmpty()
                ? entry.value(QStringLiteral("sizeBytes")).toULongLong()
                : plannedEntry.value(QStringLiteral("sectorCount")).toULongLong() * result.logicalBlockSize;
            const quint64 required = role == QStringLiteral("/boot/efi") ? 100 * MiB
                : role == QStringLiteral("/") ? rootRequired
                : role == QStringLiteral("/home") ? homeRequired
                : role == QStringLiteral("/var/lib") ? varLibRequired : MiB;
            if (size < required)
                result.errors.append(QObject::tr("The selected %1 partition is too small.").arg(role));
            const QString filesystem = role == QStringLiteral("/boot/efi") ? QStringLiteral("vfat")
                : role == QStringLiteral("/") ? QStringLiteral("xfs")
                : role == QStringLiteral("swap") ? QStringLiteral("swap") : QStringLiteral("f2fs");
            const bool encrypted = role == QStringLiteral("/") ? encryptRoot
                : role == QStringLiteral("/home") ? encryptHome
                : role == QStringLiteral("/var/lib") ? encryptVarLib
                : role == QStringLiteral("swap") ? encryptSwap : false;
            if (!plannedEntry.isEmpty() && !plannedEntry.value(QStringLiteral("format")).toBool()) {
                result.errors.append(QObject::tr("Partitions assigned to Nitrux must be formatted."));
                return result;
            }
            if (type == luksGuid && !encrypted) {
                result.errors.append(QObject::tr("Keep encryption enabled for the selected LUKS partition %1.").arg(it.key()));
                return result;
            }
            QString suffix = it.key().mid(devicePath.size());
            if (suffix.startsWith(QLatin1Char('p'))) suffix.remove(0, 1);
            bool numberOk = false;
            const int number = plannedEntry.isEmpty()
                ? suffix.toInt(&numberOk)
                : plannedEntry.value(QStringLiteral("partitionNumber")).toInt(&numberOk);
            if (!numberOk || number < 1 || number > 128) {
                result.errors.append(QObject::tr("A selected partition number is invalid."));
                return result;
            }
            const QString plannedFilesystem = encrypted
                ? (role == QStringLiteral("/") ? QStringLiteral("LUKS2 + XFS")
                    : role == QStringLiteral("swap") ? QStringLiteral("LUKS2 + Linuxswap")
                    : QStringLiteral("LUKS2 + F2FS"))
                : filesystem;
            const QString targetLabel = plannedEntry.isEmpty()
                ? partitionLabels.value(role).toString()
                : plannedEntry.value(QStringLiteral("filesystemLabel")).toString();
            const quint64 startSector = plannedEntry.isEmpty()
                ? entry.value(QStringLiteral("startSector")).toULongLong()
                : plannedEntry.value(QStringLiteral("startSector")).toULongLong();
            QVariantMap row = partitionRow(number, type, targetLabel, role,
                                           plannedFilesystem,
                                           startSector * result.logicalBlockSize,
                                           size, result.logicalBlockSize, 0, required, encrypted,
                                           role == QStringLiteral("/home") || role == QStringLiteral("/var/lib"));
            row.insert(QStringLiteral("device"), it.key());
            row.insert(QStringLiteral("action"), QStringLiteral("format"));
            row.insert(QStringLiteral("filesystemLabel"), targetLabel);
            row.insert(QStringLiteral("originalSizeBytes"), entry.value(QStringLiteral("sizeBytes")));
            result.layout.append(row);
            selectedRoles.insert(role);
        }
        for (const QString& role : {QStringLiteral("/boot/efi"), QStringLiteral("/")}) {
            if (!selectedRoles.contains(role))
                result.errors.append(QObject::tr("Choose a partition for %1.").arg(role));
        }
        const quint64 additionalHome = selectedRoles.contains(QStringLiteral("/home")) ? 0 : homeRequired;
        const quint64 additionalVar = selectedRoles.contains(QStringLiteral("/var/lib")) ? 0 : varLibRequired;
        const quint64 swapFileAllowance = swapChoice == QStringLiteral("File") ? 512 * MiB : 0;
        const QVariantMap rootRow = [&result] {
            for (const QVariant& value : result.layout) {
                if (value.toMap().value(QStringLiteral("mountPoint")).toString() == QStringLiteral("/"))
                    return value.toMap();
            }
            return QVariantMap{};
        }();
        if (!rootRow.isEmpty() && (rootRequired > std::numeric_limits<quint64>::max() - additionalHome
            || rootRequired + additionalHome > std::numeric_limits<quint64>::max() - additionalVar
            || rootRequired + additionalHome + additionalVar > std::numeric_limits<quint64>::max() - swapFileAllowance
            || rootRow.value(QStringLiteral("sizeBytes")).toULongLong()
                < rootRequired + additionalHome + additionalVar + swapFileAllowance))
            result.errors.append(QObject::tr("The root partition is too small for directories kept on it."));
        if ((swapChoice == QStringLiteral("Small") || swapChoice == QStringLiteral("Suspend"))
            && !selectedRoles.contains(QStringLiteral("swap")))
            result.errors.append(QObject::tr("Choose a partition for swap or select file or no swap."));
        if (selectedRoles.contains(QStringLiteral("swap"))) {
            const quint64 memory = ramBytes();
            const quint64 minimumSwap = partitioningMode == QStringLiteral("manual") ? MiB
                : swapChoice == QStringLiteral("Suspend")
                ? (memory > std::numeric_limits<quint64>::max() - memory / 10
                    ? std::numeric_limits<quint64>::max() : memory + memory / 10)
                : MiB;
            for (const QVariant& value : result.layout) {
                const QVariantMap row = value.toMap();
                if (row.value(QStringLiteral("mountPoint")).toString() == QStringLiteral("swap")
                    && ((partitioningMode != QStringLiteral("manual") && memory == 0)
                        || row.value(QStringLiteral("sizeBytes")).toULongLong() < minimumSwap))
                    result.errors.append(QObject::tr("The selected swap partition is too small for this swap setting."));
            }
        }
        if (selectedRoles.contains(QStringLiteral("swap"))
            && partitioningMode != QStringLiteral("manual")
            && swapChoice != QStringLiteral("Small") && swapChoice != QStringLiteral("Suspend"))
            result.errors.append(QObject::tr("The selected swap partition does not match the swap setting."));
        result.valid = result.errors.isEmpty();
        return result;
    }
    if (partitioningMode != QStringLiteral("automated") && partitioningMode != QStringLiteral("share")) {
        result.errors.append(QObject::tr("The partitioning mode is invalid."));
        return result;
    }

    const quint64 sector = result.logicalBlockSize;
    const quint64 gptReservation = sector * 34;
    quint64 usableEnd = alignDown(result.diskBytes - gptReservation, Alignment);
    quint64 efiStart = alignUp(2 * MiB, Alignment);
    const quint64 efiSize = 100 * MiB;
    QVariantMap resizeRow;
    if (partitioningMode == QStringLiteral("share")) {
        if (!diskInfo.value(QStringLiteral("gpt")).toBool()) {
            result.errors.append(QObject::tr("Sharing a disk requires an existing GPT partition table."));
            return result;
        }
        quint64 freeStart = 0;
        quint64 freeBytes = 0;
        QVariantMap sourcePartition;
        if (!sharedResizeDevice.isEmpty()) {
            for (const QVariant& value : diskInfo.value(QStringLiteral("partitions")).toList()) {
                const QVariantMap partition = value.toMap();
                if (partition.value(QStringLiteral("device")).toString() == sharedResizeDevice) {
                    sourcePartition = partition;
                    break;
                }
            }
            freeStart = sourcePartition.value(QStringLiteral("followingFreeStartSector")).toULongLong();
            freeBytes = sourcePartition.value(QStringLiteral("followingFreeBytes")).toULongLong();
        } else {
            const quint64 selectedStart = sharedSpaceStartSector == 0
                ? diskInfo.value(QStringLiteral("freeStartSector")).toULongLong()
                : sharedSpaceStartSector;
            for (const QVariant& value : diskInfo.value(QStringLiteral("freeExtents")).toList()) {
                const QVariantMap extent = value.toMap();
                const quint64 start = extent.value(QStringLiteral("startSector")).toULongLong();
                if (start == selectedStart) {
                    freeStart = start;
                    freeBytes = extent.value(QStringLiteral("sizeBytes")).toULongLong();
                    break;
                }
            }
        }
        const quint64 selectedStart = sharedSpaceStartSector == 0
            ? freeStart : sharedSpaceStartSector;
        if (freeStart == 0 || selectedStart != freeStart) {
            result.errors.append(QObject::tr("The selected space or partition changed during preflight."));
            return result;
        }

        quint64 installAreaBytes = sharedSpaceMiB == 0 ? freeBytes : 0;
        if (sharedSpaceMiB > 0) {
            if (sharedSpaceMiB > std::numeric_limits<quint64>::max() / MiB) {
                result.errors.append(QObject::tr("The requested installation area is too large."));
                return result;
            }
            installAreaBytes = sharedSpaceMiB * MiB;
        }
        if (installAreaBytes < 100 * MiB) {
            result.errors.append(QObject::tr("The selected free area cannot hold the installation layout."));
            return result;
        }

        if (!sharedResizeDevice.isEmpty()) {
            const quint64 sourceStart = sourcePartition.value(QStringLiteral("startSector")).toULongLong();
            const quint64 sourceBytes = sourcePartition.value(QStringLiteral("sizeBytes")).toULongLong();
            const QString filesystem = sourcePartition.value(QStringLiteral("filesystem")).toString();
            const bool canShrink = sourcePartition.value(QStringLiteral("shrinkable")).toBool();
            if (sourcePartition.isEmpty() || !canShrink) {
                result.errors.append(QObject::tr("Choose a partition with a supported shrink operation."));
                return result;
            }
            const quint64 sourceStartBytes = sourceStart * sector;
            const quint64 sourceEndBytes = sourceStartBytes + sourceBytes;
            quint64 newPartitionEndBytes = sourceEndBytes;
            quint64 reclaimBytes = 0;
            if (installAreaBytes > freeBytes) {
                const quint64 neededFromPartition = installAreaBytes - freeBytes;
                if (neededFromPartition > sourceBytes - std::min(sourceBytes, 2 * GiB)) {
                    result.errors.append(QObject::tr("The requested installation area would leave too little space in the selected partition."));
                    return result;
                }
                newPartitionEndBytes = alignDown(sourceEndBytes - neededFromPartition, Alignment);
                if (newPartitionEndBytes <= sourceStartBytes
                    || newPartitionEndBytes - sourceStartBytes < 2 * GiB) {
                    result.errors.append(QObject::tr("The requested installation area would leave too little space in the selected partition."));
                    return result;
                }
                reclaimBytes = sourceEndBytes - newPartitionEndBytes;
            }
            const quint64 availableEndBytes = sourcePartition.value(
                QStringLiteral("followingFreeEndSector")).toULongLong() * sector;
            efiStart = reclaimBytes > 0 ? newPartitionEndBytes : freeStart * sector;
            if (efiStart > availableEndBytes || installAreaBytes > availableEndBytes - efiStart) {
                result.errors.append(QObject::tr("The requested installation area overlaps the next partition or the end of the disk."));
                return result;
            }
            if (reclaimBytes > 0) {
                if (filesystem == QStringLiteral("btrfs"))
                    addMissingTool(&result.errors, QStringLiteral("btrfs"), QObject::tr("Btrfs filesystem shrinking"));
                else if (filesystem == QStringLiteral("ext2") || filesystem == QStringLiteral("ext3")
                         || filesystem == QStringLiteral("ext4")) {
                    addMissingTool(&result.errors, QStringLiteral("e2fsck"), QObject::tr("ext filesystem checking"));
                    addMissingTool(&result.errors, QStringLiteral("resize2fs"), QObject::tr("ext filesystem shrinking"));
                } else if (filesystem == QStringLiteral("ntfs")) {
                    addMissingTool(&result.errors, QStringLiteral("ntfsresize"), QObject::tr("NTFS filesystem shrinking"));
                } else if (filesystem == QStringLiteral("vfat")) {
                    addMissingTool(&result.errors, QStringLiteral("fatresize"), QObject::tr("FAT filesystem shrinking"));
                    if (newPartitionEndBytes - sourceStartBytes < 512 * MiB)
                        result.errors.append(QObject::tr("FAT32 partitions cannot be shrunk below 512 MiB."));
                }
            }
            const quint64 newSizeBytes = sourceBytes - reclaimBytes;
            QString suffix = sharedResizeDevice.mid(devicePath.size());
            if (suffix.startsWith(QLatin1Char('p'))) suffix.remove(0, 1);
            bool numberOk = false;
            const int number = suffix.toInt(&numberOk);
            if (!numberOk || number < 1 || number > 128) {
                result.errors.append(QObject::tr("The selected partition number is invalid."));
                return result;
            }
            resizeRow = partitionRow(number, sourcePartition.value(QStringLiteral("typeGuid")).toString(),
                sourcePartition.value(QStringLiteral("label")).toString(), QStringLiteral("existing"),
                filesystem, sourceStartBytes, newSizeBytes, sector, 0, sourceBytes, false, false);
            resizeRow.insert(QStringLiteral("device"), sharedResizeDevice);
            resizeRow.insert(QStringLiteral("action"), QStringLiteral("resize"));
            resizeRow.insert(QStringLiteral("originalSizeBytes"), QVariant::fromValue<qulonglong>(sourceBytes));
            resizeRow.insert(QStringLiteral("reclaimBytes"), QVariant::fromValue<qulonglong>(reclaimBytes));
        } else if (installAreaBytes > freeBytes) {
            result.errors.append(QObject::tr("The requested installation area exceeds the selected unallocated space. Choose a partition to shrink."));
            return result;
        } else {
            efiStart = freeStart * sector;
        }
        usableEnd = efiStart + installAreaBytes;
    }

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

    const QList<NitruxPartitionTarget> nitruxTargets = nitruxPartitionTargets();
    const auto targetForRole = [&nitruxTargets](const QString& role) {
        for (const NitruxPartitionTarget& target : nitruxTargets) {
            if (target.mountPoint == role)
                return target;
        }
        return NitruxPartitionTarget{};
    };
    const quint64 dataBytes = dataEnd - dataStart;
    const NitruxPartitionTarget rootTarget = targetForRole(QStringLiteral("/"));
    const NitruxPartitionTarget homeTarget = targetForRole(QStringLiteral("/home"));
    const NitruxPartitionTarget varLibTarget = targetForRole(QStringLiteral("/var/lib"));
    const quint64 rootSize = alignDown((dataBytes / 100) * rootTarget.percent
        + (dataBytes % 100) * rootTarget.percent / 100, Alignment);
    const quint64 homeSize = alignDown((dataBytes / 100) * homeTarget.percent
        + (dataBytes % 100) * homeTarget.percent / 100, Alignment);
    const quint64 varLibSize = dataBytes - rootSize - homeSize;
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
    const QString luksGuid = QStringLiteral("ca7d7ccb-63ed-4c53-861c-1742536059cc");
    result.layout.append(partitionRow(2, encryptRoot ? luksGuid : linuxFilesystemGuid, rootTarget.label,
                                      rootTarget.mountPoint,
                                      encryptRoot ? QStringLiteral("LUKS2 + XFS") : rootTarget.filesystem,
                                      dataStart, rootSize, sector, result.imageRootBytes,
                                      rootRequired, encryptRoot, false));
    result.layout.append(partitionRow(3, encryptHome ? luksGuid : linuxFilesystemGuid, homeTarget.label,
                                      homeTarget.mountPoint,
                                      encryptHome ? QStringLiteral("LUKS2 + F2FS") : homeTarget.filesystem,
                                      dataStart + rootSize, homeSize, sector, result.imageHomeBytes,
                                      homeRequired, encryptHome, true));
    result.layout.append(partitionRow(4, encryptVarLib ? luksGuid : linuxFilesystemGuid, varLibTarget.label,
                                      varLibTarget.mountPoint,
                                      encryptVarLib ? QStringLiteral("LUKS2 + F2FS") : varLibTarget.filesystem,
                                      dataStart + rootSize + homeSize, varLibSize, sector, result.imageVarLibBytes,
                                      varLibRequired, encryptVarLib, true));
    if (swapSize > 0)
        result.layout.append(partitionRow(5, encryptSwap ? luksGuid : QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"),
                                          QStringLiteral("SWAP"), QStringLiteral("swap"),
                                          encryptSwap ? QStringLiteral("LUKS2 + Linuxswap") : QStringLiteral("swap"),
                                          swapStart, swapSize, sector,
                                          0, 0, encryptSwap, false));
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
            {QStringLiteral("encrypted"), encryptRoot},
            {QStringLiteral("compressed"), false},
            {QStringLiteral("note"), QObject::tr("Created within the root filesystem.")}
        });
    }

    QSet<int> usedNumbers;
    if (partitioningMode == QStringLiteral("share")) {
        for (const QVariant& entry : diskInfo.value(QStringLiteral("partitions")).toList()) {
            QString suffix = entry.toMap().value(QStringLiteral("device")).toString().mid(devicePath.size());
            if (suffix.startsWith(QLatin1Char('p'))) suffix.remove(0, 1);
            bool ok = false;
            const int number = suffix.toInt(&ok);
            if (!ok || number < 1 || number > 128) {
                result.errors.append(QObject::tr("An existing GPT partition number could not be read."));
                return result;
            }
            usedNumbers.insert(number);
        }
    }
    for (QVariant& value : result.layout) {
        QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("action")).toString() == QStringLiteral("resize")
            || row.value(QStringLiteral("partitionNumber")).toInt() <= 0)
            continue;
        int number = row.value(QStringLiteral("partitionNumber")).toInt();
        if (partitioningMode == QStringLiteral("share")) {
            number = 1;
            while (usedNumbers.contains(number) && number <= 128) ++number;
            if (number > 128) {
                result.errors.append(QObject::tr("The GPT partition table has no free entries."));
                return result;
            }
            usedNumbers.insert(number);
            row.insert(QStringLiteral("partitionNumber"), number);
        }
        const QString partitionDevice = devicePath
            + (QFileInfo(devicePath).fileName().back().isDigit() ? QStringLiteral("p") : QString())
            + QString::number(number);
        row.insert(QStringLiteral("device"), partitionDevice);
        row.insert(QStringLiteral("action"), QStringLiteral("create"));
        value = row;
    }
    if (!resizeRow.isEmpty())
        result.layout.prepend(resizeRow);
    result.valid = result.errors.isEmpty();
    return result;
}
