// Pop-up picker: model = [{text, value}].
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: root
    property var theme
    property var options: []
    property var current
    signal picked(var value)
    textRole: "text"
    valueRole: "value"
    model: options
    implicitWidth: Math.max(160, contentItem.implicitWidth + 40)
    implicitHeight: 30
    currentIndex: {
        for (var i = 0; i < options.length; ++i) if (options[i].value === current) return i
        return -1
    }
    onActivated: function(index) { root.picked(options[index].value) }
    font: theme.body
    background: Rectangle {
        radius: 7
        color: root.hovered ? root.theme.surfaceContainer : root.theme.surface
        border.color: root.theme.outlineVariant
    }
    contentItem: Text {
        leftPadding: 10
        rightPadding: 24
        text: root.displayText
        font: root.theme.body
        color: root.theme.ink
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: JIcon {
        x: root.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        name: "arrow-down"
        size: 16
        color: root.theme.inkVariant
    }
    popup.background: Rectangle {
        radius: 8
        color: root.theme.surface
        border.color: root.theme.outlineVariant
    }
    delegate: ItemDelegate {
        width: root.popup.width
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: modelData.text
            font: root.theme.body
            color: root.theme.ink
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 5
            color: parent.highlighted ? root.theme.primaryContainer : "transparent"
        }
    }
}
