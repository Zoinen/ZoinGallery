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

int MasonryLayout::indexAtViewport(qreal x, qreal y) const {
    return _presentationMode == Columns
        ? indexAt(x - _paddingLeft + _contentY, y)
        : indexAt(x - _paddingLeft, y + _contentY);
}

QVariantList MasonryLayout::indexesInViewportRect(qreal x, qreal y, qreal width, qreal height) const {
    QRectF viewportRect(x, y, width, height);
    viewportRect = viewportRect.normalized();
    QRectF contentRect = _presentationMode == Columns
        ? QRectF(viewportRect.x() - _paddingLeft + _contentY,
                 viewportRect.y(), viewportRect.width(), viewportRect.height())
        : QRectF(viewportRect.x() - _paddingLeft,
                 viewportRect.y() + _contentY,
                 viewportRect.width(), viewportRect.height());

    QVariantList result;
    const QList<int> candidates = _presentationMode == Columns
        ? indexesForHorizontalRange(contentRect.left(), contentRect.right())
        : indexesForVerticalRange(contentRect.top(), contentRect.bottom());
    for (const int index : candidates) {
        if (indexGeometry(index).intersects(contentRect)) {
            result.append(index);
        }
    }
    return result;
}

QRectF MasonryLayout::indexGeometry(int index) const {
    if (index < 0 || index >= logicalBrickCount()) {
        return QRectF();
    }
    if (_presentationMode != Masonry || sparseVirtualLayout()) {
        return analyticFixedGeometry(index);
    }
    const MasonryBrick *brick = brickAt(index);
    if (!brick || !brick->normalizedSize.isValid() ||
        brick->normalizedSize.isEmpty()) {
        return QRectF();
    }
    if (_presentationMode != Masonry) {
        return brick->geometry();
    }
    if (brick->row == _bricks.constLast().row) {
        return brick->geometry().adjusted(0, 0, 0, _paddingBottom);
    }
    if (!brick->row) {
        return brick->geometry().adjusted(0, -_paddingTop, 0, 0);
    }
    return brick->geometry();
}

QRectF MasonryLayout::indexPreviewGeometry(int index) const {
    if ((_presentationMode != Masonry || sparseVirtualLayout())
        && index >= 0 && index < logicalBrickCount()) {
        MasonryBrick temporary;
        if (const MasonryBrick *source = brickAt(index)) {
            temporary.modelText = source->modelText;
            temporary.image = source->image;
        }
        applyAnalyticFixedGeometry(temporary, index);
        return temporary.previewGeometry;
    }
    const MasonryBrick *brick = brickAt(index);
    if (brick) {
        return brick->previewGeometry;
    }
    return QRectF();
}

QString MasonryLayout::indexImageIdUrl(int index) const {
    if (index >= 0 && index < logicalBrickCount()) {
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(index);
        return image ? image->imageIdUrl() : QString();
    }
    return QString();
}

QString MasonryLayout::indexText(int index) const {
    if (index >= 0 && index < logicalBrickCount()) {
        const MasonryBrick *brick = brickAt(index);
        if (brick && !brick->modelText.isEmpty()) {
            return brick->modelText;
        }
        if (_sparseCatalogRows && _model && _entryNameRole >= 0) {
            const QString text = _model->index(index, 0)
                                     .data(_entryNameRole).toString();
            if (!text.isEmpty()) {
                return text;
            }
        }
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(index);
        return image ? image->fileName() : QString();
    }
    return QString();
}

QString MasonryLayout::indexFullPath(int index) const {
    if (index >= 0 && index < logicalBrickCount()) {
        return QDir::toNativeSeparators(brickPath(index));
    }
    return QString();
}

QSize MasonryLayout::indexOriginalSize(int index) const {
    if (index >= 0 && index < logicalBrickCount()) {
        const MasonryBrick *brick = brickAt(index);
        if (brick && brick->modelKnownSize.isValid()) {
            return brick->modelKnownSize;
        }
        return brick ? brick->originalSize.toSize() : QSize();
    }
    return QSize();
}

QVariantMap MasonryLayout::indexExif(int index) const {
    if (index >= 0 && index < logicalBrickCount()) {
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(index);
        return image ? image->info().exif : QVariantMap();
    }
    return QVariantMap();
}

