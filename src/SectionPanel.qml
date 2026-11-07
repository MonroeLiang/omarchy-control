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
    readonly property var settingRows: rows.filter(function (row) { return row.type !== "action" })
    readonly property var actionRows: rows.filter(function (row) { return row.type === "action" })
    readonly property bool inset: !!spec.inset
    readonly property bool outside: !!spec.outside
    readonly property bool headed: spec.heading !== false && !root.header
    // The jump link that leads here, if any: its own label, or the section's name.
    readonly property string jumpLabel: spec.jump === false ? "" : typeof spec.jump === "string" ? spec.jump : section
    readonly property bool jumpTarget: jumpLabel.length > 0

    // From the parts' natural sizes, never from the card's stretched one.
    implicitHeight: block.length ? blockLoader.implicitHeight
                  : (outsideHeading.visible ? outsideHeading.implicitHeight + layout.spacing : 0) + card.implicitHeight

    function rowItem(id) {
        if (headerRow.item && headerRow.item.entryId === id)
            return headerRow.item
        var lists = [settings, actions]
        for (var l = 0; l < lists.length; ++l)
            for (var i = 0; i < lists[l].count; ++i)
                if (lists[l].itemAt(i) && lists[l].itemAt(i).entryId === id)
                    return lists[l].itemAt(i)
        return null
    }
    // A jump link lands on the first thing in the panel that can take the keyboard.
    function takeFocus() {
        if (blockLoader.item) {
            if (blockLoader.item.takeFocus)
                blockLoader.item.takeFocus()
            return
        }
        var first = headerRow.item || (settings.count ? settings.itemAt(0) : actions.count ? actions.itemAt(0) : null)
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
                            id: settings
                            model: root.settingRows
                            delegate: SettingRow {
                                width: body.width
                                entry: modelData
                                rich: true
                                last: index === root.settingRows.length - 1 && root.actionRows.length === 0
                                highlighted: root.focusId === modelData.id
                            }
                        }
                        Item {
                            visible: root.settingRows.length > 0 && root.actionRows.length > 0
                            width: parent.width
                            height: visible ? 10 : 0
                        }
                        Repeater {
                            id: actions
                            model: root.actionRows
                            delegate: SettingRow {
                                width: body.width
                                entry: modelData
                                rich: true
                                navigation: !!root.spec.navigation
                                last: index === root.actionRows.length - 1
                                highlighted: root.focusId === modelData.id
                            }
                        }
                    }
                }
            }
        }
    }
}
