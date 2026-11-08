import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {
    Label {
        Layout.fillWidth: true
        text: qsTr("Install Nitrux")
        font: Maui.Style.h2Font
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("This guided installation will help you configure your language, keyboard, user account, and disk layout before installing Nitrux.")
        wrapMode: Text.Wrap
    }

    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Firmware mode")
        label2.text: installationPlan.debugMode
            ? qsTr("Simulated UEFI environment")
            : (installationPlan.uefiEnvironmentDetected ? qsTr("UEFI detected") : qsTr("UEFI mode not verified"))
        label2.wrapMode: Text.Wrap
    }

    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Installation image")
        label2.text: installationPlan.imagePath
            ? qsTr("%1 (%2)").arg(installationPlan.imagePath).arg(installationPlan.imageFormat)
            : qsTr("No supported installation image was found on the live system")
        label2.wrapMode: Text.Wrap
    }
}
