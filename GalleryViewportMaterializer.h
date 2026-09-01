#ifndef GALLERYVIEWPORTMATERIALIZER_H
#define GALLERYVIEWPORTMATERIALIZER_H

#include "GalleryLayoutEngine.h"

namespace ZoinGallery {

struct GalleryViewportWindow {
    qreal visibleStart = 0;
    qreal visibleEnd = 0;
    qreal delegateStart = 0;
    qreal delegateEnd = 0;
    qreal metadataStart = 0;
    qreal metadataEnd = 0;
};

class GalleryViewportMaterializer final {
public:
    [[nodiscard]] static GalleryViewportWindow plan(
        GalleryPresentationMode mode,
        qreal contentOffset,
        qreal viewportExtent,
        qreal primaryItemExtent);
};

} // namespace ZoinGallery

#endif // GALLERYVIEWPORTMATERIALIZER_H
