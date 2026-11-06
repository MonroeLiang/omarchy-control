import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 1240
    height: 800
    minimumWidth: 560
    minimumHeight: 460
    visible: true
    title: qsTr("Omarchy Control")
    color: theme.window
    font.family: "monospace"
    font.pixelSize: theme.fontSize

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.primary: theme.accent
    Material.background: theme.window
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    property string currentPage: "overview"
    property string focusRequest: ""
    property var pageHistory: []
    property bool helpVisible: false
    readonly property bool narrow: width < theme.fontSize * 61
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")

    // ---- Shared actions, used by every page -----------------------------------
    function go(page, focus) {
        if (page !== currentPage) {
            pageHistory = pageHistory.concat([currentPage])
            currentPage = page
        }
        focusRequest = focus || ""
        searchField.text = ""
    }
    // Opening a result only navigates, even for an action: it lands on the row, and
    // running it stays one deliberate step further.
    function openResult(row) {
        if (row.launchKind === "page")
            go(row.linkPage, row.linkFocus)
        else
            go(row.page, row.id)
        searchField.focus = false
    }
    function back() {
        if (!pageHistory.length)
            return
        var list = pageHistory.slice()
        currentPage = list.pop()
        pageHistory = list
        focusRequest = ""
    }
    function requestChange(id, value) {
        var plan = backend.planChange(id, value)
        if (!plan.ok) {
            toast.show(plan.error, "error")
            backend.refresh(id)
            return
        }
        if (plan.confirmText.length) {
            confirm.open_(qsTranslate("catalog", backend.entry(id).label), qsTranslate("catalog", plan.confirmText), plan.effects, qsTr("Change"), false, function () {
                backend.commit(id, value)
            }, function () {
                backend.refresh(id)
            })
            return
        }
        backend.commit(id, value)
    }
    // The first thing to use the owned Lua file asks before adding its one line. Only
    // typed settings go through a plan, so displays and shortcuts ask here.
    function withOwnership(proceed) {
        if (backend.capabilities["hypr.import"]) {
            proceed()
            return
        }
        confirm.open_(qsTr("Save to your Hyprland config"), qsTr("To save this, Omarchy Control adds one marked line to hyprland.lua and keeps your choices in omarchy-control.lua. It loads after your own files and before Omarchy's toggles. Nothing else in your config changes."), [], qsTr("Continue"), false, proceed)
    }
    function activate(entry) {
        if (entry.confirm === "confirm" || entry.confirm === "destructive") {
            confirm.open_(qsTranslate("catalog", entry.label), qsTranslate("catalog", entry.confirmText || ""), [], entry.confirm === "destructive" ? qsTr("Continue") : qsTr("Open"), entry.confirm === "destructive", function () {
                backend.launch(entry.id)
            })
            return
        }
        backend.launch(entry.id)
    }
    function setWifi(on) {
        if (!on) {
            confirm.open_(qsTr("Turn off Wi-Fi"), qsTr("If Wi-Fi is your only connection, this computer goes offline."), [], qsTr("Turn off"), false, function () {
                backend.network.setWifiEnabled(false)
            })
            return
        }
        backend.network.setWifiEnabled(true)
    }
    function setDefaultVolume(percent) {
        var outputs = backend.audio.outputs
        for (var i = 0; i < outputs.length; ++i)
            if (outputs[i].isDefault)
                backend.audio.setVolume(outputs[i].name, percent)
    }
    function formatBytes(bytes) {
        var units = ["B", "KB", "MB", "GB", "TB", "PB"]
        var value = bytes
        var unit = 0
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024
            ++unit
        }
        return value.toLocaleString(Qt.locale(), "f", unit > 2 ? 1 : 0) + " " + units[unit]
    }

    onCurrentPageChanged: backend.pageShown(currentPage)
    onActiveChanged: if (active) { backend.windowActivated(); theme.refreshRounding() }
    Component.onCompleted: {
        if (initialPage.length)
            currentPage = initialPage
        if (initialSetting.length) {
            var target = backend.entry(initialSetting)
            if (target.page)
                go(target.page, initialSetting)
        }
        backend.pageShown(currentPage)
        theme.refreshRounding()
    }

    Connections {
        target: backend
        function onToast(text, kind) { toast.show(text, kind) }
        function onNavigate(page, focus) { win.go(page, focus) }
        function onRaiseRequested(page, focus) {
            win.raise()
            win.requestActivate()
            if (focus.length) {
                var target = backend.entry(focus)
                if (target.page)
                    win.go(target.page, focus)
            } else if (page.length) {
                win.go(page, "")
            }
        }
    }

    // ---- Keys: single letters switch off while typing --------------------------
    Shortcut { sequences: ["Ctrl+K", "Ctrl+F"]; context: Qt.ApplicationShortcut; onActivated: { searchField.forceActiveFocus(); searchField.selectAll() } }
    Shortcut { sequence: "Alt+Left"; context: Qt.ApplicationShortcut; onActivated: win.back() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: win.helpVisible || searchField.text.length > 0 || confirm.visible
        onActivated: {
            if (confirm.visible)
                confirm.close()
            else if (win.helpVisible)
                win.helpVisible = false
            else
                searchField.text = ""
        }
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: {
            var rows = backend.history
            for (var i = 0; i < rows.length; ++i)
                if (rows[i].undoable) {
                    backend.undo(rows[i].id)
                    return
                }
            toast.show(qsTr("Nothing to undo"), "ok")
        }
    }

    Shortcut {
        sequences: [StandardKey.Redo]
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: {
            // Redo is undoing the most recent undo.
            var rows = backend.history
            for (var i = 0; i < rows.length; ++i)
                if (rows[i].kind === "undo" && rows[i].undoable) {
                    backend.undo(rows[i].id)
                    return
                }
            toast.show(qsTr("Nothing to redo"), "ok")
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: shell
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Toolbar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(theme.fontSize * 3.6)
                color: theme.sidebar
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 16
                    spacing: 16
                    AppIcon { name: "logo"; tint: theme.accent; size: 22 }
                    AppText {
                        visible: !win.narrow
                        text: qsTr("Omarchy Control")
                        font.bold: true
                        scale_: 1.2
                    }
                    AppField {
                        id: searchField
                        label: qsTr("Search settings, devices, and tools")
                        Layout.fillWidth: true
                        leftPadding: 38
                        rightPadding: 76
                        placeholderText: qsTr("Search settings, devices, and tools")
                        Keys.onDownPressed: results.currentIndex = Math.min(results.currentIndex + 1, results.count - 1)
                        Keys.onUpPressed: results.currentIndex = Math.max(results.currentIndex - 1, 0)
                        Keys.onReturnPressed: results.openCurrent()
                        Keys.onEnterPressed: results.openCurrent()
                        AppIcon { x: searchField.mirrored ? searchField.width - width - 12 : 12; anchors.verticalCenter: parent.verticalCenter; name: "search"; tint: theme.muted }
                        Rectangle {
                            x: searchField.mirrored ? 6 : searchField.width - width - 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: keyHint.implicitWidth + 14
                            height: parent.height - 12
                            radius: Math.min(4, theme.radius)
                            color: theme.raised
                            visible: !searchField.activeFocus
                            AppText { id: keyHint; anchors.centerIn: parent; text: "Ctrl K"; secondary: true; scale_: 0.82 }
                        }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.divider }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Sidebar
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: win.narrow ? 60 : Math.max(250, theme.fontSize * 21)
                    color: theme.sidebar
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2
                        ListView {
                            id: navigation
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: backend.pages
                            spacing: 2
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            keyNavigationEnabled: true
                            delegate: AbstractButton {
                                id: navButton
                                width: navigation.width
                                height: Math.round(theme.fontSize * 2.9)
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                readonly property bool selected: win.currentPage === modelData.id
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTranslate("catalog", modelData.label)
                                Accessible.selected: selected
                                onClicked: win.go(modelData.id, "")
                                ToolTip.visible: win.narrow && hovered
                                ToolTip.text: qsTranslate("catalog", modelData.label)
                                background: Rectangle {
                                    radius: Math.min(4, theme.radius)
                                    color: navButton.selected ? Qt.alpha(theme.accent, 0.2) : navButton.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                                    border.width: navButton.visualFocus ? 2 : 0
                                    border.color: theme.accent
                                    Rectangle {
                                        visible: navButton.selected
                                        width: 3
                                        height: parent.height
                                        color: theme.accent
                                        x: navButton.mirrored ? parent.width - width : 0
                                    }
                                }
                                contentItem: RowLayout {
                                    spacing: 12
                                    Item { Layout.preferredWidth: 4 }
                                    AppIcon {
                                        name: modelData.icon
                                        size: 20
                                        tint: navButton.selected ? theme.accent : theme.foreground
                                    }
                                    AppText {
                                        visible: !win.narrow
                                        Layout.fillWidth: true
                                        text: qsTranslate("catalog", modelData.label)
                                        color: navButton.selected ? theme.accent : theme.foreground
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: !win.narrow
                            Layout.fillWidth: true
                            Layout.margins: 6
                            AppText {
                                Layout.fillWidth: true
                                text: backend.omarchyVersion ? qsTr("Omarchy %1").arg(backend.omarchyVersion.split("-")[0]) : ""
                                secondary: true
                                scale_: 0.88
                            }
                            AppButton {
                                style: "flat"
                                text: qsTr("?  Shortcuts")
                                implicitHeight: 24
                                onClicked: win.helpVisible = true
                            }
                        }
                    }
                    Rectangle {
                        width: 1
                        height: parent.height
                        x: shell.LayoutMirroring.enabled ? 0 : parent.width - 1
                        color: theme.divider
                    }
                }

                // Content
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    Loader {
                        id: pageLoader
                        anchors.fill: parent
                        anchors.topMargin: 10
                        sourceComponent: win.currentPage === "overview" ? overviewComponent : pageComponent
                    }
                    Component { id: overviewComponent; OverviewPage {} }
                    Component {
                        id: pageComponent
                        GenericPage {
                            pageId: win.currentPage
                            page: {
                                var pages = backend.pages
                                for (var i = 0; i < pages.length; ++i)
                                    if (pages[i].id === win.currentPage)
                                        return pages[i]
                                return {}
                            }
                            focusId: win.focusRequest
                        }
                    }

                    // Search results replace the page while a query is present, and
                    // opening one only navigates: search never runs an action.
                    Rectangle {
                        anchors.fill: parent
                        visible: searchField.text.trim().length > 0
                        color: theme.window
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 8
                            AppText {
                                Layout.fillWidth: true
                                secondary: true
                                text: results.count ? qsTr("%n result(s). Press Enter to go to the first.", "", results.count) : qsTr("Nothing matches. Try different words.")
                            }
                            ListView {
                                id: results
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 2
                                currentIndex: 0
                                model: searchField.text.trim().length ? backend.search(searchField.text) : []
                                onModelChanged: currentIndex = 0
                                function openCurrent() {
                                    if (count > 0 && currentIndex >= 0 && currentIndex < model.length) {
                                        win.openResult(model[currentIndex])
                                    }
                                }
                                delegate: AbstractButton {
                                    id: hit
                                    width: results.width
                                    height: hitColumn.implicitHeight + 16
                                    hoverEnabled: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTranslate("catalog", modelData.label)
                                    onClicked: win.openResult(modelData)
                                    background: Rectangle {
                                        radius: Math.min(4, theme.radius)
                                        color: ListView.isCurrentItem || hit.ListView.isCurrentItem ? Qt.alpha(theme.accent, 0.18) : hit.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
                                    }
                                    contentItem: RowLayout {
                                        spacing: 12
                                        ColumnLayout {
                                            id: hitColumn
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 12
                                            spacing: 2
                                            AppText { Layout.fillWidth: true; text: qsTranslate("catalog", modelData.label) }
                                            AppText {
                                                Layout.fillWidth: true
                                                secondary: true
                                                scale_: 0.88
                                                text: modelData.available
                                                      ? qsTranslate("catalog", modelData.pageLabel) + " › " + qsTranslate("catalog", modelData.section) + (modelData.description ? " — " + qsTranslate("catalog", modelData.description) : "")
                                                      : modelData.reason
                                            }
                                        }
                                        AppText {
                                            Layout.rightMargin: 12
                                            secondary: true
                                            scale_: 0.82
                                            text: {
                                                switch (modelData.integration) {
                                                case "native": return qsTr("Setting")
                                                case "omarchy-panel": return qsTr("Omarchy panel")
                                                case "omarchy-wizard": return qsTr("Guided setup")
                                                case "external-helper": return qsTr("App")
                                                case "advanced-config": return qsTr("Config file")
                                                }
                                                return ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ConfirmDialog { id: confirm; objectName: "confirmDialog" }
        ShortcutsOverlay { visible: win.helpVisible; onCloseRequested: win.helpVisible = false }
        ToastBar { id: toast }
    }
}
