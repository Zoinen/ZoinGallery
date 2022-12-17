import QtQuick
import QtQuick.Controls.Basic as T

import "."

T.TextField {
    id: control

    property bool hasBackground: true

    color: acceptableInput ? (hovered ? Style.hovered : Style.text) : Style.textError

    background: Rectangle {
        id: bg
        visible: hasBackground
        anchors.fill: parent

        color: Style.controlBackground
        border.color: parent.activeFocus ? Style.focus : bg.color
        border.width: 1
    }

    hoverEnabled: true
    selectByMouse: true
    palette.highlight: Style.focus
    palette.highlightedText: Style.hovered
    palette.brightText: "green"

    leftPadding: 10
    rightPadding: 10
}
