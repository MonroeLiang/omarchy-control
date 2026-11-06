import QtQuick
import QtQuick.Layouts

// Outcomes in words: "Opened" and "changed" are different messages, because opening
// a helper says nothing about whether anything changed.
Rectangle {
    id: root
    property string kind: "ok"
    property string message: ""

    function show(text, type) {
        if (!text || !text.length)
            return
        message = text
        kind = type
        hide.interval = type === "error" || type === "warning" ? 9000 : 3500
        hide.restart()
        opacity = 1
    }

    anchors.bottom: parent.bottom
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottomMargin: 18
    width: Math.min(row.implicitWidth + 28, parent.width - 40)
    height: row.implicitHeight + 18
    radius: theme.radius
    color: theme.raised
    border.width: 1
    border.color: kind === "error" ? theme.danger : kind === "warning" ? theme.warning : theme.accent
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { enabled: theme.animate; NumberAnimation { duration: 140 } }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: message

    Timer { id: hide; onTriggered: root.opacity = 0 }
    TapHandler { onTapped: root.opacity = 0 }

    RowLayout {
        id: row
        anchors.centerIn: parent
        width: Math.min(implicitWidth, root.parent ? root.parent.width - 68 : 500)
        spacing: 10
        AppIcon {
            name: root.kind === "error" || root.kind === "warning" ? "warning" : root.kind === "opened" ? "external" : "check"
            tint: root.kind === "error" ? theme.danger : root.kind === "warning" ? theme.warning : theme.accent
        }
        AppText { Layout.fillWidth: true; text: root.message; wrapMode: Text.Wrap }
    }
}
