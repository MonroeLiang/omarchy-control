import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 10
    readonly property var bt: backend.bluetooth
    readonly property bool adapter: !!backend.capabilities.bluetooth

    RowLayout {
        Layout.fillWidth: true
        AppText { text: qsTr("Bluetooth"); font.bold: true; scale_: 1.1; Layout.fillWidth: true }
        AppSwitch { visible: adapter; label: qsTr("Bluetooth"); on: bt.powered; busy: bt.busy; enabled: !bt.busy && bt.status === "ok"; onClicked: bt.setPowered(!on) }
    }
    AppText { visible: !adapter; text: qsTr("No Bluetooth adapter detected."); secondary: true }
    AppText { visible: adapter && bt.status === "ok" && bt.devices.length === 0; text: qsTr("No paired devices. Pair new devices in the Omarchy Bluetooth panel."); secondary: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
    AppPanel {
        Layout.fillWidth: true
        visible: adapter && bt.devices.length > 0
        Repeater {
            model: bt.devices
            delegate: Item {
                width: parent ? parent.width : 400
                height: 50
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    AppIcon { name: "bluetooth"; tint: modelData.connected ? theme.accent : theme.muted }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.name }
                        AppText {
                            Layout.fillWidth: true
                            secondary: true
                            scale_: 0.88
                            text: (modelData.connected ? qsTr("Connected") : qsTr("Not connected")) + (modelData.battery >= 0 ? " · " + qsTr("Battery %1%").arg(modelData.battery) : "")
                        }
                    }
                    AppButton {
                        enabled: !bt.busy && bt.powered
                        text: modelData.connected ? qsTr("Disconnect") : qsTr("Connect")
                        onClicked: modelData.connected ? bt.disconnectDevice(modelData.address) : bt.connectDevice(modelData.address)
                    }
                    AppButton {
                        enabled: !bt.busy
                        style: "flat"
                        text: qsTr("Forget")
                        onClicked: confirm.open_(qsTr("Forget %1").arg(modelData.name), qsTr("You will need to pair this device again to use it."), [], qsTr("Forget"), true, function () { bt.forgetDevice(modelData.address) })
                    }
                }
            }
        }
    }
}
