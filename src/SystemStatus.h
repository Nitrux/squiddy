// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class SystemStatus final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString powerSource READ powerSource NOTIFY changed)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY changed)
    Q_PROPERTY(QString ramCapacity READ ramCapacity CONSTANT)
    Q_PROPERTY(bool lowRam READ lowRam CONSTANT)
    Q_PROPERTY(bool networkConnected READ networkConnected NOTIFY changed)
    Q_PROPERTY(QString networkType READ networkType NOTIFY changed)
    Q_PROPERTY(QString networkIconName READ networkIconName NOTIFY changed)

public:
    explicit SystemStatus(QObject* parent = nullptr);

    QString powerSource() const { return m_powerSource; }
    int batteryPercent() const { return m_batteryPercent; }
    QString ramCapacity() const { return m_ramCapacity; }
    bool lowRam() const { return m_lowRam; }
    bool networkConnected() const { return m_networkConnected; }
    QString networkType() const { return m_networkType; }
    QString networkIconName() const { return m_networkIconName; }

Q_SIGNALS:
    void changed();

private:
    void refresh();

    QTimer m_timer;
    QString m_powerSource;
    int m_batteryPercent = -1;
    QString m_ramCapacity;
    bool m_lowRam = false;
    bool m_networkConnected = false;
    QString m_networkType = QStringLiteral("none");
    QString m_networkIconName = QStringLiteral("network-offline");
};
