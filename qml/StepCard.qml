import QtQuick
import QtQuick.Layouts
import org.mauikit.controls as Maui

Item {
    id: root

    default property alias content: contentColumn.data
    implicitHeight: card.implicitHeight

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        border.width: 1
        implicitHeight: contentColumn.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.medium
        }
    }
}
