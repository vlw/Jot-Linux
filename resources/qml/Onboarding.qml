// Port of OnboardingWindow.swift, adapted for Linux:
// screen 3 (Accessibility) → keyboard access; screen 4 (Globe key) → choose your key.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 640
    height: 560
    minimumWidth: 640
    maximumWidth: 640
    minimumHeight: 560
    maximumHeight: 560
    visible: true
    title: qsTr("Welcome to Jot")
    color: theme.windowBackground
    flags: Qt.Dialog

    Theme { id: theme }
    readonly property var th: theme
    property int screen: 0
    property var backStack: []
    property int r: jot.rev
    readonly property string key: (r, jot.hotkeyName())

    onClosing: jot.closeOnboarding()

    function advance() {
        var next = screen + 1
        backStack = backStack.concat([screen])
        screen = Math.min(7, next)
    }
    function back() {
        if (backStack.length === 0) return
        screen = backStack[backStack.length - 1]
        backStack = backStack.slice(0, -1)
    }
    Connections {
        target: jot
        function onOnboardingJump(n) { win.backStack = win.backStack.concat([win.screen]); win.screen = n }
    }
    Shortcut { sequence: "Ctrl+["; onActivated: win.back() }

    // Back button
    Rectangle {
        z: 2
        visible: win.backStack.length > 0
        x: 12; y: 12
        width: backRow.implicitWidth + 24; height: backRow.implicitHeight + 12
        radius: height / 2
        color: backArea.containsMouse ? theme.surfaceContainer : "transparent"
        Row { id: backRow; anchors.centerIn: parent; spacing: 4
            JIcon { name: "go-previous"; size: 16; color: theme.inkVariant; anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("Back"); font: theme.body; color: theme.inkVariant; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: backArea; anchors.fill: parent; hoverEnabled: true; onClicked: win.back() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item { Layout.preferredHeight: 28 }
        Loader {
            id: loader
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 480
            Layout.fillHeight: true
            sourceComponent: [welcome, apiKey, microphone, keyboard, chooseKey, howTo, tryIt, done][win.screen]
            onLoaded: { item.opacity = 0; item.x = 30; enter.restart() }
            ParallelAnimation {
                id: enter
                NumberAnimation { target: loader.item; property: "opacity"; to: 1; duration: 260 }
                NumberAnimation { target: loader.item; property: "x"; to: 0; duration: 380; easing.type: Easing.OutBack; easing.overshoot: 0.8 }
            }
        }
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 20
            spacing: 8
            Repeater {
                model: 8
                Rectangle { width: 6; height: 6; radius: 3; color: index === win.screen ? theme.primary : theme.outlineVariant }
            }
        }
    }

    // ---------------------------------------------------------------- pieces
    component Scaffold: ColumnLayout {
        property string headline
        property string body
        default property alias content: slot.data
        width: 480
        spacing: 20
        Text { text: parent.headline; font: theme.display; color: theme.ink; Layout.alignment: Qt.AlignHCenter; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Text { text: parent.body; font: theme.body; color: theme.inkVariant; Layout.alignment: Qt.AlignHCenter; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ColumnLayout { id: slot; Layout.alignment: Qt.AlignHCenter; spacing: 14 }
        Item { Layout.fillHeight: true }
    }
    component Primary: Rectangle {
        property string text
        signal clicked()
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: pLabel.implicitWidth + 48
        implicitHeight: pLabel.implicitHeight + 24
        radius: height / 2
        color: !enabled ? Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.14)
             : pArea.pressed ? Qt.darker(theme.primary, 1.25) : pArea.containsMouse ? Qt.darker(theme.primary, 1.1) : theme.primary
        scale: pArea.pressed ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Text { id: pLabel; anchors.centerIn: parent; text: parent.text; font: theme.title; color: parent.enabled ? theme.primaryInk : Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.55) }
        MouseArea { id: pArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: if (parent.enabled) parent.clicked() }
    }
    component Plain: Text {
        signal clicked()
        font: theme.body
        color: theme.inkVariant
        Layout.alignment: Qt.AlignHCenter
        opacity: plArea.pressed ? 0.55 : 1
        MouseArea { id: plArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }
    component Cap: Rectangle {
        property string text
        implicitWidth: capT.implicitWidth + 14
        implicitHeight: capT.implicitHeight + 6
        radius: 5
        color: theme.surfaceContainer
        border.color: Qt.rgba(theme.outlineVariant.r, theme.outlineVariant.g, theme.outlineVariant.b, 0.6)
        Text { id: capT; anchors.centerIn: parent; text: parent.text; font: theme.code; color: theme.ink }
    }
    component Capsule: Rectangle {
        default property alias inner: capsuleSlot.data
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 200; implicitHeight: 48
        radius: 24
        color: theme.surface
        border.color: Qt.rgba(0, 0, 0, 0.06)
        Item { id: capsuleSlot; anchors.fill: parent }
    }

    // ---------------------------------------------------------------- 0 welcome
    Component {
        id: welcome
        Scaffold {
            headline: qsTr("Speak. It types.")
            body: qsTr("Hold a key, say the thing, and polished text lands wherever your cursor is.")
            property real demoLevel: 0.1
            property string line1: ""
            property string line2: ""
            Capsule { Waveform { anchors.centerIn: parent; theme: win.th; level: demoLevel } }
            Column {
                Layout.alignment: Qt.AlignHCenter
                width: 420; height: 48; spacing: 2
                Text { width: 420; horizontalAlignment: Text.AlignHCenter; text: line1 ? "“" + line1 + "”" : ""; font.italic: true; font.pixelSize: 11; color: theme.inkVariant; opacity: line2 ? 0.45 : 1 }
                Text { width: 420; horizontalAlignment: Text.AlignHCenter; text: line2; font: theme.body; color: theme.ink }
            }
            Primary { text: qsTr("Get started"); onClicked: win.advance() }
            Timer {
                property int phase: 0
                property int i: 0
                readonly property string said: qsTr("um so let's meet at 1pm… actually no, 2pm")
                readonly property string wrote: qsTr("Let's meet at 2pm.")
                running: true; repeat: true; interval: 38
                onTriggered: {
                    if (phase === 0) { line1 = ""; line2 = ""; i = 0; phase = 1; interval = 38 }
                    else if (phase === 1) {
                        if (i < said.length) { line1 = said.substring(0, ++i); demoLevel = 0.35 + Math.random() * 0.45 }
                        else { demoLevel = 0.08; phase = 2; interval = 450 }
                    } else if (phase === 2) { phase = 3; i = 0; interval = 30 }
                    else if (phase === 3) {
                        if (i < wrote.length) line2 = wrote.substring(0, ++i)
                        else { phase = 4; interval = 2400 }
                    } else { phase = 0; interval = 38 }
                }
            }
        }
    }

    // ---------------------------------------------------------------- 1 API key
    Component {
        id: apiKey
        Scaffold {
            id: keyScreen
            headline: qsTr("Bring your own key.")
            body: qsTr("Add a Gemini key here, or skip and add an OpenRouter key for MAI in Settings → Advanced. Keys stay in your desktop wallet. Gemini sends audio to Google; MAI sends it through OpenRouter to Azure.")
            property bool replacing: false
            property bool validating: false
            property string status: ""
            property string detail: ""
            readonly property bool stored: (win.r, jot.geminiKeyStored())
            Connections {
                target: jot
                function onGeminiKeyResult(status, message) {
                    keyScreen.validating = false
                    keyScreen.status = status
                    keyScreen.detail = message
                    if (status === "valid" || status === "noModelAccess") win.advance()
                    else if (status === "savedOffline") advanceLater.start()
                }
            }
            Timer { id: advanceLater; interval: 1600; onTriggered: win.advance() }
            RowLayout {
                visible: keyScreen.stored && !keyScreen.replacing
                Layout.alignment: Qt.AlignHCenter
                JIcon { name: "dialog-ok-apply"; size: 16; color: theme.success }
                Text { text: qsTr("Key already in your wallet"); font: theme.body; color: theme.ink }
            }
            Plain { visible: keyScreen.stored && !keyScreen.replacing; text: qsTr("Use a different key"); font: theme.labelSmall; onClicked: { keyScreen.replacing = true; keyScreen.status = "" } }
            JField {
                id: keyField
                visible: !keyScreen.stored || keyScreen.replacing
                theme: win.th; secret: true; code: true
                Layout.preferredWidth: 320
                Layout.alignment: Qt.AlignHCenter
                placeholder: qsTr("Paste your key")
                onAccepted: saveBtn.clicked()
            }
            Text {
                Layout.maximumWidth: 320
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font: theme.labelSmall
                visible: text !== ""
                color: keyScreen.status === "savedOffline" ? theme.inkVariant : theme.error
                text: keyScreen.status === "rejected" ? (keyScreen.detail ? qsTr("That key was rejected: %1").arg(keyScreen.detail) : qsTr("That key didn't work — check it in AI Studio."))
                    : keyScreen.status === "savedOffline" ? qsTr("Couldn't reach Google to check this key — saved it anyway. Your first dictation will tell you for sure.")
                    : keyScreen.status === "saveFailed" ? qsTr("Couldn't save to your desktop wallet — try again.")
                    : keyScreen.status === "noModelAccess" ? qsTr("That key works, but it can't reach Jot's transcription model yet. Setup continues — ask for access, then try a dictation.") : ""
            }
            JButton { visible: !keyScreen.stored || keyScreen.replacing; theme: win.th; kind: "link"; text: qsTr("Get a key in Google AI Studio"); Layout.alignment: Qt.AlignHCenter; onClicked: jot.openUrl("https://aistudio.google.com/apikey") }
            BusyIndicator { visible: keyScreen.validating; running: visible; Layout.alignment: Qt.AlignHCenter }
            Primary {
                id: saveBtn
                visible: !keyScreen.validating
                text: (!keyScreen.stored || keyScreen.replacing) ? qsTr("Save & continue") : qsTr("Continue")
                enabled: keyScreen.stored && !keyScreen.replacing ? true : keyField.text.trim() !== ""
                onClicked: {
                    if (keyScreen.stored && !keyScreen.replacing) { win.advance(); return }
                    if (keyScreen.validating) return
                    keyScreen.validating = true
                    keyScreen.status = ""
                    jot.validateGeminiKey(keyField.text, true)
                }
            }
            Plain { visible: !keyScreen.stored || keyScreen.replacing; text: qsTr("I'll add it later"); onClicked: win.advance() }
        }
    }

    // ---------------------------------------------------------------- 2 microphone
    Component {
        id: microphone
        Scaffold {
            id: mic
            headline: qsTr("Say hello.")
            body: heard ? qsTr("Heard you loud and clear.") : qsTr("Jot is listening — this just checks your mic.")
            property bool heard: false
            property int count: 0
            property real peak: 0
            property string micState: "listening"
            property bool settled: false
            readonly property string name: (win.r, jot.selectedMicrophoneName())
            Component.onCompleted: jot.startMicTest()
            Component.onDestruction: jot.stopMicTest()
            Timer { interval: 4000; running: true; onTriggered: mic.settled = true }
            Connections {
                target: jot
                function onMicTestStatus(s) { mic.micState = s }
                function onMicTestLevelChanged() {
                    if (mic.heard) return
                    var l = jot.micTestLevel
                    mic.peak = Math.max(mic.peak, l)
                    mic.count = l > 0.18 ? mic.count + 1 : Math.max(0, mic.count - 1)
                    if (mic.count >= 7) { mic.heard = true; jot.stopMicTest(); heardAdvance.start() }
                }
            }
            Timer { id: heardAdvance; interval: 900; onTriggered: win.advance() }
            Capsule {
                Waveform { anchors.centerIn: parent; theme: win.th; level: jot.micTestLevel; opacity: mic.heard ? 0 : 1; Behavior on opacity { NumberAnimation { duration: 200 } } }
                JIcon { anchors.centerIn: parent; visible: mic.heard; name: "dialog-ok-apply"; size: 24; color: theme.success }
            }
            Text {
                visible: !mic.heard
                Layout.maximumWidth: 340
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font: theme.labelSmall
                readonly property bool bad: mic.micState === "missing" || mic.micState === "dead" || (mic.settled && mic.peak < 0.06)
                color: bad ? theme.error : theme.inkVariant
                text: mic.micState === "missing" ? qsTr("Connect %1 to check your microphone.").arg(mic.name)
                    : mic.micState === "dead" ? qsTr("No sound is reaching Jot from %1. Check the microphone's connection.").arg(mic.name)
                    : mic.peak >= 0.06 ? qsTr("Picking up sound from %1 — keep going.").arg(mic.name)
                    : mic.settled ? qsTr("Barely hearing anything from %1. Speak closer to the microphone.").arg(mic.name)
                    : qsTr("Listening on %1…").arg(mic.name)
            }
            JPicker {
                Layout.alignment: Qt.AlignHCenter
                theme: win.th
                implicitWidth: 320
                options: { win.r; var l = jot.microphones().map(m => ({ text: m.name, value: m.id })); l.push({ text: qsTr("Follow the system default input"), value: "__system__" }); return l }
                current: { win.r; return jot.setting("microphoneFollowsSystem") ? "__system__" : jot.setting("microphoneId") }
                onPicked: v => { if (v === "__system__") jot.selectSystemMicrophone(); else jot.selectMicrophone(v); mic.peak = 0; mic.count = 0; mic.settled = false; jot.startMicTest() }
            }
            Text { text: qsTr("Always uses the microphone you pick here"); font: theme.labelSmall; color: theme.inkVariant; Layout.alignment: Qt.AlignHCenter }
            Plain { text: qsTr("Continue without speaking"); onClicked: { jot.stopMicTest(); win.advance() } }
        }
    }

    // ---------------------------------------------------------------- 3 keyboard access
    Component {
        id: keyboard
        Scaffold {
            id: kb
            headline: qsTr("Let it hear your key.")
            body: qsTr("Wayland keeps key presses private. Jot needs to read your keyboards to notice the dictation key — and can type for you through KWin.")
            readonly property bool granted: (win.r, jot.evdevAvailable())
            property int waited: 0
            Timer { interval: 1000; running: !kb.granted; repeat: true; onTriggered: { kb.waited++; jot.recheckKeyboardAccess() } }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 440
                implicitHeight: cardCol.implicitHeight + 32
                radius: 16
                color: theme.surface
                border.color: Qt.rgba(theme.outlineVariant.r, theme.outlineVariant.g, theme.outlineVariant.b, 0.3)
                ColumnLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    RowLayout {
                        spacing: 12
                        JIcon { name: "input-keyboard"; size: 22; color: kb.granted ? theme.success : theme.primary }
                        Text { text: qsTr("Keyboard access"); font: theme.body; color: theme.ink; Layout.fillWidth: true }
                        JIcon { visible: kb.granted; name: "dialog-ok-apply"; size: 16; color: theme.success }
                    }
                    Text { visible: !kb.granted; text: qsTr("Run this once in a terminal (asks for your password):"); font: theme.labelSmall; color: theme.inkVariant }
                    RowLayout {
                        visible: !kb.granted
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 28; radius: 6; color: theme.surfaceContainer
                            Text { anchors.fill: parent; anchors.leftMargin: 8; verticalAlignment: Text.AlignVCenter; text: jot.permissionCommand(); font: theme.code; color: theme.ink; elide: Text.ElideMiddle }
                        }
                        JButton { theme: win.th; small: true; text: qsTr("Copy"); doneText: qsTr("Copied"); onClicked: jot.copyText(jot.permissionCommand()) }
                    }
                    RowLayout {
                        spacing: 12
                        JIcon { name: "edit-paste"; size: 22; color: (win.r, jot.pasteAvailable()) ? theme.success : theme.primary }
                        Text { text: qsTr("Typing at your cursor"); font: theme.body; color: theme.ink; Layout.fillWidth: true }
                        JIcon { visible: (win.r, jot.pasteAvailable()); name: "dialog-ok-apply"; size: 16; color: theme.success }
                        Text { visible: !jot.pasteAvailable(); text: qsTr("after install.sh"); color: jot.pasteAvailable() ? theme.success : theme.inkVariant; font: theme.labelSmall }
                    }
                }
            }
            Text {
                visible: !kb.granted && kb.waited >= 10
                text: qsTr("Granted but not detected? Unplug and replug the keyboard, or log out and back in.")
                font: theme.labelSmall; color: theme.inkVariant
                Layout.alignment: Qt.AlignHCenter
            }
            Primary { text: qsTr("Continue"); enabled: kb.granted; onClicked: win.advance() }
            Plain {
                visible: !kb.granted
                text: qsTr("Use a desktop shortcut instead")
                onClicked: { jot.setSetting("hotkeyBackend", "portal"); win.advance() }
            }
        }
    }

    // ---------------------------------------------------------------- 4 choose key
    Component {
        id: chooseKey
        Scaffold {
            id: ck
            headline: qsTr("Pick your dictation key.")
            body: (win.r, jot.hotkeyBackend()) === "evdev"
                ? qsTr("Any key works — a bare modifier like Right Ctrl is the closest thing to fn. Press the key you want.")
                : qsTr("Jot is using a desktop shortcut. KDE asked you to confirm it; change it any time in System Settings → Keyboard → Shortcuts → Jot.")
            property bool capturing: false
            Connections {
                target: jot
                function onHotkeyCaptured(code, name) { ck.capturing = false; if (code >= 0) jot.setSetting("hotkeyCode", code) }
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 260; implicitHeight: 64; radius: 16
                color: theme.surface
                border.color: ck.capturing ? theme.primary : Qt.rgba(theme.outlineVariant.r, theme.outlineVariant.g, theme.outlineVariant.b, 0.4)
                border.width: ck.capturing ? 2 : 1
                Text { anchors.centerIn: parent; text: ck.capturing ? qsTr("Press a key…") : win.key; font: theme.headline; color: theme.ink }
            }
            JButton {
                visible: (win.r, jot.hotkeyBackend()) === "evdev"
                Layout.alignment: Qt.AlignHCenter
                theme: win.th
                text: ck.capturing ? qsTr("Cancel") : qsTr("Change key")
                onClicked: { if (ck.capturing) { jot.cancelHotkeyCapture(); ck.capturing = false } else { ck.capturing = true; jot.captureHotkey() } }
            }
            Primary { text: qsTr("Continue"); onClicked: win.advance() }
        }
    }

    // ---------------------------------------------------------------- 5 how-to
    Component {
        id: howTo
        Scaffold {
            headline: qsTr("Talk to Jot.")
            body: qsTr("Three gestures — that's the whole product.")
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 460
                implicitHeight: gestures.implicitHeight + 32
                radius: 16
                color: theme.surface
                ColumnLayout {
                    id: gestures
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Repeater {
                        model: [
                            [[win.key], qsTr("Hold and talk"), qsTr("Release, and polished text lands at your cursor.")],
                            [[win.key, qsTr("space")], qsTr("Go hands-free"), qsTr("Tap Space while holding — talk as long as you like, tap %1 to finish.").arg(win.key)],
                            [[qsTr("esc")], qsTr("Changed your mind"), qsTr("Cancels the dictation. Long recordings are kept in History.")],
                        ]
                        RowLayout {
                            spacing: 12
                            Row { Layout.preferredWidth: 132; spacing: 4; Repeater { model: modelData[0]; Cap { text: modelData } } }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Text { text: modelData[1]; font: theme.body; color: theme.ink }
                                Text { text: modelData[2]; font: theme.labelSmall; color: theme.inkVariant; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
            Primary { text: qsTr("Got it"); onClicked: win.advance() }
        }
    }

    // ---------------------------------------------------------------- 6 try it
    Component {
        id: tryIt
        Scaffold {
            id: ti
            headline: qsTr("Try it.")
            body: qsTr("Click into the field, hold %1, and change your mind mid-sentence:").arg(win.key)
            property var reveal: null
            property bool fetched: false
            readonly property int words: field.text.trim() === "" ? 0 : field.text.trim().split(/\s+/).length
            function norm(s) { return (s || "").toLowerCase().replace(/[^\p{L}\p{N}]+/gu, " ").trim() }
            function fetchReveal() {
                var r = jot.latestRecord()
                if (!r.id) return
                var raw = r.raw || "", clean = r.cleaned || ""
                if (raw && clean && norm(raw) !== norm(clean)) { reveal = { said: raw, wrote: clean }; return }
                var script = qsTr("Let's schedule the meeting for 1pm — actually, no, make it 2pm.")
                var cw = clean.trim().split(/\s+/).length
                if (cw >= 3 && (clean.indexOf("2pm") >= 0 || clean.indexOf("2 pm") >= 0) && clean.length < script.length)
                    reveal = { said: script, wrote: clean }
            }
            Timer { id: r1; interval: 400; onTriggered: ti.fetchReveal() }
            Timer { id: r2; interval: 1000; onTriggered: if (!ti.reveal) ti.fetchReveal() }
            Rectangle {
                visible: !ti.reveal
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: scriptT.implicitWidth + 32; implicitHeight: scriptT.implicitHeight + 16
                radius: height / 2
                color: theme.surfaceContainer
                Text { id: scriptT; anchors.centerIn: parent; text: qsTr("“Let's schedule the meeting for 1pm — actually, no, make it 2pm.”"); font.italic: true; font.pixelSize: 13; color: theme.ink }
            }
            Text { visible: !ti.reveal; text: qsTr("(or say anything you like)"); font: theme.labelSmall; color: theme.inkVariant; Layout.alignment: Qt.AlignHCenter }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 400; implicitHeight: 96
                radius: 16
                color: theme.surface
                border.color: Qt.rgba(theme.outlineVariant.r, theme.outlineVariant.g, theme.outlineVariant.b, 0.4)
                TextEdit {
                    id: field
                    anchors.fill: parent
                    anchors.margins: 12
                    font: theme.bodyLarge
                    color: theme.ink
                    wrapMode: TextEdit.Wrap
                    onTextChanged: if (text.trim() !== "" && !ti.fetched) { ti.fetched = true; r1.start(); r2.start() }
                    Text { visible: field.text === ""; text: qsTr("Your words will land here."); font: theme.bodyLarge; color: theme.inkVariant; opacity: 0.6 }
                }
            }
            Rectangle {
                visible: !!ti.reveal
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 400; implicitHeight: revealCol.implicitHeight + 24
                radius: 12
                color: theme.surfaceContainer
                GridLayout {
                    id: revealCol
                    anchors.fill: parent
                    anchors.margins: 12
                    columns: 2
                    columnSpacing: 10
                    Text { text: qsTr("You said"); font: theme.labelSmall; color: theme.inkVariant; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight }
                    Text { text: ti.reveal ? ti.reveal.said : ""; font.italic: true; font.pixelSize: 11; color: theme.inkVariant; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { text: qsTr("Jot wrote"); font: theme.labelSmall; color: theme.inkVariant; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight }
                    Text { text: ti.reveal ? ti.reveal.wrote : ""; font: theme.body; color: theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            Text {
                visible: !ti.reveal && ti.words >= 2
                text: qsTr("You just dictated %n word(s). That's the whole trick.", "", ti.words)
                font: theme.body; color: theme.ink
                Layout.alignment: Qt.AlignHCenter
            }
            Primary { visible: ti.words > 0; text: qsTr("Continue"); onClicked: win.advance() }
            Plain { visible: ti.words === 0; text: qsTr("Skip for now"); onClicked: win.advance() }
        }
    }

    // ---------------------------------------------------------------- 7 done
    Component {
        id: done
        Scaffold {
            headline: qsTr("You're set.")
            body: qsTr("Jot lives in your system tray now. Hold %1 anywhere and start talking.").arg(win.key)
            Text {
                Layout.maximumWidth: 480
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("It removes filler words like \"umm\" and \"uhh\", follows your change of mind, and takes \"new paragraph\" literally. Teach it your jargon in Settings → Dictionary.")
                font: theme.body
                color: theme.ink
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                CheckBox { id: login; checked: true }
                Text { text: qsTr("Start Jot at login"); font: theme.body; color: theme.ink }
            }
            Primary { text: qsTr("Start dictating"); onClicked: jot.finishOnboarding(login.checked) }
        }
    }
}
