import QtQuick
import QtQuick.Controls.Basic as T

T.Slider {
    id: control

    focusPolicy: Qt.NoFocus

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: control.height
        width: control.availableWidth
        height: 6
        radius: 2
        color: "#292929"

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            color: "#474747"
            radius: 2
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 18
        implicitHeight: 18
        radius: 18
        color: control.pressed ? "#878787" : "#676767"
    }
}
