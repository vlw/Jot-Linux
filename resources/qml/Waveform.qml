// Port of WaveformView.swift — 5 bars, fast attack / slow release, per-bar shimmer.
import QtQuick

Item {
    id: root
    property real level: 0
    property bool processing: false
    property var theme
    readonly property var weights: [0.6, 0.88, 1.0, 0.8, 0.55]
    readonly property var phases: [0.0, 0.9, 1.7, 2.6, 3.4]
    property var current: [0, 0, 0, 0, 0]
    property real t: 0
    implicitWidth: 46
    implicitHeight: 38
    width: 46
    height: 38

    Timer {
        interval: 33   // 30 fps: smooth for both modes
        running: root.visible
        repeat: true
        onTriggered: {
            root.t = Date.now() / 1000
            if (!root.processing) {
                var c = root.current.slice()
                for (var i = 0; i < 5; ++i) {
                    var shimmer = Math.sin(root.t * 2 * Math.PI * (2.4 + i * 0.55) + root.phases[i] * 2)
                    var target = Math.max(0, Math.min(1, root.level * root.weights[i] * (1 + shimmer * 0.35)))
                    var coeff = target > c[i] ? 0.38 : 0.09
                    c[i] = c[i] + (target - c[i]) * coeff
                }
                root.current = c
            }
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 4
        Repeater {
            model: 5
            Rectangle {
                width: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                height: {
                    var i = index
                    if (root.processing)
                        return 16 + Math.sin(root.t * 2 * Math.PI / 1.4 + root.phases[i]) * 6
                    var idle = Math.sin(root.t * 2 * Math.PI * 0.8 + root.phases[i]) * 2
                    return Math.max(6, Math.min(38, 6 + idle + root.current[i] * (38 - 6)))
                }
                color: {
                    if (!root.processing) return root.theme.gBlue
                    // Colours flow continuously through the Google quad instead of stepping.
                    var pos = ((index / 5 + root.t / 2.4) % 1) * 4
                    var a = root.theme.brandQuad[Math.floor(pos) % 4]
                    var b = root.theme.brandQuad[(Math.floor(pos) + 1) % 4]
                    var f = pos - Math.floor(pos)
                    f = f * f * (3 - 2 * f)
                    return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1)
                }
            }
        }
    }
}
