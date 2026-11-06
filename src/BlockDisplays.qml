import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The arrangement: outputs drawn to scale, with staged edits below. Nothing reaches
// Hyprland until Preview, and a preview undoes itself unless it is kept.
ColumnLayout {
    id: root
    spacing: 8
    readonly property var displays: backend.displays
    property var stage: []
    property bool dirty: false

    function reload() {
        var list = []
        var monitors = displays.monitors
        for (var i = 0; i < monitors.length; ++i) {
            var m = monitors[i]
            list.push({ output: m.output, description: m.description, internal: m.internal, modes: m.modes, mode: m.mode, x: m.x, y: m.y, scale: m.scale, transform: m.transform, enabled: m.enabled, width: m.width, height: m.height })
        }
        stage = list
        dirty = false
    }
    // Fresh state only replaces the form when there are no unsaved edits in it.
    Connections {
        target: root.displays
        function onChanged() { if (!root.dirty) root.reload() }
    }
    Component.onCompleted: reload()

    function edit(index, field, value) {
        var list = stage.slice()
        var row = Object.assign({}, list[index])
        row[field] = value
        if (field === "mode") {
            var size = value.split("@")[0].split("x")
            row.width = parseInt(size[0])
            row.height = parseInt(size[1])
        }
        list[index] = row
        stage = list
        dirty = true
    }
    readonly property var bounds: {
        var minX = 0, minY = 0, maxX = 1, maxY = 1, first = true
        for (var i = 0; i < stage.length; ++i) {
            var s = stage[i]
            if (!s.enabled)
                continue
            var w = logicalWidth(s), h = logicalHeight(s)
            if (first) { minX = s.x; minY = s.y; maxX = s.x + w; maxY = s.y + h; first = false; continue }
            minX = Math.min(minX, s.x); minY = Math.min(minY, s.y)
            maxX = Math.max(maxX, s.x + w); maxY = Math.max(maxY, s.y + h)
        }
        return { minX: minX, minY: minY, width: maxX - minX, height: maxY - minY }
    }
    function logicalWidth(s) {
        var rotated = s.transform % 2 === 1
        return Math.round((rotated ? s.height : s.width) / Math.max(0.25, s.scale))
    }
    function logicalHeight(s) {
        var rotated = s.transform % 2 === 1
        return Math.round((rotated ? s.width : s.height) / Math.max(0.25, s.scale))
    }

    // "2880x1800@120.00" as a person would say it.
    function modeLabel(mode) {
        var parts = /^(\d+)x(\d+)@([\d.]+)/.exec(mode)
        return parts ? parts[1] + " × " + parts[2] + " · " + Math.round(parseFloat(parts[3]) * 100) / 100 + " Hz" : mode
    }

    BlockHeading { Layout.fillWidth: true; title: qsTr("Arrangement") }
    AppText { visible: displays.status === "unavailable"; text: qsTr("Hyprland is not reachable, so displays cannot be shown."); color: theme.dangerText; Layout.fillWidth: true; wrapMode: Text.Wrap }

    // Lock notice
    AppPanel {
        Layout.fillWidth: true
        visible: displays.status === "ok" && !displays.editingUnlocked
        padding: 14
        RowLayout {
            width: parent.width
            spacing: 12
            AppIcon { name: "shield" }
            AppText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Arranging displays here is locked until the safety timer has been proven on this computer. The check moves one display a few pixels for three seconds and confirms it comes back. Windows may shift very slightly.")
            }
            AppButton {
                text: displays.checkState === "running" ? qsTr("Checking…") : qsTr("Run check")
                enabled: displays.checkState !== "running" && !displays.previewing
                onClicked: displays.runRecoveryCheck()
            }
        }
    }

    Rectangle {
        id: canvas
        Layout.fillWidth: true
        Layout.preferredHeight: 240
        color: theme.deep
        radius: theme.panelRadius
        border.width: 1
        border.color: theme.hairline
        readonly property real pad: 24
        // The map sits in the middle of its well, whatever shape the layout makes.
        readonly property real originX: (width - root.bounds.width * unit) / 2
        readonly property real originY: (height - root.bounds.height * unit) / 2
        readonly property real unit: root.bounds.width > 0 ? Math.min((width - pad * 2) / root.bounds.width, (height - pad * 2) / root.bounds.height) : 0.1
        Repeater {
            model: root.stage
            delegate: Rectangle {
                id: tile
                visible: modelData.enabled
                readonly property real scaleUnit: canvas.unit
                width: root.logicalWidth(modelData) * scaleUnit
                height: root.logicalHeight(modelData) * scaleUnit
                x: canvas.originX + (modelData.x - root.bounds.minX) * scaleUnit
                y: canvas.originY + (modelData.y - root.bounds.minY) * scaleUnit
                // Only looks movable once it is.
                readonly property bool movable: displays.editingUnlocked && !displays.previewing
                color: movable ? Qt.alpha(theme.accent, drag.active ? 0.3 : 0.16) : Qt.alpha(theme.foreground, 0.07)
                border.width: activeFocus ? 3 : movable ? 2 : 1
                border.color: movable ? theme.accent : theme.divider
                radius: theme.controlRadius
                activeFocusOnTab: movable
                Accessible.role: movable ? Accessible.Button : Accessible.Graphic
                // The same move without a mouse: arrows nudge, Shift nudges further.
                Keys.onPressed: function (event) {
                    if (!movable)
                        return
                    var step = event.modifiers & Qt.ShiftModifier ? 100 : 10
                    var dx = event.key === Qt.Key_Left ? -step : event.key === Qt.Key_Right ? step : 0
                    var dy = event.key === Qt.Key_Up ? -step : event.key === Qt.Key_Down ? step : 0
                    if (!dx && !dy)
                        return
                    if (dx)
                        root.edit(index, "x", modelData.x + dx)
                    if (dy)
                        root.edit(index, "y", modelData.y + dy)
                    event.accepted = true
                }
                Accessible.name: qsTr("%1 at %2, %3").arg(modelData.output).arg(modelData.x).arg(modelData.y)
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    AppText { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.output; font.weight: Font.DemiBold }
                    AppText { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: root.logicalWidth(modelData) + "×" + root.logicalHeight(modelData); secondary: true; scale_: 0.88 }
                }
                DragHandler {
                    id: drag
                    enabled: displays.editingUnlocked && !displays.previewing
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: true
                    property point origin
                    onActiveChanged: if (active) origin = Qt.point(modelData.x, modelData.y)
                    onTranslationChanged: {
                        if (!active || tile.scaleUnit <= 0)
                            return
                        root.edit(index, "x", Math.round(origin.x + translation.x / tile.scaleUnit))
                        root.edit(index, "y", Math.round(origin.y + translation.y / tile.scaleUnit))
                    }
                }
            }
        }
    }

    AppPanel {
        Layout.fillWidth: true
        visible: root.stage.length > 0
        Repeater {
            model: root.stage
            delegate: Item {
                width: parent ? parent.width : 400
                height: outputColumn.implicitHeight + 18
                RowDivider {}
                ColumnLayout {
                    id: outputColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        AppText { Layout.fillWidth: true; text: modelData.output + (modelData.description ? " — " + modelData.description : "") }
                        AppSwitch {
                            label: qsTr("Use %1").arg(modelData.output)
                            on: modelData.enabled
                            enabled: displays.editingUnlocked && !displays.previewing
                            onClicked: root.edit(index, "enabled", !on)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: modelData.enabled
                        AppCombo {
                            label: qsTr("Resolution of %1").arg(modelData.output)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 240
                            Layout.maximumWidth: 240
                            enabled: displays.editingUnlocked && !displays.previewing
                            model: modelData.modes.map(root.modeLabel)
                            currentIndex: modelData.modes.indexOf(modelData.mode)
                            onActivated: function (i) { root.edit(index, "mode", modelData.modes[i]) }
                        }
                        AppCombo {
                            label: qsTr("Scale of %1").arg(modelData.output)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 120
                            Layout.maximumWidth: 120
                            enabled: displays.editingUnlocked && !displays.previewing
                            readonly property var scales: [1, 1.25, 1.5, 1.6, 1.75, 2, 2.5, 3, 4]
                            model: scales.map(function (s) { return Math.round(s * 100) + "%" })
                            currentIndex: {
                                var nearest = 0
                                for (var i = 0; i < scales.length; ++i)
                                    if (Math.abs(scales[i] - modelData.scale) < Math.abs(scales[nearest] - modelData.scale))
                                        nearest = i
                                return nearest
                            }
                            onActivated: function (i) { root.edit(index, "scale", scales[i]) }
                        }
                        AppCombo {
                            label: qsTr("Rotation of %1").arg(modelData.output)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 150
                            Layout.maximumWidth: 150
                            enabled: displays.editingUnlocked && !displays.previewing
                            model: [qsTr("Normal"), qsTr("90°"), qsTr("180°"), qsTr("270°")]
                            currentIndex: Math.min(3, modelData.transform)
                            onActivated: function (i) { root.edit(index, "transform", i) }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        visible: displays.previewing && displays.checkState !== "running"
        AppText {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.weight: Font.DemiBold
            text: qsTr("Keep this layout? Restoring the previous one in %n second(s).", "", displays.previewSeconds)
        }
        AppButton { text: qsTr("Restore now"); onClicked: displays.revert() }
        AppButton { text: qsTr("Keep"); style: "primary"; onClicked: displays.keep() }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        visible: !displays.previewing && displays.editingUnlocked
        AppText { Layout.fillWidth: true; secondary: true; wrapMode: Text.Wrap; text: root.dirty ? qsTr("Changes are not applied yet.") : qsTr("Drag a display to move it.") }
        AppButton { visible: root.dirty; text: qsTr("Discard"); onClicked: root.reload() }
        AppButton {
            visible: root.dirty
            text: qsTr("Preview")
            style: "primary"
            onClicked: {
                var problem = displays.validateLayout(root.stage)
                if (problem.length) {
                    toast.show(problem, "error")
                    return
                }
                win.withOwnership(function () {
                    displays.preview(root.stage)
                    root.dirty = false
                })
            }
        }
    }
}
