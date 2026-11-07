import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One widget on the bar: a handle to move it, and a list of where it can go. The handle
// takes the arrow keys as well as a drag, so reordering never needs a pointer.
Rectangle {
    id: root
    property string widgetId: ""
    property string widgetName: ""
    property string widgetIcon: "plugin"
    property color iconTint: theme.foreground
    property var actions: []          // { label, run } for the dropdown.
    property bool dragging: false
    signal keyMove(int key)
    signal dragStarted(point scenePoint)
    signal dragMoved(point scenePoint)
    signal dragEnded(bool commit)

    function focusHandle() {
        handle.forceActiveFocus(Qt.TabFocusReason)
    }

    objectName: "barWidgetChip"
    implicitHeight: Math.round(theme.fontSize * 2.2) + 2
    radius: theme.controlRadius
    color: hover.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"
    border.width: 1
    border.color: theme.divider
    opacity: dragging ? 0.35 : 1
    HoverHandler { id: hover }

    Item {
        id: handle
        objectName: "barWidgetHandle"
        width: Math.round(theme.fontSize * 1.6)
        height: parent.height
        x: mirrored ? parent.width - width : 0
        readonly property bool mirrored: combo.mirrored
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Reorder %1").arg(root.widgetName)
        Accessible.description: qsTr("Left and right arrows move it along its region. Up and down arrows move it to the next region.")
        Keys.onPressed: function (event) {
            if (mouse.dragActive && event.key === Qt.Key_Escape) {
                mouse.cancelDrag()
                event.accepted = true
                return
            }
            if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0 && event.modifiers === Qt.NoModifier) {
                root.keyMove(event.key)
                event.accepted = true
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Math.max(0, theme.controlRadius - 2)
            color: mouse.containsMouse ? Qt.alpha(theme.foreground, 0.08) : "transparent"
            border.width: handle.activeFocus ? 2 : 0
            border.color: theme.accent
        }
        AppIcon {
            anchors.centerIn: parent
            name: "grip"
            size: Math.round(theme.fontSize * 1.1)
            tint: theme.muted
        }
        MouseArea {
            id: mouse
            property bool dragActive: false
            property point pressedAt
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            ToolTip.visible: containsMouse && !pressed
            ToolTip.delay: 700
            ToolTip.text: qsTr("Drag to reorder, or use the arrow keys")
            function cancelDrag() {
                dragActive = false
                root.dragEnded(false)
            }
            onPressed: function (event) {
                handle.forceActiveFocus(Qt.MouseFocusReason)
                pressedAt = Qt.point(event.x, event.y)
            }
            onPositionChanged: function (event) {
                if (!pressed)
                    return
                const scene = mapToItem(null, event.x, event.y)
                if (!dragActive && Math.abs(event.x - pressedAt.x) + Math.abs(event.y - pressedAt.y) > 6) {
                    dragActive = true
                    root.dragStarted(scene)
                }
                if (dragActive)
                    root.dragMoved(scene)
            }
            onReleased: {
                if (!dragActive)
                    return
                dragActive = false
                root.dragEnded(true)
            }
            onCanceled: if (dragActive) cancelDrag()
        }
    }

    AppCombo {
        id: combo
        objectName: "barWidgetMenu"
        readonly property int handleWidth: handle.width
        x: mirrored ? 0 : handleWidth
        width: parent.width - handleWidth
        height: parent.height
        flush: true
        proportional: true
        leadingIcon: root.widgetIcon
        leadingTint: root.iconTint
        label: qsTr("Move %1").arg(root.widgetName)
        displayText: root.widgetName
        model: root.actions.map(function (action) { return action.label })
        currentIndex: -1
        hoverEnabled: true
        ToolTip.visible: hovered && contentItem.truncated
        ToolTip.delay: 500
        ToolTip.text: root.widgetName
        onActivated: function (i) {
            currentIndex = -1
            root.actions[i].run()
        }
    }
}
