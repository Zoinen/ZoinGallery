import QtQuick
import QtQuick.Controls.Basic as T

T.Slider {
    id: control

    focusPolicy: Qt.NoFocus
    property bool rectangular: false

    background: Rectangle {
        x: control.leftPadding
        y: control.orientation === Qt.Horizontal ? (control.topPadding + control.availableHeight / 2 - height / 2) : control.topPadding
        implicitWidth: control.orientation === Qt.Horizontal ? 200 : control.width
        implicitHeight: control.orientation === Qt.Horizontal ? control.height : 200
        width: control.orientation === Qt.Horizontal ? control.availableWidth : control.width
        height: control.orientation === Qt.Horizontal ? (rectangular ? control.height : 6) : control.availableHeight
        radius: !rectangular ? 2 : 0
        color: "#1f1f1f"

        Rectangle {
            width: control.orientation === Qt.Horizontal ? control.visualPosition * parent.width : parent.width
            height: control.orientation === Qt.Horizontal ? parent.height : (control.visualPosition * parent.height)
            color: "#474747"
            radius: !rectangular ? 2 : 0
        }
    }

    handle: Rectangle {
        visible: !rectangular
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: !rectangular ? 18 : 0
        implicitHeight: 18
        radius: 18
        color: control.pressed ? "#808080" : "#595959"
    }
}
