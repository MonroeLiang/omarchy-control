import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 10
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
                height: 46
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
        AppText { text: qsTr("Installed apps"); font.bold: true; scale_: 1.1; Layout.fillWidth: true }
        AppButton {
            style: "flat"
            text: root.expanded ? qsTr("Hide") : qsTr("Show %n", "", root.system.apps.length)
            iconName: "chevron"
            onClicked: root.expanded = !root.expanded
        }
    }
    AppField { visible: root.expanded; Layout.fillWidth: true; label: qsTr("Filter apps"); placeholderText: qsTr("Filter apps"); onTextChanged: root.filter = text }
    AppPanel {
        visible: root.expanded
        Layout.fillWidth: true
        Repeater {
            model: root.expanded ? root.visibleApps.slice(0, 200) : []
            delegate: Item {
                width: parent ? parent.width : 400
                height: 44
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        AppText { Layout.fillWidth: true; text: modelData.name }
                        AppText { Layout.fillWidth: true; visible: text.length > 0; secondary: true; scale_: 0.82; text: modelData.comment }
                    }
                    AppButton { text: qsTr("Open"); iconName: "external"; onClicked: root.system.launchApp(modelData.id) }
                }
            }
        }
    }
}
