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
            color: "#2d2d2d"
        }

        Component.onCompleted: {
//            viewerController.setThumbnailResolution(Qt.size(width, targetHeight), dpr)
//            console.log("NEW render size", width, targetHeight, dpr)
        }

        onTargetHeightChanged: {
//            viewerController.setThumbnailResolution(Qt.size(width, targetHeight), dpr)
//            console.log("NEW render size", width, targetHeight, dpr)
        }

        delegate: BrickItem {
            id: brickDelegate
            property alias text: textField.text
            property alias imageId: image.source
            property int index

            property bool singleMoveAnimationEnabled
            property bool behaviorAnimationEnabled: singleMoveAnimationEnabled && !masonryView.scrolling && !masonryView.keyHeld && brickDelegate.visible

            Rectangle {
                anchors.fill: parent
//                color: "#fff"
//                color: "transparent"
                border.width: 2
                border.color: masonryLayout.currentIndex === index ? "#2980b9" : "#202020"

                Rectangle {
                    width: parent.width - 2
                    height: parent.height - 2
                    x: 1
                    y: 1
//                    visible: image.source == ""
                    color: "#202020"
                    border.color: masonryLayout.currentIndex === index ? "#2980b9" : "#000"
                }

                Image {
                    id: image
                    width: parent.width - 4
                    height: parent.height - 4
                    x: 2
                    y: 2
                    fillMode: Image.PreserveAspectCrop
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
                    visible: brickMouseArea.containsMouse || image.source == ""

                    Text {
                        id: textField
                        anchors.fill: parent
                        anchors.margins: 5

                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        color: "#d1d1d1"
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 5

                    color: "#d1d1d1"
                    text: index
                }

            }

            MouseArea {
                id: brickMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }


            Behavior on x {
                enabled: brickDelegate.behaviorAnimationEnabled
                NumberAnimation { properties: "x"; duration: 300; easing.type: Easing.InOutQuad }
            }
            Behavior on y {
                enabled: brickDelegate.behaviorAnimationEnabled
                NumberAnimation { properties: "y"; duration: 300; easing.type: Easing.InOutQuad }
            }

            Behavior on width {
                enabled: brickDelegate.behaviorAnimationEnabled
                NumberAnimation { properties: "width"; duration: 300; easing.type: Easing.InOutQuad }
            }
            Behavior on height {
                enabled: brickDelegate.behaviorAnimationEnabled
                SequentialAnimation {
                    NumberAnimation { properties: "height"; duration: 300; easing.type: Easing.InOutQuad }
                    ScriptAction {
                        script: {
//                            console.log("FINISHED")
                            brickDelegate.singleMoveAnimationEnabled = false
                        }
                    }
                }
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
