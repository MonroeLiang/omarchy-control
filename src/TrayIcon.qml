import QtQuick
import Qt.labs.platform

// The app's mark in the bar's tray for as long as it runs, so a closed window is one
// click away. Kept apart and loaded on demand, like IconFolderDialog, so sandboxes and
// tests, which have no tray, never load the platform plugin.
SystemTrayIcon {
    id: tray
    signal openRequested()
    signal quitRequested()

    visible: true
    // The bar recolours a "-symbolic" icon to its own foreground, so the mark matches
    // every theme. A build that is not installed has no themed icon and shows the app's.
    icon.name: "omarchy-control-symbolic"
    icon.source: "qrc:/omarchy-control.svg"
    tooltip: qsTr("Omarchy Control")
    // A right click opens the menu below instead.
    onActivated: function (reason) {
        if (reason !== SystemTrayIcon.Context)
            tray.openRequested()
    }

    menu: Menu {
        MenuItem {
            text: qsTr("Open Omarchy Control")
            onTriggered: tray.openRequested()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Quit Omarchy Control")
            onTriggered: tray.quitRequested()
        }
    }
}
