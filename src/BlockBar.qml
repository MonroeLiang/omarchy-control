import QtQuick
import QtQuick.Layouts

// Widget placement and dock pins, through Omarchy's commands so per-widget options
// and a custom bar's identity survive a move.
ColumnLayout {
    id: root
    spacing: 10
    readonly property var plugins: backend.plugins
    readonly property var sections: [
        { id: "left", label: qsTr("Left") }, { id: "centerBegin", label: qsTr("Before center") },
        { id: "center", label: qsTr("Center") }, { id: "centerEnd", label: qsTr("After center") },
        { id: "right", label: qsTr("Right") }, { id: "tray", label: qsTr("Tray drawer") }
    ]
    function widgetName(id) {
        var list = plugins.plugins
        for (var i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i].name
        return id
    }

    BlockHeading { Layout.fillWidth: true; title: qsTr("Bar layout") }
    AppText {
        visible: plugins.activeBar.length > 0 && plugins.activeBar !== "omarchy.bar"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        secondary: true
        text: qsTr("Your active bar is %1. It reads this layout, but a custom bar can draw it differently.").arg(plugins.activeBar)
    }
    AppPanel {
        Layout.fillWidth: true
        Repeater {
            model: root.sections
            delegate: Item {
                id: sectionRow
                readonly property string sectionId: modelData.id
                readonly property var widgets: root.plugins.barLayout[modelData.id] || []
                width: parent ? parent.width : 400
                height: flow.implicitHeight + 18
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12
                    AppText { text: modelData.label; secondary: true; Layout.preferredWidth: 130; Layout.alignment: Qt.AlignTop; Layout.topMargin: 8 }
                    Flow {
                        id: flow
                        Layout.fillWidth: true
                        spacing: 6
                        AppText { visible: sectionRow.widgets.length === 0; text: qsTr("Empty"); secondary: true; height: 30 }
                        Repeater {
                            model: sectionRow.widgets
                            delegate: AppCombo {
                                id: chip
                                readonly property string widgetId: modelData
                                readonly property int widgetIndex: index
                                label: qsTr("Move %1").arg(root.widgetName(widgetId))
                                implicitWidth: Math.min(260, nameMetrics.width + 72)
                                displayText: root.widgetName(widgetId)
                                TextMetrics { id: nameMetrics; font.family: "monospace"; font.pixelSize: theme.fontSize; text: root.widgetName(chip.widgetId) }
                                model: root.sections.map(function (s) { return qsTr("Move to %1").arg(s.label) })
                                currentIndex: -1
                                onActivated: function (i) {
                                    currentIndex = -1
                                    var destination = root.sections[i].id
                                    if (destination !== sectionRow.sectionId)
                                        root.plugins.moveWidget(widgetId, destination, (root.plugins.barLayout[destination] || []).length, sectionRow.sectionId, widgetIndex)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    BlockHeading { Layout.fillWidth: true; title: qsTr("Pinned dock apps") }
    AppText { visible: plugins.dockPins.length === 0; secondary: true; text: qsTr("Nothing is pinned.") }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: root.plugins.dockPins
            delegate: AppButton {
                text: modelData
                iconName: "close"
                Accessible.name: qsTr("Unpin %1").arg(modelData)
                onClicked: root.plugins.unpin(modelData)
            }
        }
    }
}
