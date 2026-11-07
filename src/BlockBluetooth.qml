import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Paired devices and the power switch. Pairing needs an agent to answer PIN prompts,
// which Omarchy's own panel provides, so that stays there. Rows are keyed by address, so
// two devices with one name stay two, and a refresh updates them in place.
ColumnLayout {
    id: root
    readonly property var bt: backend.bluetooth
    readonly property bool adapter: !!backend.capabilities.bluetooth
    readonly property bool working: adapter && bt.status === "ok"
    property var addresses: []
    spacing: 12

    function syncAddresses() {
        var next = bt.devices.map(function (device) { return device.address })
        if (JSON.stringify(next) !== JSON.stringify(addresses))
            addresses = next
    }
    Connections {
        target: root.bt
        function onChanged() { root.syncAddresses() }
    }
    Component.onCompleted: syncAddresses()
    function deviceFor(address) {
        for (var i = 0; i < bt.devices.length; ++i)
            if (bt.devices[i].address === address)
                return bt.devices[i]
        return ({ address: address, name: address })
    }
    // Names more than one paired device shares, which then need their address to tell apart.
    readonly property var sharedNames: {
        var seen = {}, shared = {}
        for (var i = 0; i < bt.devices.length; ++i) {
            var name = bt.devices[i].name
            if (seen[name])
                shared[name] = true
            seen[name] = true
        }
        return shared
    }
    function iconFor(kind) {
        if (/headphone|headset|audio/.test(kind || ""))
            return "headphones"
        if (/mouse/.test(kind || ""))
            return "mouse"
        if (/keyboard/.test(kind || ""))
            return "input"
        return "bluetooth"
    }
    function takeFocus() {
        if (power.visible && power.enabled)
            power.forceActiveFocus(Qt.TabFocusReason)
        else if (rows.count)
            rows.itemAt(0).takeFocus()
    }

    PanelHeading {
        Layout.fillWidth: true
        icon: "bluetooth"
        title: qsTr("Bluetooth")
        detail: root.working && root.bt.devices.length > 0 ? qsTr("%n paired device(s)", "", root.bt.devices.length) : ""
        AppSwitch {
            id: power
            objectName: "bluetoothPower"
            visible: root.adapter
            label: qsTr("Bluetooth")
            on: root.bt.powered
            busy: root.bt.busy
            unknown: root.bt.status === "loading"
            enabled: !root.bt.busy && root.bt.status === "ok"
            onClicked: root.bt.setPowered(!on)
        }
    }
    AppPanel {
        Layout.fillWidth: true
        Item {
            visible: message.text.length > 0
            width: parent.width
            height: visible ? message.implicitHeight + 28 : 0
            RowLayout {
                x: 16
                width: parent.width - 32
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                AppIcon {
                    visible: root.adapter && root.bt.status === "ok" && !root.bt.powered
                    name: "info"
                    size: 14
                    tint: theme.muted
                }
                AppText {
                    id: message
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: root.adapter && root.bt.status === "unavailable" ? theme.dangerText : theme.muted
                    text: !root.adapter ? qsTr("No Bluetooth adapter detected.")
                        : root.bt.status === "unavailable" ? qsTr("Bluetooth is not responding, so devices cannot be shown.")
                        : root.bt.status === "loading" && !root.bt.devices.length ? qsTr("Looking for paired devices…")
                        : !root.bt.powered ? qsTr("Bluetooth is off. Turn it on to connect devices.")
                        : !root.bt.devices.length ? qsTr("No paired devices. Pair new devices in the Omarchy Bluetooth panel.")
                        : ""
                }
            }
        }
        Rectangle {
            visible: message.text.length > 0 && rows.count > 0
            x: 14
            width: parent.width - 28
            height: 1
            color: theme.hairline
        }
        Repeater {
            id: rows
            model: root.adapter ? root.addresses : []
            delegate: Item {
                id: row
                objectName: "bluetoothDevice"
                readonly property string address: modelData
                readonly property var device: root.deviceFor(modelData)
                readonly property string action: root.bt.pending[modelData] || ""
                readonly property string failure: root.bt.failures[modelData] || ""
                function takeFocus() { primary.forceActiveFocus(Qt.TabFocusReason) }
                width: parent ? parent.width : 400
                implicitHeight: Math.max(theme.rowHeight, names.implicitHeight + 20)
                height: implicitHeight
                Rectangle {
                    visible: index < rows.count - 1
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    height: 1
                    color: theme.hairline
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 12
                    AppIcon {
                        name: root.iconFor(row.device.kind)
                        size: Math.round(theme.fontSize * 1.45)
                        tint: row.device.connected ? theme.accentText : theme.foreground
                    }
                    ColumnLayout {
                        id: names
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: row.device.name }
                        AppText {
                            Layout.fillWidth: true
                            secondary: !row.failure.length
                            color: row.failure.length ? theme.dangerText : theme.muted
                            wrapMode: row.failure.length ? Text.Wrap : Text.NoWrap
                            scale_: 0.88
                            text: {
                                var state = row.action === "connect" ? qsTr("Connecting…")
                                          : row.action === "disconnect" ? qsTr("Disconnecting…")
                                          : row.action === "forget" ? qsTr("Forgetting…")
                                          : row.failure.length ? row.failure
                                          : row.device.connected ? qsTr("Connected") : qsTr("Not connected")
                                if (!row.action.length && !row.failure.length && row.device.connected && row.device.battery >= 0)
                                    state += " · " + qsTr("Battery %1%").arg(row.device.battery)
                                // The end of the address is what differs; the whole one is read aloud.
                                if (root.sharedNames[row.device.name])
                                    state += " · …" + row.address.slice(-5)
                                return state
                            }
                        }
                    }
                    AppButton {
                        id: primary
                        enabled: !root.bt.busy && root.bt.powered && root.working
                        text: row.device.connected ? qsTr("Disconnect") : qsTr("Connect")
                        Accessible.name: (row.device.connected ? qsTr("Disconnect %1") : qsTr("Connect %1")).arg(row.device.name + (root.sharedNames[row.device.name] ? " " + row.address : ""))
                        onClicked: row.device.connected ? root.bt.disconnectDevice(row.address) : root.bt.connectDevice(row.address)
                    }
                    // Secondary on purpose; the confirmation is where it turns serious.
                    AppButton {
                        style: "flat"
                        enabled: !root.bt.busy && root.working
                        text: qsTr("Forget")
                        Accessible.name: qsTr("Forget %1").arg(row.device.name + (root.sharedNames[row.device.name] ? " " + row.address : ""))
                        onClicked: confirm.open_(qsTr("Forget %1").arg(row.device.name), qsTr("You will need to pair this device again to use it."), [], qsTr("Forget"), true, function () { root.bt.forgetDevice(row.address) })
                    }
                }
            }
        }
    }
}
