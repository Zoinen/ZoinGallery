import QtQuick
import QtQuick.Controls.Basic as T

import "."

T.TabBar {
    id: control

    focusPolicy: Qt.NoFocus
    leftPadding: 1
    rightPadding: 1

    background: Item {
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            height: 32
            radius: 4
            color: Style.tabBarBackground
            border.width: 1
            border.color: Style.tabBarBorder
        }
    }
}
