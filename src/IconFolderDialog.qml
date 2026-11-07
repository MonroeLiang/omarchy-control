import QtQuick
import QtQuick.Dialogs

// Kept apart and loaded on demand, so opening Appearance does not pull in a file chooser.
FolderDialog {
    title: qsTr("Choose an icon pack folder")
    onAccepted: backend.iconImporter.choose(selectedFolder)
}