int MasonryLayout::nextImageIndex(bool forward, bool moveToEnd) {
    int nextIndex = _currentIndex;
    for (int i = _currentIndex + (forward ? 1 : -1);
         i >= 0 && i < logicalBrickCount();
         i += (forward ? 1 : -1)) {
        if (brickIsImage(i)) {
            nextIndex = i;
            if (!moveToEnd) {
                break;
            }
        }
    }
    return nextIndex;
}

int MasonryLayout::neighborIndex(
    int index, NavigationDirection direction) const {
    return navigationTarget(index, direction, false)
        .value(QStringLiteral("targetIndex"), index).toInt();
}

int MasonryLayout::pageIndex(
    int index, NavigationDirection direction) const {
    return navigationTarget(index, direction, true)
        .value(QStringLiteral("targetIndex"), index).toInt();
}

int MasonryLayout::columnsNavigationIndex(
    int index, NavigationDirection direction, bool page,
    int lastIndex) const {
    const int rows = rowsPerColumn();
    if (page) {
        const bool backwards =
            direction == NavigateLeft || direction == NavigateUp;
        const int step = rows * effectiveColumnCount();
        return qBound(0, index + (backwards ? -step : step), lastIndex);
    }
    const int column = index / rows;
    switch (direction) {
    case NavigateLeft:
        return column > 0 ? index - rows : 0;
    case NavigateRight:
        return qMin(lastIndex, index + rows);
    case NavigateUp:
        return qMax(0, index - 1);
    case NavigateDown:
        return qMin(lastIndex, index + 1);
    }
    return index;
}

int MasonryLayout::masonryNavigationIndex(
    int index, NavigationDirection direction, int lastIndex) const {
    if (direction == NavigateLeft) {
        return qMax(0, index - 1);
    }
    if (direction == NavigateRight) {
        return qMin(lastIndex, index + 1);
    }
    const QRectF current = _bricks.at(index).geometry();
    if (!current.isValid() || current.isEmpty()) {
        return index;
    }
    const qreal probeY = direction == NavigateUp
        ? current.top() - 2 : current.bottom() + 2;
    const int adjacent = indexAt(current.center().x(), probeY);
    return adjacent >= 0 ? adjacent : index;
}

int MasonryLayout::fixedNavigationIndex(
    int index, NavigationDirection direction, bool page,
    int lastIndex) const {
    const qreal extent = (_presentationMode == Icons
                          || sparseVirtualLayout())
        ? virtualGridRowHeight()
        : qMax<qreal>(1.0, effectiveTargetExtent());
    const int columns =
        (_presentationMode == Grid || _presentationMode == Icons)
        ? virtualGridColumnCount() : 1;
    const int viewportRows = qMax(
        1, static_cast<int>(std::floor(
               qMax<qreal>(1.0, height() - _paddingTop - _paddingBottom)
               / extent)));
    const int capacity = qMax(1, viewportRows * columns);
    if (page) {
        const bool backwards =
            direction == NavigateLeft || direction == NavigateUp;
        return qBound(
            0, index + (backwards ? -capacity : capacity), lastIndex);
    }
    if (_presentationMode == Details) {
        switch (direction) {
        case NavigateLeft: return qMax(0, index - capacity);
        case NavigateRight: return qMin(lastIndex, index + capacity);
        case NavigateUp: return qMax(0, index - 1);
        case NavigateDown: return qMin(lastIndex, index + 1);
        }
    }
    if (_presentationMode == Grid || _presentationMode == Icons
        || sparseVirtualLayout()) {
        switch (direction) {
        case NavigateLeft: return qMax(0, index - 1);
        case NavigateRight: return qMin(lastIndex, index + 1);
        case NavigateUp: return qMax(0, index - columns);
        case NavigateDown: return qMin(lastIndex, index + columns);
        }
    }
    return masonryNavigationIndex(index, direction, lastIndex);
}

QVariantMap MasonryLayout::navigationTarget(
    int index, NavigationDirection direction, bool page) const {
    if (_lightweightRewrapPending) {
        const_cast<MasonryLayout *>(this)->flushLightweightRewrap();
    }
    QVariantMap result{
        {QStringLiteral("targetIndex"), index},
        {QStringLiteral("windowTopIndex"), _windowTopIndex},
    };
    const int count = logicalBrickCount();
    if (count <= 0 || index < 0 || index >= count) {
        return result;
    }
    const int lastIndex = count - 1;
    const int target = _presentationMode == Columns
        ? columnsNavigationIndex(index, direction, page, lastIndex)
        : fixedNavigationIndex(index, direction, page, lastIndex);
    result[QStringLiteral("targetIndex")] =
        qBound(0, target, lastIndex);
    return result;
}


