// Scrollable settings pane scaffold.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ScrollView {
    id: root
    property var theme
    default property alias content: column.data
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        id: column
        width: root.availableWidth - 48
        x: 24
        spacing: 18
        Item { Layout.preferredHeight: 6 }
    }
}
