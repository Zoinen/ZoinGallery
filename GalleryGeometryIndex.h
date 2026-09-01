#ifndef GALLERYGEOMETRYINDEX_H
#define GALLERYGEOMETRYINDEX_H

#include "GalleryLayoutEngine.h"

namespace ZoinGallery {

struct GalleryGeometryRecord {
    int index = -1;
    int row = -1;
    QRectF geometry;
};

// Compact index for the one active variable-geometry presentation. Fixed
// presentations stay analytical in GalleryFixedLayoutPlan and never populate
// this structure for every catalog row.
class GalleryGeometryIndex final {
public:
    void clear();
    void rebuild(const QVector<GalleryGeometryRecord> &records);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] qsizetype size() const;
    [[nodiscard]] const QVector<GalleryLayoutBand> &bands() const;
    [[nodiscard]] int firstBandIntersecting(qreal coordinate) const;
    [[nodiscard]] QVector<int> indexesIntersecting(qreal start,
                                                   qreal end) const;

private:
    QVector<GalleryLayoutBand> _bands;
};

} // namespace ZoinGallery

#endif // GALLERYGEOMETRYINDEX_H
