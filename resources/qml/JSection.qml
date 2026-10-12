// Grouped-form section: header above, rounded card, footer caption below.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var theme
    property string header: ""
    property string footer: ""
    default property alias content: card.data
    spacing: 6
    Layout.fillWidth: true

    Text {
        visible: root.header !== ""
        text: root.header
        font: root.theme.label
        color: root.theme.inkVariant
        Layout.leftMargin: 4
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: card.implicitHeight + 8
        radius: 10
        color: root.theme.surface
        border.color: Qt.rgba(root.theme.outlineVariant.r, root.theme.outlineVariant.g, root.theme.outlineVariant.b, 0.5)
        ColumnLayout {
            id: card
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            spacing: 0
        }
    }
    Text {
        visible: root.footer !== ""
        text: root.footer
        wrapMode: Text.WordWrap
        font: root.theme.font(11, Font.Normal)
        color: root.theme.inkVariant
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        textFormat: Text.PlainText
    }
}
