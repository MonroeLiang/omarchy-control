import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shows the state that was last read back. Clicking asks for a change; the knob
// only moves once the new value has been confirmed. State is also spelled out in
// words, so it is never communicated by color alone.
AbstractButton {
    id: root
    property bool on: false
    property bool busy: false
    // Not read yet: saying "Off" here would be a claim nothing has established.
    property bool unknown: false
    property string label: ""

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: 0
    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.checkable: true
    Accessible.checked: on

    contentItem: RowLayout {
        spacing: 8
        AppText {
            text: root.unknown ? "…" : root.on ? qsTr("On") : qsTr("Off")
            secondary: true
            horizontalAlignment: Text.AlignRight
            Layout.minimumWidth: Math.round(theme.fontSize * 2)
        }
        Rectangle {
            id: track
            opacity: root.enabled ? (root.busy ? 0.6 : 1) : 0.45
            implicitWidth: 44
            implicitHeight: 24
            radius: height / 2
            color: root.on ? (root.hovered ? Qt.darker(theme.accent, 1.08) : theme.accent)
                           : Qt.alpha(theme.foreground, root.hovered ? 0.24 : 0.15)
            border.width: root.visualFocus ? 2 : 0
            border.color: root.on ? theme.foreground : theme.accent
            Behavior on color { enabled: theme.animate; ColorAnimation { duration: 120 } }
            Rectangle {
                width: 18
                height: 18
                radius: 9
                y: 3
                x: root.unknown ? 13 : root.on !== root.mirrored ? parent.width - width - 3 : 3
                color: root.on ? theme.accentForeground : theme.foreground
                Behavior on x { enabled: theme.animate; NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
}
