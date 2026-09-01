#include "MasonryLayout.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QElapsedTimer>

#include <utility>

struct MasonryLayout::RewrapTrace
{
    bool enabled = false;
    QElapsedTimer timer;
    qint64 extentCompletedNs = 0;
    qint64 layoutCompletedNs = 0;
    qint64 bandsCompletedNs = 0;
    qint64 viewportCompletedNs = 0;
    qint64 propertiesCompletedNs = 0;
};

struct MasonryLayout::ViewportAnchor
{
    int index = -1;
    qreal offset = 0;
};

void MasonryLayout::rewrap(bool animate)
{
    RewrapTrace trace;
    trace.enabled = qEnvironmentVariableIsSet("F4_NAV_BENCHMARK_TRACE");
    if (trace.enabled) {
        trace.timer.start();
    }
    qreal currentIndexOffset = _currentIndexOffsetOverride;
    _currentIndexOffsetOverride = -1;
    if (currentIndexOffset == -1 && _currentIndex != -1
        && _currentIndex >= _visibleStart && _currentIndex <= _visibleEnd) {
        currentIndexOffset = _contentY - indexGeometry(_currentIndex).y();
    }

    if (_presentationMode != Masonry) {
        rewrapFixed(animate, &trace);
    } else if (sparseVirtualLayout()) {
        rewrapSparseMasonry(animate, currentIndexOffset);
    } else {
        rewrapMasonry(animate, currentIndexOffset);
    }
}

MasonryLayout::ViewportAnchor MasonryLayout::fixedViewportAnchor(
    bool preserve) const
{
    ViewportAnchor anchor;
    if (!preserve) {
        return anchor;
    }
    anchor.index = _topItem;
    anchor.offset = _topItemOffset;
    // setContentY() records the intersecting (possibly clipped) leading row
    // and its phase while the old density is still active. Do not derive the
    // anchor again here: setDensity() has already installed the new density,
    // so doing so would reinterpret the old pixel offset in the new lattice.
    return anchor;
}

int MasonryLayout::updateFixedContentExtent(
    const ZoinGallery::GalleryFixedLayoutPlan &plan)
{
    const int columns = effectiveColumnCount();
    int virtualRows = 0;
    if (_presentationMode == Details) {
        virtualRows = logicalBrickCount();
        setContentHeight(plan.contentExtent);
    } else if (_presentationMode == Columns) {
        setContentHeight(plan.contentExtent);
    } else {
        virtualRows = columns > 0
            ? (logicalBrickCount() + columns - 1) / columns : 0;
        if (_presentationMode == Icons) {
            setContentHeight(_paddingTop
                             + virtualRows * virtualGridRowHeight()
                             + _paddingBottom);
        } else if (_presentationMode != Icons) {
            setContentHeight(plan.contentExtent);
        }
    }
    if (_presentationMode == Columns) {
        _contentY = qBound<qreal>(0, _contentY, maximumContentOffset());
    } else if (_presentationMode != Icons || sparseVirtualLayout()) {
        _contentY = qBound<qreal>(
            0, _contentY, qMax<qreal>(0, _contentHeight - height()));
    }
    return virtualRows;
}

void MasonryLayout::restoreFixedViewportAnchor(const ViewportAnchor &anchor)
{
    if (anchor.index < 0 || anchor.index >= logicalBrickCount()) {
        return;
    }
    const QRectF geometry = indexGeometry(anchor.index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return;
    }
    const qreal position = _presentationMode == Columns
        ? geometry.left() : geometry.top();
    _contentY = qBound<qreal>(0, position - anchor.offset,
                              maximumContentOffset());
    _topItem = anchor.index;
    _topItemOffset = position - _contentY;
}

void MasonryLayout::commitFixedViewport(
    qreal oldContentY, bool animate, RewrapTrace *trace)
{
    applyPreparedResetViewport();
    if (_presentationMode == Columns) {
        updateWindowTopFromContentY();
    }
    positionViewport();
    trace->viewportCompletedNs = trace->enabled
        ? trace->timer.nsecsElapsed() : 0;
    if (!_deferDelegateWindowCommit) {
        updateProperties(animate);
    }
    trace->propertiesCompletedNs = trace->enabled
        ? trace->timer.nsecsElapsed() : 0;
    updateNeedScroll();
    if (!_deferDelegateWindowCommit
        && !qFuzzyCompare(oldContentY, _contentY)) {
        emit contentYChanged();
    }
}

void MasonryLayout::traceFixedRewrap(const RewrapTrace &trace) const
{
    if (!trace.enabled) {
        return;
    }
    const qint64 completedNs = trace.timer.nsecsElapsed();
    qInfo().nospace()
        << "F4_NAV_BENCHMARK_TRACE masonry.rewrap rows="
        << logicalBrickCount() << " mode="
        << static_cast<int>(_presentationMode)
        << " extentNs=" << trace.extentCompletedNs
        << " layoutNs="
        << (trace.layoutCompletedNs - trace.extentCompletedNs)
        << " bandsNs="
        << (trace.bandsCompletedNs - trace.layoutCompletedNs)
        << " viewportNs="
        << (trace.viewportCompletedNs - trace.bandsCompletedNs)
        << " propertiesNs="
        << (trace.propertiesCompletedNs - trace.viewportCompletedNs)
        << " totalNs=" << completedNs;
    ZoinGallery::MediaTimingTrace::event(
        QStringLiteral("qt.gallery.masonry.rewrap"), {
            {QStringLiteral("rows"), logicalBrickCount()},
            {QStringLiteral("mode"), static_cast<int>(_presentationMode)},
            {QStringLiteral("extentNs"), trace.extentCompletedNs},
            {QStringLiteral("layoutNs"),
             trace.layoutCompletedNs - trace.extentCompletedNs},
            {QStringLiteral("bandsNs"),
             trace.bandsCompletedNs - trace.layoutCompletedNs},
            {QStringLiteral("viewportNs"),
             trace.viewportCompletedNs - trace.bandsCompletedNs},
            {QStringLiteral("propertiesNs"),
             trace.propertiesCompletedNs - trace.viewportCompletedNs},
            {QStringLiteral("durationNs"), completedNs},
        });
}

