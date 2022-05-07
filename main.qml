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
    width: 640
    height: 480
    visible: true
    color: "#333333"
    title: qsTr("Zoin Gallery")

    property real dpr: topLevelWindow.screen.devicePixelRatio
    property bool masonryViewMode: true

    onClosing: (closeEvent) => {
        viewerController.prepareToClose()
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
                id: gridZoomSlider
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
                text: Math.round(gridZoomSlider.value) + "," + Math.round(masonryZoomSlider.value)
                Layout.alignment: Qt.AlignVCenter
                color: "#d1d1d1"
                Layout.preferredWidth: 50
            }

            Slider {
                id: masonryZoomSlider
                Layout.preferredWidth: 100
                Layout.preferredHeight: 30
                Layout.topMargin: 10
                Layout.alignment: Qt.AlignVCenter
                from: 10
                value: 100
                to: 500
                stepSize: 1

                onValueChanged: masonryLayout.targetHeight = masonryZoomSlider.value
            }

            Button {
                id: masonryViewToggle

                Layout.topMargin: 10
                Layout.rightMargin: 10
                Layout.alignment: Qt.AlignVCenter
                checkable: true
                checked: true
                Layout.preferredWidth: 60
                Layout.preferredHeight: 30
                text: checked ? "Masonry" : "Grid"
                Binding {
                    target: topLevelWindow
                    property: "masonryViewMode"
                    value: masonryViewToggle.checked
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
                visible: topLevelWindow.masonryViewMode
            }

            GridMode {
                anchors.fill: parent
                visible: !topLevelWindow.masonryViewMode
            }

            /*T.StackView {
                id: stack
                anchors.fill: parent

                Connections {
                    target: topLevelWindow
                    function onMasonryViewModeChanged() {
                        if (stack.depth > 0) {
                            stack.pop()
                        }
                        if (masonryViewMode) {
                            stack.push("qml/MasonryMode.qml")
                        }
                        else {
                            stack.push("qml/GridMode.qml", {"zoom": Qt.binding(function() {return gridZoomSlider.value})})
//                            gridZoomSlider.value = Qt.binding(function() {return stack.currentItem.zoom})
                        }
                    }
                }
            }*/

        }
    }

    /*

    Text {
        anchors.left: zoomSlider.right
        text: zoomSlider.value + " / " + masonryLayout.contentHeight + "|" + masonryScroll.position + " of " + masonryScroll.size + ", " + masonryLayout.contentY
        verticalAlignment: Text.AlignVCenter
        color: "#d1d1d1"
        Layout.preferredWidth: 50
    }*/
}
