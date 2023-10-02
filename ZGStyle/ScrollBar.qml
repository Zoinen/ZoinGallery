import QtQuick
import QtQuick.Controls.Basic as T

T.ScrollBar {
    id: scroll
    policy: T.ScrollBar.AlwaysOn
    visible: parent.contentHeight > parent.height

    implicitWidth: 15
    hoverEnabled: true

    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0

    contentItem: Item {
        id: contentItem
        implicitWidth: 15

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: 4
            color: scroll.pressed ? Style.scrollBarHandlePressed : Style.scrollBarHandle
        }
    }

    background: Rectangle {
        color: Style.scrollBarBackground
    }
}
