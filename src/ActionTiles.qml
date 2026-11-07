import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Actions that each do one thing at once, as tiles: a line icon over a short name. Each
// runs exactly as its row would, through the same confirmation and launch, and one that
// cannot run here says why when hovered or read aloud.
Item {
    id: root
    property var entries: []
    property string focusId: ""
    // Four to a line where there is room, as the design has it, and two otherwise.
    readonly property int perLine: width > theme.fontSize * 30 ? 4 : 2
    readonly property var lines: {
        var out = []
        for (var i = 0; i < entries.length; i += perLine)
            out.push(entries.slice(i, i + perLine))
        return out
    }
    implicitHeight: column.implicitHeight

    function tileFor(id) {
        for (var l = 0; l < lineRepeater.count; ++l) {
            var line = lineRepeater.itemAt(l)
            for (var i = 0; line && i < line.tiles.count; ++i)
                if (line.tiles.itemAt(i) && line.tiles.itemAt(i).entryId === id)
                    return line.tiles.itemAt(i)
        }
        return null
    }
    function takeFocus() {
        if (lineRepeater.count && lineRepeater.itemAt(0).tiles.count)
            lineRepeater.itemAt(0).tiles.itemAt(0).takeFocus()
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 10
        Repeater {
            id: lineRepeater
            model: root.lines
            delegate: RowLayout {
                readonly property alias tiles: tileRepeater
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    id: tileRepeater
                    model: modelData
                    delegate: AbstractButton {
                        id: tile
                        objectName: "actionTile"
                        readonly property var entry: modelData
                        readonly property string entryId: entry.id
                        readonly property var st: backend.states[backend.stateKey(entryId)] || ({})
                        readonly property bool unavailable: st.status === "unavailable"
                        readonly property bool danger: entry.tone === "danger"
                        readonly property bool highlighted: root.focusId === entryId
                        readonly property string reason: st.reason || ""
                        function takeFocus() { tile.forceActiveFocus(Qt.TabFocusReason) }
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        implicitHeight: Math.max(Math.round(theme.fontSize * 5.2), label.implicitHeight + Math.round(theme.fontSize * 3.4))
                        enabled: !unavailable
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        text: qsTranslate("catalog", entry.label || "")
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.description: unavailable ? reason : qsTranslate("catalog", entry.description || "")
                        ToolTip.visible: hovered && (unavailable ? reason.length > 0 : !!entry.description)
                        ToolTip.delay: 500
                        ToolTip.text: Accessible.description
                        onClicked: win.activate(entry)
                        background: Rectangle {
                            radius: theme.controlRadius + 2
                            color: tile.highlighted ? Qt.alpha(theme.accent, 0.14)
                                 : tile.down ? Qt.alpha(theme.foreground, 0.1)
                                 : tile.hovered && tile.enabled ? Qt.alpha(theme.foreground, 0.06)
                                 : Qt.alpha(theme.foreground, 0.02)
                            border.width: tile.visualFocus ? 2 : 1
                            border.color: tile.visualFocus ? theme.accent : tile.danger && tile.enabled ? Qt.alpha(theme.dangerText, 0.45) : theme.divider
                            opacity: tile.enabled ? 1 : 0.5
                            Behavior on color { enabled: theme.animate; ColorAnimation { duration: 100 } }
                        }
                        contentItem: ColumnLayout {
                            spacing: 6
                            opacity: tile.enabled ? 1 : 0.5
                            Item { Layout.fillHeight: true }
                            AppIcon {
                                Layout.alignment: Qt.AlignHCenter
                                name: tile.entry.icon || "power"
                                size: Math.round(theme.fontSize * 1.6)
                                tint: tile.danger ? theme.dangerText : theme.foreground
                            }
                            AppText {
                                id: label
                                Layout.fillWidth: true
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                text: tile.text
                                color: tile.danger ? theme.dangerText : theme.foreground
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
        }
    }
}
