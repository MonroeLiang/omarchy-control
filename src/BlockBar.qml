import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Widget placement and dock pins, through Omarchy's commands so per-widget options
// and a custom bar's identity survive a move.
ColumnLayout {
    id: root
    spacing: 8
    readonly property var plugins: backend.plugins
    readonly property var sections: [
        { id: "left", label: qsTr("Left") }, { id: "centerBegin", label: qsTr("Before center") },
        { id: "center", label: qsTr("Center") }, { id: "centerEnd", label: qsTr("After center") },
        { id: "right", label: qsTr("Right") }, { id: "tray", label: qsTr("Tray drawer") }
    ]
    readonly property var barRegions: sections.filter(function (s) { return s.id !== "tray" })
    // Omarchy's shell uses its built-in bar when the config names none.
    readonly property string activeBar: plugins.activeBar.length > 0 ? plugins.activeBar : "omarchy.bar"
    readonly property bool customBar: activeBar !== "omarchy.bar"

    function widgetName(id) {
        var list = plugins.plugins
        for (var i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i].name
        return friendly(id)
    }
    // "omarchy.menu" and "org.gnome.Nautilus" are identifiers, not names.
    function friendly(id) {
        var name = id.split(".").pop().replace(/[-_]/g, " ")
        return name.charAt(0).toUpperCase() + name.slice(1)
    }
    // A plugin's manifest has no icon of its own, so this goes by what the widget is. It only
    // picks a glyph, and anything unrecognized gets the same plain one.
    function widgetIcon(id) {
        const known = {
            "menu": "logo", "workspaces": "layers", "indicators": "layers", "clock": "clock", "elsewhen": "calendar",
            "weather": "weather", "system-update": "refresh", "update": "refresh", "tray": "system", "dropbox": "dropbox",
            "keyboard-layout": "input", "tailscale": "nodes", "agents": "accounts", "bluetooth": "bluetooth",
            "network": "wifi", "wifi": "wifi", "audio": "sound", "sound": "sound", "monitor": "displays", "display": "displays",
            "power": "power", "battery": "battery", "microphone": "mic", "media": "music", "active-window": "windows",
            "notifications": "bell", "nightlight": "moon", "spacer": "minus", "superwhisper": "wave", "blip": "bolt",
            "facet": "cube", "reminders": "clock"
        }
        // "omarchy.clock" is named last, and "superwhisper.panel" first.
        const parts = id.split(".").reverse()
        for (var i = 0; i < parts.length; ++i)
            if (known[parts[i]])
                return known[parts[i]]
        return "plugin"
    }
    function iconTint(id) {
        return /(^|\.)menu$/.test(id) ? theme.accentText : theme.foreground
    }

    // Moving: Omarchy takes the widget out and then puts it in at the index, so a place
    // further along its own region is one less than it looks.
    property string focusAfterMove: ""
    function layoutOf(section) {
        return plugins.barLayout[section] || []
    }
    function move(id, fromSection, fromIndex, toSection, toIndex, keepFocus) {
        if (plugins.changing || (toSection === fromSection && toIndex === fromIndex))
            return
        focusAfterMove = keepFocus ? id : ""
        plugins.moveWidget(id, toSection, toIndex, fromSection, fromIndex)
    }
    // A move that failed rebuilds nothing, and must not take the keyboard later on.
    readonly property bool changing: plugins.changing
    onChangingChanged: if (!changing) Qt.callLater(function () {
        // Unless the next one has already started, and these are its own.
        if (!root.changing) {
            root.focusAfterMove = ""
            dockPanel.focusAfterRemove = -1
        }
    })
    function sectionIndex(id) {
        for (var i = 0; i < sections.length; ++i)
            if (sections[i].id === id)
                return i
        return -1
    }
    function keyMove(key, id, section, index) {
        const count = layoutOf(section).length
        // What the arrow means follows what is on screen, which mirrors in right-to-left.
        const forward = Qt.application.layoutDirection === Qt.RightToLeft ? Qt.Key_Left : Qt.Key_Right
        const backward = Qt.application.layoutDirection === Qt.RightToLeft ? Qt.Key_Right : Qt.Key_Left
        if (key === backward && index > 0) {
            move(id, section, index, section, index - 1, true)
        } else if (key === forward && index < count - 1) {
            move(id, section, index, section, index + 1, true)
        } else if (key === Qt.Key_Up || key === Qt.Key_Down) {
            const next = sectionIndex(section) + (key === Qt.Key_Up ? -1 : 1)
            if (next >= 0 && next < sections.length)
                move(id, section, index, sections[next].id, layoutOf(sections[next].id).length, true)
        }
    }

    // Dragging: which region and place the pointer is over, worked out from where the chips
    // are, so wrapped rows and right-to-left both come out right.
    property var dragInfo: null
    property string dropSection: ""
    property int dropIndex: -1
    function dropTarget(scene) {
        for (var i = 0; i < sectionRepeater.count; ++i) {
            const row = sectionRepeater.itemAt(i)
            if (!row)
                continue
            const inRow = row.mapFromItem(null, scene.x, scene.y)
            if (inRow.y < 0 || inRow.y >= row.height)
                continue
            const count = row.chips.count
            for (var j = 0; j < count; ++j) {
                const chip = row.chips.itemAt(j)
                const inChip = chip.mapFromItem(null, scene.x, scene.y)
                if (inChip.y < 0)
                    return { section: row.sectionId, index: j }
                if (inChip.y < chip.height) {
                    const mirrored = Qt.application.layoutDirection === Qt.RightToLeft
                    if (mirrored ? inChip.x > chip.width / 2 : inChip.x < chip.width / 2)
                        return { section: row.sectionId, index: j }
                }
            }
            return { section: row.sectionId, index: count }
        }
        return null
    }
    function dragTo(scene) {
        ghost.x = scene.x - ghost.width / 2
        ghost.y = scene.y - ghost.height / 2
        const target = dropTarget(scene)
        dropSection = target ? target.section : ""
        dropIndex = target ? target.index : -1
    }
    function endDrag(commit) {
        const info = dragInfo
        const section = dropSection
        var index = dropIndex
        dragInfo = null
        dropSection = ""
        dropIndex = -1
        if (!commit || !info || !section.length)
            return
        if (section === info.section && index > info.index)
            --index
        move(info.id, info.section, info.index, section, index, true)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            BlockHeading { Layout.fillWidth: true; title: qsTr("Bar layout") }
            AppText {
                visible: root.customBar
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                secondary: true
                text: qsTr("Custom bars may display this layout differently.")
            }
        }
        Rectangle {
            objectName: "activeBar"
            Layout.alignment: Qt.AlignBottom
            implicitWidth: activeBarRow.implicitWidth + 22
            implicitHeight: Math.round(theme.fontSize * 2.2)
            radius: theme.controlRadius
            color: "transparent"
            border.width: 1
            border.color: theme.divider
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Active bar: %1").arg(root.activeBar)
            Row {
                id: activeBarRow
                anchors.centerIn: parent
                spacing: 6
                AppText { text: qsTr("Active bar:"); secondary: true; anchors.verticalCenter: parent.verticalCenter }
                AppText { text: root.activeBar; mono: true; color: theme.accentText; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }

    AppPanel {
        id: editor
        Layout.fillWidth: true
        Item {
            width: parent ? parent.width : 400
            height: preview.implicitHeight + 16
            BarPreview {
                id: preview
                x: 14
                y: 14
                width: parent.width - 28
                layout: root.plugins.barLayout
                regions: root.barRegions
                nameOf: root.widgetName
                iconOf: root.widgetIcon
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.hairline }
        }
        Repeater {
            id: sectionRepeater
            model: root.sections
            delegate: Item {
                id: sectionRow
                objectName: "barSection_" + modelData.id
                readonly property string sectionId: modelData.id
                readonly property alias chips: chipRepeater
                // A refresh hands over a new list every time. Rebuilding the chips for one that
                // says the same thing would close an open menu and lose the keyboard's place.
                readonly property var incoming: root.layoutOf(modelData.id)
                property var widgets: []
                property string signature: ""
                onIncomingChanged: {
                    const next = JSON.stringify(incoming)
                    if (next !== signature) {
                        signature = next
                        widgets = incoming
                    }
                }
                Component.onCompleted: incomingChanged()

                // Wide enough to read when they fit on a line, and whole columns when they do not.
                readonly property int chipGap: 6
                readonly property real naturalWidth: Math.round(theme.fontSize * 15.8)
                readonly property real smallestWidth: Math.round(theme.fontSize * 12.6)
                readonly property real chipWidth: {
                    const room = flow.width
                    const count = widgets.length
                    if (count * naturalWidth + (count - 1) * chipGap <= room)
                        return naturalWidth
                    const columns = Math.floor((room + chipGap) / (smallestWidth + chipGap))
                    if (columns <= 1)
                        return Math.min(naturalWidth, room)
                    return Math.min(naturalWidth * 1.5, (room - chipGap * (columns - 1)) / columns)
                }

                width: parent ? parent.width : 400
                height: Math.max(flow.implicitHeight, Math.round(theme.fontSize * 2.2) + 2) + 13
                Rectangle {
                    anchors.fill: parent
                    visible: root.dragInfo !== null && root.dropSection === sectionRow.sectionId
                    color: Qt.alpha(theme.accent, 0.06)
                }
                RowDivider {}
                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 6
                    anchors.bottomMargin: 7
                    anchors.leftMargin: 20
                    anchors.rightMargin: 14
                    spacing: 12
                    AppText {
                        text: modelData.label
                        secondary: true
                        Layout.preferredWidth: Math.round(theme.fontSize * 9)
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Math.round(theme.fontSize * 2.2) + 2
                        verticalAlignment: Text.AlignVCenter
                    }
                    Flow {
                        id: flow
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: sectionRow.chipGap
                        AppText {
                            visible: sectionRow.widgets.length === 0
                            text: qsTr("Empty")
                            font.italic: true
                            secondary: true
                            height: Math.round(theme.fontSize * 2.2) + 2
                            verticalAlignment: Text.AlignVCenter
                        }
                        Repeater {
                            id: chipRepeater
                            model: sectionRow.widgets
                            delegate: BarWidgetChip {
                                id: chip
                                readonly property int widgetIndex: index
                                width: sectionRow.chipWidth
                                widgetId: modelData
                                widgetName: root.widgetName(modelData)
                                widgetIcon: root.widgetIcon(modelData)
                                iconTint: root.iconTint(modelData)
                                dragging: root.dragInfo !== null && root.dragInfo.id === modelData && root.dragInfo.section === sectionRow.sectionId
                                actions: {
                                    const self = chip
                                    const section = sectionRow.sectionId
                                    var list = []
                                    if (index > 0)
                                        list.push({ label: qsTr("Move earlier"), run: function () { root.move(self.widgetId, section, self.widgetIndex, section, self.widgetIndex - 1, false) } })
                                    if (index < sectionRow.widgets.length - 1)
                                        list.push({ label: qsTr("Move later"), run: function () { root.move(self.widgetId, section, self.widgetIndex, section, self.widgetIndex + 1, false) } })
                                    root.sections.forEach(function (s) {
                                        list.push({ label: qsTr("Move to %1").arg(s.label), run: function () {
                                            if (s.id !== section)
                                                root.move(self.widgetId, section, self.widgetIndex, s.id, root.layoutOf(s.id).length, false)
                                        } })
                                    })
                                    return list
                                }
                                onKeyMove: function (key) { root.keyMove(key, widgetId, sectionRow.sectionId, widgetIndex) }
                                onDragStarted: function (point) {
                                    root.dragInfo = { id: widgetId, name: widgetName, icon: widgetIcon, section: sectionRow.sectionId, index: widgetIndex }
                                    root.dragTo(point)
                                }
                                onDragMoved: function (point) { root.dragTo(point) }
                                onDragEnded: function (commit) { root.endDrag(commit) }
                                // The move rebuilt this row, so the keyboard goes back to the widget it moved.
                                Component.onCompleted: {
                                    if (root.focusAfterMove === widgetId) {
                                        root.focusAfterMove = ""
                                        focusHandle()
                                    }
                                }
                            }
                        }
                    }
                }
                // Where it would land.
                Rectangle {
                    readonly property bool active: root.dragInfo !== null && root.dropSection === sectionRow.sectionId && chipRepeater.count > 0
                    readonly property var neighbour: active ? chipRepeater.itemAt(Math.min(root.dropIndex, chipRepeater.count - 1)) : null
                    readonly property bool after: root.dropIndex >= chipRepeater.count
                    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
                    visible: active && neighbour !== null
                    width: 2
                    height: neighbour ? neighbour.height : 0
                    color: theme.accent
                    x: neighbour ? neighbour.mapToItem(sectionRow, (after !== rtl) ? neighbour.width + 2 : -4, 0).x : 0
                    y: neighbour ? neighbour.mapToItem(sectionRow, 0, 0).y : 0
                }
            }
        }
    }

    BlockHeading { Layout.fillWidth: true; Layout.topMargin: 18; title: qsTr("Pinned dock apps") }
    AppPanel {
        id: dockPanel
        objectName: "dockPanel"
        Layout.fillWidth: true
        readonly property int smallest: Math.round(theme.fontSize * 7.4)
        readonly property real room: width - 16
        readonly property int columns: Math.max(1, Math.floor(room / smallest))
        readonly property real cellWidth: {
            const count = root.plugins.dockApps.length
            return count >= columns ? room / columns : Math.min(room / Math.max(1, count), smallest * 1.1)
        }
        // After removing one, the keyboard moves to whichever took its place.
        property int focusAfterRemove: -1
        property string signature: ""
        property var apps: []
        readonly property var incoming: root.plugins.dockApps
        onIncomingChanged: {
            const next = JSON.stringify(incoming)
            if (next !== signature) {
                signature = next
                apps = incoming
            }
        }
        Component.onCompleted: incomingChanged()
        AppText {
            visible: dockPanel.apps.length === 0
            x: 16
            height: theme.rowHeight
            secondary: true
            text: qsTr("Nothing is pinned.")
        }
        Item {
            visible: dockPanel.apps.length > 0
            width: parent ? parent.width : 400
            height: dockFlow.implicitHeight + 20
            Flow {
                id: dockFlow
                x: 8
                y: 10
                width: parent.width - 16
                Repeater {
                    model: dockPanel.apps
                    delegate: Item {
                        id: app
                        objectName: "dockApp"
                        readonly property string appName: modelData.name || root.friendly(modelData.id)
                        width: dockPanel.cellWidth
                        height: icon.height + nameText.implicitHeight + 8
                        Item {
                            id: icon
                            readonly property int side: Math.round(theme.fontSize * 3.15)
                            width: side
                            height: side
                            anchors.horizontalCenter: parent.horizontalCenter
                            Image {
                                id: picture
                                objectName: "dockAppIcon"
                                anchors.fill: parent
                                source: modelData.icon
                                visible: status === Image.Ready
                                asynchronous: true
                                cache: true
                                smooth: true
                                mipmap: true
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: icon.side * 2 * Screen.devicePixelRatio
                                sourceSize.height: icon.side * 2 * Screen.devicePixelRatio
                            }
                            // No entry, no icon, or one that will not draw: all look the same.
                            Rectangle {
                                objectName: "dockAppFallback"
                                anchors.fill: parent
                                visible: picture.status !== Image.Ready
                                radius: theme.panelRadius
                                color: theme.raised
                                border.width: 1
                                border.color: theme.divider
                                AppIcon {
                                    anchors.centerIn: parent
                                    name: "apps"
                                    size: Math.round(icon.side * 0.5)
                                    tint: theme.muted
                                }
                            }
                            AbstractButton {
                                id: removeButton
                                objectName: "dockRemove"
                                readonly property int side: Math.round(theme.fontSize * 1.3)
                                width: side
                                height: side
                                x: parent.width - width / 2 - 2
                                y: -height / 2 + 2
                                focusPolicy: Qt.StrongFocus
                                hoverEnabled: true
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Unpin %1").arg(app.appName)
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.text: Accessible.name
                                onClicked: {
                                    if (root.plugins.changing)
                                        return
                                    dockPanel.focusAfterRemove = activeFocus ? index : -1
                                    root.plugins.unpin(modelData.id)
                                }
                                background: Rectangle {
                                    radius: theme.rounded ? width / 2 : 2
                                    color: removeButton.hovered ? theme.raised : theme.surfaceBottom
                                    border.width: removeButton.visualFocus ? 2 : 1
                                    border.color: removeButton.visualFocus ? theme.accent : theme.controlBorder
                                }
                                contentItem: Item {
                                    AppIcon {
                                        anchors.centerIn: parent
                                        name: "close"
                                        size: Math.round(removeButton.side * 0.62)
                                        tint: theme.foreground
                                    }
                                }
                                Component.onCompleted: {
                                    if (dockPanel.focusAfterRemove >= 0 && index === Math.min(dockPanel.focusAfterRemove, dockPanel.apps.length - 1)) {
                                        dockPanel.focusAfterRemove = -1
                                        forceActiveFocus(Qt.TabFocusReason)
                                    }
                                }
                            }
                        }
                        AppText {
                            id: nameText
                            y: icon.height + 6
                            width: parent.width - 6
                            x: 3
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: app.appName
                            scale_: 0.92
                        }
                        HoverHandler { id: appHover }
                        ToolTip.visible: appHover.hovered && nameText.truncated && !removeButton.hovered
                        ToolTip.delay: 500
                        ToolTip.text: app.appName
                    }
                }
            }
        }
    }

    // What is being dragged, following the pointer over everything else.
    Rectangle {
        id: ghost
        objectName: "barDragGhost"
        // Above the whole window, and out of this column's layout.
        parent: Overlay.overlay
        visible: root.dragInfo !== null
        z: 100
        width: ghostRow.implicitWidth + 24
        height: Math.round(theme.fontSize * 2.2) + 2
        radius: theme.controlRadius
        color: theme.raised
        border.width: 1
        border.color: theme.accent
        Row {
            id: ghostRow
            anchors.centerIn: parent
            spacing: 8
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.dragInfo ? root.dragInfo.icon : ""
                size: Math.round(theme.fontSize * 1.15)
            }
            AppText {
                anchors.verticalCenter: parent.verticalCenter
                text: root.dragInfo ? root.dragInfo.name : ""
            }
        }
    }
}
