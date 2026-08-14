import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

import ZoinGallery.Native 1.0

BrickItem {
    id: brick

    required property var panelRoot
    property var model
    property int viewIndex: -1
    property int sourceIndex: -1
    property alias thumbnailItem: thumbnail

    readonly property string mode: panelRoot.presentationMode
    readonly property bool masonryMode: mode === "masonry"
    readonly property bool columnsMode: mode === "columns"
    readonly property bool detailsMode: mode === "details"
    readonly property bool gridMode: mode === "grid"
    readonly property bool iconsMode: mode === "icons"
    readonly property bool largePreviewMode:
        masonryMode || gridMode || iconsMode
    readonly property real renderDpr:
        Math.max(0.01, Number(panelRoot.devicePixelRatio) || 1)
    readonly property string entryId:
        panelRoot.session && viewIndex >= 0
        ? panelRoot.session.entryIdAt(viewIndex) : ""
    readonly property bool current:
        panelRoot.visualCursorIndex === viewIndex
    readonly property bool selected: panelRoot.effectiveEntrySelected(
        entryId, Boolean(model && model.isSelected))
    readonly property bool cursorChromeSuppressed:
        current && panelRoot.cursorChromeTransitionActive
    readonly property bool cursorChromeExposesUnderlay:
        panelRoot.cursorChromeTransitionActive
        && panelRoot.cursorChromeCoveredIndex === viewIndex
    readonly property var displayFields:
        model && model.displayFields ? model.displayFields : ({})
    readonly property string displayName:
        model ? model.text : ""
    readonly property string displayBaseName:
        displayFields.displayBaseName !== undefined
        ? displayFields.displayBaseName : displayName
    readonly property string displayExtension:
        displayFields.displayExtension || ""
    readonly property string displaySize:
        displayFields.sizeText || ""
    readonly property bool hiddenEntry:
        Boolean(displayFields.isHidden)
    readonly property var highlightStyle:
        model && model.highlightStyle ? model.highlightStyle : ({})
    readonly property var highlightPatch: {
        const cursor = current && panelRoot.showCursor
        if (cursor && selected)
            return highlightStyle.selectedCursor || ({})
        if (cursor)
            return highlightStyle.cursor || ({})
        if (selected)
            return highlightStyle.selected || ({})
        return highlightStyle.normal || ({})
    }
    readonly property string highlightForegroundValue:
        highlightPatch.foreground || ""
    readonly property string highlightBackgroundValue:
        highlightPatch.background || ""
    readonly property var nonCursorHighlightPatch:
        selected ? (highlightStyle.selected || ({}))
                 : (highlightStyle.normal || ({}))
    readonly property string nonCursorHighlightBackgroundValue:
        nonCursorHighlightPatch.background || ""
    readonly property color highlightForeground:
        selected
        ? panelRoot.markedTextColor
        : (highlightForegroundValue !== ""
           ? highlightForegroundValue : panelRoot.foregroundColor)
    // Selection is a persistent state and therefore has higher visual
    // priority than the transient cursor. In particular, a selected folder
    // keeps its yellow Lucide icon while the cursor passes over it instead of
    // being recoloured to the cursor's white foreground.
    readonly property color fallbackIconColor:
        selected
        ? panelRoot.markedTextColor
        : (model && model.isFolder
           ? (current && panelRoot.showCursor
              ? panelRoot.foregroundColor : panelRoot.folderIconColor)
           : (highlightForegroundValue !== ""
              ? highlightForeground : panelRoot.mutedColor))
    readonly property color highlightLabelBackground:
        highlightPatch.background || "#aa101216"
    readonly property rect effectivePreviewRect: {
        if (detailsMode) {
            return Qt.rect(panelRoot.detailsRowInset,
                           Math.max(0, (height
                                        - panelRoot.detailsIconSlotSize) / 2),
                           panelRoot.detailsIconSlotSize,
                           panelRoot.detailsIconSlotSize)
        }
        const rect = brick.previewRect
        if (rect && rect.width > 0 && rect.height > 0)
            return rect
        const inset = masonryMode || gridMode ? 4 : 3
        return Qt.rect(inset, inset,
                       Math.max(0, width - inset * 2),
                       Math.max(0, height - inset * 2))
    }

    // IconLabel implements icon.color by treating the source as a mask.  That
    // is correct for the monochrome Lucide set, but destroys the native
    // colours of system/file icons supplied by an image provider (and of
    // user-provided icon files).  Keep the distinction source-based so the
    // rendering policy does not depend on the current palette.
    function isLucideIconSource(source) {
        const value = source ? source.toString() : ""
        if (value.indexOf("qrc:/F4QtHost/icons/lucide/") === 0
                || value.indexOf("qrc:/F4QtHost/icons/lucide-gallery/") === 0)
            return true

        const scheme = value.indexOf("://")
        if (scheme < 0)
            return false
        const path = value.indexOf("/", scheme + 3)
        return path >= 0 && value.indexOf("/lucide/", path) === path
    }

    Rectangle {
        id: selectionSurface
        objectName: "gallerySelectionSurface-" + brick.viewIndex
        readonly property real visualBorderWidth: border.width
        readonly property color visualBorderColor: border.color
        readonly property real surfaceInset: brick.detailsMode ? 0
                                              : (brick.columnsMode ? 1 : 2)
        x: surfaceInset
        y: surfaceInset
        width: Math.max(0, parent.width - surfaceInset * 2)
        // Icons rows share the tallest brick's layout stride, but a short
        // filename must not inherit the empty label space required by a
        // neighbour with several wrapped lines. Keep hit testing/navigation
        // on the full row geometry and size only the painted selection chrome
        // to this brick's actual preview + rendered text content.
        height: brick.iconsMode
                ? Math.max(0, Math.min(parent.height - surfaceInset * 2,
                    gridLabel.y + gridLabel.contentHeight + 3
                    - surfaceInset))
                : Math.max(0, parent.height - surfaceInset * 2)
        radius: brick.columnsMode || brick.detailsMode ? 4 : 6
        color: {
            if (brick.cursorChromeExposesUnderlay)
                return "transparent"
            if (brick.detailsMode) {
                if (brick.cursorChromeSuppressed) {
                    if (!brick.selected
                            && brick.nonCursorHighlightBackgroundValue !== "")
                        return brick.nonCursorHighlightBackgroundValue
                    return "transparent"
                }
                if ((!brick.selected || (brick.current
                                          && brick.panelRoot.showCursor))
                        && brick.highlightBackgroundValue !== "")
                    return brick.highlightBackgroundValue
                if (brick.current && brick.panelRoot.showCursor)
                    return brick.panelRoot.cursorBackgroundColor
                return "transparent"
            }
            if (brick.current && brick.panelRoot.showCursor
                    && !brick.cursorChromeSuppressed)
                return brick.panelRoot.cursorColor
            if (brick.selected)
                return "transparent"
            if (brickPointer.containsMouse && !brick.iconsMode)
                return Qt.lighter(brick.panelRoot.backgroundColor, 1.25)
            if (brick.masonryMode || brick.gridMode)
                return Qt.lighter(brick.panelRoot.backgroundColor,
                                  brick.model && brick.model.isFolder ? 1.20 : 1.09)
            return "transparent"
        }
        border.width: {
            if (brick.cursorChromeExposesUnderlay)
                return 0
            // Compact presentations communicate persistent selection through
            // the marked foreground only.  Keep the thin border exclusively
            // for the transient keyboard cursor; a selected non-current row
            // must not acquire the large coloured card outline used by the
            // image-centric presentations.
            if (brick.detailsMode || brick.columnsMode) {
                return brick.current && brick.panelRoot.showCursor
                        && !brick.cursorChromeSuppressed ? 1 : 0
            }
            if (brick.selected)
                return 3
            return brick.current && brick.panelRoot.showCursor
                    && !brick.cursorChromeSuppressed ? 1 : 0
        }
        border.color: brick.detailsMode
            ? brick.panelRoot.cursorBorderColor
            : (brick.columnsMode
               ? Qt.lighter(brick.panelRoot.cursorColor, 1.35)
               : (brick.selected
               ? brick.panelRoot.selectionColor
               : (brick.current && brick.panelRoot.showCursor
                  ? Qt.lighter(brick.panelRoot.cursorColor, 1.35)
                  : brick.panelRoot.selectionColor)))
    }


    // Details deliberately uses the same layout tree as f4's classic
    // Detailed delegate. RowLayout centers each Text at its implicit height;
    // stretching a Label to the full fractional row height changes its
    // baseline and antialiasing at DPR 2.
    RowLayout {
        id: detailsRow
        objectName: brick.detailsMode ? "galleryDetailsRow-"
                                        + brick.viewIndex : ""
        visible: brick.detailsMode
        opacity: brick.hiddenEntry ? 0.5 : 1.0
        anchors.fill: parent
        anchors.leftMargin: brick.panelRoot.detailsRowInset
        anchors.rightMargin: brick.panelRoot.detailsRowInset
        spacing: brick.panelRoot.detailsRowSpacing

        Item {
            id: detailsIconSlot
            objectName: brick.detailsMode ? "galleryDetailsIconSlot-"
                                            + brick.viewIndex : ""
            Layout.preferredWidth: brick.panelRoot.detailsIconSlotSize
            Layout.preferredHeight: brick.panelRoot.detailsIconSlotSize

            IconLabel {
                id: detailsFallbackIcon
                objectName: brick.detailsMode ? "galleryFallbackIcon-"
                                                + brick.viewIndex : ""
                readonly property color effectiveIconColor:
                    brick.fallbackIconColor
                width: brick.panelRoot.detailsIconSize
                height: brick.panelRoot.detailsIconSize
                anchors.centerIn: parent
                readonly property bool lucideSource:
                    brick.isLucideIconSource(
                        brick.model ? brick.model.iconPath : "")
                visible: Boolean(lucideSource
                    && brick.model
                    && thumbnail.source.toString() === ""
                    && brick.model.iconPath.toString() !== ""
                    && !(brick.panelRoot.viewerTransitionActive
                         && brick.panelRoot.viewerTransitionEntryId
                            === brick.entryId))
                icon.source: brick.model ? brick.model.iconPath : ""
                icon.width: brick.panelRoot.detailsIconSize
                icon.height: brick.panelRoot.detailsIconSize
                icon.color: effectiveIconColor
            }

            Image {
                id: detailsSourceColorIcon
                objectName: brick.detailsMode
                    ? "gallerySourceColorIcon-" + brick.viewIndex : ""
                anchors.centerIn: parent
                width: brick.panelRoot.detailsIconSize
                height: brick.panelRoot.detailsIconSize
                source: brick.model ? brick.model.iconPath : ""
                fillMode: Image.PreserveAspectFit
                smooth: true
                cache: false
                opacity: 1
                visible: Boolean(
                    brick.model
                    && source.toString() !== ""
                    && !brick.isLucideIconSource(source)
                    && thumbnail.source.toString() === ""
                    && !(brick.panelRoot.viewerTransitionActive
                         && brick.panelRoot.viewerTransitionEntryId
                            === brick.entryId))
            }

            Text {
                objectName: brick.detailsMode ? "galleryFallbackMarker-"
                                                + brick.viewIndex : ""
                anchors.centerIn: parent
                visible: thumbnail.source.toString() === ""
                         && (!brick.model
                             || brick.model.iconPath.toString() === "")
                text: brick.highlightStyle.marker || (brick.model
                      && brick.model.isFolder
                      ? (brick.displayName === ".." ? "↰" : "▸") : " ")
                color: brick.fallbackIconColor
                font.pixelSize: brick.panelRoot.detailsNameFontPixelSize
            }
        }

        Text {
            id: detailsBaseName
            objectName: brick.detailsMode ? "galleryBaseName-"
                                            + brick.viewIndex : ""
            text: brick.panelRoot.separateFileExtensions
                ? brick.displayBaseName
                : brick.displayBaseName
                  + (brick.displayExtension !== ""
                     ? "." + brick.displayExtension : "")
            color: brick.highlightForegroundValue !== "" || brick.selected
                   ? brick.highlightForeground
                   : brick.panelRoot.foregroundColor
            elide: Text.ElideMiddle
            font.pixelSize: brick.panelRoot.detailsNameFontPixelSize
            Layout.fillWidth: true
        }

        Text {
            id: detailsExtension
            objectName: brick.detailsMode ? "galleryExtension-"
                                            + brick.viewIndex : ""
            text: brick.displayExtension
            visible: brick.panelRoot.separateFileExtensions
                     && text.length > 0
            color: brick.highlightForegroundValue !== "" || brick.selected
                   ? brick.highlightForeground
                   : brick.panelRoot.mutedColor
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            font.pixelSize: brick.panelRoot.detailsSecondaryFontPixelSize
            Layout.preferredWidth: Math.min(
                brick.panelRoot.detailsExtensionMaximumWidth,
                Math.max(brick.panelRoot.detailsExtensionMinimumWidth,
                         implicitWidth))
        }

        Text {
            id: detailsSize
            objectName: brick.detailsMode ? "gallerySize-"
                                            + brick.viewIndex : ""
            text: brick.displaySize
            color: brick.highlightForegroundValue !== "" || brick.selected
                   ? brick.highlightForeground
                   : brick.panelRoot.mutedColor
            horizontalAlignment: Text.AlignRight
            font.pixelSize: brick.panelRoot.detailsSecondaryFontPixelSize
            Layout.preferredWidth: brick.panelRoot.detailsSizeColumnWidth
        }
    }

    Item {
        id: previewContainer
        // Details reuses the exact same provider source and shader as every
        // other presentation, but reparents the surface into the classic
        // 18x18 icon slot.  This keeps the pixel-perfect row geometry while
        // avoiding a second Image/provider request for the same entry.
        parent: brick.detailsMode ? detailsIconSlot : brick
        visible: true
        // In Details this item is already a child of the dimmed detailsRow.
        // Other presentations dim only their icon/thumbnail payload; cursor,
        // selection, hover, and card chrome remain fully opaque siblings.
        opacity: brick.hiddenEntry && !brick.detailsMode ? 0.5 : 1.0
        x: brick.detailsMode ? 0 : brick.effectivePreviewRect.x
        y: brick.detailsMode ? 0 : brick.effectivePreviewRect.y
        width: brick.detailsMode ? parent.width
                                 : brick.effectivePreviewRect.width
        height: brick.detailsMode ? parent.height
                                  : brick.effectivePreviewRect.height
        clip: true

        IconLabel {
            id: fallbackIcon
            objectName: brick.detailsMode ? ""
                                         : "galleryFallbackIcon-"
                                           + brick.viewIndex
            readonly property color effectiveIconColor:
                brick.fallbackIconColor
            property url source: {
                const value = brick.model ? brick.model.iconPath.toString() : ""
                const prefix = "qrc:/F4QtHost/icons/lucide/"
                // The thinner Gallery pack compensates for genuinely large
                // icon rendering. Columns is a compact text presentation just
                // like Details, so both must use the same ordinary Lucide SVG
                // and the same 16px visual metrics.
                if (brick.largePreviewMode && value.indexOf(prefix) === 0)
                    return "qrc:/F4QtHost/icons/lucide-gallery/"
                            + value.substring(prefix.length)
                return value
            }
            anchors.centerIn: parent
            width: brick.detailsMode || brick.columnsMode
                   ? Math.min(parent.width, brick.panelRoot.detailsIconSize)
                   : brick.iconsMode
                   ? Math.max(0, Math.min(parent.width, parent.height))
                   : Math.max(0, Math.min(parent.width, parent.height)
                              * 0.55)
            height: width
            icon.source: source
            icon.width: width
            icon.height: height
            icon.color: effectiveIconColor
            opacity: brick.detailsMode || brick.columnsMode ? 1 : 0.78
            visible: !brick.detailsMode
                     && brick.isLucideIconSource(source)
                     && thumbnail.source.toString() === ""
                     && source.toString() !== ""
                     && !(brick.panelRoot.viewerTransitionActive
                          && brick.panelRoot.viewerTransitionEntryId
                             === brick.entryId)
        }


        Image {
            id: sourceColorFallbackIcon
            objectName: brick.detailsMode ? ""
                                         : "gallerySourceColorIcon-"
                                           + brick.viewIndex
            anchors.centerIn: parent
            width: fallbackIcon.width
            height: fallbackIcon.height
            source: fallbackIcon.source
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            opacity: 1
            visible: !brick.detailsMode
                     && !brick.isLucideIconSource(source)
                     && thumbnail.source.toString() === ""
                     && source.toString() !== ""
                     && !(brick.panelRoot.viewerTransitionActive
                          && brick.panelRoot.viewerTransitionEntryId
                             === brick.entryId)
        }

        Text {
            objectName: brick.detailsMode ? ""
                                         : "galleryFallbackMarker-"
                                           + brick.viewIndex
            anchors.centerIn: parent
            visible: brick.detailsMode
                     && thumbnail.source.toString() === ""
                     && fallbackIcon.source.toString() === ""
            text: brick.highlightStyle.marker || (brick.model
                  && brick.model.isFolder
                  ? (brick.displayName === ".." ? "↰" : "▸") : " ")
            color: brick.fallbackIconColor
            font.pixelSize: brick.panelRoot.detailsNameFontPixelSize
        }

        Item {
            id: thumbnail
            objectName: "galleryThumbnail-" + brick.viewIndex
            x: 0
            y: 0
            width: Math.round(parent.width * brick.renderDpr)
                   / brick.renderDpr
            height: Math.round(parent.height * brick.renderDpr)
                    / brick.renderDpr
            property url source: brick.model ? brick.model.imageIdUrl : ""
            visible: source !== ""
                     && !(brick.panelRoot.viewerTransitionActive
                          && brick.panelRoot.viewerTransitionEntryId
                             === brick.entryId)

            // Keep one source item for every renderer so presentation changes
            // never request or retain another decoded frame. As in the
            // standalone ImageView, the source stays hidden and feeds the
            // unsharp-mask, checkerboard and rounded-corner shader. Each mode
            // retains its established Crop/Fit policy below.
            Rectangle {
                objectName: "galleryThumbnailBackdrop-" + brick.viewIndex
                readonly property bool enabledForPresentation:
                    brick.masonryMode || brick.gridMode
                anchors.fill: parent
                radius: 4
                color: Qt.styleHints.colorScheme === Qt.Dark
                       ? Qt.rgba(0, 0, 0, 0.3)
                       : Qt.rgba(0, 0, 0, 0.2)
                // Compact Columns/Details slots must remain visually clean:
                // no card/checkerboard layer underneath their small icon or
                // preview.  Larger image-centric presentations retain the
                // loading backdrop and transparency affordance.
                visible: enabledForPresentation
                         && thumbnailSource.status !== Image.Ready
            }

            Image {
                id: thumbnailSource
                objectName: "galleryThumbnailImage-" + brick.viewIndex
                property int xDiff:
                    implicitWidth - thumbnail.width
                                    * brick.renderDpr
                property int yDiff:
                    implicitHeight - thumbnail.height
                                     * brick.renderDpr
                property bool diffIsSmall:
                    Math.abs(xDiff) < 4 && Math.abs(yDiff) < 4
                width: diffIsSmall
                       ? implicitWidth / brick.renderDpr
                       : thumbnail.width
                height: diffIsSmall
                        ? implicitHeight / brick.renderDpr
                        : thumbnail.height
                source: thumbnail.source
                fillMode: diffIsSmall
                          ? Image.Pad
                          : (brick.masonryMode
                             ? Image.PreserveAspectCrop
                             : Image.PreserveAspectFit)
                asynchronous: false
                cache: false
                visible: false
            }

            ShaderEffect {
                id: thumbnailShader
                objectName: "galleryThumbnailShader-" + brick.viewIndex

                // ShaderEffect samples the Image's raw provider texture; it
                // does not inherit QQuickImage's PreserveAspectFit geometry.
                // Size the visible quad to the fitted texture explicitly for
                // every fixed renderer. Masonry already gives the brick the
                // decoded image aspect and keeps its original full surface.
                readonly property real textureAspect:
                    thumbnailSource.implicitWidth > 0
                    && thumbnailSource.implicitHeight > 0
                    ? thumbnailSource.implicitWidth
                      / thumbnailSource.implicitHeight : 0
                readonly property real fittedWidth:
                    brick.masonryMode || textureAspect <= 0
                    ? thumbnail.width
                    : Math.min(thumbnail.width,
                               thumbnail.height * textureAspect)
                readonly property real fittedHeight:
                    brick.masonryMode || textureAspect <= 0
                    ? thumbnail.height
                    : Math.min(thumbnail.height,
                               thumbnail.width / textureAspect)
                x: (thumbnail.width - width) / 2
                y: (thumbnail.height - height) / 2
                width: fittedWidth
                height: fittedHeight

                property var source: thumbnailSource
                property size viewportSize: Qt.size(
                    width * brick.renderDpr,
                    height * brick.renderDpr)
                property real sharpenAmount: 1.5
                property bool showCheckerboard:
                    brick.masonryMode || brick.gridMode
                property int checkerboardSize:
                    4 * brick.renderDpr
                property real borderRadius:
                    4.1 * brick.renderDpr

                fragmentShader:
                    "qrc:/ZoinGallery/resources/shader.frag.qsb"
                visible: thumbnail.source.toString() !== ""
            }
        }
    }

    Label {
        id: masonryLabel
        objectName: brick.masonryMode
                    ? "galleryMasonryLabel-" + brick.viewIndex : ""
        visible: brick.masonryMode
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        padding: 6
        text: brick.model ? brick.displayName : ""
        color: brick.hiddenEntry
               ? Qt.rgba(brick.highlightForeground.r,
                         brick.highlightForeground.g,
                         brick.highlightForeground.b,
                         brick.highlightForeground.a * 0.5)
               : brick.highlightForeground
        elide: Text.ElideMiddle
        horizontalAlignment: Text.AlignHCenter
        background: Rectangle {
            color: brick.highlightLabelBackground
            radius: 3
        }
    }

    Label {
        id: gridLabel
        objectName: brick.iconsMode
                    ? "galleryIconsLabel-" + brick.viewIndex
                    : (brick.gridMode
                       ? "galleryGridLabel-" + brick.viewIndex : "")
        visible: brick.gridMode || brick.iconsMode
        x: 4
        y: brick.effectivePreviewRect.y + brick.effectivePreviewRect.height + 3
        width: Math.max(0, parent.width - 8)
        height: Math.max(0, parent.height - y - 3)
        text: brick.iconsMode ? brick.iconLabelText : brick.displayName
        font: brick.panelRoot.iconLabelFont
        color: brick.hiddenEntry
               ? Qt.rgba(brick.highlightForeground.r,
                         brick.highlightForeground.g,
                         brick.highlightForeground.b,
                         brick.highlightForeground.a * 0.5)
               : brick.highlightForeground
        elide: brick.iconsMode ? Text.ElideNone : Text.ElideMiddle
        wrapMode: brick.iconsMode
                  ? Text.WrapAnywhere : Text.NoWrap
        maximumLineCount: brick.iconsMode ? 4 : 1
        clip: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
    }

    Item {
        id: textRow
        visible: brick.columnsMode
        opacity: brick.hiddenEntry ? 0.5 : 1.0
        x: brick.detailsMode ? 0
           : brick.effectivePreviewRect.x
             + brick.effectivePreviewRect.width + 6
        y: 0
        width: brick.detailsMode ? parent.width
               : Math.max(0, parent.width - x - 7)
        height: parent.height
        readonly property real gap: brick.detailsMode
            ? brick.panelRoot.detailsRowSpacing : 4
        Text {
            id: extensionMeasurement
            visible: false
            text: extensionLabel.text
            font: extensionLabel.font
        }
        readonly property real sizeColumnWidth: brick.detailsMode
            ? brick.panelRoot.detailsSizeColumnWidth : 0
        readonly property real extensionColumnWidth: {
            if (!brick.panelRoot.separateFileExtensions)
                return 0
            if (brick.detailsMode) {
                return Math.min(
                    brick.panelRoot.detailsExtensionMaximumWidth,
                    Math.max(brick.panelRoot.detailsExtensionMinimumWidth,
                             extensionMeasurement.implicitWidth))
            }
            const available = Math.max(
                0, width - sizeColumnWidth
                   - (brick.detailsMode ? gap : 0))
            const preferred = Math.max(40, available * 0.28)
            return Math.max(0, Math.min(112, preferred, available * 0.45))
        }

        Label {
            id: baseNameLabel
            objectName: brick.detailsMode ? ""
                                         : "galleryBaseName-"
                                           + brick.viewIndex
            anchors.left: parent.left
            anchors.leftMargin: brick.detailsMode
                ? brick.panelRoot.detailsRowInset
                  + brick.panelRoot.detailsIconSlotSize
                  + brick.panelRoot.detailsRowSpacing : 0
            anchors.right: extensionLabel.visible
                ? extensionLabel.left
                : (sizeLabel.visible ? sizeLabel.left : parent.right)
            anchors.rightMargin: extensionLabel.visible || sizeLabel.visible
                ? parent.gap : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            text: brick.panelRoot.separateFileExtensions
                ? brick.displayBaseName
                : brick.displayBaseName
                  + (brick.displayExtension !== ""
                     ? "." + brick.displayExtension : "")
            color: brick.detailsMode
                   ? (brick.highlightForegroundValue !== "" || brick.selected
                      ? brick.highlightForeground
                      : brick.panelRoot.foregroundColor)
                   : brick.highlightForeground
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: brick.detailsMode
                            ? brick.panelRoot.detailsNameFontPixelSize : -1
        }

        Label {
            id: extensionLabel
            objectName: brick.detailsMode ? ""
                                         : "galleryExtension-"
                                           + brick.viewIndex
            visible: brick.panelRoot.separateFileExtensions
                     && brick.displayExtension !== ""
            anchors.right: sizeLabel.visible ? sizeLabel.left : parent.right
            anchors.rightMargin: sizeLabel.visible ? parent.gap : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.extensionColumnWidth
            text: brick.displayExtension !== ""
                ? (brick.detailsMode ? brick.displayExtension
                   : "." + brick.displayExtension) : ""
            color: brick.detailsMode
                   ? (brick.highlightForegroundValue !== "" || brick.selected
                      ? brick.highlightForeground
                      : brick.panelRoot.mutedColor)
                   : brick.highlightForeground
            horizontalAlignment: brick.detailsMode
                                 ? Text.AlignLeft : Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: brick.detailsMode ? Text.ElideRight : Text.ElideLeft
            font.pixelSize: brick.detailsMode
                            ? brick.panelRoot.detailsSecondaryFontPixelSize : -1
        }

        Label {
            id: sizeLabel
            objectName: brick.detailsMode ? ""
                                         : "gallerySize-"
                                           + brick.viewIndex
            visible: brick.detailsMode
            anchors.right: parent.right
            anchors.rightMargin: brick.panelRoot.detailsRowInset
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.sizeColumnWidth
            text: brick.displaySize
            color: brick.highlightForegroundValue !== "" || brick.selected
                   ? brick.highlightForeground
                   : brick.panelRoot.mutedColor
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideNone
            font.pixelSize: brick.panelRoot.detailsSecondaryFontPixelSize
        }
    }

    Rectangle {
        objectName: "gallerySelectionIndicator-" + brick.viewIndex
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 7
        width: 14
        height: 14
        radius: 7
        color: brick.panelRoot.selectionColor
        border.width: 2
        border.color: brick.panelRoot.foregroundColor
        visible: false
        z: 2
    }

    MouseArea {
        id: brickPointer
        objectName: "galleryBrickPointer-" + brick.viewIndex
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        preventStealing: true
        onPressed: mouse => {
            brick.panelRoot.handlePointerPress(brick.viewIndex,
                                               mouse.button,
                                               mouse.modifiers)
            mouse.accepted = true
        }
        onDoubleClicked: mouse => {
            if ((mouse.button & Qt.LeftButton) !== 0)
                brick.panelRoot.selectIndex(brick.viewIndex, true)
            mouse.accepted = true
        }
    }
}
