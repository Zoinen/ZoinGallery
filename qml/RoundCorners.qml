import QtQuick
import QtQuick.Layouts

Item {
    property color backgroundColor

    Image {
        anchors.left: parent.left
        anchors.top: parent.top
        source: "image://resources/top_left_round_corner|" + dpr + "|" + backgroundColor
        cache: true
        sourceSize.width: 4 * dpr
        sourceSize.height: 4 * dpr
    }

    Image {
        anchors.right: parent.right
        anchors.top: parent.top
        source: "image://resources/top_right_round_corner|" + dpr + "|" + backgroundColor
        cache: true
        sourceSize.width: 4 * dpr
        sourceSize.height: 4 * dpr
    }

    Image {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        source: "image://resources/bottom_left_round_corner|" + dpr + "|" + backgroundColor
        cache: true
        sourceSize.width: 4 * dpr
        sourceSize.height: 4 * dpr
    }

    Image {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        source: "image://resources/bottom_right_round_corner|" + dpr + "|" + backgroundColor
        cache: true
        sourceSize.width: 4 * dpr
        sourceSize.height: 4 * dpr
    }
}
