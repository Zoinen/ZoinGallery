import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

T.MenuItem {
    id: menuItem
    implicitWidth: 200
    implicitHeight: 36

    focusPolicy: Qt.NoFocus

    contentItem: Text {
        leftPadding: menuItem.indicator.width + 17
        rightPadding: menuItem.arrow.width
        text: menuItem.text
        font: menuItem.font
        opacity: enabled ? 1.0 : 0.3
        color: Style.text
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 36
        opacity: enabled ? 1 : 0.3
        color: menuItem.highlighted ? Style.lighter : "transparent"
        radius: 4
    }
}
