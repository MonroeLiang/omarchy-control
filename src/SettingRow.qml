import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One row of the settings list, for every type in the catalog. It shows the
// effective value as last read, why it cannot be edited if it cannot, and keeps
// file paths and commands behind an expandable details line.
Item {
    id: root
    property var entry: ({})
    property bool compact: false
    property bool last: false
    property bool highlighted: false

    readonly property string entryId: entry.id || ""
    readonly property var st: backend.states[backend.stateKey(entryId)] || ({})
    readonly property string status: st.status || "loading"
    readonly property bool busy: !!st.busy
    readonly property bool unavailable: status === "unavailable"
    readonly property string kind: entry.type || "status"
    readonly property bool editable: !!entry.editable && !unavailable && status !== "custom" && !busy
    property bool showDetails: false
    readonly property bool pinned: backend.pins.indexOf(entryId) >= 0
    readonly property bool hasDetails: (entry.details || []).length > 0
    readonly property bool actionsRevealed: hover.hovered || pinButton.activeFocus || detailsButton.activeFocus

    implicitHeight: body.implicitHeight + 18 + (last ? 0 : 1)
    width: parent ? parent.width : 400

    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: root.last ? 0 : 1
        color: root.highlighted ? Qt.alpha(theme.accent, 0.14) : hover.hovered ? Qt.alpha(theme.foreground, 0.03) : "transparent"
        Behavior on color { enabled: theme.animate; ColorAnimation { duration: 200 } }
    }
    HoverHandler { id: hover }
    Rectangle {
        visible: !root.last
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        height: 1
        color: theme.divider
        opacity: 0.6
    }

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 3

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                spacing: 2
                AppText {
                    Layout.fillWidth: true
                    text: qsTranslate("catalog", root.entry.label || "")
                    opacity: root.unavailable ? 0.6 : 1
                }
                AppText {
                    Layout.fillWidth: true
                    visible: !root.compact && !!root.entry.description
                    text: qsTranslate("catalog", root.entry.description || "")
                    secondary: true
                    scale_: 0.88
                    wrapMode: Text.Wrap
                }
            }

            AppText {
                visible: root.status === "loading" || !!root.st.loading && root.kind === "status"
                text: "…"
                secondary: true
                Accessible.name: qsTr("Loading")
            }
            // Control
            Loader {
                id: control
                objectName: "settingControl"
                Layout.alignment: Qt.AlignVCenter
                active: root.kind !== "action" && !root.unavailable
                sourceComponent: {
                    switch (root.kind) {
                    case "toggle": return toggleControl
                    case "choice": return choiceControl
                    case "int": return stepperControl
                    case "slider": return sliderControl
                    case "float": return sliderControl
                    case "text": return textControl
                    default: return statusControl
                    }
                }
            }
            AppButton {
                visible: root.kind === "action"
                enabled: !root.unavailable
                text: qsTranslate("HelperLabels", root.actionVerb())
                iconName: root.actionIcon()
                style: root.entry.confirm === "destructive" ? "danger" : "outline"
                onClicked: win.activate(root.entry)
            }
            AppButton {
                visible: !root.compact && !!root.st.isOverride && !!root.entry.hasReset && root.kind !== "action" && !root.unavailable
                style: "flat"
                text: qsTr("Use default")
                enabled: !root.busy
                onClicked: backend.resetValue(root.entryId)
            }
            // These two always hold their space and only fade, so hovering a row moves
            // nothing and controls line up down the page. Staying in the layout also
            // keeps them reachable by Tab. A tap has no hover to reveal them, hence the
            // opacity check: a touch must not change something it cannot see.
            AppButton {
                id: pinButton
                objectName: "pinButton"
                visible: !root.compact
                style: "flat"
                text: ""
                implicitWidth: 28
                implicitHeight: 28
                iconName: "pin"
                opacity: root.pinned ? 1 : root.actionsRevealed ? 0.5 : 0
                Behavior on opacity { enabled: theme.animate; NumberAnimation { duration: 120 } }
                Accessible.name: root.pinned ? qsTr("Unpin from Overview") : qsTr("Pin to Overview")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                ToolTip.delay: 500
                onClicked: if (opacity > 0) backend.togglePin(root.entryId)
            }
            AppButton {
                id: detailsButton
                objectName: "detailsButton"
                visible: !root.compact
                enabled: root.hasDetails
                style: "flat"
                text: ""
                implicitWidth: 28
                implicitHeight: 28
                iconName: "info"
                opacity: root.hasDetails && (root.actionsRevealed || root.showDetails) ? 0.6 : 0
                Behavior on opacity { enabled: theme.animate; NumberAnimation { duration: 120 } }
                Accessible.name: qsTr("Technical details")
                Accessible.ignored: !root.hasDetails
                ToolTip.visible: hovered && root.hasDetails
                ToolTip.text: Accessible.name
                ToolTip.delay: 500
                onClicked: if (opacity > 0) root.showDetails = !root.showDetails
            }
        }

        // State that needs words: why it is unavailable, what it is set by, what failed.
        RowLayout {
            visible: warningText.text.length > 0
            Layout.fillWidth: true
            spacing: 6
            AppIcon {
                name: root.st.error ? "warning" : "info"
                tint: root.st.error ? theme.danger : theme.muted
                size: 14
            }
            AppText {
                id: warningText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                scale_: 0.88
                secondary: !root.st.error
                color: root.st.error ? theme.danger : theme.muted
                text: root.st.error || root.st.reason || root.st.note || ""
            }
        }
        AppText {
            // Only worth a line when it explains something: an inherited value is the
            // normal case, and saying so on every row is noise.
            visible: !root.compact && !!root.st.isOverride && root.entry.owned === true && root.status === "ok"
            text: root.st.source || ""
            secondary: true
            scale_: 0.82
        }
        Column {
            visible: root.showDetails
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: root.showDetails ? (root.entry.details || []) : []
                delegate: AppText {
                    width: parent.width
                    text: modelData
                    secondary: true
                    scale_: 0.82
                    wrapMode: Text.WrapAnywhere
                }
            }
            AppText {
                visible: !!root.entry.domain
                text: qsTr("Scope: %1 · Undo: %2").arg(root.entry.scope || "—").arg(root.entry.recovery || "—")
                secondary: true
                scale_: 0.82
            }
            Repeater {
                model: root.showDetails ? (root.entry.effects || []) : []
                delegate: AppText {
                    width: parent.width
                    text: qsTranslate("catalog", modelData)
                    secondary: true
                    scale_: 0.82
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    function actionVerb() {
        switch (root.entry.launchKind) {
        case "page": return "View"
        case "config": return "Edit"
        case "panel": return "Open"
        case "terminal": return "Run"
        case "url": return "Open"
        default: return "Open"
        }
    }
    function actionIcon() {
        return root.entry.launchKind === "page" ? "arrow" : "external"
    }

    Component {
        id: toggleControl
        AppSwitch {
            on: root.st.value === "true"
            enabled: root.editable
            busy: root.busy
            label: qsTranslate("catalog", root.entry.label || "")
            onClicked: win.requestChange(root.entryId, on ? "false" : "true")
        }
    }
    Component {
        id: choiceControl
        AppCombo {
            id: combo
            label: qsTranslate("catalog", root.entry.label || "")
            enabled: root.editable
            textRole: "label"
            valueRole: "value"
            implicitWidth: root.compact ? 180 : Math.min(320, Math.max(180, root.width * 0.32))
            readonly property string current: root.st.value === undefined ? "" : root.st.value
            // A value set outside the app may not be in the list; show it as it is
            // rather than pretending it is one of the choices.
            model: {
                var list = []
                var source = root.st.choices || []
                var found = false
                for (var i = 0; i < source.length; ++i) {
                    list.push({ value: source[i].value, label: qsTranslate("catalog", source[i].label) })
                    if (source[i].value === current)
                        found = true
                }
                if (!found && root.status !== "loading" && current.length)
                    list.unshift({ value: current, label: qsTr("Custom: %1").arg(current.replace(/\n/g, " ")) })
                return list
            }
            function sync() {
                for (var i = 0; i < model.length; ++i)
                    if (model[i].value === current) {
                        currentIndex = i
                        return
                    }
                currentIndex = -1
            }
            onModelChanged: sync()
            onCurrentChanged: sync()
            Connections {
                target: root
                function onStChanged() { combo.sync() }
            }
            onActivated: function (index) {
                var value = model[index].value
                if (value !== current)
                    win.requestChange(root.entryId, value)
            }
        }
    }
    Component {
        id: stepperControl
        RowLayout {
            spacing: 4
            property int pending: parseInt(root.st.value) || 0
            Timer {
                id: settle
                interval: 500
                onTriggered: win.requestChange(root.entryId, String(parent.pending))
            }
            AppButton {
                text: ""
                iconName: "minus"
                implicitWidth: 32
                enabled: root.editable && parent.pending > root.entry.min
                Accessible.name: qsTr("Decrease %1").arg(qsTranslate("catalog", root.entry.label || ""))
                onClicked: { parent.pending -= root.entry.step; settle.restart() }
            }
            AppText {
                Layout.minimumWidth: 60
                horizontalAlignment: Text.AlignHCenter
                text: parent.pending.toLocaleString(Qt.locale(), "f", 0) + (root.entry.unit ? " " + root.entry.unit : "")
            }
            AppButton {
                text: ""
                iconName: "plus"
                implicitWidth: 32
                enabled: root.editable && parent.pending < root.entry.max
                Accessible.name: qsTr("Increase %1").arg(qsTranslate("catalog", root.entry.label || ""))
                onClicked: { parent.pending += root.entry.step; settle.restart() }
            }
        }
    }
    Component {
        id: sliderControl
        RowLayout {
            spacing: 10
            AppSlider {
                id: slider
                label: qsTranslate("catalog", root.entry.label || "")
                Layout.preferredWidth: root.compact ? 150 : 200
                from: root.entry.min
                to: root.entry.max
                stepSize: root.entry.step
                snapMode: Slider.SnapAlways
                enabled: root.editable
                value: parseFloat(root.st.value) || 0
                onPressedChanged: if (!pressed) win.requestChange(root.entryId, String(root.kind === "float" ? Math.round(value * 1000) / 1000 : Math.round(value)))
                Keys.onReleased: function (event) {
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                        win.requestChange(root.entryId, String(root.kind === "float" ? Math.round(value * 1000) / 1000 : Math.round(value)))
                }
            }
            AppText {
                Layout.minimumWidth: 50
                horizontalAlignment: Text.AlignRight
                text: slider.value.toLocaleString(Qt.locale(), "f", root.kind === "float" ? 2 : 0) + (root.entry.unit ? " " + root.entry.unit : "")
            }
        }
    }
    Component {
        id: textControl
        AppField {
            label: qsTranslate("catalog", root.entry.label || "")
            implicitWidth: 240
            enabled: root.editable
            placeholderText: qsTranslate("catalog", root.entry.placeholder || "")
            // An unfinished edit stays put when the file changes underneath it.
            text: activeFocus ? text : (root.st.value || "")
            onEditingFinished: if (text !== (root.st.value || "")) win.requestChange(root.entryId, text)
        }
    }
    Component {
        id: statusControl
        AppText {
            text: root.status === "unknown" ? qsTr("Unknown") : (root.st.display || "")
            secondary: true
            horizontalAlignment: Text.AlignRight
            Layout.maximumWidth: root.width * 0.5
        }
    }
}
