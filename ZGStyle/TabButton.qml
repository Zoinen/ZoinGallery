import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts

import "."

T.TabButton {
    id: control

    focusPolicy: Qt.NoFocus

    icon.color: Style.text

    property bool tooltipReady: false

    onHoveredChanged: tooltipReady = hovered
    onPressed: tooltipReady = false

    ToolTip.visible: tooltipReady && ToolTip.text != ""
    ToolTip.delay: 1000
    ToolTip.timeout: 5000

    background: Item{
        Rectangle {
            anchors.centerIn: parent
            width: 32
            height: 30
            radius: 3
            // border.width: control.checked ? 1 : 0
            // border.color: Style.tabBarBorder
            color: control.down ? Style.darker : (control.hovered ? Style.lighter : (control.checked ? Qt.rgba(1, 1, 1, 0.06) : "transparent"))

            Rectangle {
                anchors {
                    horizontalCenter: parent.horizontalCenter
                    bottom: parent.bottom
                    bottomMargin: 2
                }
                width: 6
                height: 3
                radius: 6

                visible: control.checked
                color: Qt.darker(Style.accentColor, 1.17)
            }
        }
    }
}
