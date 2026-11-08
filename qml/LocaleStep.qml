import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {

    Label { text: qsTr("Language") }
    ComboBox {
        Layout.fillWidth: true
        model: installationPlan.availableLanguages
        currentIndex: installationPlan.availableLanguages.indexOf(installationPlan.language)
        displayText: currentIndex >= 0 ? currentText : qsTr("No supported languages were found on the live system")
        onActivated: installationPlan.language = currentText
    }

    Label { text: qsTr("Locale and regional formats") }
    ComboBox {
        Layout.fillWidth: true
        model: installationPlan.availableLocales
        currentIndex: installationPlan.availableLocales.indexOf(installationPlan.locale)
        displayText: currentIndex >= 0 ? currentText : qsTr("No supported locales were found on the live system")
        onActivated: installationPlan.locale = currentText
    }

    Label { text: qsTr("Time zone") }
    ComboBox {
        Layout.fillWidth: true
        model: installationPlan.availableTimeZones
        currentIndex: installationPlan.availableTimeZones.indexOf(installationPlan.timeZone)
        displayText: currentIndex >= 0 ? currentText : qsTr("No time zones were found on the live system")
        onActivated: installationPlan.timeZone = currentText
    }
}
