//import QtQuick 2.15
//import QtQuick.Layouts 1.15
//import QtQuick.Controls 2.15 as T
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

import ZoinGallery 1.0

MouseArea {
    id: masonryView

    property alias view: masonryLayout

    Rectangle {
        anchors {
            fill: parent
            topMargin: 1
        }
        color: "#202020"
    }
    focus: true

    signal toggleViewer()

    // <Scrolling>
    property bool scrollingStarted: false
    property var scrollingStartedAtY
    property bool scrollingMode: false

    acceptedButtons: Qt.MiddleButton

    function startScrolling() {
        scrollingStarted = false
        scrollingMode = true
        scrollingStartedAtY = masonryView.mouseY
        scrollingTimer.start()
        masonryLayout.setScrollingMode(true)
    }

    function endScrolling() {
        scrollingStarted = false
        scrollingMode = false
        scrollingTimer.stop()
        masonryLayout.setScrollingMode(false)

        hideHovered = false
    }

    onPressed: {
        if (!scrollingMode) {
            startScrolling()
        }
        else {
            endScrolling()
        }
    }

    onReleased: {
        if (scrollingStarted) {
            endScrolling()
        }
    }

    Timer {
        id: scrollingTimer
        repeat: true
        interval: 1000/60
        onTriggered: {
            let distance = masonryView.mouseY - scrollingStartedAtY
            distance = Math.min(Math.max(0, distance - 25), distance + 25)
            let totalHeight = topLevelWindow.screen.height
            let fraction = Math.abs(distance) / (totalHeight - 25)
            if (fraction > 0) {
                fraction += 0.05
            }

            let increment = Math.pow(fraction, 3) * totalHeight
            masonryLayout.contentY += increment * (distance < 0 ? -1 : 1)

            if (distance !== 0 && !scrollingStarted) {
                scrollingStarted = true
                hideHovered = true
            }

            if (distance > 0) {
                masonryLayout.setScrollingMode(true, 1)
            }
            else if (distance < 0) {
                masonryLayout.setScrollingMode(true, -1)
            }
            else {
                masonryLayout.setScrollingMode(true)
            }
        }
    }
    // </Scrolling>

    hoverEnabled: true
    onPositionChanged: {
        if (!scrollingMode) {
            hideHovered = false
        }
    }

    property bool hideHovered: false
    property real currentItemCenterX: 0
    property real currentItemCenterY: 0
    property bool alwaysShowFileNames: false

    Connections {
        target: masonryLayout
        function onLayoutReset() {
            let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
            currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
            currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
        }
    }

    function moveInImageList(forward, toEnd) {
        let nextIndex = masonryLayout.nextImageIndex(forward, toEnd)
        setCurrentIndex(nextIndex)
        return nextIndex
    }

    function currentItemImageGeometry() {
        let currentItemGeometry = Qt.rect(masonryLayout.currentItem.x,
                                          masonryLayout.currentItem.y,
                                          masonryLayout.currentItem.width,
                                          masonryLayout.currentItem.height)
        let spacing = masonryLayout.spacing
        let imageGeometry = Qt.rect(currentItemGeometry.x + spacing / 2,
                                    currentItemGeometry.y + spacing / 2 - masonryLayout.contentY,
                                    currentItemGeometry.width - spacing,
                                    currentItemGeometry.height - spacing)
        return imageGeometry
    }

    function setCurrentIndex(index, keepLastPosX = false, keepLastPosY = false, neverScroll = false) {
        masonryLayout.currentIndex = index
        let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
        if (!keepLastPosX) {
            currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
        }
        if (!keepLastPosY) {
            if (!neverScroll) {
                ensureVisible(masonryLayout.currentIndex)
            }
            currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
        }
        hideHovered = true
    }

    function scrollBy(deltaY) {
        let newContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY) + deltaY
        newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
        scrollAnimation2.from = masonryLayout.contentY
        scrollAnimation2.to = newContentY
        scrollAnimation2.restart()
    }

    function ensureVisible(index) {
        let indexGeometry = masonryLayout.indexGeometry(index)
        let newContentY = -1
        if (indexGeometry.y < masonryLayout.contentY) {
            newContentY = indexGeometry.y
        }
        else if (indexGeometry.y + indexGeometry.height > masonryLayout.contentY + masonryLayout.height) {
            newContentY = indexGeometry.y + indexGeometry.height - masonryLayout.height
        }

        if (newContentY !== -1) {
            newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
            scrollAnimation2.from = masonryLayout.contentY
            scrollAnimation2.to = newContentY
            scrollAnimation2.restart()
        }
    }

    Keys.onPressed:
        (event) => {
            event.accepted = true
            if (event.key === Qt.Key_Left) {
                setCurrentIndex(masonryLayout.currentIndex - 1)
            }
            else if (event.key === Qt.Key_Right) {
                setCurrentIndex(masonryLayout.currentIndex + 1)
            }
            else if (event.key === Qt.Key_Up && !(event.modifiers & Qt.AltModifier)) {
                let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                let yAbove = currentItemGeometry.y - 2
                let indexAbove = masonryLayout.indexAt(currentItemCenterX, yAbove)
                if (indexAbove !== -1) {
                    if (event.modifiers & Qt.ControlModifier) {
                        ensureVisible(masonryLayout.currentIndex)
                        setCurrentIndex(indexAbove, true, false, true)
                        let newCurrentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                        scrollBy(newCurrentItemGeometry.y - currentItemGeometry.y)
                    }
                    else {
                        setCurrentIndex(indexAbove, true)
                    }
                }
            }
            else if (event.key === Qt.Key_Down && !(event.modifiers & Qt.AltModifier)) {
                let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                let yBelow = currentItemGeometry.y + currentItemGeometry.height + 2
                let indexBelow = masonryLayout.indexAt(currentItemCenterX, yBelow)
                if (indexBelow !== -1) {
                    if (event.modifiers & Qt.ControlModifier) {
                        ensureVisible(masonryLayout.currentIndex)
                        setCurrentIndex(indexBelow, true, false, true)
                        let newCurrentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                        scrollBy(newCurrentItemGeometry.y - currentItemGeometry.y)
                    }
                    else {
                        setCurrentIndex(indexBelow, true)
                    }
                }
            }
            else if (event.key === Qt.Key_Home) {
                setCurrentIndex(0)
            }
            else if (event.key === Qt.Key_End) {
                setCurrentIndex(masonryLayout.count - 1)
            }
            else if (event.key === Qt.Key_PageUp) {
                let deltaY = (masonryLayout.height - masonryLayout.height / 8)
                let futureContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
                let prevPageY = Math.max(0, futureContentY - deltaY) + currentItemCenterY
                let newCurrentIndex = masonryLayout.indexAt(currentItemCenterX, prevPageY)
                if (newCurrentIndex === -1) {
                    newCurrentIndex = 0
                }
                let hitStart = newCurrentIndex === 0
                if (newCurrentIndex === masonryLayout.currentIndex) {
                    newCurrentIndex = masonryLayout.indexAt(currentItemCenterX, 1)
                    hitStart = true

                    if (newCurrentIndex === masonryLayout.currentIndex) {
                        newCurrentIndex = 0
                    }
                }

                setCurrentIndex(newCurrentIndex, !hitStart, !hitStart)
                scrollBy(-deltaY)
            }
            else if (event.key === Qt.Key_PageDown) {
                let deltaY = (masonryLayout.height - masonryLayout.height / 8)
                let futureContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
                let nextPageY = Math.min(masonryLayout.contentHeight - masonryLayout.height, futureContentY + deltaY) + currentItemCenterY
                let newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, nextPageY)

                if (newCurrentIndex2 === -1) {
                    newCurrentIndex2 = masonryLayout.count - 1
                }
                let hitEnd = newCurrentIndex2 === masonryLayout.count - 1
                if (newCurrentIndex2 === masonryLayout.currentIndex) {
                    newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, masonryLayout.contentHeight - 1)
                    hitEnd = true

                    if (newCurrentIndex2 === -1) {
                        newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, masonryLayout.contentHeight - masonryLayout.targetHeight)
                    }
                    if (newCurrentIndex2 === masonryLayout.currentIndex) {
                        newCurrentIndex2 = masonryLayout.count - 1
                    }
                }

                setCurrentIndex(newCurrentIndex2, !hitEnd, !hitEnd)
                scrollBy(deltaY)
            }
            else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                setCurrentIndex(viewerController.up())
            }
            else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier) ||
                     (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier)) {
                topLevelWindow.toggleFullscreen()
            }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                if (masonryLayout.currentItem.isFolder) {
                    viewerController.cd(masonryLayout.currentItem.text)
                }
                else {
                    masonryView.toggleViewer()
                }
            }
            else if (event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) {
                masonryLayout.zoomIn()
            }
            else if (event.key === Qt.Key_Minus) {
                masonryLayout.zoomOut()
            }
            else if (event.key === Qt.Key_Backslash) {
                masonryView.alwaysShowFileNames = !masonryView.alwaysShowFileNames
            }
        }

    MasonryLayout {
        id: masonryLayout
        anchors {
            topMargin: 1
            leftMargin: masonryLayout.spacing / 2

            left: parent.left
            top: parent.top
            bottom: parent.bottom
            right: masonryScroll.left
            rightMargin: masonryLayout.spacing / 2
        }
        clip: true
        model: fileListModel

        Behavior on contentY {
            enabled: scrollingMode

            NumberAnimation {
                id: contentYAnimation
                duration: 1000/60
            }
        }

        delegate: BrickItem {
            id: brickDelegate
            property string text
            property string imageId
            property int index
            property bool isImage
            property bool isFolder
            property string iconPath

            Rectangle {
                width: image.width + 2
                height: image.height + 2
                x: image.x - 1
                y: image.y - 1
                color: "#202020"
                border.color: "#000"
            }

            Rectangle {
                anchors {
                    fill: image
                    leftMargin: -2
                    topMargin: -2
                    rightMargin: -2
                    bottomMargin: -2
                }
                color: "#2980b9"
                visible: masonryLayout.currentIndex === index
            }

            Image {
                id: image
                width: parent.width - masonryLayout.spacing
                height: parent.height - masonryLayout.spacing
                x: masonryLayout.spacing / 2
                y: masonryLayout.spacing / 2
                visible: imageId !== "" && isImage
                fillMode: Image.PreserveAspectCrop
                source: imageId
//                    opacity: 0.1
//                    asynchronous: true
            }

            Image {
                x: parent.width / 2 - width / 2
                y: Math.max(masonryLayout.spacing, (parent.height - 26) / 2 - height / 2)
                width: Math.min(parent.width - masonryLayout.spacing, 128)
                height: Math.min(parent.height - 26 - masonryLayout.spacing, 128)
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                visible: !isImage
                source: iconPath
            }

            Rectangle {
                id: infoPanel
                color: masonryLayout.currentIndex === index ? "#B3002943" : Qt.rgba(0, 0, 0, 0.5)
                anchors {
                    left: parent.left
                    leftMargin: masonryLayout.spacing / 2
                    right: parent.right
                    rightMargin: masonryLayout.spacing / 2
                    bottom: parent.bottom
                    bottomMargin: masonryLayout.spacing / 2
                }
                height: textField.height + 10
                visible: (brickMouseArea.containsMouse && !hideHovered) || imageId === "" ||
                         masonryLayout.currentIndex === index || masonryView.alwaysShowFileNames
                z: 1

                Text {
                    id: textField
                    anchors{
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 5
                    }

                    text: brickDelegate.text

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: "#d1d1d1"
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }
            }

            MouseArea {
                id: brickMouseArea
                anchors.fill: parent
                hoverEnabled: true

                onPressed: {
                    if (scrollingMode) {
                        endScrolling()
                    }
                    setCurrentIndex(index)
                }

                onDoubleClicked: {
                    if (isFolder) {
                        viewerController.cd(text)
                    }
                    else {
                        masonryView.toggleViewer()
                    }
                }
            }
        }

        MouseArea {
            property alias animation: scrollAnimation2

            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel:
                (wheel) => {
                    if (wheel.modifiers & Qt.ControlModifier) {
                        if (wheel.angleDelta.y < 0) {
                            masonryLayout.zoomOut()
                        }
                        else {
                            masonryLayout.zoomIn()
                        }
                    }
                    else {
                        scrollBy(-wheel.angleDelta.y)
                    }
                }
        }

        NumberAnimation {
            id: scrollAnimation2
            target: masonryLayout
            property: "contentY"
            duration: 150
            easing.type: Easing.OutSine
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
            }
        }

        Connections {
            id: conn
            target: masonryLayout
            function onContentYChanged() {
                masonryScroll.position = masonryLayout.contentY / masonryLayout.contentHeight
            }

            function onContentHeightChanged() {
                masonryScroll.size = masonryLayout.height / masonryLayout.contentHeight
            }

            function onHeightChanged() {
                masonryScroll.size = masonryLayout.height / masonryLayout.contentHeight
            }
        }
    }
}
