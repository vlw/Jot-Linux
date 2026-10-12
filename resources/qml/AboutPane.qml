// About pane.
import QtQuick
import QtQuick.Layouts

Item {
    id: pane
    property var theme
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16
        Image { source: "qrc:/icons/jot.png"; sourceSize: Qt.size(96, 96); Layout.alignment: Qt.AlignHCenter }
        ColumnLayout {
            spacing: 4
            Layout.alignment: Qt.AlignHCenter
            Text { text: "Jot"; font: pane.theme.display; color: pane.theme.ink; Layout.alignment: Qt.AlignHCenter }
            Text {
                text: qsTr("Version %1 · Linux edition").arg(jot.version)
                font: pane.theme.body
                color: pane.theme.inkVariant
                Layout.alignment: Qt.AlignHCenter
            }
        }
        RowLayout {
            spacing: 4
            Layout.alignment: Qt.AlignHCenter
            Text { text: qsTr("Created by"); font: pane.theme.body; color: pane.theme.inkVariant }
            JButton { theme: pane.theme; kind: "link"; text: "Vladimir Bely"; onClicked: jot.openUrl("https://github.com/vlw/Jot-Linux") }
        }
        RowLayout {
            spacing: 16
            Layout.alignment: Qt.AlignHCenter
            JButton { theme: pane.theme; kind: "link"; text: qsTr("Source"); onClicked: jot.openUrl("https://github.com/vlw/Jot-Linux") }
            JButton { theme: pane.theme; kind: "link"; text: qsTr("Privacy"); onClicked: jot.openUrl("https://github.com/google-gemini/jot-gemini-transcribe-macOS/blob/main/docs/PRIVACY.md") }
            JButton { theme: pane.theme; kind: "link"; text: qsTr("Report a bug"); onClicked: jot.openUrl("https://github.com/vlw/Jot-Linux/issues") }
        }
        Text {
            text: qsTr("Open source under the Apache License 2.0.\nThis is not an officially supported Google product.")
            horizontalAlignment: Text.AlignHCenter
            font: pane.theme.labelSmall
            color: pane.theme.inkVariant
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
        }
    }
}
