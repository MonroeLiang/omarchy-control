import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Flat, outlined, or filled, with a visible keyboard focus state and a label that
// says what pressing it does.
AbstractButton {
    id: root
    property string iconName: ""
    property string leadingIcon: ""
    property real iconRotation: 0
    property string style: "outline" // outline, flat, primary, danger, link
    property bool busy: false

    implicitHeight: Math.round(theme.fontSize * 2.2) + 2
    implicitWidth: row.implicitWidth + (style === "link" ? 2 : 24)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text
    Accessible.role: Accessible.Button
    opacity: enabled ? 1 : 0.5

    // A disabled destructive button is not a warning, so it stops looking like one.
    readonly property color tint: !enabled && style === "danger" ? theme.muted
                                : style === "primary" ? theme.accentForeground
                                : style === "danger" ? theme.dangerText
                                : style === "link" ? theme.accentText
                                : theme.foreground

    background: Rectangle {
        radius: theme.controlRadius
        color: root.style === "primary" ? (root.down ? Qt.darker(theme.accent, 1.2) : root.hovered ? Qt.darker(theme.accent, 1.08) : theme.accent)
             : root.style === "link" ? "transparent"
             : root.down ? Qt.alpha(theme.foreground, 0.14)
             : root.hovered ? Qt.alpha(theme.foreground, 0.08)
             : root.style === "outline" ? Qt.alpha(theme.foreground, 0.03)
             : "transparent"
        border.width: root.visualFocus ? 2 : (root.style === "outline" || root.style === "danger" ? 1 : 0)
        border.color: root.visualFocus ? theme.accent
                    : root.style === "danger" && root.enabled ? theme.dangerText
                    : theme.divider
        Behavior on color { enabled: theme.animate; ColorAnimation { duration: 100 } }
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 8
            AppIcon {
                visible: root.leadingIcon.length > 0
                name: root.leadingIcon
                tint: root.tint
                size: Math.round(theme.fontSize * 1.1)
            }
            AppText {
                visible: root.text.length > 0
                text: root.text
                color: root.tint
                font.weight: root.style === "primary" || root.style === "link" ? Font.Medium : Font.Normal
                font.underline: root.style === "link" && (root.hovered || root.visualFocus)
            }
            AppIcon {
                visible: root.iconName.length > 0
                mirror: root.mirrored && (root.iconName === "arrow" || root.iconName === "chevronright")
                name: root.iconName
                tint: root.tint
                size: Math.round(theme.fontSize * 1.1)
                rotation: root.iconRotation
                Behavior on rotation { enabled: theme.animate; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }
        }
    }
}
