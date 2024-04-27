import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

import ZoinGallery 1.0

BrickItem {
    id: brickDelegate
    property string text
    property string fullPath
    property string imageId
    property int index
    property bool isImage
    property bool isDecodedImage
    property bool isFolder
    property string iconPath
    property bool folderView: false
    property string nestingInfo: ""
    property int nestingLevel: nestingInfo.length

    property real sizeBase: Math.min(width, height - 26) - masonryLayout.spacing
    readonly property real selectionExtendsFor: 5
    readonly property real selectionExtendsForImage: 2
    property bool isSelected: masonryLayout.currentIndex === index
    readonly property real nestingShift: 29

    property var imageItem: brickDelegate

    component ImageView : Item {
        id: imageViewRoot
        property alias source: image.source
        property real padding
        property bool needScaling: image.sourceSize.width !== Math.round(image.width * dpr) ||
                                   image.sourceSize.height !== Math.round(image.height * dpr)
        property alias image: image

        x: padding / 2
        y: padding / 2
        width: Math.round((parent.width - padding) * dpr) / dpr
        height: Math.round((parent.height - padding) * dpr) / dpr

        Rectangle {
            width: parent.width
            height: parent.height
            radius: 4
            color: Style.darker
            visible: image.status !== Image.Ready
        }

        Image {
            id: image

            width: parent.width
            height: parent.height

            fillMode: Image.PreserveAspectCrop
            cache: false
            // Async adds black blinking for folder views
            //asynchronous: true
            visible: false
        }

        ShaderEffect {
            id: imageShader
            anchors.fill: image

            property var source: image
            property var viewportSize: Qt.size(width * dpr, height * dpr)
            property real sharpenAmount: 1
            property bool showCheckerboard: masonryLayout.showTransparentGrid
            property int checkerboardSize: 4 * dpr
            property real borderRadius: 4.1 * dpr

            fragmentShader: "qrc:/resources/shader.frag.qsb"
            visible: image.source != ""
        }
    }

    component TreeBranch : Row {
        spacing: 0

        Repeater {
            model: nestingLevel

            Item {
                id: treeBranch
                width: nestingShift
                height: parent.height

                property bool lastElement: index === nestingLevel - 1
                property bool hasNextElement: nestingInfo[index] === "1"

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 2
                    height: treeBranch.hasNextElement ? parent.height : masonryLayout.listRowHeight / 2 + 2

                    color: Style.lighter2
                    visible: treeBranch.hasNextElement || treeBranch.lastElement
                }

                Rectangle {
                    anchors {
                        left: parent.horizontalCenter
                        leftMargin: 1
                        right: parent.right
                    }
                    y: masonryLayout.listRowHeight / 2
                    height: 2
                    color: Style.lighter2
                    visible: treeBranch.lastElement
                }
            }
        }
    }

    Component {
        id: fileDelegate

        Item {
            Rectangle {
                anchors {
                    fill: fileDelegateContent
                    leftMargin: -selectionExtendsFor
                    topMargin: -selectionExtendsFor
                    rightMargin: -selectionExtendsFor
                    bottomMargin: -selectionExtendsFor + fileInfoPanel.height - fileName.contentHeight - sizeBase / 40
                }

                color: brickMouseArea.pressed ? Style.brickPressed : (isSelected ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isSelected ? Style.brickSelectedBorder : color
                radius: 4
                visible: isSelected || brickMouseArea.containsMouse && !hideHovered
            }

            Item {
                id: fileDelegateContent
                anchors {
                    fill: parent
                    margins: masonryLayout.spacing / 2
                }

                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: sizeBase - sizeBase / 10
                    height: width
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    source: iconPath
                }

                Item {
                    id: fileInfoPanel
                    anchors {
                        left: parent.left
                        leftMargin: masonryLayout.spacing / 2
                        right: parent.right
                        rightMargin: masonryLayout.spacing / 2
                        top: icon.bottom
                        bottom: parent.bottom
                        bottomMargin: masonryLayout.spacing / 2
                    }
                    z: 1

                    Text {
                        id: fileName
                        anchors{
                            fill: parent
                            margins: sizeBase / 40
                        }
                        width: parent.width

                        text: brickDelegate.text
                        textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        color: Style.text
                        maximumLineCount: 4
                        wrapMode: Text.Wrap
                    }
                }

            }
            Component.onCompleted: {
                brickDelegate.imageItem = icon
            }
        }
    }

    Component {
        id: fileListDelegate

        Item {
            Rectangle {
                id: outline
                anchors {
                    fill: fileListDelegateContent
                    margins: -selectionExtendsFor
                }
                color: brickMouseArea.pressed ? Style.brickPressed : (isSelected ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isSelected ? Style.brickSelectedBorder : color
                radius: 4
                visible: isSelected || brickMouseArea.containsMouse && !hideHovered
            }

            TreeBranch {
                height: parent.height
            }

            Item {
                id: fileListDelegateContent
                anchors {
                    fill: parent
                    margins: masonryLayout.spacing / 2
                    leftMargin: masonryLayout.spacing / 2 + nestingShift * nestingLevel
                }

                Image {
                    id: icon
                    anchors {
                        verticalCenter: parent.verticalCenter
                        left: parent.left
                    }
                    height: parent.height
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    source: iconPath
                }

                Text {
                    id: fileName
                    anchors {
                        left: icon.right
                        leftMargin: 5
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    text: brickDelegate.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    elide: Text.ElideRight
                    color: Style.text
                    maximumLineCount: 1
                }
            }

            Component.onCompleted: {
                brickDelegate.imageItem = icon
            }
        }
    }

    Component {
        id: imageDelegate

        Item {
            visible: !(isSelected && root.viewerShowAnimationRunning)
            Rectangle {
                id: delegateOutline
                anchors {
                    fill: image
                    margins: -selectionExtendsForImage
                }
                color: brickMouseArea.pressed ? Style.brickImagePressed : (isSelected ? Style.brickImageSelected : Style.brickImageHovered)
                radius: 4
                visible: isSelected || brickMouseArea.containsMouse && !hideHovered
            }

            ImageView {
                id: image
                padding: masonryLayout.spacing
                source: imageId
            }

            Item {
                id: imageInfoPanel
                anchors {
                    left: parent.left
                    leftMargin: masonryLayout.spacing / 2
                    right: parent.right
                    rightMargin: masonryLayout.spacing / 2
                    bottom: parent.bottom
                    bottomMargin: masonryLayout.spacing / 2
                }
                height: imageText.height + 10
                visible: brickMouseArea.containsMouse && !hideHovered ||
                         isSelected ||
                         masonryView.alwaysShowFileNames ||
                         masonryView.quickSearchMode
                z: 1
                clip: true

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: -radius
                    radius: 4
                    color: brickMouseArea.pressed ? Style.brickInfoPanelPressed : (isSelected ? Style.brickInfoPanelSelected : Style.brickInfoPanelHovered)
                }


                Text {
                    id: imageText
                    anchors{
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 5
                    }

                    text: brickDelegate.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: Style.text
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }
            }

            Component.onCompleted: {
                brickDelegate.imageItem = image.image
            }
        }
    }

    Component {
        id: folderViewDelegate

        Item {
            Rectangle {
                id: folderViewDelegateBackground
                anchors {
                    fill: folderViewDelegateContent
                    margins: -selectionExtendsFor
                }
                color: brickMouseArea.pressed ? Style.brickPressed : (isSelected ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isSelected ? Style.brickSelectedBorder : color
                radius: 4
                visible: isSelected || brickMouseArea.containsMouse && !hideHovered
            }

            TreeBranch {
                height: parent.height
            }

            Item {
                id: folderViewDelegateContent
                anchors {
                    fill: parent
                    margins: masonryLayout.spacing / 2
                    leftMargin: masonryLayout.spacing / 2 + nestingShift * nestingLevel
                }

                Rectangle {
                    id: branchDown
                    x: icon.x + icon.width / 2 - 1
                    anchors {
                        top: folderViewTitle.bottom
                        topMargin: 2
                        bottom: masonryLayout2.verticalCenter
                        bottomMargin: -1
                    }
                    width: 2

                    color: Style.lighter2
                }

                Rectangle {
                    anchors {
                        left: branchDown.right
                        right: masonryLayout2.left
                        rightMargin: 2
                        verticalCenter: masonryLayout2.verticalCenter
                    }
                    height: 2

                    color: Style.lighter2
                }

                Item {
                    id: folderViewTitle
                    anchors {
                        top: parent.top
                        left: parent.left
                    }
                    width: childrenRect.width + 20
                    height: masonryLayout.listRowHeight - masonryLayout.spacing

                    Image {
                        id: icon
                        anchors {
                            bottom: parent.bottom
                            left: folderViewTitle.left
                        }
                        height: parent.height
                        sourceSize.height: height
                        fillMode: Image.PreserveAspectFit
                        source: iconPath
                    }

                    Text {
                        id: fileName
                        anchors {
                            left: icon.right
                            leftMargin: 5
                            verticalCenter: icon.verticalCenter
                        }
                        text: brickDelegate.text
                        textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                        elide: Text.ElideRight
                        color: Style.text
                        maximumLineCount: 1
                    }
                }

                MasonryLayout {
                    id: masonryLayout2

                    anchors {
                        left: parent.left
                        top: folderViewTitle.bottom
                        right: parent.right
                        bottom: parent.bottom
                        topMargin: -spacing / 2 + masonryLayout.spacing / 2
                        leftMargin: fileName.x
                        rightMargin: -spacing / 2
                        bottomMargin: -spacing / 2
                    }
                    clip: true
                    model: fileListModel.folderModel(index)

                    spacing: 2

                    onHeightChanged: {
                        if (height > 0) {
                            targetHeight = height
                        }
                    }
                    Connections {
                        target: masonryLayout
                        function onLayoutReset() {
                            masonryLayout2.reReadAndDecodeThumbnails()
                        }
                    }

                    delegate: BrickItem {
                        id: brck
                        property string text
                        property string imageId
                        property int index
                        property bool isImage
                        property bool isFolder
                        property string iconPath
                        property bool folderView: false

                        ImageView {
                            id: image2

                            needScaling: false
                            padding: masonryLayout2.spacing
                            source: imageId
                        }
                    }
                }
            }
            Component.onCompleted: {
                brickDelegate.imageItem = masonryLayout2
            }
        }
    }

    Component {
        id: folderViewDelegateGrid

        Item {
            Rectangle {
                id: folderViewDelegateGridBackground
                anchors {
                    fill: folderViewDelegateGridContent
                    margins: -selectionExtendsFor
                }
                color: brickMouseArea.pressed ? Style.brickPressed : (isSelected ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isSelected ? Style.brickSelectedBorder : color
                radius: 4
                visible: isSelected || brickMouseArea.containsMouse && !hideHovered
            }

            Item {
                id: folderViewDelegateGridContent
                anchors {
                    fill: parent
                    margins: masonryLayout.spacing / 2
                }

                Rectangle {
                    width: Math.min(110, parent.width * 0.44)
                    height: sizeBase / 27 + 10*2
                    radius: 4

                    color: folderBackground.color
                }

                Rectangle {
                    id: folderBackground

                    anchors {
                        fill: parent
                        topMargin: sizeBase / 27
                        bottomMargin: imageInfoPanel.height + masonryLayout.spacing
                    }
                    color: Style.folderIcon
                    radius: 4

                    MasonryLayout {
                        id: masonryLayout2

                        anchors {
                            fill: parent
                            leftMargin: spacing / 2
                            rightMargin: spacing / 2
                            topMargin: spacing / 2
                            bottomMargin: spacing
                        }
                        clip: true
                        model: fileListModel.folderModel(index)

                        spacing: 2

                        onHeightChanged: {
                            if (height > 0) {
                                targetHeight = height
                            }
                        }
                        Connections {
                            target: masonryLayout
                            function onLayoutReset() {
                                masonryLayout2.reReadAndDecodeThumbnails()
                            }
                        }

                        delegate: BrickItem {
                            id: brck
                            property string text
                            property string imageId
                            property int index
                            property bool isImage
                            property bool isFolder
                            property string iconPath
                            property bool folderView: false

                            ImageView {
                                id: image2

                                needScaling: false
                                padding: masonryLayout2.spacing
                                source: imageId
                            }
                        }

                        Component.onCompleted: {
                            brickDelegate.imageItem = masonryLayout2
                        }
                    }
                }

                Item {
                    id: imageInfoPanel
                    anchors {
                        left: parent.left
                        leftMargin: masonryLayout.spacing / 2
                        right: parent.right
                        rightMargin: masonryLayout.spacing / 2
                        bottom: parent.bottom
                        bottomMargin: masonryLayout.spacing / 2
                    }
                    height: imageText.height
                    z: 1

                    Text {
                        id: imageText
                        anchors {
                            left: parent.left
                            right: parent.right
                            bottom: parent.bottom
                        }

                        text: brickDelegate.text
                        textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                        horizontalAlignment: masonryLayout.listView ? Text.AlignLeft : Text.AlignHCenter
                        elide: Text.ElideRight
                        color: Style.text
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    Item {
        id: draggable

        anchors.fill: parent
        Drag.active: brickMouseArea.drag.active
        Drag.dragType: Drag.Automatic
        Drag.mimeData: {
            "text/uri-list": [viewerController.indexUrl(masonryLayout.currentIndex)]
        }
    }

    MouseArea {
        id: brickMouseArea
        anchors.fill: parent
        hoverEnabled: true

        drag.target: draggable

        onPressed: (mouse) => {
            let mappedPos = imageItem.mapFromItem(brickMouseArea, mouse.x, mouse.y)
            draggable.Drag.hotSpot.x = mappedPos.x
            draggable.Drag.hotSpot.y = mappedPos.y
            imageItem.grabToImage(function(result) {
                draggable.Drag.imageSource = result.url
            })

            focusProxy.forceActiveFocus()

            if (scrollingMode) {
                endScrolling()
            }
            setCurrentIndex(index)
        }

        onDoubleClicked: {
            if (isFolder) {
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                viewerController.cd(fullPath)
            }
            else {
                if (masonryLayout.currentItem.isImage) {
                    masonryView.toggleViewer()
                }
            }
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: fileDelegate
    }

    states: [
        State {
            when: masonryLayout.listView && folderView
            PropertyChanges { loader.sourceComponent: folderViewDelegate }
        },
        State {
            when: folderView
            PropertyChanges { loader.sourceComponent: folderViewDelegateGrid }
        },
        State {
            when: imageId !== "" || isDecodedImage
            PropertyChanges { loader.sourceComponent: imageDelegate }
        },
        State {
            when: masonryLayout.listView && isFolder
            PropertyChanges { loader.sourceComponent: fileListDelegate }
        }
    ]
}
