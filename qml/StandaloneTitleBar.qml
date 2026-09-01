import QtQuick
import QtQuick.Window
import QtQuick.Layouts

import ZoinGallery 1.0
import QWindowKit 1.0

pragma ComponentBehavior: Bound

Item {
    id: titleBar

    required property QtObject hostWindow
    required property QtObject windowAgentObject
    required property bool quickWindowKitEnabled
    property string shellState: "thumbnails"
    property int animationDuration: 150
    property int easingType: Easing.OutSine

    readonly property alias systemButtonsLayout: titleBarButtonsLayout
    readonly property alias minimizeControl: minButton
    readonly property alias maximizeControl: maxButton
    readonly property alias closeControl: closeButton
    readonly property alias macSystemButtonRegion: macSystemButtonArea

    anchors {
        left: parent.left
        right: parent.right
        top: parent.top
    }
    property int viewerHeight: 32
    property int thumbnailsHeight: 48

    height: titleBar.shellState === "thumbnails" ? thumbnailsHeight : viewerHeight
    Behavior on height {
        NumberAnimation {
            duration: titleBar.animationDuration
            easing.type: titleBar.easingType
        }
    }

    z: 1
    visible: titleBar.quickWindowKitEnabled

    Item {
        id: macSystemButtonArea
        visible: false
        x: titleBar.hostWindow.macSystemButtonAreaLeftMargin
        y: 0
        width: 70
        height: titleBar.height
    }

    RowLayout {
        id: titleBarButtonsLayout
        anchors {
            top: parent.top
            right: parent.right
            bottom: parent.bottom
        }
        spacing: 0
        visible: !titleBar.hostWindow.useMacNativeTitleBar

        TitleButton {
            id: minButton

            Layout.alignment: Qt.AlignTop

            source: "qrc:/ZoinGallery/resources/WindowMinimize.svg"
            onClicked: titleBar.hostWindow.showMinimized()
            Component.onCompleted: {
                if (!titleBar.hostWindow.useMacNativeTitleBar) {
                    titleBar.windowAgentObject.setSystemButton(WindowAgent.Minimize, minButton)
                }
            }
        }

        TitleButton {
            id: maxButton

            Layout.alignment: Qt.AlignTop

            source: titleBar.hostWindow.visibility === Window.Maximized ? "qrc:/ZoinGallery/resources/WindowRestore.svg" :
                    titleBar.hostWindow.visibility === Window.FullScreen ? "qrc:/ZoinGallery/resources/WindowFullscreen.svg" :"qrc:/ZoinGallery/resources/WindowMaximize.svg"
            onClicked: {
                if (titleBar.hostWindow.visibility === Window.FullScreen) {
                    titleBar.hostWindow.toggleFullscreen()
                }
                else if (titleBar.hostWindow.visibility === Window.Maximized) {
                    titleBar.hostWindow.showNormal()
                }
                else {
                    titleBar.hostWindow.showMaximized()
                }
            }
            Component.onCompleted: {
                if (!titleBar.hostWindow.useMacNativeTitleBar) {
                    titleBar.windowAgentObject.setSystemButton(WindowAgent.Maximize, maxButton)
                }
            }
        }

        TitleButton {
            id: closeButton

            Layout.alignment: Qt.AlignTop

            source: "qrc:/ZoinGallery/resources/WindowClose.svg"
            icon.color: closeButton.hovered ? Style.closeButtonHoveredIcon : Style.text
            backgroundColor: {
                if (!closeButton.enabled) {
                    return "gray";
                }
                if (closeButton.pressed) {
                    return Style.closeButtonPressed;
                }
                if (closeButton.hovered) {
                    return Style.closeButtonHovered;
                }
                return "transparent";
            }
            onClicked: titleBar.hostWindow.close()

            Component.onCompleted: {
                if (!titleBar.hostWindow.useMacNativeTitleBar) {
                    titleBar.windowAgentObject.setSystemButton(WindowAgent.Close, closeButton)
                }
            }
        }
    }

}