struct MasonryLayout::MasonryPageState {
    int currentIndex = -1;
    qreal anchorX = 0;
    qreal itemViewportY = 0;
    qreal planned = 0;
    qreal rowViewportY = 0;
    int pageDirection = 1;
    qreal distance = 1;
    qreal maximum = 0;
    bool hasViewportRow = false;
    int sourceBandIndex = -1;
    int targetBandIndex = -1;
    qreal sourceBandTop = 0;
    qreal targetBandTop = 0;
    qreal destination = 0;
    bool terminalClamp = false;
    bool atStart = false;
    bool atEnd = false;
    qreal lastProbeY = 0;
    int targetIndex = -1;
    bool hitStart = false;
    bool hitEnd = false;
};

QVariantMap MasonryLayout::initialMasonryPageResult(
    const MasonryPageState &state) const {
    return {
        {QStringLiteral("valid"), false},
        {QStringLiteral("layoutRevision"),
         QVariant::fromValue<qulonglong>(_layoutRevision)},
        {QStringLiteral("targetIndex"), state.currentIndex},
        {QStringLiteral("contentY"), state.planned},
        {QStringLiteral("rowViewportY"), state.rowViewportY},
        {QStringLiteral("sourceBandIndex"), -1},
        {QStringLiteral("targetBandIndex"), -1},
        {QStringLiteral("sourceBandTop"), 0.0},
        {QStringLiteral("targetBandTop"), 0.0},
        {QStringLiteral("hitEdge"), false},
        {QStringLiteral("terminalClamp"), false},
    };
}

QVariantMap MasonryLayout::sparseMasonryPagePlan(
    MasonryPageState state, QVariantMap result) const {
    state.destination = qBound<qreal>(
        0, state.planned + state.pageDirection * state.distance,
        state.maximum);
    const qreal retainedRowViewportY =
        std::isfinite(state.rowViewportY)
        ? state.rowViewportY
        : (std::isfinite(state.itemViewportY)
           ? state.itemViewportY
           : indexGeometry(state.currentIndex).top() - state.planned);
    state.atStart = state.destination <= 0.01;
    state.atEnd = state.destination >= state.maximum - 0.01;
    if (state.atStart || state.atEnd) {
        const int columns = virtualGridColumnCount();
        const int column = qBound(
            0, state.currentIndex % qMax(1, columns),
            qMax(0, columns - 1));
        if (state.atStart) {
            state.targetIndex = qMin(logicalBrickCount() - 1, column);
        } else {
            const int lastRowStart =
                ((logicalBrickCount() - 1) / qMax(1, columns))
                * qMax(1, columns);
            state.targetIndex = qMin(
                logicalBrickCount() - 1, lastRowStart + column);
        }
    }
    if (state.targetIndex < 0) {
        const qreal lastProbeY = qMax<qreal>(
            0, std::nextafter(
                   _contentHeight,
                   -std::numeric_limits<qreal>::infinity()));
        state.targetIndex = indexAt(
            state.anchorX,
            qBound<qreal>(
                0, state.destination + retainedRowViewportY, lastProbeY));
    }
    if (state.targetIndex < 0) {
        state.targetIndex = state.pageDirection < 0
            ? 0 : logicalBrickCount() - 1;
    }
    result[QStringLiteral("valid")] = true;
    result[QStringLiteral("targetIndex")] = state.targetIndex;
    result[QStringLiteral("contentY")] = state.destination;
    result[QStringLiteral("rowViewportY")] = retainedRowViewportY;
    result[QStringLiteral("hitEdge")] = state.atStart || state.atEnd;
    result[QStringLiteral("terminalClamp")] =
        state.atStart || state.atEnd;
    return result;
}

