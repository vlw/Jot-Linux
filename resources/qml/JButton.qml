// Bordered / prominent / destructive / link / plain button with hover and press
// feedback. `doneText` (optional) briefly replaces the label with a check mark
// after a click — e.g. "Copied" — so the action visibly happened.
import QtQuick

Rectangle {
    id: root
    property var theme
    property string text: ""
    property string kind: "bordered"   // bordered | prominent | destructive | link | plain
    property bool small: false
    property string icon: ""
    property string doneText: ""
    property bool hovered: area.containsMouse
    readonly property bool pressed: area.pressed
    property bool showingDone: false
    signal clicked()

    readonly property bool flat: kind === "link" || kind === "plain"
    implicitWidth: flat ? content.implicitWidth + (kind === "plain" && text === "" ? 8 : 0)
                        : Math.max(content.implicitWidth, doneMeasure.implicitWidth) + (small ? 16 : 24)
    implicitHeight: flat ? Math.max(content.implicitHeight, kind === "plain" && text === "" ? 24 : 0) : (small ? 24 : 30)
    radius: kind === "prominent" ? height / 2 : 7
    opacity: enabled ? 1 : 0.45
    scale: area.pressed ? 0.95 : 1
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
    Behavior on color { ColorAnimation { duration: 90 } }

    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    color: {
        if (kind === "prominent")
            return area.pressed ? Qt.darker(theme.primary, 1.25) : area.containsMouse ? Qt.darker(theme.primary, 1.1) : theme.primary
        if (flat)
            return area.pressed ? tint(theme.ink, 0.14) : area.containsMouse && kind === "plain" ? tint(theme.ink, 0.07) : "transparent"
        if (showingDone) return tint(theme.success, 0.14)
        return area.pressed ? tint(theme.ink, 0.14) : area.containsMouse ? theme.surfaceContainer : theme.surface
    }
    border.width: kind === "bordered" || kind === "destructive" ? 1 : 0
    border.color: showingDone ? theme.success : area.pressed ? theme.outline : theme.outlineVariant

    Timer { id: doneTimer; interval: 1400; onTriggered: root.showingDone = false }

    Text { id: doneMeasure; visible: false; text: root.doneText; font: label.font }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 6
        JIcon {
            visible: root.showingDone || root.icon !== ""
            name: root.showingDone ? "dialog-ok-apply" : root.icon
            size: 16
            color: root.showingDone ? root.theme.success : label.color
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: label
            visible: text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: root.showingDone ? root.doneText : root.text
            font: root.small || root.kind === "link" ? root.theme.label : root.theme.body
            color: root.showingDone ? root.theme.success
                 : root.kind === "prominent" ? root.theme.primaryInk
                 : root.kind === "destructive" ? root.theme.error
                 : root.kind === "link" ? (area.pressed ? Qt.darker(root.theme.primary, 1.3) : root.theme.primary)
                 : root.kind === "plain" ? root.theme.inkVariant
                 : root.theme.ink
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (!root.enabled) return
            root.clicked()
            if (root.doneText !== "") { root.showingDone = true; doneTimer.restart() }
        }
    }
}
