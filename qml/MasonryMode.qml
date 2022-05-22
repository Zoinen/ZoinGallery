//import QtQuick 2.15
//import QtQuick.Layouts 1.15
//import QtQuick.Controls 2.15 as T
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

import ZoinGallery 1.0

Item {
    id: masonryView

    property bool scrolling: masonryScroll.pressed || scrollAnimation2.running // || flicking || dragging
    onScrollingChanged: {
//        console.log("SCROLLING?", scrolling, scrollAnimation2.running)
    }

    property bool keyHeld: false
    property alias targetHeight: masonryLayout.targetHeight
    property alias currentIndex: masonryLayout.currentIndex

//    visible: topLevelWindow.masonryViewMode

    MasonryLayout {
        id: masonryLayout
        anchors {
            topMargin: 1
            leftMargin: 2

            left: parent.left
            top: parent.top
            bottom: parent.bottom
            right: masonryScroll.left
            rightMargin: 2
        }
        clip: true
        model: fileListModel

        Rectangle {
            anchors.fill: parent
            color: "#202020"
        }

        delegate: BrickItem {
            id: brickDelegate
            property string text
            property string imageId
            property int index

            Rectangle {
                width: parent.width - 2
                height: parent.height - 2
                x: 1
                y: 1
                color: "#202020"
                border.color: "#000"
            }

            Rectangle {
                anchors.fill: parent
                color: "#2980b9"
                visible: masonryLayout.currentIndex === index
            }

            Image {
                id: image
                width: parent.width - 4
                height: parent.height - 4
                x: 2
                y: 2
                visible: imageId !== ""
                fillMode: Image.PreserveAspectCrop
                source: imageId
//                    opacity: 0.1
//                    asynchronous: true
            }

            Rectangle {
                color: Qt.rgba(0, 0, 0, 0.5)
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 22
                visible: brickMouseArea.containsMouse || imageId === ""

                Text {
                    id: textField
                    anchors.fill: parent
                    anchors.margins: 5

                    text: brickDelegate.text

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: "#d1d1d1"
                }
            }

            MouseArea {
                id: brickMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }

        MouseArea {
            property alias animation: scrollAnimation2

            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: (wheel) => {
                         let deltaCell = wheel.angleDelta.y/* / 120*/

//                         deltaCell *= masonryLayout.targetHeight
                         var newContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY) - deltaCell
                         newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
                         scrollAnimation2.from = masonryLayout.contentY
                         scrollAnimation2.to = newContentY
                         scrollAnimation2.restart()
                     }

            NumberAnimation {
                id: scrollAnimation2
                target: masonryLayout
                property: "contentY"
                duration: 200
                easing.type: Easing.OutSine
            }
        }
    }

    ScrollBar {
        id: masonryScroll
        anchors {
            top: masonryLayout.top
            bottom: masonryLayout.bottom
            right: parent.right
        }
        visible: true

        onPositionChanged: {
            if (pressed) {
                masonryLayout.contentY = masonryScroll.position * masonryLayout.contentHeight
//                            console.log("contentY", masonryScroll.position, "*", masonryLayout.contentHeight)
            }
        }

        Connections {
            id: conn
            target: masonryLayout
            function onContentYChanged() {
//                            console.log("onContentY", masonryLayout.contentY, "/", masonryLayout.contentHeight)
                masonryScroll.position = masonryLayout.contentY / masonryLayout.contentHeight
//                            console.log("after")
            }

            function onContentHeightChanged() {
                masonryScroll.size = masonryLayout.height / masonryLayout.contentHeight
//                            conn.onContentYChanged()
            }

            function onHeightChanged() {
                masonryScroll.size = masonryLayout.height / masonryLayout.contentHeight
            }

            // Bug: going to fullscreen, scrolling, then back to windowed gets incorrect pos for scrollbar
        }
    }
}
