import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 8
    readonly property var services: backend.services
    property bool systemScope: false
    property bool expanded: services.failedCount > 0
    property string filter: ""
    readonly property var rows: (systemScope ? services.systemServices : services.userServices).filter(function (s) {
        return !filter.length || (s.unit + " " + s.description).toLowerCase().indexOf(filter.toLowerCase()) >= 0
    })

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.round(theme.fontSize * 1.75)
        AppText { text: qsTr("Services"); font.bold: true; scale_: 1.1 }
        AppText {
            Layout.fillWidth: true
            visible: root.services.failedCount > 0
            color: theme.warningText
            text: qsTr("%n failed", "", root.services.failedCount)
        }
        Item { Layout.fillWidth: !(root.services.failedCount > 0); width: 1 }
        AppButton {
            style: "flat"
            implicitHeight: Math.round(theme.fontSize * 1.75)
            text: root.expanded ? qsTr("Hide") : qsTr("Show")
            iconName: "chevron"
            iconRotation: root.expanded ? 180 : 0
            onClicked: root.expanded = !root.expanded
        }
    }
    ColumnLayout {
        visible: root.expanded
        Layout.fillWidth: true
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppButton { text: qsTr("Mine"); style: root.systemScope ? "outline" : "primary"; onClicked: root.systemScope = false }
            AppButton { text: qsTr("System"); style: root.systemScope ? "primary" : "outline"; onClicked: root.systemScope = true }
            AppField { Layout.fillWidth: true; label: qsTr("Filter services"); placeholderText: qsTr("Filter services"); onTextChanged: root.filter = text }
        }
        AppText {
            visible: root.systemScope
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            secondary: true
            scale_: 0.88
            text: qsTr("Changing a system service asks for administrator approval. Stopping the wrong one can disconnect or freeze this computer.")
        }
        AppPanel {
            Layout.fillWidth: true
            Repeater {
                model: root.expanded ? root.rows.slice(0, 120) : []
                delegate: Item {
                    width: parent ? parent.width : 400
                    height: theme.rowHeight
                    RowDivider {}
                    readonly property bool running: modelData.active === "active"
                    function act(action) {
                        if (root.systemScope && (action === "stop" || action === "disable" || action === "restart")) {
                            confirm.open_(qsTr("%1 %2").arg(action).arg(modelData.unit), qsTr("This is a system service. Other things may depend on it."), [], qsTr("Continue"), true, function () {
                                root.services.serviceAction(modelData.unit, action, true)
                            })
                            return
                        }
                        root.services.serviceAction(modelData.unit, action, root.systemScope)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            AppText { Layout.fillWidth: true; text: modelData.description || modelData.unit }
                            AppText {
                                Layout.fillWidth: true
                                scale_: 0.88
                                color: modelData.failed ? theme.dangerText : theme.muted
                                text: modelData.unit + " · " + (modelData.failed ? qsTr("failed") : modelData.active) + (modelData.enabled ? " · " + modelData.enabled : "")
                            }
                        }
                        AppButton { style: "flat"; visible: running || modelData.failed; text: qsTr("Restart"); onClicked: parent.parent.act("restart") }
                        AppButton { style: "flat"; Layout.minimumWidth: Math.round(theme.fontSize * 5.5); text: running ? qsTr("Stop") : qsTr("Start"); onClicked: parent.parent.act(running ? "stop" : "start") }
                        AppButton {
                            style: "flat"
                            Layout.minimumWidth: Math.round(theme.fontSize * 6)
                            visible: modelData.enabled === "enabled" || modelData.enabled === "disabled"
                            text: modelData.enabled === "enabled" ? qsTr("Disable") : qsTr("Enable")
                            onClicked: parent.parent.act(modelData.enabled === "enabled" ? "disable" : "enable")
                        }
                    }
                }
            }
        }
    }
}
