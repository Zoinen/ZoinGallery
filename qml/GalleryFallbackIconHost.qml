pragma ComponentBehavior: Bound

import QtQuick

Loader {
    id: fallbackIcon
    required property GalleryEntryPreview preview
    // Keep the primitive through identity resets, but clear its source.
    property bool frameCreated: false
    active: (preview.activePresentation && !preview.thumbnailExpected) || frameCreated
    onLoaded: frameCreated = true
    readonly property Component requestedComponent:
        useAspectRaster ? preview.visualFactories.fallbackAspect : preview.visualFactories.fallbackTinted
    property Component retainedComponent: preview.visualFactories.fallbackTinted
    function updateComponent() {
        Qt.callLater(commitComponent)
    }
    function commitComponent() {
        if (preview.activePresentation && !preview.thumbnailExpected)
            retainedComponent = requestedComponent
    }
    onRequestedComponentChanged: updateComponent()
    Component.onCompleted: updateComponent()
    asynchronous: false
    readonly property color effectiveIconColor:
        preview.entry.fallbackIconColor
    property url modelIconSource:
        !preview.activePresentation ? "" : preview.entry.panelRoot.iconResolver.resolve(
                    preview.entry.iconKey, preview.entry.iconPath,
                    preview.entry.largePreviewMode,
                    preview.entry.isFolder,
                    preview.entry.isImage,
                    preview.entry.effectiveDisplayName === "..")
    readonly property string modelIconSourceText: modelIconSource.toString()
    readonly property bool hasModelIconSource: modelIconSourceText !== ""
    readonly property bool lucideSource:
        preview.entry.isLucideIconSource(modelIconSourceText)
    readonly property bool systemFileSource:
        preview.entry.isSystemFileIconSource(modelIconSourceText)
    readonly property bool tintedSource:
        !systemFileSource && (preview.entry.visualModel.tintIcon === true
            || (preview.entry.visualModel.tintIcon === undefined && lucideSource))
    anchors.centerIn: parent
    readonly property real nominalIconSize:
        preview.entry.detailsMode || preview.entry.columnsMode
        ? Math.max(0, Math.min(parent.width,
                   lucideSource && preview.entry.panelRoot.density <= 30
                       ? preview.entry.panelRoot.detailsIconSize : Infinity,
                   preview.entry.height
                   - 2 * preview.entry.panelRoot.metrics.detailsIconVerticalPadding))
        : preview.entry.iconsMode || preview.entry.gridMode
        ? Math.max(0, Math.min(parent.width, parent.height))
        : Math.max(0, Math.min(parent.width, parent.height) * 0.55)
    readonly property bool fullAreaIcon: preview.entry.iconsMode || preview.entry.gridMode
    width: fullAreaIcon ? parent.width
                                  : preview.entry.snapIconExtent(nominalIconSize)
    height: fullAreaIcon ? parent.height : width
    readonly property var pixelGridOffset:
        preview.entry.iconPixelAlignment(fallbackIcon)
    transform: Translate {
        x: (fallbackIcon.pixelGridOffset ? fallbackIcon.pixelGridOffset.offsetX : 0)
        y: (fallbackIcon.pixelGridOffset ? fallbackIcon.pixelGridOffset.offsetY : 0)
    }
    readonly property bool useAspectRaster: fullAreaIcon && lucideSource
                                           && rasterSource.toString().startsWith("image://")
    readonly property real rasterExtent: fullAreaIcon
                                            ? Math.max(width, height)
                                            : Math.min(width, height)
    readonly property url rasterSource: !Window.window || !preview.activePresentation
        || preview.thumbnailExpected ? "" : tintedSource
        ? preview.entry.sourceColorIconAtSize(modelIconSourceText, rasterExtent, effectiveIconColor)
        : (systemFileSource ? preview.entry.systemFileFallbackSource(rasterExtent) : "")
    sourceComponent: retainedComponent
}
