// Port of WaveformView.swift — 5 bars, fast attack / slow release, per-bar shimmer.
//
// Drawn every display frame (FrameAnimation is vsync-driven: 165 Hz on a 165 Hz
// monitor, 60 Hz on a 60 Hz one). All motion is time-based, so the sparse mic
// level updates (every ~20 ms) are smoothed into continuous movement instead
// of stepping, and the look is identical at any refresh rate.
import QtQuick

Item {
    id: root
    property real level: 0
    property bool processing: false
    property var theme
    readonly property var weights: [0.6, 0.88, 1.0, 0.8, 0.55]
    readonly property var phases: [0.0, 0.9, 1.7, 2.6, 3.4]
    // Per-bar smoothed amplitude (0…1), updated every frame.
    property var current: [0, 0, 0, 0, 0]
    // Monotonic animation clock in seconds, advanced by the real frame time.
    property real t: 0
    implicitWidth: 46
    implicitHeight: 38
    width: 46
    height: 38

    // The original tuned its attack/release per 24 fps tick; convert to per-second
    // rates so the feel is the same at any frame rate.
    readonly property real attackPerTick: 0.38
    readonly property real releasePerTick: 0.09
    readonly property real tickRate: 24

    FrameAnimation {
        running: root.visible
        onTriggered: {
            // Clamp the step so a stall (window hidden, suspend) never makes the bars jump.
            const dt = Math.min(frameTime, 0.05)
            root.t += dt
            if (root.processing) return
            const c = root.current.slice()
            const ticks = dt * root.tickRate
            const attack = 1 - Math.pow(1 - root.attackPerTick, ticks)
            const release = 1 - Math.pow(1 - root.releasePerTick, ticks)
            for (let i = 0; i < 5; ++i) {
                const shimmer = Math.sin(root.t * 2 * Math.PI * (2.4 + i * 0.55) + root.phases[i] * 2)
                const target = Math.max(0, Math.min(1, root.level * root.weights[i] * (1 + shimmer * 0.35)))
                c[i] += (target - c[i]) * (target > c[i] ? attack : release)
            }
            root.current = c
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
                    const i = index
                    if (root.processing)
                        return 16 + Math.sin(root.t * 2 * Math.PI / 1.4 + root.phases[i]) * 6
                    const idle = Math.sin(root.t * 2 * Math.PI * 0.8 + root.phases[i]) * 2
                    return Math.max(6, Math.min(38, 6 + idle + root.current[i] * (38 - 6)))
                }
                color: {
                    if (!root.processing) return root.theme.gBlue
                    // Colours flow continuously through the Google quad.
                    const pos = ((index / 5 + root.t / 2.4) % 1) * 4
                    const a = root.theme.brandQuad[Math.floor(pos) % 4]
                    const b = root.theme.brandQuad[(Math.floor(pos) + 1) % 4]
                    let f = pos - Math.floor(pos)
                    f = f * f * (3 - 2 * f)
                    return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1)
                }
            }
        }
    }
}
