import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

Maui.ApplicationWindow {
    id: root

    title: qsTr("Nitrux Installer")
    color: "transparent"
    background: null
    Maui.Theme.colorSet: Maui.Theme.Window

    readonly property var steps: [
        qsTr("Welcome"),
        qsTr("User"),
        qsTr("Partitioning"),
        qsTr("Summary")
    ]
    readonly property var stepDescriptions: [
        qsTr("Prepare to install Nitrux."),
        qsTr("Create the first user account for this installation."),
        qsTr("Choose how to prepare a GPT disk for Nitrux."),
        qsTr("Review your choices before installation.")
    ]

    property int currentStep: 0
    property bool simulationActive: false
    property bool showingIntro: true
    onCurrentStepChanged: if (currentStep === 3) installationPlan.runStoragePreflight()

    function goBack() {
        if (currentStep > 0 && !stepStack.busy) {
            stepStack.popCurrentItem()
            currentStep--
        }
    }

    function goNext() {
        if (currentStep < steps.length - 1 && !stepStack.busy) {
            stepStack.pushItem([welcomePage, userPage,
                                partitioningPage, summaryPage][currentStep + 1])
            currentStep++
        }
    }

    onSimulationActiveChanged: {
        if (stepStack) {
            if (simulationActive)
                stepStack.pushItem(debugPage)
            else if (stepStack.currentItem === debugPage)
                stepStack.popCurrentItem()
        }
    }

    Maui.WindowBlur {
        view: root
        geometry: Qt.rect(0, 0, root.width, root.height)
        windowRadius: Maui.Style.radiusV
        enabled: true
    }

    Rectangle {
        anchors.fill: parent
        color: Maui.Theme.backgroundColor
        opacity: 0.76
        radius: Maui.Style.radiusV
    }

    Item {
        visible: false

        WelcomeStep {
            id: welcomePage
            width: stepStack.width
        }

        ColumnLayout {
            id: userPage
            width: stepStack.width
            spacing: Maui.Style.space.big

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: root.steps[1]
                text2: root.stepDescriptions[1]
                label2.wrapMode: Text.Wrap
            }

            UserStep {
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            id: partitioningPage
            width: stepStack.width
            spacing: Maui.Style.space.big

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: root.steps[2]
                text2: root.stepDescriptions[2]
                label2.wrapMode: Text.Wrap
            }

            PartitioningStep {
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            id: summaryPage
            width: stepStack.width
            spacing: Maui.Style.space.big

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: root.steps[3]
                text2: root.stepDescriptions[3]
                label2.wrapMode: Text.Wrap
            }

            SummaryStep {
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            id: debugPage
            width: stepStack.width
            spacing: Maui.Style.space.big

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Debug simulation")
                text2: qsTr("Installation phases are simulated. No commands or disk operations will run.")
                label2.wrapMode: Text.Wrap
            }

            DebugSimulationStep {
                id: debugSimulationStep
                Layout.fillWidth: true
                onReturnToSummary: if (!stepStack.busy) root.simulationActive = false
            }
        }
    }

    Maui.Page {
        id: page
        anchors.fill: parent
        title: root.title
        flickable: scrollColumn.flickable
        headerMargins: Maui.Handy.isMobile ? 0 : Maui.Style.contentMargins
        footerMargins: headerMargins
        background: null
        Maui.Theme.colorSet: Maui.Theme.View
        headBar.visible: false
        footerContainer.visible: true
        footBar.visible: true
        footBar.preferredHeight: Maui.Style.rowHeightAlt
        footBar.topPadding: Maui.Style.defaultPadding
        footBar.bottomPadding: Maui.Style.defaultPadding

        Maui.ScrollColumn {
            id: scrollColumn
            anchors.fill: parent
            visible: !root.showingIntro || introToWelcomeAnimation.running
            opacity: 0
            spacing: Maui.Style.space.big
            Item {
                Layout.fillWidth: true
                implicitHeight: Math.max(scrollColumn.availableHeight, bodyLayout.implicitHeight)

                RowLayout {
                    id: bodyLayout
                    anchors.centerIn: parent
                    width: Math.min(parent.width, Maui.Style.units.gridUnit * 36)
                    spacing: Maui.Style.space.big

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: Maui.Style.units.gridUnit * 36
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                        spacing: Maui.Style.space.big

                        StackView {
                            id: stepStack
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            implicitHeight: currentItem ? currentItem.implicitHeight : 0
                            background: null
                            initialItem: welcomePage

                            Behavior on implicitHeight {
                                NumberAnimation {
                                    duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                                    easing.type: Easing.InOutCubic
                                }
                            }

                            pushEnter: Transition {
                                NumberAnimation {
                                    property: "opacity"
                                    from: 0
                                    to: 1
                                    duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                                }
                            }
                            pushExit: Transition {
                                NumberAnimation {
                                    property: "opacity"
                                    from: 1
                                    to: 0
                                    duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                                }
                            }
                            popEnter: Transition {
                                NumberAnimation {
                                    property: "opacity"
                                    from: 0
                                    to: 1
                                    duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                                }
                            }
                            popExit: Transition {
                                NumberAnimation {
                                    property: "opacity"
                                    from: 1
                                    to: 0
                                    duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                                }
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            id: introLayout
            anchors.centerIn: parent
            width: Math.min(parent.width - Maui.Style.contentMargins * 2,
                            Maui.Style.units.gridUnit * 36)
            spacing: Maui.Style.space.big
            visible: root.showingIntro || introToWelcomeAnimation.running

            RowLayout {
                Layout.fillWidth: true
                spacing: Maui.Style.space.big

                Maui.IconItem {
                    id: introLogo
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    imageSource: Qt.resolvedUrl("data/squiddy_welcome_logo.svg")
                    imageSizeHint: 64
                    opacity: 0
                    scale: 0.6
                    rotation: -12
                }

                ToolSeparator {
                    id: introSeparator
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: 16
                    topPadding: 0
                    bottomPadding: 0
                    opacity: introHeader.opacity

                    Binding {
                        target: introSeparator.contentItem
                        property: "color"
                        value: Qt.rgba(introHeader.label2.color.r,
                                       introHeader.label2.color.g,
                                       introHeader.label2.color.b,
                                       introHeader.label2.color.a * introHeader.label2.opacity)
                    }
                }

                Maui.SectionHeader {
                    id: introHeader
                    Layout.fillWidth: true
                    text1: nitruxVersion.length > 0
                        ? qsTr("Welcome to Nitrux %1").arg(nitruxVersion)
                        : qsTr("Welcome to Nitrux")
                    text2: qsTr("You can use this live media to install Nitrux or use it as a temporary system. You can start the installation anytime by launching the installer.")
                    label1.font.weight: Font.Normal
                    label1.font.pointSize: Maui.Style.fontSizes.enormous * 2
                    label1.wrapMode: Text.WordWrap
                    label2.wrapMode: Text.WordWrap
                    opacity: 0
                    transform: Translate {
                        id: introTitleOffset
                        y: Maui.Style.units.gridUnit * 2
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: introHeader.x + introHeader.leftPadding
                Layout.preferredHeight: implicitHeight
                spacing: Maui.Style.space.small
                opacity: introHeader.opacity

                Repeater {
                    model: [
                        {
                            icon: systemStatus.powerSource === "battery"
                                ? "battery-good" : "squiddy-ac-power",
                            text: systemStatus.powerSource === "battery"
                                ? qsTr("Battery") : qsTr("AC"),
                            emblem: systemStatus.batteryPercent >= 0
                                && systemStatus.batteryPercent < 25
                                ? "emblem-warning" : "emblem-default"
                        },
                        {
                            icon: "memory",
                            text: systemStatus.ramCapacity.length > 0
                                ? qsTr("RAM %1").arg(systemStatus.ramCapacity)
                                : qsTr("RAM unavailable"),
                            emblem: systemStatus.lowRam ? "emblem-warning" : "emblem-default"
                        },
                        {
                            icon: systemStatus.networkIconName,
                            text: systemStatus.networkType === "wifi" ? qsTr("Wi-Fi")
                                : (systemStatus.networkType === "ethernet"
                                    ? qsTr("Ethernet") : qsTr("No network adapter")),
                            emblem: systemStatus.networkConnected
                                ? "emblem-default" : "emblem-error"
                        }
                    ]

                    delegate: Maui.Chip {
                        id: statusChip
                        required property int index
                        required property var modelData
                        enabled: index !== 2 || systemStatus.networkType !== "none"
                        hoverEnabled: false
                        focusPolicy: Qt.NoFocus
                        color: Qt.rgba(0, 0, 0, 0.3)
                        background: Rectangle {
                            color: statusChip.color
                            radius: Maui.Style.radiusV
                        }
                        label.font.weight: Font.Medium
                        implicitWidth: indicatorRow.implicitWidth + leftPadding + rightPadding
                        implicitHeight: indicatorRow.implicitHeight + Maui.Style.space.small * 2

                        contentItem: RowLayout {
                            id: indicatorRow
                            spacing: Maui.Style.space.small

                            Maui.Icon {
                                Layout.preferredWidth: Maui.Style.iconSizes.small
                                Layout.preferredHeight: Maui.Style.iconSizes.small
                                source: modelData.icon
                            }

                            Maui.IconLabel {
                                display: ToolButton.TextOnly
                                text: modelData.text
                                font.weight: Font.Medium
                            }

                            Maui.Icon {
                                Layout.preferredWidth: Maui.Style.iconSizes.small
                                Layout.preferredHeight: Maui.Style.iconSizes.small
                                source: modelData.emblem
                            }
                        }
                    }
                }
            }

            SequentialAnimation {
                id: introAnimation
                running: root.visible && root.showingIntro

                PauseAnimation {
                    duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration : 0
                }
                ParallelAnimation {
                    NumberAnimation {
                        target: introLogo
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 3 : 0
                    }
                    NumberAnimation {
                        target: introLogo
                        property: "scale"
                        from: 0.6
                        to: 1
                        duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 3 : 0
                        easing.type: Easing.OutBack
                    }
                    NumberAnimation {
                        target: introLogo
                        property: "rotation"
                        from: -12
                        to: 0
                        duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 3 : 0
                        easing.type: Easing.OutCubic
                    }
                }
                ParallelAnimation {
                    NumberAnimation {
                        target: introHeader
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 3 : 0
                    }
                    NumberAnimation {
                        target: introTitleOffset
                        property: "y"
                        from: Maui.Style.units.gridUnit * 2
                        to: 0
                        duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 3 : 0
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        ParallelAnimation {
            id: introToWelcomeAnimation
            onFinished: introBackButton.opacity = Qt.binding(function() {
                return introBackButton.enabled ? 1 : 0.5
            })

            NumberAnimation {
                target: introLayout
                property: "opacity"
                from: 1
                to: 0
                duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                easing.type: Easing.InOutCubic
            }
            NumberAnimation {
                target: scrollColumn
                property: "opacity"
                from: 0
                to: 1
                duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                easing.type: Easing.InOutCubic
            }
            NumberAnimation {
                target: introBackButton
                property: "opacity"
                from: 1
                to: 0
                duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                easing.type: Easing.InOutCubic
            }
            NumberAnimation {
                target: stepIndicator
                property: "opacity"
                from: 0
                to: 1
                duration: Maui.Style.enableEffects ? Maui.Style.units.longDuration * 2 : 0
                easing.type: Easing.InOutCubic
            }
        }

        footBar.farLeftContent: [
            Button {
                id: quitInstallerButton
                text: qsTr("Quit Installer")
                visible: !root.showingIntro && !introToWelcomeAnimation.running
                    && !privilegedInstaller.busy && !privilegedInstaller.installationFinished
                Maui.Controls.status: Maui.Controls.Negative
                onClicked: Qt.quit()
            },
            ToolSeparator {
                visible: quitInstallerButton.visible && introBackButton.visible
                topPadding: 10
                bottomPadding: 10
            }
        ]

        footBar.leftContent: Button {
            id: introBackButton
            text: root.showingIntro || introToWelcomeAnimation.running ? qsTr("Not Now") : qsTr("Previous Step")
            visible: root.showingIntro || introToWelcomeAnimation.running
                || (!root.simulationActive && root.currentStep > 0)
            enabled: root.showingIntro || introToWelcomeAnimation.running
                ? !introAnimation.running
                : root.currentStep > 0 && !stepStack.busy && !privilegedInstaller.busy
                    && !privilegedInstaller.installationFinished
            Maui.Controls.status: root.showingIntro || introToWelcomeAnimation.running
                ? Maui.Controls.Negative : Maui.Controls.Normal
            onClicked: {
                if (introToWelcomeAnimation.running)
                    return
                if (root.showingIntro)
                    Qt.quit()
                else
                    root.goBack()
            }
        }

        footBar.middleContent: PageIndicator {
            id: stepIndicator
            Layout.alignment: Qt.AlignHCenter
            count: root.steps.length
            currentIndex: root.currentStep
            visible: !root.showingIntro && !root.simulationActive
            opacity: 0

            delegate: Rectangle {
                implicitWidth: Maui.Style.iconSizes.small / 2
                implicitHeight: implicitWidth
                radius: width / 2
                color: index === stepIndicator.currentIndex
                    ? Maui.Theme.highlightColor
                    : Maui.Theme.textColor
                opacity: index === stepIndicator.currentIndex ? 1 : 0.4
            }
        }

        footBar.rightContent: Button {
            visible: root.showingIntro || !root.simulationActive
            text: root.showingIntro ? qsTr("Install Nitrux")
                : (root.currentStep < root.steps.length - 1
                    ? qsTr("Continue")
                    : (installationPlan.debugMode ? qsTr("Simulate installation")
                        : (privilegedInstaller.busy ? qsTr("Installing…") : qsTr("Install"))))
            enabled: root.showingIntro ? !introAnimation.running
                : !stepStack.busy && (root.currentStep < root.steps.length - 1
                    || (installationPlan.debugMode && installationPlan.planValid)
                    || (!installationPlan.debugMode && installationPlan.planValid
                        && installationPlan.storagePreflightComplete
                        && !installationPlan.storagePreflightBusy && !privilegedInstaller.busy
                        && !privilegedInstaller.installationFinished))
            Maui.Controls.status: Maui.Controls.Positive
            onClicked: {
                if (introToWelcomeAnimation.running)
                    return
                if (root.showingIntro) {
                    introToWelcomeAnimation.start()
                    root.showingIntro = false
                } else if (root.currentStep < root.steps.length - 1)
                    root.goNext()
                else if (installationPlan.debugMode && installationPlan.validateAndBuildPlan()) {
                    debugSimulationStep.reset()
                    root.simulationActive = true
                } else if (!installationPlan.debugMode) {
                    privilegedInstaller.beginInstall()
                }
            }
        }
    }
}
