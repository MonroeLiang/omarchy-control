import QtQuick
import QtQuick.Controls

// Thin, in the theme's own colors, and only there while there is something to scroll.
ScrollBar {
    id: root
    padding: 2
    minimumSize: 0.06
    contentItem: Rectangle {
        implicitWidth: root.hovered || root.pressed ? 8 : 5
        implicitHeight: 100
        radius: theme.rounded ? width / 2 : 0
        color: Qt.alpha(theme.foreground, root.pressed ? 0.45 : root.hovered ? 0.32 : 0.2)
        opacity: root.size < 1 ? 1 : 0
        Behavior on implicitWidth { enabled: theme.animate; NumberAnimation { duration: 100 } }
    }
    background: Item {}
}
