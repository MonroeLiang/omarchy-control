import QtQuick
import QtQuick.Layouts

// One section of a composed page: a heading with a line icon, inside its card or above
// it, or a setting standing in as the heading, then the section's rows. Settings come
// first and actions after a small gap, so what runs something sits apart from what
// changes something. A panel can instead hold one of the page's own blocks, which then
// draws its heading and card itself.
Item {
    id: root
    objectName: "sectionPanel"
    property var spec: ({})
    property string focusId: ""
    readonly property string section: spec.section || ""
    readonly property string title: qsTranslate("catalog", spec.title || section)
    readonly property string block: spec.block || ""
    readonly property var header: spec.header || null
    readonly property var rows: spec.rows || []
    // Rows in runs: settings first, then actions, each split by the group it belongs
    // to, in the order the groups first appear, with a gap between runs.
    readonly property var runs: {
        var settingKeys = [], actionKeys = [], byKey = {}
        for (var i = 0; i < rows.length; ++i) {
            var action = rows[i].type === "action"
            var key = (action ? "a:" : "s:") + (rows[i].group || "")
            if (!byKey[key]) {
                byKey[key] = []
                ;(action ? actionKeys : settingKeys).push(key)
            }
            byKey[key].push(rows[i])
        }
        return settingKeys.concat(actionKeys).map(function (key) { return byKey[key] })
    }
    readonly property bool inset: !!spec.inset
    readonly property bool outside: !!spec.outside
    readonly property bool headed: spec.heading !== false && !root.header
    // The jump link that leads here, if any: its own label, or the section's name.
    readonly property string jumpLabel: spec.jump === false ? "" : typeof spec.jump === "string" ? spec.jump : section
    readonly property bool jumpTarget: jumpLabel.length > 0

    // From the parts' natural sizes, never from the card's stretched one.
    // A block can leave the page entirely, as Battery does on a computer without one.
    // Its own wish, apart from whether its row is on screen.
    readonly property bool wanted: !blockLoader.item || blockLoader.item.shown === undefined || blockLoader.item.shown
    visible: wanted
    implicitHeight: block.length ? blockLoader.implicitHeight
                  : (outsideHeading.visible ? outsideHeading.implicitHeight + layout.spacing : 0) + card.implicitHeight

    function rowItem(id) {
        if (blockLoader.item)
            return blockLoader.item.rowItem ? blockLoader.item.rowItem(id) : null
        if (headerRow.item && headerRow.item.entryId === id)
            return headerRow.item
        for (var r = 0; r < runRepeater.count; ++r) {
            var run = runRepeater.itemAt(r)
            if (run && run.tiled) {
                var tile = run.tiles.tileFor(id)
                if (tile)
                    return tile
            }
            for (var i = 0; run && i < run.rows.count; ++i)
                if (run.rows.itemAt(i) && run.rows.itemAt(i).entryId === id)
                    return run.rows.itemAt(i)
        }
        return null
    }
    // A jump link lands on the first thing in the panel that can take the keyboard.
    function takeFocus() {
        if (blockLoader.item) {
            if (blockLoader.item.takeFocus)
                blockLoader.item.takeFocus()
            return
        }
        var firstRun = runRepeater.count ? runRepeater.itemAt(0) : null
        var first = headerRow.item || (firstRun && firstRun.rows.count ? firstRun.rows.itemAt(0) : firstRun && firstRun.tiled ? firstRun.tiles : null)
        if (first)
            first.takeFocus()
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: title

    Column {
        id: layout
        width: parent.width
        spacing: 12

        Loader {
            id: blockLoader
            width: parent.width
            active: root.block.length > 0
            visible: active
            source: active ? "qrc:/Block" + root.block.charAt(0).toUpperCase() + root.block.slice(1) + ".qml" : ""
            // A block can take the catalog rows its panel claims and draw them in its own card.
            onLoaded: {
                if (item.panelRows !== undefined)
                    item.panelRows = Qt.binding(function () { return root.rows })
                if (item.focusId !== undefined)
                    item.focusId = Qt.binding(function () { return root.focusId })
            }
        }

        PanelHeading {
            id: outsideHeading
            visible: !root.block.length && root.outside && root.headed
            width: parent.width
            icon: root.spec.icon || ""
            title: root.title
        }

        AppPanel {
            id: card
            visible: !root.block.length
            width: parent.width
            // Side by side, the last panel of a column stretches to its neighbor's line.
            height: Math.max(implicitHeight, root.height - y)

            // A setting as the heading, as a device's on/off switch is.
            Loader {
                id: headerRow
                width: parent.width
                active: !!root.header
                visible: active
                sourceComponent: SettingRow {
                    width: card.width
                    entry: root.header
                    rich: true
                    last: true
                    headingTitle: root.title
                    headingIcon: root.spec.icon || ""
                    highlighted: root.focusId === root.header.id
                }
            }
            Item {
                visible: root.headed && !root.outside
                width: parent.width
                implicitHeight: heading.implicitHeight + 30
                height: visible ? implicitHeight : 0
                RowLayout {
                    id: heading
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    AppIcon {
                        visible: name.length > 0
                        name: root.spec.icon || ""
                        size: Math.round(theme.fontSize * 1.75)
                        tint: theme.foreground
                    }
                    AppText {
                        Layout.fillWidth: true
                        text: root.title
                        font.weight: Font.DemiBold
                        scale_: 1.45
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                }
            }
            Rectangle {
                visible: (root.headed && !root.outside) || !!root.header
                x: 16
                width: parent.width - 32
                height: 1
                color: theme.hairline
            }

            // An engine's own set of rows reads as one card within the section.
            Item {
                width: parent.width
                implicitHeight: body.implicitHeight + (root.inset ? 26 : root.headed && !root.outside ? 4 : 0)
                height: implicitHeight
                Rectangle {
                    id: inner
                    x: root.inset ? 14 : 0
                    y: root.inset ? 14 : 0
                    width: parent.width - (root.inset ? 28 : 0)
                    implicitHeight: body.implicitHeight
                    height: implicitHeight
                    radius: theme.panelRadius
                    color: root.inset ? Qt.alpha(theme.foreground, 0.025) : "transparent"
                    border.width: root.inset ? 1 : 0
                    border.color: theme.hairline
                    Column {
                        id: body
                        width: parent.width
                        Item {
                            visible: root.inset
                            width: parent.width
                            height: visible ? insetTitle.implicitHeight + 22 : 0
                            AppText {
                                id: insetTitle
                                x: 16
                                width: parent.width - 32
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.spec.inset || ""
                                font.weight: Font.DemiBold
                                scale_: 1.2
                                Accessible.role: Accessible.Heading
                                Accessible.name: text
                            }
                        }
                        Rectangle {
                            visible: root.inset
                            x: 14
                            width: parent.width - 28
                            height: 1
                            color: theme.hairline
                        }
                        Repeater {
                            id: runRepeater
                            model: root.runs
                            delegate: Column {
                                readonly property alias rows: runRows
                                readonly property alias tiles: runTiles
                                readonly property int run: index
                                // The panel's tile group is drawn as tiles, not rows.
                                readonly property bool tiled: !!root.spec.tiles && modelData.length > 0 && modelData[0].group === root.spec.tiles
                                width: body.width
                                Item {
                                    visible: parent.run > 0
                                    width: parent.width
                                    height: visible ? (parent.tiled ? 16 : 10) : 0
                                }
                                ActionTiles {
                                    id: runTiles
                                    visible: parent.tiled
                                    x: 16
                                    width: parent.width - 32
                                    height: visible ? implicitHeight + 16 : 0
                                    entries: parent.tiled ? modelData : []
                                    focusId: root.focusId
                                }
                                Repeater {
                                    id: runRows
                                    model: parent.tiled ? [] : modelData
                                    delegate: SettingRow {
                                        width: body.width
                                        entry: modelData
                                        rich: true
                                        navigation: !!root.spec.navigation && modelData.type === "action"
                                        last: parent.run === root.runs.length - 1 && index === runRows.count - 1
                                        highlighted: root.focusId === modelData.id
                                    }
                                }
                            }
                        }
                        // A state that changes what the rows above mean, said where they are.
                        Item {
                            readonly property var notice: root.spec.notice || null
                            readonly property var st: notice ? (backend.states[backend.stateKey(notice.setting)] || ({})) : ({})
                            visible: !!notice && st.value === notice.value
                            width: parent.width
                            height: visible ? noticeBox.height + 22 : 0
                            Rectangle {
                                id: noticeBox
                                objectName: "panelNotice"
                                x: 14
                                y: 8
                                width: parent.width - 28
                                height: noticeRow.implicitHeight + 18
                                radius: theme.controlRadius
                                color: Qt.alpha(theme.foreground, 0.03)
                                border.width: 1
                                border.color: theme.divider
                                RowLayout {
                                    id: noticeRow
                                    x: 14
                                    width: parent.width - 28
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 10
                                    AppIcon { name: root.spec.notice ? root.spec.notice.icon || "info" : "info"; size: Math.round(theme.fontSize * 1.2); tint: theme.muted }
                                    AppText {
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                        scale_: 0.93
                                        text: root.spec.notice ? qsTranslate("catalog", root.spec.notice.text || "") : ""
                                    }
                                }
                            }
                        }
                        // What the actions above do beyond their names.
                        Item {
                            visible: !!root.spec.footnote
                            width: parent.width
                            height: visible ? footRow.implicitHeight + 30 : 0
                            Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }
                            RowLayout {
                                id: footRow
                                x: 16
                                y: 15
                                width: parent.width - 32
                                spacing: 8
                                AppIcon { name: "info"; size: 14; tint: theme.muted; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                                AppText {
                                    objectName: "panelFootnote"
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    secondary: true
                                    scale_: 0.9
                                    text: qsTranslate("catalog", root.spec.footnote || "")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
