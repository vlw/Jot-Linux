// One labelled row inside a JSection.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    property var theme
    property string label: ""
    property bool divider: true
    default property alias trailing: slot.data
    Layout.fillWidth: true
    Layout.minimumHeight: 40
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 12
    Text {
        text: root.label
        font: root.theme.body
        color: enabled ? root.theme.ink : root.theme.inkVariant
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        visible: root.label !== ""
    }
    RowLayout { id: slot; spacing: 8 }
}
