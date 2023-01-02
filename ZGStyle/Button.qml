import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts

import "."

T.Button {
    id: control

    focusPolicy: Qt.NoFocus
    property real backgroundWidth: 36
    property real backgroundHeight: 36

    icon.color: Style.hovered
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
        width: backgroundWidth
        height: backgroundHeight
        opacity: enabled ? 1 : 0.3
        color: control.down ? Style.controlBackgroundPressed : (control.hovered ? Style.controlBackground : "transparent")
        radius: 4
    }
}
