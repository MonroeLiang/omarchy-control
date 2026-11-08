import QtQuick
import QtQuick.Dialogs

// Kept apart and loaded on demand, like the folder chooser beside it.
FileDialog {
    title: qsTr("Choose an icon pack archive")
    nameFilters: [qsTr("Icon pack archives (*.zip *.7z *.tar *.tar.gz *.tgz *.tar.xz *.txz *.tar.bz2 *.tbz *.tbz2 *.tar.zst *.tzst *.tar.lz4 *.tar.lzma)"), qsTr("All files (*)")]
    onAccepted: backend.iconImporter.choose(selectedFile)
}
