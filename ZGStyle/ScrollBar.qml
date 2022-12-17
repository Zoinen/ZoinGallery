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

    contentItem: Rectangle {
        id: contentItem
        implicitWidth: 15
        color: scroll.pressed ? Style.midDarkLighter : Style.midDark
    }

    background: Rectangle {
        color: Style.controlBackground
    }
}
