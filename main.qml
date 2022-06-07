//import QtQuick 2.15
//import QtQuick.Window 2.15
//import QtQuick.Layouts 1.15
//import QtQuick.Controls 2.15 as T
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

import "qml"

Window {
    id: topLevelWindow
    width: 1640
    height: 980
    visible: true
    color: "#333333"
    title: qsTr("Zoin Gallery")

    property real dpr: topLevelWindow.screen.devicePixelRatio

    onClosing: (closeEvent) => {
                   viewerController.prepareToClose()
               }

    Component.onCompleted: {
        viewerController.mainWindow = topLevelWindow
    }

    Connections {
        target: viewerController

        function onMainWindowResized() {
            masonryLayout.view.reReadAndDecodeThumbnails()
        }
    }

    Item {
        id: root
        anchors.fill: parent

        function toggleViewer() {
            if (root.state === "thumbnails") {
                switchToViewer()
            }
            else {
                switchToThumbnails()
            }
        }

        function switchToViewer() {
            root.state = "viewer"

            if (!viewerAnimation.running) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                viewerImage.x = mappedGeometry.x
                viewerImage.y = mappedGeometry.y
                viewerImage.width = mappedGeometry.width
                viewerImage.height = mappedGeometry.height
            }

            viewerImage.source = masonryLayout.view.currentItem.imageId

            viewerMode.visible = true

            viewerAnimation.x = 0
            viewerAnimation.y = 0
            viewerAnimation.width = viewerMode.width
            viewerAnimation.height = viewerMode.height
            viewerAnimation.restart()
        }

        function switchToThumbnails() {
            root.state = "thumbnails"

            if (masonryLayout.view.currentItem) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                viewerAnimation.x = mappedGeometry.x
                viewerAnimation.y = mappedGeometry.y
                viewerAnimation.width = mappedGeometry.width
                viewerAnimation.height = mappedGeometry.height
                viewerAnimation.restart()
            }
        }

        ColumnLayout {
            id: thumbnailsView
            anchors.fill: parent

            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                spacing: 10

                Button {
                    Layout.leftMargin: 10
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter

                    text: "Up"

                    onReleased: {
                        masonryLayout.view.currentIndex = viewerController.up()
                    }
                }

                Text {
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter

                    elide: Text.ElideRight
                    text: viewerController.currentPath
                    color: "#d1d1d1"
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 50
                    horizontalAlignment: Text.AlignRight

                    text: Math.round(masonryZoomSlider.value)
                    color: "#d1d1d1"
                }

                Slider {
                    id: masonryZoomSlider
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 30
                    Layout.rightMargin: 10
                    Layout.alignment: Qt.AlignVCenter
                    from: 30
                    value: masonryLayout.view.targetHeight
                    to: 500
                    stepSize: 1

                    onValueChanged: masonryLayout.view.targetHeight = masonryZoomSlider.value
                    property int lastValue: value
                    onPressedChanged: {
                        if (pressed) {
                            lastValue = value
                        }
                        else if (lastValue !== value) {
                            masonryLayout.view.reReadAndDecodeThumbnails()
                        }
                    }
                }
            }


            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#111111"

                MasonryMode {
                    id: masonryLayout
                    anchors.fill: parent

                    onToggleViewer: root.toggleViewer()
                }
            }
        }

        Item {
            id: viewerMode
            anchors.fill: parent

            property int animationDuration: 150
            property int easingType: Easing.OutSine

            Connections {
                target: masonryLayout.view
                function onCurrentIndexChanged() {
                    if (root.state === "viewer") {
                        let imageId = masonryLayout.view.indexImage(masonryLayout.view.currentIndex)
                        if (imageId) {
                            viewerImage.source = imageId
                        }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "black"
                opacity: root.state === "viewer"
                Behavior on opacity {
                    NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
                }
            }

            Image {
                id: viewerImage
                fillMode: Image.PreserveAspectFit
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.state === "viewer"

                onDoubleClicked: root.toggleViewer()
                onPressed:
                    (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            root.toggleViewer()
                        }
                    }

                onWheel: {

                }

                acceptedButtons: Qt.AllButtons
            }

            ParallelAnimation {
                id: viewerAnimation

                property alias x: viewerMaximizeAnimationX.to
                property alias y: viewerMaximizeAnimationY.to
                property alias width: viewerMaximizeAnimationWidth.to
                property alias height: viewerMaximizeAnimationHeight.to

                NumberAnimation {
                    id: viewerMaximizeAnimationX
                    target: viewerImage
                    property: "x"
                    duration: viewerMode.animationDuration
                    easing.type: viewerMode.easingType
                }

                NumberAnimation {
                    id: viewerMaximizeAnimationY
                    target: viewerImage
                    property: "y"
                    duration: viewerMode.animationDuration
                    easing.type: viewerMode.easingType
                }

                NumberAnimation {
                    id: viewerMaximizeAnimationWidth
                    target: viewerImage
                    property: "width"
                    duration: viewerMode.animationDuration
                    easing.type: viewerMode.easingType
                }

                NumberAnimation {
                    id: viewerMaximizeAnimationHeight
                    target: viewerImage
                    property: "height"
                    duration: viewerMode.animationDuration
                    easing.type: viewerMode.easingType
                }

                onFinished: {
                    if (root.state === "thumbnails") {
                        viewerMode.visible = false
                    }
                    else {
                        viewerImage.width = Qt.binding(function() {return viewerMode.width})
                        viewerImage.height = Qt.binding(function() {return viewerMode.height})
                    }
                }
            }
        }

        state: "thumbnails"

        states: [
            State {
                name: "thumbnails"
            },
            State {
                name: "viewer"
            }
        ]
    }
}
