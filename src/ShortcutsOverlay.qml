import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    signal closeRequested()
    anchors.fill: parent
    color: Qt.alpha(theme.deep, 0.75)
    MouseArea { anchors.fill: parent; onClicked: root.closeRequested() }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 40)
        height: list.implicitHeight + 40
        radius: theme.radius
        color: theme.window
        border.width: 1
        border.color: theme.divider
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10
            AppText { text: qsTr("Keyboard shortcuts"); font.bold: true; scale_: 1.1; Layout.bottomMargin: 6; Accessible.role: Accessible.Heading }
            Repeater {
                model: [
                    ["Ctrl K, Ctrl F", qsTr("Search")],
                    ["Tab, Shift Tab", qsTr("Move between controls")],
                    ["Space", qsTr("Activate the focused control")],
                    ["Alt Left", qsTr("Go back")],
                    ["Ctrl Z", qsTr("Undo the last change that can be undone")],
                    ["Ctrl Shift Z", qsTr("Redo")],
                    ["Esc", qsTr("Close this, a dialog, or search")],
                    ["?", qsTr("Show or hide this list")],
                    ["Q, Ctrl Q", qsTr("Quit")]
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Item {
                        Layout.preferredWidth: Math.round(theme.fontSize * 12)
                        Layout.preferredHeight: keys.implicitHeight + 6
                        Rectangle {
                            width: keys.implicitWidth + 12
                            height: parent.height
                            radius: Math.min(4, theme.radius)
                            color: Qt.alpha(theme.raised, 0.8)
                            border.width: 1
                            border.color: theme.divider
                            AppText { id: keys; anchors.centerIn: parent; text: modelData[0]; scale_: 0.88 }
                        }
                    }
                    AppText { text: modelData[1]; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
