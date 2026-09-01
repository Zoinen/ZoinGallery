#include "MasonryLayout.h"
#include "FileListModel.h"

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
    if (_presentationMode != Masonry || sparseVirtualLayout()) {
        const int count = logicalBrickCount();
        if (count <= 0) {
            return -1;
        }
        const qreal extent = qMax<qreal>(1.0, effectiveTargetExtent());
        const bool virtualLayout = _presentationMode == Icons
            || sparseVirtualLayout();
        const qreal rowExtent = virtualLayout
            ? virtualGridRowHeight() : extent;
        int index = -1;
        if (_presentationMode == Columns) {
            const qreal stride = columnStride();
            if (stride <= 0 || y < _paddingTop) {
                return -1;
            }
            const int column = qMax(0, int(std::floor(x / stride)));
            const int row = int(std::floor((y - _paddingTop) / extent));
            if (row < 0 || row >= rowsPerColumn()) {
                return -1;
            }
            index = column * rowsPerColumn() + row;
        }
        else {
            if (y < _paddingTop) {
                return -1;
            }
            const int row = int(std::floor((y - _paddingTop) / rowExtent));
            if (_presentationMode == Grid || _presentationMode == Icons ||
                sparseVirtualLayout()) {
                const int columns = virtualLayout
                    ? virtualGridColumnCount() : effectiveColumnCount();
                const qreal canvasWidth = qMax<qreal>(
                    0, width() - _paddingLeft - _paddingRight);
                const qreal cellWidth = columns > 0
                    ? canvasWidth / columns : canvasWidth;
                if (cellWidth <= 0 || x < 0 || x >= canvasWidth) {
                    return -1;
                }
                const int column = qBound(
                    0, int(std::floor(x / cellWidth)), columns - 1);
                index = row * columns + column;
            }
            else {
                index = row;
            }
        }
        if (index < 0 || index >= count ||
            !analyticFixedGeometry(index).contains(x, y)) {
            return -1;
        }
        return index;
    }
    const int firstBand = bandIndexAt(y);
    if (firstBand < 0) {
        return -1;
    }
    // Bands normally do not overlap, but animated/reused masonry geometry can
    // share an edge. Check every band intersecting the queried scan line.
    for (int bandIndex = firstBand; bandIndex < _layoutBands.size(); ++bandIndex) {
        const LayoutBand &band = _layoutBands.at(bandIndex);
        if (band.top > y) {
            break;
        }
        if (band.bottom < y) {
            continue;
        }
        for (const int index : band.indexes) {
            if (index >= 0 && index < _bricks.size() &&
                _bricks[index].geometry().contains(x, y)) {
                return index;
            }
        }
    }
    return -1;
}
