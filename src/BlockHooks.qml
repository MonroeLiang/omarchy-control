import QtQuick
import QtQuick.Layouts

// Hooks are executable code that belongs to the user, so this lists them and points
// at them; it does not guess at what they do.
ColumnLayout {
    id: root
    spacing: 8
    readonly property var hooks: backend.system.hooks
    BlockHeading { Layout.fillWidth: true; title: qsTr("Hooks") }
    AppText { visible: root.hooks.length === 0; text: qsTr("No active hooks. Omarchy's samples in ~/.config/omarchy/hooks stay inert until renamed."); secondary: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
    AppPanel {
        Layout.fillWidth: true
        visible: root.hooks.length > 0
        Repeater {
            model: root.hooks
            delegate: AppText {
                width: parent ? parent.width : 400
                height: 34
                leftPadding: 14
                text: modelData.name
            }
        }
    }
}
