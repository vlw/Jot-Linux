// Plain rounded text field.
import QtQuick

Rectangle {
    id: root
    property var theme
    property alias text: input.text
    property string placeholder: ""
    property bool secret: false
    property bool code: false
    signal accepted()
    signal edited()
    implicitWidth: 260
    implicitHeight: 30
    radius: 7
    color: root.theme.surface
    border.color: input.activeFocus ? root.theme.primary : root.theme.outlineVariant
    border.width: input.activeFocus ? 2 : 1
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        font: root.code ? root.theme.code : root.theme.body
        color: root.theme.ink
        selectionColor: root.theme.primaryContainer
        selectedTextColor: root.theme.primaryContainerInk
        echoMode: root.secret ? TextInput.Password : TextInput.Normal
        onAccepted: root.accepted()
        onTextEdited: root.edited()
    }
    Text {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: input.text === "" && !input.inputMethodComposing
        text: root.placeholder
        font: root.code ? root.theme.code : root.theme.body
        color: Qt.rgba(root.theme.inkVariant.r, root.theme.inkVariant.g, root.theme.inkVariant.b, 0.6)
        elide: Text.ElideRight
    }
    function forceFocus() { input.forceActiveFocus() }
}
