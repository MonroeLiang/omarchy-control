import QtQuick
import QtQuick.Layouts

// What this app is: its mark, its name, the version running now, and what it is for.
AppPanel {
    id: root
    readonly property var updates: backend.updates
    // Side by side where there is room; the mark above the words where there is not.
    readonly property bool roomy: width > theme.fontSize * 34
    readonly property int tile: Math.round(theme.fontSize * (roomy ? 8.5 : 5.5))
    function takeFocus() {}

    Item {
        width: parent.width
        implicitHeight: layout.implicitHeight + (root.roomy ? 52 : 36)
        height: implicitHeight
        GridLayout {
            id: layout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root.roomy ? 36 : 20
            anchors.rightMargin: root.roomy ? 36 : 20
            columns: root.roomy ? 3 : 1
            columnSpacing: 28
            rowSpacing: 16
            Image {
                objectName: "aboutMark"
                source: "qrc:/omarchy-control.svg"
                sourceSize.width: root.tile * 2
                sourceSize.height: root.tile * 2
                Layout.preferredWidth: root.tile
                Layout.preferredHeight: root.tile
                Layout.alignment: Qt.AlignVCenter
                smooth: true
                Accessible.ignored: true
            }
            Rectangle {
                visible: root.roomy
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: -6
                Layout.bottomMargin: -6
                color: theme.divider
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: root.roomy ? 14 : 10
                Flow {
                    Layout.fillWidth: true
                    spacing: 18
                    AppText {
                        id: nameText
                        objectName: "aboutName"
                        // The product's name stays as it is in every language.
                        text: "Omarchy Control"
                        width: Math.min(implicitWidth, parent.width)
                        wrapMode: Text.Wrap
                        font.weight: Font.DemiBold
                        scale_: root.roomy ? 2.6 : 1.9
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    // The version running now, as the update provider reads it, centred on
                    // the name's line and wrapping below it when the name takes the width.
                    Item {
                        width: badge.width
                        height: Math.max(badge.height, nameText.height)
                        Rectangle {
                            id: badge
                            anchors.verticalCenter: parent.verticalCenter
                            width: versionText.implicitWidth + 30
                            height: versionText.implicitHeight + 14
                            radius: theme.controlRadius
                            color: Qt.alpha(theme.foreground, 0.05)
                            border.width: 1
                            border.color: theme.divider
                            AppText {
                                id: versionText
                                objectName: "aboutVersion"
                                anchors.centerIn: parent
                                text: qsTr("Version %1").arg(root.updates.currentVersion)
                                scale_: 1.05
                            }
                        }
                    }
                }
                AppText {
                    Layout.fillWidth: true
                    Layout.maximumWidth: theme.fontSize * 62
                    text: qsTr("One searchable place to understand this computer, change its settings, and reach the right tool.")
                    wrapMode: Text.Wrap
                    scale_: root.roomy ? 1.12 : 1
                }
            }
        }
    }
}
