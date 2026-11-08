import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

Maui.ApplicationWindow {
    id: root

    title: qsTr("Nitrux Installer")

    readonly property var steps: [
        qsTr("Welcome"),
        qsTr("Locale"),
        qsTr("Keyboard"),
        qsTr("User"),
        qsTr("Partitioning"),
        qsTr("Summary")
    ]
    readonly property var stepDescriptions: [
        qsTr("Prepare to install Nitrux."),
        qsTr("Choose the language, regional formats, and time zone."),
        qsTr("Choose the keyboard layout for the installed system."),
        qsTr("Create the first user account for this installation."),
        qsTr("Choose how to prepare a GPT disk for Nitrux."),
        qsTr("Review your choices before installation.")
    ]

    property int currentStep: 0
    property bool simulationActive: false
    onCurrentStepChanged: if (currentStep === 5) installationPlan.runStoragePreflight()

    function goBack() {
        if (currentStep > 0)
            currentStep--
    }

    function goNext() {
        if (currentStep < steps.length - 1)
            currentStep++
    }

    Maui.SideBarView {
        id: shell
        anchors.fill: parent
        background: null

        sideBar.preferredWidth: Maui.Style.units.gridUnit * 12
        sideBar.minimumWidth: Maui.Style.units.gridUnit * 12
        sideBar.autoHide: true
        sideBar.autoShow: true
        sideBar.floats: sideBar.collapsed

        sideBarContent: Item {
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins

            Maui.ScrollColumn {
                anchors.fill: parent

                Maui.SectionHeader {
                    Layout.fillWidth: true
                    text1: qsTr("Nitrux Setup")
                    text2: qsTr("Installation steps")
                    label2.wrapMode: Text.Wrap
                }

                Repeater {
                    model: root.steps

                    delegate: Maui.ListDelegate {
                        required property int index
                        required property string modelData

                        Layout.fillWidth: true
                        label: modelData
                        iconVisible: false
                        isCurrentItem: index === root.currentStep
                        enabled: !root.simulationActive && !privilegedInstaller.busy
                            && !privilegedInstaller.installationFinished
                            && (installationPlan.debugMode || index <= root.currentStep)

                        onClicked: root.currentStep = index
                    }
                }
            }
        }

        Maui.Page {
            id: page
            anchors.fill: parent
            title: root.simulationActive ? qsTr("Debug simulation") : root.steps[root.currentStep]
            flickable: scrollColumn.flickable
            headerMargins: Maui.Handy.isMobile ? 0 : Maui.Style.contentMargins
            footerMargins: headerMargins
            background: null
            Maui.Controls.showCSD: true

            headBar.visible: true
            headBar.preferredHeight: Maui.Style.rowHeightAlt
            headBar.topPadding: Maui.Style.defaultPadding
            headBar.bottomPadding: Maui.Style.defaultPadding
            footBar.preferredHeight: Maui.Style.rowHeightAlt
            footBar.topPadding: Maui.Style.defaultPadding
            footBar.bottomPadding: Maui.Style.defaultPadding

            headBar.leftContent: ToolButton {
                icon.name: shell.sideBar.visible ? "sidebar-collapse" : "sidebar-expand"
                display: ToolButton.IconOnly
                checkable: true
                checked: shell.sideBar.visible
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Toggle setup steps")
                onClicked: {
                    if (shell.sideBar.visible)
                        shell.sideBar.close()
                    else
                        shell.sideBar.open()
                }
            }

            headBar.rightContent: Maui.ToolButtonMenu {
                icon.name: "overflow-menu"

                MenuItem {
                    text: qsTr("About")
                    icon.name: "documentinfo"
                    onTriggered: Maui.App.aboutDialog()
                }
            }

            Maui.ScrollColumn {
                id: scrollColumn
                anchors.fill: parent
                spacing: Maui.Style.space.big

                Maui.SectionHeader {
                    Layout.fillWidth: true
                    text1: root.simulationActive ? qsTr("Debug simulation") : root.steps[root.currentStep]
                    text2: root.simulationActive
                        ? qsTr("Installation phases are simulated. No commands or disk operations will run.")
                        : root.stepDescriptions[root.currentStep]
                    label2.wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Step %1 of %2").arg(root.currentStep + 1).arg(root.steps.length)
                    horizontalAlignment: Text.AlignHCenter
                    visible: !root.simulationActive
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: root.steps.length
                    value: root.currentStep + 1
                    visible: !root.simulationActive
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Maui.Style.units.gridUnit * 40
                    Layout.alignment: Qt.AlignHCenter
                    currentIndex: root.simulationActive ? root.steps.length : root.currentStep

                    WelcomeStep {
                        Layout.fillWidth: true
                    }
                    LocaleStep {
                        Layout.fillWidth: true
                    }
                    KeyboardStep {
                        Layout.fillWidth: true
                    }
                    UserStep {
                        Layout.fillWidth: true
                    }
                    PartitioningStep {
                        Layout.fillWidth: true
                    }
                    SummaryStep {
                        Layout.fillWidth: true
                    }
                    DebugSimulationStep {
                        id: debugSimulationStep
                        Layout.fillWidth: true
                        onReturnToSummary: root.simulationActive = false
                    }
                }
            }

            footBar.leftContent: Button {
                text: qsTr("Back")
                visible: !root.simulationActive
                enabled: root.currentStep > 0 && !privilegedInstaller.busy
                    && !privilegedInstaller.installationFinished
                onClicked: root.goBack()
            }

            footBar.rightContent: Button {
                visible: !root.simulationActive
                text: root.currentStep < root.steps.length - 1
                    ? qsTr("Next")
                    : (installationPlan.debugMode ? qsTr("Simulate installation")
                        : (privilegedInstaller.busy ? qsTr("Installing…") : qsTr("Install")))
                enabled: root.currentStep < root.steps.length - 1
                    || (installationPlan.debugMode && installationPlan.planValid)
                    || (!installationPlan.debugMode && installationPlan.planValid
                        && installationPlan.storagePreflightComplete
                        && !installationPlan.storagePreflightBusy && !privilegedInstaller.busy
                        && !privilegedInstaller.installationFinished)
                Maui.Controls.status: Maui.Controls.Positive
                onClicked: {
                    if (root.currentStep < root.steps.length - 1)
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
}
