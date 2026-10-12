// Port of PillView.swift — the HUD at the bottom of the screen.
import QtQuick
import QtQuick.Effects

Item {
    id: root
    width: 600
    height: 96
    Theme { id: theme }
    readonly property var th: theme

    readonly property string st: pill.state
    property bool hovering: dotArea.containsMouse

    function timerText(s) { return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0") }

    // The capsule; geometry morphs between states with a spring-like curve.
    Item {
        id: capsule
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        height: 48
        visible: root.st !== "hidden"
        opacity: root.st === "hidden" ? 0 : 1

        width: {
            switch (root.st) {
            case "idleDot": return 116
            case "listening": return pill.locked ? 268 : 200
            case "processing": return pill.slow ? 264 : 176
            case "success": return 48
            case "notice": return Math.min(560, noticeText.implicitWidth + 40)
            case "error": return Math.min(560, errorRow.implicitWidth + 32)
            }
            return 48
        }
        Behavior on width { NumberAnimation { duration: 380; easing.type: Easing.OutBack; easing.overshoot: 0.9 } }
        Behavior on opacity { NumberAnimation { duration: 160 } }

        onWidthChanged: reportRect()
        onVisibleChanged: reportRect()
        function reportRect() {
            if (!visible) { pill.setInteractiveRect(0, 0, 0, 0); return }
            var p = mapToItem(root, 0, 0)
            pill.setInteractiveRect(Math.round(p.x), Math.round(p.y), Math.round(width), Math.round(height))
        }
        Component.onCompleted: reportRect()

        // Surface (Linux fallback for Liquid Glass: solid surface + soft shadow).
        RectangularShadow {
            anchors.fill: surface
            radius: surface.radius
            blur: 12
            offset.y: 2
            color: Qt.rgba(0, 0, 0, 0.20)
        }
        Rectangle {
            id: surface
            anchors.centerIn: parent
            width: root.st === "idleDot" ? (root.hovering ? 116 : 40) : parent.width
            height: root.st === "idleDot" ? (root.hovering ? 34 : 8) : parent.height
            radius: height / 2
            color: root.st === "error" ? theme.errorContainer : theme.surface
            Behavior on width { NumberAnimation { duration: root.hovering ? 450 : 300; easing.type: root.hovering ? Easing.OutBack : Easing.OutCubic; easing.overshoot: 1.4 } }
            Behavior on height { NumberAnimation { duration: root.hovering ? 450 : 300; easing.type: root.hovering ? Easing.OutBack : Easing.OutCubic; easing.overshoot: 1.4 } }
            Behavior on color { ColorAnimation { duration: 160 } }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                visible: root.st === "idleDot"
                color: theme.inkVariant
                opacity: root.hovering ? 0.04 : 0.18
                Behavior on opacity { NumberAnimation { duration: 200 } }
            }
        }

        // ---- idle dot ("the invitation")
        Row {
            anchors.centerIn: parent
            spacing: 8
            visible: root.st === "idleDot"
            opacity: root.hovering ? 1 : 0
            scale: root.hovering ? 1 : 0.4
            Behavior on opacity { NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: 340; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }
            JIcon { name: "mic-on"; color: theme.gBlue; size: 16; anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("Dictate"); color: theme.ink; font: theme.label; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea {
            id: dotArea
            anchors.centerIn: parent
            width: 116; height: 48
            enabled: root.st === "idleDot"
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.dotTapped()
        }

        // ---- listening
        Row {
            anchors.centerIn: parent
            spacing: 12
            visible: root.st === "listening"
            JIcon {
                visible: pill.locked
                name: "object-locked"
                size: 14
                color: theme.inkVariant
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: pill.locked || pill.elapsed >= 10
                text: root.timerText(pill.elapsed)
                color: theme.inkVariant
                font: theme.font(12, Font.Medium)
                anchors.verticalCenter: parent.verticalCenter
            }
            Waveform {
                theme: root.th
                level: pill.level
                processing: false
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                visible: pill.locked
                width: 32; height: 32; radius: 16
                color: stopArea.containsMouse ? Qt.darker(theme.primary, 1.08) : theme.primary
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 2.5; color: theme.primaryInk }
                MouseArea {
                    id: stopArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: pill.stopTapped()
                }
            }
        }

        // ---- processing
        Row {
            anchors.centerIn: parent
            spacing: 12
            visible: root.st === "processing"
            Waveform { theme: root.th; processing: true; anchors.verticalCenter: parent.verticalCenter }
            Text {
                visible: pill.slow
                text: qsTr("Still working…")
                color: theme.inkVariant
                font: theme.label
                anchors.verticalCenter: parent.verticalCenter
            }
            // Cancel: stop waiting for this transcript and free Jot for the next one.
            Rectangle {
                width: 32; height: 32; radius: 16
                anchors.verticalCenter: parent.verticalCenter
                color: cancelArea.pressed ? Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.18)
                     : cancelArea.containsMouse ? Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.10)
                     : Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.06)
                scale: cancelArea.pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: 90 } }
                JIcon { anchors.centerIn: parent; name: "window-close"; size: 16; color: theme.inkVariant }
                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: pill.cancelTapped()
                }
            }
        }

        // ---- success
        Canvas {
            id: check
            anchors.centerIn: parent
            width: 20; height: 20
            visible: root.st === "success"
            scale: visible ? 1 : 0.6
            Behavior on scale { NumberAnimation { duration: 320; easing.type: Easing.OutBack } }
            onVisibleChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = theme.success
                ctx.lineWidth = 2.5
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(0.05 * width, height / 2 + 0.1 * height)
                ctx.lineTo(0.38 * width, height - 0.12 * height)
                ctx.lineTo(width - 0.02 * width, 0.15 * height)
                ctx.stroke()
            }
        }

        // ---- notice
        Text {
            id: noticeText
            anchors.centerIn: parent
            visible: root.st === "notice"
            text: pill.message
            color: theme.ink
            font: theme.label
        }

        // ---- error
        Row {
            id: errorRow
            anchors.centerIn: parent
            spacing: 8
            visible: root.st === "error"
            property real shake: 0
            transform: Translate { x: 4 * Math.sin(errorRow.shake * Math.PI * 3 * 2) }
            JIcon { name: "dialog-warning"; size: 16; color: theme.errorContainerInk; anchors.verticalCenter: parent.verticalCenter }
            Text { text: pill.message; color: theme.errorContainerInk; font: theme.label; anchors.verticalCenter: parent.verticalCenter }
            NumberAnimation on shake { id: shakeAnim; from: 0; to: 1; duration: 250; running: false; easing.type: Easing.OutQuad }
        }
    }

    // Words count under the success check.
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: capsule.top
        anchors.bottomMargin: 4
        visible: root.st === "success" && pill.words > 20
        text: qsTr("%n word(s)", "", pill.words)
        color: theme.inkVariant
        font: theme.labelSmall
    }

    Connections {
        target: pill
        function onChanged() {
            if (pill.state === "error") shakeAnim.restart()
            capsule.reportRect()
        }
    }
}
