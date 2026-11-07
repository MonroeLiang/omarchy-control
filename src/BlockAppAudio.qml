import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Each app stream on its own row, even two from the same app, with the output it plays
// through. A stream whose output cannot be told says Not set rather than guess.
ColumnLayout {
    id: root
    readonly property var audio: backend.audio
    readonly property var streams: audio.streams
    // The outputs as the dropdown lists them: the short name, and the line under it
    // where two short names alone would read the same.
    readonly property var outputNames: {
        var titles = audio.outputs.map(function (o) { return o.title || o.description })
        return audio.outputs.map(function (o, i) {
            var repeated = titles.indexOf(titles[i]) !== titles.lastIndexOf(titles[i])
            return repeated && o.subtitle ? titles[i] + " · " + o.subtitle : titles[i]
        })
    }
    spacing: 12

    function takeFocus() {
        if (rows.count)
            rows.itemAt(0).takeFocus()
    }

    PanelHeading {
        Layout.fillWidth: true
        icon: "layers"
        title: qsTr("App audio")
    }
    AppPanel {
        Layout.fillWidth: true
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
                text: root.audio.status === "unavailable" ? qsTr("PipeWire is not responding, so app audio cannot be shown.")
                    : root.streams.length ? "" : qsTr("No app is playing sound right now.")
            }
        }
        Repeater {
            id: rows
            model: root.streams
            delegate: Item {
                id: row
                objectName: "streamRow"
                readonly property var stream: modelData
                function takeFocus() { route.forceActiveFocus(Qt.TabFocusReason) }
                width: parent ? parent.width : 400
                implicitHeight: Math.max(theme.rowHeight, names.implicitHeight + 20)
                height: implicitHeight
                Rectangle {
                    visible: index < root.streams.length - 1
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
                    anchors.rightMargin: 14
                    spacing: 14
                    // The app's own icon from the icon theme, or one plain sign for all
                    // that have none.
                    Item {
                        implicitWidth: Math.round(theme.fontSize * 2)
                        implicitHeight: implicitWidth
                        Image {
                            id: appIcon
                            anchors.fill: parent
                            visible: status === Image.Ready
                            source: row.stream.icon || ""
                            sourceSize.width: width * 2
                            sourceSize.height: height * 2
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        AppIcon {
                            anchors.centerIn: parent
                            visible: !appIcon.visible
                            name: "apps"
                            size: Math.round(theme.fontSize * 1.4)
                            tint: theme.muted
                        }
                    }
                    ColumnLayout {
                        id: names
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: row.stream.app }
                        AppText {
                            visible: text.length > 0
                            Layout.fillWidth: true
                            text: row.stream.title
                            secondary: true
                            scale_: 0.88
                        }
                    }
                    AppCombo {
                        id: route
                        objectName: "streamOutput"
                        proportional: true
                        label: row.stream.title ? qsTr("Output for %1, %2").arg(row.stream.app).arg(row.stream.title) : qsTr("Output for %1").arg(row.stream.app)
                        implicitWidth: Math.max(150, Math.min(240, root.width * 0.34))
                        model: root.outputNames
                        // The output the stream was last confirmed on, or none. A ComboBox
                        // jumps to its first entry when its list fills, which would claim
                        // an output nobody read, so the selection is put back each time.
                        readonly property int verified: {
                            for (var i = 0; i < root.audio.outputs.length; ++i)
                                if (root.audio.outputs[i].name === row.stream.output)
                                    return i
                            return -1
                        }
                        function sync() {
                            if (currentIndex !== verified)
                                currentIndex = verified
                        }
                        onVerifiedChanged: sync()
                        onCountChanged: Qt.callLater(sync)
                        onCurrentIndexChanged: if (currentIndex !== verified) Qt.callLater(sync)
                        Component.onCompleted: sync()
                        onActivated: function (i) {
                            if (root.audio.outputs[i].name !== row.stream.output)
                                root.audio.moveStream(row.stream.index, root.audio.outputs[i].name)
                        }
                    }
                }
            }
        }
    }
}
