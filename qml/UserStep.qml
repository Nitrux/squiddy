import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {
    Label { text: qsTr("Full name") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Full name")
        text: installationPlan.fullName
        onTextChanged: installationPlan.fullName = text
    }

    Label { text: qsTr("Username") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Username")
        text: installationPlan.username
        onTextChanged: installationPlan.username = text
    }

    Label { text: qsTr("Computer name") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Computer name")
        text: installationPlan.hostname
        onTextChanged: installationPlan.hostname = text
    }

    Label { text: qsTr("Password") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Password")
        echoMode: TextInput.Password
        onTextChanged: installationPlan.setUserPassword(text)
    }

    Label { text: qsTr("Confirm password") }
    Maui.TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Confirm password")
        echoMode: TextInput.Password
        onTextChanged: installationPlan.setPasswordConfirmation(text)
    }

    Maui.FlexSectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Log in automatically")
        label2.text: qsTr("Disabled by default.")
        label2.wrapMode: Text.Wrap

        Switch {
            checked: installationPlan.automaticLogin
            onToggled: installationPlan.automaticLogin = checked
        }
    }

    Maui.FlexSectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Allow a weak password")
        label2.text: qsTr("The password quality override is off by default.")
        label2.wrapMode: Text.Wrap

        Switch {
            checked: installationPlan.allowWeakPassword
            onToggled: installationPlan.allowWeakPassword = checked
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Passwords must be 12–64 characters. The default shell is Zsh.")
        wrapMode: Text.Wrap
    }
}
