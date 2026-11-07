import QtQuick

// How many things are behind a closed heading, in a quiet pill. The number is also in
// the heading's accessible name, so this one is not read twice.
Rectangle {
    property int count: 0
    implicitHeight: Math.round(theme.fontSize * 1.55)
    implicitWidth: Math.max(implicitHeight, countLabel.implicitWidth + 16)
    radius: height / 2
    color: Qt.alpha(theme.foreground, 0.07)
    Accessible.ignored: true
    AppText {
        id: countLabel
        anchors.centerIn: parent
        text: parent.count.toLocaleString(Qt.locale(), "f", 0)
        scale_: 0.85
        mono: true
    }
}
