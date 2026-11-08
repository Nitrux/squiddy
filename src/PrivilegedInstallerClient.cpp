// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "PrivilegedInstallerClient.h"

#include "InstallationPlan.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {

const QString serviceName = QStringLiteral("org.nitrux.Squiddy");
const QString objectPath = QStringLiteral("/org/nitrux/Squiddy");
const QString interfaceName = QStringLiteral("org.nitrux.Squiddy1");

} // namespace

PrivilegedInstallerClient::PrivilegedInstallerClient(InstallationPlanController* plan, QObject* parent)
    : QObject(parent)
    , m_plan(plan)
{
    QDBusConnection::systemBus().connect(serviceName, objectPath, interfaceName,
        QStringLiteral("InstallationProgressChanged"), this,
        SLOT(receiveProgress(int,QString,QString,bool,bool)));
}

bool PrivilegedInstallerClient::busy() const
{
    return m_busy;
}

QString PrivilegedInstallerClient::statusMessage() const { return m_statusMessage; }
int PrivilegedInstallerClient::progressPercent() const { return m_progressPercent; }
QString PrivilegedInstallerClient::progressPhase() const { return m_progressPhase; }
QString PrivilegedInstallerClient::progressDetail() const { return m_progressDetail; }
QStringList PrivilegedInstallerClient::progressLog() const { return m_progressLog; }
bool PrivilegedInstallerClient::installationFinished() const { return m_installationFinished; }
bool PrivilegedInstallerClient::installationSucceeded() const { return m_installationSucceeded; }

void PrivilegedInstallerClient::beginInstall()
{
    if (m_busy)
        return;

    if (!m_plan || !m_plan->validateAndBuildPlan()) {
        setStatusMessage(tr("The installation plan is incomplete or invalid."));
        return;
    }

    const QVariantMap payload = m_plan->validatedPlanMap();
    if (payload.isEmpty()) {
        setStatusMessage(tr("The validated installation plan is unavailable."));
        return;
    }

    m_busy = true;
    m_installationFinished = false;
    m_installationSucceeded = false;
    m_progressPercent = 0;
    m_progressPhase.clear();
    m_progressDetail.clear();
    m_progressLog.clear();
    setStatusMessage(tr("Requesting authorization…"));
    emit statusChanged();

    auto* interface = new QDBusInterface(serviceName, objectPath, interfaceName,
                                          QDBusConnection::systemBus(), this);
    if (!interface->isValid()) {
        m_busy = false;
        setStatusMessage(tr("The privileged installer service is unavailable."));
        interface->deleteLater();
        return;
    }

    auto* watcher = new QDBusPendingCallWatcher(interface->asyncCall(QStringLiteral("AuthorizeInstall")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, interface, payload] {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        interface->deleteLater();

        if (reply.isError()) {
            m_busy = false;
            setStatusMessage(tr("Authorization failed: %1").arg(reply.error().message()));
            return;
        }

        const QString token = reply.value();
        if (token.isEmpty()) {
            m_busy = false;
            setStatusMessage(tr("Authorization was denied or cancelled."));
            return;
        }

        submitPlan(token, payload);
    });
}

void PrivilegedInstallerClient::submitPlan(const QString& authorizationToken, const QVariantMap& payload)
{
    auto* interface = new QDBusInterface(serviceName, objectPath, interfaceName,
                                          QDBusConnection::systemBus(), this);
    auto* watcher = new QDBusPendingCallWatcher(
        interface->asyncCall(QStringLiteral("SubmitPlan"), authorizationToken, payload), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, interface] {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        interface->deleteLater();
        if (reply.isError()) {
            m_busy = false;
            setStatusMessage(tr("The privileged helper rejected the plan: %1").arg(reply.error().message()));
            return;
        }

        setStatusMessage(reply.value());
    });
}

void PrivilegedInstallerClient::receiveProgress(int percent, const QString& phase,
                                                const QString& detail, bool finished, bool success)
{
    m_progressPercent = qBound(0, percent, 100);
    m_progressPhase = phase;
    m_progressDetail = detail;
    const QString logLine = phase + QStringLiteral(": ") + detail;
    if (m_progressLog.isEmpty() || m_progressLog.constLast() != logLine)
        m_progressLog.append(logLine);
    if (finished) {
        m_busy = false;
        m_installationFinished = true;
        m_installationSucceeded = success;
        setStatusMessage(success ? tr("Installation completed.") : tr("Installation stopped: %1").arg(detail));
    }
    emit statusChanged();
}

void PrivilegedInstallerClient::setStatusMessage(const QString& message)
{
    if (m_statusMessage == message && !m_busy)
        return;
    m_statusMessage = message;
    emit statusChanged();
}
