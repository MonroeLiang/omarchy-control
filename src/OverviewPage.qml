import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: AppScrollBar {
        parent: root
        x: root.mirrored ? 0 : root.width - width
        height: root.availableHeight
    }
    readonly property var history: backend.history
    readonly property var problems: backend.problems
    readonly property var connectedWifi: {
        var devices = backend.network.devices
        for (var i = 0; i < devices.length; ++i)
            if (devices[i].type === "wifi" && devices[i].connected)
                return devices[i]
        return null
    }

    ColumnLayout {
        width: Math.min(root.availableWidth - 8, theme.fontSize * 72)
        x: root.mirrored ? root.availableWidth - width : 0
        spacing: 26

        Item { Layout.preferredHeight: 2 }
        PageHeading {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            title: qsTr("Overview")
            subtitle: qsTr("Settings and tools for this computer.")
        }

        // Status band: only things this app has actually established, said once.
        AppPanel {
            readonly property var only: root.problems.length === 1 ? root.problems[0] : null
            id: status
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            RowLayout {
                width: parent.width
                height: theme.rowHeight
                spacing: 12
                Item { Layout.preferredWidth: 2 }
                AppIcon { name: root.problems.length ? "warning" : "checkcircle"; tint: root.problems.length ? theme.warningText : theme.muted }
                AppText {
                    Layout.fillWidth: true
                    text: status.only ? status.only.text
                        : root.problems.length ? qsTr("%n problem(s) to look at", "", root.problems.length)
                        : qsTr("No problems found")
                }
                AppButton {
                    style: "link"
                    text: status.only ? qsTr("Open") : qsTr("View system")
                    iconName: "arrow"
                    onClicked: win.go(status.only ? status.only.page : "system", "")
                }
                Item { Layout.preferredWidth: 4 }
            }
        }

        Repeater {
            model: root.problems.length > 1 ? root.problems : []
            delegate: RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8
                AppIcon { name: "warning"; tint: theme.warningText; size: 16 }
                AppText { Layout.fillWidth: true; text: modelData.text; wrapMode: Text.Wrap }
                AppButton { style: "link"; text: qsTr("Open"); iconName: "arrow"; onClicked: win.go(modelData.page, "") }
            }
        }
        Repeater {
            model: backend.pendingRestarts
            delegate: RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8
                AppIcon { name: "refresh"; size: 16 }
                AppText { Layout.fillWidth: true; text: qsTranslate("catalog", modelData); wrapMode: Text.Wrap }
                AppButton { style: "flat"; text: qsTr("Dismiss"); onClicked: backend.clearRestartNotice() }
            }
        }

        ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        BlockHeading { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; title: qsTr("Quick settings") }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            // Scales with text size, so large text gets one wide column rather than clipped labels.
            columns: root.availableWidth > theme.fontSize * 58 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12

            QuickPanel {
                title: qsTr("Appearance")
                icon: "appearance"
                SettingRow { width: parent.width; compact: true; entry: backend.entry("appearance.theme") }
                SettingRow { width: parent.width; compact: true; entry: backend.entry("appearance.textsize"); last: true }
                footer: [
                    AppButton { style: "link"; text: qsTr("Customize appearance"); iconName: "arrow"; onClicked: win.go("appearance", "") }
                ]
            }
            QuickPanel {
                title: qsTr("Network & Bluetooth")
                icon: "wifi"
                Item {
                    width: parent.width
                    height: Math.max(wifiColumn.implicitHeight + 18, theme.rowHeight)
                    RowLayout {
                        id: wifiRow
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        ColumnLayout {
                            id: wifiColumn
                            Layout.fillWidth: true
                            spacing: 2
                            AppText { text: qsTr("Wi-Fi"); Layout.fillWidth: true }
                            AppText {
                                Layout.fillWidth: true
                                secondary: true
                                scale_: 0.88
                                // A Latin network name must not flip the line in a right-to-left language.
                                text: (root.mirrored ? "\u200F" : "") + (backend.network.status !== "ok" ? qsTr("NetworkManager is not responding")
                                    : !backend.network.wifiEnabled ? qsTr("Off")
                                    : root.connectedWifi ? qsTr("%1 · Connected").arg(root.connectedWifi.connection)
                                    : qsTr("Not connected"))
                            }
                        }
                        AppSwitch {
                            label: qsTr("Wi-Fi")
                            on: backend.network.wifiEnabled
                            enabled: backend.network.status === "ok"
                            onClicked: win.setWifi(!on)
                        }
                    }
                    RowDivider {}
                }
                Item {
                    readonly property var bt: backend.bluetooth
                    readonly property bool adapter: !!backend.capabilities.bluetooth
                    width: parent.width
                    height: Math.max(btColumn.implicitHeight + 18, theme.rowHeight)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        ColumnLayout {
                            id: btColumn
                            Layout.fillWidth: true
                            spacing: 2
                            AppText { text: qsTr("Bluetooth"); Layout.fillWidth: true }
                            AppText {
                                Layout.fillWidth: true
                                visible: !parent.parent.parent.adapter
                                secondary: true
                                scale_: 0.88
                                text: qsTr("No Bluetooth adapter detected.")
                            }
                        }
                        AppSwitch {
                            label: qsTr("Bluetooth")
                            on: parent.parent.bt.powered
                            busy: parent.parent.bt.busy
                            enabled: parent.parent.adapter && !parent.parent.bt.busy && parent.parent.bt.status === "ok"
                            onClicked: parent.parent.bt.setPowered(!on)
                        }
                    }
                }
                footer: [
                    AppButton { style: "link"; text: qsTr("Open network panel"); iconName: "external"; onClicked: win.activate(backend.entry("panel.network")) }
                ]
            }
            QuickPanel {
                title: qsTr("Sound & display")
                icon: "sound"
                Item {
                    width: parent.width
                    height: theme.rowHeight + 1
                    Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 14; height: 1; color: theme.divider; opacity: 0.6 }
                    RowLayout {
                        anchors.fill: parent
                        anchors.bottomMargin: 1
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10
                        AppText { text: qsTr("Output volume"); Layout.fillWidth: true }
                        AppSlider {
                            id: volume
                            label: qsTr("Output volume")
                            Layout.preferredWidth: 150
                            from: 0
                            to: 100
                            stepSize: 1
                            enabled: backend.audio.defaultOutputVolume >= 0
                            value: Math.min(100, Math.max(0, backend.audio.defaultOutputVolume))
                            onPressedChanged: if (!pressed) win.setDefaultVolume(Math.round(value))
                            Keys.onReleased: function (event) {
                                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                                    win.setDefaultVolume(Math.round(value))
                            }
                        }
                        AppText {
                            Layout.minimumWidth: 50
                            horizontalAlignment: Text.AlignRight
                            text: backend.audio.defaultOutputVolume >= 0 ? Math.round(volume.value).toLocaleString(Qt.locale(), "f", 0) + "%" : "—"
                        }
                    }
                }
                SettingRow { width: parent.width; compact: true; entry: backend.entry("displays.brightness") }
                SettingRow { width: parent.width; compact: true; entry: backend.entry("displays.nightlight"); last: true }
                footer: [
                    AppButton { style: "link"; text: qsTr("Sound settings"); iconName: "arrow"; onClicked: win.go("sound", "") },
                    AppButton { style: "link"; text: qsTr("Display settings"); iconName: "arrow"; onClicked: win.go("displays", "") }
                ]
            }
            QuickPanel {
                title: qsTr("Power & session")
                icon: "power"
                SettingRow { width: parent.width; compact: true; entry: backend.entry("power.profile") }
                SettingRow { width: parent.width; compact: true; entry: backend.entry("power.idle.lock") }
                SettingRow { width: parent.width; compact: true; entry: backend.entry("power.stayawake"); last: true }
                footer: [
                    AppButton { style: "link"; text: qsTr("Power settings"); iconName: "arrow"; onClicked: win.go("power", "") }
                ]
            }
        }
        }

        // Pinned destinations
        ColumnLayout {
        visible: backend.pins.length > 0
        Layout.fillWidth: true
        spacing: 8
        BlockHeading { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; title: qsTr("Pinned") }
        AppPanel {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Repeater {
                model: backend.pins
                delegate: SettingRow {
                    width: parent ? parent.width : 400
                    entry: backend.entry(modelData)
                    last: index === backend.pins.length - 1
                    Component.onCompleted: backend.refresh(modelData)
                }
            }
        }
        }

        ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        BlockHeading { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; title: qsTr("Tools & maintenance") }
        AppPanel {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Repeater {
                model: [
                    { icon: "printer", title: qsTr("Printer settings"), detail: qsTr("Printers and print queues"), entry: "helper.printer" },
                    { icon: "disk", title: qsTr("Disks & snapshots"), detail: qsTr("Storage and recovery"), page: "system", focus: "block.storage" },
                    { icon: "download", title: qsTr("Updates"), detail: qsTr("Packages and firmware"), page: "system", focus: "update.omarchy" },
                    { icon: "list", title: qsTr("Startup & services"), detail: qsTr("What runs on your machine"), page: "system", focus: "block.startup" }
                ]
                delegate: AbstractButton {
                    id: tool
                    width: parent ? parent.width : 400
                    height: theme.rowHeight
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    // Says the same thing here as the row it leads to.
                    readonly property var reach: modelData.entry ? backend.availability(modelData.entry) : ({ available: true })
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.title
                    onClicked: modelData.entry ? win.activate(backend.entry(modelData.entry)) : win.go(modelData.page, modelData.focus)
                    background: Rectangle {
                        color: tool.hovered ? Qt.alpha(theme.foreground, 0.04) : "transparent"
                        border.width: tool.visualFocus ? 2 : 0
                        border.color: theme.accent
                        Rectangle {
                            visible: index < 3
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            height: 1
                            color: theme.divider
                            opacity: 0.6
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Item { Layout.preferredWidth: 2 }
                        AppIcon { name: modelData.icon; size: 18; tint: theme.muted }
                        AppText { text: modelData.title; opacity: tool.reach.available ? 1 : 0.6; Layout.preferredWidth: Math.min(240, tool.width * 0.33) }
                        AppText { text: tool.reach.available ? modelData.detail : tool.reach.reason; secondary: true; Layout.fillWidth: true }
                        AppIcon { name: "arrow"; tint: tool.hovered ? theme.accentText : theme.muted; mirror: tool.mirrored }
                        Item { Layout.preferredWidth: 4 }
                    }
                }
            }
        }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10
            AppText {
                Layout.fillWidth: true
                secondary: true
                text: root.history.length ? qsTr("Recent changes: %1").arg(qsTranslate("catalog", root.history[0].label)) : qsTr("Recent changes: none yet")
            }
            AppButton { visible: root.history.length > 0; style: "link"; text: qsTr("View history"); iconName: "arrow"; onClicked: win.go("advanced", "block.history") }
        }
        Item { Layout.preferredHeight: 12 }
    }
}
