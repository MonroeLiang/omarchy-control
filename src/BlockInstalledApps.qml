import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every installed app, closed behind its count until asked. Each can be opened, or given
// another name or icon, or hidden from the launcher; a package's own launcher is never
// edited, and every change is in History. Installing and removing live in the Software
// library.
AppPanel {
    id: root
    readonly property var launchers: backend.launchers
    readonly property var all: launchers.entries
    property bool expanded: false
    property bool showHidden: false
    property string filter: ""
    property string editing: ""
    readonly property var shownApps: all.filter(function (a) { return a.visible && !a.omarchyHidden })
    readonly property int hiddenCount: all.length - shownApps.length
    readonly property var visibleApps: (showHidden ? all : shownApps).filter(function (a) {
        return !filter.length || (a.name + " " + a.comment + " " + a.id).toLowerCase().indexOf(filter.toLowerCase()) >= 0
    })
    Component.onCompleted: launchers.reload()
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
                    AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Open an app, or change its name, icon, or place in the launcher."); secondary: true; scale_: 0.9 }
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
            RowLayout {
                x: 16
                width: parent.width - 32
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                AppField {
                    id: field
                    objectName: "installedAppsFilter"
                    Layout.fillWidth: true
                    label: qsTr("Filter apps")
                    placeholderText: qsTr("Filter apps")
                    onTextChanged: root.filter = text
                }
                AppSwitch {
                    objectName: "installedAppsShowHidden"
                    visible: root.hiddenCount > 0
                    on: root.showHidden
                    label: qsTr("Show hidden (%1)").arg(root.hiddenCount)
                    onClicked: root.showHidden = !root.showHidden
                }
            }
        }
        AppText {
            visible: root.expanded && !root.visibleApps.length
            x: 18
            width: parent.width - 36
            height: visible ? implicitHeight + 20 : 0
            wrapMode: Text.Wrap
            secondary: true
            text: root.all.length ? qsTr("No app matches that.") : qsTr("No apps found.")
        }
        Repeater {
            model: root.expanded ? root.visibleApps.slice(0, 200) : []
            delegate: Column {
                id: app
                objectName: "installedApp"
                width: parent ? parent.width : 400
                readonly property bool editing: root.editing === modelData.id
                Item {
                    width: parent.width
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
                            opacity: modelData.visible ? 1 : 0.5
                            Image {
                                id: appIcon
                                anchors.fill: parent
                                visible: status === Image.Ready
                                source: modelData.iconUrl || ""
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
                            AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: modelData.name; secondary: !modelData.visible }
                            AppText {
                                Layout.fillWidth: true
                                // What makes this launcher different, in words.
                                readonly property var notes: [
                                    modelData.outdated ? qsTr("Out of date") : "",
                                    modelData.kind === "orphan" ? qsTr("Its app was removed") : "",
                                    modelData.modified && !modelData.outdated && modelData.kind !== "orphan" ? qsTr("Changed") : "",
                                    !modelData.visible ? qsTr("Hidden") : "",
                                    modelData.omarchyHidden ? qsTr("Hidden by Omarchy") : ""
                                ].filter(function (n) { return n.length })
                                visible: text.length > 0
                                secondary: true
                                scale_: 0.88
                                wrapMode: Text.Wrap
                                text: notes.length ? notes.join(" · ") : modelData.comment
                                color: modelData.outdated ? theme.warningText : theme.muted
                            }
                        }
                        AppButton {
                            objectName: "editLauncher"
                            style: app.editing ? "primary" : "outline"
                            iconName: "edit"
                            text: qsTr("Edit")
                            Accessible.name: qsTr("Edit %1").arg(modelData.name)
                            onClicked: root.editing = app.editing ? "" : modelData.id
                        }
                        AppButton {
                            text: qsTr("Open")
                            iconName: "external"
                            visible: modelData.visible
                            Accessible.name: qsTr("Open %1").arg(modelData.name)
                            onClicked: backend.system.launchApp(modelData.id)
                        }
                    }
                }
                Loader {
                    width: parent.width
                    active: app.editing
                    visible: active
                    sourceComponent: LauncherEditorPanel {
                        entry: modelData
                        onDone: root.editing = ""
                    }
                }
            }
        }
        Item { width: parent.width; height: 6 }
    }
}
