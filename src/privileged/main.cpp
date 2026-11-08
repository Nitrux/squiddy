// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include "InstallerHelper.h"

#include <QCoreApplication>
#include <QDBusConnection>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Squiddy Installer Helper"));

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected() || !bus.registerService(QStringLiteral("org.nitrux.Squiddy")))
        return 1;

    InstallerHelper helper;
    if (!bus.registerObject(QStringLiteral("/org/nitrux/Squiddy"),
                            &helper,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals))
        return 1;

    return app.exec();
}
