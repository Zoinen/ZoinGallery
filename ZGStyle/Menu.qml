import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

T.Menu {
    padding: 4

    background: Rectangle {
        // TODO: Fix width for back/forward menu
        implicitWidth: 200
        implicitHeight: 36
        color: Style.menuBackground
        border.color: Style.menuBorder
        radius: 7
    }
}
