import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every change made here, newest first, grouped by day, with undo where the engine says
// it is safe. A filter searches what each record says; the first look is short, and the
// rest is there on request. Nothing here decides what can be undone: the record does.
AppPanel {
    id: root
    readonly property var records: backend.history
    property string filter: ""
    property bool showAll: false
    readonly property int preview: 9
    // One undo at a time; the next waits for the engine to say how the last one went.
    property string pendingUndo: ""

    // An icon pack's stored value says how it was chosen, which no one needs to read.
    function shown(record, value) {
        if (record.settingId !== "appearance.icons")
            return value
        return value === "follow-theme" ? qsTranslate("catalog", "Follow desktop theme") : value.replace(/^pack:/, "")
    }
    function outcome(record) {
        switch (record.phase) {
        case "committed": return record.undoneBy ? qsTr("Undone") : qsTr("Done and confirmed")
        case "rolled-back": return qsTr("Failed, previous value restored")
        case "partial": return qsTr("Partly applied")
        case "failed": return qsTr("Not applied")
        case "interrupted": return qsTr("Interrupted; check the result")
        case "not-applied": return qsTr("Not applied")
        }
        return record.phase
    }
    // A tone and a mark for each outcome, so the color is never the only sign.
    function tone(record) {
        switch (record.phase) {
        case "committed": return record.undoneBy ? "muted" : "good"
        case "rolled-back":
        case "partial":
        case "interrupted": return "warning"
        case "failed":
        case "not-applied": return "danger"
        }
        return "muted"
    }
    function mark(record) {
        switch (tone(record)) {
        case "good": return "check"
        case "warning": return "warning"
        case "danger": return "close"
        }
        return record.undoneBy ? "undo" : "info"
    }
    function toneColor(name) {
        return name === "good" ? theme.goodText : name === "warning" ? theme.warningText : name === "danger" ? theme.dangerText : theme.muted
    }
    // The setting's own icon, or its page's, so a record reads like the place it came from.
    function iconFor(record) {
        if (record.settingId === "appearance.icons")
            return "layers"
        if (record.settingId === "appearance.theme")
            return "brush"
        var entry = backend.entry(record.settingId)
        if (entry.icon)
            return entry.icon
        var pages = backend.pages
        for (var i = 0; i < pages.length; ++i)
            if (pages[i].id === entry.page)
                return pages[i].icon
        return "history"
    }
    function searchable(record) {
        return [qsTranslate("catalog", record.label), record.label, shown(record, record.before || ""), shown(record, record.after || ""),
                record.message || "", outcome(record)].join(" ").toLowerCase()
    }
    readonly property var matching: {
        var needle = filter.trim().toLowerCase()
        if (!needle.length)
            return records
        return records.filter(function (record) { return root.searchable(record).indexOf(needle) >= 0 })
    }
    readonly property var visibleRecords: showAll ? matching : matching.slice(0, preview)
    // The day as the locale writes it, without the weekday.
    function day(date) {
        var format = Qt.locale().dateFormat(Locale.LongFormat).replace(/d{4}[,،.]?\s*/, "").replace(/^[\s,،.]+|[\s,،]+$/g, "")
        return date.toLocaleDateString(Qt.locale(), format)
    }
    function takeFocus() {
        if (field.visible)
            field.forceActiveFocus(Qt.TabFocusReason)
        else
            focusTarget.forceActiveFocus(Qt.TabFocusReason)
    }

    Connections {
        target: backend
        function onToast() { root.pendingUndo = "" }
    }
    Timer {
        // Undo always reports back; this only keeps a lost reply from locking the buttons.
        running: root.pendingUndo.length > 0
        interval: 120000
        onTriggered: root.pendingUndo = ""
    }

    Item {
        id: focusTarget
        width: parent.width
        implicitHeight: heading.implicitHeight + 30
        height: implicitHeight
        activeFocusOnTab: !field.visible
        Accessible.role: Accessible.Heading
        Accessible.name: qsTr("Change history")
        RowLayout {
            id: heading
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            AppIcon { name: "history"; size: Math.round(theme.fontSize * 1.75); tint: theme.foreground }
            AppText { Layout.fillWidth: true; text: qsTr("Change history"); font.weight: Font.DemiBold; scale_: 1.45; wrapMode: Text.Wrap }
            AppText { visible: root.records.length > 1; text: qsTr("Newest first"); secondary: true; scale_: 0.9 }
        }
    }
    Item {
        visible: root.records.length > 0
        width: parent.width
        height: visible ? field.implicitHeight + 8 : 0
        AppField {
            id: field
            objectName: "historyFilter"
            x: 16
            width: parent.width - 32
            label: qsTr("Filter changes")
            placeholderText: qsTr("Filter changes")
            onTextChanged: root.filter = text
            Keys.onEscapePressed: function (event) {
                if (text.length) {
                    text = ""
                    event.accepted = true
                } else {
                    event.accepted = false
                }
            }
        }
    }

    // First use: nothing to show yet, and what will be here.
    Item {
        objectName: "historyEmpty"
        visible: !root.records.length
        width: parent.width
        height: visible ? emptyColumn.implicitHeight + 40 : 0
        Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }
        ColumnLayout {
            id: emptyColumn
            x: 18
            width: parent.width - 36
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Nothing has been changed here yet.") }
            AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; secondary: true; scale_: 0.9; text: qsTr("Changes you make in this app appear here, with undo where it is safe.") }
        }
    }
    // A filter that finds nothing says so and offers the way back.
    Item {
        objectName: "historyNoMatch"
        visible: root.records.length > 0 && !root.matching.length
        width: parent.width
        height: visible ? noMatch.implicitHeight + 28 : 0
        RowLayout {
            id: noMatch
            x: 18
            width: parent.width - 36
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            AppText { Layout.fillWidth: true; wrapMode: Text.Wrap; secondary: true; text: qsTr("No change matches “%1”.").arg(root.filter.trim()) }
            AppButton {
                objectName: "historyClearFilter"
                text: qsTr("Clear filter")
                onClicked: {
                    field.text = ""
                    field.forceActiveFocus(Qt.TabFocusReason)
                }
            }
        }
    }

    Repeater {
        model: root.visibleRecords
        delegate: Column {
            id: entry
            objectName: "historyRecord"
            readonly property var record: modelData
            readonly property string recordId: record.id
            readonly property bool newDay: index === 0 || root.day(root.visibleRecords[index - 1].at) !== root.day(record.at)
            readonly property bool hasChange: (record.after || "").length > 0
            width: parent ? parent.width : 400
            // The day, above its first change.
            Item {
                visible: entry.newDay
                width: parent.width
                height: visible ? dayLabel.implicitHeight + (index === 0 ? 22 : 30) : 0
                AppText {
                    id: dayLabel
                    objectName: "historyDay"
                    x: 18
                    width: parent.width - 36
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    text: root.day(entry.record.at)
                    font.weight: Font.DemiBold
                    scale_: 1.08
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
            }
            Rectangle { x: 16; width: parent.width - 32; height: 1; color: theme.hairline }
            Item {
                width: parent.width
                implicitHeight: Math.max(recordRow.implicitHeight + 18, theme.rowHeight)
                height: implicitHeight
                Accessible.role: Accessible.ListItem
                Accessible.name: [qsTranslate("catalog", entry.record.label), change.fullText, timeText.text, outcomeText.text, entry.record.message || ""]
                                 .filter(function (part) { return part.length > 0 }).join(", ")
                RowLayout {
                    id: recordRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 14
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        implicitWidth: Math.round(theme.fontSize * 3)
                        implicitHeight: implicitWidth
                        radius: theme.controlRadius
                        color: Qt.alpha(theme.foreground, 0.06)
                        border.width: 1
                        border.color: theme.hairline
                        AppIcon { anchors.centerIn: parent; name: root.iconFor(entry.record); size: Math.round(theme.fontSize * 1.5); tint: theme.foreground }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        AppText {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.weight: Font.Medium
                            text: qsTranslate("catalog", entry.record.label)
                        }
                        // Before, then after. Short values share a line; long ones, such as
                        // paths, get a line each that keeps their start and their end.
                        Item {
                            id: change
                            readonly property string before: root.shown(entry.record, entry.record.before || "")
                            readonly property string after: root.shown(entry.record, entry.record.after || "")
                            readonly property string fullText: before.length ? before + " → " + after : after
                            readonly property bool split: changeMetrics.advanceWidth(fullText) > width
                            visible: entry.hasChange
                            Layout.fillWidth: true
                            implicitHeight: split ? splitColumn.implicitHeight : inlineText.implicitHeight
                            FontMetrics { id: changeMetrics; font: inlineText.font }
                            HoverHandler { id: changeHover }
                            ToolTip.visible: changeHover.hovered && change.split
                            ToolTip.text: fullText
                            ToolTip.delay: 500
                            AppText {
                                id: inlineText
                                visible: !change.split
                                width: parent.width
                                text: change.fullText
                            }
                            Column {
                                id: splitColumn
                                visible: change.split
                                width: parent.width
                                AppText {
                                    visible: change.before.length > 0
                                    width: parent.width
                                    elide: Text.ElideMiddle
                                    text: change.before
                                }
                                AppText {
                                    width: parent.width
                                    elide: Text.ElideMiddle
                                    text: (change.before.length ? "→ " : "") + change.after
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            AppText { id: timeText; secondary: true; scale_: 0.88; text: entry.record.at.toLocaleTimeString(Qt.locale(), Locale.ShortFormat) + " ·" }
                            AppIcon { name: root.mark(entry.record); size: 13; tint: root.toneColor(root.tone(entry.record)) }
                            AppText {
                                id: outcomeText
                                objectName: "historyOutcome"
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                scale_: 0.88
                                color: root.tone(entry.record) === "good" ? theme.muted : root.toneColor(root.tone(entry.record))
                                text: root.outcome(entry.record)
                            }
                        }
                        AppText {
                            objectName: "historyMessage"
                            visible: text.length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            secondary: true
                            scale_: 0.85
                            text: entry.record.message || ""
                        }
                    }
                    AppButton {
                        objectName: "historyUndo"
                        Layout.alignment: Qt.AlignVCenter
                        visible: !!entry.record.undoable
                        enabled: !root.pendingUndo.length
                        busy: root.pendingUndo === entry.recordId
                        text: qsTr("Undo")
                        iconName: "undo"
                        Accessible.name: qsTr("Undo %1").arg(qsTranslate("catalog", entry.record.label))
                        onClicked: {
                            if (root.pendingUndo.length)
                                return
                            root.pendingUndo = entry.recordId
                            backend.undo(entry.recordId)
                        }
                    }
                }
            }
        }
    }

    // The rest, in place, and back to the first look.
    Item {
        visible: root.matching.length > root.preview
        width: parent.width
        height: visible ? showButton.implicitHeight + 28 : 0
        AbstractButton {
            id: showButton
            objectName: "historyShowAll"
            x: 16
            y: 12
            width: parent.width - 32
            implicitHeight: Math.max(Math.round(theme.fontSize * 2.9), showRow.implicitHeight + 16)
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: showLabel.text
            Accessible.expandable: true
            Accessible.expanded: root.showAll
            onClicked: root.showAll = !root.showAll
            background: Rectangle {
                radius: theme.controlRadius
                color: showButton.hovered ? Qt.alpha(theme.foreground, 0.06) : Qt.alpha(theme.foreground, 0.025)
                border.width: showButton.visualFocus ? 2 : 1
                border.color: showButton.visualFocus ? theme.accent : theme.hairline
            }
            contentItem: Item {
                RowLayout {
                    id: showRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 16
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    AppText {
                        id: showLabel
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.showAll ? qsTr("Show fewer changes") : qsTr("Show all history (%1)").arg(root.matching.length)
                    }
                    AppIcon {
                        name: "chevron"
                        size: 16
                        tint: theme.muted
                        rotation: root.showAll ? 180 : 0
                        Behavior on rotation { enabled: theme.animate; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }
    Item { width: parent.width; height: root.matching.length > root.preview ? 2 : 8 }
}
