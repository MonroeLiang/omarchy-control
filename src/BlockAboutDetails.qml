import QtQuick
import QtQuick.Layouts

// The facts someone reporting a problem is asked for, in the order they are asked.
AppPanel {
    id: root
    readonly property var updates: backend.updates
    readonly property var rows: [
        { label: qsTr("Omarchy"), value: backend.omarchyVersion ? backend.omarchyVersion.split("-")[0] : qsTr("Unknown") },
        { label: qsTr("Qt"), value: root.updates.build.qt || qsTr("Unknown") },
        { label: qsTr("Architecture"), value: root.updates.build.architecture || qsTr("Unknown") },
        { label: qsTr("License"), value: "MIT" },
        { label: qsTr("Copyright"), value: "© 2026 Jason Alexander" }
    ]
    function takeFocus() {}

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
            AppIcon { name: "info"; size: Math.round(theme.fontSize * 1.75); tint: theme.foreground }
            AppText {
                Layout.fillWidth: true
                text: qsTr("Details")
                font.weight: Font.DemiBold
                scale_: 1.45
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
    }
    Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }
    Repeater {
        model: root.rows
        delegate: Item {
            objectName: "aboutDetail"
            width: parent ? parent.width : 400
            implicitHeight: Math.max(Math.round(theme.fontSize * 2.85), detailRow.implicitHeight + 14)
            height: implicitHeight
            Accessible.role: Accessible.StaticText
            Accessible.name: modelData.label + ", " + modelData.value
            Rectangle {
                visible: index < root.rows.length - 1
                anchors.bottom: parent.bottom
                x: 16
                width: parent.width - 32
                height: 1
                color: theme.hairline
            }
            RowLayout {
                id: detailRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                spacing: 20
                AppText { Layout.alignment: Qt.AlignTop; text: modelData.label; scale_: 1.05 }
                AppText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.WrapAnywhere
                    text: modelData.value
                    mono: true
                    color: Qt.alpha(theme.foreground, 0.86)
                    scale_: 1.02
                }
            }
        }
    }
    Item { width: parent.width; height: 8 }
}
