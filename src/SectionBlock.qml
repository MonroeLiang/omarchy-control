import QtQuick
import QtQuick.Layouts

// A titled group of rows. Long lists, like everything installable, start closed so
// a page stays readable; search still reaches every row.
ColumnLayout {
    id: root
    property string title: ""
    property var rows: []
    property string focusId: ""
    // A quiet note at the heading's end, such as which display the rows act on.
    property string aside: ""
    readonly property int actionCount: rows.filter(function (row) { return row.type === "action" }).length
    readonly property bool collapsible: actionCount > 12 && actionCount === rows.length
    readonly property bool containsFocus: {
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].id === focusId)
                return true
        return false
    }
    property bool expanded: !collapsible
    spacing: 8
    onContainsFocusChanged: if (containsFocus) expanded = true

    function rowItem(id) {
        for (var i = 0; i < repeater.count; ++i)
            if (repeater.itemAt(i) && repeater.itemAt(i).entryId === id)
                return repeater.itemAt(i)
        return null
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.round(theme.fontSize * 2)
        spacing: 8
        AppText {
            text: qsTranslate("catalog", root.title)
            font.weight: Font.DemiBold
            scale_: 1.25
            Layout.fillWidth: true
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        AppText {
            objectName: "sectionAside"
            visible: root.aside.length > 0
            text: root.aside
            secondary: true
            scale_: 0.95
        }
        AppButton {
            visible: root.collapsible
            style: "flat"
            implicitHeight: Math.round(theme.fontSize * 1.75)
            text: root.expanded ? qsTr("Hide") : qsTr("Show %n", "", root.rows.length)
            iconName: "chevron"
            iconRotation: root.expanded ? 180 : 0
            onClicked: root.expanded = !root.expanded
        }
    }
    AppPanel {
        visible: root.expanded
        Layout.fillWidth: true
        Repeater {
            id: repeater
            model: root.expanded ? root.rows : []
            delegate: SettingRow {
                width: parent ? parent.width : 400
                entry: modelData
                last: index === root.rows.length - 1
                highlighted: root.focusId === modelData.id
                rich: true
            }
        }
    }
}
