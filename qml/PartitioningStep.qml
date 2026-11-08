import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {

    ButtonGroup { id: partitioningModeGroup }

    RadioButton {
        text: qsTr("Automated partitioning")
        checked: installationPlan.partitioningMode === "automated"
        ButtonGroup.group: partitioningModeGroup
        onToggled: if (checked) installationPlan.partitioningMode = "automated"
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Creates an XFS root partition and encrypted, compressed F2FS partitions for /home and /var/lib.")
        wrapMode: Text.Wrap
    }

    RadioButton {
        text: qsTr("Manual partitioning")
        checked: installationPlan.partitioningMode === "manual"
        ButtonGroup.group: partitioningModeGroup
        onToggled: if (checked) installationPlan.partitioningMode = "manual"
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Manual partition editing is not available yet. The detected target disks are limited to writable, unmounted whole disks.")
        wrapMode: Text.Wrap
    }

    Label { text: qsTr("Target disk") }
    ComboBox {
        Layout.fillWidth: true
        model: installationPlan.availableTargetDisks
        currentIndex: installationPlan.availableTargetDisks.indexOf(installationPlan.targetDisk)
        displayText: currentIndex >= 0
            ? currentText
            : (installationPlan.availableTargetDisks.length > 0
                ? qsTr("Select a target disk")
                : qsTr("No suitable target disks were found"))
        onActivated: installationPlan.targetDisk = currentText
    }

    Label { text: qsTr("Swap") }
    ComboBox {
        Layout.fillWidth: true
        textRole: "text"
        model: [
            { text: qsTr("None"), value: "None" },
            { text: qsTr("Small"), value: "Small" },
            { text: qsTr("Suspend"), value: "Suspend" },
            { text: qsTr("File"), value: "File" }
        ]
        currentIndex: model.findIndex(choice => choice.value === installationPlan.swapChoice)
        onActivated: installationPlan.swapChoice = model[currentIndex].value
    }

    Maui.FlexSectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Encrypt /home")
        label2.text: qsTr("Use LUKS2 for this volume.")
        label2.wrapMode: Text.Wrap

        Switch {
            checked: installationPlan.encryptHome
            onToggled: installationPlan.encryptHome = checked
        }
    }

    Maui.FlexSectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Encrypt /var/lib")
        label2.text: qsTr("Use LUKS2 for this volume.")
        label2.wrapMode: Text.Wrap

        Switch {
            checked: installationPlan.encryptVarLib
            onToggled: installationPlan.encryptVarLib = checked
        }
    }

    Label {
        Layout.fillWidth: true
        visible: installationPlan.encryptHome || installationPlan.encryptVarLib
        text: qsTr("Encryption passphrase")
    }
    Maui.TextField {
        Layout.fillWidth: true
        visible: installationPlan.encryptHome || installationPlan.encryptVarLib
        placeholderText: qsTr("Encryption passphrase")
        echoMode: TextInput.Password
        onTextChanged: installationPlan.setEncryptionPassphrase(text)
    }
    Label {
        Layout.fillWidth: true
        visible: installationPlan.encryptHome || installationPlan.encryptVarLib
        text: qsTr("Confirm encryption passphrase")
    }
    Maui.TextField {
        Layout.fillWidth: true
        visible: installationPlan.encryptHome || installationPlan.encryptVarLib
        placeholderText: qsTr("Confirm encryption passphrase")
        echoMode: TextInput.Password
        onTextChanged: installationPlan.setEncryptionPassphraseConfirmation(text)
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("The EFI System Partition mounts at /boot/efi.")
        wrapMode: Text.Wrap
    }
}
