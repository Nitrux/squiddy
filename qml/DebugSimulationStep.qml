import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

StepCard {
    id: root

    signal returnToSummary()

    readonly property var phases: [
        qsTr("Prepare storage"),
        qsTr("Install the base system"),
        qsTr("Configure locale, keyboard, and storage"),
        qsTr("Configure the installed system"),
        qsTr("Configure the kernel and initramfs"),
        qsTr("Configure UEFI boot"),
        qsTr("Finalize"),
        qsTr("Finish and report success")
    ]
    property int completedPhases: 0
    property string output: qsTr("Debug mode is active. No commands will run and no disks will be changed.")

    function reset() {
        completedPhases = 0
        output = qsTr("Debug mode is active. No commands will run and no disks will be changed.")
    }

    function runNextPhase() {
        if (completedPhases >= phases.length)
            return

        completedPhases++
        output += qsTr("\n[%1/%2] %3 — simulated; no system changes made.")
            .arg(completedPhases)
            .arg(phases.length)
            .arg(phases[completedPhases - 1])
    }

    Maui.SectionHeader {
        Layout.fillWidth: true
        text1: qsTr("Simulated installation")
        text2: qsTr("Advance through the installer phases without executing them.")
        label2.wrapMode: Text.Wrap
    }

    Label {
        Layout.fillWidth: true
        text: completedPhases < phases.length
            ? qsTr("Next phase: %1").arg(phases[completedPhases])
            : qsTr("Simulation complete. No installer operations were performed.")
        wrapMode: Text.Wrap
    }

    ProgressBar {
        Layout.fillWidth: true
        from: 0
        to: phases.length
        value: completedPhases
    }

    TextArea {
        Layout.fillWidth: true
        Layout.preferredHeight: Maui.Style.rowHeightAlt * 6
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.WrapAnywhere
        text: root.output
    }

    Button {
        Layout.fillWidth: true
        text: completedPhases < phases.length ? qsTr("Simulate next phase") : qsTr("Return to summary")
        Maui.Controls.status: Maui.Controls.Positive
        onClicked: completedPhases < phases.length ? root.runNextPhase() : root.returnToSummary()
    }
}
