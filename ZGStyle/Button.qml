import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts

import "."

T.Button {
    id: control

    focusPolicy: Qt.NoFocus
    property real backgroundWidth: 32
    property real backgroundHeight: 32
    property real centerOffset: 0

    icon.color: enabled ? (control.down ? Style.textPressed : Style.hovered) : Style.buttonDisabled
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
        opacity: enabled ? 1 : 0.3
        color: control.enabled ? (control.down ? Style.controlBackgroundPressed : (control.hovered ? Style.controlBackground : "transparent")) : "transparent"
        radius: 4
    }
}
