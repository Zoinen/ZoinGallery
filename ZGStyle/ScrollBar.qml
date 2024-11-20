import QtQuick
import QtQuick.Controls.Basic as T

T.ScrollBar {
    id: scroll
    policy: T.ScrollBar.AlwaysOn
    visible: horizontal ? parent.contentWidth > parent.width : parent.contentHeight > parent.height

    implicitWidth: 15
    implicitHeight: 15
    hoverEnabled: true

    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0

    contentItem: MouseArea {
        id: contentItem
        implicitWidth: 15
        implicitHeight: 15
        hoverEnabled: true
        acceptedButtons: Qt.NoButton

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: 4
            color: scroll.hovered ? (scroll.pressed ? Style.scrollBarHandlePressed : (contentItem.containsMouse ? Style.scrollBarHandleHovered : Style.scrollBarHandleBackgroundHovered)) : Style.scrollBarHandle
        }
    }

    background: Rectangle {
        color: (scroll.hovered || scroll.pressed) ? Style.lighter : "transparent"
        radius: 15
    }
}
