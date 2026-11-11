import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

ColumnLayout {
    id: partitioningStep
    spacing: Maui.Style.space.big

    function assignmentCount(role) {
        const assignments = installationPlan.manualAssignments
        let count = 0
        for (const device in assignments) {
            if (assignments[device] === role)
                count++
        }
        return count
    }

    function hasAssignment(role) { return assignmentCount(role) > 0 }

    function hasShareSource() {
        const info = installationPlan.targetDiskInfo
        return (info.freeExtents || []).some(extent => extent.sizeBytes > 0)
            || (info.partitions || []).some(partition => partition.shrinkable)
    }

    function shareAreaOptions() {
        const info = installationPlan.targetDiskInfo
        const options = (info.freeExtents || []).filter(extent => extent.sizeBytes > 0).map(extent => ({
            text: qsTr("Unallocated · %1 GiB · starts at %2 GiB")
                .arg((extent.sizeBytes / 1073741824).toFixed(2))
                .arg((extent.startSector * info.sectorSize / 1073741824).toFixed(2)),
            startSector: extent.startSector,
            resizeDevice: ""
        }))
        for (const partition of info.partitions || []) {
            if (!partition.shrinkable)
                continue
            options.push({
                text: qsTr("Shrink %1 (%2) · %3 GiB already free after it")
                    .arg(partition.device).arg(partition.filesystem)
                    .arg((partition.followingFreeBytes / 1073741824).toFixed(2)),
                startSector: partition.followingFreeStartSector,
                resizeDevice: partition.device
            })
        }
        return options
    }

    function partitionTypeName(typeGuid) {
        const type = String(typeGuid).toLowerCase()
        if (type === "c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
            return qsTr("EFI")
        if (type === "0fc63daf-8483-4772-8e79-3d69d8477de4")
            return qsTr("Linux")
        if (type === "ca7d7ccb-63ed-4c53-861c-1742536059cc")
            return qsTr("LUKS")
        if (type === "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f")
            return qsTr("Swap")
        return qsTr("Other")
    }

    readonly property bool encryptionSelected: installationPlan.partitioningMode !== "manual"
        ? installationPlan.encryptInstallation
        : ((installationPlan.encryptRoot && hasAssignment("/"))
            || (installationPlan.encryptHome && hasAssignment("/home"))
            || (installationPlan.encryptVarLib && hasAssignment("/var/lib"))
            || (installationPlan.encryptSwap && hasAssignment("swap")))

    function encryptionDescription() {
        if (installationPlan.partitioningMode !== "manual")
            return installationPlan.encryptInstallation
                ? qsTr("Shared by encrypted partitions.")
                : qsTr("Encryption is disabled.")
        const roles = []
        if (installationPlan.encryptRoot && hasAssignment("/")) roles.push("/")
        if (installationPlan.encryptHome && hasAssignment("/home")) roles.push("/home")
        if (installationPlan.encryptVarLib && hasAssignment("/var/lib")) roles.push("/var/lib")
        if (installationPlan.encryptSwap && hasAssignment("swap")) roles.push(qsTr("swap"))
        return roles.length > 0
            ? qsTr("Encrypted partitions: %1.").arg(roles.join(", "))
            : qsTr("Choose encryption for each assigned partition.")
    }

    readonly property bool readyToContinue: installationPlan.targetDisk.length > 0
        && (installationPlan.partitioningMode !== "share"
            || (installationPlan.targetDiskInfo.gpt === true
                && partitioningStep.hasShareSource()))
        && (installationPlan.partitioningMode !== "manual"
            || (assignmentCount("/boot/efi") === 1 && assignmentCount("/") === 1
                && assignmentCount("/home") <= 1 && assignmentCount("/var/lib") <= 1
                && assignmentCount("swap") <= 1))

    ButtonGroup { id: partitioningModeGroup }
    ButtonGroup { id: targetDiskSelectionGroup; exclusive: true }

    Maui.SectionHeader {
        Layout.fillWidth: true
        text1: qsTr("Where should Nitrux live?")
        text2: qsTr("Select a disk and partitioning method.")
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
        implicitHeight: diskCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: diskCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Choose a Disk")
                text2: qsTr("Writable, unmounted disks are listed.")
                label2.wrapMode: Text.WordWrap
            }

            Repeater {
                model: installationPlan.availableTargetDisks

                delegate: CheckBox {
                    id: targetDiskChoice

                    Layout.fillWidth: true
                    LayoutMirroring.enabled: true
                    ButtonGroup.group: targetDiskSelectionGroup
                    padding: Maui.Style.contentMargins
                    text: installationPlan.diskDisplayName(modelData)
                    implicitHeight: Math.max(contentItem.implicitHeight, indicator.implicitHeight)
                        + topPadding + bottomPadding
                    Binding on checked {
                        value: installationPlan.targetDisk === modelData
                        restoreMode: Binding.RestoreBinding
                    }
                    onClicked: installationPlan.targetDisk = modelData

                    contentItem: ColumnLayout {
                        spacing: Maui.Style.space.small

                        Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: targetDiskChoice.indicator.implicitWidth + targetDiskChoice.spacing
                            text: installationPlan.diskDisplayName(modelData)
                            font: Maui.Style.h2Font
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: targetDiskChoice.indicator.implicitWidth + targetDiskChoice.spacing
                            text: qsTr("Device: %1").arg(modelData)
                            font.pointSize: Maui.Style.fontSizes.small
                            opacity: targetDiskChoice.checked ? 0.8 : 0.6
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }
                    }

                    background: Rectangle {
                        radius: Maui.Style.radiusV
                        color: targetDiskChoice.checked ? Maui.Theme.backgroundColor : Maui.Theme.alternateBackgroundColor
                        border.color: targetDiskChoice.checked ? Maui.Theme.highlightColor : "transparent"
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: installationPlan.availableTargetDisks.length === 0
                text: qsTr("No suitable target disks were found.")
                wrapMode: Text.WordWrap
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        enabled: installationPlan.targetDisk.length > 0
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: modeCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: modeCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Partitioning Method")
                text2: qsTr("Choose how to use this disk.")
            }

            RadioButton {
                Layout.fillWidth: true
                text: qsTr("Use the whole disk")
                checked: installationPlan.partitioningMode === "automated"
                ButtonGroup.group: partitioningModeGroup
                onClicked: installationPlan.partitioningMode = "automated"
            }

            Maui.SectionHeader {
                Layout.fillWidth: true
                text2: qsTr("Erase the disk and create Nitrux partitions.")
                label2.wrapMode: Text.WordWrap
            }

            RadioButton {
                Layout.fillWidth: true
                text: qsTr("Install alongside existing partitions")
                checked: installationPlan.partitioningMode === "share"
                ButtonGroup.group: partitioningModeGroup
                onClicked: installationPlan.partitioningMode = "share"
            }

            Maui.SectionHeader {
                Layout.fillWidth: true
                text2: installationPlan.targetDiskInfo.gpt && partitioningStep.hasShareSource()
                    ? qsTr("Use free space or shrink a supported partition.")
                    : qsTr("Requires a GPT disk with free or supported shrinkable space.")
                label2.wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                RadioButton {
                    Layout.fillWidth: true
                    text: qsTr("Manual partitioning")
                    checked: installationPlan.partitioningMode === "manual"
                    ButtonGroup.group: partitioningModeGroup
                    onClicked: installationPlan.partitioningMode = "manual"
                }

                Button {
                    visible: installationPlan.partitioningMode === "manual"
                    text: qsTr("Edit partitions")
                    onClicked: manualPartitionEditor.open()
                }
            }

            Maui.SectionHeader {
                Layout.fillWidth: true
                visible: installationPlan.partitioningMode === "manual"
                text2: qsTr("Assign partitions for Nitrux.")
                label2.wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                visible: installationPlan.partitioningMode === "manual"
                    && installationPlan.manualPartitionLayout.length > 0
                text: qsTr("%1 partition entries in the staged layout.")
                    .arg(installationPlan.manualPartitionLayout.length)
                color: Maui.Theme.textColor
                wrapMode: Text.WordWrap
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                visible: installationPlan.partitioningMode !== "manual"
                label1.text: qsTr("Suspend mode")
                label2.text: qsTr("Choose swap space for suspension or hibernation.")
                label2.wrapMode: Text.WordWrap

                ComboBox {
                    id: suspendModeCombo
                    Layout.fillWidth: true
                    model: [
                        { text: qsTr("Suspension"), value: "Small" },
                        { text: qsTr("Hibernation"), value: "Suspend" }
                    ]
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: installationPlan.swapChoice === "Suspend" ? 1 : 0
                    onActivated: installationPlan.swapChoice = currentValue
                    popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 4)
                    popupScrollBar: true
                    delegate: MenuItem {
                        width: ListView.view.width
                        text: suspendModeCombo.model[index].text
                        highlighted: suspendModeCombo.highlightedIndex === index
                    }
                }
            }
        }
    }

    ManualPartitionEditor {
        id: manualPartitionEditor
        targetDevice: installationPlan.targetDisk
        diskInfo: installationPlan.targetDiskInfo
        assignments: installationPlan.manualAssignments
        labels: installationPlan.partitionLabels
        stagedLayout: installationPlan.manualPartitionLayout
        onPartitionLayoutAccepted: installationPlan.setManualPartitionLayout(layout)
    }

    Rectangle {
        Layout.fillWidth: true
        enabled: installationPlan.targetDisk.length > 0
        visible: installationPlan.partitioningMode === "share"
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: shareCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: shareCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Space source")
                label2.text: qsTr("Choose where to reclaim installation space.")
                label2.wrapMode: Text.WordWrap

                ComboBox {
                    Layout.fillWidth: true
                    textRole: "text"
                    model: partitioningStep.shareAreaOptions()
                    currentIndex: Math.max(0, model.findIndex(choice =>
                        choice.startSector === (installationPlan.sharedSpaceStartSector
                            || installationPlan.targetDiskInfo.freeStartSector)
                        && choice.resizeDevice === installationPlan.sharedResizeDevice))
                    onActivated: {
                        const choice = model[currentIndex]
                        installationPlan.sharedSpaceStartSector = choice.startSector
                        installationPlan.sharedResizeDevice = choice.resizeDevice
                    }
                    popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                    popupScrollBar: true
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Space for Nitrux (MiB)")
                label2.text: installationPlan.sharedResizeDevice.length > 0
                    ? qsTr("Set the total size; only needed space is reclaimed.")
                    : qsTr("Leave blank to use all free space.")
                label2.wrapMode: Text.WordWrap

                Maui.TextField {
                    Layout.fillWidth: true
                    text: installationPlan.sharedSpaceMiB > 0
                        ? String(installationPlan.sharedSpaceMiB) : ""
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /^[0-9]{0,12}$/ }
                    placeholderText: installationPlan.sharedResizeDevice.length > 0
                        ? qsTr("Enter total installation size") : qsTr("All available space")
                    onEditingFinished: installationPlan.sharedSpaceMiB = Number(text) || 0
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        enabled: installationPlan.targetDisk.length > 0
        visible: installationPlan.partitioningMode !== "manual"
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: optionsCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: optionsCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Encryption")
                text2: installationPlan.partitioningMode === "manual"
                    ? qsTr("Set LUKS2 encryption for assigned partitions.")
                    : qsTr("Optionally encrypt Nitrux partitions with LUKS2.")
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                visible: installationPlan.partitioningMode !== "manual"
                label1.text: qsTr("Encrypt the Nitrux installation")
                label2.text: qsTr("System, data, and swap are encrypted; EFI stays unencrypted.")
                label2.wrapMode: Text.WordWrap

                template.content: Switch {
                    checked: installationPlan.encryptInstallation
                    onToggled: installationPlan.encryptInstallation = checked
                }
            }

            Maui.SectionHeader {
                Layout.fillWidth: true
                visible: installationPlan.partitioningMode === "manual"
                text1: qsTr("Encryption by Partition")
                text2: qsTr("Set encryption for each assigned partition.")
                label2.wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: partitioningStep.encryptionSelected
                spacing: Maui.Style.space.small

                Maui.FlexSectionItem {
                    Layout.fillWidth: true
                    flat: true
                    label1.text: qsTr("Shared encryption passphrase")
                    label2.text: partitioningStep.encryptionDescription()
                    label2.wrapMode: Text.WordWrap

                    Maui.TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Passphrase")
                        echoMode: TextInput.Password
                        onTextChanged: installationPlan.setEncryptionPassphrase(text)
                    }
                }

                Maui.FlexSectionItem {
                    Layout.fillWidth: true
                    flat: true
                    label1.text: qsTr("Confirm encryption passphrase")
                    label2.text: qsTr("Enter the same passphrase again.")
                    label2.wrapMode: Text.Wrap

                    Maui.TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Confirm passphrase")
                        echoMode: TextInput.Password
                        onTextChanged: installationPlan.setEncryptionPassphraseConfirmation(text)
                    }
                }
            }

        }
    }
}
