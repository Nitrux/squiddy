// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDate>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlContext>
#include <QQmlApplicationEngine>

#include <KAboutData>
#include <MauiKit4/Core/mauiapp.h>

#include "InstallationPlan.h"
#include "PrivilegedInstallerClient.h"
#include "SystemDiscovery.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Nitrux installer"));
    parser.addHelpOption();
    const QCommandLineOption debugOption(
        QStringList{QStringLiteral("debug")},
        QStringLiteral("Enable safe simulated installation steps."));
    parser.addOption(debugOption);
    parser.process(app);
    app.setOrganizationName(QStringLiteral("Nitrux"));
    app.setApplicationName(QStringLiteral("Squiddy"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    const QIcon appIcon = QIcon::fromTheme(QStringLiteral("system-software-install"));
    app.setWindowIcon(appIcon);

    KAboutData about(QStringLiteral("squiddy"),
                     QStringLiteral("Squiddy"),
                     QStringLiteral("0.1.0"),
                     QStringLiteral("A Nitrux installer built with MauiKit."),
                     KAboutLicense::BSD_3_Clause,
                     QStringLiteral("© %1 Made by Nitrux | Built with MauiKit")
                         .arg(QDate::currentDate().year()),
                     QString::fromUtf8(GIT_BRANCH) + QStringLiteral("/") + QString::fromUtf8(GIT_COMMIT_HASH));

    about.addAuthor(QStringLiteral("Uri Herrera"),
                    QStringLiteral("Developer"),
                    QStringLiteral("uri_herrera@nxos.org"));
    about.setHomepage(QStringLiteral("https://nxos.org"));
    about.setProductName(QByteArrayLiteral("nitrux/squiddy"));
    about.setOrganizationDomain(QByteArrayLiteral("nxos.org"));
    about.setDesktopFileName(QStringLiteral("org.nxos.squiddy"));
    about.setProgramLogo(appIcon);
    KAboutData::setApplicationData(about);

    MauiApp::instance()->setIconName(QStringLiteral("system-software-install"));

    InstallationPlanController installationPlan(parser.isSet(debugOption));
    if (!installationPlan.debugMode()) {
        const SystemDiscoveryResults discoveredSystem = SystemDiscovery::discover();
        installationPlan.setUefiEnvironmentDetected(discoveredSystem.uefiEnvironmentDetected);
        installationPlan.setDetectedImage(discoveredSystem.imagePath, discoveredSystem.imageFormat);
        installationPlan.setAvailableLanguages(discoveredSystem.languages);
        installationPlan.setAvailableLocales(discoveredSystem.locales);
        installationPlan.setAvailableTimeZones(discoveredSystem.timeZones);
        installationPlan.setAvailableKeyboardLayouts(discoveredSystem.keyboardLayouts);
        installationPlan.setAvailableTargetDisks(discoveredSystem.targetDisks);
        installationPlan.setLanguage(discoveredSystem.defaultLanguage);
        installationPlan.setLocale(discoveredSystem.defaultLocale);
        installationPlan.setTimeZone(discoveredSystem.defaultTimeZone);
        installationPlan.setKeyboardLayout(discoveredSystem.defaultKeyboardLayout);
    }
    PrivilegedInstallerClient privilegedInstaller(&installationPlan);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("installationPlan"), &installationPlan);
    engine.rootContext()->setContextProperty(QStringLiteral("privilegedInstaller"), &privilegedInstaller);
    engine.loadFromModule(QStringLiteral("org.nitrux.squiddy"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
