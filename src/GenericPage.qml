import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every page but Overview: heading, its specialized blocks, then catalog sections. A page
// the catalog composes into panels shows those instead, under a heading that stays put
// with a jump link to each section.
ScrollView {
    id: root
    property string pageId: ""
    property var page: ({})
    property string focusId: ""
    contentWidth: availableWidth
    // Said outright: the pinned heading is a second child, and ScrollView only sizes its
    // content by itself when there is one.
    contentHeight: column.implicitHeight
    clip: true
    topPadding: composed ? stickyHeader.height : 0
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: AppScrollBar {
        parent: root
        x: root.mirrored ? 0 : root.width - width
        y: root.topPadding
        height: root.availableHeight
    }

    readonly property var pageSections: backend.sections(pageId)
    readonly property var panelRows: backend.panels(pageId)
    readonly property bool composed: panelRows.length > 0
    // Side by side while every panel keeps room for a label and its control.
    readonly property bool wide: column.width > theme.fontSize * 60
    // These fold away into one panel; the rest are shown as they are. Each is still a block
    // of its own, so search can land on it by name.
    readonly property var foldedNames: ["plugins", "shortcuts", "rules"]
    readonly property var allBlocks: backend.blocks(pageId)
    readonly property var pageBlocks: allBlocks.filter(function (name) { return foldedNames.indexOf(name) < 0 })
    readonly property var foldedBlocks: allBlocks.filter(function (name) { return foldedNames.indexOf(name) >= 0 })
    // Blocks a panel holds are drawn there, not in the list above the panels.
    readonly property var panelBlocks: {
        var found = []
        for (var r = 0; r < panelRows.length; ++r)
            for (var c = 0; c < panelRows[r].columns.length; ++c)
                for (var p = 0; p < panelRows[r].columns[c].length; ++p)
                    if (panelRows[r].columns[c][p].block)
                        found.push(panelRows[r].columns[c][p].block)
        return found
    }
    // Where the jump links lead, in the page's own order: its list of links if it has
    // one, or else its sections.
    readonly property var jumpSections: {
        var found = []
        for (var r = 0; r < panelRows.length; ++r)
            for (var c = 0; c < panelRows[r].columns.length; ++c)
                for (var p = 0; p < panelRows[r].columns[c].length; ++p) {
                    var spec = panelRows[r].columns[c][p]
                    var label = spec.jump === false ? "" : typeof spec.jump === "string" ? spec.jump : spec.section
                    // Only sections on screen: a panel can leave, as Battery does without a battery.
                    if (label && found.indexOf(label) < 0 && specShown(spec))
                        found.push(label)
                }
        var order = (page.jumps && page.jumps.length) ? page.jumps : (page.sections || [])
        return found.filter(function (label) { return order.indexOf(label) >= 0 })
                    .sort(function (a, b) { return order.indexOf(a) - order.indexOf(b) })
                    .concat(found.filter(function (label) { return order.indexOf(label) < 0 }))
    }

    // Blocks that have left the page, as reported by their panels. Plain data, so what
    // depends on it never reads back the visibility it decides.
    property var hiddenBlocks: ({})
    function setBlockHidden(name, hidden) {
        if (!name || !!hiddenBlocks[name] === hidden)
            return
        var next = Object.assign({}, hiddenBlocks)
        if (hidden)
            next[name] = true
        else
            delete next[name]
        hiddenBlocks = next
    }
    function specShown(spec) {
        return !spec.block || !hiddenBlocks[spec.block]
    }
    // Panels in reading order, top to bottom, then along the line.
    function panelItems() {
        var found = []
        var walk = function (item) {
            if (item.objectName === "sectionPanel" && item.visible)
                found.push(item)
            else
                for (var i = 0; i < item.children.length; ++i)
                    walk(item.children[i])
        }
        walk(column)
        return found.sort(function (a, b) {
            var pa = a.mapToItem(column, 0, 0), pb = b.mapToItem(column, 0, 0)
            return Math.abs(pa.y - pb.y) > 2 ? pa.y - pb.y : (root.mirrored ? pb.x - pa.x : pa.x - pb.x)
        })
    }
    function panelFor(section) {
        var panels = panelItems()
        for (var i = 0; i < panels.length; ++i)
            if (panels[i].jumpLabel === section)
                return panels[i]
        return null
    }

    // Which section the reader is in: the last one whose top has reached the upper quarter
    // of the view.
    property string spyTab: ""
    // A jump link stays lit after it is used, even where the page cannot scroll that far,
    // until the reader scrolls on.
    property string pinnedTab: ""
    property real pinnedY: -1
    property bool jumping: false
    readonly property string activeTab: pinnedTab || spyTab || (jumpSections.length ? jumpSections[0] : "")
    function updateSpy() {
        if (!composed || jumping)
            return
        var y = contentItem.contentY
        if (pinnedTab.length && Math.abs(y - pinnedY) > 4)
            pinnedTab = ""
        var panels = panelItems()
        var current = ""
        for (var i = 0; i < panels.length; ++i)
            if (panels[i].jumpTarget && panels[i].mapToItem(column, 0, 0).y <= y + Math.max(24, root.availableHeight * 0.25))
                current = panels[i].jumpLabel
        spyTab = current
    }
    Connections {
        target: root.contentItem
        function onContentYChanged() { root.updateSpy() }
    }
    function jumpTo(section) {
        var panel = panelFor(section)
        if (!panel)
            return
        var limit = Math.max(0, column.implicitHeight - root.availableHeight)
        var target = Math.max(0, Math.min(panel.mapToItem(column, 0, 0).y - 12, limit))
        pinnedTab = section
        pinnedY = target
        contentItem.cancelFlick()
        if (theme.animate) {
            jumping = true
            jumpAnimation.target = contentItem
            jumpAnimation.to = target
            jumpAnimation.landing = panel
            jumpAnimation.restart()
        } else {
            jumping = true
            contentItem.contentY = target
            land(panel)
        }
    }
    // Focus may nudge the view to show the row it lands on; that is still the jump.
    function land(panel) {
        if (panel)
            panel.takeFocus()
        pinnedY = contentItem.contentY
        jumping = false
    }
    NumberAnimation {
        id: jumpAnimation
        property var landing: null
        property: "contentY"
        duration: 260
        easing.type: Easing.OutCubic
        onStopped: root.land(landing)
    }

    function scrollToFocus() {
        if (!focusId.length)
            return
        var target = null
        if (focusId.indexOf("block.") === 0) {
            var name = focusId.substring(6)
            for (var i = 0; i < blockRepeater.count; ++i) {
                var loader = blockRepeater.itemAt(i)
                if (loader && loader.blockName === name)
                    target = loader
            }
            if (!target && composed) {
                var holders = panelItems()
                for (var h = 0; h < holders.length && !target; ++h)
                    if (holders[h].block === name)
                        target = holders[h]
            }
            if (!target)
                target = folded.reveal(name)
        } else if (composed) {
            var panels = panelItems()
            for (var p = 0; p < panels.length && !target; ++p)
                target = panels[p].rowItem(focusId)
        } else {
            for (var j = 0; j < sectionRepeater.count; ++j) {
                var row = sectionRepeater.itemAt(j).rowItem(focusId)
                if (row)
                    target = row
            }
        }
        if (!target)
            return
        var y = target.mapToItem(column, 0, 0).y
        contentItem.contentY = Math.max(0, Math.min(y - 24, column.implicitHeight - availableHeight + 40))
        if (target.takeFocus)
            target.takeFocus()
    }
    onFocusIdChanged: settleTimer.restart()
    Timer {
        id: settleTimer
        interval: 60
        onTriggered: root.scrollToFocus()
    }
    Timer {
        // The highlight marks where search landed, then gets out of the way.
        running: root.focusId.length > 0
        interval: 2600
        // Clear the source: assigning focusId here would cut its binding, and later
        // search results on this page would no longer land.
        onTriggered: win.focusRequest = ""
    }

    // The heading and the jump links, outside the scrolling content so they stay put.
    Rectangle {
        id: stickyHeader
        objectName: "pageHeader"
        parent: root
        visible: root.composed
        z: 2
        width: root.width
        height: visible ? headerColumn.implicitHeight + 6 : 0
        color: theme.canvas
        ColumnLayout {
            id: headerColumn
            x: column.x
            width: column.width
            spacing: 10
            Item { Layout.preferredHeight: 18 }
            PageHeading {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTranslate("catalog", root.page.label || "")
                subtitle: qsTranslate("catalog", root.page.description || "")
            }
            Item {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                implicitHeight: links.implicitHeight
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.divider
                }
                Flow {
                    id: links
                    objectName: "sectionLinks"
                    width: parent.width
                    spacing: 6
                    Accessible.role: Accessible.PageTabList
                    Accessible.name: qsTr("Sections")
                    Repeater {
                        model: root.jumpSections
                        delegate: AbstractButton {
                            id: link
                            objectName: "sectionLink_" + modelData
                            readonly property bool current: root.activeTab === modelData
                            implicitHeight: Math.round(theme.fontSize * 2.7)
                            implicitWidth: linkLabel.implicitWidth + 24
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true
                            text: qsTranslate("catalog", modelData)
                            Accessible.role: Accessible.PageTab
                            Accessible.name: text
                            Accessible.selected: current
                            onClicked: root.jumpTo(modelData)
                            background: Item {
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.bottomMargin: 4
                                    radius: theme.controlRadius
                                    color: link.hovered && !link.current ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                                    border.width: link.visualFocus ? 2 : 0
                                    border.color: theme.accent
                                }
                                // The section in view, marked by its line as well as its color.
                                Rectangle {
                                    visible: link.current
                                    anchors.bottom: parent.bottom
                                    x: 6
                                    width: parent.width - 12
                                    height: 3
                                    radius: 1.5
                                    color: theme.accent
                                }
                            }
                            contentItem: AppText {
                                id: linkLabel
                                text: link.text
                                horizontalAlignment: Text.AlignHCenter
                                scale_: 1.08
                                color: link.current ? theme.accentText : theme.foreground
                                bottomPadding: 4
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: column
        // A line stays readable from label to control, however wide the window gets.
        width: Math.min(root.availableWidth - 8, theme.fontSize * 92)
        spacing: root.composed ? 18 : 28
        x: root.mirrored ? root.availableWidth - width : 0

        Item { Layout.preferredHeight: 2 }

        PageHeading {
            visible: !root.composed
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            title: qsTranslate("catalog", root.page.label || "")
            subtitle: qsTranslate("catalog", root.page.description || "")
        }
        Repeater {
            id: blockRepeater
            model: root.pageBlocks.filter(function (name) { return root.panelBlocks.indexOf(name) < 0 })
            delegate: Loader {
                readonly property string blockName: modelData
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                source: "qrc:/Block" + modelData.charAt(0).toUpperCase() + modelData.slice(1) + ".qml"
            }
        }
        DisclosurePanel {
            id: folded
            visible: blocks.length > 0
            blocks: root.foldedBlocks
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
        }
        // The composed page: rows of panels, side by side where there is room.
        ColumnLayout {
            id: panelGrid
            visible: root.composed
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 20
            Repeater {
                model: root.panelRows
                delegate: GridLayout {
                    id: panelRow
                    readonly property int count: modelData.columns.length
                    // Columns share the grid's width by weight, equally unless the page
                    // says otherwise. From the grid around it: the row's own width
                    // follows its columns.
                    readonly property var weights: modelData.weights || []
                    // Whether the last panel of each column reaches its neighbor's line.
                    readonly property bool stretch: modelData.stretch !== false
                    readonly property real weightSum: {
                        var sum = 0
                        for (var i = 0; i < count; ++i)
                            sum += weights[i] || 1
                        return sum
                    }
                    readonly property real free: Math.max(0, panelGrid.width - columnSpacing * (count - 1))
                    Layout.fillWidth: true
                    // A row whose panels have all left takes no room, spacing included.
                    visible: modelData.columns.some(function (column) { return column.some(root.specShown) })
                    columns: root.wide ? count : 1
                    columnSpacing: 20
                    rowSpacing: 20
                    Repeater {
                        model: modelData.columns
                        delegate: ColumnLayout {
                            id: panelColumn
                            readonly property var panels: modelData
                            Layout.alignment: Qt.AlignTop
                            Layout.fillWidth: true
                            Layout.fillHeight: root.wide && panelRow.stretch
                            // Equal shares. No maximum: a layout's own maximum comes from its
                            // children's, and one tied to this width would pin the grid shut.
                            Layout.preferredWidth: root.wide ? panelRow.free * (panelRow.weights[index] || 1) / panelRow.weightSum : panelGrid.width
                            spacing: 20
                            Repeater {
                                model: panelColumn.panels
                                delegate: SectionPanel {
                                    Layout.fillWidth: true
                                    // Side by side, the last panel of each column reaches
                                    // the same line as its neighbor's.
                                    Layout.fillHeight: root.wide && panelRow.stretch && index === panelColumn.panels.length - 1
                                    spec: modelData
                                    focusId: root.focusId
                                    Component.onCompleted: root.setBlockHidden(modelData.block, !wanted)
                                    onWantedChanged: root.setBlockHidden(modelData.block, !wanted)
                                }
                            }
                        }
                    }
                }
            }
        }
        Repeater {
            id: sectionRepeater
            model: root.composed ? [] : root.pageSections
            delegate: SectionBlock {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: modelData.title
                rows: modelData.rows
                focusId: root.focusId
                // With several displays, say which one the Screen rows act on.
                aside: root.pageId === "displays" && modelData.title === "Screen" && backend.displays.monitors.length > 1 && backend.displays.selected.length
                       ? qsTr("Selected display · %1").arg(backend.displays.selected) : ""
            }
        }
        Item { Layout.preferredHeight: 20 }
    }
}
