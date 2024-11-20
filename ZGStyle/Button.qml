import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts
import QtQuick.Controls.impl

import "."

T.Button {
    id: control

    focusPolicy: Qt.NoFocus
    property real backgroundWidth: 32
    property real backgroundHeight: 32
    property real centerOffset: 0
    property bool colorfulIcon: false
    property bool inactive: false
    opacity: inactive ? 0.37 : 1

    icon.color: colorfulIcon ? "transparent" : Style.text
    icon.width: 16
    icon.height: 16

    property bool tooltipReady: false

    onHoveredChanged: tooltipReady = hovered
    onPressed: tooltipReady = false

    ToolTip.visible: tooltipReady && ToolTip.text != ""
    ToolTip.delay: 1000
    ToolTip.timeout: 5000

    background: Rectangle {
        anchors.centerIn: control
        anchors.horizontalCenterOffset: control.centerOffset
        width: backgroundWidth
        height: backgroundHeight
        color: control.enabled && !inactive ? (control.down ? Style.darker : (control.hovered ? Style.lighter : "transparent")) : "transparent"
        radius: 4
    }
}
