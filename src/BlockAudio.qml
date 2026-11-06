import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 14
    readonly property var audio: backend.audio

    BlockHeading { Layout.fillWidth: true; title: qsTr("Output") }
    AppText { visible: audio.status === "unavailable"; text: qsTr("PipeWire is not responding, so sound devices cannot be shown."); color: theme.danger; Layout.fillWidth: true; wrapMode: Text.Wrap }
    AppPanel {
        Layout.fillWidth: true
        visible: audio.outputs.length > 0
        Repeater {
            model: audio.outputs
            delegate: AudioDeviceRow {
                device: modelData
                last: index === audio.outputs.length - 1
                onSelect: audio.setDefaultOutput(modelData.name)
                onVolume: function (v) { audio.setVolume(modelData.name, v) }
                onMute: function (m) { audio.setMute(modelData.name, m) }
            }
        }
    }

    BlockHeading { Layout.fillWidth: true; title: qsTr("Microphone") }
    AppText { visible: audio.status === "ok" && audio.inputs.length === 0; text: qsTr("No microphone detected."); secondary: true }
    AppPanel {
        Layout.fillWidth: true
        visible: audio.inputs.length > 0
        Repeater {
            model: audio.inputs
            delegate: AudioDeviceRow {
                device: modelData
                last: index === audio.inputs.length - 1
                onSelect: audio.setDefaultInput(modelData.name)
                onVolume: function (v) { audio.setInputVolume(modelData.name, v) }
                onMute: function (m) { audio.setInputMute(modelData.name, m) }
            }
        }
    }

    BlockHeading { Layout.fillWidth: true; title: qsTr("App audio"); visible: audio.streams.length > 0 }
    AppPanel {
        Layout.fillWidth: true
        visible: audio.streams.length > 0
        Repeater {
            model: audio.streams
            delegate: Item {
                width: parent ? parent.width : 400
                height: 48
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.app }
                        AppText { Layout.fillWidth: true; text: modelData.title; secondary: true; scale_: 0.88 }
                    }
                    AppCombo {
                        label: qsTr("Output for %1").arg(modelData.app)
                        textRole: "description"
                        valueRole: "name"
                        model: audio.outputs
                        implicitWidth: 260
                        currentIndex: {
                            for (var i = 0; i < audio.outputs.length; ++i)
                                if (audio.outputs[i].objectId === String(modelData.sink))
                                    return i
                            return -1
                        }
                        onActivated: function (i) { audio.moveStream(modelData.index, audio.outputs[i].name) }
                    }
                }
            }
        }
    }
}
