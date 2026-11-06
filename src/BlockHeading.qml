import QtQuick
import QtQuick.Layouts

RowLayout {
    property string title: ""
    default property alias actions: tail.data
    spacing: 8
    Layout.preferredHeight: Math.round(theme.fontSize * 2)
    AppText {
        text: parent.title
        font.weight: Font.DemiBold
        scale_: 1.25
        Layout.fillWidth: true
    }
    Row {
        id: tail
        spacing: 8
    }
}
