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
        border.color: theme.accent
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 20
            spacing: 8
            AppText { text: qsTr("Keyboard shortcuts"); font.bold: true; scale_: 1.2 }
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
                    AppText { text: modelData[0]; color: theme.accent; Layout.preferredWidth: 170 }
                    AppText { text: modelData[1]; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