void MasonryLayout::rewrapFixed(bool animate, RewrapTrace *trace)
{
    const qreal oldContentY = _contentY;
    const bool preserve = std::exchange(
        _preserveViewportAnchorForNextRewrap, false);
    const ViewportAnchor anchor = fixedViewportAnchor(preserve);
    const ZoinGallery::GalleryFixedLayoutPlan plan = fixedLayoutPlan();
    const int virtualRows = updateFixedContentExtent(plan);
    trace->extentCompletedNs = trace->enabled
        ? trace->timer.nsecsElapsed() : 0;

    calcFixedLayout();
    trace->layoutCompletedNs = trace->enabled
        ? trace->timer.nsecsElapsed() : 0;
    if (_presentationMode == Icons) {
        const qreal iconContentHeight = _paddingTop
            + virtualRows * virtualGridRowHeight() + _paddingBottom;
        setContentHeight(iconContentHeight);
        _contentY = qBound<qreal>(
            0, _contentY, qMax<qreal>(0, _contentHeight - height()));
    }
    rebuildLayoutBands();
    trace->bandsCompletedNs = trace->enabled
        ? trace->timer.nsecsElapsed() : 0;
    restoreFixedViewportAnchor(anchor);
    commitFixedViewport(oldContentY, animate, trace);
    traceFixedRewrap(*trace);
}

void MasonryLayout::rewrapSparseMasonry(bool animate,
                                        qreal currentIndexOffset)
{
    const qreal oldContentY = _contentY;
    const int count = logicalBrickCount();
    const int columns = virtualGridColumnCount();
    const int rows = columns > 0 ? (count + columns - 1) / columns : 0;
    setContentHeight(_paddingTop + rows * virtualGridRowHeight()
                     + _paddingBottom);
    _contentY = qBound<qreal>(
        0, _contentY, qMax<qreal>(0, _contentHeight - height()));
    rebuildLayoutBands();
    if (applyPreparedResetViewport()) {
        positionViewport();
    } else {
        qreal nextContentY = _contentY;
        if (currentIndexOffset != -1 && _currentIndex >= 0
            && _currentIndex < count) {
            nextContentY = qBound<qreal>(
                0, indexGeometry(_currentIndex).top() + currentIndexOffset,
                qMax<qreal>(0, _contentHeight - height()));
        } else if (_topItem >= 0 && _topItem < count) {
            nextContentY = qBound<qreal>(
                0, indexGeometry(_topItem).top() - _topItemOffset,
                qMax<qreal>(0, _contentHeight - height()));
        }
        if (!qFuzzyCompare(nextContentY + 1, _contentY + 1)) {
            setContentYInternal(nextContentY);
        }
        positionViewport();
    }
    if (!_deferDelegateWindowCommit) {
        updateProperties(animate);
    }
    updateNeedScroll();
    if (!_deferDelegateWindowCommit
        && !qFuzzyCompare(oldContentY + 1, _contentY + 1)) {
        emit contentYChanged();
    }
}

void MasonryLayout::rewrapMasonry(bool animate, qreal currentIndexOffset)
{
    calcLayout(_bricks, width() - _paddingLeft - _paddingRight,
               _targetHeight, _spacing, !_listView, _paddingTop,
               layoutMode());
    for (MasonryBrick &brick : _bricks) {
        const QRectF geometry = brick.geometry();
        brick.previewGeometry = geometry.isValid() && !geometry.isEmpty()
            ? geometry.adjusted(_spacing / 2.0, _spacing / 2.0,
                                -_spacing / 2.0, -_spacing / 2.0)
            : QRectF();
    }
    rebuildLayoutBands();
    if (_bricks.isEmpty()) {
        setContentHeight(0);
    } else {
        setContentHeight(static_cast<int>(
            _bricks.last().y + _bricks.last().normalizedSize.height()
            + _paddingBottom));
    }

    const qreal oldContentY = _contentY;
    if (applyPreparedResetViewport()) {
        positionViewport();
        if (!_deferDelegateWindowCommit) {
            updateProperties(animate);
            if (!qFuzzyCompare(oldContentY + 1, _contentY + 1)) {
                emit contentYChanged();
            }
        }
        return;
    }
    qreal nextContentY = _contentY;
    const qreal maximumOffset = qMax<qreal>(
        0, contentHeight() - height());
    if (currentIndexOffset != -1) {
        nextContentY = qBound<qreal>(
            0, _bricks[_currentIndex].y + currentIndexOffset,
            maximumOffset);
    } else if (_topItem < _bricks.size()) {
        nextContentY = qBound<qreal>(
            0, _bricks[_topItem].y - _topItemOffset,
            maximumOffset);
    }
    if (nextContentY != _contentY) {
        setContentYInternal(nextContentY);
    } else if (!_deferDelegateWindowCommit) {
        updateProperties(animate);
    }
}
