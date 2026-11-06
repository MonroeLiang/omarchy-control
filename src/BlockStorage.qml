import QtQuick
import QtQuick.Layouts

// Disk space. System snapshots protect the system, not personal files, so this is
// never presented as a backup.
ColumnLayout {
    id: root
    spacing: 8
    readonly property var disks: backend.system.storage
    BlockHeading { Layout.fillWidth: true; title: qsTr("Storage") }
    AppText { visible: root.disks.length === 0; text: qsTr("No disks found."); secondary: true }
    AppPanel {
        Layout.fillWidth: true
        visible: root.disks.length > 0
        Repeater {
            model: root.disks
            delegate: Item {
                width: parent ? parent.width : 400
                height: 58
                RowDivider {}
                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        AppText { Layout.fillWidth: true; text: modelData.mount + " (" + modelData.fstype + ")" }
                        AppText {
                            secondary: true
                            text: qsTr("%1 free of %2").arg(win.formatBytes(modelData.available)).arg(win.formatBytes(modelData.size))
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 5
                        radius: 2
                        color: Qt.alpha(theme.foreground, 0.12)
                        Rectangle {
                            width: parent.width * modelData.percent / 100
                            height: parent.height
                            radius: 2
                            color: modelData.percent >= 90 ? theme.danger : theme.accent
                            x: mirrored ? parent.width - width : 0
                            readonly property bool mirrored: root.LayoutMirroring.enabled
                        }
                    }
                }
            }
        }
    }
    AppText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        secondary: true
        scale_: 0.88
        text: qsTr("System snapshots cover the system, not your personal files. Keep separate backups of your own data.")
    }
}
