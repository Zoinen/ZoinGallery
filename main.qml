import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
//import QtQuick.Controls.Basic
import QtQuick.Controls 2.15 as T
//import QtGraphicalEffects 1.15

Window {
    id: topLevelWindow
    width: 640
    height: 480
    visible: true
    color: "#333333"
    title: qsTr("Zoin Gallery")

    property real dpr: topLevelWindow.screen.devicePixelRatio

    onClosing: (closeEvent) => {
        viewerController.prepareToClose()
    }

    property bool secondChangeNotify: true

    onThumbnailWidthChanged: {
        secondChangeNotify = !secondChangeNotify
        if (secondChangeNotify) {
//            console.log(thumbnailWidth, thumbnailHeight, secondChangeNotify)
            viewerController.setThumbnailResolution(Qt.size(topLevelWindow.thumbnailWidth, topLevelWindow.thumbnailHeight), dpr)
        }
    }
    onThumbnailHeightChanged: {
        secondChangeNotify = !secondChangeNotify
        if (secondChangeNotify) {
//            console.log(thumbnailWidth, thumbnailHeight, secondChangeNotify)
            viewerController.setThumbnailResolution(Qt.size(topLevelWindow.thumbnailWidth, topLevelWindow.thumbnailHeight), dpr)
        }
    }

    property int thumbnailWidth: cellWidth - cellPadding * 2
    property int thumbnailHeight: cellHeight - cellPadding * 2 - captionHeight
    property int captionHeight: cellWidth > 250 ? 14 + 10 : cellWidth > 150 ? 14 + 5 : 14
    property int cellPadding: cellWidth > 250 ? 10 : cellWidth > 150 ? 7 : 5
    property real cellWidth: (width - 15) / columnCount
    property real cellHeight: cellWidth - 44 + 40
    property alias columnCount: zoomSlider.value

    Behavior on captionHeight {
        NumberAnimation { properties: "captionHeight"; duration: 300; easing.type: Easing.InOutQuad }
    }

    Behavior on cellPadding {
        NumberAnimation { properties: "cellPadding"; duration: 300; easing.type: Easing.InOutQuad }
    }

    component Button : T.Button {
        id: control

        focusPolicy: Qt.NoFocus

        contentItem: Text {
            text: control.text
            font: control.font
            opacity: enabled ? 1.0 : 0.3
            color: control.hovered ? "#ffffff" : "#e1e1e1"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            opacity: enabled ? 1 : 0.3
            border.color: control.hovered ? "#676767" : "#474747"
            border.width: 1
            color: control.down ? "#191919" : "#292929"
            radius: 2
            layer.enabled: true
        }
    }

    component Slider : T.Slider {
        id: control

        focusPolicy: Qt.NoFocus

        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            implicitWidth: 200
            implicitHeight: control.height
            width: control.availableWidth
            height: 6
            radius: 2
            color: "#292929"

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                color: "#474747"
                radius: 2
            }
        }

        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            implicitWidth: 18
            implicitHeight: 18
            radius: 18
            color: control.pressed ? "#878787" : "#676767"
        }
    }

    component ScrollBar : T.ScrollBar {
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
            color: scroll.pressed ? "#676767" : "#474747"
        }

        background: Rectangle {
            color: "#292929"
        }
    }

    ColumnLayout {
        anchors.fill: parent

        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            spacing: 10

            Button {
                Layout.leftMargin: 10
                Layout.topMargin: 10
                Layout.preferredWidth: 80
                Layout.preferredHeight: 30

                text: "Up"

                onReleased: {
                    gridView.highlightMoveDuration = 0
                    gridView.currentIndex = viewerController.up()
                    gridView.highlightMoveDuration = 150
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                Layout.topMargin: 10
                Layout.preferredHeight: 30

                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: viewerController.currentPath
                color: "#d1d1d1"
            }

            Slider {
                id: zoomSlider
                Layout.rightMargin: 10
                Layout.topMargin: 10
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 100
                Layout.preferredHeight: 30
                from: 19
//                value: 6
                value: 19
                to: 3
                stepSize: 1
            }

            Text {
                text: Math.round(zoomSlider.value)
                verticalAlignment: Text.AlignVCenter
                color: "#d1d1d1"
                Layout.preferredWidth: 50
            }
        }


        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#111111"

            GridView {
                id: gridView

                anchors {
                    fill: parent
                    leftMargin: 1
                    topMargin: 1
                }

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
                        zoomSlider.value -= 1
                    }
                    else if (event.key === Qt.Key_Minus) {
                        zoomSlider.value += 1
                    }
                    else {
                        event.accepted = false
                    }

                    gridView.keyHeld = true
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

                cellWidth: topLevelWindow.cellWidth
                cellHeight: topLevelWindow.cellHeight
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
                             zoomSlider.value -= deltaCell
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
                    property bool behaviorAnimationEnabled: loadCompleted && !gridView.scrolling && !gridView.keyHeld
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
                            enabled: delegateWrapper.behaviorAnimationEnabled
                            NumberAnimation { properties: "x"; duration: 300; easing.type: Easing.InOutQuad }
                        }
                        Behavior on y {
                            enabled: delegateWrapper.behaviorAnimationEnabled
                            NumberAnimation { properties: "y"; duration: 300; easing.type: Easing.InOutQuad }
                        }

                        Behavior on width {
                            enabled: delegateWrapper.behaviorAnimationEnabled
                            NumberAnimation { properties: "width"; duration: 300; easing.type: Easing.InOutQuad }
                        }
                        Behavior on height {
                            enabled: delegateWrapper.behaviorAnimationEnabled
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
                            //                                sourceSize.width: topLevelWindow.thumbnailWidth
                            //                                sourceSize.height: topLevelWindow.thumbnailHeight
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
    }
}
