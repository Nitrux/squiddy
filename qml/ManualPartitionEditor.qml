import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import org.mauikit.controls as Maui

Maui.PopupPage {
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
    headBar.topPadding: Maui.Style.space.medium
    headBar.bottomPadding: Maui.Style.space.medium
    maxWidth: parent ? Math.min(parent.width - Maui.Style.contentMargins * 2,
                                Maui.Style.units.gridUnit * 58) : implicitWidth
    maxHeight: parent
        ? Math.min(parent.height - Maui.Style.contentMargins * 2, implicitHeight)
        : implicitHeight
    onOpened: resetDraft()

    actions: [
        Action {
            text: qsTr("Cancel")
            onTriggered: dialog.close()
        },
        Action {
            text: qsTr("Apply")
            Maui.Controls.status: Maui.Controls.Positive
            onTriggered: {
                dialog.partitionLayoutAccepted(dialog.draft)
                dialog.close()
            }
        }
    ]

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

    function sizeDisplay(bytes) {
        const value = Number(bytes)
        if (!Number.isFinite(value) || value <= 0) return qsTr("Unknown")
        if (value >= 1073741824) return qsTr("%1 GiB").arg((value / 1073741824).toFixed(2))
        return qsTr("%1 MiB").arg(Math.max(1, Math.round(value / 1048576)))
    }

    function mountPointLabel(role) {
        if (role === "/boot/efi") return qsTr("/boot/efi")
        if (role === "/") return qsTr("/")
        if (role === "/home") return qsTr("/home")
        if (role === "/var/lib") return qsTr("/var/lib")
        if (role === "swap") return qsTr("Swap")
        return ""
    }

    function partitionFilesystem(row) {
        if (row.mountPoint === "/boot/efi") return qsTr("FAT32")
        if (row.mountPoint === "/") return qsTr("XFS")
        if (row.mountPoint === "/home" || row.mountPoint === "/var/lib") return qsTr("F2FS")
        if (row.mountPoint === "swap") return qsTr("Linuxswap")
        return row.filesystem || dialog.typeLabel(row.typeGuid)
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

    stack: ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Maui.Style.space.small

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Maui.Style.contentMargins
            Layout.topMargin: Maui.Style.space.medium
            Layout.bottomMargin: Maui.Style.space.medium
            Maui.SectionHeader {
                Layout.fillWidth: true
                text2: qsTr("Changes apply at installation. Removed or formatted partitions lose data.")
                label2.wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("Revert All Changes")
                enabled: dialog.draft.some(partition => partition.action !== "keep")
                    || dialog.draft.some(partition => !!partition.mountPoint)
                onClicked: dialog.resetDraft(true)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Maui.Style.contentMargins
            Layout.rightMargin: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Item {
                id: partitionMap
                Layout.fillWidth: true
                Layout.preferredHeight: Maui.Style.rowHeight * 0.55
                readonly property real totalSectors: Math.max(1,
                    Math.floor(Number(dialog.diskInfo.sizeBytes) / Number(dialog.diskInfo.sectorSize)))

                Rectangle {
                    anchors.fill: parent
                    radius: Maui.Style.radiusV
                    color: Qt.rgba(Maui.Theme.textColor.r, Maui.Theme.textColor.g,
                                   Maui.Theme.textColor.b, 0.08)
                    border.color: Qt.rgba(Maui.Theme.textColor.r, Maui.Theme.textColor.g,
                                          Maui.Theme.textColor.b, 0.25)
                    border.width: 1
                }
                Repeater {
                    model: dialog.draft.filter(row => row.action !== "delete")
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        readonly property real segmentHue: (index * 0.19 + 0.54) % 1
                        readonly property color segmentColor: Qt.hsla(segmentHue, 0.52, 0.48, 1)
                        readonly property color segmentBorderColor: Qt.hsla(segmentHue, 0.52, 0.40, 1)
                        x: Math.max(0, Number(modelData.startSector) / partitionMap.totalSectors * partitionMap.width)
                        width: Math.max(0, Number(modelData.sectorCount) / partitionMap.totalSectors * partitionMap.width)
                        height: partitionMap.height
                        color: segmentColor
                        border.color: segmentBorderColor
                        border.width: 1
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.topMargin: Maui.Style.contentMargins + Maui.Style.space.medium - Maui.Style.space.small
                implicitHeight: partitionLegend.implicitHeight
                Row {
                    id: partitionLegend
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Maui.Style.space.small
                    Repeater {
                        model: dialog.draft.filter(row => row.action !== "delete")
                        delegate: RowLayout {
                            required property int index
                            required property var modelData
                            spacing: Maui.Style.space.tiny
                            Rectangle {
                                Layout.preferredWidth: Maui.Style.iconSizes.small
                                Layout.preferredHeight: Maui.Style.iconSizes.small
                                radius: Maui.Style.radiusV / 2
                                color: Qt.hsla((index * 0.19 + 0.54) % 1, 0.52, 0.48, 1)
                            }
                            Label {
                                text: qsTr("%1 · %2")
                                    .arg(dialog.partitionDisplayName(modelData.device))
                                    .arg(dialog.sizeDisplay(modelData.sizeBytes))
                                color: Maui.Theme.textColor
                                opacity: 0.8
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: partitionTable
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: Maui.Style.contentMargins + Maui.Style.space.medium - Maui.Style.space.small
                Layout.preferredHeight: Maui.Style.rowHeight * Math.max(4, dialog.draft.length + 2)
                Layout.minimumHeight: Maui.Style.rowHeight * Math.max(4, dialog.draft.length + 2)
                color: "transparent"
                border.color: Qt.rgba(Maui.Theme.textColor.r, Maui.Theme.textColor.g,
                                      Maui.Theme.textColor.b, 0.25)
                border.width: 1
                radius: Maui.Style.radiusV

                ColumnLayout {
                    id: partitionTableContent
                    readonly property real nameStart: 0.00
                    readonly property real nameWidth: 0.23
                    readonly property real filesystemStart: 0.25
                    readonly property real filesystemWidth: 0.15
                    readonly property real labelStart: 0.42
                    readonly property real labelWidth: 0.20
                    readonly property real mountPointStart: 0.64
                    readonly property real mountPointWidth: 0.20
                    readonly property real sizeStart: 0.86
                    readonly property real sizeWidth: 0.14
                    readonly property real firstSeparator: 0.24
                    readonly property real secondSeparator: 0.41
                    readonly property real thirdSeparator: 0.63
                    readonly property real fourthSeparator: 0.85
                    readonly property color separatorColor: Qt.rgba(Maui.Theme.textColor.r,
                        Maui.Theme.textColor.g, Maui.Theme.textColor.b, 0.18)
                    anchors.fill: parent
                    anchors.bottomMargin: Maui.Style.space.small
                    anchors.leftMargin: 0
                    anchors.rightMargin: 0
                    spacing: Maui.Style.space.small

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Maui.Style.rowHeight
                        color: "transparent"
                        Shape {
                            id: headerBackground
                            anchors.fill: parent
                            anchors.margins: partitionTable.border.width
                            readonly property real cornerRadius: Math.max(0,
                                partitionTable.radius - partitionTable.border.width)

                            ShapePath {
                                fillColor: Qt.rgba(Maui.Theme.textColor.r, Maui.Theme.textColor.g,
                                                   Maui.Theme.textColor.b, 0.04)
                                strokeColor: "transparent"
                                startX: headerBackground.cornerRadius
                                startY: 0
                                PathLine { x: headerBackground.width - headerBackground.cornerRadius; y: 0 }
                                PathArc {
                                    x: headerBackground.width
                                    y: headerBackground.cornerRadius
                                    radiusX: headerBackground.cornerRadius
                                    radiusY: headerBackground.cornerRadius
                                    direction: PathArc.Clockwise
                                }
                                PathLine { x: headerBackground.width; y: headerBackground.height }
                                PathLine { x: 0; y: headerBackground.height }
                                PathLine { x: 0; y: headerBackground.cornerRadius }
                                PathArc {
                                    x: headerBackground.cornerRadius
                                    y: 0
                                    radiusX: headerBackground.cornerRadius
                                    radiusY: headerBackground.cornerRadius
                                    direction: PathArc.Clockwise
                                }
                            }
                        }
                        Item {
                            anchors.fill: parent
                            anchors.leftMargin: Maui.Style.space.tiny
                            anchors.rightMargin: Maui.Style.space.tiny
                            Label { x: parent.width * partitionTableContent.nameStart + Maui.Style.space.small; width: Math.max(0, parent.width * partitionTableContent.nameWidth - Maui.Style.space.small); anchors.verticalCenter: parent.verticalCenter; text: qsTr("Name"); font: Maui.Style.h2Font; elide: Text.ElideRight }
                            Label { x: parent.width * partitionTableContent.filesystemStart; width: parent.width * partitionTableContent.filesystemWidth; anchors.verticalCenter: parent.verticalCenter; text: qsTr("File System"); font: Maui.Style.h2Font; elide: Text.ElideRight }
                            Label { x: parent.width * partitionTableContent.labelStart; width: parent.width * partitionTableContent.labelWidth; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Label"); font: Maui.Style.h2Font; elide: Text.ElideRight }
                            Label { x: parent.width * partitionTableContent.mountPointStart; width: parent.width * partitionTableContent.mountPointWidth; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Mount Point"); font: Maui.Style.h2Font; elide: Text.ElideRight }
                            Label { x: parent.width * partitionTableContent.sizeStart + Maui.Style.space.small; width: Math.max(0, parent.width * partitionTableContent.sizeWidth - Maui.Style.space.small * 2); anchors.verticalCenter: parent.verticalCenter; text: qsTr("Size"); font: Maui.Style.h2Font; elide: Text.ElideRight }
                            Rectangle { x: parent.width * partitionTableContent.firstSeparator; width: partitionTable.border.width; height: parent.height; color: partitionTableContent.separatorColor }
                            Rectangle { x: parent.width * partitionTableContent.secondSeparator; width: partitionTable.border.width; height: parent.height; color: partitionTableContent.separatorColor }
                            Rectangle { x: parent.width * partitionTableContent.thirdSeparator; width: partitionTable.border.width; height: parent.height; color: partitionTableContent.separatorColor }
                            Rectangle { x: parent.width * partitionTableContent.fourthSeparator; width: partitionTable.border.width; height: parent.height; color: partitionTableContent.separatorColor }
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: partitionTable.border.width
                            anchors.rightMargin: partitionTable.border.width
                            height: partitionTable.border.width
                            color: partitionTable.border.color
                        }
                    }

                    Item {
                        id: partitionRowsArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: partitionRows
                            anchors.fill: parent
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            boundsMovement: Flickable.StopAtBounds
                            model: dialog.draft.map(row => ({
                                device: row.device,
                                mountPoint: row.mountPoint,
                                filesystemLabel: row.filesystemLabel,
                                label: row.label,
                                filesystem: row.filesystem,
                                typeGuid: row.typeGuid,
                                action: row.action,
                                sizeBytes: row.sizeBytes
                            }))
                            spacing: Maui.Style.space.tiny
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                required property int index
                                required property string device
                                required property string mountPoint
                                required property string filesystemLabel
                                required property string label
                                required property string filesystem
                                required property string typeGuid
                                required property string action
                                required property var sizeBytes
                                width: partitionRows.width
                                height: Maui.Style.rowHeight
                                radius: Maui.Style.radiusV / 2
                                color: dialog.selectedIndex === index
                                    ? Qt.rgba(Maui.Theme.textColor.r, Maui.Theme.textColor.g,
                                              Maui.Theme.textColor.b, 0.1) : "transparent"
                                opacity: action === "delete" ? 0.5 : 1
                                border.color: dialog.selectedIndex === index
                                    ? Maui.Theme.highlightColor : "transparent"
                                border.width: 1
                                TapHandler { onTapped: dialog.selectedIndex = index }

                                Item {
                                    anchors.fill: parent
                                    anchors.leftMargin: Maui.Style.space.tiny
                                    anchors.rightMargin: Maui.Style.space.tiny
                                    Label {
                                        x: parent.width * partitionTableContent.nameStart + Maui.Style.space.small
                                        width: Math.max(0, parent.width * partitionTableContent.nameWidth - Maui.Style.space.small)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: dialog.partitionDisplayName(device)
                                        elide: Text.ElideMiddle
                                        font.strikeout: action === "delete"
                                    }
                                    Label {
                                        x: parent.width * partitionTableContent.filesystemStart
                                        width: parent.width * partitionTableContent.filesystemWidth
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: dialog.partitionFilesystem({mountPoint: mountPoint, filesystem: filesystem, typeGuid: typeGuid})
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        x: parent.width * partitionTableContent.labelStart
                                        width: parent.width * partitionTableContent.labelWidth
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: mountPoint ? filesystemLabel : label
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        x: parent.width * partitionTableContent.mountPointStart
                                        width: parent.width * partitionTableContent.mountPointWidth
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: dialog.mountPointLabel(mountPoint)
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        x: parent.width * partitionTableContent.sizeStart + Maui.Style.space.small
                                        width: Math.max(0, parent.width * partitionTableContent.sizeWidth - Maui.Style.space.small * 2)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: dialog.sizeDisplay(sizeBytes)
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: dialog.draft.length === 0
                            text: qsTr("No partitions are present on this disk.")
                            color: Maui.Theme.textColor
                            opacity: 0.7
                        }
                    }
                }

                Repeater {
                    model: [partitionTableContent.firstSeparator,
                            partitionTableContent.secondSeparator,
                            partitionTableContent.thirdSeparator,
                            partitionTableContent.fourthSeparator]
                    delegate: Rectangle {
                        required property real modelData
                        x: partitionTableContent.x + Maui.Style.space.tiny
                            + (partitionRowsArea.width - Maui.Style.space.tiny * 2) * modelData
                        y: partitionTableContent.y + partitionRowsArea.y
                        width: partitionTable.border.width
                        height: partitionRowsArea.height
                        color: partitionTableContent.separatorColor
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Maui.Style.contentMargins
            Layout.rightMargin: Maui.Style.contentMargins
            Layout.topMargin: Maui.Style.space.medium
            Layout.bottomMargin: Maui.Style.space.medium
            Button {
                text: qsTr("New Partition Table")
                enabled: !dialog.diskInfo.gpt || dialog.draft.some(partition => partition.action !== "delete")
                onClicked: newTableDialog.open()
            }
            Item { Layout.fillWidth: true }
            Maui.ToolActions {
                display: ToolButton.IconOnly
                checkable: false
                flat: true

                Action {
                    text: qsTr("Create")
                    icon.name: "list-add"
                    enabled: dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed
                    onTriggered: addDialog.open()
                }
                Action {
                    text: qsTr("Edit")
                    icon.name: "document-edit"
                    enabled: dialog.selectedIndex >= 0
                        && dialog.draft[dialog.selectedIndex].action !== "delete"
                        && (dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed)
                    onTriggered: editPartitionDialog.open()
                }
                Action {
                    text: qsTr("Delete")
                    icon.name: "list-remove"
                    enabled: dialog.selectedIndex >= 0
                        && dialog.draft[dialog.selectedIndex].action !== "delete"
                    onTriggered: {
                        const row = dialog.draft[dialog.selectedIndex]
                        if (row.action === "create")
                            dialog.draft = dialog.draft.filter((_, index) => index !== dialog.selectedIndex)
                        else
                            dialog.updateRow(dialog.selectedIndex, {action: "delete", mountPoint: ""})
                        dialog.selectedIndex = -1
                    }
                }
            }
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
        id: editPartitionDialog
        title: dialog.selectedIndex >= 0 && dialog.selectedIndex < dialog.draft.length
            && dialog.draft[dialog.selectedIndex].action === "create"
            ? qsTr("Create Partition") : qsTr("Edit Existing Partition")
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Apply
        property string contentValue: "keep"
        property string mountPointValue: ""
        property string sizeText: ""
        property string labelText: ""
        property bool encryptedValue: false

        onOpened: {
            if (dialog.selectedIndex < 0 || dialog.selectedIndex >= dialog.draft.length)
                return
            const row = dialog.draft[dialog.selectedIndex]
            contentValue = row.format || row.action === "create" ? "format" : "keep"
            mountPointValue = row.mountPoint || ""
            sizeText = String(Math.floor(Number(row.sectorCount) * Number(dialog.diskInfo.sectorSize) / 1048576))
            labelText = row.mountPoint ? (row.filesystemLabel || "") : (row.label || "")
            encryptedValue = !!row.encrypted
        }
        onApplied: {
            if (dialog.selectedIndex < 0 || dialog.selectedIndex >= dialog.draft.length)
                return
            const row = dialog.draft[dialog.selectedIndex]
            const sizeMiB = Number(sizeText)
            const sectorCount = Math.floor((sizeMiB * 1048576) / Number(dialog.diskInfo.sectorSize))
            if (sectorCount < 1)
                return
            const formatPartition = contentValue === "format"
            const role = formatPartition ? mountPointValue : ""
            const encrypted = !!role && role !== "/boot/efi" && encryptedValue
            const action = row.action === "create" ? "create"
                : (sectorCount !== Number(row.originalSectorCount) ? "resize" : row.action)
            const label = labelText || (role ? dialog.defaultLabel(role) : "")
            dialog.updateRow(dialog.selectedIndex, {
                mountPoint: role,
                sectorCount: sectorCount,
                sizeBytes: sectorCount * Number(dialog.diskInfo.sectorSize),
                action: action,
                label: label,
                filesystemLabel: role ? label : "",
                encrypted: encrypted,
                format: formatPartition && !!role,
                plannedTypeGuid: role ? dialog.typeForRole(role, encrypted) : row.typeGuid
            })
        }

        contentItem: ColumnLayout {
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: dialog.selectedIndex >= 0 && dialog.selectedIndex < dialog.draft.length
                    ? dialog.partitionDisplayName(dialog.draft[dialog.selectedIndex].device) : qsTr("Partition")
                text2: dialog.selectedIndex >= 0 && dialog.selectedIndex < dialog.draft.length
                    ? dialog.sizeDisplay(dialog.draft[dialog.selectedIndex].sizeBytes) : ""
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                label1.text: qsTr("Size")
                Maui.TextField {
                    Layout.fillWidth: true
                    text: editPartitionDialog.sizeText
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /^[0-9]{1,12}$/ }
                    onTextChanged: editPartitionDialog.sizeText = text
                    placeholderText: qsTr("MiB")
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                label1.text: qsTr("Content")
                template.content: RowLayout {
                    ButtonGroup { id: contentButtonGroup }
                    RadioButton {
                        text: qsTr("Keep")
                        checked: editPartitionDialog.contentValue === "keep"
                        ButtonGroup.group: contentButtonGroup
                        onToggled: if (checked) editPartitionDialog.contentValue = "keep"
                    }
                    RadioButton {
                        text: qsTr("Format")
                        checked: editPartitionDialog.contentValue === "format"
                        ButtonGroup.group: contentButtonGroup
                        enabled: dialog.diskInfo.gpt || dialog.newPartitionTableConfirmed
                        onToggled: if (checked) editPartitionDialog.contentValue = "format"
                    }
                }
            }

            Maui.SectionHeader {
                Layout.fillWidth: true
                visible: editPartitionDialog.contentValue === "format"
                text2: qsTr("Formatting erases all data on this partition.")
                label2.wrapMode: Text.WordWrap
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                visible: editPartitionDialog.contentValue === "format"
                label1.text: qsTr("File System")
                ComboBox {
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [{
                        text: editPartitionDialog.mountPointValue.length > 0
                            ? dialog.partitionFilesystem({mountPoint: editPartitionDialog.mountPointValue})
                            : qsTr("Choose a mount point first"),
                        value: editPartitionDialog.mountPointValue
                    }]
                    currentIndex: 0
                    enabled: false
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                visible: editPartitionDialog.contentValue === "format"
                label1.text: qsTr("Mount Point")
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
                    currentIndex: Math.max(0, model.findIndex(item => item.value === editPartitionDialog.mountPointValue))
                    onActivated: editPartitionDialog.mountPointValue = currentValue
                    popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 7)
                    popupScrollBar: true
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                visible: editPartitionDialog.contentValue === "format"
                label1.text: qsTr("FS Label")
                Maui.TextField {
                    Layout.fillWidth: true
                    text: editPartitionDialog.labelText
                    maximumLength: editPartitionDialog.mountPointValue === "/boot/efi" ? 11 : 36
                    validator: RegularExpressionValidator {
                        regularExpression: editPartitionDialog.mountPointValue === "/boot/efi"
                            ? /^[A-Za-z0-9_]{1,11}$/ : /^[A-Za-z0-9 _.-]{0,36}$/
                    }
                    onTextChanged: editPartitionDialog.labelText = text
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                visible: editPartitionDialog.contentValue === "format"
                    && editPartitionDialog.mountPointValue.length > 0
                    && editPartitionDialog.mountPointValue !== "/boot/efi"
                label1.text: qsTr("Encrypt")
                template.content: Switch {
                    checked: editPartitionDialog.encryptedValue
                    onToggled: editPartitionDialog.encryptedValue = checked
                }
            }
        }
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
            if (extent && sectors > 0 && sectors <= extent.sectorCount) {
                dialog.addPartition(extent.startSector, sectors)
                Qt.callLater(() => editPartitionDialog.open())
            }
        }
    }
}
