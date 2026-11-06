import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Flat, outlined, or filled, with a visible keyboard focus state and a label that
// says where a launch goes.
AbstractButton {
    id: root
    property string iconName: ""
    property string style: "outline" // outline, flat, primary, danger, link
    property bool busy: false

    implicitHeight: Math.round(theme.fontSize * 2.2) + 2
    implicitWidth: row.implicitWidth + (style === "link" ? 2 : 24)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text
    Accessible.role: Accessible.Button
    opacity: enabled ? 1 : style === "primary" ? 0.85 : 0.5

    readonly property color tint: style === "primary" ? theme.accentForeground
                                : style === "danger" ? theme.danger
                                : style === "link" ? theme.accent
                                : theme.foreground

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: root.style === "primary" ? theme.accent
             : root.style === "link" ? "transparent"
             : (root.hovered || root.down) ? Qt.alpha(theme.foreground, 0.08) : "transparent"
        border.width: root.visualFocus ? 2 : (root.style === "outline" || root.style === "danger" ? 1 : 0)
        border.color: root.visualFocus ? theme.accent : root.style === "danger" ? theme.danger : theme.divider
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 8
            AppText {
                visible: root.text.length > 0
                text: root.text
                color: root.tint
            }
            AppIcon {
                visible: root.iconName.length > 0
                mirror: root.mirrored && root.iconName === "arrow"
                name: root.iconName
                tint: root.tint
                size: Math.round(theme.fontSize * 1.1)
            }
        }
    }
}
