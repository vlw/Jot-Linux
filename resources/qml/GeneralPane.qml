// General pane — dictation key, keyboard access, microphone, insertion, login.
import QtQuick
import QtQuick.Layouts

PaneScroll {
    id: pane
    property bool capturing: false
    property bool testing: false
    Component.onDestruction: if (testing) jot.stopMicTest()
    property int r: jot.rev

    Connections {
        target: jot
        function onHotkeyCaptured(code, name) {
            pane.capturing = false
            if (code >= 0) jot.setSetting("hotkeyCode", code)
        }
    }

    JSection {
        theme: pane.theme
        JRow {
            theme: pane.theme
            label: qsTr("Language")
            JPicker {
                theme: pane.theme
                options: [
                    { text: qsTr("System default"), value: "system" },
                    { text: "English", value: "en" },
                    { text: "Русский", value: "ru" },
                ]
                current: (pane.r, jot.setting("language"))
                onPicked: v => jot.setSetting("language", v)
            }
        }
    }

    JSection {
        theme: pane.theme
        footer: qsTr("Hold to talk. Tap Space while holding to go hands-free. Esc cancels.")
        JRow {
            theme: pane.theme
            label: qsTr("Dictation key")
            Keycap { text: (pane.r, pane.capturing ? qsTr("Press a key…") : jot.hotkeyName()); theme: pane.theme }
            JButton {
                theme: pane.theme
                small: true
                text: pane.capturing ? qsTr("Cancel") : qsTr("Change…")
                enabled: (pane.r, jot.hotkeyBackend() === "evdev")
                onClicked: {
                    if (pane.capturing) { jot.cancelHotkeyCapture(); pane.capturing = false }
                    else { pane.capturing = true; jot.captureHotkey() }
                }
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Double-tap to lock hands-free")
            JSwitch {
                theme: pane.theme
                checked: (pane.r, jot.setting("doubleTapLock"))
                onToggled: v => jot.setSetting("doubleTapLock", v)
            }
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("Keyboard access")
        footer: (pane.r, jot.hotkeyBackend() === "evdev")
            ? qsTr("Jot reads key presses from your keyboards directly, so any key works — including a bare modifier like Right Ctrl. Nothing is logged or stored.")
            : (jot.hotkeyBackend() === "portal"
               ? qsTr("Using a desktop shortcut. Change it in System Settings → Keyboard → Shortcuts → Jot. For a bare-modifier key, grant keyboard access below.")
               : qsTr("Jot can't see the dictation key yet. Grant keyboard access once with the command below, then press Check Again."))
        JRow {
            theme: pane.theme
            label: qsTr("Key source")
            JPicker {
                theme: pane.theme
                options: [
                    { text: qsTr("Automatic"), value: "auto" },
                    { text: qsTr("Keyboard devices (evdev)"), value: "evdev" },
                    { text: qsTr("Desktop shortcut (portal)"), value: "portal" },
                ]
                current: (pane.r, jot.setting("hotkeyBackend"))
                onPicked: v => jot.setSetting("hotkeyBackend", v)
            }
        }
        JRow {
            theme: pane.theme
            label: (pane.r, jot.evdevAvailable()) ? qsTr("Keyboard access granted") : qsTr("Keyboard access not granted")
            JButton { theme: pane.theme; small: true; text: qsTr("Check Again"); onClicked: jot.recheckKeyboardAccess() }
        }
        ColumnLayout {
            visible: (pane.r, !jot.evdevAvailable())
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 6
            Text { text: qsTr("Run once in a terminal:"); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 28
                    radius: 6
                    color: pane.theme.surfaceContainer
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: jot.permissionCommand()
                        font: pane.theme.code
                        color: pane.theme.ink
                        elide: Text.ElideMiddle
                    }
                }
                JButton { theme: pane.theme; small: true; text: qsTr("Copy"); doneText: qsTr("Copied"); onClicked: jot.copyText(jot.permissionCommand()) }
            }
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("Microphone")
        footer: ((pane.r, jot.setting("microphoneFollowsSystem"))
            ? qsTr("Follows the system default input — it changes when you plug in a headset.")
            : qsTr("Jot always records from this microphone. Headsets and the system default never reroute it, and there is no silent fallback if it's unplugged."))
            + "\n" + qsTr("System volume is the device's input level in PipeWire. Software gain boosts only Jot's recordings, with a soft limiter against clipping. Keep the level bar mostly green when you speak.")
        JRow {
            theme: pane.theme
            label: qsTr("Record from")
            JPicker {
                theme: pane.theme
                implicitWidth: 300
                options: {
                    pane.r
                    var list = jot.microphones().map(m => ({ text: m.name, value: m.id }))
                    list.push({ text: qsTr("Follow the system default input"), value: "__system__" })
                    return list
                }
                current: {
                    pane.r
                    if (jot.setting("microphoneFollowsSystem")) return "__system__"
                    return jot.setting("microphoneId")
                }
                onPicked: v => v === "__system__" ? jot.selectSystemMicrophone() : jot.selectMicrophone(v)
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Microphone volume (system)")
            JSlider {
                id: volume
                theme: pane.theme
                from: 0; to: 150; stepSize: 1
                enabled: (pane.r, jot.microphoneAvailable())
                Component.onCompleted: value = Math.max(0, jot.micSystemVolume())
                onMoved: volumeApply.restart()
                Timer { id: volumeApply; interval: 60; onTriggered: jot.setMicSystemVolume(Math.round(volume.value)) }
            }
            Text {
                text: Math.round(volume.value) + "%"
                font: pane.theme.body
                color: pane.theme.inkVariant
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Software gain")
            JSlider {
                id: gain
                theme: pane.theme
                from: 0; to: 24; stepSize: 1
                value: (pane.r, Number(jot.setting("micGainDb")))
                onMoved: jot.setSetting("micGainDb", value)
            }
            Text {
                text: qsTr("+%1 dB").arg(Math.round(gain.value))
                font: pane.theme.body
                color: pane.theme.inkVariant
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Input level")
            Rectangle {
                implicitWidth: 220
                implicitHeight: 8
                radius: 4
                color: Qt.rgba(pane.theme.inkVariant.r, pane.theme.inkVariant.g, pane.theme.inkVariant.b, 0.15)
                Rectangle {
                    width: parent.width * Math.min(1, jot.micTestLevel)
                    height: parent.height
                    radius: 4
                    color: jot.micTestLevel > 0.92 ? pane.theme.error : jot.micTestLevel > 0.18 ? pane.theme.gGreen : pane.theme.gBlue
                    Behavior on width { NumberAnimation { duration: 60 } }
                }
            }
            JButton {
                theme: pane.theme
                small: true
                text: pane.testing ? qsTr("Stop") : qsTr("Test microphone")
                enabled: (pane.r, jot.microphoneAvailable())
                onClicked: {
                    if (pane.testing) { jot.stopMicTest(); pane.testing = false }
                    else { jot.startMicTest(); pane.testing = true }
                }
            }
        }
        JRow {
            theme: pane.theme
            label: (pane.r, jot.microphoneAvailable()) ? qsTr("Connected") : qsTr("%1 is unavailable — reconnect it").arg(jot.selectedMicrophoneName())
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("Inserting text")
        footer: (pane.r, jot.pasteAvailable())
            ? qsTr("Every finished transcript is copied to the clipboard first, then pasted into the window you were dictating into. Terminals get Ctrl+Shift+V. Jot never pastes into a window that took focus while you spoke.")
            : qsTr("KWin hasn't granted Jot its paste keystroke yet, so transcripts land on the clipboard only. Run scripts/install.sh once and restart Jot.")
        JRow {
            theme: pane.theme
            label: qsTr("Paste with")
            JPicker {
                theme: pane.theme
                options: [
                    { text: qsTr("Automatic"), value: "auto" },
                    { text: "Ctrl+V", value: "ctrlv" },
                    { text: "Ctrl+Shift+V", value: "ctrlshiftv" },
                    { text: "Shift+Insert", value: "shiftinsert" },
                    { text: qsTr("Don't paste (clipboard only)"), value: "none" },
                ]
                current: (pane.r, jot.setting("pasteShortcut"))
                onPicked: v => jot.setSetting("pasteShortcut", v)
            }
        }
    }

    JSection {
        theme: pane.theme
        JRow {
            theme: pane.theme
            label: qsTr("Start Jot at login")
            JSwitch {
                id: loginSwitch
                theme: pane.theme
                checked: (pane.r, jot.launchAtLogin())
                onToggled: v => { if (v !== jot.launchAtLogin()) jot.setLaunchAtLogin(v) }
            }
        }
    }
    Item { Layout.preferredHeight: 12 }

    component Keycap: Rectangle {
        property alias text: capText.text
        property var theme
        implicitWidth: capText.implicitWidth + 14
        implicitHeight: capText.implicitHeight + 6
        radius: 5
        color: theme.surfaceContainer
        border.color: Qt.rgba(theme.outlineVariant.r, theme.outlineVariant.g, theme.outlineVariant.b, 0.6)
        Text { id: capText; anchors.centerIn: parent; font: parent.theme.code; color: parent.theme.ink }
    }
}
