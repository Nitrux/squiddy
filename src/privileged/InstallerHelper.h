// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#pragma once

#include <QDBusContext>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

class InstallerHelper final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.nitrux.Squiddy1")

public:
    explicit InstallerHelper(QObject* parent = nullptr);

public slots:
    QString AuthorizeInstall();
    QString SubmitPlan(const QString& token, const QVariantMap& payload);

signals:
    void InstallationProgressChanged(int percent, const QString& phase, const QString& detail,
                                    bool finished, bool success);

private:
    struct AuthorizationGrant {
        QString caller;
        QDateTime expiresAt;
    };

    QHash<QString, AuthorizationGrant> m_authorizationGrants;
    bool m_installationActive = false;
};
