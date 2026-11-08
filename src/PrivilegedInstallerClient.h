// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class InstallationPlanController;

class PrivilegedInstallerClient final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY statusChanged)
    Q_PROPERTY(QString progressPhase READ progressPhase NOTIFY statusChanged)
    Q_PROPERTY(QString progressDetail READ progressDetail NOTIFY statusChanged)
    Q_PROPERTY(QStringList progressLog READ progressLog NOTIFY statusChanged)
    Q_PROPERTY(bool installationFinished READ installationFinished NOTIFY statusChanged)
    Q_PROPERTY(bool installationSucceeded READ installationSucceeded NOTIFY statusChanged)

public:
    explicit PrivilegedInstallerClient(InstallationPlanController* plan, QObject* parent = nullptr);

    bool busy() const;
    QString statusMessage() const;
    int progressPercent() const;
    QString progressPhase() const;
    QString progressDetail() const;
    QStringList progressLog() const;
    bool installationFinished() const;
    bool installationSucceeded() const;

    Q_INVOKABLE void beginInstall();

Q_SIGNALS:
    void statusChanged();

private Q_SLOTS:
    void receiveProgress(int percent, const QString& phase, const QString& detail,
                         bool finished, bool success);

private:
    void setStatusMessage(const QString& message);
    void submitPlan(const QString& authorizationToken, const QVariantMap& payload);

    InstallationPlanController* m_plan = nullptr;
    bool m_busy = false;
    QString m_statusMessage;
    int m_progressPercent = 0;
    QString m_progressPhase;
    QString m_progressDetail;
    QStringList m_progressLog;
    bool m_installationFinished = false;
    bool m_installationSucceeded = false;
};
