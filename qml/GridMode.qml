//import QtQuick 2.15
//import QtQuick.Layouts 1.15
//import QtQuick.Controls 2.15 as T
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

import ZoinGallery 1.0

Item {
    id: gridRoot
    property bool secondChangeNotify: true
    property int zoom: 19
    property int zoomMin: 19
    property int zoomMax: 3

    property real cellWidth//: (width - 15) / columnCount
    Binding on cellWidth {
        value: (width - 15) / columnCount
        when: visible
    }


    property real cellHeight: cellWidth - 44 + 40
    property int thumbnailWidth: cellWidth - cellPadding * 2
    property int thumbnailHeight: cellHeight - cellPadding * 2 - captionHeight
    property int captionHeight: cellWidth > 250 ? 14 + 10 : cellWidth > 150 ? 14 + 5 : 14
    property int cellPadding: cellWidth > 250 ? 10 : cellWidth > 150 ? 7 : 5
    property alias columnCount: gridRoot.zoom

    onThumbnailWidthChanged: {
        secondChangeNotify = !secondChangeNotify
        if (secondChangeNotify && thumbnailWidth > 0 && thumbnailHeight > 0) {
            console.log("set thumbs size", thumbnailWidth, thumbnailHeight, secondChangeNotify)
            viewerController.setThumbnailResolution(Qt.size(gridRoot.thumbnailWidth, gridRoot.thumbnailHeight), dpr)
        }
    }
    onThumbnailHeightChanged: {
        secondChangeNotify = !secondChangeNotify
        if (secondChangeNotify && thumbnailWidth > 0 && thumbnailHeight > 0) {
            console.log("set thumbs size", thumbnailWidth, thumbnailHeight, secondChangeNotify)
            viewerController.setThumbnailResolution(Qt.size(gridRoot.thumbnailWidth, gridRoot.thumbnailHeight), dpr)
        }
    }

    Behavior on captionHeight {
        NumberAnimation { properties: "captionHeight"; duration: 300; easing.type: Easing.InOutQuad }
    }

    Behavior on cellPadding {
        NumberAnimation { properties: "cellPadding"; duration: 300; easing.type: Easing.InOutQuad }
    }

    GridView {
        id: gridView

        anchors {
            fill: parent
            leftMargin: 1
            topMargin: 1
        }
        //    visible: !gridRoot.masonryViewMode

        cellWidth: gridRoot.cellWidth
        cellHeight: gridRoot.cellHeight

        clip: true
        highlight: Item {
            id: highlightItem
            Binding { target: highlightOnTop; property: "x"; value: x }
            Binding { target: highlightOnTop; property: "y"; value: y - gridView.contentY }
            Binding { target: highlightOnTop; property: "width"; value: width - 1 }
            Binding { target: highlightOnTop; property: "height"; value: height - 1 }
        }

        Rectangle {
            id: highlightOnTop
            color: "transparent"
            border.color: "#2980b9"
            border.width: 1
            z: 2
        }

        Keys.onPressed: (event) => {
                            event.accepted = true
                            if (event.key === Qt.Key_Home) {
                                currentIndex = 0
                            }
                            else if (event.key === Qt.Key_End) {
                                currentIndex = count - 1
                            }
                            else if (event.key === Qt.Key_PageUp) {
                                currentIndex = Math.max(0, currentIndex - columnCount * Math.floor(gridView.height / cellHeight))
                            }
                            else if (event.key === Qt.Key_PageDown) {
                                currentIndex = Math.min(count - 1, currentIndex + columnCount * Math.floor(gridView.height / cellHeight))
                            }
                            else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                                gridView.highlightMoveDuration = 0
                                currentIndex = viewerController.up()
                                gridView.highlightMoveDuration = 150
                            }
                            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                                if (currentItem.folderRoleProperty) {
                                    gridView.highlightMoveDuration = 0
                                    viewerController.cd(currentItem.displayRoleProperty)
                                    currentIndex = 0
                                    gridView.highlightMoveDuration = 150
                                }
                            }
                            else if (event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) {
                                gridRoot.zoom.value = Math.max(gridRoot.zoom.value - 1, gridRoot.zoomMax)
                            }
                            else if (event.key === Qt.Key_Minus) {
                                gridRoot.zoom.value = Math.min(gridRoot.zoom.value + 1, gridRoot.zoomMin)
                            }
                            else {
                                event.accepted = false
                            }

                            if (event.accepted ||
                                event.key === Qt.Key_Left || event.key === Qt.Key_Right ||
                                event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                                gridView.keyHeld = true
                            }
                            delayedAnimationEnabler.stop()
                        }

        Keys.onReleased: (event) => {
                             delayedAnimationEnabler.start()
                         }

        Timer {
            id: delayedAnimationEnabler
            interval: 150

            onTriggered: {
                gridView.keyHeld = false
            }
        }

        focus: true
        currentIndex: 0
        cacheBuffer: 2000

        model: fileListModel
        keyNavigationEnabled: true

        T.ScrollBar.vertical: ScrollBar {
            id: verticalScrollBar
        }

        property bool scrolling: verticalScrollBar.pressed || scrollAnimation.running || flicking || dragging
        property bool keyHeld: false
        property int lastTopIndex: 0
        onLastTopIndexChanged: {
            if (!fileListModel.generationFinished) {
                fileListModel.setNextRequestIndex(lastTopIndex)
            }
        }

        onContentYChanged: {
            if (!fileListModel.generationFinished) {
                lastTopIndex = indexAt(0, contentY)
            }
        }

        MouseArea {
            property alias animation: scrollAnimation

            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: (wheel) => {
                         let deltaCell = wheel.angleDelta.y / 120
                         if (wheel.modifiers & Qt.ControlModifier) {
                             gridRoot.zoom.value = Math.max(gridRoot.zoom.value - deltaCell, gridRoot.zoomMax)
                         }
                         else {
                             wheel.accepted = false
                         }
                         //                         else {
                         //                             deltaCell *= gridView.cellHeight / 2
                         //                             var newContentY = (scrollAnimation.running ? scrollAnimation.to : gridView.contentY) - deltaCell
                         //                             newContentY = Math.max(0, Math.min(gridView.contentHeight - gridView.height, newContentY))
                         //                             scrollAnimation.from = gridView.contentY
                         //                             scrollAnimation.to = newContentY
                         //                             scrollAnimation.restart()
                         //                         }
                     }

            NumberAnimation {
                id: scrollAnimation
                target: gridView
                property: "contentY"
                duration: 250
                easing.type: Easing.OutSine
            }
        }

        delegate: Item {
            id: delegateWrapper
            width: gridView.cellWidth
            height: gridView.cellHeight
            property bool sizeAnimationEnabled: false //loadCompleted && !gridView.scrolling && !gridView.keyHeld
            property bool moveAnimationEnabled: false // loadCompleted && !gridView.scrolling && !gridView.keyHeld
            property bool loadCompleted: false
            property string displayRoleProperty: displayRole
            property bool folderRoleProperty: folderRole

            Item {
                id: delegateContent
                x: delegateWrapper.x
                y: delegateWrapper.y - gridView.contentY
                width: delegateWrapper.width
                height: delegateWrapper.height
                parent: gridView

                Component.onCompleted: {
                    delegateWrapperPostInit.start()
                }

                Timer {
                    id: delegateWrapperPostInit
                    interval: 0

                    onTriggered: {
                        delegateWrapper.loadCompleted = true
                    }
                }

                Behavior on x {
                    enabled: delegateWrapper.moveAnimationEnabled
                    NumberAnimation { properties: "x"; duration: 300; easing.type: Easing.InOutQuad }
                }
                Behavior on y {
                    enabled: delegateWrapper.moveAnimationEnabled
                    NumberAnimation { properties: "y"; duration: 300; easing.type: Easing.InOutQuad }
                }

                Behavior on width {
                    enabled: delegateWrapper.sizeAnimationEnabled
                    NumberAnimation { properties: "width"; duration: 300; easing.type: Easing.InOutQuad }
                }
                Behavior on height {
                    enabled: delegateWrapper.sizeAnimationEnabled
                    NumberAnimation { properties: "height"; duration: 300; easing.type: Easing.InOutQuad }
                }


                Rectangle {
                    anchors {
                        fill: parent
                        rightMargin: 1
                        bottomMargin: 1
                    }
                    color: cellMouse.containsMouse ? "#3B3B3B" : "#333333"
                }

                /*BorderImage {
                        id: shadow
                        anchors {
                            fill: thumbnail
                            leftMargin: -29 / dpr
                            topMargin: -25 / dpr
                            rightMargin: -29 / dpr
                            bottomMargin: -33 / dpr
                        }
                        //                                anchors.centerIn: thumbnail
                        //                                anchors.verticalCenterOffset: 4 / dpr
                        //                                width: thumbnail.paintedWidth + (29 * 2) / dpr
                        //                                height: thumbnail.paintedHeight + (25 + 33) / dpr
                        visible: (thumbnail.width > 1 || thumbnail.height > 1) && thumbnail.visible
                        source: "qrc:/resources/ThumbnailsShadow.png"
                        border.left: 45 / dpr
                        border.top: 45 / dpr
                        border.right: 45 / dpr
                        border.bottom: 45 / dpr
                    }*/

                //                        Rectangle {
                //                            color: "yellow"
                //                            anchors.fill: thumbnail
                //                        }

                Image {
                    id: thumbnail
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: cellPadding
                    visible: imageIdRole !== undefined
                    fillMode: Image.PreserveAspectFit
                    width: parent.width - cellPadding * 2 //thumbnailWidth
                    height: parent.height - cellPadding * 2 - captionHeight // thumbnailHeight
                    //                                sourceSize.width: gridRoot.thumbnailWidth
                    //                                sourceSize.height: gridRoot.thumbnailHeight
                    source: "image://thumbnails/" + imageIdRole
                }

                Image {
                    anchors.centerIn: thumbnail
                    source: folderRole ? "qrc:/resources/FolderIcon.svg" : "qrc:/resources/FileIcon.svg"
                    visible: imageIdRole === ""
                    fillMode: Image.PreserveAspectFit
                    width: thumbnail.width
                    height: thumbnail.height
                    sourceSize.width: thumbnail.width //Math.min(100, parent.height)
                    sourceSize.height: thumbnail.height //Math.min(100, parent.height)
                }


                //                        Rectangle {
                //                            color: "orange"
                //                            anchors.fill: fileName
                //                        }

                Text {
                    id: fileName
                    anchors {
                        left: parent.left
                        leftMargin: cellPadding
                        right: parent.right
                        rightMargin: cellPadding
                        bottom: parent.bottom
                        bottomMargin: cellPadding / 2
                    }

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: displayRole
                    color: "#d1d1d1"
                }

                MouseArea {
                    id: cellMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onDoubleClicked: {
                        if (folderRole !== undefined) {
                            gridView.currentIndex = 0
                            viewerController.cd(displayRole)
                        }
                    }
                    onClicked: gridView.currentIndex = index
                }
            }
        }
    }
}
