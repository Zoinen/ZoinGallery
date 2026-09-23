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
    } else if (groupingActive() && !_sparseCatalogRows) {
        rewrapGroupedMasonry(animate, currentIndexOffset);
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
    if (groupingActive()) {
        rebuildGroupSections(plan);
        if (!_groupSections.isEmpty()) {
            const GroupSection &last = _groupSections.constLast();
            if (_presentationMode == Columns) {
                setContentHeight(last.offset + last.extent);
            }
            else {
                setContentHeight(last.offset + last.extent + _paddingBottom);
            }
        }
        else {
            setContentHeight(0);
        }
        if (_presentationMode == Columns) {
            _contentY = qBound<qreal>(0, _contentY, maximumContentOffset());
        }
        else {
            _contentY = qBound<qreal>(
                0, _contentY, qMax<qreal>(0, _contentHeight - height()));
        }
        return 0;
    }
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
    if (_presentationMode == Icons && !groupingActive()) {
        const qreal iconContentHeight = _paddingTop
            + virtualRows * virtualGridRowHeight() + _paddingBottom;
        setContentHeight(iconContentHeight);
        _contentY = qBound<qreal>(
            0, _contentY, qMax<qreal>(0, _contentHeight - height()));
    }
    rebuildLayoutBands();
    if (groupingActive()) {
        emit groupHeadersChanged();
    }
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
    if (groupingActive()) {
        rebuildGroupSections(fixedLayoutPlan());
        if (!_groupSections.isEmpty()) {
            const GroupSection &last = _groupSections.constLast();
            setContentHeight(last.offset + last.extent + _paddingBottom);
        }
        else {
            setContentHeight(_paddingTop + rows * virtualGridRowHeight()
                             + _paddingBottom);
        }
        emit groupHeadersChanged();
    }
    else {
        setContentHeight(_paddingTop + rows * virtualGridRowHeight()
                         + _paddingBottom);
    }
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
    if (groupingActive() && !_containedPreview) {
        rewrapGroupedMasonry(animate, currentIndexOffset);
        return;
    }
    calcLayout(_bricks, width() - _paddingLeft - _paddingRight,
               _targetHeight, _spacing, !_listView, _paddingTop,
               layoutMode());
    if (_containedPreview) {
        const qreal dpr = devicePixelRatio();
        const int columns = width() < 80 ? 1 : width() < 150 ? 2 : width() < 300 ? 3 : 4;
        const int gap = qMax(1, qRound(_spacing * dpr));
        const int canvasWidth = qMax(0, qRound(width() * dpr));
        const int canvasHeight = qMax(0, qRound(height() * dpr));
        for (int i = 0; i < _bricks.size(); ++i) {
            auto &brick = _bricks[i];
            if (i >= columns * columns) { brick.normalizedSize = {}; continue; }
            const int column = i % columns, row = i / columns;
            const auto edge = [gap, columns](int extent, int cell) {
                return gap + qRound(qreal(extent - gap) * cell / columns);
            };
            const int left = edge(canvasWidth, column), top = edge(canvasHeight, row);
            const int right = edge(canvasWidth, column + 1) - gap;
            const int bottom = edge(canvasHeight, row + 1) - gap;
            const QSizeF original = brick.originalSize.isEmpty() ? QSizeF(1, 1) : brick.originalSize;
            const QSizeF fit = original.scaled(QSizeF(qMax(0, right - left), qMax(0, bottom - top)), Qt::KeepAspectRatio);
            const int w = qRound(fit.width()), h = qRound(fit.height());
            brick.x = (left + (right - left - w) / 2) / dpr;
            brick.y = (top + (bottom - top - h) / 2) / dpr;
            brick.normalizedSize = QSizeF(w / dpr, h / dpr);
            brick.row = row;
            brick.column = column;
        }
    }
    for (MasonryBrick &brick : _bricks) {
        const QRectF geometry = brick.geometry();
        brick.previewGeometry = _containedPreview ? geometry : geometry.isValid() && !geometry.isEmpty()
            ? geometry.adjusted(_spacing / 2.0, _spacing / 2.0,
                                -_spacing / 2.0, -_spacing / 2.0)
            : QRectF();
    }
    rebuildLayoutBands();
    if (_containedPreview) {
        setContentHeight(height());
        _contentY = 0;
    } else if (_bricks.isEmpty()) {
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
            0, indexGeometry(_currentIndex).y() + currentIndexOffset,
            maximumOffset);
    } else if (_topItem < _bricks.size()) {
        // The saved anchor uses indexGeometry(), whose first row includes
        // the top padding. Restoring from the raw brick Y adds that padding
        // once per inserted row and walks a stationary viewport downward.
        nextContentY = qBound<qreal>(
            0, indexGeometry(_topItem).y() - _topItemOffset,
            maximumOffset);
    }
    if (nextContentY != _contentY) {
        if (ZoinGallery::MediaTimingTrace::enabled()) {
            ZoinGallery::MediaTimingTrace::event(QStringLiteral("qt.gallery.viewport.rewrap"), {
                {QStringLiteral("fix"), QStringLiteral("[FIX:parent-reentry]")},
                {QStringLiteral("currentIndex"), _currentIndex},
                {QStringLiteral("currentIndexOffset"), currentIndexOffset},
                {QStringLiteral("topItem"), _topItem},
                {QStringLiteral("topItemOffset"), _topItemOffset},
                {QStringLiteral("previous"), _contentY},
                {QStringLiteral("next"), nextContentY},
                {QStringLiteral("count"), _bricks.size()},
                {QStringLiteral("firstPath"), brickPath(0)},
            });
        }
        setContentYInternal(nextContentY);
    } else if (!_deferDelegateWindowCommit) {
        updateProperties(animate);
    }
}

