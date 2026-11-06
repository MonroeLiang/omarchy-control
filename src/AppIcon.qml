import QtQuick

// A bundled line icon, tinted with any theme color. The wrapper fixes the size that
// layouts see, which would otherwise be the 2x source image.
Item {
    id: root
    property string name: ""
    property color tint: theme.foreground
    property int size: 18
    property bool mirror: false

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Image {
        anchors.fill: parent
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        smooth: true
        fillMode: Image.PreserveAspectFit
        mirror: root.mirror
        source: root.name.length ? "image://icons/" + root.name + "?color=" + root.tint.toString().replace("#", "") : ""
    }
}
