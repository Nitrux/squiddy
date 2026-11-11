// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "SystemStatus.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QLocale>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

namespace {

QString readText(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text)
        ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

quint64 totalRamBytes()
{
    QProcess process;
    process.start(QStringLiteral("free"));
    if (process.waitForStarted(1000)
        && process.waitForFinished(1000)
        && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0) {
        const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            if (!line.startsWith(QStringLiteral("Mem:")))
                continue;

            const QStringList fields = line.split(
                QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
            if (fields.size() >= 3) {
                bool totalValid = false;
                bool usedValid = false;
                const double total = fields.at(1).toDouble(&totalValid);
                const double used = fields.at(2).toDouble(&usedValid);
                if (totalValid && usedValid && total > 0.0 && used >= 0.0)
                    return static_cast<quint64>(total * 1024.0);
            }
            break;
        }
    }

    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const qsizetype separator = line.indexOf(':');
        if (separator < 0 || line.left(separator).trimmed() != "MemTotal")
            continue;

        const QByteArray valueField = line.mid(separator + 1).trimmed().split(' ').value(0);
        bool valid = false;
        const quint64 kibibytes = valueField.toULongLong(&valid);
        return valid ? kibibytes * 1024 : 0;
    }
    return 0;
}

struct PowerState {
    QString source;
    int batteryPercent = -1;
};

PowerState currentPowerState()
{
    bool batteryPresent = false;
    bool externalOnline = false;
    bool batteryCharging = false;
    int batteryPercent = -1;
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    for (const QString& entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = supplies.absoluteFilePath(entry);
        if (readText(path + QStringLiteral("/type")) == QStringLiteral("Battery")) {
            if (readText(path + QStringLiteral("/present")) == QStringLiteral("0"))
                continue;

            batteryPresent = true;
            const QString status = readText(path + QStringLiteral("/status"));
            batteryCharging = batteryCharging || status == QStringLiteral("Charging")
                || status == QStringLiteral("Full")
                || status == QStringLiteral("Not charging");
            bool valid = false;
            const int capacity = readText(path + QStringLiteral("/capacity")).toInt(&valid);
            if (valid && capacity >= 0 && capacity <= 100
                && (batteryPercent < 0 || capacity < batteryPercent))
                batteryPercent = capacity;
        } else if (readText(path + QStringLiteral("/online")) == QStringLiteral("1")) {
            externalOnline = true;
        }
    }

    const bool onBattery = batteryPresent && !externalOnline && !batteryCharging;
    return {onBattery ? QStringLiteral("battery") : QStringLiteral("external"), batteryPercent};
}

} // namespace

SystemStatus::SystemStatus(QObject* parent)
    : QObject(parent)
{
    const quint64 ramBytes = totalRamBytes();
    if (ramBytes > 0)
        m_ramCapacity = QLocale().formattedDataSize(
            static_cast<qint64>(ramBytes), 1, QLocale::DataSizeIecFormat);
    constexpr quint64 minimumRamBytes = quint64(4) * 1024 * 1024 * 1024;
    m_lowRam = ramBytes > 0 && ramBytes < minimumRamBytes;

    connect(&m_timer, &QTimer::timeout, this, &SystemStatus::refresh);
    refresh();
    m_timer.start(5000);
}

void SystemStatus::refresh()
{
    const PowerState power = currentPowerState();
    bool networkConnected = false;
    QString networkType = QStringLiteral("none");
    QString networkIconName = QStringLiteral("network-offline");
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (flags & QNetworkInterface::IsLoopBack)
            continue;

        const QString devicePath = QStringLiteral("/sys/class/net/") + interface.name();
        const bool wifi = interface.type() == QNetworkInterface::Wifi
            || QDir(devicePath + QStringLiteral("/wireless")).exists()
            || QFileInfo::exists(devicePath + QStringLiteral("/phy80211"));
        const bool ethernet = interface.type() == QNetworkInterface::Ethernet
            && QFileInfo::exists(devicePath + QStringLiteral("/device"));
        if (!wifi && !ethernet)
            continue;

        const QString adapterType = wifi ? QStringLiteral("wifi") : QStringLiteral("ethernet");
        const QString adapterIcon = wifi ? QStringLiteral("network-wireless")
                                         : QStringLiteral("network-wired");
        const bool connected = (flags & QNetworkInterface::IsUp)
            && (flags & QNetworkInterface::IsRunning)
            && !interface.addressEntries().isEmpty();
        if (connected) {
            networkConnected = true;
            networkType = adapterType;
            networkIconName = adapterIcon;
            break;
        }
        if (networkType == QStringLiteral("none") || wifi) {
            networkType = adapterType;
            networkIconName = adapterIcon;
        }
    }

    if (m_powerSource == power.source && m_batteryPercent == power.batteryPercent
        && m_networkConnected == networkConnected && m_networkType == networkType
        && m_networkIconName == networkIconName)
        return;

    m_powerSource = power.source;
    m_batteryPercent = power.batteryPercent;
    m_networkConnected = networkConnected;
    m_networkType = networkType;
    m_networkIconName = networkIconName;
    Q_EMIT changed();
}
