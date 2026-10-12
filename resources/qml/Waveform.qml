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
        interval: root.processing ? 83 : 42   // 12 fps processing, 24 fps live
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
                    var sweep = (root.t % 1.2) / 1.2
                    var hue = (index / 5 + sweep) % 1
                    return root.theme.brandQuad[Math.floor(hue * 4) % 4]
                }
            }
        }
    }
}
