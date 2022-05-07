//import QtQuick 2.15
//import QtQuick.Controls 2.15 as T
import QtQuick
import QtQuick.Controls.Basic as T

T.Button {
    id: control

    focusPolicy: Qt.NoFocus

    contentItem: Text {
        text: control.text
        font: control.font
        opacity: enabled ? 1.0 : 0.3
        color: control.hovered ? "#ffffff" : "#e1e1e1"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        opacity: enabled ? 1 : 0.3
        border.color: control.hovered ? "#676767" : "#474747"
        border.width: 1
        color: control.down ? "#191919" : "#292929"
        radius: 2
        layer.enabled: true
    }
}