int MasonryLayout::closestLayoutBand(
    qreal target, int first, int last, bool preferHigherOnTie) const {
    first = qMax(0, first);
    last = qMin(last, _layoutBands.size() - 1);
    if (first > last) {
        return -1;
    }
    const auto begin = _layoutBands.cbegin() + first;
    const auto end = _layoutBands.cbegin() + last + 1;
    const auto next = std::lower_bound(
        begin, end, target,
        [](const LayoutBand &band, qreal value) {
            return band.top < value;
        });
    if (next == begin) {
        return first;
    }
    if (next == end) {
        return last;
    }
    const int nextIndex =
        int(std::distance(_layoutBands.cbegin(), next));
    const int previousIndex = nextIndex - 1;
    const qreal previousDistance =
        qAbs(_layoutBands.at(previousIndex).top - target);
    const qreal nextDistance =
        qAbs(_layoutBands.at(nextIndex).top - target);
    if (qFuzzyCompare(previousDistance, nextDistance)) {
        return preferHigherOnTie ? nextIndex : previousIndex;
    }
    return previousDistance < nextDistance ? previousIndex : nextIndex;
}

bool MasonryLayout::prepareDenseMasonryPage(
    MasonryPageState *state) const {
    state->hasViewportRow = std::isfinite(state->rowViewportY);
    if (state->hasViewportRow) {
        state->sourceBandIndex = closestLayoutBand(
            state->planned + state->rowViewportY,
            0, _layoutBands.size() - 1);
    } else {
        const auto after = std::upper_bound(
            _layoutBands.cbegin(), _layoutBands.cend(), state->planned,
            [](qreal value, const LayoutBand &band) {
                return value < band.top;
            });
        state->sourceBandIndex = after == _layoutBands.cbegin()
            ? 0 : int(std::distance(_layoutBands.cbegin(), after)) - 1;
    }
    if (state->sourceBandIndex < 0) {
        return false;
    }
    state->sourceBandTop =
        _layoutBands.at(state->sourceBandIndex).top;
    if (!state->hasViewportRow) {
        state->rowViewportY = state->sourceBandTop - state->planned;
    }
    const int firstCandidate = state->pageDirection < 0
        ? 0 : state->sourceBandIndex + 1;
    const int lastCandidate = state->pageDirection < 0
        ? state->sourceBandIndex - 1 : _layoutBands.size() - 1;
    state->targetBandIndex = closestLayoutBand(
        state->sourceBandTop + state->pageDirection * state->distance,
        firstCandidate, lastCandidate, state->pageDirection < 0);
    state->destination =
        state->pageDirection < 0 ? 0 : state->maximum;
    state->targetBandTop = state->sourceBandTop;
    if (state->targetBandIndex >= 0) {
        state->targetBandTop =
            _layoutBands.at(state->targetBandIndex).top;
        state->destination =
            state->targetBandTop - state->rowViewportY;
    }
    state->terminalClamp = state->targetBandIndex < 0
        || state->destination < 0
        || state->destination > state->maximum;
    state->destination = qBound<qreal>(
        0, state->destination, state->maximum);
    state->atStart = state->destination <= 0.01;
    state->atEnd = state->destination >= state->maximum - 0.01;
    state->lastProbeY = qMax<qreal>(
        0, std::nextafter(
               _contentHeight,
               -std::numeric_limits<qreal>::infinity()));
    return true;
}

