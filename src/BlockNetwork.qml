import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 8
    readonly property var net: backend.network
    readonly property bool hasWifi: !!backend.capabilities.wifi

    function savedUuid(ssid) {
        var saved = net.saved
        for (var i = 0; i < saved.length; ++i)
            if (saved[i].name === ssid)
                return saved[i].uuid
        return ""
    }

    // The deliberate keep step. Doing nothing restores the previous connection,
    // which is what you want if this window just lost its network.
    AppPanel {
        Layout.fillWidth: true
        visible: net.awaitingKeep
        padding: 12
        border.color: theme.accent
        RowLayout {
            width: parent.width
            spacing: 10
            AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Keep this connection? Going back to the previous one in %n second(s).", "", net.keepSeconds) }
            AppButton { text: qsTr("Go back"); onClicked: net.revertConnection() }
            AppButton { text: qsTr("Keep"); style: "primary"; onClicked: net.keepConnection() }
        }
    }

    AppText { visible: net.status === "unavailable"; text: qsTr("NetworkManager is not responding."); color: theme.dangerText }

    BlockHeading { Layout.fillWidth: true; title: qsTr("Connections"); visible: net.devices.length > 0 }
    AppPanel {
        Layout.fillWidth: true
        visible: net.devices.length > 0
        Repeater {
            model: net.devices
            delegate: Item {
                width: parent ? parent.width : 400
                height: theme.rowHeight
                RowDivider {}
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    AppIcon { name: modelData.type === "wifi" ? "wifi" : "network"; tint: modelData.connected ? theme.accentText : theme.muted }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.connection || modelData.device }
                        AppText { Layout.fillWidth: true; secondary: true; scale_: 0.88; text: modelData.device + " · " + modelData.state }
                    }
                    AppButton {
                        visible: modelData.connected && (modelData.type === "wifi" || modelData.type === "ethernet")
                        text: qsTr("Disconnect")
                        onClicked: confirm.open_(qsTr("Disconnect %1").arg(modelData.connection), qsTr("If this is your only connection, this computer goes offline."), [], qsTr("Disconnect"), false, function () { net.deactivate(modelData.device) })
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.preferredHeight: Math.round(theme.fontSize * 1.75)
        visible: hasWifi
        AppText { text: qsTr("Wi-Fi"); font.bold: true; scale_: 1.1; Layout.fillWidth: true }
        AppButton { style: "flat"; implicitHeight: Math.round(theme.fontSize * 1.75); text: net.busy ? qsTr("Scanning…") : qsTr("Scan"); enabled: !net.busy && net.wifiEnabled; onClicked: net.rescan() }
        AppSwitch { label: qsTr("Wi-Fi"); on: net.wifiEnabled; enabled: net.status === "ok"; onClicked: win.setWifi(!on) }
    }
    AppText { visible: !hasWifi; text: qsTr("No Wi-Fi adapter detected."); secondary: true }
    AppPanel {
        Layout.fillWidth: true
        visible: hasWifi && net.wifiEnabled && net.networks.length > 0
        Repeater {
            model: net.networks.slice(0, 12)
            delegate: Item {
                width: parent ? parent.width : 400
                height: theme.rowHeight
                RowDivider {}
                readonly property string uuid: savedUuid(modelData.ssid)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    AppIcon { name: "wifi"; tint: modelData.active ? theme.accentText : theme.muted }
                    AppText { Layout.fillWidth: true; text: modelData.ssid }
                    AppText { secondary: true; scale_: 0.88; text: qsTr("%1%").arg(modelData.signal) + (modelData.secured ? " · " + qsTr("Secured") : " · " + qsTr("Open")) }
                    AppButton {
                        visible: !modelData.active
                        enabled: !net.busy
                        Layout.preferredWidth: Math.round(theme.fontSize * 11.5)
                        text: parent.parent.uuid.length ? qsTr("Connect") : qsTr("Join in panel")
                        iconName: parent.parent.uuid.length ? "" : "external"
                        // Passwords belong to Omarchy's panel, never to this window.
                        onClicked: parent.parent.uuid.length ? net.activate(parent.parent.uuid, modelData.ssid) : win.activate(backend.entry("panel.network"))
                    }
                    AppText { visible: modelData.active; text: qsTr("Connected"); Layout.preferredWidth: Math.round(theme.fontSize * 11.5); horizontalAlignment: Text.AlignHCenter }
                }
            }
        }
    }
}
