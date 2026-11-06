import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 8
    readonly property var services: backend.services
    BlockHeading { Layout.fillWidth: true; title: qsTr("Startup apps") }
    AppText { visible: root.services.startupApps.length === 0; text: qsTr("No login items found."); secondary: true }
    AppPanel {
        Layout.fillWidth: true
        visible: root.services.startupApps.length > 0
        Repeater {
            model: root.services.startupApps
            delegate: Item {
                width: parent ? parent.width : 400
                height: 46
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.name }
                        AppText { Layout.fillWidth: true; secondary: true; scale_: 0.82; visible: text.length > 0; text: modelData.comment }
                    }
                    AppSwitch {
                        label: qsTr("Start %1 at login").arg(modelData.name)
                        on: modelData.enabled
                        onClicked: root.services.setStartupEnabled(modelData.id, !on)
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; secondary: true; scale_: 0.88; text: qsTr("Things started from Hyprland's config live in autostart.lua.") }
        AppButton { style: "link"; text: qsTr("Edit autostart.lua"); iconName: "external"; onClicked: win.activate(backend.entry("config.autostart")) }
    }
}
