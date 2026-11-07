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
    // A leading icon that turns while the work it starts is under way.
    property bool spinning: false
    // A larger label for the one action a panel is about.
    property real textScale: 1

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
        border.width: root.visualFocus && root.style !== "primary" ? 2 : (root.style === "outline" || root.style === "danger" ? 1 : 0)
        border.color: root.visualFocus ? theme.accent
                    : root.style === "danger" && root.enabled ? theme.dangerText
                    : theme.divider
        Behavior on color { enabled: theme.animate; ColorAnimation { duration: 100 } }
        // An amber button's own edge cannot show amber focus, so the ring sits just outside it.
        Rectangle {
            visible: root.visualFocus && root.style === "primary"
            anchors.fill: parent
            anchors.margins: -4
            radius: parent.radius + 4
            color: "transparent"
            border.width: 2
            border.color: theme.accent
        }
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 8
            AppIcon {
                id: leading
                visible: root.leadingIcon.length > 0
                name: root.leadingIcon
                tint: root.tint
                size: Math.round(theme.fontSize * 1.1 * root.textScale)
                rotation: 0
                RotationAnimator on rotation {
                    running: root.spinning && theme.animate
                    from: 0
                    to: 360
                    duration: 1200
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) leading.rotation = 0
                }
            }
            AppText {
                visible: root.text.length > 0
                text: root.text
                scale_: root.textScale
                color: root.tint
                font.weight: root.textScale > 1 ? Font.DemiBold : root.style === "primary" || root.style === "link" ? Font.Medium : Font.Normal
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
