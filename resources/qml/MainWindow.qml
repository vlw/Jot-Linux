// Port of the main window: sidebar (History, Dictionary | Settings: General,
// Dictation, Privacy & Storage, Advanced, About) + detail pane.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 940
    height: 580
    minimumWidth: 880
    minimumHeight: 580
    visible: false
    title: currentTitle()
    color: theme.windowBackground

    Theme { id: theme }

    property string section: "history"
    readonly property var sections: [
        { key: "history", title: qsTr("History"), glyph: "view-history", tile: theme.gBlue, group: "data" },
        { key: "dictionary", title: qsTr("Dictionary"), glyph: "accessories-dictionary-symbolic", tile: theme.tileOrange, group: "data" },
        { key: "general", title: qsTr("General"), glyph: "configure", tile: theme.tileGray, group: "settings" },
        { key: "dictation", title: qsTr("Dictation"), glyph: "waveform", tile: theme.tileTeal, group: "settings" },
        { key: "privacy", title: qsTr("Privacy & Storage"), glyph: "view-private", tile: theme.tileGreen, group: "settings" },
        { key: "advanced", title: qsTr("Advanced"), glyph: "tools", tile: theme.tileIndigo, group: "settings" },
        { key: "about", title: qsTr("About"), glyph: "help-about", tile: theme.tilePink, group: "settings" },
    ]
    function currentTitle() {
        for (var i = 0; i < sections.length; ++i) if (sections[i].key === section) return sections[i].title
        return "Jot"
    }

    Connections {
        target: jot
        function onShowSection(s) { win.section = s }
    }

    Shortcut { sequence: "Ctrl+W"; onActivated: win.close() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- sidebar
        Rectangle {
            Layout.preferredWidth: 273
            Layout.fillHeight: true
            color: theme.sidebarBackground
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 3
                Text {
                    text: "Jot"
                    font: theme.font(21, Font.Medium)
                    color: theme.ink
                    Layout.leftMargin: 14
                    Layout.topMargin: 20
                    Layout.bottomMargin: 12
                }
                Repeater {
                    model: win.sections.slice(0, 2)
                    delegate: SidebarRow { entry: modelData }
                }
                Text {
                    text: qsTr("Settings")
                    font: theme.font(14, Font.Medium)
                    color: theme.inkVariant
                    Layout.leftMargin: 14
                    Layout.topMargin: 16
                    Layout.bottomMargin: 4
                }
                Repeater {
                    model: win.sections.slice(2)
                    delegate: SidebarRow { entry: modelData }
                }
                Item { Layout.fillHeight: true }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.outlineVariant; opacity: 0.6 }

        // ---- detail
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: {
                switch (win.section) {
                case "history": return "HistoryPane.qml"
                case "dictionary": return "DictionaryPane.qml"
                case "general": return "GeneralPane.qml"
                case "dictation": return "DictationPane.qml"
                case "privacy": return "PrivacyPane.qml"
                case "advanced": return "AdvancedPane.qml"
                case "about": return "AboutPane.qml"
                }
                return "GeneralPane.qml"
            }
            onLoaded: item.theme = theme
        }
    }

    component SidebarRow: Rectangle {
        id: row
        property var entry
        readonly property bool selected: win.section === entry.key
        Layout.fillWidth: true
        implicitHeight: 42
        radius: 10
        scale: rowArea.pressed ? 0.98 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        color: selected ? theme.primary : rowArea.pressed ? Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.14) : (rowArea.containsMouse ? Qt.rgba(theme.ink.r, theme.ink.g, theme.ink.b, 0.08) : "transparent")
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10
            Rectangle {
                width: 31; height: 31; radius: 8
                color: row.entry.tile
                JIcon { anchors.centerIn: parent; name: row.entry.glyph; color: "white"; size: 20 }
            }
            Text {
                text: row.entry.title
                font: theme.font(18, Font.Normal)
                color: row.selected ? (theme.dark ? theme.primaryInk : "white") : theme.ink
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: win.section = row.entry.key
        }
    }
}
