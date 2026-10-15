// Port of DictionaryView.swift.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: pane
    property var theme
    property int dr: jot.dictRev
    property string query: ""
    property var entries: (dr, jot.dictionaryEntries(query))
    property string feedback: ""

    function say(msg) { feedback = msg; feedbackTimer.restart() }
    Timer { id: feedbackTimer; interval: 3000; onTriggered: pane.feedback = "" }

    function add() {
        if (term.text.trim() === "") return
        var r = jot.addTerm(term.text, forms.text)
        if (r !== "") say(r)
        term.text = ""
        forms.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 16
            implicitHeight: 54
            radius: 12
            color: pane.theme.surfaceContainer
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                JField { id: term; theme: pane.theme; Layout.fillWidth: true; placeholder: qsTr("Add a word or phrase…"); onAccepted: pane.add() }
                JField { id: forms; theme: pane.theme; Layout.fillWidth: true; placeholder: qsTr("Transcribed as… (optional, comma-separated)"); onAccepted: pane.add() }
                Rectangle {
                    width: 26; height: 26; radius: 13
                    color: term.text.trim() !== "" ? pane.theme.primary : pane.theme.inkVariant
                    opacity: term.text.trim() !== "" ? 1 : 0.5
                    JIcon { anchors.centerIn: parent; name: "list-add"; color: "white"; size: 16 }
                    MouseArea { anchors.fill: parent; enabled: term.text.trim() !== ""; onClicked: pane.add() }
                }
            }
        }

        JField {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            theme: pane.theme
            placeholder: qsTr("Search")
            visible: (pane.dr, jot.dictionaryCount()) > 0
            onEdited: pane.query = text
        }

        Item {
            visible: (pane.dr, jot.dictionaryCount()) === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Column {
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 12
                JIcon { name: "accessories-dictionary-symbolic"; size: 32; color: pane.theme.inkVariant; anchors.horizontalCenter: parent.horizontalCenter }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: qsTr("Teach it your words"); font: pane.theme.title; color: pane.theme.ink }
                Text {
                    width: parent.width
                    text: qsTr("Names, jargon, product terms — add them once,\nthey're spelled right in every dictation.")
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font: pane.theme.body
                    color: pane.theme.inkVariant
                }
            }
        }

        ListView {
            id: list
            visible: (pane.dr, jot.dictionaryCount()) > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: pane.entries
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                width: list.width - 32
                x: 16
                height: modelData.forms.length > 0 ? 50 : 38
                radius: 8
                color: pane.theme.surface
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 12
                    IconButton {
                        name: modelData.starred ? "starred-symbolic" : "non-starred-symbolic"
                        tint: modelData.starred ? pane.theme.gYellow : Qt.rgba(pane.theme.inkVariant.r, pane.theme.inkVariant.g, pane.theme.inkVariant.b, 0.5)
                        onClicked: jot.toggleStar(modelData.id)
                        ToolTip.visible: false
                    }
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text { text: modelData.term; font: pane.theme.body; color: pane.theme.ink }
                        Text {
                            visible: modelData.forms.length > 0
                            text: modelData.forms.map(f => "\"" + f + "\"").join(", ") + " → " + modelData.term
                            font: pane.theme.labelSmall
                            color: pane.theme.inkVariant
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    IconButton {
                        name: "document-edit"
                        onClicked: { editor.entry = modelData; editorText.text = modelData.forms.join("\n"); editor.open() }
                    }
                    IconButton {
                        name: "window-close"
                        onClicked: jot.removeTerm(modelData.id)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Text {
                text: pane.feedback !== "" ? pane.feedback
                    : qsTr("%n word(s)", "", (pane.dr, jot.dictionaryCount()))
                color: pane.feedback !== "" ? pane.theme.primary : pane.theme.inkVariant
                font: pane.theme.labelSmall
            }
            Item { Layout.fillWidth: true }
            JButton { theme: pane.theme; kind: "link"; text: qsTr("Import CSV…"); onClicked: importDialog.open() }
            JButton { theme: pane.theme; kind: "link"; text: qsTr("Export CSV…"); enabled: (pane.dr, jot.dictionaryCount()) > 0; onClicked: exportDialog.open() }
        }
    }

    Dialog {
        id: editor
        property var entry: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 16
        background: Rectangle { radius: 12; color: pane.theme.surface; border.color: pane.theme.outlineVariant }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: qsTr("%1 is transcribed as… (one per line)").arg(editor.entry.term || ""); font: pane.theme.labelSmall; color: pane.theme.inkVariant }
            Rectangle {
                implicitWidth: 280; implicitHeight: 160
                radius: 6
                color: pane.theme.surfaceContainer
                TextEdit {
                    id: editorText
                    anchors.fill: parent
                    anchors.margins: 8
                    font: pane.theme.body
                    color: pane.theme.ink
                    wrapMode: TextEdit.Wrap
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                JButton { theme: pane.theme; text: qsTr("Cancel"); onClicked: editor.close() }
                JButton { theme: pane.theme; kind: "prominent"; text: qsTr("Save"); onClicked: { jot.setForms(editor.entry.id, editorText.text); editor.close() } }
            }
        }
    }

    FileDialog {
        id: importDialog
        nameFilters: [qsTr("CSV or text (*.csv *.txt)"), qsTr("All files (*)")]
        onAccepted: {
            var n = jot.importCSV(selectedFile)
            pane.say(n === 0 ? qsTr("Nothing new to import") : qsTr("Imported %n change(s)", "", n))
        }
    }
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        currentFile: "jot-dictionary.csv"
        nameFilters: [qsTr("CSV (*.csv)")]
        onAccepted: {
            var n = jot.exportCSV(selectedFile)
            pane.say(n < 0 ? qsTr("Export failed — couldn't write the file") : qsTr("Exported %n word(s)", "", n))
        }
    }

    component IconButton: Rectangle {
        id: ib
        property string name
        property color tint: pane.theme.inkVariant
        signal clicked()
        implicitWidth: 26; implicitHeight: 26; radius: 6
        color: ibArea.pressed ? Qt.rgba(pane.theme.ink.r, pane.theme.ink.g, pane.theme.ink.b, 0.14)
             : ibArea.containsMouse ? Qt.rgba(pane.theme.ink.r, pane.theme.ink.g, pane.theme.ink.b, 0.07) : "transparent"
        scale: ibArea.pressed ? 0.9 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        JIcon { anchors.centerIn: parent; name: ib.name; color: ib.tint; size: 16 }
        MouseArea { id: ibArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ib.clicked() }
    }
}
