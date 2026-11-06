import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 8
    readonly property var machine: backend.system.machine
    readonly property var caps: backend.capabilities
    BlockHeading { Layout.fillWidth: true; title: qsTr("Diagnostics") }
    AppPanel {
        Layout.fillWidth: true
        padding: 14
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 20
            rowSpacing: 4
            AppText { secondary: true; text: qsTr("Omarchy") }
            AppText { Layout.fillWidth: true; text: backend.omarchyVersion + (backend.compatible ? " — " + qsTr("tested") : " — " + qsTr("newer than tested")) }
            AppText { secondary: true; text: qsTr("Hyprland") }
            AppText { Layout.fillWidth: true; text: caps.hyprland ? (root.machine.hyprland || qsTr("Running")) : qsTr("Not reachable") }
            AppText { secondary: true; text: qsTr("Omarchy shell") }
            AppText { Layout.fillWidth: true; text: caps.shell ? qsTr("Responding") : qsTr("Not responding") }
            AppText { secondary: true; text: qsTr("Computer") }
            AppText { Layout.fillWidth: true; text: (root.machine.vendor || "") + " " + (root.machine.model || "") }
            AppText { secondary: true; text: qsTr("Kernel") }
            AppText { Layout.fillWidth: true; text: root.machine.kernel || "" }
            AppText { secondary: true; text: qsTr("Hyprland overrides") }
            AppText { Layout.fillWidth: true; text: caps["hypr.import"] ? qsTr("omarchy-control.lua is loaded") : qsTr("Not in use") }
        }
    }
    AppPanel {
        Layout.fillWidth: true
        visible: backend.compatChanges.length > 0
        padding: 12
        ColumnLayout {
            width: parent.width
            spacing: 6
            AppText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.warningText
                text: qsTr("These Omarchy commands changed since this app was tested. Their settings read normally, but changes are paused.")
            }
            Repeater {
                model: backend.compatChanges
                delegate: AppText { Layout.fillWidth: true; text: modelData; secondary: true }
            }
            AppButton {
                text: qsTr("Resume changes")
                onClicked: confirm.open_(qsTr("Resume changes"), qsTr("These settings may behave differently from what was tested. Every change is still verified by reading it back."), [], qsTr("Resume"), false, function () { backend.acceptCompat() })
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; secondary: true; scale_: 0.88; text: qsTr("Saves a report on this computer with secrets removed. Nothing is uploaded.") }
        AppButton { text: qsTr("Save report"); onClicked: backend.system.exportDiagnostics() }
    }
}
