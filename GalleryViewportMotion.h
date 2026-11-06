#pragma once

#include <QRectF>
#include <atomic>

// A contiguous camera-offset interval whose expected cells have scene nodes.
// Construct during synchronization from a bounded layout query, then consume
// without touching GUI objects on render frames. Empty layout space is safe;
// a bounding union of prepared cells cannot establish this interval.
struct GalleryViewportMotionRange {
    qreal anchor = 0;
    qreal minimum = 0;
    qreal maximum = 0;
    bool coversAnchor = true;

    void excludeUnprepared(const QRectF &cell, const QRectF &viewportAtAnchor) {
        if (cell.isEmpty() || cell.right() <= viewportAtAnchor.left()
            || cell.left() >= viewportAtAnchor.right()) return;
        if (cell.bottom() <= viewportAtAnchor.top()) {
            minimum = qMax(minimum,
                anchor + cell.bottom() - viewportAtAnchor.top());
        } else if (cell.top() >= viewportAtAnchor.bottom()) {
            maximum = qMin(maximum,
                anchor + cell.top() - viewportAtAnchor.bottom());
        } else {
            coversAnchor = false;
        }
    }

    qreal constrained(qreal requested) const {
        return coversAnchor ? qBound(minimum, requested, maximum) : anchor;
    }
};

struct GalleryViewportMotionState {
    std::atomic_bool active{false};
    bool edgeJump = false; // immutable while published to the renderer
    bool page = false; // immutable while published to the renderer
    bool prepareAlongPath = false; // immutable while published to the renderer
    std::atomic<qreal> contentY{0};
    // GUI-owned input target, published before activating/retargeting motion.
    std::atomic<qreal> targetY{0};
    // Accessed only by the render thread. May include a bounded prepared margin;
    // the owning viewport's clip remains responsible for the visible boundary.
    QRectF renderViewport;
    // The actual visible region, without a prepared margin. SVG presentation
    // and upload priority use this rather than the conservative culling region.
    QRectF renderVisibleViewport;
};
