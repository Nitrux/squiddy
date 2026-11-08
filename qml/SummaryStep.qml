import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Language")
        label2.text: installationPlan.language || qsTr("Not selected")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Locale")
        label2.text: installationPlan.locale || qsTr("Not selected")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Time zone")
        label2.text: installationPlan.timeZone || qsTr("Not selected")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Keyboard")
        label2.text: [installationPlan.keyboardLayout, installationPlan.keyboardVariant,
                      installationPlan.keyboardModel, installationPlan.keyboardOptions]
                     .filter(value => value).join(" / ") || qsTr("Not selected")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Full name")
        label2.text: installationPlan.fullName || qsTr("Not configured")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Username")
        label2.text: installationPlan.username || qsTr("Not configured")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Computer name")
        label2.text: installationPlan.hostname || qsTr("Not configured")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Target disk")
        label2.text: installationPlan.targetDisk || qsTr("Not selected")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Partitioning")
        label2.text: installationPlan.partitioningMode === "automated" ? qsTr("Automated") : qsTr("Manual")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Filesystem layout")
        label2.text: installationPlan.partitioningMode === "automated"
            ? qsTr("/ XFS 23%; /home F2FS 67%; /var/lib F2FS 10%")
            : qsTr("Manual layout not configured")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("EFI System Partition")
        label2.text: qsTr("Mount at /boot/efi; recommended size 100 MiB")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("User shell")
        label2.text: qsTr("Zsh (/bin/zsh)")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Automatic login")
        label2.text: installationPlan.automaticLogin ? qsTr("On") : qsTr("Off")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Weak password override")
        label2.text: installationPlan.allowWeakPassword ? qsTr("On") : qsTr("Off")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Swap")
        label2.text: installationPlan.swapChoice
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Encryption")
        label2.text: qsTr("/home: %1; /var/lib: %2")
            .arg(installationPlan.encryptHome ? qsTr("On") : qsTr("Off"))
            .arg(installationPlan.encryptVarLib ? qsTr("On") : qsTr("Off"))
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Read-only storage preflight")
        label2.text: installationPlan.debugMode
            ? qsTr("Simulation preview; no system or disk checks were run.")
            : (installationPlan.storagePreflightBusy
            ? qsTr("Inspecting the image and selected disk…")
            : (!installationPlan.storagePreflightComplete
                ? qsTr("Not run")
                : (installationPlan.storagePreflightErrors.length === 0
                    ? qsTr("Ready") : qsTr("Blocked; review the reported issues"))))
        label2.wrapMode: Text.Wrap
    }

    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        visible: !installationPlan.debugMode && installationPlan.targetDiskBytes > 0
        label1.text: qsTr("Selected disk geometry")
        label2.text: qsTr("%1 GiB total; %2-byte logical sectors")
            .arg((installationPlan.targetDiskBytes / 1073741824).toFixed(2))
            .arg(installationPlan.logicalBlockSize)
        label2.wrapMode: Text.Wrap
    }

    Repeater {
        model: installationPlan.storageLayout

        delegate: Maui.SectionItem {
            required property var modelData
            Layout.fillWidth: true
            flat: true
            label1.text: modelData.mountPoint === "swap"
                ? qsTr("Swap (%1)").arg(modelData.label)
                : (modelData.partitionNumber > 0
                    ? qsTr("Partition %1: %2 → %3")
                        .arg(modelData.partitionNumber).arg(modelData.label).arg(modelData.mountPoint)
                    : qsTr("%1 → %2").arg(modelData.label).arg(modelData.mountPoint))
            label2.text: modelData.filesystem
                + (modelData.sizeBytes > 0
                    ? qsTr(" · %1 GiB").arg((modelData.sizeBytes / 1073741824).toFixed(2))
                    : "")
                + (modelData.sizeBytes > 0 && modelData.sectorCount > 0
                    ? qsTr(" · start LBA %1, %2 sectors")
                        .arg(modelData.startSector).arg(modelData.sectorCount)
                    : "")
                + (modelData.minimumBytes > 0
                    ? qsTr(" · estimated image %1 GiB; minimum %2 GiB")
                        .arg((modelData.estimatedImageBytes / 1073741824).toFixed(2))
                        .arg((modelData.minimumBytes / 1073741824).toFixed(2))
                    : "")
                + (modelData.note ? qsTr(" · %1").arg(modelData.note) : "")
            label2.wrapMode: Text.Wrap
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Image sizes are conservative estimates from a read-only file listing. The storage preflight makes no disk changes.")
        wrapMode: Text.Wrap
    }

    CheckBox {
        Layout.fillWidth: true
        visible: !installationPlan.debugMode
        checked: installationPlan.acknowledgeDiskErasure
        text: qsTr("I understand that installing will erase all data on %1.")
            .arg(installationPlan.targetDisk || qsTr("the selected disk"))
        onToggled: installationPlan.acknowledgeDiskErasure = checked
    }

    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Plan validation")
        label2.text: installationPlan.planValid ? qsTr("Ready") : qsTr("Incomplete")
        label2.wrapMode: Text.Wrap
    }

    Repeater {
        model: installationPlan.validationErrors

        delegate: Label {
            required property string modelData
            Layout.fillWidth: true
            text: modelData
            color: Maui.Theme.negativeTextColor
            wrapMode: Text.Wrap
        }
    }

    Label {
        Layout.fillWidth: true
        visible: privilegedInstaller.busy || privilegedInstaller.installationFinished
        text: privilegedInstaller.progressPhase
        font.bold: true
        wrapMode: Text.Wrap
    }

    ProgressBar {
        Layout.fillWidth: true
        visible: privilegedInstaller.busy || privilegedInstaller.installationFinished
        from: 0
        to: 100
        value: privilegedInstaller.progressPercent
    }

    Repeater {
        model: privilegedInstaller.progressLog

        delegate: Label {
            required property string modelData
            Layout.fillWidth: true
            text: modelData
            color: privilegedInstaller.installationFinished && !privilegedInstaller.installationSucceeded
                && index === privilegedInstaller.progressLog.length - 1
                ? Maui.Theme.negativeTextColor : Maui.Theme.textColor
            wrapMode: Text.Wrap
        }
    }

    Label {
        Layout.fillWidth: true
        text: privilegedInstaller.statusMessage
        visible: text.length > 0
        color: Maui.Theme.textColor
        wrapMode: Text.Wrap
    }
}
