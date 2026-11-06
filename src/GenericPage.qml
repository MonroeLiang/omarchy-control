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

    readonly property var pageSections: backend.sections(pageId)
    readonly property var pageBlocks: backend.blocks(pageId)

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
        width: root.availableWidth - 8
        spacing: 22
        x: 0

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
