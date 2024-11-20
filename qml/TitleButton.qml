import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Button {
    id: titleButton

    objectName: "titleBarButton"

    implicitHeight: titleBar.height
    implicitWidth: 46

    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0
    leftInset: 0
    topInset: 0
    rightInset: 0
    bottomInset: 0

    opacity: topLevelWindow.active || titleButton.hoveredOverride ? 1 : 0.4
    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
    }

    property alias source: titleButton.icon.source
    property bool pressedOverride: pressed
    property bool hoveredOverride: hovered
    property color backgroundColor: {
        if (!titleButton.enabled) {
            return "gray";
        }
        if (titleButton.pressedOverride) {
            return Style.darker;
        }
        if (titleButton.hoveredOverride) {
            return Style.lighter;
        }
        return "transparent";
    }

    icon.width: 10
    icon.height: 10
    icon.color: Style.text

    // property alias source: image.source
    //     contentItem: Item {
    //     Image {
    //         id: image
    //         anchors.centerIn: parent
    //         mipmap: true
    //         width: 10
    //         height: 10
    //     }
    // }
    background: Rectangle {
        color: backgroundColor
    }
}
