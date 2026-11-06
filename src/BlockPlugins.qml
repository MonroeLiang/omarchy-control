import QtQuick
import QtQuick.Layouts

// A plugin manifest says what a plugin is, not what it can be configured to do, so
// this offers lifecycle actions rather than inventing settings for arbitrary code.
ColumnLayout {
    id: root
    spacing: 8
    readonly property var plugins: backend.plugins
    property bool expanded: false
    property string filter: ""
    readonly property var visibleRows: plugins.plugins.filter(function (p) {
        return !filter.length || (p.name + " " + p.id).toLowerCase().indexOf(filter.toLowerCase()) >= 0
    })

    RowLayout {
        Layout.fillWidth: true
        AppText { text: qsTr("Plugins"); font.bold: true; scale_: 1.1; Layout.fillWidth: true }
        AppButton {
            style: "flat"
            text: root.expanded ? qsTr("Hide") : qsTr("Show %n", "", root.plugins.plugins.length)
            iconName: "chevron"
            onClicked: root.expanded = !root.expanded
        }
    }
    AppText {
        visible: root.plugins.status === "shell-down"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: theme.warning
        text: qsTr("The Omarchy shell is not responding, so which plugins are active is unknown. Installed plugins are listed from disk.")
    }
    AppField {
        visible: root.expanded
        Layout.fillWidth: true
        label: qsTr("Filter plugins")
        placeholderText: qsTr("Filter plugins")
        onTextChanged: root.filter = text
    }
    AppPanel {
        visible: root.expanded
        Layout.fillWidth: true
        Repeater {
            model: root.expanded ? root.visibleRows : []
            delegate: Item {
                width: parent ? parent.width : 400
                height: 48
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.name || modelData.id }
                        AppText {
                            Layout.fillWidth: true
                            secondary: true
                            scale_: 0.82
                            text: modelData.id + (modelData.clonedFrom ? " · " + qsTr("cloned from %1").arg(modelData.clonedFrom) : "") + (modelData.firstParty ? "" : " · " + qsTr("your plugin"))
                        }
                    }
                    AppButton {
                        visible: !!modelData.firstParty && root.plugins.status === "ok"
                        style: "flat"
                        text: qsTr("Clone")
                        onClicked: root.plugins.clonePlugin(modelData.id)
                    }
                    AppButton {
                        visible: !modelData.firstParty && root.plugins.status === "ok"
                        style: "flat"
                        text: qsTr("Remove")
                        onClicked: confirm.open_(qsTr("Remove %1").arg(modelData.name || modelData.id), qsTr("This deletes the plugin from your config."), [], qsTr("Remove"), true, function () { root.plugins.removePlugin(modelData.id) })
                    }
                    AppSwitch {
                        visible: !modelData.unknownState
                        label: modelData.name || modelData.id
                        on: !!modelData.enabled
                        enabled: !!modelData.canDisable
                        onClicked: root.plugins.setEnabled(modelData.id, !on)
                    }
                }
            }
        }
    }
}
