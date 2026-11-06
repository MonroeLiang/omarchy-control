import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

TextField {
    id: root
    property string label: ""
    implicitHeight: Math.round(theme.fontSize * 2.2) + 2
    font.family: "monospace"
    font.pixelSize: theme.fontSize
    color: theme.foreground
    selectionColor: theme.accent
    selectedTextColor: theme.accentForeground
    placeholderTextColor: theme.muted
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: Text.AlignVCenter
    Accessible.name: label
    Material.containerStyle: Material.Outlined
    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: "transparent"
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? theme.accent : theme.divider
    }
}
