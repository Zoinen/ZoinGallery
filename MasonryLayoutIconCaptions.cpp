#include "MasonryLayout.h"
#include "GalleryIconTextMeasurer.h"
#include "GalleryPixelGrid.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

struct CaptionRequest {
    int index = 0;
    QString text;
    size_t nameHash = 0;
    qreal width = 0;
};

struct CaptionMetric {
    int index = 0;
    size_t nameHash = 0;
    char lines = 1;
};

} // namespace

void MasonryLayout::setIconLabelFont(const QFont &font) {
    if (_iconLabelFont == font) {
        return;
    }
    if (_presentationMode == Icons)
        _preserveViewportAnchorForNextRewrap = true;
    _iconLabelFont = font;
    emit iconLabelFontChanged();
    if (_presentationMode == Icons || _presentationMode == Grid || _presentationMode == Details
        || _presentationMode == Columns) {
        rewrap(false);
    }
}

void MasonryLayout::setCellHorizontalPadding(qreal padding) {
    if (!qIsFinite(padding)) return;
    padding = qMax<qreal>(0, padding);
    if (qFuzzyCompare(_cellHorizontalPadding + 1, padding + 1)) return;
    _cellHorizontalPadding = padding;
    invalidateIconCaptionMetrics();
    emit cellHorizontalPaddingChanged();
    if (_presentationMode == Masonry || _presentationMode == Grid || _presentationMode == Icons) {
        _preserveViewportAnchorForNextRewrap = true;
        rewrap(false);
    }
}

qreal MasonryLayout::cellCaptionWidth(const QRectF &geometry) const {
    const qreal dpr = devicePixelRatio();
    const qreal width = ZoinGallery::PixelGrid::snapDeviceRect(geometry, dpr).width();
    const qreal padding = _cellHorizontalPadding < 0 ? 4 : _cellHorizontalPadding;
    return qMax<qreal>(1 / dpr, width - 2 * ZoinGallery::PixelGrid::cellHorizontalInset(width, padding, dpr));
}

void MasonryLayout::setCellVerticalPadding(qreal padding) {
    if (!qIsFinite(padding)) return;
    padding = qMax<qreal>(0, padding);
    if (qFuzzyCompare(_cellVerticalPadding + 1, padding + 1)) return;
    _cellVerticalPadding = padding;
    if (qEnvironmentVariableIsSet("F4_NAV_BENCHMARK_TRACE"))
        qInfo() << "F4_NAV_BENCHMARK_TRACE [FIX] layout.cellVerticalPadding=" << padding;
    emit cellVerticalPaddingChanged();
    if (_presentationMode == Masonry || _presentationMode == Grid || _presentationMode == Icons) {
        // Vertical insets do not change the caption width or line counts.
        _preserveViewportAnchorForNextRewrap = true;
        rewrap(false);
    }
}

void MasonryLayout::setCellCaptionSpacing(qreal spacing) {
    if (!qIsFinite(spacing)) return;
    spacing = qBound<qreal>(0, spacing, 32);
    if (qFuzzyCompare(_cellCaptionSpacing + 1, spacing + 1)) return;
    _cellCaptionSpacing = spacing;
    emit cellCaptionSpacingChanged();
    if (_presentationMode == Grid || _presentationMode == Icons) {
        // Reuse the existing wrapped-text measurements.
        _preserveViewportAnchorForNextRewrap = true;
        rewrap(false);
    }
}

void MasonryLayout::invalidateIconCaptionMetrics() {
    if (_iconCaptionCancelled)
        _iconCaptionCancelled->store(true, std::memory_order_relaxed);
    ++_iconCaptionGeneration;
    ++_iconCaptionStyleRevision;
    _iconCaptionLineCounts.clear();
    _iconCaptionNameHashes.clear();
    _iconCaptionMetricsDirty = true;
}

