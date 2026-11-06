import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 8
    readonly property var system: backend.system
    property bool expanded: false
    property string filter: ""
    readonly property var visibleApps: system.apps.filter(function (a) {
        return !a.hidden && (!filter.length || (a.name + " " + a.comment).toLowerCase().indexOf(filter.toLowerCase()) >= 0)
    })

    BlockHeading { Layout.fillWidth: true; title: qsTr("File associations") }
    AppPanel {
        Layout.fillWidth: true
        visible: system.associations.length > 0
        Repeater {
            model: system.associations
            delegate: Item {
                width: parent ? parent.width : 400
                height: theme.rowHeight
                RowDivider {}
                readonly property var options: {
                    var list = modelData.candidates.slice()
                    var known = false
                    for (var i = 0; i < list.length; ++i)
                        if (list[i].value === modelData.current)
                            known = true
                    if (!known)
                        list.unshift({ value: modelData.current, label: modelData.current.length ? modelData.current : qsTr("Not set") })
                    return list
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    AppText { Layout.fillWidth: true; text: modelData.label }
                    AppCombo {
                        label: modelData.label
                        implicitWidth: 300
                        model: parent.parent.options
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: {
                            for (var i = 0; i < parent.parent.options.length; ++i)
                                if (parent.parent.options[i].value === modelData.current)
                                    return i
                            return -1
                        }
                        onActivated: function (i) {
                            var value = parent.parent.options[i].value
                            if (value.length && value !== modelData.current)
                                root.system.setAssociation(modelData.mime, value)
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.preferredHeight: Math.round(theme.fontSize * 2)
        AppText { text: qsTr("Installed apps"); font.weight: Font.DemiBold; scale_: 1.25; Layout.fillWidth: true }
        AppButton {
            style: "flat"
            implicitHeight: Math.round(theme.fontSize * 1.75)
            text: root.expanded ? qsTr("Hide") : qsTr("Show %n", "", root.system.apps.length)
            iconName: "chevron"
            iconRotation: root.expanded ? 180 : 0
            visible: root.system.apps.length > 0
            onClicked: root.expanded = !root.expanded
        }
    }
    AppText { visible: root.system.apps.length === 0; text: qsTr("No apps found."); secondary: true }
    AppField { visible: root.expanded; Layout.fillWidth: true; label: qsTr("Filter apps"); placeholderText: qsTr("Filter apps"); onTextChanged: root.filter = text }
    AppPanel {
        visible: root.expanded
        Layout.fillWidth: true
        Repeater {
            model: root.expanded ? root.visibleApps.slice(0, 200) : []
            delegate: Item {
                width: parent ? parent.width : 400
                height: theme.rowHeight
                RowDivider {}
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.name }
                        AppText { Layout.fillWidth: true; visible: text.length > 0; secondary: true; scale_: 0.88; text: modelData.comment }
                    }
                    AppButton { text: qsTr("Open"); iconName: "external"; onClicked: root.system.launchApp(modelData.id) }
                }
            }
        }
    }
}
