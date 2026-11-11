// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDate>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include <QStringView>
#include <QUrl>
#include <QXmlStreamReader>

#include <KAboutData>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <MauiKit4/Core/mauiapp.h>

#include "InstallationPlan.h"
#include "PrivilegedInstallerClient.h"
#include "SystemDiscovery.h"
#include "SystemStatus.h"

namespace {

QString versionFromOsRelease()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.startsWith(QStringLiteral("VERSION_ID=")))
            continue;

        QString version = line.mid(sizeof("VERSION_ID=") - 1).trimmed();
        if (version.size() >= 2
            && ((version.front() == QLatin1Char('"') && version.back() == QLatin1Char('"'))
                || (version.front() == QLatin1Char('\'') && version.back() == QLatin1Char('\''))))
            version = version.mid(1, version.size() - 2);
        return version;
    }
    return {};
}

} // namespace

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
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("squiddy"));

    const QIcon appIcon = QIcon::fromTheme(QStringLiteral("live-installer"));
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

    MauiApp::instance()->setIconName(QStringLiteral("live-installer"));

    InstallationPlanController installationPlan(parser.isSet(debugOption));
    if (!installationPlan.debugMode()) {
        const SystemDiscoveryResults discoveredSystem = SystemDiscovery::discover();
        installationPlan.setUefiEnvironmentDetected(discoveredSystem.uefiEnvironmentDetected);
        installationPlan.setDetectedImage(discoveredSystem.imagePath, discoveredSystem.imageFormat);
        installationPlan.setAvailableLanguages(discoveredSystem.languages);
        installationPlan.setAvailableLocales(discoveredSystem.locales);
        installationPlan.setAvailableTimeZones(discoveredSystem.timeZones);
        installationPlan.setAvailableKeyboardLayouts(discoveredSystem.keyboardLayouts);
        installationPlan.setKeyboardLayoutOptions(discoveredSystem.keyboardLayoutOptions);
        installationPlan.setKeyboardAdvancedOptions(discoveredSystem.keyboardVariantOptionsByLayout,
                                                    discoveredSystem.keyboardModelOptions,
                                                    discoveredSystem.keyboardOptionOptions);
        installationPlan.setAvailableTargetDisks(discoveredSystem.targetDisks);
        installationPlan.setLanguage(discoveredSystem.defaultLanguage);
        installationPlan.setLocale(discoveredSystem.defaultLocale);
        installationPlan.setTimeZone(discoveredSystem.defaultTimeZone);
        installationPlan.setKeyboardLayout(discoveredSystem.defaultKeyboardLayout);
    }
    PrivilegedInstallerClient privilegedInstaller(&installationPlan);
    SystemStatus systemStatus;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedQmlContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("installationPlan"), &installationPlan);
    engine.rootContext()->setContextProperty(QStringLiteral("privilegedInstaller"), &privilegedInstaller);
    engine.rootContext()->setContextProperty(QStringLiteral("systemStatus"), &systemStatus);
    engine.rootContext()->setContextProperty(QStringLiteral("nitruxVersion"), versionFromOsRelease());
    engine.loadFromModule(QStringLiteral("org.nitrux.squiddy"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    if (!installationPlan.debugMode()) {
        // The live system's settings remain the defaults if GeoIP is unavailable.
        const QString initialLanguage = installationPlan.language();
        const QString initialLocale = installationPlan.locale();
        const QString initialTimeZone = installationPlan.timeZone();
        QString countryDefaultTimeZone;
        QNetworkAccessManager geoIpNetwork;

        QNetworkRequest countryRequest(QUrl(QStringLiteral("https://geoip.kde.org/v1/ubiquity")));
        countryRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                    QNetworkRequest::NoLessSafeRedirectPolicy);
        countryRequest.setTransferTimeout(7000);
        QNetworkReply* countryReply = geoIpNetwork.get(countryRequest);
        QObject::connect(countryReply, &QNetworkReply::finished, &app,
                         [countryReply, &installationPlan, &countryDefaultTimeZone,
                          initialLanguage, initialLocale] {
            if (countryReply->error() != QNetworkReply::NoError) {
                countryReply->deleteLater();
                return;
            }

            QXmlStreamReader xml(countryReply->readAll());
            countryReply->deleteLater();
            QString countryCode;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QStringLiteral("CountryCode")) {
                    countryCode = xml.readElementText().trimmed();
                    break;
                }
            }
            if (xml.hasError() || countryCode.size() != 2
                || installationPlan.regionalSettingsChangedByUser()
                || installationPlan.language() != initialLanguage
                || installationPlan.locale() != initialLocale)
                return;

            const QLocale::Territory territory = QLocale::codeToTerritory(QStringView(countryCode));
            if (territory == QLocale::AnyTerritory)
                return;

            const QLocale likelyLocale(QLocale::AnyLanguage, territory);
            const QLocale fallbackLocale(initialLocale);
            QString matchingLocale;
            QString fallbackMatch;
            for (const QString& localeName : installationPlan.availableLocales()) {
                if (!localeName.contains(QStringLiteral(".UTF-8"), Qt::CaseInsensitive))
                    continue;
                const QLocale candidate(localeName);
                if (candidate.territory() != territory
                    || !installationPlan.availableLanguages().contains(
                        QLocale::languageToString(candidate.language())))
                    continue;
                if (candidate.language() == likelyLocale.language()) {
                    matchingLocale = localeName;
                    break;
                }
                if (fallbackMatch.isEmpty() && candidate.language() == fallbackLocale.language())
                    fallbackMatch = localeName;
            }
            if (matchingLocale.isEmpty())
                matchingLocale = fallbackMatch;
            if (matchingLocale.isEmpty())
                return;

            installationPlan.applyLanguageDefaults(
                QLocale::languageToString(QLocale(matchingLocale).language()), matchingLocale);
            countryDefaultTimeZone = installationPlan.timeZone();
        });

        QNetworkRequest timeZoneRequest(QUrl(QStringLiteral("https://geoip.kde.org/v1/calamares")));
        timeZoneRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);
        timeZoneRequest.setTransferTimeout(7000);
        QNetworkReply* timeZoneReply = geoIpNetwork.get(timeZoneRequest);
        QObject::connect(timeZoneReply, &QNetworkReply::finished, &app,
                         [timeZoneReply, &installationPlan, &countryDefaultTimeZone,
                          initialTimeZone] {
            if (timeZoneReply->error() != QNetworkReply::NoError) {
                timeZoneReply->deleteLater();
                return;
            }

            const QJsonDocument document = QJsonDocument::fromJson(timeZoneReply->readAll());
            timeZoneReply->deleteLater();
            if (!document.isObject() || installationPlan.regionalSettingsChangedByUser())
                return;
            const QString zone = document.object().value(QStringLiteral("time_zone")).toString().trimmed();
            if (installationPlan.availableTimeZones().contains(zone)
                && (installationPlan.timeZone() == initialTimeZone
                    || installationPlan.timeZone() == countryDefaultTimeZone))
                installationPlan.setTimeZone(zone);
        });

        return app.exec();
    }

    return app.exec();
}
