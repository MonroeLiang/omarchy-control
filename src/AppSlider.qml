import QtQuick
import QtQuick.Controls

Slider {
    id: root
    property string label: ""
    implicitWidth: 200
    implicitHeight: 24
    focusPolicy: Qt.StrongFocus
    Accessible.name: label

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 4
        radius: 2
        color: Qt.alpha(theme.foreground, 0.15)
        Rectangle {
            // visualPosition already flips when mirrored; the fill's length must not.
            width: root.position * parent.width
            height: parent.height
            radius: 2
            color: theme.accent
            x: root.mirrored ? parent.width - width : 0
        }
    }
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: theme.foreground
        border.width: root.visualFocus ? 2 : 0
        border.color: theme.accent
    }
}
