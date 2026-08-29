import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

import ZoinGallery.Native 1.0

BrickItem {
    id: brick

    required property var panelRoot
    property var model
    // Embedded catalog resets paint from the current model's POD snapshot.
    // Keeping this facade authoritative after ImageFile materialization makes
    // the later QObject pointer assignment invisible to the large delegate
    // tree; thumbnails and metadata update the same snapshot role.
    readonly property var effectiveModel:
        visualModel && visualModel.valid === true ? visualModel : model
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
    // Icon leaves are centered inside several presentation-specific wrapper
    // hierarchies. At fractional scale an integral logical center can still
    // land between physical pixels (13 DIPs is 22.75 px at 175%). Keep a
    // broad, bindable dependency for the non-bindable mapToItem() lookup and
    // translate the leaf only; no ancestor or raster texture is rescaled.
    readonly property real iconPixelAlignmentRevision: {
        const viewport = panelRoot.cursorPixelGridViewportOrigin
        return Number(viewport.x || 0) + Number(viewport.y || 0)
                + x + y + width + height
                + previewContainer.x + previewContainer.y
                + previewContainer.width + previewContainer.height
                + detailsRow.x + detailsRow.y
                + detailsIconSlot.x + detailsIconSlot.y
                + mode.length
    }
    function snapIconExtent(value) {
        return Math.max(0, Math.round(Number(value || 0) * renderDpr)
                           / renderDpr)
    }
    function iconPixelOffsetX(item) {
        if (!item || !item.parent)
            return 0
        const revision = iconPixelAlignmentRevision
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return Math.round(scenePoint.x * renderDpr) / renderDpr
                - scenePoint.x + revision * 0
    }
    function iconPixelOffsetY(item) {
        if (!item || !item.parent)
            return 0
        const revision = iconPixelAlignmentRevision
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return Math.round(scenePoint.y * renderDpr) / renderDpr
                - scenePoint.y + revision * 0
    }
    readonly property string entryId:
        effectiveModel && effectiveModel.entryId !== undefined
        ? effectiveModel.entryId
        : (panelRoot.session && viewIndex >= 0
           ? panelRoot.session.entryIdAt(viewIndex) : "")
    readonly property bool current:
        panelRoot.visualCursorIndex === viewIndex
    readonly property bool selected: panelRoot.effectiveEntrySelected(
        entryId, Boolean(effectiveModel && effectiveModel.isSelected))
    readonly property bool cursorChromeSuppressed:
        current && panelRoot.cursorChromeTransitionActive
    readonly property bool cursorChromeExposesUnderlay:
        panelRoot.cursorChromeTransitionActive
        && panelRoot.cursorChromeCoveredIndex === viewIndex
    readonly property bool typedVisualModel: effectiveModel === visualModel
    readonly property var legacyDisplayFields:
        !typedVisualModel && effectiveModel && effectiveModel.displayFields
        ? effectiveModel.displayFields : ({})
    readonly property string displayName:
        visualFacadeReady && model ? model.text
                                   : (effectiveModel
                                      ? effectiveModel.text : "")
    readonly property string displayBaseName:
        typedVisualModel
        ? visualModel.displayBaseName
        : (legacyDisplayFields.displayBaseName !== undefined
           ? legacyDisplayFields.displayBaseName : displayName)
    readonly property string displayExtension:
        typedVisualModel ? visualModel.displayExtension
                         : (legacyDisplayFields.displayExtension || "")
    readonly property string displaySize:
        typedVisualModel ? visualModel.sizeText
                         : (legacyDisplayFields.sizeText || "")
    readonly property bool hiddenEntry:
        typedVisualModel ? visualModel.isHidden
                         : Boolean(legacyDisplayFields.isHidden)
    readonly property var legacyHighlightStyle:
        !typedVisualModel && effectiveModel && effectiveModel.highlightStyle
        ? effectiveModel.highlightStyle : ({})
    readonly property string highlightMarker:
        typedVisualModel ? visualModel.highlightMarker
                         : (legacyHighlightStyle.marker || "")
    readonly property string highlightForegroundValue: {
        const cursor = current && panelRoot.showCursor
        if (cursor && selected)
            return typedVisualModel
                    ? visualModel.selectedCursorForeground
                    : ((legacyHighlightStyle.selectedCursor || ({}))
                       .foreground || "")
        if (cursor)
            return typedVisualModel
                    ? visualModel.cursorForeground
                    : ((legacyHighlightStyle.cursor || ({})).foreground || "")
        if (selected)
            return typedVisualModel
                    ? visualModel.selectedForeground
                    : ((legacyHighlightStyle.selected || ({})).foreground || "")
        return typedVisualModel
                ? visualModel.normalForeground
                : ((legacyHighlightStyle.normal || ({})).foreground || "")
    }
    readonly property string highlightBackgroundValue: {
        const cursor = current && panelRoot.showCursor
        if (cursor && selected)
            return typedVisualModel
                    ? visualModel.selectedCursorBackground
                    : ((legacyHighlightStyle.selectedCursor || ({}))
                       .background || "")
        if (cursor)
            return typedVisualModel
                    ? visualModel.cursorBackground
                    : ((legacyHighlightStyle.cursor || ({})).background || "")
        if (selected)
            return typedVisualModel
                    ? visualModel.selectedBackground
                    : ((legacyHighlightStyle.selected || ({})).background || "")
        return typedVisualModel
                ? visualModel.normalBackground
                : ((legacyHighlightStyle.normal || ({})).background || "")
    }
    readonly property string nonCursorHighlightBackgroundValue:
        typedVisualModel
        ? (selected ? visualModel.selectedBackground
                    : visualModel.normalBackground)
        : ((selected ? (legacyHighlightStyle.selected || ({}))
                     : (legacyHighlightStyle.normal || ({}))).background || "")
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
        : (effectiveModel && effectiveModel.isFolder
           ? (current && panelRoot.showCursor
              ? panelRoot.foregroundColor : panelRoot.folderIconColor)
           : (highlightForegroundValue !== ""
              ? highlightForeground : panelRoot.mutedColor))
    readonly property color highlightLabelBackground:
        highlightBackgroundValue || panelRoot.labelBackgroundColor
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

    // System file URLs are resolved by the host's native image provider.  A
    // precise shell icon can take noticeably longer than the first catalog
    // frame (especially on a cold folder), so keep a small immutable fallback
    // under it until that image is actually ready.  The host first supplies a
    // shared native generic file/folder URL; this QRC image covers only its
    // very first asynchronous load and is never used for custom icon URLs.
    function isSystemFileIconSource(source) {
        const value = source ? source.toString() : ""
        return value.indexOf("image://") === 0
                && value.indexOf("/file/") >= 0
    }

    function systemFileFallbackSource(logicalSize) {
        const folder = effectiveModel && effectiveModel.isFolder
        const iconName = folder
                ? (displayName === ".." ? "folder-up" : "folder")
                : "file"
        const group = Math.max(1, Number(logicalSize) || 1) >= 48
                ? "lucide-gallery" : "lucide"
        return "qrc:/F4QtHost/icons/" + group + "/" + iconName + ".svg"
    }

    // f4 supplies one model icon URL that can be reused by every Gallery
    // presentation. Native Windows icon resources contain separately drawn
    // small frames, while large Lucide routes are already rasterized by the
    // provider. Rendering either 128-DIP frame into a 16-DIP compact slot can
    // lose single-physical-pixel detail completely. Retarget supported image
    // provider routes to the live visual size; ordinary file/qrc URLs remain
    // untouched.
    function sourceColorIconAtSize(source, logicalSize, tint) {
        let value = source ? source.toString() : ""
        if (value.indexOf("image://") !== 0
                || (value.indexOf("/file/") < 0
                    && value.indexOf("/lucide/") < 0)
                || value.indexOf("size=") < 0)
            return value

        const size = Math.max(1, Math.round(Number(logicalSize) || 1))
        const dpr = Math.max(0.5, Number(renderDpr) || 1)
        value = value.replace(/([?&])size=[^&]*/, "$1size=" + size)
        value = value.replace(/([?&])dpr=[^&]*/, "$1dpr=" + dpr)
        // Only monochrome Lucide routes are masks. Native/system file routes
        // may be multicolour and must never receive a tint query.
        if (value.indexOf("/lucide/") >= 0 && tint !== undefined
                && tint !== null) {
            const encodedTint = encodeURIComponent(String(tint))
            if (/([?&])color=/.test(value)) {
                value = value.replace(/([?&])color=[^&]*/,
                                      "$1color=" + encodedTint)
            } else {
                value += (value.indexOf("?") >= 0 ? "&" : "?")
                        + "color=" + encodedTint
            }
        }
        return value
    }

    Rectangle {
        id: selectionSurface
        objectName: "gallerySelectionSurface-" + brick.viewIndex
        readonly property real visualBorderWidth: border.width
        readonly property color visualBorderColor: border.color
        readonly property bool visualBorderPixelAligned: border.pixelAligned
        readonly property real surfaceInset: brick.detailsMode ? 0
                                              : (brick.columnsMode ? 1 : 2)
        readonly property rect nominalSurfaceRect: Qt.rect(
            surfaceInset, surfaceInset,
            Math.max(0, parent.width - surfaceInset * 2),
            brick.iconsMode
                ? Math.max(0, Math.min(parent.height - surfaceInset * 2,
                    gridLabel.y + gridLabel.contentHeight + 3
                    - surfaceInset))
                : Math.max(0, parent.height - surfaceInset * 2))
        // Quantize only the settled cursor. Applying the correction while its
        // independent chrome, viewport, or owning brick is animating would
        // turn smooth fractional motion into physical-pixel stair steps.
        readonly property bool pixelAlignedCursorGeometry:
            brick.current && brick.panelRoot.showCursor
            && !brick.panelRoot.cursorPixelAlignmentSuspended
            && !brick.geometryAnimationRunning
        readonly property rect visualSurfaceRect:
            pixelAlignedCursorGeometry
            ? brick.panelRoot.alignViewportItemRectToDevicePixels(
                  brick, nominalSurfaceRect)
            : nominalSurfaceRect
        readonly property real nominalBorderWidth: {
            if (brick.cursorChromeExposesUnderlay)
                return 0
            // Compact presentations communicate persistent selection through
            // the marked foreground only. Keep the thin border exclusively
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
        x: visualSurfaceRect.x
        y: visualSurfaceRect.y
        width: visualSurfaceRect.width
        // Icons rows share the tallest brick's layout stride, but a short
        // filename must not inherit the empty label space required by a
        // neighbour with several wrapped lines. Keep hit testing/navigation
        // on the full row geometry and size only the painted selection chrome
        // to this brick's actual preview + rendered text content.
        height: visualSurfaceRect.height
        radius: brick.columnsMode || brick.detailsMode ? 4 : 6
        // Keep rounded corners smooth in every state. Settled straight edges
        // get their crispness from scene-space geometry snapping and Qt's
        // pixel-aligned border, not from disabling antialiasing for the whole
        // rounded rectangle.
        antialiasing: true
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
                return brick.panelRoot.itemHoverColor
            if (brick.masonryMode || brick.gridMode)
                return brick.effectiveModel && brick.effectiveModel.isFolder
                        ? brick.panelRoot.directoryBackgroundColor
                        : brick.panelRoot.itemBackgroundColor
            return "transparent"
        }
        border.width: nominalBorderWidth
        // Always let Qt round the rendered stroke after applying the actual
        // window DPR. Cursor geometry may remain fractional during animation;
        // this property quantizes only the physical border width.
        border.pixelAligned: true
        border.color: brick.detailsMode
            ? brick.panelRoot.cursorBorderColor
            : (brick.columnsMode
               ? brick.panelRoot.cardCursorBorderColor
               : (brick.selected
               ? brick.panelRoot.selectionColor
               : (brick.current && brick.panelRoot.showCursor
                  ? brick.panelRoot.cardCursorBorderColor
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

            IconImage {
                id: detailsFallbackIcon
                objectName: brick.detailsMode ? "galleryFallbackIcon-"
                                                + brick.viewIndex : ""
                readonly property color effectiveIconColor:
                    brick.fallbackIconColor
                readonly property real nominalIconSize:
                    brick.panelRoot.detailsIconSize
                width: brick.snapIconExtent(nominalIconSize)
                height: width
                sourceSize: Qt.size(width, height)
                color: effectiveIconColor
                fillMode: Image.PreserveAspectFit
                smooth: false
                mipmap: false
                anchors.centerIn: parent
                transform: Translate {
                    x: brick.iconPixelOffsetX(detailsFallbackIcon)
                    y: brick.iconPixelOffsetY(detailsFallbackIcon)
                }
                readonly property url modelIconSource:
                    brick.effectiveModel
                    ? brick.effectiveModel.iconPath : ""
                readonly property bool lucideSource:
                    brick.detailsMode && brick.isLucideIconSource(
                        modelIconSource)
                readonly property bool systemFileSource:
                    brick.detailsMode && brick.isSystemFileIconSource(
                        modelIconSource)
                visible: Boolean(brick.detailsMode
                    && (lucideSource || (systemFileSource
                                         && detailsSourceColorIcon.status
                                            !== Image.Ready))
                    && brick.effectiveModel
                    && thumbnail.source.toString() === ""
                    && modelIconSource.toString() !== ""
                    // A marker suppresses only a native/system icon (kept
                    // redundant next to a colored marker glyph); a Lucide
                    // icon is a thin monochrome glyph that combines fine
                    // with a marker and must still show.
                    && (!brick.highlightMarker || lucideSource)
                    && !(brick.panelRoot.viewerTransitionActive
                          && brick.panelRoot.viewerTransitionEntryId
                             === brick.entryId))
                source: brick.detailsMode && lucideSource
                             ? brick.sourceColorIconAtSize(
                                   modelIconSource,
                                   brick.panelRoot.detailsIconSize,
                                   effectiveIconColor)
                             : (brick.detailsMode && systemFileSource
                                ? brick.systemFileFallbackSource(
                                      brick.panelRoot.detailsIconSize) : "")
            }

            Image {
                id: detailsSourceColorIcon
                objectName: brick.detailsMode
                    ? "gallerySourceColorIcon-" + brick.viewIndex : ""
                anchors.centerIn: parent
                width: detailsFallbackIcon.width
                height: detailsFallbackIcon.height
                source: brick.detailsMode && !detailsFallbackIcon.lucideSource
                        ? brick.sourceColorIconAtSize(
                              detailsFallbackIcon.modelIconSource,
                              brick.panelRoot.detailsIconSize)
                        : ""
                fillMode: Image.PreserveAspectFit
                smooth: false
                transform: Translate {
                    x: brick.iconPixelOffsetX(detailsSourceColorIcon)
                    y: brick.iconPixelOffsetY(detailsSourceColorIcon)
                }
                // Native Windows shell icon extraction can take tens or
                // hundreds of milliseconds for a metadata batch. Never run
                // an image-provider request on the GUI/input thread.
                asynchronous: true
                // The base catalog uses two shared native generic URLs.  Keep
                // them cached and retain that resolved image while metadata
                // replaces the source with the precise shell icon.
                cache: true
                retainWhileLoading: true
                opacity: 1
                visible: Boolean(
                    brick.effectiveModel
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
                         && (!brick.effectiveModel
                             || brick.effectiveModel.iconPath.toString() === "")
                text: brick.highlightMarker || (brick.effectiveModel
                      && brick.effectiveModel.isFolder
                      ? (brick.displayName === ".." ? "↰" : "▸") : " ")
                color: brick.fallbackIconColor
                font.pixelSize: brick.panelRoot.detailsNameFontPixelSize
            }
        }

        Text {
            id: detailsBaseName
            objectName: brick.detailsMode ? "galleryBaseName-"
                                            + brick.viewIndex : ""
            text: brick.panelRoot.quickSearchStyledText(
                brick.panelRoot.separateFileExtensions
                    ? brick.displayBaseName
                    : brick.displayBaseName
                      + (brick.displayExtension !== ""
                         ? "." + brick.displayExtension : ""),
                brick.entryId, 0)
            textFormat: brick.panelRoot.quickSearchMatch(brick.entryId)
                        ? Text.StyledText : Text.AutoText
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
            text: brick.panelRoot.quickSearchStyledText(
                brick.displayExtension, brick.entryId,
                brick.panelRoot.codePointLength(brick.displayBaseName) + 1)
            textFormat: brick.panelRoot.quickSearchMatch(brick.entryId)
                        ? Text.StyledText : Text.AutoText
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

        Rectangle {
            id: previewBackdrop
            objectName: "galleryThumbnailBackdrop-" + brick.viewIndex
            readonly property bool enabledForPresentation:
                brick.masonryMode || brick.gridMode
            anchors.fill: parent
            radius: 4
            color: brick.panelRoot.previewBackdropColor
            // The card is presentation chrome, not part of the thumbnail.
            // Keep it below both fallback icon renderers so an empty image
            // URL can never dim an opaque native/system icon again.
            visible: enabledForPresentation
                     && thumbnailSource.status !== Image.Ready
                     && !(brick.panelRoot.viewerTransitionActive
                          && brick.panelRoot.viewerTransitionEntryId
                             === brick.entryId)
        }

        IconImage {
            id: fallbackIcon
            objectName: brick.detailsMode ? ""
                                         : "galleryFallbackIcon-"
                                           + brick.viewIndex
            readonly property color effectiveIconColor:
                brick.fallbackIconColor
            property url modelIconSource: {
                if (brick.detailsMode)
                    return ""
                const value = brick.effectiveModel
                        ? brick.effectiveModel.iconPath.toString() : ""
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
            readonly property bool lucideSource:
                brick.isLucideIconSource(modelIconSource)
            readonly property bool systemFileSource:
                brick.isSystemFileIconSource(modelIconSource)
            source: lucideSource
                    ? brick.sourceColorIconAtSize(
                          modelIconSource, width, effectiveIconColor)
                    : (systemFileSource
                       ? brick.systemFileFallbackSource(width) : "")
            anchors.centerIn: parent
            readonly property real nominalIconSize:
                brick.detailsMode || brick.columnsMode
                ? Math.min(parent.width, brick.panelRoot.detailsIconSize)
                : brick.iconsMode
                ? Math.max(0, Math.min(parent.width, parent.height))
                : Math.max(0, Math.min(parent.width, parent.height) * 0.55)
            width: brick.snapIconExtent(nominalIconSize)
            height: width
            sourceSize: Qt.size(width, height)
            color: effectiveIconColor
            fillMode: Image.PreserveAspectFit
            smooth: false
            mipmap: false
            transform: Translate {
                x: brick.iconPixelOffsetX(fallbackIcon)
                y: brick.iconPixelOffsetY(fallbackIcon)
            }
            opacity: brick.detailsMode || brick.columnsMode ? 1 : 0.78
            visible: !brick.detailsMode
                      && (lucideSource || (systemFileSource
                                           && sourceColorFallbackIcon.status
                                              !== Image.Ready))
                      && thumbnail.source.toString() === ""
                      && source.toString() !== ""
                      // A marker suppresses only a native/system icon (kept
                      // redundant next to a colored marker glyph); a Lucide
                      // icon is a thin monochrome glyph that combines fine
                      // with a marker and must still show.
                      && (!brick.highlightMarker || lucideSource)
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
            source: !brick.detailsMode && !fallbackIcon.lucideSource
                    ? brick.sourceColorIconAtSize(
                          fallbackIcon.modelIconSource, width)
                    : ""
            fillMode: Image.PreserveAspectFit
            smooth: false
            transform: Translate {
                x: brick.iconPixelOffsetX(sourceColorFallbackIcon)
                y: brick.iconPixelOffsetY(sourceColorFallbackIcon)
            }
            // Keep system/native icon lookup out of the GUI thread. The
            // fallback remains visible while the asynchronous result loads.
            asynchronous: true
            cache: true
            retainWhileLoading: true
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
            // Details owns its fallback marker in detailsIconSlot above.
            // Keeping this second copy visible would overlay that dedicated
            // icon after inactive generic icon sources are gated to empty.
            visible: false
            text: brick.highlightMarker || (brick.effectiveModel
                  && brick.effectiveModel.isFolder
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
            // Static row semantics remain snapshot-owned, but thumbnail URLs
            // are inherently dynamic. Once this exact row's QObject facade
            // is generation-checked and installed, follow only its notifying
            // image URL instead of rebuilding the whole visualRow map for
            // every cache publish/eviction.
            property url source:
                brick.visualFacadeReady && brick.model
                ? brick.model.imageIdUrl
                : (brick.effectiveModel
                   ? brick.effectiveModel.imageIdUrl : "")
            // `source` is a url value. Strictly comparing that object with a
            // string makes even an empty URL look non-empty, leaving the dark
            // loading backdrop above ordinary file/folder icons.
            visible: source.toString() !== ""
                     && !(brick.panelRoot.viewerTransitionActive
                          && brick.panelRoot.viewerTransitionEntryId
                             === brick.entryId)

            // Keep one source item for every renderer so presentation changes
            // never request or retain another decoded frame. As in the
            // standalone ImageView, the source stays hidden and feeds the
            // unsharp-mask, checkerboard and rounded-corner shader. Each mode
            // retains its established Crop/Fit policy below.
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
                property real sharpenAmount:
                    brick.masonryMode || brick.gridMode ? 1.5 : 0
                property bool showCheckerboard:
                    brick.masonryMode || brick.gridMode
                property int checkerboardSize:
                    4 * brick.renderDpr
                property real borderRadius:
                    (brick.masonryMode || brick.gridMode)
                    ? 4.1 * brick.renderDpr : 0

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
        text: brick.panelRoot.quickSearchStyledText(
            brick.effectiveModel ? brick.displayName : "",
            brick.entryId, 0)
        textFormat: brick.panelRoot.quickSearchMatch(brick.entryId)
                    ? Text.StyledText : Text.AutoText
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
        text: brick.iconsMode
              ? brick.panelRoot.quickSearchStyledElidedText(
                    brick.iconLabelText, brick.displayName, brick.entryId)
              : brick.panelRoot.quickSearchStyledText(
                    brick.displayName, brick.entryId, 0)
        textFormat: brick.panelRoot.quickSearchMatch(brick.entryId)
                    ? Text.StyledText : Text.AutoText
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
            textFormat: extensionLabel.textFormat
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
            text: brick.panelRoot.quickSearchStyledText(
                brick.panelRoot.separateFileExtensions
                    ? brick.displayBaseName
                    : brick.displayBaseName
                      + (brick.displayExtension !== ""
                         ? "." + brick.displayExtension : ""),
                brick.entryId, 0)
            textFormat: brick.panelRoot.quickSearchMatch(brick.entryId)
                        ? Text.StyledText : Text.AutoText
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
            text: brick.panelRoot.quickSearchStyledText(
                brick.displayExtension !== ""
                    ? (brick.detailsMode ? brick.displayExtension
                       : "." + brick.displayExtension) : "",
                brick.entryId,
                brick.panelRoot.codePointLength(brick.displayBaseName)
                    + (brick.detailsMode ? 1 : 0))
            textFormat: brick.panelRoot.quickSearchMatch(brick.entryId)
                        ? Text.StyledText : Text.AutoText
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
        // Middle-button input belongs to GalleryPanel's shared wheel
        // contract. Let it bubble to the panel-level GUI auto-scroll or
        // console-forwarding surface instead of turning it into a tile click.
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        preventStealing: true
        onPressed: mouse => {
            brick.panelRoot.handlePointerPress(brick.viewIndex,
                                               mouse.button,
                                               mouse.modifiers)
            mouse.accepted = true
        }
        onPositionChanged: mouse => {
            if (!(mouse.buttons & (Qt.LeftButton | Qt.RightButton)))
                return
            const point = mapToItem(brick.panelRoot, mouse.x, mouse.y)
            brick.panelRoot.handlePointerDrag(point.x, point.y)
        }
        onReleased: {
            brick.panelRoot.endPointerDrag()
        }
        onCanceled: {
            brick.panelRoot.endPointerDrag()
        }
        onDoubleClicked: mouse => {
            if ((mouse.button & Qt.LeftButton) !== 0)
                brick.panelRoot.selectIndex(brick.viewIndex, true)
            else if ((mouse.button & Qt.RightButton) !== 0)
                brick.panelRoot.invertPanelSelection()
            mouse.accepted = true
        }
    }
}
