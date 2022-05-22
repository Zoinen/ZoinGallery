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

    ColumnLayout {
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
                    masonryLayout.currentIndex = viewerController.up()
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
                value: masonryLayout.targetHeight
                to: 500
                stepSize: 1

                onValueChanged: masonryLayout.targetHeight = masonryZoomSlider.value
            }
        }


        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#111111"

            MasonryMode {
                id: masonryLayout
                anchors.fill: parent
            }
        }
    }
}
