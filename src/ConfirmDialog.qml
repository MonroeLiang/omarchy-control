import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Consequences, in words, for the changes that warrant asking. Ordinary reversible
// changes never come through here.
Popup {
    id: root
    property string heading: ""
    property string body: ""
    property var effects: []
    property string acceptLabel: ""
    property bool danger: false
    property var onAccept: null
    property var onDecline: null
    property var opener: null

    function accept_() {
        var accept = root.onAccept
        root.onDecline = null
        root.close()
        if (accept)
            accept()
    }

    function open_(title, text, effectList, label, isDanger, accept, decline) {
        heading = title
        body = text
        effects = effectList || []
        acceptLabel = label
        danger = isDanger
        onAccept = accept
        onDecline = decline || null
        opener = win.activeFocusItem
        open()
        // A stray Space must never confirm something that cannot be taken back.
        if (isDanger)
            cancelButton.forceActiveFocus(Qt.TabFocusReason)
        else
            acceptButton.forceActiveFocus(Qt.TabFocusReason)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 40 : 520)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20
    Overlay.modal: Rectangle { color: Qt.alpha(theme.deep, 0.6) }
    background: Rectangle {
        radius: theme.radius
        color: theme.window
        border.width: 1
        border.color: root.danger ? theme.dangerText : theme.divider
    }
    onClosed: {
        if (onDecline)
            onDecline()
        onDecline = null
        if (opener)
            opener.forceActiveFocus()
        opener = null
    }

    contentItem: ColumnLayout {
        Accessible.role: Accessible.Dialog
        Accessible.name: root.heading
        spacing: 10
        AppText { Layout.fillWidth: true; text: root.heading; font.bold: true; scale_: 1.1; wrapMode: Text.Wrap; Accessible.role: Accessible.Heading }
        AppText { Layout.fillWidth: true; text: root.body; wrapMode: Text.Wrap; visible: text.length > 0 }
        Repeater {
            model: root.effects
            delegate: AppText { Layout.fillWidth: true; text: "• " + qsTranslate("catalog", modelData); secondary: true; wrapMode: Text.Wrap }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 8
            spacing: 8
            AppButton {
                id: cancelButton
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                id: acceptButton
                text: root.acceptLabel
                style: root.danger ? "danger" : "primary"
                onClicked: root.accept_()
            }
        }
    }
}
