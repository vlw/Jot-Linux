// A Breeze icon, recolored. Renders at device pixels, so it stays crisp.
import QtQuick

Image {
    id: root
    property string name
    property color color: "black"
    property int size: 16
    width: size
    height: size
    sourceSize: Qt.size(size * Screen.devicePixelRatio, size * Screen.devicePixelRatio)
    source: name ? "image://icon/" + name + "?" + String(color) : ""
    smooth: true
    asynchronous: false
    cache: true
}
