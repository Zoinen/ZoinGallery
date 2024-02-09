import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts

import "."

T.TabButton {
    id: control

    focusPolicy: Qt.NoFocus

    icon.color: checked ? Style.buttonIconSelected : Style.text

    background: Item{
        Rectangle {
            anchors.centerIn: parent
            width: 32
            height: 32
            radius: 4
            color: control.down ? Style.darker : (control.hovered ? Style.lighter : "transparent")
        }
    }
}
