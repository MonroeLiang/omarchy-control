import QtQuick
import QtQuick.Controls

Slider {
    id: root
    property string label: ""
    implicitWidth: 200
    implicitHeight: 24
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: label
    opacity: enabled ? 1 : 0.5

    // A range either side of zero fills from zero, so its middle reads as neutral.
    readonly property real origin: from < 0 && to > 0 ? -from / (to - from) : 0

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 4
        radius: 2
        color: Qt.alpha(theme.foreground, 0.15)
        Rectangle {
            readonly property real low: Math.min(root.origin, root.position)
            readonly property real high: Math.max(root.origin, root.position)
            width: (high - low) * parent.width
            height: parent.height
            radius: 2
            color: theme.accent
            x: root.mirrored ? parent.width * (1 - high) : parent.width * low
        }
    }
    handle: Rectangle {
        property int diameter: root.pressed ? 18 : root.hovered || root.visualFocus ? 17 : 16
        x: root.leftPadding + root.visualPosition * (root.availableWidth - 16) - (diameter - 16) / 2
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: diameter
        height: diameter
        radius: diameter / 2
        color: theme.foreground
        border.width: root.visualFocus ? 3 : 0
        border.color: theme.accent
        Behavior on diameter { enabled: theme.animate; NumberAnimation { duration: 90 } }
    }
}
