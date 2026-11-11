import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

ColumnLayout {
    spacing: Maui.Style.space.big

    Maui.SectionHeader {
        Layout.fillWidth: true
        text1: qsTr("Create a User")
        text2: qsTr("Create the first user account.")
        label1.font.weight: Font.Normal
        label1.font.pointSize: Maui.Style.fontSizes.enormous * 2
        label1.wrapMode: Text.WordWrap
        label2.wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: accountCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: accountCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Account Details")
                text2: qsTr("Set the account and computer names.")
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Full name")
                label2.text: qsTr("Name shown for this user.")
                label2.wrapMode: Text.Wrap

                Maui.TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Full name")
                    text: installationPlan.fullName
                    onTextChanged: installationPlan.fullName = text
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Username")
                label2.text: qsTr("Used to sign in.")
                label2.wrapMode: Text.Wrap

                Maui.TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Username")
                    text: installationPlan.username
                    onTextChanged: installationPlan.username = text
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Computer name")
                label2.text: qsTr("Network name for this computer.")
                label2.wrapMode: Text.Wrap

                Maui.TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Computer name")
                    text: installationPlan.hostname
                    onTextChanged: installationPlan.hostname = text
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: passwordCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: passwordCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Password")
                text2: qsTr("Use 12–64 characters.")
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Password")
                label2.text: qsTr("Set the account password.")
                label2.wrapMode: Text.Wrap

                Maui.TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                    onTextChanged: installationPlan.setUserPassword(text)
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Confirm password")
                label2.text: qsTr("Repeat the password.")
                label2.wrapMode: Text.Wrap

                Maui.TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Confirm password")
                    echoMode: TextInput.Password
                    onTextChanged: installationPlan.setPasswordConfirmation(text)
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Allow a weak password")
                label2.text: qsTr("Allow passwords that fail quality checks.")
                label2.wrapMode: Text.Wrap

                Switch {
                    checked: installationPlan.allowWeakPassword
                    onToggled: installationPlan.allowWeakPassword = checked
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: signInCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: signInCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Sign-In Options")
                text2: qsTr("Choose sign-in behavior.")
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Log in automatically")
                label2.text: qsTr("Sign in automatically.")
                label2.wrapMode: Text.Wrap

                Switch {
                    checked: installationPlan.automaticLogin
                    onToggled: installationPlan.automaticLogin = checked
                }
            }
        }
    }
}
