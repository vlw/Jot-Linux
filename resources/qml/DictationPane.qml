// Dictation pane — verbatim port of the macOS pane.
import QtQuick
import QtQuick.Layouts

PaneScroll {
    id: pane
    property int r: jot.rev
    readonly property bool mai: (r, jot.setting("transcriptionProvider") === "mai")
    readonly property bool legacy: (r, jot.setting("legacyTranscribeEndpoint"))

    JSection {
        theme: pane.theme
        footer: pane.mai
            ? qsTr("MAI sends audio through OpenRouter to Azure after you release the key. No Live streaming or extra Gemini cleanup. Changes apply to the next dictation or retry.")
            : qsTr("Gemini can stream while you speak with Live enabled. Your Gemini settings are saved separately. Changes apply to the next dictation or retry.")
        JRow {
            theme: pane.theme
            label: qsTr("Transcription service")
            JPicker {
                theme: pane.theme
                options: [{ text: "Gemini", value: "gemini" }, { text: "MAI-Transcribe-2", value: "mai" }]
                current: (pane.r, jot.setting("transcriptionProvider"))
                onPicked: v => jot.setSetting("transcriptionProvider", v)
            }
        }
        JRow {
            theme: pane.theme
            visible: pane.mai
            label: qsTr("MAI mode")
            JPicker {
                theme: pane.theme
                options: [{ text: "Дословно", value: "verbatim" }, { text: "Со встроенной очисткой", value: "clean" }]
                current: (pane.r, jot.setting("maiStyle"))
                onPicked: v => jot.setSetting("maiStyle", v)
            }
        }
    }

    JSection {
        theme: pane.theme
        footer: qsTr("If no text arrives within this time after you release the key, Jot stops waiting and keeps the recording in History for Retry. Long recordings get 1 extra second per 10 s of audio beyond 30 s. Recordings shorter than the minimum are treated as accidental presses and never sent. The × on the pill cancels waiting at any moment.")
        JRow {
            theme: pane.theme
            label: qsTr("Wait for transcription")
            JPicker {
                theme: pane.theme
                options: [5, 8, 12, 20, 30].map(v => ({ text: qsTr("%1 s").arg(v), value: v }))
                current: (pane.r, Number(jot.setting("transcriptionTimeout")))
                onPicked: v => jot.setSetting("transcriptionTimeout", v)
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Minimum recording length")
            JPicker {
                theme: pane.theme
                options: [0.5, 1, 1.5, 2].map(v => ({ text: qsTr("%1 s").arg(v.toLocaleString(Qt.locale(jot.localeName), 'f', v % 1 ? 1 : 0)), value: v }))
                current: (pane.r, Number(jot.setting("minimumRecording")))
                onPicked: v => jot.setSetting("minimumRecording", v)
            }
        }
    }

    JSection {
        theme: pane.theme
        footer: qsTr("Resumes only media paused by Jot. Works with every player that supports MPRIS — browsers, Spotify, VLC, mpv and more.")
        JRow {
            theme: pane.theme
            label: qsTr("Pause music and videos while recording")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("pauseMediaWhileRecording")); onToggled: v => jot.setSetting("pauseMediaWhileRecording", v) }
        }
    }

    JSection {
        theme: pane.theme
        footer: qsTr("The resting dot grows into a Dictate button on hover; click it for hands-free. Off = the pill appears only while dictating.")
        JRow {
            theme: pane.theme
            label: qsTr("Sounds")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("soundsEnabled")); onToggled: v => jot.setSetting("soundsEnabled", v) }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Show resting indicator")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("showIdleIndicator")); onToggled: v => jot.setSetting("showIdleIndicator", v) }
        }
    }

    JSection {
        theme: pane.theme
        visible: !pane.mai
        footer: qsTr("Removes filler words and applies self-corrections (\"at 2 — actually 3\") as it transcribes. Turn both this and text cleanup off for word-for-word dictation.")
        JRow {
            theme: pane.theme
            label: qsTr("Smart transcription")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("smartTranscription")); onToggled: v => jot.setSetting("smartTranscription", v) }
        }
    }

    JSection {
        theme: pane.theme
        visible: !pane.mai
        footer: qsTr("Shortens filler and repeated ideas while keeping your meaning, uncertainty, names and numbers. Keeps messages as messages and tasks clear. Uses one extra Gemini request; if an edit fails validation or times out, the original transcript is kept. Every finished transcript stays on the clipboard, even if automatic insertion fails.")
        JRow {
            theme: pane.theme
            label: qsTr("Remove repetition and tidy up text")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("smartCleanupPass")); onToggled: v => jot.setSetting("smartCleanupPass", v) }
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("Experimental")
        footer: {
            pane.r
            if (pane.mai) return qsTr("Live is available only with Gemini. Your Live setting is preserved when you use MAI.")
            if (pane.legacy) return qsTr("Live transcription is unavailable while the legacy transcription endpoint is on in Advanced.")
            var base = qsTr("Loud rooms judges your voice against the actual room noise instead of a fixed level. Live streams your voice as you speak instead of uploading at the end, then applies the same text cleanup. If the connection stumbles it falls back to the saved recording. These experiments are off by default.")
            var summary = jot.liveSummary()
            return summary ? base + "\n" + summary : base
        }
        JRow {
            theme: pane.theme
            label: qsTr("Better hearing in loud rooms")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("experimentalNoiseHandling")); onToggled: v => jot.setSetting("experimentalNoiseHandling", v) }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Live transcription")
            enabled: !pane.mai && !pane.legacy
            JSwitch {
                theme: pane.theme
                enabled: !pane.mai && !pane.legacy
                checked: (pane.r, jot.setting("liveTranscription"))
                onToggled: v => jot.setSetting("liveTranscription", v)
            }
        }
    }
    Item { Layout.preferredHeight: 12 }
}
