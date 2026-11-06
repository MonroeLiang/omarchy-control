import QtQuick

// The hairline between rows of a panel, gone after the last one.
Rectangle {
    anchors.bottom: parent.bottom
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.leftMargin: 14
    anchors.rightMargin: 14
    height: 1
    color: theme.hairline
    visible: !!parent.parent && parent.y + parent.height < parent.parent.height - 1
}
