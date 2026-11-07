import QtQuick
import QtQuick.Layouts

// Every output, or every microphone, with the one in use marked. Rows are keyed by the
// device's name, so a fresh reading updates them in place instead of rebuilding them.
ColumnLayout {
    id: root
    property bool input: false
    readonly property var audio: backend.audio
    readonly property var devices: input ? audio.inputs : audio.outputs
    property var names: []
    spacing: 12

    function syncNames() {
        var next = devices.map(function (device) { return device.name })
        if (JSON.stringify(next) !== JSON.stringify(names))
            names = next
    }
    onDevicesChanged: syncNames()
    Component.onCompleted: syncNames()
    function deviceFor(name) {
        for (var i = 0; i < devices.length; ++i)
            if (devices[i].name === name)
                return devices[i]
        return ({})
    }
    function takeFocus() {
        if (rows.count)
            rows.itemAt(0).takeFocus()
    }

    PanelHeading {
        Layout.fillWidth: true
        icon: root.input ? "mic" : "sound"
        title: root.input ? qsTr("Microphone") : qsTr("Output")
        detail: !root.input && root.devices.length > 0 ? qsTr("%n output(s)", "", root.devices.length) : ""
    }
    AppPanel {
        Layout.fillWidth: true
        // Nothing to list says why, in the card where the list would be.
        Item {
            visible: message.text.length > 0
            width: parent.width
            height: visible ? message.implicitHeight + 28 : 0
            AppText {
                id: message
                x: 16
                width: parent.width - 32
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.Wrap
                color: root.audio.status === "unavailable" ? theme.dangerText : theme.muted
                text: root.audio.status === "unavailable" ? qsTr("PipeWire is not responding, so sound devices cannot be shown.")
                    : root.audio.status === "loading" && !root.devices.length ? qsTr("Looking for sound devices…")
                    : root.devices.length ? ""
                    : root.input ? qsTr("No microphone detected.") : qsTr("No sound output detected.")
            }
        }
        Repeater {
            id: rows
            model: root.names
            delegate: AudioDeviceRow {
                device: root.deviceFor(modelData)
                last: index === root.names.length - 1
                pending: root.audio.pendingDefault === modelData
                locked: root.audio.pendingDefault.length > 0
                onSelect: root.input ? root.audio.setDefaultInput(modelData) : root.audio.setDefaultOutput(modelData)
                onVolume: function (v) { root.input ? root.audio.setInputVolume(modelData, v) : root.audio.setVolume(modelData, v) }
                onMute: function (m) { root.input ? root.audio.setInputMute(modelData, m) : root.audio.setMute(modelData, m) }
            }
        }
    }
}
