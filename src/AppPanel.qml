import QtQuick

// A grouped surface: a faint vertical light and a hairline edge, no shadow. Its corners
// are the desktop's.
Rectangle {
    default property alias content: inner.data
    property int padding: 0
    implicitHeight: inner.implicitHeight + padding * 2
    radius: theme.panelRadius
    border.width: 1
    border.color: theme.hairline
    gradient: Gradient {
        GradientStop { position: 0; color: theme.surfaceTop }
        GradientStop { position: 1; color: theme.surfaceBottom }
    }
    Column {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: parent.padding
        spacing: 0
    }
}
