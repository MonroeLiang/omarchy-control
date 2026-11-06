import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 8
    readonly property var rows: backend.history
    BlockHeading { Layout.fillWidth: true; title: qsTr("Change history") }
    AppText { visible: root.rows.length === 0; text: qsTr("Nothing has been changed here yet."); secondary: true }
    AppPanel {
        Layout.fillWidth: true
        visible: root.rows.length > 0
        Repeater {
            model: root.rows.slice(0, 40)
            delegate: Item {
                width: parent ? parent.width : 400
                height: historyColumn.implicitHeight + 16
                function outcome() {
                    switch (modelData.phase) {
                    case "committed": return modelData.undoneBy ? qsTr("Undone") : qsTr("Done and confirmed")
                    case "rolled-back": return qsTr("Failed, previous value restored")
                    case "partial": return qsTr("Partly applied")
                    case "failed": return qsTr("Not applied")
                    case "interrupted": return qsTr("Interrupted; check the result")
                    case "not-applied": return qsTr("Not applied")
                    }
                    return modelData.phase
                }
                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    ColumnLayout {
                        id: historyColumn
                        Layout.fillWidth: true
                        spacing: 1
                        AppText {
                            Layout.fillWidth: true
                            text: qsTranslate("catalog", modelData.label) + (modelData.after.length ? ": " + (modelData.before.length ? modelData.before + " → " : "") + modelData.after : "")
                        }
                        AppText {
                            Layout.fillWidth: true
                            secondary: true
                            scale_: 0.82
                            wrapMode: Text.Wrap
                            text: modelData.at.toLocaleString(Qt.locale(), Locale.ShortFormat) + " · " + parent.parent.parent.outcome() + (modelData.message.length ? " · " + modelData.message : "")
                        }
                    }
                    AppButton {
                        visible: modelData.undoable
                        text: qsTr("Undo")
                        iconName: "undo"
                        onClicked: backend.undo(modelData.id)
                    }
                }
            }
        }
    }
}
