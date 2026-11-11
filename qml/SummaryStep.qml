import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {
    id: summaryStep

    function hasAssignment(role) {
        const assignments = installationPlan.manualAssignments
        for (const device in assignments) {
            if (assignments[device] === role)
                return true
        }
        return false
    }

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
        label2.text: installationPlan.partitioningMode === "automated" ? qsTr("Use the whole disk")
            : (installationPlan.partitioningMode === "share" ? qsTr("Install alongside existing partitions")
                : qsTr("Manual partitioning"))
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Filesystem layout")
        label2.text: installationPlan.partitioningMode === "manual"
            ? qsTr("Review the staged partition changes below.")
            : installationPlan.storageLayout.length > 0
                ? installationPlan.storageLayout
                    .filter(partition => partition.partitionNumber > 0 && partition.action !== "resize")
                    .map(partition => qsTr("%1 %2 (%3)").arg(partition.mountPoint)
                        .arg(partition.filesystem).arg(partition.label))
                    .join("; ")
                : qsTr("The filesystem layout is not available.")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        visible: installationPlan.partitioningMode === "manual"
        label1.text: qsTr("Manual partition changes")
        label2.text: installationPlan.manualPartitionLayout.map(partition => {
            const action = partition.action === "delete" ? qsTr("Remove")
                : partition.action === "create" ? qsTr("Create")
                : partition.action === "resize" ? qsTr("Resize") : qsTr("Keep")
            const size = partition.action === "delete" ? ""
                : partition.action === "resize"
                    ? qsTr(" · %1 → %2 GiB")
                        .arg((partition.originalSectorCount * installationPlan.logicalBlockSize / 1073741824).toFixed(2))
                        .arg((partition.sizeBytes / 1073741824).toFixed(2))
                    : qsTr(" · %1 GiB").arg((partition.sizeBytes / 1073741824).toFixed(2))
            const role = partition.mountPoint ? qsTr(" · %1").arg(partition.mountPoint) : ""
            const label = partition.filesystemLabel
                ? qsTr(" · Label: %1").arg(partition.filesystemLabel) : ""
            const format = partition.format && partition.action !== "create" ? qsTr(" · Format") : ""
            const encrypted = partition.encrypted ? qsTr(" · Encrypted") : ""
            return qsTr("%1: %2%3%4%5%6%7")
                .arg(partition.device).arg(action).arg(size).arg(role).arg(label).arg(format).arg(encrypted)
        }).join("; ") || qsTr("No staged partition changes.")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        visible: installationPlan.partitioningMode === "share"
            && installationPlan.storageLayout.some(partition =>
                partition.action === "resize" && partition.reclaimBytes > 0)
        label1.text: qsTr("Existing partition size")
        label2.text: {
            const partition = installationPlan.storageLayout.find(entry =>
                entry.action === "resize" && entry.reclaimBytes > 0)
            return partition
                ? qsTr("%1 (%2): %3 GiB → %4 GiB")
                    .arg(partition.device)
                    .arg(partition.filesystem)
                    .arg((partition.originalSizeBytes / 1073741824).toFixed(2))
                    .arg((partition.sizeBytes / 1073741824).toFixed(2))
                : ""
        }
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
        visible: installationPlan.partitioningMode !== "manual"
        label1.text: qsTr("Suspend mode")
        label2.text: installationPlan.swapChoice === "Suspend"
            ? qsTr("Hibernation") : qsTr("Suspension")
        label2.wrapMode: Text.Wrap
    }
    Maui.SectionItem {
        Layout.fillWidth: true
        flat: true
        label1.text: qsTr("Encryption")
        label2.text: {
            if (installationPlan.partitioningMode !== "manual")
                return installationPlan.encryptInstallation
                    ? qsTr("All Nitrux partitions encrypted; EFI remains unencrypted.")
                    : qsTr("Off")
            const encryptedRoles = []
            if (installationPlan.encryptRoot && summaryStep.hasAssignment("/")) encryptedRoles.push("/")
            if (installationPlan.encryptHome && summaryStep.hasAssignment("/home")) encryptedRoles.push("/home")
            if (installationPlan.encryptVarLib && summaryStep.hasAssignment("/var/lib")) encryptedRoles.push("/var/lib")
            if (installationPlan.encryptSwap && summaryStep.hasAssignment("swap")) encryptedRoles.push(qsTr("swap"))
            return encryptedRoles.length > 0
                ? qsTr("Encrypted: %1").arg(encryptedRoles.join(", ")) : qsTr("Off")
        }
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
                + (modelData.note ? qsTr(" · %1").arg(modelData.note) : "")
            label2.wrapMode: Text.Wrap
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Storage preflight is read only.")
        wrapMode: Text.Wrap
    }

    CheckBox {
        Layout.fillWidth: true
        visible: !installationPlan.debugMode
        checked: installationPlan.acknowledgeDiskErasure
        text: installationPlan.partitioningMode === "automated"
            ? qsTr("I understand that installing will erase all data on %1.")
                .arg(installationPlan.targetDisk || qsTr("the selected disk"))
            : (installationPlan.partitioningMode === "share"
                ? qsTr("I understand that installing will add and format partitions in unallocated space on %1.")
                    .arg(installationPlan.targetDisk || qsTr("the selected disk"))
                : (!installationPlan.targetDiskInfo.gpt
                    ? qsTr("I understand that installing will replace the partition table and format assigned partitions on %1.")
                        .arg(installationPlan.targetDisk || qsTr("the selected disk"))
                    : qsTr("I understand that installing will format the assigned partitions on %1, including the selected EFI partition.")
                        .arg(installationPlan.targetDisk || qsTr("the selected disk"))))
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
