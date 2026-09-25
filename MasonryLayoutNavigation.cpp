#include "MasonryLayout.h"
#include "FileListModel.h"
#include "GalleryPixelGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {

ImageFile *imageFileFromNavigationIndex(const QModelIndex &index) {
    return index.data(FileListModel::ImageFileRole).value<ImageFile *>();
}

} // namespace

QQuickItem *MasonryLayout::itemAt(qreal x, qreal y) const {
    const int index = indexAt(x, y);
    const MasonryBrick *brick = brickAt(index);
    return brick ? brick->item : nullptr;
}

int MasonryLayout::indexAt(qreal x, qreal y) const {
    const qreal margin = 1 / devicePixelRatio();
    if (_presentationMode != Masonry || sparseVirtualLayout()) {
        const ZoinGallery::GalleryFixedLayoutPlan plan = fixedLayoutPlan();
        const QVector<int> candidates = _presentationMode == Columns
            ? plan.indexesIntersecting(x - margin, x + margin)
            : plan.indexesIntersecting(y - margin, y + margin);
        for (const int index : candidates) {
            if (ZoinGallery::PixelGrid::snapDeviceRect(
                    analyticFixedGeometry(index), devicePixelRatio()).contains(x, y)) {
                return index;
            }
        }
        return -1;
    }
    const int firstBand = bandIndexAt(y - margin);
    if (firstBand < 0) {
        return -1;
    }
    // Bands normally do not overlap, but animated/reused masonry geometry can
    // share an edge. Check every band intersecting the queried scan line.
    for (int bandIndex = firstBand; bandIndex < _layoutBands.size(); ++bandIndex) {
        const LayoutBand &band = _layoutBands.at(bandIndex);
        if (band.top > y + margin) {
            break;
        }
        if (band.bottom < y - margin) {
            continue;
        }
        for (const int index : band.indexes) {
            if (index >= 0 && index < _bricks.size() &&
                ZoinGallery::PixelGrid::snapDeviceRect(
                    _bricks[index].geometry(), devicePixelRatio()).contains(x, y)) {
                return index;
            }
        }
    }
    return -1;
}
