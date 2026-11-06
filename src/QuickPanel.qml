import QtQuick
import QtQuick.Layouts

// One card of Overview's quick settings: a title, controls, and footer links.
AppPanel {
    id: root
    property string title: ""
    property string icon: ""
    // The last card in a column takes up the slack, so both columns end level.
    property bool stretch: false
    default property alias body: controls.data
    property alias footer: footerRow.data

    Layout.fillWidth: true
    Layout.fillHeight: stretch
    implicitHeight: header.height + controls.implicitHeight + footerArea.height + 1

    Item {
        id: header
        parent: root
        width: root.width
        height: Math.round(theme.fontSize * 3.2)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12
            AppIcon { name: root.icon; size: 19 }
            AppText { text: root.title; font.weight: Font.DemiBold; scale_: 1.15; Layout.fillWidth: true }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.hairline }
    }
    Column {
        id: controls
        parent: root
        y: header.height
        width: root.width
    }
    Item {
        id: footerArea
        parent: root
        visible: footerRow.children.length > 0
        y: root.height - height
        width: root.width
        height: visible ? footerRow.implicitHeight + 16 : 0
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 14; height: 1; color: theme.hairline }
        Row {
            id: footerRow
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 20
        }
    }
}
