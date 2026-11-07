import QtQuick
import QtQuick.Layouts

// The battery at a glance: how full, what it is doing, and how worn. Only what the
// battery reports is shown, and a computer without one has no such section at all.
ColumnLayout {
    id: root
    readonly property var battery: backend.system.battery
    readonly property int chargePercent: battery.percent === undefined ? -1 : battery.percent
    readonly property bool chargeKnown: chargePercent >= 0 && chargePercent <= 100
    // Charge colors keep their meaning even when a theme maps its green to an accent.
    readonly property color chargeColor: chargePercent <= 20 ? "#e05252"
                                        : chargePercent <= 50 ? "#aa8200" : "#32945d"
    // Read by the panel holding this block, which leaves the page when this is false.
    readonly property bool shown: !!battery.present
    visible: shown
    spacing: 12

    function takeFocus() {
        summary.forceActiveFocus(Qt.TabFocusReason)
    }

    Timer {
        interval: 30000
        repeat: true
        running: root.visible && win.active
        onTriggered: backend.system.refresh()
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
        GridLayout {
            id: batteryLayout
            readonly property bool compact: width < theme.fontSize * 28
            width: parent.width
            columns: compact ? 2 : 4
            columnSpacing: 12
            rowSpacing: 6
            Item {
                objectName: "batteryGauge"
                implicitWidth: Math.round(theme.fontSize * 4.8)
                implicitHeight: Math.round(theme.fontSize * 1.6)
                Layout.leftMargin: 4
                Layout.alignment: Qt.AlignVCenter
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                Accessible.ignored: true

                Rectangle {
                    id: batteryBody
                    width: parent.width - 4
                    height: parent.height
                    radius: Math.min(3, theme.controlRadius)
                    color: "transparent"
                    border.width: 1
                    border.color: root.chargeKnown ? root.chargeColor : theme.muted

                    Row {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 2
                        Repeater {
                            model: 10
                            delegate: Rectangle {
                                required property int index
                                width: (parent.width - parent.spacing * 9) / 10
                                height: parent.height
                                color: theme.hairline
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(1, root.chargePercent / 10 - index))
                                    height: parent.height
                                    visible: root.chargeKnown
                                    color: root.chargeColor
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    x: batteryBody.width + 1
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: Math.round(parent.height * 0.35)
                    color: batteryBody.border.color
                }
            }
            AppText {
                id: percent
                Layout.fillWidth: batteryLayout.compact
                text: root.chargeKnown ? qsTr("%1%").arg(root.chargePercent) : qsTr("Unknown")
                font.weight: Font.DemiBold
                scale_: 1.4
            }
            AppText {
                id: status
                Layout.fillWidth: true
                Layout.columnSpan: batteryLayout.compact ? 2 : 1
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
                Layout.fillWidth: batteryLayout.compact
                Layout.columnSpan: batteryLayout.compact ? 2 : 1
                secondary: true
                text: qsTr("%n charge cycle(s)", "", root.battery.cycles || 0)
                wrapMode: Text.Wrap
                Layout.rightMargin: 4
            }
        }
    }
}
