#ifndef GALLERYPIXELGRID_H
#define GALLERYPIXELGRID_H

#include <QPointF>
#include <QRectF>

namespace ZoinGallery::PixelGrid {

// Legacy callers without a window/DPR still use logical-pixel geometry.
inline QRectF snapLogicalRect(const QRectF &rect) {
    return rect.toRect();
}

inline QPointF snapDevicePoint(const QPointF &point, qreal devicePixelRatio) {
    const qreal dpr = qMax<qreal>(0.01, devicePixelRatio);
    return QPointF(qRound64(point.x() * dpr) / dpr,
                   qRound64(point.y() * dpr) / dpr);
}

// Round shared edges, not each width independently: adjacent bricks must
// neither overlap nor leave a gap, including fractional row densities.
inline QRectF snapDeviceRect(const QRectF &rect, qreal devicePixelRatio) {
    return QRectF(snapDevicePoint(rect.topLeft(), devicePixelRatio),
                  snapDevicePoint(rect.bottomRight(), devicePixelRatio));
}

inline QPointF devicePixelOffset(const QPointF &sceneOrigin,
                                 qreal devicePixelRatio) {
    return snapDevicePoint(sceneOrigin, devicePixelRatio) - sceneOrigin;
}

} // namespace ZoinGallery::PixelGrid

#endif
