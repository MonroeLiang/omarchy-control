import QtQuick
import QtQuick.Layouts

// A picture of the bar, drawn from the same layout the editor below it changes, with each
// placement region marked underneath. It is a schematic, not the shell's own rendering,
// so widgets are shown as their icon and, where there is room, their name.
Item {
    id: root
    // The bar has a physical left and right whatever language this window is in.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    property var layout: ({})
    // { id, label } for each region, left to right.
    property var regions: []
    property var nameOf: function (id) { return id }
    property var iconOf: function (id) { return "plugin" }

    readonly property int side: Math.round(theme.fontSize * 1.3)
    readonly property int gap: Math.round(theme.fontSize * 0.95)
    readonly property int pad: Math.round(theme.fontSize * 0.8)
    readonly property int dotSize: Math.round(theme.fontSize * 0.62)

    FontMetrics { id: labelMetrics; font.family: theme.uiFont; font.pixelSize: Math.round(theme.fontSize * 0.92) }
    FontMetrics { id: captionMetrics; font.family: theme.uiFont; font.pixelSize: Math.round(theme.fontSize * 0.88) }

    function kindOf(id) {
        return /(^|\.)workspaces$/.test(id) ? "dots" : "icon"
    }
    function itemWidth(item) {
        if (item.kind === "dots")
            return dotSize * 4 + Math.round(dotSize * 1.1) * 3
        if (item.kind === "more")
            return labelMetrics.advanceWidth(item.name) + 2
        return side + (item.labelled ? 6 + labelMetrics.advanceWidth(item.name) : 0)
    }
    function regionWidth(region) {
        var total = pad * 2
        for (var i = 0; i < region.items.length; ++i)
            total += itemWidth(region.items[i]) + (i > 0 ? gap : 0)
        return Math.max(total, captionMetrics.advanceWidth(region.label) + 10)
    }
    function build(labelled) {
        return regions.map(function (spec) {
            var named = labelled && (spec.id === "center" || spec.id === "centerEnd")
            return { id: spec.id, label: spec.label, widgets: root.layout[spec.id] || [],
                     items: (root.layout[spec.id] || []).map(function (id) {
                         return { id: id, name: root.nameOf(id), kind: root.kindOf(id), labelled: named }
                     }) }
        })
    }
    function total(list) {
        var sum = 0
        for (var i = 0; i < list.length; ++i)
            sum += regionWidth(list[i])
        return sum
    }
    // Names give way first, then the fullest region keeps what fits and counts the rest.
    readonly property var placed: {
        var available = width
        if (available <= 0 || !regions.length)
            return []
        var list = build(true)
        if (total(list) > available)
            list = build(false)
        var guard = 200
        while (total(list) > available && --guard > 0) {
            var widest = -1
            for (var i = 0; i < list.length; ++i)
                if (list[i].items.length > 1 && (widest < 0 || regionWidth(list[i]) > regionWidth(list[widest])))
                    widest = i
            if (widest < 0)
                break
            var region = list[widest]
            var kept = region.items.filter(function (item) { return item.kind !== "more" })
            kept.pop()
            region.items = kept.concat([{ id: "", name: "+" + (region.widgets.length - kept.length), kind: "more", labelled: false }])
        }
        var spare = Math.max(0, available - total(list)) / list.length
        return list.map(function (entry) {
            entry.width = regionWidth(entry) + spare
            return entry
        })
    }

    implicitHeight: bar.height + brackets.implicitHeight + 6
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Preview of the bar layout")

    Rectangle {
        id: bar
        objectName: "barPreview"
        width: parent.width
        height: Math.round(theme.fontSize * 2.5)
        radius: theme.controlRadius
        color: theme.deep
        border.width: 1
        border.color: theme.divider
        Row {
            anchors.fill: parent
            Repeater {
                model: root.placed
                delegate: Item {
                    id: region
                    objectName: "barPreviewRegion"
                    readonly property string regionId: modelData.id
                    readonly property var regionItems: modelData.items
                    width: modelData.width
                    height: bar.height
                    Rectangle {
                        // Where this region meets the next, except the two that read as one run.
                        visible: index > 0 && modelData.id !== "centerEnd"
                        width: 1
                        height: parent.height - 14
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.divider
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: root.gap
                        x: modelData.id === "left" ? root.pad
                         : modelData.id === "right" ? parent.width - width - root.pad
                         : (parent.width - width) / 2
                        Repeater {
                            model: modelData.items
                            delegate: Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                readonly property var item: modelData
                                // The workspace row, drawn as the dots it is on the bar.
                                Row {
                                    visible: parent.item.kind === "dots"
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Math.round(root.dotSize * 1.1)
                                    Repeater {
                                        model: 4
                                        delegate: Rectangle {
                                            width: root.dotSize
                                            height: root.dotSize
                                            radius: theme.rounded ? width / 2 : 1
                                            color: index === 0 ? theme.accent : Qt.alpha(theme.foreground, 0.34)
                                        }
                                    }
                                }
                                AppIcon {
                                    visible: parent.item.kind === "icon"
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: parent.item.kind === "icon" ? root.iconOf(parent.item.id) : ""
                                    size: root.side
                                    tint: /(^|\.)menu$/.test(parent.item.id) ? theme.accent : theme.foreground
                                }
                                AppText {
                                    visible: parent.item.labelled || parent.item.kind === "more"
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: parent.item.name
                                    scale_: 0.92
                                    secondary: parent.item.kind === "more"
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Underneath: a bracket for each region's span, and what the region is called.
    Row {
        id: brackets
        y: bar.height + 6
        width: parent.width
        Repeater {
            model: root.placed
            delegate: Item {
                width: modelData.width
                height: Math.round(theme.fontSize * 2.25)
                implicitHeight: height
                Rectangle { x: 4; y: 0; width: 1; height: 6; color: theme.controlBorder }
                Rectangle { x: parent.width - 5; y: 0; width: 1; height: 6; color: theme.controlBorder }
                Rectangle { x: 4; y: 6; width: parent.width - 8; height: 1; color: theme.controlBorder }
                AppText {
                    y: 11
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData.label
                    secondary: true
                    scale_: 0.88
                }
            }
        }
    }
}
