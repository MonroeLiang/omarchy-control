import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ComboBox {
    id: root
    property string label: ""
    implicitHeight: Math.round(theme.fontSize * 2.2) + 2
    implicitWidth: 220
    font.family: "monospace"
    font.pixelSize: theme.fontSize
    Accessible.name: label
    Material.roundedScale: theme.radius
    Material.foreground: theme.foreground
    Material.background: theme.raised
    Material.accent: theme.accent
    leftPadding: 0
    rightPadding: 0
    opacity: enabled ? 1 : 0.55

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: root.down ? Qt.alpha(theme.foreground, 0.1) : root.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? theme.accent : theme.divider
        Behavior on color { enabled: theme.animate; ColorAnimation { duration: 100 } }
    }
    contentItem: AppText {
        leftPadding: root.mirrored ? 30 : 12
        rightPadding: root.mirrored ? 12 : 30
        verticalAlignment: Text.AlignVCenter
        // An empty list says so instead of drawing a blank box.
        text: root.displayText.length ? root.displayText : qsTr("Not set")
        secondary: root.displayText.length === 0
    }
    indicator: AppIcon {
        name: "chevron"
        size: 14
        x: root.mirrored ? 10 : root.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        tint: theme.muted
        rotation: root.popup.visible ? 180 : 0
        Behavior on rotation { enabled: theme.animate; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: ListView.view ? ListView.view.width : root.width
        height: Math.round(theme.fontSize * 2.5)
        padding: 0
        highlighted: root.highlightedIndex === index
        hoverEnabled: true
        Accessible.name: root.textAt(index)
        background: Rectangle {
            color: option.highlighted ? Qt.alpha(theme.accent, 0.18) : option.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"
        }
        contentItem: Item {
            AppText {
                anchors.left: parent.left
                anchors.right: mark.left
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: root.textAt(option.index)
            }
            AppIcon {
                id: mark
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                size: 14
                name: "check"
                tint: theme.accentText
                visible: root.currentIndex === option.index
            }
        }
    }
    popup: Popup {
        y: root.height + 2
        width: Math.max(root.width, 200)
        implicitHeight: Math.min(contentItem.implicitHeight + 2, Math.round(theme.fontSize * 2.5) * 10 + 2)
        padding: 1
        transformOrigin: Item.Top
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
        }
        background: Rectangle {
            radius: Math.min(4, theme.radius)
            color: theme.window
            border.width: 1
            border.color: theme.divider
        }
    }
}
