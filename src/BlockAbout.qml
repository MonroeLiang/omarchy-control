import QtQuick
import QtQuick.Layouts

// What this app is, and the one place to update it. Checking is always a click: nothing
// here reaches the network until someone asks. Installing happens in a terminal, so
// pacman shows what it will change and sudo, not this app, asks for the password.
ColumnLayout {
    id: root
    spacing: 22
    readonly property var updates: backend.updates
    readonly property string updateState: updates.state

    AppPanel {
        Layout.fillWidth: true
        padding: 16

        RowLayout {
            width: parent.width
            spacing: 16
            Image {
                source: "qrc:/omarchy-control.svg"
                sourceSize.width: 64
                sourceSize.height: 64
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                Layout.alignment: Qt.AlignTop
                Accessible.ignored: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                AppText {
                    Layout.fillWidth: true
                    text: "Omarchy Control"
                    font.weight: Font.DemiBold
                    scale_: 1.35
                }
                AppText {
                    Layout.fillWidth: true
                    objectName: "aboutVersion"
                    text: qsTr("Version %1").arg(root.updates.currentVersion)
                    secondary: true
                }
                AppText {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: qsTr("One searchable place to understand this computer, change its settings, and reach the right tool.")
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        BlockHeading { Layout.fillWidth: true; title: qsTr("Details") }
        AppPanel {
            Layout.fillWidth: true
            padding: 14
            Repeater {
                model: [
                    { label: qsTr("Omarchy"), value: backend.omarchyVersion ? backend.omarchyVersion.split("-")[0] : qsTr("Unknown") },
                    { label: qsTr("Qt"), value: root.updates.build.qt },
                    { label: qsTr("Architecture"), value: root.updates.build.architecture },
                    { label: qsTr("License"), value: "MIT" },
                    { label: qsTr("Copyright"), value: "© 2026 Jason Alexander" }
                ]
                delegate: RowLayout {
                    width: parent ? parent.width : 400
                    height: Math.round(theme.fontSize * 2)
                    spacing: 12
                    AppText { Layout.fillWidth: true; text: modelData.label }
                    AppText { text: modelData.value; secondary: true; mono: true }
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        BlockHeading { Layout.fillWidth: true; title: qsTr("Updates") }
        AppPanel {
            Layout.fillWidth: true
            padding: 14
            RowLayout {
                width: parent.width
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppText {
                        Layout.fillWidth: true
                        objectName: "updateStatus"
                        wrapMode: Text.Wrap
                        color: root.updateState === "error" ? theme.dangerText : theme.foreground
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
                    AppText {
                        Layout.fillWidth: true
                        visible: root.updateState === "available" && root.updates.notes.length > 0
                        text: root.updates.notes
                        textFormat: Text.PlainText
                        secondary: true
                        scale_: 0.88
                        wrapMode: Text.Wrap
                        maximumLineCount: 10
                        elide: Text.ElideRight
                    }
                    AppText {
                        Layout.fillWidth: true
                        visible: root.updateState === "available" && !root.updates.canInstall
                        text: qsTr("This release has no package for this computer.")
                        secondary: true
                        wrapMode: Text.Wrap
                    }
                    AppText {
                        Layout.fillWidth: true
                        visible: root.updateState === "available" && root.updates.canInstall
                        text: root.updates.installStarted
                              ? qsTr("Finish in the terminal that opened, then come back to this window.")
                              : qsTr("Installing opens a terminal, where pacman shows the change and asks for your password.")
                        secondary: true
                        scale_: 0.88
                        wrapMode: Text.Wrap
                    }
                }
                Row {
                    Layout.alignment: Qt.AlignTop
                    spacing: 8
                    AppButton {
                        objectName: "installUpdate"
                        visible: root.updateState === "available" && root.updates.canInstall
                        style: "primary"
                        text: qsTr("Install update")
                        onClicked: root.updates.install()
                    }
                    AppButton {
                        objectName: "restartApp"
                        visible: root.updateState === "restart"
                        style: "primary"
                        text: qsTr("Restart now")
                        onClicked: if (root.updates.restart()) Qt.quit()
                    }
                    AppButton {
                        objectName: "checkUpdates"
                        visible: root.updateState !== "restart"
                        enabled: root.updateState !== "checking"
                        text: qsTr("Check for updates")
                        onClicked: root.updates.check()
                    }
                }
            }
        }
    }
}