void MasonryLayout::rewrapGroupedMasonry(
    bool animate, qreal currentIndexOffset)
{
    const qreal oldContentY = _contentY;
    const int count = logicalBrickCount();
    if (count <= 0 || _groupIndex.groupCount() == 0) {
        _groupSections.clear();
        _groupIndex.setSectionGeometry({}, {}, 0);
        setContentHeight(0);
        rebuildLayoutBands();
        if (!_deferDelegateWindowCommit) {
            updateProperties(animate);
        }
        return;
    }

    const int canvasWidth = qMax(0, static_cast<int>(
        width() - _paddingLeft - _paddingRight));
    const int targetHeight = qMax(1, static_cast<int>(_targetHeight));
    const int spacing = qMax(0, static_cast<int>(_spacing));
    const bool matchTrailingRow = !_listView;
    const CalcLayoutMode mode = layoutMode();
    int globalRowBase = 0;
    auto layoutRange = [&](int first, int rangeCount, qreal origin,
                           qreal header, qreal padding) -> qreal {
        QList<MasonryBrick> local;
        local.reserve(rangeCount);
        for (int i = 0; i < rangeCount; ++i) {
            local.append(_bricks.at(first + i));
        }
        calcLayout(local, canvasWidth, targetHeight, spacing,
                   matchTrailingRow, padding, mode);
        qreal extent = padding;
        int localRows = 0;
        for (int i = 0; i < local.size(); ++i) {
            const MasonryBrick &source = local.at(i);
            MasonryBrick &target = _bricks[first + i];
            target.x = source.x;
            target.y = origin + header + source.y;
            target.normalizedSize = source.normalizedSize;
            target.row = globalRowBase + qMax(0, source.row);
            target.column = source.column;
            target.temporaryLineBreakAfter = false;
            target.previewGeometry = target.normalizedSize.isValid()
                && !target.normalizedSize.isEmpty()
                ? QRectF(target.x, target.y, target.normalizedSize.width(),
                         target.normalizedSize.height()).adjusted(
                             _spacing / 2.0, _spacing / 2.0,
                             -_spacing / 2.0, -_spacing / 2.0)
                : QRectF();
            extent = qMax(extent,
                          source.y + source.normalizedSize.height());
            localRows = qMax(localRows, source.row + 1);
        }
        globalRowBase += localRows;
        return extent;
    };

    const int groupCount = _groupIndex.groupCount();
    _groupSections.resize(groupCount);
    QVector<qreal> offsets;
    QVector<qreal> extents;
    offsets.reserve(groupCount);
    extents.reserve(groupCount);

    const int prefixCount = _groupIndex.descriptor(0)->startIndex;
    qreal cursor = _paddingTop;
    if (prefixCount > 0) {
        cursor = layoutRange(0, qMin(prefixCount, count), 0, 0,
                             _paddingTop);
    }

    const qreal headerExtent = groupHeaderHeight();
    for (int group = 0; group < groupCount; ++group) {
        const auto *descriptor = _groupIndex.descriptor(group);
        GroupSection &section = _groupSections[group];
        section.offset = cursor;
        section.columns = 1;
        section.rowsPerColumn = 1;
        offsets.append(cursor);
        if (_groupIndex.isCollapsed(group)) {
            // Keep the group's cached file geometry intact. The visibility
            // index and indexGeometry() hide it without walking every file;
            // this is important when one large group is collapsed.
            section.extent = headerExtent;
        }
        else {
            const qreal localExtent = layoutRange(
                descriptor->startIndex, descriptor->count, cursor,
                headerExtent, 0);
            section.extent = headerExtent + localExtent;
        }
        extents.append(section.extent);
        cursor += section.extent;
    }

    const int trailingStart = groupCount > 0
        ? _groupIndex.descriptor(groupCount - 1)->startIndex
          + _groupIndex.descriptor(groupCount - 1)->count : prefixCount;
    if (trailingStart < count) {
        cursor += layoutRange(trailingStart, count - trailingStart,
                              cursor, 0, 0);
    }
    _groupIndex.setSectionGeometry(offsets, extents, headerExtent);
    setContentHeight(cursor + _paddingBottom);
    _contentY = qBound<qreal>(
        0, _contentY, qMax<qreal>(0, _contentHeight - height()));
    rebuildLayoutBands();
    emit groupHeadersChanged();

    if (applyPreparedResetViewport()) {
        positionViewport();
    }
    else {
        qreal nextContentY = _contentY;
        if (currentIndexOffset != -1 && _currentIndex >= 0
            && _currentIndex < count) {
            nextContentY = qBound<qreal>(
                0, indexGeometry(_currentIndex).top() + currentIndexOffset,
                qMax<qreal>(0, _contentHeight - height()));
        }
        else if (_topItem >= 0 && _topItem < count) {
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
