import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import ZoinGallery 1.0

MouseArea {
    id: masonryView

    property alias view: masonryLayout
    property alias focusProxy: quickSearchField

    Rectangle {
        anchors {
            fill: parent
        }
        color: "#202020"
    }

    signal toggleViewer()

    // <Scrolling>
    property bool scrollingStarted: false
    property var scrollingStartedAtY
    property bool scrollingMode: false
    property bool quickSearchMode: false

    property bool disableAnimation: false

    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

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
        // This immediately stops behavior animation
        masonryLayout.contentY += 0.01
        masonryLayout.contentY -= 0.01

        scrollingTimer.stop()
        masonryLayout.setScrollingMode(false)

        hideHovered = false
    }

    onPressed: (mouse) => {
        if (mouse.button === Qt.LeftButton) {
            focusProxy.forceActiveFocus()
        }
        else {
            if (mouse.button === Qt.MiddleButton && !scrollingMode) {
                startScrolling()
            }
            else {
                endScrolling()
            }
        }
    }

    onReleased: (mouse) => {
        if (mouse.button !== Qt.LeftButton) {
            if (scrollingStarted) {
                endScrolling()
            }
        }
    }

    Timer {
        id: scrollingTimer
        repeat: true
        interval: animationInterval / 10
        property real animationInterval: 250
        onTriggered: {
            let distance = masonryView.mouseY - scrollingStartedAtY
            distance = Math.min(Math.max(0, distance - 25), distance + 25)
            let totalHeight = topLevelWindow.availableScreenHeight()
            let fraction = Math.abs(distance) / (totalHeight - 25)
            if (fraction > 0) {
                fraction += 0.05
            }

            let speed = scrollingTimer.interval / (1000 / 60) * (animationInterval / scrollingTimer.interval)
            let increment = (Math.pow(fraction, 3) * totalHeight) * speed * 2
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

    Connections {
        target: viewerController
        function onSetCurrentIndex(index) {
            masonryLayout.currentIndex = index
            let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
            currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
            let indexGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
            let newContentY = indexGeometry.y + indexGeometry.height / 2 - masonryLayout.height / 2
            newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
            masonryLayout.contentY = newContentY

            currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
            hideHovered = true
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

    function setCurrentIndex(index, keepLastPosX = false, keepLastPosY = false, neverScroll = false, keepQuickSearch = false) {
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
        if (!keepQuickSearch) {
            hideQuickSearch()
        }
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
            if (!masonryView.disableAnimation) {
                scrollAnimation2.from = masonryLayout.contentY
                scrollAnimation2.to = newContentY
                scrollAnimation2.restart()
            }
            else {
                masonryLayout.contentY = newContentY
            }

        }
    }

    function hideQuickSearch() {
        if (quickSearchMode) {
            quickSearchField.text = ""
            masonryLayout.quickSearch.mask = quickSearchField.text
            backspaceDisabledUntilKeyUp = true
            quickSearchMode = false
        }
    }

    function searchNext(forward) {
        let nextIndex = masonryLayout.quickSearch.nextImage(forward, true)
        setCurrentIndex(nextIndex, /*<defaults>*/ false, false, false /*</defaults>*/, true)
    }

    property bool backspaceDisabledUntilKeyUp: false
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
                let hitEnd = newCurrentIndex2 >= masonryLayout.count - 1
                if (newCurrentIndex2 === masonryLayout.currentIndex) {
                    newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, masonryLayout.contentHeight - 1)
                    hitEnd = true

                    if (newCurrentIndex2 === -1) {
                        newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, masonryLayout.contentHeight - masonryLayout.targetHeight * 1.5)
                    }
                    if (newCurrentIndex2 === masonryLayout.currentIndex) {
                        newCurrentIndex2 = masonryLayout.count - 1
                    }
                }

                setCurrentIndex(newCurrentIndex2, !hitEnd, !hitEnd)
                scrollBy(deltaY)
            }
            else if ((event.key === Qt.Key_Backspace && !quickSearchMode && !backspaceDisabledUntilKeyUp) || event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                masonryView.disableAnimation = true
                setCurrentIndex(viewerController.up())
                masonryView.disableAnimation = false
            }
            else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier) ||
                     (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier)) {
                topLevelWindow.toggleFullscreen()
            }
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ControlModifier) || event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                hideQuickSearch()
                if (masonryLayout.currentItem.isFolder) {
                    viewerController.cd(masonryLayout.currentItem.text)
                }
                else if (masonryLayout.currentItem.isImage) {
                    masonryView.toggleViewer()
                }
            }
            else if ((event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) && !quickSearchMode) {
                masonryLayout.zoomIn()
            }
            else if (event.key === Qt.Key_Minus && !quickSearchMode) {
                masonryLayout.zoomOut()
            }
            else if (event.key === Qt.Key_Backslash && !quickSearchMode) {
                masonryView.alwaysShowFileNames = !masonryView.alwaysShowFileNames
            }
            else if (event.key === Qt.Key_Escape) {
                endScrolling()

                if (quickSearchMode) {
                    event.accepted = false
                }
            }
            else if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_C || event.key === Qt.Key_Insert)) {
                viewerController.clipboardCopyIndexName(masonryLayout.currentIndex)
            }
            else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_D) {
                viewerController.clipboardCopyIndexFullPath(masonryLayout.currentIndex)
            }
            else {
                event.accepted = false
            }
        }

    Keys.onReleased:
        (event) => {
            if (!event.isAutoRepeat) {
                backspaceDisabledUntilKeyUp = false
            }
        }

    MasonryLayout {
        id: masonryLayout
        anchors {
//            topMargin: 1
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
                duration: scrollingTimer.animationInterval * 2
            }
        }

        delegate: BrickDelegate {}

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
        visible: masonryLayout.needScroll
        width: masonryLayout.needScroll ? 16 : 0

        Connections {
            target: masonryLayout

            function onNeedScrollChanged() {
                if (!topLevelWindow.isResizing) {
                    masonryLayout.reReadAndDecodeThumbnails()
                }
            }
        }

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

    Rectangle {
        id: quickSearchArea
        anchors {
            left: parent.left
            bottom: parent.bottom
            leftMargin: 20
            bottomMargin: 20
        }
        width: 330
        height: 49
        color: "#303030"
        border.width: 1
        border.color: "#404040"
        radius: 4
        visible: quickSearchMode

        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            TextField {
                id: quickSearchField
                Layout.fillWidth: true
                Layout.fillHeight: true

                leftPadding: 17
                rightPadding: 17
                focus: true
                hasBackground: false
                color: masonryLayout.quickSearch.matches ? (hovered ? Style.hovered : "#f0f0f0") : Style.textError

                validator: masonryLayout.quickSearch.validator

                Keys.forwardTo: masonryView

                Keys.onPressed:
                    (event) => {
                        delayedOnPressed.start()

                        if (event.key === Qt.Key_Escape) {
                            quickSearchField.text = ""
                            event.accepted = false
                        }
                        else if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return) && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier) ||
                                 event.key === Qt.Key_F3 && (event.modifiers & Qt.ShiftModifier)) {
                            searchNext(false)
                        }
                        else if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return) && (event.modifiers & Qt.ControlModifier) ||
                                 event.key === Qt.Key_F3) {
                            searchNext(true)
                        }
                    }
            }

            Text {
                id: matchesFoundText
                Layout.fillHeight: true
                Layout.preferredWidth: 34

                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: "#969696"
                text: masonryLayout.quickSearch.matchesInfo
            }

            Rectangle {
                color: "#505050"
                Layout.preferredWidth: 1
                Layout.preferredHeight: 32
                Layout.leftMargin: 16
                Layout.rightMargin: 8
                Layout.alignment: Qt.AlignVCenter
            }

            component SearchButton : Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 32

                icon.width: 9
                icon.height: 9
            }

            SearchButton {
                icon.source: "qrc:/resources/SearchBack.svg"
                onClicked: searchNext(false)

                ToolTip.text: "Previous\tShift+F3"
            }

            SearchButton {
                icon.source: "qrc:/resources/SearchNext.svg"
                onClicked: searchNext(true)

                ToolTip.text: "Next\tF3"
            }

            SearchButton {
                Layout.rightMargin: 8
                icon.source: "qrc:/resources/SearchClose.svg"
                onClicked: hideQuickSearch()

                ToolTip.text: "Close quick search\tEsc"
            }
        }

        Timer {
            id: delayedOnPressed
            interval: 0
            onTriggered: {
                if (quickSearchField.text !== "" && !quickSearchMode) {
                    quickSearchMode = true
                }
                else if (quickSearchField.text === "" && quickSearchMode) {
                    hideQuickSearch()
                }

                masonryLayout.quickSearch.mask = quickSearchField.text
                if (quickSearchField.text) {
                    let nextIndex = masonryLayout.quickSearch.nextImage(true, false)
                    setCurrentIndex(nextIndex, /*<defaults>*/ false, false, false /*</defaults>*/, true)
                }
            }
        }
    }
}
