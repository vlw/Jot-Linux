// Port of PillView.swift — the HUD at the bottom of the screen.
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

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
        // Fade out fully before disappearing — a hard visibility cut read as a blink.
        visible: opacity > 0.001
        opacity: root.st === "hidden" ? 0 : 1

        // Size first, content second — the whole change takes 0.35 s:
        // width 210 ms, then the new state's content fades in over 140 ms.
        // Appearing from hidden snaps straight to the right size.
        property bool animateWidth: false
        property bool revealed: true
        // Explicitly driven, never by a binding-triggered Behavior: hide at once on a
        // state change, fade in only after the size has settled.
        property real contentOpacity: 1
        NumberAnimation { id: revealAnim; target: capsule; property: "contentOpacity"; to: 1; duration: 140; easing.type: Easing.InOutQuad }
        property string shownState: "hidden"
        property string contentState: "hidden"
        Timer { id: revealTimer; interval: 210; onTriggered: { capsule.revealed = true; revealAnim.restart() } }
        Connections {
            target: root
            function onStChanged() {
                // "Appearing from nothing" only when the pill is really invisible. A pill that
                // is still on screen (e.g. fading out after a short tap, when the hint comes
                // right after) gets the normal resize-then-reveal transition.
                const fromHidden = capsule.shownState === "hidden" && capsule.opacity < 0.05
                capsule.shownState = root.st
                if (root.st === "hidden") return          // keep the last content while fading out
                // Order matters: enable/disable the width animation and hide the content
                // BEFORE switching state, or the new size/content lands in one frame.
                capsule.animateWidth = !fromHidden
                revealAnim.stop()
                if (fromHidden) {
                    capsule.revealed = true
                    capsule.contentOpacity = 1
                } else {
                    capsule.revealed = false
                    capsule.contentOpacity = 0
                    revealTimer.restart()
                }
                capsule.contentState = root.st
            }
        }

        width: {
            switch (capsule.contentState) {
            case "idleDot": return 116
            case "listening": return pill.locked ? 268 : 200
            case "processing": return pill.slow ? 264 : 176
            case "success": return 48
            case "notice": return Math.min(560, noticeText.implicitWidth + 40)
            case "error": return Math.min(560, errorRow.implicitWidth + 32)
            }
            return 48
        }
        Behavior on width { enabled: capsule.animateWidth; NumberAnimation { duration: 210; easing.type: Easing.InOutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

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
            width: capsule.contentState === "idleDot" ? (root.hovering ? 116 : 40) : parent.width
            height: capsule.contentState === "idleDot" ? (root.hovering ? 34 : 8) : parent.height
            radius: height / 2
            color: capsule.contentState === "error" ? theme.errorContainer : theme.surface
            // Only the resting dot morphs on its own (hover); otherwise the surface tracks the capsule exactly.
            Behavior on width { enabled: capsule.contentState === "idleDot"; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on height { enabled: capsule.contentState === "idleDot"; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 160 } }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                visible: capsule.contentState === "idleDot"
                color: theme.inkVariant
                opacity: root.hovering ? 0.04 : 0.18
                Behavior on opacity { NumberAnimation { duration: 200 } }
            }
        }

        // ---- idle dot ("the invitation")
        Row {
            anchors.centerIn: parent
            spacing: 8
            visible: capsule.contentState === "idleDot"
            opacity: root.hovering ? 1 : 0
            scale: root.hovering ? 1 : 0.4
            Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            JIcon { name: "mic-on"; color: theme.gBlue; size: 16; anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("Dictate"); color: theme.ink; font: theme.label; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea {
            id: dotArea
            anchors.centerIn: parent
            width: 116; height: 48
            enabled: capsule.contentState === "idleDot"
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.dotTapped()
        }

        // ---- listening
        Row {
            anchors.centerIn: parent
            spacing: 12
            visible: capsule.contentState === "listening"
            opacity: capsule.contentOpacity
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
            visible: capsule.contentState === "processing"
            opacity: capsule.contentOpacity
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
        Shape {
            id: check
            anchors.centerIn: parent
            width: 20; height: 20
            visible: capsule.contentState === "success"
            opacity: capsule.contentOpacity
            scale: 0.8 + 0.2 * capsule.contentOpacity
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: theme.success
                strokeWidth: 2.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 1; startY: 12
                PathLine { x: 7.6; y: 17.6 }
                PathLine { x: 19.6; y: 3 }
            }
        }

        // ---- notice
        Text {
            id: noticeText
            anchors.centerIn: parent
            visible: capsule.contentState === "notice"
            opacity: capsule.contentOpacity
            text: pill.message
            color: theme.ink
            font: theme.label
        }

        // ---- error
        Row {
            id: errorRow
            anchors.centerIn: parent
            spacing: 8
            visible: capsule.contentState === "error"
            opacity: capsule.contentOpacity
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
        visible: capsule.contentState === "success" && pill.words > 20
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
