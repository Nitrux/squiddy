import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

Dialog {
    id: dialog

    required property string targetDevice
    required property var diskInfo
    required property var assignments
    required property var labels
    property var draft: []
    property var stagedLayout: []
    property int selectedIndex: -1
    property bool newPartitionTableConfirmed: false

    title: qsTr("Manual Partitioning")
    parent: Overlay.overlay
    modal: true
    standardButtons: Dialog.Cancel | Dialog.Apply
    width: Math.min(parent.width - Maui.Style.contentMargins * 2, Maui.Style.units.gridUnit * 58)
    height: Math.min(implicitHeight, parent.height - Maui.Style.contentMargins * 4)
    onOpened: resetDraft()
    onApplied: partitionLayoutAccepted(draft)

    signal partitionLayoutAccepted(var layout)

    function deviceForNumber(number) {
        const disk = String(diskInfo.device || targetDevice)
        return disk + (/[0-9]$/.test(disk) ? "p" : "") + number
    }

    function defaultLabel(role) {
        if (role === "/boot/efi") return "ESP"
        if (role === "/") return "NX_ROOT"
        if (role === "/home") return "NX_HOME"
        if (role === "/var/lib") return "NX_VAR_LIB"
        if (role === "swap") return "SWAP"
        return ""
    }

    function resetDraft(clearChanges) {
        if (!clearChanges && stagedLayout.length > 0) {
            draft = stagedLayout.map(row => Object.assign({}, row))
            newPartitionTableConfirmed = !diskInfo.gpt
            selectedIndex = -1
            return
        }
        const parts = diskInfo.partitions || []
        if (!diskInfo.gpt) {
            draft = parts.map(partition => Object.assign({}, partition, {
                action: "keep",
                partitionNumber: partition.partitionNumber || dialog.partitionNumberFor(partition.device),
                originalStartSector: partition.startSector,
                originalSectorCount: partition.sectorCount,
                originalUuid: partition.uuid,
                originalTypeGuid: partition.typeGuid,
                originalLabel: partition.label,
                originalAttributes: partition.attributes,
                originalFilesystem: partition.filesystem,
                tableId: "",
                mountPoint: "",
                filesystemLabel: "",
                encrypted: false,
                format: false
            }))
            newPartitionTableConfirmed = false
            selectedIndex = -1
            return
        }
        draft = parts.map(partition => {
            const role = clearChanges ? "" : (assignments[partition.device] || "")
            return Object.assign({}, partition, {
                action: "keep",
                tableId: diskInfo.tableId,
                partitionNumber: partition.partitionNumber || dialog.partitionNumberFor(partition.device),
                originalStartSector: partition.startSector,
                originalSectorCount: partition.sectorCount,
                originalUuid: partition.uuid,
                originalTypeGuid: partition.typeGuid,
                originalLabel: partition.label,
                originalAttributes: partition.attributes,
                originalFilesystem: partition.filesystem,
                mountPoint: role,
                filesystemLabel: role ? (labels[role] || defaultLabel(role)) : "",
                encrypted: role === "/" ? installationPlan.encryptRoot
                    : role === "/home" ? installationPlan.encryptHome
                    : role === "/var/lib" ? installationPlan.encryptVarLib
                    : role === "swap" ? installationPlan.encryptSwap : false,
                format: !!role,
                plannedTypeGuid: role ? dialog.typeForRole(role,
                    role === "/" ? installationPlan.encryptRoot
                    : role === "/home" ? installationPlan.encryptHome
                    : role === "/var/lib" ? installationPlan.encryptVarLib
                    : role === "swap" ? installationPlan.encryptSwap : false) : partition.typeGuid
            })
        })
        selectedIndex = -1
    }

    function updateRow(index, values) {
        const next = draft.slice()
        next[index] = Object.assign({}, next[index], values)
        draft = next
    }

    function partitionNumberFor(device) {
        const disk = String(diskInfo.device || targetDevice)
        const suffix = device.substring(disk.length).replace(/^p/, "")
        return Number(suffix)
    }

    function partitionDisplayName(device) {
        return String(device).split("/").pop()
    }

    function availableExtents() {
        const initialExtents = diskInfo.gpt ? (diskInfo.freeExtents || [])
            : (newPartitionTableConfirmed ? [{
                startSector: 2048,
                sectorCount: Math.max(0, Math.floor(Number(diskInfo.sizeBytes) / Number(diskInfo.sectorSize)) - 2048 - 33)
            }] : [])
        const extents = initialExtents.filter(extent => extent.sectorCount > 0)
            .map(extent => ({startSector: Number(extent.startSector), sectorCount: Number(extent.sectorCount)}))
        for (const partition of draft) {
            if (partition.action === "delete")
                extents.push({startSector: Number(partition.originalStartSector),
                    sectorCount: Number(partition.originalSectorCount)})
            else if (partition.action === "resize"
                     && Number(partition.sectorCount) < Number(partition.originalSectorCount))
                extents.push({startSector: Number(partition.startSector) + Number(partition.sectorCount),
                    sectorCount: Number(partition.originalStartSector) + Number(partition.originalSectorCount)
                        - Number(partition.startSector) - Number(partition.sectorCount)})
        }
        const occupied = draft.filter(partition => partition.action !== "delete")
            .map(partition => ({start: Number(partition.startSector), end: Number(partition.startSector) + Number(partition.sectorCount)}))
        const result = []
        for (const extent of extents) {
            let segments = [{start: Number(extent.startSector), end: Number(extent.startSector) + Number(extent.sectorCount)}]
            for (const part of occupied) {
                const next = []
                for (const segment of segments) {
                    if (part.end <= segment.start || part.start >= segment.end) {
                        next.push(segment)
                        continue
                    }
                    if (part.start > segment.start) next.push({start: segment.start, end: part.start})
                    if (part.end < segment.end) next.push({start: part.end, end: segment.end})
                }
                segments = next
            }
            for (const segment of segments) {
                if (segment.end > segment.start)
                    result.push({startSector: segment.start, sectorCount: segment.end - segment.start,
                                 sizeBytes: (segment.end - segment.start) * Number(diskInfo.sectorSize)})
            }
        }
        return result
    }

    function nextPartitionNumber() {
        const used = draft.filter(partition => partition.action !== "delete")
            .map(partition => Number(partition.partitionNumber))
        for (let number = 1; number <= 128; ++number)
            if (!used.includes(number)) return number
        return 0
    }

    function filesystemForRole(role) {
        if (role === "/boot/efi") return "vfat"
        if (role === "/") return "xfs"
        if (role === "/home" || role === "/var/lib") return "f2fs"
        if (role === "swap") return "swap"
        return ""
    }

    function typeForRole(role, encrypted) {
        if (role === "/boot/efi") return "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
        if (role === "swap") return encrypted ? "ca7d7ccb-63ed-4c53-861c-1742536059cc"
                                              : "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"
        return encrypted ? "ca7d7ccb-63ed-4c53-861c-1742536059cc"
                         : "0fc63daf-8483-4772-8e79-3d69d8477de4"
    }

    function addPartition(startSector, sectorCount) {
        const number = nextPartitionNumber()
        if (number < 1 || sectorCount < 1) return
        const row = {
            device: deviceForNumber(number),
            partitionNumber: number,
            startSector: startSector,
            sectorCount: sectorCount,
            sizeBytes: sectorCount * Number(diskInfo.sectorSize),
            typeGuid: "0fc63daf-8483-4772-8e79-3d69d8477de4",
            plannedTypeGuid: "0fc63daf-8483-4772-8e79-3d69d8477de4",
            uuid: "",
            filesystem: "",
            label: "",
            attributes: "",
            action: "create",
            originalStartSector: 0,
            originalSectorCount: 0,
            originalUuid: "",
            originalTypeGuid: "",
            originalLabel: "",
            originalAttributes: "",
            originalFilesystem: "",
            tableId: String(diskInfo.tableId || ""),
            mountPoint: "",
            filesystemLabel: "",
            encrypted: false
        }
        draft = draft.concat([row])
        selectedIndex = draft.length - 1
    }

    contentItem: ColumnLayout {
        spacing: Maui.Style.space.small

        Maui.SectionHeader {
            Layout.fillWidth: true
            text2: qsTr("Changes apply at installation. Removed or formatted partitions lose data.")
            label2.wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: qsTr("Device"); font: Maui.Style.h2Font }
            Label { Layout.fillWidth: true; text: qsTr("Type"); font: Maui.Style.h2Font }
            Label { Layout.fillWidth: true; text: qsTr("Mount point"); font: Maui.Style.h2Font }
            Label { Layout.fillWidth: true; text: qsTr("Size"); font: Maui.Style.h2Font }
            Label { Layout.fillWidth: true; text: qsTr("Label"); font: Maui.Style.h2Font }
            Label { Layout.fillWidth: true; text: qsTr("Format"); font: Maui.Style.h2Font }
            Label { Layout.fillWidth: true; text: qsTr("Encrypt"); font: Maui.Style.h2Font }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Maui.Style.rowHeight * 8
            color: "transparent"
            border.color: Maui.Theme.backgroundColor
            border.width: 1
            radius: Maui.Style.radiusV

            ScrollView {
                anchors.fill: parent
                clip: true

                ColumnLayout {
                    width: parent.width
                    Repeater {
                        model: dialog.draft
                        delegate: RowLayout {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Maui.Style.space.small
                            opacity: modelData.action === "delete" ? 0.5 : 1

                            TapHandler { onTapped: dialog.selectedIndex = index }

                            Label {
                                Layout.fillWidth: true
                                text: dialog.partitionDisplayName(modelData.device)
                                elide: Text.ElideMiddle
                                font.strikeout: modelData.action === "delete"
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.filesystem || dialog.typeLabel(modelData.typeGuid)
                                elide: Text.ElideRight
                            }

                            ComboBox {
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    {text: qsTr("Not used"), value: ""},
                                    {text: qsTr("EFI System Partition"), value: "/boot/efi"},
                                    {text: qsTr("Root"), value: "/"},
                                    {text: qsTr("Home"), value: "/home"},
                                    {text: qsTr("System data"), value: "/var/lib"},
                                    {text: qsTr("Linux swap"), value: "swap"}
                                ]
                                currentIndex: Math.max(0, model.findIndex(item => item.value === modelData.mountPoint))
                                onActivated: dialog.updateRow(index, {
                                    mountPoint: currentValue,
                                    filesystemLabel: currentValue ? (dialog.labels[currentValue] || dialog.defaultLabel(currentValue)) : "",
                                    encrypted: false,
                                    format: !!currentValue,
                                    plannedTypeGuid: currentValue ? dialog.typeForRole(currentValue, false) : modelData.typeGuid
                                })
                                enabled: modelData.action !== "delete"
                                    && (dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed)
                                popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 7)
                                popupScrollBar: true
                            }

                            Maui.TextField {
                                Layout.fillWidth: true
                                Layout.preferredWidth: Maui.Style.units.gridUnit * 5
                                text: String(Math.floor(Number(modelData.sectorCount) * Number(dialog.diskInfo.sectorSize) / 1048576))
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: RegularExpressionValidator { regularExpression: /^[0-9]{1,12}$/ }
                                enabled: modelData.action !== "delete"
                                    && (dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed)
                                onEditingFinished: {
                                    const sectors = Math.floor((Number(text) * 1048576) / Number(dialog.diskInfo.sectorSize))
                                    if (sectors > 0) dialog.updateRow(index, {
                                        sectorCount: sectors,
                                        sizeBytes: sectors * Number(dialog.diskInfo.sectorSize),
                                        action: modelData.action === "create" ? "create" : "resize"
                                    })
                                }
                            }

                            Maui.TextField {
                                Layout.fillWidth: true
                                placeholderText: modelData.mountPoint ? dialog.defaultLabel(modelData.mountPoint)
                                    : qsTr("Partition label")
                                text: modelData.mountPoint ? modelData.filesystemLabel : modelData.label
                                maximumLength: modelData.mountPoint ? 11 : 36
                                validator: RegularExpressionValidator {
                                    regularExpression: modelData.mountPoint
                                        ? /^[A-Za-z0-9_]{1,11}$/ : /^[A-Za-z0-9 _.-]{0,36}$/
                                }
                                enabled: modelData.action !== "delete"
                                    && (dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed)
                                onEditingFinished: dialog.updateRow(index, modelData.mountPoint
                                    ? {filesystemLabel: text, label: text} : {label: text})
                            }

                            CheckBox {
                                checked: !!modelData.mountPoint
                                enabled: false
                            }

                            Switch {
                                visible: !!modelData.mountPoint && modelData.mountPoint !== "/boot/efi"
                                checked: !!modelData.encrypted
                                enabled: modelData.action !== "delete"
                                    && (dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed)
                                onToggled: dialog.updateRow(index, {
                                    encrypted: checked,
                                    plannedTypeGuid: dialog.typeForRole(modelData.mountPoint, checked)
                                })
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: qsTr("Add")
                enabled: dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed
                onClicked: addDialog.open()
            }
            Button {
                text: qsTr("Remove")
                enabled: dialog.selectedIndex >= 0 && dialog.draft[dialog.selectedIndex].action !== "delete"
                onClicked: {
                    const row = dialog.draft[dialog.selectedIndex]
                    if (row.action === "create")
                        dialog.draft = dialog.draft.filter((_, index) => index !== dialog.selectedIndex)
                    else
                        dialog.updateRow(dialog.selectedIndex, {action: "delete", mountPoint: ""})
                    dialog.selectedIndex = -1
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("New partition table")
                enabled: !dialog.diskInfo.gpt || dialog.draft.some(partition => partition.action !== "delete")
                onClicked: newTableDialog.open()
            }
            Button { text: qsTr("Revert"); onClicked: dialog.resetDraft(true) }
        }
    }

    function typeLabel(type) {
        const value = String(type).toLowerCase()
        if (value === "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") return qsTr("EFI")
        if (value === "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f") return qsTr("Linux swap")
        if (value === "ca7d7ccb-63ed-4c53-861c-1742536059cc") return qsTr("LUKS")
        if (value === "0fc63daf-8483-4772-8e79-3d69d8477de4" || value === "83") return qsTr("Linux")
        if (value === "ef") return qsTr("EFI")
        return qsTr("Other")
    }

    Dialog {
        id: newTableDialog
        title: qsTr("Create a new GPT partition table?")
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Yes
        contentItem: Label {
            text: dialog.diskInfo.gpt
                ? qsTr("This removes every existing partition from the staged layout. The changes are applied only when installation starts.")
                : qsTr("Creating a GPT partition table replaces the disk's current partition table. The changes are applied only when installation starts.")
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (!dialog.diskInfo.gpt) {
                dialog.draft = []
                dialog.newPartitionTableConfirmed = true
            } else {
                dialog.draft = dialog.draft.filter(row => row.action !== "create")
                    .map(row => Object.assign({}, row, {action: "delete", mountPoint: "", encrypted: false}))
            }
            dialog.selectedIndex = -1
        }
    }

    Dialog {
        id: addDialog
        title: qsTr("Create Partition")
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        property int extentIndex: 0
        property string sizeText: ""
        contentItem: ColumnLayout {
            Maui.FlexSectionItem {
                Layout.fillWidth: true
                label1.text: qsTr("Free space")
                ComboBox {
                    Layout.fillWidth: true
                    textRole: "text"
                    model: dialog.availableExtents().map(extent => ({
                        text: qsTr("%1 GiB available at %2 GiB")
                            .arg((extent.sizeBytes / 1073741824).toFixed(2))
                            .arg((extent.startSector * dialog.diskInfo.sectorSize / 1073741824).toFixed(2)),
                        startSector: extent.startSector,
                        sectorCount: extent.sectorCount
                    }))
                    currentIndex: addDialog.extentIndex
                    onActivated: addDialog.extentIndex = currentIndex
                    popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 6)
                    popupScrollBar: true
                }
            }
            Maui.FlexSectionItem {
                Layout.fillWidth: true
                label1.text: qsTr("Size (MiB)")
                Maui.TextField {
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /^[0-9]{1,12}$/ }
                    text: addDialog.sizeText
                    onTextChanged: addDialog.sizeText = text
                }
            }
        }
        onAccepted: {
            const extent = dialog.availableExtents()[extentIndex]
            const sectors = Math.floor((Number(sizeText) * 1048576) / Number(dialog.diskInfo.sectorSize))
            if (extent && sectors > 0 && sectors <= extent.sectorCount)
                dialog.addPartition(extent.startSector, sectors)
        }
    }
}
