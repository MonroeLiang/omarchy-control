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
    leftPadding: 10

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: root.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? theme.accent : theme.divider
    }
    contentItem: AppText {
        leftPadding: root.mirrored ? 28 : 10
        rightPadding: root.mirrored ? 10 : 28
        text: root.displayText
    }
    indicator: AppIcon {
        name: "chevron"
        size: 14
        x: root.mirrored ? 8 : root.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
    }
}
