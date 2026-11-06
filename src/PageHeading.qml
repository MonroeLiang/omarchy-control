import QtQuick
import QtQuick.Layouts

ColumnLayout {
    property string title: ""
    property string subtitle: ""
    spacing: 4
    AppText {
        text: title
        font.weight: Font.Bold
        font.letterSpacing: -0.3
        scale_: 2
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
