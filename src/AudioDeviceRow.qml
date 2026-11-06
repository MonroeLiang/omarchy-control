import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var device: ({})
    property bool last: false
    signal select()
    signal volume(int value)
    signal mute(bool on)

    width: parent ? parent.width : 400
    height: theme.rowHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10
        // One of a set, so a radio: picking is what it does, and which one is in use
        // shows without a button pretending to be a status.
        AbstractButton {
            id: choose
            implicitWidth: 22
            implicitHeight: 22
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            enabled: !root.device.isDefault
            Accessible.role: Accessible.RadioButton
            Accessible.name: qsTr("Use %1").arg(root.device.description)
            Accessible.checkable: true
            Accessible.checked: !!root.device.isDefault
            onClicked: root.select()
            background: Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 18
                radius: 9
                color: choose.hovered && choose.enabled ? Qt.alpha(theme.foreground, 0.08) : "transparent"
                border.width: choose.visualFocus || root.device.isDefault ? 2 : 1
                border.color: choose.visualFocus || root.device.isDefault ? theme.accent : theme.muted
                Rectangle {
                    anchors.centerIn: parent
                    visible: !!root.device.isDefault
                    width: 8
                    height: 8
                    radius: 4
                    color: theme.accent
                }
            }
        }
        AppText {
            Layout.fillWidth: true
            text: root.device.description
            MouseArea { anchors.fill: parent; enabled: !root.device.isDefault; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: root.select() }
        }
        AppButton {
            text: root.device.mute ? qsTr("Muted") : qsTr("Mute")
            Layout.minimumWidth: Math.round(theme.fontSize * 5.5)
            onClicked: root.mute(!root.device.mute)
        }
        AppSlider {
            id: slider
            label: qsTr("Volume for %1").arg(root.device.description)
            Layout.preferredWidth: Math.min(200, root.width * 0.22)
            from: 0
            to: 100
            stepSize: 1
            value: Math.min(100, Math.max(0, root.device.volume))
            onPressedChanged: if (!pressed) root.volume(Math.round(value))
            Keys.onReleased: function (event) { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) root.volume(Math.round(value)) }
        }
        AppText { Layout.minimumWidth: 50; horizontalAlignment: Text.AlignRight; text: Math.round(slider.value).toLocaleString(Qt.locale(), "f", 0) + "%" }
    }
    Rectangle { visible: !root.last; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 14; height: 1; color: theme.divider; opacity: 0.6 }
}
