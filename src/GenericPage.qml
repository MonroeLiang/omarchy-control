import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every page but Overview: heading, its specialized blocks, then catalog sections.
ScrollView {
    id: root
    property string pageId: ""
    property var page: ({})
    property string focusId: ""
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: AppScrollBar {
        parent: root
        x: root.mirrored ? 0 : root.width - width
        height: root.availableHeight
    }

    readonly property var pageSections: backend.sections(pageId)
    // These fold away into one panel; the rest are shown as they are. Each is still a block
    // of its own, so search can land on it by name.
    readonly property var foldedNames: ["plugins", "shortcuts", "rules"]
    readonly property var allBlocks: backend.blocks(pageId)
    readonly property var pageBlocks: allBlocks.filter(function (name) { return foldedNames.indexOf(name) < 0 })
    readonly property var foldedBlocks: allBlocks.filter(function (name) { return foldedNames.indexOf(name) >= 0 })

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
            if (!target)
                target = folded.reveal(name)
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

    ColumnLayout {
        id: column
        // A line stays readable from label to control, however wide the window gets.
        width: Math.min(root.availableWidth - 8, theme.fontSize * 92)
        spacing: 28
        x: root.mirrored ? root.availableWidth - width : 0

        Item { Layout.preferredHeight: 2 }

        PageHeading {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            title: qsTranslate("catalog", root.page.label || "")
            subtitle: qsTranslate("catalog", root.page.description || "")
        }
        Repeater {
            id: blockRepeater
            model: root.pageBlocks
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
        Repeater {
            id: sectionRepeater
            model: root.pageSections
            delegate: SectionBlock {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: modelData.title
                rows: modelData.rows
                focusId: root.focusId
            }
        }
        Item { Layout.preferredHeight: 20 }
    }
}
