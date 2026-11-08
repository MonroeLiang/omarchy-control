import QtQuick
import QtQuick.Dialogs

// Kept apart and loaded on demand, like the other choosers. The image is copied into this
// app's own folder, so the launcher keeps it if the original moves.
FileDialog {
    signal chosen(string path)
    title: qsTr("Choose an icon for this app")
    nameFilters: [qsTr("Icons (*.png *.svg *.xpm)")]
    onAccepted: {
        var path = backend.launchers.chooseIcon(selectedFile)
        if (path.length)
            chosen(path)
    }
}
