import QtQuick
import QtQuick.Layouts

ColumnLayout {
    property string title: ""
    property string subtitle: ""
    spacing: 2
    AppText {
        text: title
        font.bold: true
        scale_: 1.75
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
    }
    AppText {
        text: subtitle
        secondary: true
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }
}
