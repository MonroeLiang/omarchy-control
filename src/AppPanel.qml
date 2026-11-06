import QtQuick

// A grouped surface: a thin border, no shadow, and square unless the desktop rounds.
Rectangle {
    default property alias content: inner.data
    property int padding: 0
    implicitHeight: inner.implicitHeight + padding * 2
    radius: theme.radius
    color: Qt.alpha(theme.raised, 0.35)
    border.width: 1
    border.color: theme.divider
    Column {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: parent.padding
        spacing: 0
    }
}
