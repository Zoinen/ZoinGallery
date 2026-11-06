#include "GalleryViewportMaterializer.h"

#include <QtGlobal>

namespace ZoinGallery {

GalleryViewportWindow GalleryViewportMaterializer::withMotionTarget(
    GalleryViewportWindow window, qreal targetOffset) {
    const qreal delta = targetOffset - window.visibleStart;
    // Spend the existing metadata budget where the camera is heading. Fast
    // motion can consume the symmetric leading margin before the next GUI
    // synchronization. Keep the ordinary delegate margin behind the camera
    // for reversals; do not widen the total materialization window.
    if (delta > 0) {
        const qreal extra = qBound<qreal>(0,
            delta - (window.metadataEnd - window.visibleEnd),
            qMax<qreal>(0, window.delegateStart - window.metadataStart));
        window.metadataStart += extra;
        window.metadataEnd += extra;
    } else if (delta < 0) {
        const qreal extra = qBound<qreal>(0,
            -delta - (window.visibleStart - window.metadataStart),
            qMax<qreal>(0, window.metadataEnd - window.delegateEnd));
        window.metadataStart -= extra;
        window.metadataEnd -= extra;
    }
    if (delta > 0)
        window.delegateEnd = qMax(window.delegateEnd,
            qMin(window.metadataEnd, window.visibleEnd + delta));
    else if (delta < 0)
        window.delegateStart = qMin(window.delegateStart,
            qMax(window.metadataStart, targetOffset));
    return window;
}

GalleryViewportWindow GalleryViewportMaterializer::plan(
    GalleryPresentationMode mode,
    qreal contentOffset,
    qreal viewportExtent,
    qreal primaryItemExtent) {
    const qreal extent = qMax<qreal>(1, viewportExtent);
    const qreal itemExtent = qMax<qreal>(1, primaryItemExtent);
    // Fixed layouts are analytical and commit synchronously with content
    // movement. Spatial overscan would materialize a complete extra row (or
    // a complete 25-item column) for a one-pixel range, making an atomic mode
    // switch pay for invisible delegates. Masonry retains a small margin
    // because its variable bands benefit from crossing a frame boundary.
    // Metadata/decode look-ahead remains wider and independent below.
    const qreal delegateOverscan =
        mode == GalleryPresentationMode::Masonry
        ? qMin<qreal>(48.0, itemExtent * 2) : 0.0;
    // Metadata/decode planning may lead visuals by a few rows. Columns use a
    // whole column because their primary axis advances in column-width steps;
    // every vertical presentation remains capped at four nominal rows.
    // Masonry needs at least one row to prepare the next band in a short panel.
    const qreal metadataOverscan = mode == GalleryPresentationMode::Columns
        ? itemExtent : qMin<qreal>(mode == GalleryPresentationMode::Masonry
            ? qMax(itemExtent, extent * 0.25) : extent * 0.25, itemExtent * 4);

    return {
        .visibleStart = contentOffset,
        .visibleEnd = contentOffset + extent,
        .delegateStart = contentOffset - delegateOverscan,
        .delegateEnd = contentOffset + extent + delegateOverscan,
        .metadataStart = contentOffset - metadataOverscan,
        .metadataEnd = contentOffset + extent + metadataOverscan,
    };
}

} // namespace ZoinGallery