void MasonryLayout::prepareIconCaptionMetrics() {
    if (_presentationMode != Icons) return;
    const int count = logicalBrickCount();
    const int columns = virtualGridColumnCount();
    const qreal cellWidth = qMax<qreal>(1,
        (width() - _paddingLeft - _paddingRight) / columns);
    const qreal dpr = devicePixelRatio();
    if (_measuredIconFont != _iconLabelFont
        || _measuredIconCellWidth != cellWidth || _measuredIconDpr != dpr
        || _iconCaptionLineCounts.size() != count) {
        invalidateIconCaptionMetrics();
        _measuredIconFont = _iconLabelFont;
        _measuredIconCellWidth = cellWidth;
        _measuredIconDpr = dpr;
        _iconCaptionLineCounts = QByteArray(count, 0);
        _iconCaptionNameHashes.resize(count);
    }
    if (!_iconCaptionMetricsDirty || count <= 0) return;
    _iconCaptionMetricsDirty = false;
    // Rewrap may have queried the outgoing geometry to capture its cursor
    // offset. Never retain that plan after replacing the caption metrics.
    _fixedLayoutPlanCached = false;
    const bool trace = qEnvironmentVariableIsSet("F4_NAV_BENCHMARK_TRACE");
    QElapsedTimer timer;
    if (trace) timer.start();

    const QList<int> loaded = _sparseCatalogRows ? materializedModelRows() : QList<int>();
    const int available = _sparseCatalogRows ? loaded.size() : count;
    const int firstVisible = qMax(0, _topItem - columns);
    const int lastVisible = firstVisible + columns *
        (qMax(1, int(std::ceil(height() / cellWidth))) + 3);
    ZoinGallery::GalleryIconTextMeasurer measurer(_iconLabelFont);
    QVector<CaptionRequest> pending;
    int synchronousCount = 0;
    for (int offset = 0; offset < available; ++offset) {
        const int index = _sparseCatalogRows ? loaded.at(offset) : offset;
        if (index < 0 || index >= count) continue;
        const MasonryBrick *brick = brickAt(index);
        const QString text = _sparseCatalogRows
            ? _model->index(index, 0).data(_entryNameRole).toString()
            : brick ? brick->modelText : QString();
        const int group = groupingActive() ? _groupIndex.groupForSourceIndex(index) : -1;
        int local = group >= 0 ? index - _groupIndex.descriptor(group)->startIndex : index;
        if (!groupingActive() && !_groupRanges.isEmpty()) {
            const auto next = std::upper_bound(_groupRanges.cbegin(), _groupRanges.cend(), index,
                [](int row, const auto &range) { return row < range.start; });
            if (next != _groupRanges.cbegin()) {
                const auto &range = *std::prev(next);
                local = index - (index < range.start + range.count
                    ? range.start : range.start + range.count);
            }
        }
        const int column = local % columns;
        const QRectF rect(column * cellWidth, 0, cellWidth, cellWidth);
        const qreal labelWidth = cellCaptionWidth(rect);
        const size_t nameHash = qHashMulti(0, text, labelWidth);
        if (_iconCaptionLineCounts.at(index) > 0
            && _iconCaptionNameHashes.at(index) == nameHash) continue;
        _iconCaptionLineCounts[index] = 0;
        _iconCaptionNameHashes[index] = nameHash;
        // A viewport is measured before it is painted. Large complete catalogs
        // use a worker for the rest; no ImageFile facade is requested here.
        if (synchronousCount < 128 && (available <= 128
            || (index >= firstVisible && index <= lastVisible))) {
            _iconCaptionLineCounts[index] = char(measurer.lineCount(text, labelWidth));
            ++synchronousCount;
        } else {
            pending.append({index, text, nameHash, labelWidth});
        }
    }
    _iconCaptionMeasurementCount += synchronousCount + pending.size();
    if (trace)
        qInfo() << "F4_NAV_BENCHMARK_TRACE icons.captionPrepare loaded=" << available
                << "synchronous=" << synchronousCount << "worker=" << pending.size()
                << "durationNs=" << timer.nsecsElapsed();
    if (pending.isEmpty()) return;

    const quint64 generation = _iconCaptionGeneration;
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    _iconCaptionCancelled = cancelled;
    auto *watcher = new QFutureWatcher<QVector<CaptionMetric>>(this);
    connect(watcher, &QFutureWatcher<QVector<CaptionMetric>>::finished,
        this, [this, watcher, generation]() {
            const auto metrics = watcher->result();
            watcher->deleteLater();
            if (generation != _iconCaptionGeneration) return;
            bool changed = false;
            for (const auto &metric : metrics) {
                if (metric.index >= _iconCaptionLineCounts.size()
                    || _iconCaptionNameHashes.at(metric.index) != metric.nameHash) continue;
                changed |= _iconCaptionLineCounts.at(metric.index) != metric.lines;
                _iconCaptionLineCounts[metric.index] = metric.lines;
            }
            if (changed && _presentationMode == Icons) {
                _preserveViewportAnchorForNextRewrap = true;
                requestRewrap(false);
            }
        });
    const QFont font = _iconLabelFont;
    watcher->setFuture(QtConcurrent::run([font, pending = std::move(pending), cancelled]() {
        QElapsedTimer timer;
        timer.start();
        ZoinGallery::GalleryIconTextMeasurer measurer(font);
        QVector<CaptionMetric> result;
        result.reserve(pending.size());
        for (const auto &request : pending) {
            if (cancelled->load(std::memory_order_relaxed)) break;
            result.append({request.index, request.nameHash,
                char(measurer.lineCount(request.text, request.width))});
        }
        if (qEnvironmentVariableIsSet("F4_NAV_BENCHMARK_TRACE"))
            qInfo() << "F4_NAV_BENCHMARK_TRACE icons.captionMeasure rows="
                    << result.size() << "durationNs=" << timer.nsecsElapsed();
        return result;
    }));
}
