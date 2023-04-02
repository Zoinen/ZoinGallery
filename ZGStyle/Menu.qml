import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

T.Menu {
    padding: 4

    background: Rectangle {
        // TODO: Fix width for back/forward menu
        implicitWidth: 200
        implicitHeight: 36
        color: "#2c2c2c"
        border.color: "#33000000"
        radius: 7
    }
}
