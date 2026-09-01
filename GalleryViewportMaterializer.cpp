#include "GalleryViewportMaterializer.h"

#include <QtGlobal>

namespace ZoinGallery {

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
    const qreal metadataOverscan = mode == GalleryPresentationMode::Columns
        ? itemExtent : qMin<qreal>(extent * 0.25, itemExtent * 4);

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
