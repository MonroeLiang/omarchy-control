import QtQuick
import QtQuick.Layouts

// Reported separately on purpose, so that a helper launch is never counted as if
// this app could edit the setting itself.
ColumnLayout {
    id: root
    spacing: 8
    readonly property var c: backend.system.coverage
    BlockHeading { Layout.fillWidth: true; title: qsTr("Coverage report") }
    AppPanel {
        Layout.fillWidth: true
        padding: 14
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 20
            rowSpacing: 4
            AppText { secondary: true; text: qsTr("Native settings and pages") }
            AppText { Layout.fillWidth: true; text: root.c.native }
            AppText { secondary: true; text: qsTr("Omarchy panels") }
            AppText { Layout.fillWidth: true; text: root.c.panels }
            AppText { secondary: true; text: qsTr("Guided setup routes") }
            AppText { Layout.fillWidth: true; text: root.c.wizards }
            AppText { secondary: true; text: qsTr("Specialist apps") }
            AppText { Layout.fillWidth: true; text: root.c.helpers }
            AppText { secondary: true; text: qsTr("Config files") }
            AppText { Layout.fillWidth: true; text: root.c.advanced }
            AppText { secondary: true; text: qsTr("Not applicable on this computer") }
            AppText { Layout.fillWidth: true; text: root.c.unavailable }
            AppText { secondary: true; text: qsTr("Recorded exclusions") }
            AppText { Layout.fillWidth: true; text: root.c.exclusions }
        }
    }
    AppText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        secondary: true
        scale_: 0.88
        text: qsTr("Run “omarchy-control coverage --live” after an Omarchy update to find anything new.")
    }
}
