// Privacy & Storage pane.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

PaneScroll {
    id: pane
    property int r: jot.rev

    JSection {
        theme: pane.theme
        footer: qsTr("Audio is needed for Retry. Deleting a transcript removes its whole History entry, audio included. Entries still waiting to be transcribed are always kept.")
        JRow {
            theme: pane.theme
            label: qsTr("Keep audio recordings")
            JPicker {
                theme: pane.theme
                options: [
                    { text: qsTr("Never (disables Retry)"), value: -1 },
                    { text: qsTr("24 hours"), value: 1 },
                    { text: qsTr("7 days"), value: 7 },
                    { text: qsTr("30 days"), value: 30 },
                    { text: qsTr("Forever"), value: 0 },
                ]
                current: (pane.r, Number(jot.setting("audioRetentionDays")))
                onPicked: v => { jot.setSetting("audioRetentionDays", v); jot.purgeRetention() }
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Keep transcripts")
            JPicker {
                theme: pane.theme
                options: [
                    { text: qsTr("1 hour"), value: 1 },
                    { text: qsTr("12 hours"), value: 12 },
                    { text: qsTr("24 hours"), value: 24 },
                    { text: qsTr("7 days"), value: 7 * 24 },
                    { text: qsTr("15 days"), value: 15 * 24 },
                    { text: qsTr("30 days"), value: 30 * 24 },
                    { text: qsTr("3 months"), value: 91 * 24 },
                    { text: qsTr("6 months"), value: 182 * 24 },
                    { text: qsTr("1 year"), value: 365 * 24 },
                    { text: qsTr("Forever"), value: 0 },
                ]
                current: (pane.r, Number(jot.setting("transcriptRetentionHours")))
                onPicked: v => { jot.setSetting("transcriptRetentionHours", v); jot.purgeRetention() }
            }
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("What leaves your computer")
        footer: qsTr("Audio and dictionary terms go to the selected service. No analytics, screenshots or keystroke logging.")
        Repeater {
            model: [
                [qsTr("Audio"), qsTr("Gemini: Google. MAI: OpenRouter / Azure.")],
                [qsTr("Transcript text"), qsTr("Gemini cleanup sends text to Google when enabled. MAI cleanup runs with transcription.")],
                [qsTr("Dictionary terms"), qsTr("Sent with the audio, so names are spelled right as you speak")],
                [qsTr("Everything else"), qsTr("Never leaves this computer")],
            ]
            JRow {
                theme: pane.theme
                label: modelData[0]
                Text {
                    text: modelData[1]
                    font: pane.theme.body
                    color: pane.theme.inkVariant
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignRight
                    Layout.maximumWidth: 380
                }
            }
        }
    }

    JSection {
        theme: pane.theme
        JRow {
            theme: pane.theme
            JButton { theme: pane.theme; kind: "destructive"; text: qsTr("Delete All History…"); onClicked: confirm.open() }
            Item { Layout.fillWidth: true }
        }
    }

    Dialog {
        id: confirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 380
        background: Rectangle { radius: 14; color: pane.theme.surface; border.color: pane.theme.outlineVariant }
        contentItem: ColumnLayout {
            spacing: 16
            Text {
                text: qsTr("Delete all dictation history? Audio and transcripts will be removed from this computer.")
                wrapMode: Text.WordWrap
                font: pane.theme.body
                color: pane.theme.ink
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                JButton { theme: pane.theme; text: qsTr("Cancel"); onClicked: confirm.close() }
                JButton { theme: pane.theme; kind: "destructive"; text: qsTr("Delete Everything"); onClicked: { jot.deleteAllHistory(); confirm.close() } }
            }
        }
    }
    Item { Layout.preferredHeight: 12 }
}
