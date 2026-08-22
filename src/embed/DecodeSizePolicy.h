#ifndef ZOINGALLERY_DECODESIZEPOLICY_H
#define ZOINGALLERY_DECODESIZEPOLICY_H

#include <QSize>

namespace ZoinGallery {

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
