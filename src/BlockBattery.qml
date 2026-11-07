import QtQuick
import QtQuick.Layouts

// The battery at a glance: how full, what it is doing, and how worn. Only what the
// battery reports is shown, and a computer without one has no such section at all.
ColumnLayout {
    id: root
    readonly property var battery: backend.system.battery
    // Read by the panel holding this block, which leaves the page when this is false.
    readonly property bool shown: !!battery.present
    visible: shown
    spacing: 12

    function takeFocus() {
        summary.forceActiveFocus(Qt.TabFocusReason)
    }

    PanelHeading {
        Layout.fillWidth: true
        title: qsTr("Battery")
    }
    AppPanel {
        id: summary
        objectName: "batterySummary"
        Layout.fillWidth: true
        padding: 14
        activeFocusOnTab: true
        border.color: activeFocus ? theme.accent : theme.hairline
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Battery") + ", " + percent.text + (status.text ? ", " + status.text : "") + (cycles.visible ? ", " + cycles.text : "")
        RowLayout {
            width: parent.width
            spacing: 12
            AppIcon { name: "battery"; size: Math.round(theme.fontSize * 1.6); Layout.leftMargin: 4 }
            AppText {
                id: percent
                text: root.battery.percent !== undefined && root.battery.percent >= 0 ? qsTr("%1%").arg(root.battery.percent) : qsTr("Unknown")
                font.weight: Font.DemiBold
                scale_: 1.4
            }
            AppText {
                id: status
                Layout.fillWidth: true
                secondary: true
                // The kernel's words, in the window's language where they are known ones.
                text: {
                    switch (root.battery.status || "") {
                    case "Full": return qsTr("Full")
                    case "Charging": return qsTr("Charging")
                    case "Discharging": return qsTr("On battery")
                    case "Not charging": return qsTr("Plugged in, not charging")
                    case "Unknown": case "": return ""
                    default: return root.battery.status
                    }
                }
                wrapMode: Text.Wrap
            }
            AppText {
                id: cycles
                visible: !!root.battery.cycles
                secondary: true
                text: qsTr("%n charge cycle(s)", "", root.battery.cycles || 0)
                Layout.rightMargin: 4
            }
        }
    }
}
