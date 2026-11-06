import QtQuick

Text {
    property bool secondary: false
    property real scale_: 1

    color: secondary ? theme.muted : theme.foreground
    font.family: "monospace"
    font.pixelSize: Math.round(theme.fontSize * scale_)
    wrapMode: Text.NoWrap
    elide: Text.ElideRight
    textFormat: Text.PlainText
    verticalAlignment: Text.AlignVCenter
    // Follows the layout rather than the text, so a Latin name in a right-to-left
    // window still lines up with the Arabic around it.
    horizontalAlignment: Text.AlignLeft
    linkColor: theme.accent
}
