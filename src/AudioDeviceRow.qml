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
    height: Math.round(theme.fontSize * 3.6)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12
        AppButton {
            style: root.device.isDefault ? "primary" : "outline"
            text: root.device.isDefault ? qsTr("In use") : qsTr("Use")
            iconName: root.device.isDefault ? "check" : ""
            enabled: !root.device.isDefault
            Accessible.name: qsTr("Use %1").arg(root.device.description)
            onClicked: root.select()
        }
        AppText { Layout.fillWidth: true; text: root.device.description }
        AppButton {
            style: "flat"
            text: root.device.mute ? qsTr("Muted") : qsTr("Mute")
            onClicked: root.mute(!root.device.mute)
        }
        AppSlider {
            id: slider
            label: qsTr("Volume for %1").arg(root.device.description)
            Layout.preferredWidth: Math.min(200, root.width * 0.2)
            from: 0
            to: 100
            stepSize: 1
            value: Math.min(100, Math.max(0, root.device.volume))
            onPressedChanged: if (!pressed) root.volume(Math.round(value))
            Keys.onReleased: function (event) { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) root.volume(Math.round(value)) }
        }
        AppText { Layout.minimumWidth: 44; horizontalAlignment: Text.AlignRight; text: Math.round(slider.value).toLocaleString(Qt.locale(), "f", 0) + "%" }
    }
    Rectangle { visible: !root.last; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 14; height: 1; color: theme.divider; opacity: 0.6 }
}
