import QtQuick
import QtQuick.Layouts

// One card of Overview's quick settings: header, controls, optional footer links.
Rectangle {
    id: root
    property string title: ""
    property string icon: ""
    default property alias content: controls.data
    property alias footer: footerRow.data

    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    Layout.preferredHeight: implicitHeight
    implicitHeight: column.implicitHeight + 1
    radius: theme.radius
    color: Qt.alpha(theme.raised, 0.25)
    border.width: 1
    border.color: theme.divider

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        Rectangle {
            id: header
            width: parent.width
            height: Math.round(theme.fontSize * 2.9)
            color: Qt.alpha(theme.raised, 0.7)
            radius: theme.radius
            // Only the outer corners are rounded; the edge above the controls is square.
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.min(parent.radius, parent.height)
                color: parent.color
                visible: parent.radius > 0
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                AppIcon { name: root.icon; size: 20 }
                AppText { text: root.title; font.bold: true; scale_: 1.08; Layout.fillWidth: true }
            }
        }
        Column {
            id: controls
            width: parent.width
        }
        Item {
            visible: footerRow.children.length > 0
            width: parent.width
            height: visible ? footerRow.implicitHeight + 14 : 0
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 14; height: 1; color: theme.divider; opacity: 0.6 }
            Row {
                id: footerRow
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 20
            }
        }
    }
}
