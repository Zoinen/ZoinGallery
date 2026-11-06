#ifndef ZOINGALLERY_DECODESIZEPOLICY_H
#define ZOINGALLERY_DECODESIZEPOLICY_H

#include <QSize>

namespace ZoinGallery {

inline constexpr int MaximumPanelThumbnailEdge = 1024;

// Bound actual frame pixels while preserving aspect and a nonzero short edge.
QSize boundedPanelThumbnailSize(const QSize &size);
bool panelThumbnailCovers(const QSize &available, const QSize &bounds,
                          bool preserveAspect);

enum class DecodeSizeFamily {
    Thumbnail,
    ViewerFit,
};

// Converts an exact presentation target into a stable source-aspect decode
// tier for remote/materializing sources. Direct-local callers pass false and
// retain the historical exact-size behavior.
QSize stableDecodeTarget(const QSize &requestedTarget,
                         const QSize &nativeSourceSize,
                         const QSize &previousPreparedTarget,
                         DecodeSizeFamily family,
                         bool expensiveSource);

} // namespace ZoinGallery

#endif // ZOINGALLERY_DECODESIZEPOLICY_H
