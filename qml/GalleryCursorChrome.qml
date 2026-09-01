pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: chrome
    required property bool active
    required property bool cursorVisible
    required property rect geometry
    required property real cornerRadius
    required property color fillColor
    required property color outlineColor
    required property real outlineWidth

    clip: true
    visible: active && cursorVisible

    Item {
        anchors.fill: parent
        z: -1

        Rectangle {
            objectName: "galleryCursorChromeUnderlay"
            x: chrome.geometry.x
            y: chrome.geometry.y
            width: chrome.geometry.width
            height: chrome.geometry.height
            radius: chrome.cornerRadius
            antialiasing: true
            color: chrome.fillColor
        }
    }

    Item {
        anchors.fill: parent
        z: 1

        Rectangle {
            objectName: "galleryCursorChromeBorder"
            readonly property bool visualBorderPixelAligned:
                border.pixelAligned
            x: chrome.geometry.x
            y: chrome.geometry.y
            width: chrome.geometry.width
            height: chrome.geometry.height
            radius: chrome.cornerRadius
            antialiasing: true
            color: "transparent"
            border.width: chrome.outlineWidth
            border.pixelAligned: true
            border.color: chrome.outlineColor
        }
    }
}
