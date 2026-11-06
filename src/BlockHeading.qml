import QtQuick
import QtQuick.Layouts

RowLayout {
    property string title: ""
    default property alias actions: tail.data
    spacing: 8
    AppText {
        text: parent.title
        font.bold: true
        scale_: 1.1
        Layout.fillWidth: true
    }
    Row {
        id: tail
        spacing: 8
    }
}
