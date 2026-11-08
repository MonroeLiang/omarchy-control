import QtQuick
import QtQuick.Layouts

// The one place to update this app. Checking is always a click: nothing here reaches the
// network until someone asks. Installing happens in a terminal, so pacman shows what it
// will change and sudo, not this app, asks for the password. Restarting starts the new
// version first and closes this one only once that worked.
AppPanel {
    id: root
    readonly property var updates: backend.updates
    readonly property string updateState: updates.state
    readonly property bool available: updateState === "available"
    function takeFocus() {
        var target = installButton.visible ? installButton : restartButton.visible ? restartButton : checkButton
        target.forceActiveFocus(Qt.TabFocusReason)
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
            AppIcon { name: "refresh"; size: Math.round(theme.fontSize * 1.75); tint: theme.foreground }
            AppText {
                Layout.fillWidth: true
                text: qsTr("Updates")
                font.weight: Font.DemiBold
                scale_: 1.45
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
    }
    Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }

    Item {
        width: parent.width
        implicitHeight: body.implicitHeight + 40
        height: implicitHeight
        ColumnLayout {
            id: body
            x: 24
            y: 20
            width: parent.width - 48
            spacing: 16

            // What is known now, in one sentence, with a mark where the news is good or bad.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AppIcon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    visible: root.updateState === "current" || root.updateState === "error" || root.updateState === "restart"
                    name: root.updateState === "error" ? "warning" : "check"
                    size: Math.round(theme.fontSize * 1.15)
                    tint: root.updateState === "error" ? theme.dangerText : theme.goodText
                }
                AppText {
                    objectName: "updateStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    scale_: 1.05
                    color: root.updateState === "error" ? theme.dangerText : theme.foreground
                    Accessible.role: root.updateState === "error" ? Accessible.AlertMessage : Accessible.StaticText
                    Accessible.name: text
                    text: {
                        switch (root.updateState) {
                        case "checking": return qsTr("Checking for updates…")
                        case "current": return qsTr("You have the latest version.")
                        case "none": return qsTr("No releases have been published yet.")
                        case "error": return root.updates.error
                        case "available": return qsTr("Version %1 is available. You have %2.").arg(root.updates.latestVersion).arg(root.updates.currentVersion)
                        case "restart": return qsTr("Version %1 is installed. Restart to start using it.").arg(root.updates.latestVersion)
                        default: return qsTr("Updates are only checked when you ask.")
                        }
                    }
                }
            }

            // The two versions side by side, when there is a choice between them.
            Rectangle {
                visible: root.available
                Layout.fillWidth: true
                implicitHeight: versions.implicitHeight + 4
                radius: theme.controlRadius
                color: "transparent"
                border.width: 1
                border.color: theme.hairline
                Column {
                    id: versions
                    width: parent.width
                    y: 2
                    Repeater {
                        model: [
                            { label: qsTr("Available"), value: root.updates.latestVersion, name: "updateAvailableVersion" },
                            { label: qsTr("Installed"), value: root.updates.currentVersion, name: "updateInstalledVersion" }
                        ]
                        delegate: Item {
                            width: versions.width
                            implicitHeight: Math.max(Math.round(theme.fontSize * 2.4), versionRow.implicitHeight + 10)
                            height: implicitHeight
                            Accessible.role: Accessible.StaticText
                            Accessible.name: modelData.label + ", " + modelData.value
                            Rectangle { visible: index === 0; anchors.bottom: parent.bottom; x: 12; width: parent.width - 24; height: 1; color: theme.hairline }
                            RowLayout {
                                id: versionRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                AppText { text: modelData.label; secondary: true }
                                AppText {
                                    objectName: modelData.name
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    wrapMode: Text.WrapAnywhere
                                    mono: true
                                    text: modelData.value
                                    color: index === 0 ? theme.accentText : theme.foreground
                                }
                            }
                        }
                    }
                }
            }

            // What the release says about itself, as plain text and only so much of it.
            Rectangle {
                visible: root.available && root.updates.notes.length > 0
                Layout.fillWidth: true
                implicitHeight: notesColumn.implicitHeight + 24
                radius: theme.controlRadius
                color: Qt.alpha(theme.foreground, 0.03)
                border.width: 1
                border.color: theme.hairline
                ColumnLayout {
                    id: notesColumn
                    x: 14
                    y: 12
                    width: parent.width - 28
                    spacing: 6
                    AppText { text: qsTr("Release notes"); font.weight: Font.Medium; scale_: 0.93 }
                    AppText {
                        objectName: "updateNotes"
                        Layout.fillWidth: true
                        text: root.updates.notes
                        textFormat: Text.PlainText
                        secondary: true
                        scale_: 0.88
                        wrapMode: Text.Wrap
                        maximumLineCount: 10
                        elide: Text.ElideRight
                    }
                }
            }

            // A release this computer cannot install, and why.
            RowLayout {
                visible: root.available && !root.updates.canInstall
                Layout.fillWidth: true
                spacing: 10
                AppIcon { Layout.alignment: Qt.AlignTop; Layout.topMargin: 2; name: "info"; size: Math.round(theme.fontSize * 1.1); tint: theme.warningText }
                AppText {
                    objectName: "updateNoPackage"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("This release has no package for this computer.")
                }
            }

            AppButton {
                id: installButton
                objectName: "installUpdate"
                visible: root.available && root.updates.canInstall
                Layout.fillWidth: true
                implicitHeight: Math.round(theme.fontSize * 3.8)
                textScale: 1.2
                // Once the terminal is open, finishing there is the next step; this only reopens it.
                style: root.updates.installStarted ? "outline" : "primary"
                leadingIcon: "download"
                text: qsTr("Install update")
                onClicked: root.updates.install()
            }
            RowLayout {
                visible: installButton.visible
                Layout.fillWidth: true
                spacing: 10
                AppIcon { Layout.alignment: Qt.AlignTop; Layout.topMargin: 2; name: root.updates.installStarted ? "terminal" : "info"; size: Math.round(theme.fontSize * 1.1); tint: root.updates.installStarted ? theme.accentText : theme.muted }
                AppText {
                    objectName: "updateInstallNote"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    scale_: 0.93
                    color: root.updates.installStarted ? theme.foreground : theme.muted
                    text: root.updates.installStarted
                          ? qsTr("Finish in the terminal that opened, then come back to this window.")
                          : qsTr("Installing opens a terminal, where pacman shows the change and asks for your password.")
                }
            }

            AppButton {
                id: restartButton
                objectName: "restartApp"
                visible: root.updateState === "restart"
                Layout.fillWidth: true
                implicitHeight: Math.round(theme.fontSize * 3.8)
                textScale: 1.2
                style: "primary"
                leadingIcon: "refresh"
                text: qsTr("Restart now")
                onClicked: if (root.updates.restart()) win.quitApp()
            }

            AppButton {
                id: checkButton
                objectName: "checkUpdates"
                visible: root.updateState !== "restart"
                enabled: root.updateState !== "checking"
                Layout.fillWidth: true
                implicitHeight: Math.round(theme.fontSize * (root.available ? 2.6 : 3.8))
                textScale: root.available ? 1 : 1.2
                // The next step, unless a newer version is already waiting.
                style: root.available ? "outline" : "primary"
                leadingIcon: "refresh"
                spinning: root.updateState === "checking"
                text: qsTr("Check for updates")
                Accessible.description: root.updateState === "checking" ? qsTr("Checking for updates…") : ""
                onClicked: root.updates.check()
            }
        }
    }
}
