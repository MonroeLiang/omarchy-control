import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 8
    readonly property var battery: backend.system.battery
    visible: !!battery.present
    BlockHeading { Layout.fillWidth: true; title: qsTr("Battery") }
    AppPanel {
        Layout.fillWidth: true
        padding: 14
        RowLayout {
            width: parent.width
            spacing: 12
            AppIcon { name: "battery"; size: 22 }
            AppText { text: qsTr("%1%").arg(battery.percent); font.weight: Font.DemiBold; scale_: 1.2 }
            AppText { Layout.fillWidth: true; secondary: true; text: battery.status || "" }
            AppText { visible: !!battery.cycles; secondary: true; text: qsTr("%n charge cycle(s)", "", battery.cycles || 0) }
        }
    }
}
