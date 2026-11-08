import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One app's launcher, being changed: its name, its icon, and whether the launcher shows it.
// Nothing is written until Save, which is one change in History that undo takes back.
Rectangle {
    id: root
    property var entry: ({})
    signal done()
    readonly property var launchers: backend.launchers
    property string icon: entry.icon || ""
    property string iconUrl: entry.iconUrl || ""
    property bool shown: !!entry.visible
    property bool picking: false
    property string query: ""
    readonly property int labelWidth: Math.round(theme.fontSize * 7)
    readonly property int cell: Math.round(theme.fontSize * 4.2)
    width: parent ? parent.width : 400
    implicitHeight: body.implicitHeight + 32
    color: Qt.alpha(theme.foreground, 0.03)

    function save() {
        var changes = {}
        var name = nameField.text.trim()
        if (name.length && name !== root.entry.name)
            changes.name = name
        if (root.icon !== (root.entry.icon || ""))
            changes.icon = root.icon
        if (root.shown !== !!root.entry.visible)
            changes.hidden = !root.shown
        if (Object.keys(changes).length)
            root.launchers.save(root.entry.id, changes)
        root.done()
    }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.hairline }

    ColumnLayout {
        id: body
        x: 24
        y: 16
        width: parent.width - 48
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            AppText { Layout.preferredWidth: root.labelWidth; text: qsTr("Name") }
            AppField {
                id: nameField
                objectName: "launcherName"
                Layout.fillWidth: true
                label: qsTr("Name")
                text: root.entry.name || ""
                Component.onCompleted: forceActiveFocus(Qt.TabFocusReason)
                Keys.onReturnPressed: root.save()
                Keys.onEscapePressed: root.done()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            AppText { Layout.preferredWidth: root.labelWidth; text: qsTr("Icon") }
            Rectangle {
                implicitWidth: Math.round(theme.fontSize * 2.8)
                implicitHeight: implicitWidth
                radius: theme.controlRadius
                color: theme.deep
                Image {
                    id: preview
                    anchors.fill: parent
                    anchors.margins: 5
                    source: root.iconUrl
                    visible: status === Image.Ready
                    sourceSize.width: width * 2
                    sourceSize.height: height * 2
                    fillMode: Image.PreserveAspectFit
                }
                AppIcon { anchors.centerIn: parent; visible: !preview.visible; name: "apps"; size: Math.round(theme.fontSize * 1.3); tint: theme.muted }
            }
            AppText {
                Layout.fillWidth: true
                text: root.icon
                elide: Text.ElideMiddle
                secondary: true
                mono: true
                scale_: 0.85
            }
            AppButton {
                objectName: "launcherIconFromPack"
                text: qsTr("From your pack…")
                style: root.picking ? "primary" : "outline"
                onClicked: root.picking = !root.picking
            }
            AppButton {
                text: qsTr("Image file…")
                onClicked: {
                    fileDialog.active = true
                    fileDialog.item.open()
                }
            }
        }

        // Every app icon the current pack has, to choose one by sight.
        ColumnLayout {
            visible: root.picking
            Layout.fillWidth: true
            spacing: 8
            AppField {
                objectName: "launcherIconSearch"
                Layout.fillWidth: true
                label: qsTr("Search icons")
                placeholderText: qsTr("Search icons")
                onTextChanged: root.query = text
            }
            GridView {
                id: grid
                objectName: "launcherIconGrid"
                Layout.fillWidth: true
                // As tall as its icons need, up to three rows, then it scrolls.
                readonly property int columns: Math.max(1, Math.floor(width / cellWidth))
                Layout.preferredHeight: Math.max(1, Math.min(3, Math.ceil(count / columns))) * root.cell
                clip: true
                cellWidth: root.cell
                cellHeight: root.cell
                model: root.picking ? root.launchers.packIcons(root.query, 300) : []
                ScrollBar.vertical: AppScrollBar {}
                delegate: AbstractButton {
                    width: grid.cellWidth
                    height: grid.cellHeight
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    Accessible.name: modelData.name
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.name
                    ToolTip.delay: 400
                    onClicked: {
                        root.icon = modelData.name
                        root.iconUrl = modelData.url
                        root.picking = false
                    }
                    background: Rectangle {
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: theme.controlRadius
                        color: parent.hovered ? Qt.alpha(theme.foreground, 0.06) : "transparent"
                        border.width: root.icon === modelData.name || parent.visualFocus ? 2 : 0
                        border.color: theme.accent
                    }
                    contentItem: Image {
                        source: modelData.url
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: root.cell * 1.4
                        sourceSize.height: root.cell * 1.4
                        anchors.margins: Math.round(root.cell * 0.2)
                    }
                    padding: Math.round(root.cell * 0.18)
                }
            }
            AppText {
                visible: grid.count === 0
                text: qsTr("No icons in your pack match that.")
                secondary: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            AppText { Layout.fillWidth: true; text: qsTr("Show in the launcher") }
            AppSwitch {
                objectName: "launcherShown"
                on: root.shown
                label: qsTr("Show in the launcher")
                onClicked: root.shown = !root.shown
            }
        }

        AppText {
            visible: !!root.entry.omarchyHidden
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            secondary: true
            scale_: 0.88
            text: qsTr("Omarchy hides this app from its launcher in any case.")
        }
        AppText {
            visible: root.entry.kind === "package"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            secondary: true
            scale_: 0.88
            text: qsTr("Saving keeps your changes in a copy of this app's launcher. The app and its package do not change.")
        }
        AppText {
            visible: !!root.entry.outdated
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            scale_: 0.88
            color: theme.warningText
            text: qsTr("An update changed this app's launcher. Update brings that in, with your changes made again.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                objectName: "saveLauncher"
                style: "primary"
                text: qsTr("Save")
                onClicked: root.save()
            }
            AppButton {
                style: "flat"
                text: qsTr("Cancel")
                onClicked: root.done()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "updateLauncher"
                visible: !!root.entry.outdated
                text: qsTr("Update")
                iconName: "refresh"
                onClicked: {
                    root.launchers.updateEntry(root.entry.id)
                    root.done()
                }
            }
            AppButton {
                objectName: "resetLauncher"
                visible: !!root.entry.modified
                text: qsTr("Reset")
                iconName: "undo"
                Accessible.description: qsTr("Puts the launcher back as its package made it")
                onClicked: {
                    root.launchers.resetEntry(root.entry.id)
                    root.done()
                }
            }
        }
    }

    Loader {
        id: fileDialog
        active: false
        source: "qrc:/LauncherIconDialog.qml"
    }
    Connections {
        target: fileDialog.item
        function onChosen(path) {
            root.icon = path
            root.iconUrl = "file://" + path
        }
    }
}
