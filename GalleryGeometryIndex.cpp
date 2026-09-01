#include "GalleryGeometryIndex.h"

#include <QMap>

#include <algorithm>

namespace ZoinGallery {

void GalleryGeometryIndex::clear() {
    _bands.clear();
}

void GalleryGeometryIndex::rebuild(
    const QVector<GalleryGeometryRecord> &records) {
    QMap<int, GalleryLayoutBand> rows;
    for (const GalleryGeometryRecord &record : records) {
        if (record.index < 0 || !record.geometry.isValid()
            || record.geometry.isEmpty()) {
            continue;
        }
        GalleryLayoutBand &band = rows[record.row];
        if (band.indexes.isEmpty()) {
            band.row = record.row;
            band.top = record.geometry.top();
            band.bottom = record.geometry.bottom();
        } else {
            band.top = qMin(band.top, record.geometry.top());
            band.bottom = qMax(band.bottom, record.geometry.bottom());
        }
        band.indexes.append(record.index);
    }
    _bands = rows.values();
    std::sort(_bands.begin(), _bands.end(),
              [](const GalleryLayoutBand &left,
                 const GalleryLayoutBand &right) {
                  if (!qFuzzyCompare(left.top, right.top)) {
                      return left.top < right.top;
                  }
                  return left.bottom < right.bottom;
              });
}

bool GalleryGeometryIndex::empty() const {
    return _bands.isEmpty();
}

qsizetype GalleryGeometryIndex::size() const {
    return _bands.size();
}

const QVector<GalleryLayoutBand> &GalleryGeometryIndex::bands() const {
    return _bands;
}

int GalleryGeometryIndex::firstBandIntersecting(qreal coordinate) const {
    int low = 0;
    int high = _bands.size();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (_bands.at(middle).bottom < coordinate) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < _bands.size() ? low : -1;
}

QVector<int> GalleryGeometryIndex::indexesIntersecting(
    qreal start, qreal end) const {
    QVector<int> indexes;
    if (end < start) {
        std::swap(start, end);
    }
    const int first = firstBandIntersecting(start);
    if (first < 0) {
        return indexes;
    }
    for (int bandIndex = first; bandIndex < _bands.size(); ++bandIndex) {
        const GalleryLayoutBand &band = _bands.at(bandIndex);
        if (band.top > end) {
            break;
        }
        if (band.bottom >= start) {
            indexes.append(band.indexes);
        }
    }
    return indexes;
}

} // namespace ZoinGallery
