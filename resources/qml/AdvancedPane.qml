// Advanced pane — API keys (Secret Service), model overrides, legacy endpoint.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

PaneScroll {
    id: pane
    property int r: jot.rev
    property string geminiStatus: (r, jot.geminiKeyStored()) ? "stored" : "missing"
    property string orMessage: ""
    property bool orChecking: false

    Connections {
        target: jot
        function onGeminiKeyResult(status, message) {
            if (status === "rejected") { pane.geminiStatus = "invalid"; return }
            geminiField.text = ""
            pane.geminiStatus = status === "savedOffline" ? "savedOffline"
                              : status === "saveFailed" ? "saveFailed" : "valid"
        }
        function onOpenRouterResult(ok, message) {
            pane.orChecking = false
            pane.orMessage = message
            if (ok) orField.text = ""
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("Gemini API key")
        footer: qsTr("Stored in your desktop's secret store (KWallet) and only ever sent to Google.")
        JRow {
            theme: pane.theme
            label: qsTr("API key")
            JField {
                id: geminiField
                theme: pane.theme
                secret: true
                code: true
                implicitWidth: 300
                placeholder: (pane.r, jot.geminiKeyStored()) ? qsTr("••••••••  (stored in KWallet)") : qsTr("Paste your key")
                onAccepted: saveGemini.clicked()
            }
            JIcon {
                size: 16
                name: pane.geminiStatus === "valid" || pane.geminiStatus === "stored" || pane.geminiStatus === "savedOffline" ? "dialog-ok-apply"
                    : pane.geminiStatus === "invalid" || pane.geminiStatus === "saveFailed" ? "window-close"
                    : pane.geminiStatus === "validating" ? "view-refresh" : "dialog-password"
                color: pane.geminiStatus === "valid" ? pane.theme.success
                     : (pane.geminiStatus === "invalid" || pane.geminiStatus === "saveFailed") ? pane.theme.error : pane.theme.inkVariant
            }
        }
        Text {
            Layout.leftMargin: 12
            visible: text !== ""
            font: pane.theme.labelSmall
            color: pane.geminiStatus === "savedOffline" ? pane.theme.inkVariant : pane.theme.error
            text: pane.geminiStatus === "invalid" && jot.geminiKeyStored() ? qsTr("That key didn't work — your saved key is unchanged.")
                : pane.geminiStatus === "invalid" ? qsTr("That key didn't work — check it in AI Studio.")
                : pane.geminiStatus === "saveFailed" ? qsTr("The key validated but couldn't be saved to your wallet — try again.")
                : pane.geminiStatus === "savedOffline" ? qsTr("You look offline — key saved; it'll be checked on your first dictation.") : ""
        }
        RowLayout {
            Layout.margins: 12
            spacing: 10
            JButton {
                id: saveGemini
                theme: pane.theme
                text: qsTr("Save & Validate")
                enabled: geminiField.text.trim() !== "" && pane.geminiStatus !== "validating"
                onClicked: { pane.geminiStatus = "validating"; jot.validateGeminiKey(geminiField.text, false) }
            }
            JButton {
                theme: pane.theme
                kind: "destructive"
                text: qsTr("Remove Key…")
                visible: ["stored", "valid", "savedOffline"].indexOf(pane.geminiStatus) >= 0
                onClicked: { jot.removeGeminiKey(); pane.geminiStatus = "missing" }
            }
            Item { Layout.fillWidth: true }
            JButton { theme: pane.theme; kind: "link"; text: qsTr("Get a key in Google AI Studio"); onClicked: jot.openUrl("https://aistudio.google.com/apikey") }
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("OpenRouter API key — MAI")
        footer: qsTr("Stored separately in Jot's wallet entry. Sent only to OpenRouter. Audio is processed by Azure.")
        JRow {
            theme: pane.theme
            label: qsTr("OpenRouter API key")
            JField {
                id: orField
                theme: pane.theme
                secret: true
                code: true
                implicitWidth: 300
                placeholder: (pane.r, jot.openRouterKeyStored()) ? qsTr("Stored in KWallet") : qsTr("Paste your OpenRouter key")
            }
        }
        RowLayout {
            Layout.margins: 12
            spacing: 10
            JButton {
                theme: pane.theme
                text: orField.text === "" ? qsTr("Validate Saved Key") : qsTr("Save & Validate")
                enabled: !pane.orChecking && (orField.text !== "" || (pane.r, jot.openRouterKeyStored()))
                onClicked: { pane.orChecking = true; pane.orMessage = qsTr("Checking OpenRouter…"); jot.validateOpenRouterKey(orField.text) }
            }
            JButton {
                theme: pane.theme
                kind: "destructive"
                text: qsTr("Remove OpenRouter Key")
                visible: (pane.r, jot.openRouterKeyStored())
                enabled: !pane.orChecking
                onClicked: pane.orMessage = jot.removeOpenRouterKey() ? qsTr("OpenRouter key removed") : qsTr("Could not remove the OpenRouter key from the wallet")
            }
            BusyIndicator { running: pane.orChecking; visible: running; implicitWidth: 20; implicitHeight: 20 }
            Item { Layout.fillWidth: true }
            JButton { theme: pane.theme; kind: "link"; text: qsTr("OpenRouter keys and credits"); onClicked: jot.openUrl("https://openrouter.ai/settings/keys") }
        }
        Text {
            Layout.leftMargin: 12
            Layout.bottomMargin: 8
            visible: pane.orMessage !== ""
            text: pane.orMessage
            font: pane.theme.labelSmall
            color: pane.theme.inkVariant
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    JSection {
        theme: pane.theme
        header: qsTr("Gemini model overrides")
        footer: qsTr("Preview models get renamed — override here if a model 404s. Leave blank for defaults — every edit saves as you type.")
        JRow {
            theme: pane.theme
            label: qsTr("Endpoint")
            JField {
                id: endpoint
                theme: pane.theme; code: true; implicitWidth: 340
                placeholder: "https://generativelanguage.googleapis.com"
                text: jot.setting("endpointOverride") || ""
                onEdited: jot.setSetting("endpointOverride", text)
            }
        }
        Text {
            Layout.leftMargin: 12
            visible: endpoint.text.trim() !== "" && !jot.usableEndpoint(endpoint.text)
            text: qsTr("Not a valid http(s) URL — the default endpoint is being used.")
            font: pane.theme.labelSmall
            color: pane.theme.error
        }
        JRow {
            theme: pane.theme
            label: qsTr("Transcription model")
            JField {
                theme: pane.theme; code: true; implicitWidth: 340
                placeholder: "gemini-3.5-transcribe"
                text: jot.setting("transcribeModelOverride") || ""
                onEdited: jot.setSetting("transcribeModelOverride", text)
            }
        }
        JRow {
            theme: pane.theme
            label: qsTr("Formatting model")
            JField {
                theme: pane.theme; code: true; implicitWidth: 340
                placeholder: "gemini-3.5-flash-lite"
                text: jot.setting("cleanupModelOverride") || ""
                onEdited: jot.setSetting("cleanupModelOverride", text)
            }
        }
    }

    JSection {
        theme: pane.theme
        footer: qsTr("For Gemini, Jot uses the newer interactions endpoint, which is what makes Smart transcription possible. If it starts misbehaving, this switches back to the older one — transcription still works, but it will be word-for-word and Smart transcription will have no effect.")
        JRow {
            theme: pane.theme
            label: qsTr("Use the previous transcription endpoint")
            JSwitch { theme: pane.theme; checked: (pane.r, jot.setting("legacyTranscribeEndpoint")); onToggled: v => jot.setSetting("legacyTranscribeEndpoint", v) }
        }
    }
    Item { Layout.preferredHeight: 12 }
}
