import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What starts at login, with a switch each. A switch says whether an app starts at login,
// not whether it is running now. Filtering only narrows what is shown.
AppPanel {
    id: root
    readonly property var services: backend.services
    readonly property var apps: services.startupApps
    property string filter: ""
    property bool showAll: false
    // A first look at the list; the rest, or everything a filter matches, on request.
    readonly property int preview: 12
    readonly property var matching: apps.filter(function (a) {
        return !filter.length || (a.name + " " + (a.comment || "")).toLowerCase().indexOf(filter.toLowerCase()) >= 0
    })
    readonly property var shown: showAll || filter.length ? matching : matching.slice(0, preview)
    function takeFocus() {
        field.forceActiveFocus(Qt.TabFocusReason)
    }

    Item {
        width: parent.width
        implicitHeight: heading.implicitHeight + 30
        height: implicitHeight
        RowLayout {
            id: heading
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            AppIcon { name: "login"; size: Math.round(theme.fontSize * 1.75); tint: theme.foreground }
            AppText {
                Layout.fillWidth: true
                text: qsTr("Startup apps")
                font.weight: Font.DemiBold
                scale_: 1.45
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
    }
    Item {
        width: parent.width
        height: field.implicitHeight + 12
        AppField {
            id: field
            objectName: "startupFilter"
            x: 16
            width: parent.width - 32
            label: qsTr("Filter startup apps")
            placeholderText: qsTr("Filter startup apps")
            onTextChanged: root.filter = text
        }
    }
    Item {
        visible: !root.matching.length
        width: parent.width
        height: visible ? note1.implicitHeight + 24 : 0
        AppText {
            id: note1
            x: 18
            width: parent.width - 36
            anchors.verticalCenter: parent.verticalCenter
            wrapMode: Text.Wrap
            secondary: true
            text: root.apps.length ? qsTr("No startup app matches that.") : qsTr("No login items found.")
        }
    }
    Repeater {
        model: root.shown
        delegate: Item {
            objectName: "startupApp"
            readonly property string appId: modelData.id
            width: parent ? parent.width : 400
            implicitHeight: Math.max(theme.rowHeight, names.implicitHeight + 16)
            height: implicitHeight
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                height: 1
                color: theme.hairline
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 14
                spacing: 12
                Item {
                    implicitWidth: Math.round(theme.fontSize * 1.8)
                    implicitHeight: implicitWidth
                    Image {
                        id: appIcon
                        anchors.fill: parent
                        visible: status === Image.Ready
                        source: modelData.icon || ""
                        sourceSize.width: width * 2
                        sourceSize.height: height * 2
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    AppIcon { anchors.centerIn: parent; visible: !appIcon.visible; name: "apps"; size: Math.round(theme.fontSize * 1.3); tint: theme.muted }
                }
                ColumnLayout {
                    id: names
                    Layout.fillWidth: true
                    spacing: 1
                    AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: modelData.name }
                    AppText {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                        secondary: true
                        scale_: 0.88
                        // Whether this is your own entry or your change to the system's.
                        text: [modelData.comment || "",
                               modelData.source === "user" ? qsTr("Added by you") : modelData.source === "override" ? qsTr("Changed by you") : ""]
                              .filter(function (part) { return part.length > 0 }).join(" · ")
                    }
                }
                AppSwitch {
                    label: qsTr("Start %1 at login").arg(modelData.name)
                    on: modelData.enabled
                    onClicked: root.services.setStartupEnabled(modelData.id, !on)
                }
            }
        }
    }
    // The rest of the list, in place.
    Item {
        visible: !root.filter.length && root.matching.length > root.preview
        width: parent.width
        height: visible ? showButton.implicitHeight + 24 : 0
        AbstractButton {
            id: showButton
            objectName: "startupShowAll"
            x: 14
            y: 12
            width: parent.width - 28
            implicitHeight: Math.max(Math.round(theme.fontSize * 2.8), showRow.implicitHeight + 16)
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: showLabel.text
            Accessible.expandable: true
            Accessible.expanded: root.showAll
            onClicked: root.showAll = !root.showAll
            background: Rectangle {
                radius: theme.controlRadius
                color: showButton.hovered ? Qt.alpha(theme.foreground, 0.06) : Qt.alpha(theme.foreground, 0.025)
                border.width: showButton.visualFocus ? 2 : 1
                border.color: showButton.visualFocus ? theme.accent : theme.hairline
            }
            contentItem: Item {
                RowLayout {
                    id: showRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    AppIcon { name: "list"; size: Math.round(theme.fontSize * 1.2); tint: theme.foreground }
                    AppText {
                        id: showLabel
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.showAll ? qsTr("Show fewer startup apps") : qsTr("Show all startup apps (%1)").arg(root.matching.length)
                    }
                    AppIcon {
                        name: "chevron"
                        size: 16
                        tint: theme.muted
                        rotation: root.showAll ? 180 : 0
                        Behavior on rotation { enabled: theme.animate; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }
    Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }
    RowLayout {
        x: 16
        width: parent.width - 32
        height: implicitHeight + 24
        spacing: 12
        AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; secondary: true; scale_: 0.93; text: qsTr("Hyprland startup entries live in autostart.lua.") }
        AppButton { style: "link"; text: qsTr("Edit autostart.lua"); iconName: "external"; onClicked: win.activate(backend.entry("config.autostart")) }
    }
}
