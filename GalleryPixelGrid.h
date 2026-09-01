#ifndef GALLERYPIXELGRID_H
#define GALLERYPIXELGRID_H

#include <QPointF>
#include <QRectF>

namespace ZoinGallery::PixelGrid {

// Geometry owned by the gallery is expressed in logical pixels. Keep every
// delegate on one deterministic logical-pixel lattice; the QML-facing icon
// helpers apply the final device-pixel offset at render time.
inline QRectF snapLogicalRect(const QRectF &rect) {
    return rect.toRect();
}

inline QPointF devicePixelOffset(const QPointF &sceneOrigin,
                                 qreal devicePixelRatio) {
    const qreal dpr = qMax<qreal>(1.0, devicePixelRatio);
    const qreal snappedX = qRound(sceneOrigin.x() * dpr) / dpr;
    const qreal snappedY = qRound(sceneOrigin.y() * dpr) / dpr;
    return QPointF(snappedX - sceneOrigin.x(),
                   snappedY - sceneOrigin.y());
}

} // namespace ZoinGallery::PixelGrid

#endif
