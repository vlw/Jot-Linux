// Port of HistoryWindow.swift.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: pane
    property var theme
    property string query: ""
    property int hr: jot.historyRev
    property var records: (hr, jot.historyRecords(query))
    property var attention: (hr, jot.attentionRecords(query))
    property var stats: (hr, jot.historyStats())
    property bool expanded: false
    property var timeline: records.filter(r => r.text !== "" && r.status !== "queuedForRetry" && r.status !== "failed")

    function dayLabel(d) {
        var now = new Date()
        var day = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        var today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        var diff = Math.round((today - day) / 86400000)
        if (diff === 0) return qsTr("Today")
        if (diff === 1) return qsTr("Yesterday")
        return d.toLocaleDateString(Qt.locale(jot.localeName), qsTr("MMM d, yyyy"))
    }
    function timeLabel(d) { return d.toLocaleTimeString(Qt.locale(jot.localeName), Locale.ShortFormat) }
    function attentionTitle(r) {
        if (r.status === "queuedForRetry") {
            if (r.errorCode === "auth") return qsTr("Waiting for a working API key — retries automatically")
            if (r.errorCode === "quota") return qsTr("Waiting for quota or credits — retries automatically")
            return qsTr("Waiting for network")
        }
        if (r.status === "cancelled") return r.audioExists ? qsTr("Cancelled recording — audio kept") : qsTr("Cancelled recording — audio deleted by your retention setting")
        if (r.status === "failed") {
            switch (r.errorCode) {
            case "audio_purged": return qsTr("Audio was deleted by your retention setting")
            case "tooNoisy": return qsTr("Too noisy — no speech heard")
            case "bad_request": return qsTr("Couldn't process this one")
            case "model": return qsTr("Model not available to your key — see Settings → Advanced")
            case "cancelled": return qsTr("Transcription cancelled — audio kept")
            case "gave_up": return qsTr("No answer in time — tap Retry")
            case "auth": return qsTr("API key problem — retries automatically after you fix it")
            case "quota": return qsTr("Quota or credits exhausted — retries automatically")
            }
            return qsTr("Transcription failed")
        }
        return qsTr("Recovered recording")
    }
    function chip(r) {
        if (r.status === "awaitingChip") return r.ageSeconds < 300 ? [qsTr("Ready to paste"), theme.primary] : [qsTr("Wasn't pasted"), theme.inkVariant]
        if (r.status === "recovered") return [qsTr("Recovered"), theme.primary]
        if (r.status === "heldSecure") return [qsTr("Kept — secure field"), theme.inkVariant]
        if (r.status === "cancelled") return [qsTr("Cancelled"), theme.inkVariant]
        return null
    }
    function statusName(s) {
        return ({ inserted: qsTr("Inserted at the cursor"), copiedToClipboard: qsTr("Copied to the clipboard"), awaitingChip: qsTr("Ready to paste"),
                  recovered: qsTr("Recovered — use Copy to grab the text"), heldSecure: qsTr("Kept — secure field blocked insertion"),
                  queuedForRetry: qsTr("Queued — retries automatically"), cancelled: qsTr("Cancelled"), failed: qsTr("Failed"), silent: qsTr("No speech detected") })[s] || s
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 20
            Layout.bottomMargin: 12
            spacing: 12
            RowLayout {
                spacing: 24
                Repeater {
                    model: [[pane.stats.words, qsTr("words dictated")], [pane.stats.dictations, qsTr("dictations")],
                            [pane.stats.wpm > 0 ? pane.stats.wpm : "—", qsTr("avg WPM")]]
                    ColumnLayout {
                        spacing: 1
                        Text { text: String(modelData[0]); font: pane.theme.title; color: pane.theme.ink }
                        Text { text: modelData[1]; font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                    }
                }
                Item { Layout.fillWidth: true }
                Row {
                    spacing: 3
                    Repeater { model: 4; Rectangle { width: 12; height: 4; radius: 2; color: pane.theme.brandQuad[index] } }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 32
                radius: 8
                color: Qt.rgba(pane.theme.inkVariant.r, pane.theme.inkVariant.g, pane.theme.inkVariant.b, 0.08)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8
                    JIcon { name: "search"; size: 16; color: pane.theme.inkVariant }
                    TextInput {
                        id: search
                        Layout.fillWidth: true
                        font: pane.theme.body
                        color: pane.theme.ink
                        clip: true
                        onTextChanged: pane.query = text
                        Text {
                            anchors.fill: parent
                            visible: search.text === ""
                            text: qsTr("Search your dictations")
                            font: pane.theme.body
                            color: pane.theme.inkVariant
                        }
                    }
                }
            }
        }

        // ---- empty
        ColumnLayout {
            visible: pane.records.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            Item { Layout.fillHeight: true }
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 5
                Repeater {
                    model: [18, 30, 24, 14]
                    Rectangle { width: 6; height: modelData; radius: 3; color: pane.theme.brandQuad[index]; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Text { text: jot.historyAvailable() ? qsTr("Nothing here yet") : qsTr("History unavailable"); font: pane.theme.title; color: pane.theme.ink; Layout.alignment: Qt.AlignHCenter }
            Text { text: qsTr("Hold %1 and say hello.").arg(jot.hotkeyName()); font: pane.theme.body; color: pane.theme.inkVariant; Layout.alignment: Qt.AlignHCenter }
            Item { Layout.fillHeight: true }
        }

        // ---- list
        ListView {
            id: list
            visible: pane.records.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            ScrollBar.vertical: ScrollBar {}
            header: Column {
                width: list.width
                visible: pane.attention.length > 0
                height: visible ? implicitHeight + 10 : 0
                spacing: 2
                RowLayout {
                    width: parent.width - 40
                    x: 20
                    Rectangle { width: 6; height: 6; radius: 3; color: pane.theme.gYellow }
                    Text { text: qsTr("Needs attention (%1)").arg(pane.attention.length); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                    Item { Layout.fillWidth: true }
                    JButton { theme: pane.theme; kind: "link"; text: qsTr("Discard All"); onClicked: discardAll.open() }
                }
                Repeater {
                    model: pane.expanded ? pane.attention : pane.attention.slice(0, 3)
                    Rectangle {
                        width: list.width - 24
                        x: 12
                        height: 50
                        radius: 8
                        color: pane.theme.surface
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 12
                            ColumnLayout {
                                spacing: 3
                                Layout.fillWidth: true
                                Text { text: pane.attentionTitle(modelData); font: pane.theme.body; color: pane.theme.ink; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    font: pane.theme.labelSmall
                                    color: pane.theme.inkVariant
                                    text: [modelData.app, modelData.duration !== undefined && modelData.duration !== null ? qsTr("%1s of audio").arg(Math.round(modelData.duration)) : "",
                                           modelData.startedAt.toLocaleString(Qt.locale(jot.localeName), qsTr("MMM d, h:mm AP"))].filter(s => s).join("    ")
                                }
                            }
                            JButton {
                                theme: pane.theme; small: true; text: qsTr("Retry")
                                visible: modelData.audioExists || (modelData.raw !== undefined && modelData.raw !== null)
                                onClicked: jot.retryRecord(modelData.id)
                            }
                            JButton { theme: pane.theme; kind: "plain"; icon: "window-close"; onClicked: jot.deleteRecord(modelData.id); ToolTip.visible: hovered; ToolTip.text: qsTr("Discard this recording") }
                        }
                    }
                }
                JButton {
                    x: 20
                    theme: pane.theme
                    kind: "link"
                    visible: pane.attention.length > 3
                    text: pane.expanded ? qsTr("Show less") : qsTr("Show %1 more").arg(pane.attention.length - 3)
                    onClicked: pane.expanded = !pane.expanded
                }
            }
            model: pane.timeline
            delegate: Column {
                width: list.width
                readonly property bool firstOfDay: index === 0 || pane.dayLabel(pane.timeline[index - 1].startedAt) !== pane.dayLabel(modelData.startedAt)
                Text {
                    visible: parent.firstOfDay
                    text: pane.dayLabel(modelData.startedAt)
                    font: pane.theme.labelSmall
                    color: pane.theme.inkVariant
                    leftPadding: 20
                    topPadding: 10
                    bottomPadding: 4
                }
                Rectangle {
                    width: list.width - 24
                    x: 12
                    height: 52
                    radius: 8
                    color: rowArea.containsMouse ? Qt.rgba(pane.theme.ink.r, pane.theme.ink.g, pane.theme.ink.b, 0.05) : pane.theme.surface
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 12
                        ColumnLayout {
                            spacing: 3
                            Layout.fillWidth: true
                            Text {
                                text: modelData.text.length > 0 ? modelData.text.substring(0, 110).replace(/\n/g, " ") : "—"
                                font: pane.theme.body
                                color: pane.theme.ink
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                font: pane.theme.labelSmall
                                color: pane.theme.inkVariant
                                text: [modelData.app, modelData.duration !== undefined && modelData.duration !== null ? qsTr("%1s").arg(Math.round(modelData.duration)) : "",
                                       pane.timeLabel(modelData.startedAt)].filter(s => s).join("    ")
                            }
                        }
                        Rectangle {
                            readonly property var c: pane.chip(modelData)
                            visible: c !== null
                            implicitWidth: chipText.implicitWidth + 14
                            implicitHeight: chipText.implicitHeight + 4
                            radius: height / 2
                            color: c ? Qt.rgba(c[1].r, c[1].g, c[1].b, 0.12) : "transparent"
                            Text { id: chipText; anchors.centerIn: parent; text: parent.c ? parent.c[0] : ""; color: parent.c ? parent.c[1] : "transparent"; font: pane.theme.labelSmall }
                        }
                        JIcon { name: "go-next"; size: 16; color: pane.theme.inkVariant }
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) { menu.record = modelData; menu.popup() }
                            else { detail.record = modelData; detail.showRaw = false; detail.open() }
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: menu
        property var record
        MenuItem { text: qsTr("Copy"); onTriggered: jot.copyText(menu.record.text) }
        MenuItem { text: qsTr("Retry Transcription"); onTriggered: jot.retryRecord(menu.record.id) }
        MenuSeparator {}
        MenuItem { text: qsTr("Delete"); onTriggered: jot.deleteRecord(menu.record.id) }
    }

    Dialog {
        id: discardAll
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 400
        background: Rectangle { radius: 14; color: pane.theme.surface; border.color: pane.theme.outlineVariant }
        contentItem: ColumnLayout {
            spacing: 16
            Text {
                text: qsTr("Discard all %n recording(s) that need attention? Their audio will be deleted.", "", pane.attention.length)
                wrapMode: Text.WordWrap; font: pane.theme.body; color: pane.theme.ink; Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                JButton { theme: pane.theme; text: qsTr("Cancel"); onClicked: discardAll.close() }
                JButton {
                    theme: pane.theme; kind: "destructive"
                    text: qsTr("Discard %n Recording(s)", "", pane.attention.length)
                    onClicked: { pane.attention.forEach(r => jot.deleteRecord(r.id)); discardAll.close() }
                }
            }
        }
    }

    Dialog {
        id: detail
        property var record: ({ id: "", status: "", audioExists: false, text: "" })
        property bool showRaw: false
        readonly property bool distinct: !!(record.raw && record.cleaned && record.raw !== record.cleaned)
        readonly property string shown: showRaw && distinct ? record.raw : (record.cleaned || record.raw || "—")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 520
        padding: 20
        onClosed: jot.stopAudio()
        background: Rectangle { radius: 14; color: pane.theme.surface; border.color: pane.theme.outlineVariant }
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Row {
                    visible: detail.distinct
                    spacing: 0
                    Repeater {
                        model: [qsTr("Cleaned"), qsTr("Raw")]
                        Rectangle {
                            width: 85; height: 26; radius: 6
                            color: (index === 1) === detail.showRaw ? pane.theme.primaryContainer : pane.theme.surfaceContainer
                            Text { anchors.centerIn: parent; text: modelData; font: pane.theme.label; color: pane.theme.ink }
                            MouseArea { anchors.fill: parent; onClicked: detail.showRaw = index === 1 }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                JButton { theme: pane.theme; icon: "edit-copy"; text: qsTr("Copy"); doneText: qsTr("Copied"); onClicked: jot.copyText(detail.shown) }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(120, Math.min(260, detailText.implicitHeight))
                TextArea {
                    id: detailText
                    text: detail.shown
                    readOnly: true
                    selectByMouse: true
                    wrapMode: Text.WordWrap
                    font: pane.theme.bodyLarge
                    color: pane.theme.ink
                    background: null
                }
            }
            RowLayout {
                spacing: 12
                JButton {
                    theme: pane.theme
                    visible: detail.record.audioExists === true
                    icon: jot.playingId === detail.record.id ? "media-playback-stop" : "media-playback-start"
                    text: jot.playingId === detail.record.id ? qsTr("Stop") : qsTr("Play Audio")
                    onClicked: jot.playingId === detail.record.id ? jot.stopAudio() : jot.playAudio(detail.record.id)
                }
                Text {
                    visible: detail.record.audioExists !== true
                    text: qsTr("Audio removed by retention policy")
                    font: pane.theme.labelSmall
                    color: pane.theme.inkVariant
                }
                JButton { theme: pane.theme; text: qsTr("Retry Transcription"); onClicked: jot.retryRecord(detail.record.id) }
                Item { Layout.fillWidth: true }
                JButton { theme: pane.theme; kind: "destructive"; icon: "edit-delete"; onClicked: { jot.deleteRecord(detail.record.id); detail.close() } }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: pane.theme.outlineVariant }
            GridLayout {
                columns: 2
                columnSpacing: 20
                rowSpacing: 4
                Text { visible: !!detail.record.app; text: qsTr("Dictated into"); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                Text { visible: !!detail.record.app; text: detail.record.app || ""; font: pane.theme.labelSmall; color: pane.theme.ink }
                Text { visible: detail.record.duration != null; text: qsTr("Duration"); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                Text { visible: detail.record.duration != null; text: qsTr("%1s").arg((detail.record.duration || 0).toFixed(1)); font: pane.theme.labelSmall; color: pane.theme.ink }
                Text { visible: detail.record.pipeline != null; text: qsTr("Pipeline"); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                Text { visible: detail.record.pipeline != null; text: qsTr("%1s").arg((detail.record.pipeline || 0).toFixed(2)); font: pane.theme.labelSmall; color: pane.theme.ink }
                Text { text: qsTr("Status"); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                Text { text: pane.statusName(detail.record.status || ""); font: pane.theme.labelSmall; color: pane.theme.ink }
                Text { visible: !!detail.record.errorMessage; text: qsTr("Details"); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
                Text { visible: !!detail.record.errorMessage; text: (detail.record.errorMessage || "").substring(0, 160); font: pane.theme.labelSmall; color: pane.theme.ink; wrapMode: Text.WordWrap; Layout.maximumWidth: 380 }
            }
            JButton { theme: pane.theme; kind: "prominent"; text: qsTr("Done"); Layout.alignment: Qt.AlignRight; onClicked: detail.close() }
        }
    }
}
