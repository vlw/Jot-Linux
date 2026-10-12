// Slider in the app's style.
import QtQuick
import QtQuick.Controls.Basic

Slider {
    id: root
    property var theme
    implicitWidth: 220
    implicitHeight: 24
    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 4
        radius: 2
        color: Qt.rgba(root.theme.inkVariant.r, root.theme.inkVariant.g, root.theme.inkVariant.b, 0.25)
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: root.theme.primary
        }
    }
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: 18; height: 18; radius: 9
        color: "white"
        border.color: root.pressed ? root.theme.primary : root.theme.outlineVariant
        border.width: root.pressed ? 2 : 1
    }
}
