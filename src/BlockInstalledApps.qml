import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every installed app, closed behind its count until asked. This only reads the desktop
// entries and opens apps; installing and removing live in the Software library.
AppPanel {
    id: root
    readonly property var system: backend.system
    readonly property var shownApps: system.apps.filter(function (a) { return !a.hidden })
    property bool expanded: false
    property string filter: ""
    readonly property var visibleApps: shownApps.filter(function (a) {
        return !filter.length || (a.name + " " + a.comment).toLowerCase().indexOf(filter.toLowerCase()) >= 0
    })
    // Search for Installed apps lands here, open and ready to type.
    function takeFocus() {
        expanded = true
        Qt.callLater(function () { field.forceActiveFocus(Qt.TabFocusReason) })
    }

    AbstractButton {
        id: header
        objectName: "installedAppsHeader"
        width: parent.width
        implicitHeight: headerRow.implicitHeight + 28
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Installed apps") + ", " + root.shownApps.length
        Accessible.expandable: true
        Accessible.expanded: root.expanded
        onClicked: root.expanded = !root.expanded
        background: Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: theme.controlRadius
            color: header.hovered ? Qt.alpha(theme.foreground, 0.04) : "transparent"
            border.width: header.visualFocus ? 2 : 0
            border.color: theme.accent
        }
        contentItem: Item {
            RowLayout {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                AppIcon { name: "apps"; size: Math.round(theme.fontSize * 1.75); tint: theme.foreground }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Installed apps"); font.weight: Font.DemiBold; scale_: 1.45 }
                    AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Browse and open installed applications."); secondary: true; scale_: 0.9 }
                }
                CountPill { count: root.shownApps.length }
                AppIcon {
                    name: "chevron"
                    size: 16
                    tint: theme.muted
                    rotation: root.expanded ? 180 : 0
                    Behavior on rotation { enabled: theme.animate; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }
        }
    }
    Column {
        visible: root.expanded
        width: parent.width
        Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }
        Item {
            width: parent.width
            height: field.implicitHeight + 24
            AppField {
                id: field
                objectName: "installedAppsFilter"
                x: 16
                width: parent.width - 32
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("Filter apps")
                placeholderText: qsTr("Filter apps")
                onTextChanged: root.filter = text
            }
        }
        AppText {
            visible: root.expanded && !root.visibleApps.length
            x: 18
            width: parent.width - 36
            height: visible ? implicitHeight + 20 : 0
            wrapMode: Text.Wrap
            secondary: true
            text: root.shownApps.length ? qsTr("No app matches that.") : qsTr("No apps found.")
        }
        Repeater {
            model: root.expanded ? root.visibleApps.slice(0, 200) : []
            delegate: Item {
                objectName: "installedApp"
                width: parent ? parent.width : 400
                implicitHeight: Math.max(theme.rowHeight, names.implicitHeight + 16)
                height: implicitHeight
                RowDivider {}
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
                        AppText { Layout.fillWidth: true; visible: text.length > 0; secondary: true; scale_: 0.88; wrapMode: Text.Wrap; text: modelData.comment }
                    }
                    AppButton {
                        text: qsTr("Open")
                        iconName: "external"
                        Accessible.name: qsTr("Open %1").arg(modelData.name)
                        onClicked: root.system.launchApp(modelData.id)
                    }
                }
            }
        }
        Item { width: parent.width; height: 6 }
    }
}
