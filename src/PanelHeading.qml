import QtQuick
import QtQuick.Layouts

// A section's heading above its card: a line icon, the name, a quiet count, and room at
// the end for a control that belongs to the whole section, such as its power switch.
RowLayout {
    id: root
    property string icon: ""
    property string title: ""
    property string detail: ""
    default property alias trailing: tail.data
    spacing: 12
    AppIcon {
        visible: root.icon.length > 0
        name: root.icon
        size: Math.round(theme.fontSize * 1.6)
        tint: theme.foreground
        Layout.alignment: Qt.AlignVCenter
    }
    AppText {
        text: root.title
        font.weight: Font.DemiBold
        scale_: 1.4
        Layout.alignment: Qt.AlignVCenter
        Layout.fillWidth: root.detail.length === 0
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }
    AppText {
        visible: root.detail.length > 0
        text: root.detail
        secondary: true
        scale_: 0.95
        Layout.leftMargin: 2
        Layout.alignment: Qt.AlignVCenter
        Layout.fillWidth: true
    }
    Row {
        id: tail
        spacing: 8
        Layout.alignment: Qt.AlignVCenter
    }
}
