// macOS-style switch.
import QtQuick

Item {
    id: root
    property var theme
    property bool checked: false
    signal toggled(bool value)
    implicitWidth: 38
    implicitHeight: 22
    opacity: enabled ? 1 : 0.45
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? root.theme.primary : Qt.rgba(root.theme.inkVariant.r, root.theme.inkVariant.g, root.theme.inkVariant.b, 0.3)
        Behavior on color { ColorAnimation { duration: 160 } }
        Rectangle {
            width: 18; height: 18; radius: 9
            y: 2
            x: root.checked ? parent.width - width - 2 : 2
            color: "white"
            Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
