import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects

import ZoinGallery 1.0

BrickItem {
    id: brickDelegate

    property var model

    property int nestingLevel: model !== undefined ? model.nestingInfo.length : 0
    /// ZZZZZZZZZ TODO: GET RID OF THIS
    property real sizeBase: Math.min(width, height - 26) - masonryLayout.spacing
    property real folderGridDelegateTop: Math.round(width / 20)
    readonly property real selectionExtendsFor: 5
    readonly property real selectionExtendsForImage: 2
    property bool isCurrent: model !== undefined ? (masonryLayout.currentIndex === model.index) : false
    property bool isItemSelected: model !== undefined ? model.isSelected : false
    readonly property real nestingShift: 29

    property var imageItem: brickDelegate

    component ImageView : Item {
        id: imageViewRoot
        property alias source: internalImage.source
        property real padding
        property bool needScaling: internalImage.sourceSize.width !== Math.round(internalImage.width * dpr) ||
                                   internalImage.sourceSize.height !== Math.round(internalImage.height * dpr)
        property alias internalImage: internalImage
        property alias sharpen: imageShader.sharpenAmount
        // property real sharpen

        x: padding / 2
        y: padding / 2
        width: Math.round((parent.width - padding) * dpr) / dpr
        height: Math.round((parent.height - padding) * dpr) / dpr

        Rectangle {
            width: parent.width
            height: parent.height
            radius: 4
            color: Style.darker
            visible: internalImage.status !== Image.Ready
        }

        Image {
            id: internalImage

            property int xDiff: implicitWidth - imageViewRoot.width * dpr
            property int yDiff: implicitHeight - imageViewRoot.height * dpr
            property bool diffIsSmall: Math.abs(xDiff) < 4 && Math.abs(yDiff) < 4

            width: diffIsSmall ? implicitWidth / dpr : imageViewRoot.width
            height: diffIsSmall ? implicitHeight / dpr : imageViewRoot.height
            fillMode: diffIsSmall ? Image.Pad : Image.PreserveAspectCrop

            cache: false
            // Async adds black blinking for folder views
            //asynchronous: true
            visible: false
        }

        ShaderEffect {
            id: imageShader
            anchors.fill: internalImage

            property var source: internalImage
            property var viewportSize: Qt.size(width * dpr, height * dpr)
            property real sharpenAmount: 1.5
            property bool showCheckerboard: masonryLayout.showTransparentGrid
            property int checkerboardSize: 4 * dpr
            property real borderRadius: 4.1 * dpr

            fragmentShader: "qrc:/resources/shader.frag.qsb"
            visible: internalImage.source != ""
        }

        // Text {
        //     id: imageSizeDiff
        //     text: (internalImage.xDiff !== 0 || internalImage.yDiff !== 0) ? internalImage.xDiff + "x" + internalImage.yDiff : "0"
        //     visible: !internalImage.diffIsSmall && internalImage.status === Image.Ready
        //     color: "orange"
        //     style: Text.Outline; styleColor: "black"
        // }
    }

    component TreeBranch : Row {
        spacing: 0

        Repeater {
            model: nestingLevel

            Item {
                id: treeBranch
                width: nestingShift
                height: parent.height

                property bool lastElement: model.index === nestingLevel - 1
                property bool hasNextElement: model.nestingInfo[model.index] === "1"

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

    component PersistentSelectionBorder : Rectangle {
        color: "transparent"
        border.width: 3
        border.color: Style.persistentSelectionBorder
        radius: 4
        visible: isItemSelected
        z: 20
        antialiasing: true
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

                color: brickMouseArea.pressed ? Style.brickPressed : (isCurrent ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isCurrent ? Style.brickSelectedBorder : color
                radius: 4
                visible: isCurrent || brickMouseArea.containsMouse && !hideHovered
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
                    source: model.iconPath
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

                        text: model.text
                        textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        color: Style.text
                        maximumLineCount: 4
                        wrapMode: Text.Wrap
                    }
                }

            }
            PersistentSelectionBorder {
                anchors.fill: fileDelegateContent
            }
            Component.onCompleted: {
                brickDelegate.imageItem = fileDelegateContent
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
                color: brickMouseArea.pressed ? Style.brickPressed : (isCurrent ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isCurrent ? Style.brickSelectedBorder : color
                radius: 4
                visible: isCurrent || brickMouseArea.containsMouse && !hideHovered
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
                    source: model.iconPath
                }

                Text {
                    id: fileName
                    anchors {
                        left: icon.right
                        leftMargin: 5
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    text: model.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    elide: Text.ElideRight
                    color: Style.text
                    maximumLineCount: 1
                }
            }

            PersistentSelectionBorder {
                anchors.fill: fileListDelegateContent
                radius: 3
            }

            Component.onCompleted: {
                brickDelegate.imageItem = fileListDelegateContent
            }
        }
    }

    Component {
        id: imageDelegate

        Item {
            visible: !(isCurrent && root.viewerShowAnimationRunning)
            Rectangle {
                id: delegateOutline
                anchors {
                    fill: image
                    margins: -selectionExtendsForImage
                }
                color: brickMouseArea.pressed ? Style.brickImagePressed : (isCurrent ? Style.brickImageSelected : Style.brickImageHovered)
                radius: 4
                visible: isCurrent || brickMouseArea.containsMouse && !hideHovered
            }

            ImageView {
                id: image
                padding: masonryLayout.spacing
                source: model.imageIdUrl
            }

            PersistentSelectionBorder {
                anchors.fill: image
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
                         isCurrent ||
                         masonryView.alwaysShowFileNames ||
                         masonryView.quickSearchMode
                z: 1
                clip: true

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: -radius
                    radius: 4
                    color: brickMouseArea.pressed ? Style.brickInfoPanelPressed : (isCurrent ? Style.brickInfoPanelSelected : Style.brickInfoPanelHovered)
                }


                Text {
                    id: imageText
                    anchors{
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 5
                    }

                    text: model.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: Style.text
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }

                IconLabel {
                    anchors {
                        verticalCenter: parent.verticalCenter
                        right: parent.right
                        rightMargin: 5
                    }

                    visible: model.isPanorama
                    icon.source: "qrc:/resources/Sphere.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: Style.text
                }
            }

            Component.onCompleted: {
                brickDelegate.imageItem = image
            }
        }
    }

    Component {
        id: folderListViewDelegate

        Item {
            Rectangle {
                id: folderViewDelegateBackground
                anchors {
                    fill: folderViewDelegateContent
                    margins: -selectionExtendsFor
                }
                color: brickMouseArea.pressed ? Style.brickPressed : (isCurrent ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isCurrent ? Style.brickSelectedBorder : color
                radius: 4
                visible: isCurrent || brickMouseArea.containsMouse && !hideHovered
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
                        bottom: folderListMasonryLayout.verticalCenter
                        bottomMargin: -1
                    }
                    width: 2

                    color: Style.lighter2
                }

                Rectangle {
                    anchors {
                        left: branchDown.right
                        right: folderListMasonryLayout.left
                        rightMargin: 2
                        verticalCenter: folderListMasonryLayout.verticalCenter
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
                        source: model.iconPath
                    }

                    Text {
                        id: fileName
                        anchors {
                            left: icon.right
                            leftMargin: 5
                            verticalCenter: icon.verticalCenter
                        }
                        text: model.text
                        textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                        elide: Text.ElideRight
                        color: Style.text
                        maximumLineCount: 1
                    }
                }

                MasonryLayout {
                    id: folderListMasonryLayout

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

                    model: brickDelegate.model ? fileListModel.folderModel(brickDelegate.model.index) : null

                    spacing: 2

                    onHeightChanged: {
                        if (height > 0) {
                            targetHeight = height
                        }
                    }
                    Connections {
                        target: masonryLayout
                        function onLayoutReset() {
                            // console.log("on layout reset 2 1")
                            folderListMasonryLayout.reReadAndDecodeThumbnails()
                        }
                    }

                    delegate: BrickItem {
                        id: brck

                        property var model

                        ImageView {
                            id: image2

                            needScaling: false
                            padding: folderListMasonryLayout.spacing
                            source: model !== undefined ? model.imageIdUrl : ""
                        }
                    }
                }
            }
            PersistentSelectionBorder {
                anchors.fill: folderViewDelegateContent
                radius: 3
            }
            Component.onCompleted: {
                brickDelegate.imageItem = folderViewDelegateContent
            }
        }
    }

    Component {
        id: folderGridViewDelegate

        Item {
            id: folderGridViewDelegateItem
            // Rectangle {
            //     id: actualGridSize

            //     x: masonryLayout.spacing / 2 + /*folderGridMasonryLayout.spacing*/2
            //     y: masonryLayout.spacing / 2 + /*folderGridMasonryLayout.spacing*/2 + /*folderGridDelegateTop*/masonryLayout.targetHeight/20
            //     property real canvasWidth: masonryLayout.width - masonryLayout.paddingLeft - masonryLayout.paddingRight
            //     property real averageCellWidth: canvasWidth / Math.floor(canvasWidth / masonryLayout.targetHeight)
            //     width: averageCellWidth - x * 2
            //     height: averageCellWidth - y - (masonryLayout.spacing / 2 + /*folderGridMasonryLayout.spacing*/2) - (/*imageInfoPanel.height*/17 + masonryLayout.spacing)

            //     z: 100
            //     color: "orange"
            //     radius: 10
            //     opacity: 0.2
            // }

            Rectangle {
                id: folderViewGridDelegateBackground
                anchors {
                    fill: folderViewDelegateGridContent
                    margins: -selectionExtendsFor
                }
                color: brickMouseArea.pressed ? Style.brickPressed : (isCurrent ? Style.brickSelected : Style.brickHovered)
                border.width: 1
                border.color: isCurrent ? Style.brickSelectedBorder : color
                radius: 4
                visible: isCurrent || brickMouseArea.containsMouse && !hideHovered
            }

            Item {
                id: folderViewDelegateGridContent
                anchors {
                    fill: parent
                    margins: masonryLayout.spacing / 2
                }

                Rectangle {
                    id: folderTop
                    width: Math.min(110, parent.width * 0.44)
                    height: folderGridDelegateTop + 10*2
                    radius: 4

                    color: folderBackground.color
                }

                // Rectangle {
                //     z: 1
                //     anchors.fill: folderTop
                //     anchors.margins: 1
                //     anchors.bottomMargin: 16
                //     topLeftRadius: 4
                //     topRightRadius: 4
                //     color: "#19191b"
                // }

                Rectangle {
                    id: folderBackground

                    anchors {
                        fill: parent
                        topMargin: folderGridDelegateTop
                        bottomMargin: imageInfoPanel.height + masonryLayout.spacing
                    }
                    color: Style.folderIcon
                    radius: 4

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 4
                        color: Style.isDarkTheme ? "#304051" : "#60b0eb"
                    }

                    MasonryLayout {
                        id: folderGridMasonryLayout

                        anchors {
                            fill: parent
                            margins: folderGridMasonryLayout.spacing * 2
                        }
                        clip: true
                        model: fileListModel.folderModel(brickDelegate.model.index)

                        spacing: 1

                        targetHeight: height
                        // onHeightChanged: {
                        //     if (height > 0) {
                        //         targetHeight = height / 3
                        //     }
                        // }
                        Connections {
                            target: masonryLayout
                            function onLayoutReset() {
                                // console.log("on layout reset 2 2")
                                folderGridMasonryLayout.reReadAndDecodeThumbnails()
                            }
                        }

                        delegate: BrickItem {
                            id: brck

                            property var model

                            ImageView {
                                id: image2

                                needScaling: false
                                padding: folderGridMasonryLayout.spacing
                                source: model !== undefined ? model.imageIdUrl : ""
                            }
                        }

                        Component.onCompleted: {
                            brickDelegate.imageItem = folderViewDelegateGridContent
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

                        text: model.text
                        textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                        horizontalAlignment: masonryLayout.listView ? Text.AlignLeft : Text.AlignHCenter
                        elide: Text.ElideRight
                        color: Style.text
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        // renderType: Text.CurveRendering
                    }
                }
            }

            PersistentSelectionBorder {
                anchors.fill: folderViewDelegateGridContent
            }
        }
    }

    Item {
        id: draggable

        property var dragUrls: []
        property var dragPreviewItems: []
        property int dragPreviewTotalCount: 0
        property int dragPreviewRemainingCount: 0

        anchors.fill: parent
        Drag.active: brickMouseArea.drag.active
        Drag.dragType: Drag.Automatic
        Drag.mimeData: ({
            "text/uri-list": dragUrls
        })

        function updateDragPayload(sourceIndex) {
            dragUrls = fileListModel.dragUrlsForIndex(sourceIndex)
            let preview = fileListModel.dragPreviewItemsForIndex(sourceIndex, 5)
            dragPreviewItems = preview.items || []
            dragPreviewTotalCount = preview.totalCount || dragPreviewItems.length
            dragPreviewRemainingCount = preview.remainingCount || 0
            draggable.Drag.imageSource = ""
        }

        function grabCompactPreview() {
            dragPreviewSource.visible = true
            Qt.callLater(function() {
                dragPreviewSource.grabToImage(function(result) {
                    draggable.Drag.imageSource = result.url
                    dragPreviewSource.visible = false
                })
            })
        }

        Item {
            id: dragPreviewSource

            x: -width - 1000
            y: -height - 1000
            width: dragPreviewRow.implicitWidth + 12
            height: 58
            visible: false

            Rectangle {
                anchors.fill: parent
                radius: 6
                color: Style.popupBackground
                border.width: 1
                border.color: Style.popupBorder
            }

            Row {
                id: dragPreviewRow
                anchors.centerIn: parent
                spacing: 6

                Repeater {
                    model: draggable.dragPreviewItems

                    Rectangle {
                        width: 46
                        height: 46
                        radius: 4
                        color: Style.darker
                        border.width: 1
                        border.color: Style.lighter2
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: 3
                            source: modelData.imageIdUrl !== "" ? modelData.imageIdUrl : modelData.iconPath
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: modelData.imageIdUrl !== "" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                        }
                    }
                }

                Rectangle {
                    width: 46
                    height: 46
                    radius: 4
                    color: Style.lighter
                    border.width: 1
                    border.color: Style.persistentSelectionBorder
                    visible: draggable.dragPreviewRemainingCount > 0

                    Text {
                        anchors.centerIn: parent
                        text: "+" + draggable.dragPreviewRemainingCount
                        color: Style.text
                        font.bold: true
                    }
                }
            }
        }
    }

    MouseArea {
        id: brickMouseArea
        anchors.fill: parent
        hoverEnabled: true

        drag.target: draggable

        onPressed:
            (mouse) => {
                draggable.updateDragPayload(model.index)

                if (imageItem !== undefined) {
                    let mappedPos = imageItem.mapFromItem(brickMouseArea, mouse.x, mouse.y)
                    if (draggable.dragPreviewTotalCount > 1) {
                        draggable.Drag.hotSpot.x = 18
                        draggable.Drag.hotSpot.y = 18
                        draggable.grabCompactPreview()
                    }
                    else {
                        draggable.Drag.hotSpot.x = mappedPos.x
                        draggable.Drag.hotSpot.y = mappedPos.y
                        imageItem.grabToImage(function(result) {
                            draggable.Drag.imageSource = result.url
                        })
                    }
                }

                focusProxy.forceActiveFocus()

                if (scrollingMode) {
                    endScrolling()
                }

                if (mouse.modifiers & Qt.ShiftModifier) {
                    masonryView.shiftClickSelection(model.index)
                    return
                }

                if (mouse.modifiers & Qt.ControlModifier) {
                    setCurrentIndex(model.index)
                    fileListModel.toggleSelection(model.index)
                    return
                }

                setCurrentIndex(model.index)
        }

        onDoubleClicked: {
            if (model.isFolder) {
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                viewerController.cd(model.fullPath)
            }
            else {
                if (masonryLayout.currentItem.model.isImage) {
                    masonryView.toggleViewer()
                }
            }
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        asynchronous: true
    }

    states: [
        State {
            when: model === undefined
            PropertyChanges { loader.sourceComponent: undefined }
        },
        State {
            when: model !== undefined && masonryLayout.listView && model.folderView
            PropertyChanges { loader.sourceComponent: folderListViewDelegate }
        },
        State {
            when: model !== undefined && model.folderView
            PropertyChanges { loader.sourceComponent: folderGridViewDelegate }
        },
        State {
            when: model !== undefined && (model.imageIdUrl !== "" || model.isShowAsImage)
            PropertyChanges { loader.sourceComponent: imageDelegate }
        },
        State {
            when: model !== undefined && masonryLayout.listView && model.isFolder
            PropertyChanges { loader.sourceComponent: fileListDelegate }
        },
        State {
            when: model !== undefined
            PropertyChanges { loader.sourceComponent: fileDelegate }
        }
    ]
}
