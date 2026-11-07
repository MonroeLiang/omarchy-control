import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One sound device: which one is in use, what it is, whether it is muted, and how loud.
// The name shown is the short one; the full description stays in what is read aloud.
Item {
    id: root
    property var device: ({})
    property bool last: false
    // A change of default on its way to this device, not confirmed yet.
    property bool pending: false
    // Another device is being made the default, so this choice waits.
    property bool locked: false
    readonly property bool chosen: !!device.isDefault
    readonly property string title: device.title || device.description || ""
    signal select()
    signal volume(int value)
    signal mute(bool on)

    function takeFocus() {
        (choose.enabled ? choose : muteButton).forceActiveFocus(Qt.TabFocusReason)
    }

    width: parent ? parent.width : 400
    implicitHeight: Math.max(theme.rowHeight, names.implicitHeight + 20)
    height: implicitHeight

    // The device in use stands out by more than its radio.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: theme.controlRadius
        visible: root.chosen || root.pending
        color: Qt.alpha(theme.accent, root.pending ? 0.05 : 0.09)
    }
    Rectangle {
        visible: !root.last
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        height: 1
        color: theme.hairline
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 14
        spacing: 12
        // One of a set, so a radio: picking is what it does, and which one is in use
        // shows without a button pretending to be a status.
        AbstractButton {
            id: choose
            objectName: "deviceChoose"
            implicitWidth: 24
            implicitHeight: 24
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            enabled: !root.chosen && !root.pending && !root.locked
            Accessible.role: Accessible.RadioButton
            Accessible.name: qsTr("Use %1").arg(root.device.description || "")
            Accessible.checkable: true
            Accessible.checked: root.chosen
            onClicked: root.select()
            background: Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 18
                radius: 9
                color: choose.hovered && choose.enabled ? Qt.alpha(theme.foreground, 0.08) : "transparent"
                border.width: choose.visualFocus || root.chosen ? 2 : 1
                border.color: choose.visualFocus || root.chosen ? theme.accent : theme.muted
                Rectangle {
                    anchors.centerIn: parent
                    visible: root.chosen
                    width: 8
                    height: 8
                    radius: 4
                    color: theme.accent
                }
            }
        }
        AppIcon {
            name: root.device.kind || "speaker"
            size: Math.round(theme.fontSize * 1.45)
            tint: root.device.mute ? theme.muted : theme.foreground
        }
        // The names, and whether this is the one in use, picked by clicking them too.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item {
                Layout.fillWidth: true
                implicitHeight: names.implicitHeight
                // Clicking the names picks the device, as the radio does.
                MouseArea {
                    anchors.fill: parent
                    enabled: choose.enabled
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.select()
                }
            ColumnLayout {
                id: names
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                AppText {
                    objectName: "deviceTitle"
                    Layout.fillWidth: true
                    text: root.title
                    font.weight: root.chosen ? Font.Medium : Font.Normal
                }
                AppText {
                    visible: text.length > 0
                    Layout.fillWidth: true
                    text: root.pending ? qsTr("Switching to this device…") : root.device.subtitle || ""
                    secondary: true
                    scale_: 0.88
                }
            }
            }
            Rectangle {
                visible: root.chosen
                implicitHeight: Math.round(theme.fontSize * 1.6)
                implicitWidth: defaultLabel.implicitWidth + 16
                radius: theme.controlRadius
                color: Qt.alpha(theme.accent, 0.14)
                border.width: 1
                border.color: Qt.alpha(theme.accent, 0.5)
                AppText {
                    id: defaultLabel
                    anchors.centerIn: parent
                    text: qsTr("Default")
                    color: theme.accentText
                    scale_: 0.85
                }
            }
        }
        // Muted says so three ways: the crossed-out speaker, the filled button, and its name.
        AppButton {
            id: muteButton
            objectName: "deviceMute"
            style: root.device.mute ? "primary" : "outline"
            text: ""
            iconName: root.device.mute ? "soundoff" : "sound"
            implicitWidth: Math.round(theme.fontSize * 2.3)
            implicitHeight: Math.round(theme.fontSize * 2.1)
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Mute %1").arg(root.device.description || "")
            Accessible.checkable: true
            Accessible.checked: !!root.device.mute
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: root.device.mute ? qsTr("Unmute") : qsTr("Mute")
            onClicked: root.mute(!root.device.mute)
        }
        AppSlider {
            id: slider
            objectName: "deviceVolume"
            label: qsTr("Volume for %1").arg(root.device.description || "")
            Layout.preferredWidth: Math.max(80, Math.min(130, root.width * 0.2))
            opacity: root.device.mute ? 0.55 : 1
            from: 0
            to: 100
            stepSize: 1
            onPressedChanged: if (!pressed) root.volume(Math.round(value))
            Keys.onReleased: function (event) { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) root.volume(Math.round(value)) }
            // A fresh reading never yanks the handle out from under a drag.
            Binding on value {
                when: !slider.pressed
                value: Math.min(100, Math.max(0, root.device.volume))
            }
        }
        AppText {
            Layout.minimumWidth: Math.round(theme.fontSize * 3.2)
            horizontalAlignment: Text.AlignRight
            mono: true
            secondary: !!root.device.mute
            text: Math.round(slider.value).toLocaleString(Qt.locale(), "f", 0) + "%"
        }
    }
}