void MasonryLayout::resolveDenseMasonryPageTarget(
    MasonryPageState *state) const {
    const qreal probeY = qBound<qreal>(
        0, state->destination + state->itemViewportY,
        state->lastProbeY);
    state->targetIndex = indexAt(state->anchorX, probeY);
    if (state->targetIndex < 0 && !state->atStart && !state->atEnd) {
        const int probeBandIndex = closestLayoutBand(
            probeY, 0, _layoutBands.size() - 1);
        if (probeBandIndex >= 0) {
            qreal closestDistance = std::numeric_limits<qreal>::max();
            for (const int index :
                 _layoutBands.at(probeBandIndex).indexes) {
                const QRectF geometry = _bricks.at(index).geometry();
                const qreal horizontalDistance =
                    state->anchorX < geometry.left()
                    ? geometry.left() - state->anchorX
                    : state->anchorX > geometry.right()
                        ? state->anchorX - geometry.right() : 0;
                if (horizontalDistance < closestDistance) {
                    closestDistance = horizontalDistance;
                    state->targetIndex = index;
                }
            }
        }
    }
    if (state->targetIndex < 0) {
        state->targetIndex =
            state->pageDirection < 0 ? 0 : _bricks.size() - 1;
    }
    state->hitStart = state->targetIndex == 0;
    state->hitEnd = state->targetIndex >= _bricks.size() - 1;
    if (state->pageDirection < 0
        && state->targetIndex == state->currentIndex
        && state->atStart) {
        state->targetIndex = indexAt(
            state->anchorX, qMin<qreal>(state->lastProbeY, 1));
        state->hitStart = true;
        if (state->targetIndex == state->currentIndex
            || state->targetIndex < 0) {
            state->targetIndex = 0;
        }
    } else if (state->pageDirection > 0
               && state->targetIndex == state->currentIndex
               && state->atEnd) {
        state->targetIndex = indexAt(
            state->anchorX, state->lastProbeY);
        state->hitEnd = true;
        if (state->targetIndex < 0) {
            state->targetIndex = indexAt(
                state->anchorX,
                qMax<qreal>(
                    0, _contentHeight
                       - effectiveTargetExtent() * 0.5));
        }
        if (state->targetIndex == state->currentIndex
            || state->targetIndex < 0) {
            state->targetIndex = _bricks.size() - 1;
        }
    }
    state->targetIndex = qBound(
        0, state->targetIndex, _bricks.size() - 1);
    state->hitStart = state->hitStart || state->targetIndex == 0;
    state->hitEnd = state->hitEnd
        || state->targetIndex == _bricks.size() - 1;
}

QVariantMap MasonryLayout::commitDenseMasonryPage(
    const MasonryPageState &state, QVariantMap result) const {
    result[QStringLiteral("valid")] = true;
    result[QStringLiteral("targetIndex")] = state.targetIndex;
    result[QStringLiteral("contentY")] = state.destination;
    result[QStringLiteral("rowViewportY")] = state.rowViewportY;
    result[QStringLiteral("sourceBandIndex")] = state.sourceBandIndex;
    result[QStringLiteral("targetBandIndex")] = state.targetBandIndex;
    result[QStringLiteral("sourceBandTop")] = state.sourceBandTop;
    result[QStringLiteral("targetBandTop")] = state.targetBandTop;
    result[QStringLiteral("hitEdge")] =
        state.hitStart || state.hitEnd;
    result[QStringLiteral("terminalClamp")] = state.terminalClamp;
    return result;
}

QVariantMap MasonryLayout::denseMasonryPagePlan(
    MasonryPageState state, QVariantMap result) const {
    if (!prepareDenseMasonryPage(&state)) {
        return result;
    }
    resolveDenseMasonryPageTarget(&state);
    return commitDenseMasonryPage(state, std::move(result));
}

QVariantMap MasonryLayout::masonryPagePlan(
    int currentIndex, qreal anchorX, qreal itemViewportY,
    qreal plannedContentY, qreal rowViewportY, int direction,
    qreal preferredDistance) const {
    MasonryPageState state;
    state.currentIndex = currentIndex;
    state.anchorX = anchorX;
    state.itemViewportY = itemViewportY;
    state.rowViewportY = rowViewportY;
    state.pageDirection = direction < 0 ? -1 : 1;
    state.distance = qMax<qreal>(1, qAbs(preferredDistance));
    state.maximum = qMax<qreal>(0, _contentHeight - height());
    state.planned = qBound<qreal>(
        0, plannedContentY, state.maximum);
    QVariantMap result = initialMasonryPageResult(state);
    if (_presentationMode != Masonry || currentIndex < 0
        || currentIndex >= logicalBrickCount() || direction == 0) {
        return result;
    }
    if (sparseVirtualLayout()) {
        return sparseMasonryPagePlan(state, std::move(result));
    }
    if (_layoutBands.isEmpty() || _bricks.isEmpty()
        || currentIndex >= _bricks.size()) {
        return result;
    }
    return denseMasonryPagePlan(state, std::move(result));
}


int MasonryLayout::windowTopIndexForIndex(int index) const {
    const int count = logicalBrickCount();
    if (_presentationMode != Columns || count <= 0) {
        return 0;
    }
    index = qBound(0, index, count - 1);
    const int rows = rowsPerColumn();
    const int columnTop = qBound(0, (index / rows) * rows,
                                 maximumWindowTopIndex());
    return windowTopIndexForContentY(
        contentYForWindowTopIndex(columnTop));
}
