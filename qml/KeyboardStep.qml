import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {

    Label { text: qsTr("Layout") }
    ComboBox {
        Layout.fillWidth: true
        model: installationPlan.availableKeyboardLayouts
        currentIndex: installationPlan.availableKeyboardLayouts.indexOf(installationPlan.keyboardLayout)
        displayText: currentIndex >= 0 ? currentText : qsTr("No keyboard layouts were found on the live system")
        onActivated: installationPlan.keyboardLayout = currentText
    }

    Label { text: qsTr("Variant") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Optional")
        text: installationPlan.keyboardVariant
        onTextChanged: installationPlan.keyboardVariant = text
    }

    Label { text: qsTr("Model") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Optional")
        text: installationPlan.keyboardModel
        onTextChanged: installationPlan.keyboardModel = text
    }

    Label { text: qsTr("Options") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Optional")
        text: installationPlan.keyboardOptions
        onTextChanged: installationPlan.keyboardOptions = text
    }
}
