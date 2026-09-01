#include "MasonryLayout.h"
#include "GalleryPixelGrid.h"

#include <ZoinGallery/GalleryCatalogSource.h>

#include <QQuickWindow>
#include <QSettings>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

bool propertyModelRequestsViewStatePreservation(QAbstractItemModel *model) {
    const auto *requestModel =
        dynamic_cast<const ZoinGallery::GalleryCatalogSource *>(model);
    return requestModel && requestModel->preserveViewStateOnReset();
}

} // namespace

void MasonryLayout::zoom(bool in) {
    if (_presentationMode == Columns || _presentationMode == Details) {
        const qreal previousDensity = _density;
        setDensity(_density + (in ? 2.0 : -2.0));
        if (!qFuzzyCompare(previousDensity, _density)) {
            reReadAndDecodeThumbnails();
        }
        return;
    }
    if (_presentationMode == Grid || _presentationMode == Icons) {
        const qreal canvasWidth = qMax<qreal>(
            1.0, width() - _paddingLeft - _paddingRight);
        const int currentColumns = effectiveColumnCount();
        const int targetColumns = in ? currentColumns - 1
                                     : currentColumns + 1;
        if (targetColumns < 1) {
            return;
        }

        // The interval for exactly N cells is
        // (canvasWidth / (N + 1), canvasWidth / N].  Pick its midpoint so
        // tiny floating-point differences cannot land on a boundary and make
        // an action keep the same number of visible cells.
        const qreal targetDensity = normalizedDensity(
            _presentationMode, canvasWidth /
            (static_cast<qreal>(targetColumns) + 0.5));
        const int resultingColumns = qMax(1, static_cast<int>(std::floor(
            canvasWidth / qMax<qreal>(1.0, targetDensity))));
        if (resultingColumns != targetColumns) {
            // The min/max density range can make the next cell count
            // unreachable (for example one cell at the maximum grid size).
            // A zoom action must then be a true no-op rather than silently
            // changing the pixel density while retaining the same lattice.
            return;
        }
        const qreal previousDensity = _density;
        setDensity(targetDensity);
        if (!qFuzzyCompare(previousDensity, _density)) {
            reReadAndDecodeThumbnails();
        }
        return;
    }
    const int smallestHeight = 30;
    const int largestHeight = 500;

    QList<MasonryBrick> bricks;
    QSize minSize = QSize(_targetHeight * GridView_Folder.width() / GridView_Folder.height(), _targetHeight);
    for (int i = 0; i <= ((width() - _paddingLeft - _paddingRight) / minSize.width()) * 2; i++) {
        bricks.append(MasonryBrick {
            .originalSize = GridView_Folder.toSize(),
        });
    }
    int columns = -1;
    int newTargetHeight = -1;
    int increment = in ? 1 : -1;

    int targetHeightRangeStart = -1;
    int targetHeightRangeEnd = -1;

    for (int targetHeight = _targetHeight - _paddingBottom; targetHeight >= smallestHeight && targetHeight <= largestHeight; targetHeight += increment) {
        calcLayout(bricks, width() - _paddingLeft - _paddingRight, targetHeight, _spacing, !_listView, _paddingTop, layoutMode());
        for (int i = 0; i < bricks.size(); i++) {
            if (bricks[i].row && i) {
                if (columns == -1) {
                    columns = bricks[i - 1].column;
                }
                else if (bricks[i - 1].column != columns) {
                    columns = bricks[i - 1].column;
                    if (targetHeightRangeStart == -1) {
                        targetHeightRangeStart = targetHeight;
                    }
                    else {
                        targetHeightRangeEnd = targetHeight;
                        newTargetHeight = (targetHeightRangeStart + targetHeightRangeEnd) / 2;
                    }
                }
                break;
            }
        }
        if (newTargetHeight != -1) {
            break;
        }
    }
    if (newTargetHeight != -1 || (_targetHeight != largestHeight && in) || (_targetHeight != smallestHeight && !in)) {
        setTargetHeight((newTargetHeight != -1 ? newTargetHeight : (in ? largestHeight : smallestHeight)) + _paddingBottom);
        reReadAndDecodeThumbnails();
    }
}

void MasonryLayout::updateNeedScroll() {
    // Sparse fixed layouts intentionally leave _bricks empty: only the
    // viewport-sized facade window is materialized. Their overflow is defined
    // by the model's logical row count and analytic content extent.
    if (logicalBrickCount() <= 0) {
        if (_needScroll) {
            _needScroll = false;
            emit needScrollChanged();
        }
        return;
    }
    if (_presentationMode != Masonry || sparseVirtualLayout()) {
        const bool newNeedScroll = _contentHeight > viewportExtent();
        if (newNeedScroll != _needScroll && height() > 0) {
            _needScroll = newNeedScroll;
            emit needScrollChanged();
        }
        return;
    }
    // Variable-height Masonry cannot derive overflow from placeholder rows.
    // A sparse catalog may therefore have a logical count before it has any
    // concrete bricks; retain the old no-scroll state until geometry exists.
    if (_bricks.isEmpty()) {
        if (_needScroll) {
            _needScroll = false;
            emit needScrollChanged();
        }
        return;
    }
    // Legacy nested folder-preview Masonry layouts never own a scroll surface.
    // Fixed presentation strategies, however, are also used by the reusable
    // external panel and must expose their real viewport overflow.
    if (isEmbedded()) {
        return;
    }

    bool newNeedScroll = _contentHeight > height();
    if (newNeedScroll != _needScroll && height() > 0) {
        QList<MasonryBrick> bricks = _bricks;
        // TODO: Scrollbar height is hardcoded here
        calcLayout(bricks, width() - _paddingLeft - _paddingRight + (newNeedScroll ? 0 : 16), _targetHeight, _spacing,
                   !_listView, _paddingTop, layoutMode());

        const int newContentHeight = static_cast<int>(
            bricks.last().y + bricks.last().normalizedSize.height()
            + _paddingBottom);
        newNeedScroll = newContentHeight > height();
        if (newNeedScroll != _needScroll && newContentHeight > 0) {
            _needScroll = newNeedScroll;
            emit needScrollChanged();
        }
    }
}

void MasonryLayout::pushBrickItem(BrickItem *item) {
    _delegatePool.release(item);
}

BrickItem *MasonryLayout::popBrickItem(
    int viewIndex, const QVariantMap &initialProperties) {
    Q_UNUSED(viewIndex)
    // A delegate is a viewport slot, not a cached row or presentation. Reuse
    // any free slot and bind its final mode/row in updateProperties(). This
    // keeps allocation bounded by the largest live viewport rather than the
    // number of visited modes.
    return _delegatePool.acquire(
        reinterpret_cast<quintptr>(delegateComponent(_presentationMode)),
        [this, &initialProperties]() {
            return createComponent(_presentationMode,
                                   initialProperties);
        });
}

void MasonryLayout::trimFreeDelegatePool() {
    // updateProperties() has already claimed every slot required by the
    // active viewport. Anything left is surplus from a larger previous
    // viewport/mode and must not become a hidden panel cache.
    _delegatePool.trimSurplus();
}

QSize MasonryLayout::dp(QSizeF value) {
    return QSize(dp(value.width()), dp(value.height()));
}

qreal MasonryLayout::dp(qreal value) {
    return qRound(value * dpValue());
}

qreal MasonryLayout::dpValue() {
    if (_devicePixelRatioOverride > 0) {
        _dp = _devicePixelRatioOverride;
    }
    else if (QWindow *window_ = window()) {
        _dp = window_->devicePixelRatio();
    }
    return _dp;
}

qreal MasonryLayout::devicePixelRatio() const {
    return _devicePixelRatioOverride > 0
        ? _devicePixelRatioOverride : _dp;
}

void MasonryLayout::setDevicePixelRatio(qreal value) {
    const qreal normalized = value > 0 ? value : 0;
    if (qFuzzyCompare(_devicePixelRatioOverride, normalized)) {
        return;
    }
    _devicePixelRatioOverride = normalized;
    emit devicePixelRatioChanged();
    if (_presentationMode == Columns) {
        // Columns geometry is expressed on the device-pixel lattice. Moving
        // the window between screens therefore changes its logical stride.
        rewrap(false);
    }
    reReadAndDecodeThumbnails();
}

QFont MasonryLayout::iconLabelFont() const {
    return _iconLabelFont;
}

void MasonryLayout::setIconLabelFont(const QFont &font) {
    if (_iconLabelFont == font) {
        return;
    }
    _iconLabelFont = font;
    emit iconLabelFontChanged();
    if (_presentationMode == Icons) {
        rewrap(false);
    }
}

bool MasonryLayout::deferDelegateRefreshOnReset() const {
    return _deferDelegateRefreshOnReset;
}

void MasonryLayout::setDeferDelegateRefreshOnReset(bool defer) {
    if (_deferDelegateRefreshOnReset == defer) {
        return;
    }
    _deferDelegateRefreshOnReset = defer;
    emit deferDelegateRefreshOnResetChanged();
}

MasonryLayout::PresentationMode MasonryLayout::presentationMode() const {
    return _presentationMode;
}

void MasonryLayout::requestRewrap(bool animate) {
    if (_layoutUpdateDepth > 0) {
        _layoutUpdateNeedsRewrap = true;
        // One non-animated participant makes the whole atomic commit
        // non-animated. Presentation switches always take this path.
        _layoutUpdateAnimate = _layoutUpdateAnimate && animate;
        return;
    }
    rewrap(animate);
}

void MasonryLayout::capturePresentationViewportAnchor(
    qreal *viewportY, bool *wasVisible) const {
    if (!viewportY || !wasVisible) {
        return;
    }
    *viewportY = 0;
    *wasVisible = false;
    if (_currentIndex < 0 || _currentIndex >= logicalBrickCount()) {
        return;
    }
    const QRectF geometry = indexGeometry(_currentIndex);
    *wasVisible = geometry.isValid() && !geometry.isEmpty()
        && geometry.top() < _contentY + height()
        && geometry.bottom() > _contentY;
    if (*wasVisible) {
        *viewportY = geometry.top() - _contentY;
    }
}

void MasonryLayout::prepareViewportForModelReset(
    int cursorIndex, qreal savedOffset, bool restoreSavedOffset) {
    _preparedResetViewportPending = true;
    _preparedResetViewportRestoresOffset = restoreSavedOffset;
    _preparedResetCursorIndex = cursorIndex;
    _preparedResetScrollOffset = qMax<qreal>(0, savedOffset);
}

void MasonryLayout::beginLayoutUpdate() {
    if (_layoutUpdateDepth++ > 0) {
        return;
    }
    _layoutUpdateNeedsRewrap = false;
    _layoutUpdateAnimate = true;
    _layoutUpdateNeedsPositionViewport = false;
    _layoutUpdateNeedsScrollRefresh = false;
    _layoutUpdatePresentationModeChanged = false;
    _layoutUpdateModelResetSnapshotRefresh = false;
    capturePresentationViewportAnchor(
        &_layoutUpdateCurrentViewportY,
        &_layoutUpdateCurrentWasVisible);
}

void MasonryLayout::endLayoutUpdate() {
    if (_layoutUpdateDepth <= 0) {
        return;
    }
    if (--_layoutUpdateDepth > 0) {
        return;
    }

    const bool needsRewrap = _layoutUpdateNeedsRewrap;
    const bool animate = _layoutUpdateAnimate;
    const bool needsPositionViewport =
        _layoutUpdateNeedsPositionViewport;
    const bool needsScrollRefresh = _layoutUpdateNeedsScrollRefresh;
    const bool presentationModeChanged =
        _layoutUpdatePresentationModeChanged;
    const bool modelResetSnapshotRefresh =
        _layoutUpdateModelResetSnapshotRefresh;
    const qreal previousCurrentViewportY =
        _layoutUpdateCurrentViewportY;
    const bool previousCurrentWasVisible =
        _layoutUpdateCurrentWasVisible;

    _layoutUpdateNeedsRewrap = false;
    _layoutUpdateAnimate = true;
    _layoutUpdateNeedsPositionViewport = false;
    _layoutUpdateNeedsScrollRefresh = false;
    _layoutUpdatePresentationModeChanged = false;
    _layoutUpdateModelResetSnapshotRefresh = false;

    if (needsPositionViewport) {
        positionViewport();
    }
    if (presentationModeChanged) {
        // setDensity() may have requested a same-mode viewport anchor after
        // setPresentationMode(). A cross-mode switch owns the stronger
        // current-item viewport contract below.
        _preserveViewportAnchorForNextRewrap = false;
        completePresentationModeChange(previousCurrentViewportY,
                                       previousCurrentWasVisible);
    }
    else if (needsRewrap) {
        rewrap(animate);
    }
    if (modelResetSnapshotRefresh) {
        _visualSnapshotRefresh = false;
        scheduleDeferredDelegateMaterialization();
        update();
    }
    if (needsScrollRefresh) {
        updateNeedScroll();
    }
}

void MasonryLayout::completePresentationModeChange(
    qreal previousCurrentViewportY, bool previousCurrentWasVisible) {
    if (_model && (_presentationMode == Details
                   || (_presentationMode != Masonry && !isEmbedded()))) {
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model)
            ->cancelAllDecodeRunners();
    }
    const qreal previousContentY = _contentY;
    Q_ASSERT(!_deferDelegateWindowCommit);
    _deferDelegateWindowCommit = true;
    rewrap(false);

    // A raw pixel contentY has no semantic meaning across presentation modes:
    // Icons and Grid, for example, use different row pitches and column
    // counts. Preserve the current item's viewport-space Y when possible,
    // then clamp only as much as needed to keep the whole new cell visible.
    // This happens synchronously so the first rendered frame is valid.
    if (_currentIndex >= 0 && _currentIndex < logicalBrickCount()) {
        const QRectF currentGeometry = indexGeometry(_currentIndex);
        if (currentGeometry.isValid() && !currentGeometry.isEmpty()) {
            qreal targetY = _contentY;
            if (_presentationMode == Columns) {
                if (currentGeometry.left() < targetY) {
                    targetY = currentGeometry.left();
                }
                else if (currentGeometry.right() > targetY + width()) {
                    targetY = currentGeometry.right() - width();
                }
            }
            else if (previousCurrentWasVisible) {
                const qreal desired = currentGeometry.top()
                    - previousCurrentViewportY;
                const qreal minimumForVisibility =
                    currentGeometry.bottom() - height();
                targetY = qBound(
                    qMin(currentGeometry.top(), minimumForVisibility),
                    desired,
                    qMax(currentGeometry.top(), minimumForVisibility));
            }
            else {
                if (currentGeometry.top() < targetY) {
                    targetY = currentGeometry.top();
                }
                else if (currentGeometry.bottom() > targetY + height()) {
                    targetY = currentGeometry.bottom() - height();
                }
            }
            targetY = qBound<qreal>(
                0, targetY, maximumContentOffset());
            if (!qFuzzyCompare(targetY + 1, _contentY + 1)) {
                setContentY(targetY);
            }
        }
    }
    _deferDelegateWindowCommit = false;
    // rewrap() must first compute target-mode geometry before the semantic
    // cursor anchor can be converted into contentY. Populate delegates only
    // now, at that final offset, instead of painting and discarding a viewport
    // addressed by the outgoing mode's unrelated numeric scroll coordinate.
    _presentationModeCommitInProgress = true;
    updateProperties(false);
    _presentationModeCommitInProgress = false;
    updateNeedScroll();
    if (!qFuzzyCompare(previousContentY + 1, _contentY + 1)) {
        emit contentYChanged();
    }
    if (_presentationMode == Masonry) {
        reReadAndDecodeThumbnails();
    }
    else {
        emit layoutReset();
        planViewportThumbnails(_overscanIndexSet, true);
    }
}

void MasonryLayout::setPresentationMode(PresentationMode mode) {
    const int normalizedValue = qBound(
        static_cast<int>(Masonry), static_cast<int>(mode),
        static_cast<int>(Icons));
    mode = static_cast<PresentationMode>(normalizedValue);
    if (_presentationMode == mode) {
        return;
    }
    qreal previousCurrentViewportY = _layoutUpdateCurrentViewportY;
    bool previousCurrentWasVisible = _layoutUpdateCurrentWasVisible;
    if (_layoutUpdateDepth == 0) {
        capturePresentationViewportAnchor(
            &previousCurrentViewportY, &previousCurrentWasVisible);
    }
    _presentationMode = mode;
    _density = _modeDensities[normalizedValue];
    _targetHeight = qRound(_density);
    _currentLoadingRow.clear();
    _thumbnailPlanner.reset();
    _windowTopIndex = qBound(
        0, _topItem, maximumWindowTopIndex());
    if (_presentationMode == Columns && _currentIndex >= 0) {
        _windowTopIndex = windowTopIndexForIndex(_currentIndex);
    }
    if (_persistSettings && !isEmbedded()) {
        QSettings settings;
        settings.setValue(QStringLiteral("layout/presentationMode"),
                          normalizedValue);
    }
    emit presentationModeChanged();
    emit densityChanged();
    emit targetExtentChanged();
    emit targetHeightChanged();
    if (_layoutUpdateDepth > 0) {
        _layoutUpdatePresentationModeChanged = true;
        requestRewrap(false);
        return;
    }
    completePresentationModeChange(previousCurrentViewportY,
                                   previousCurrentWasVisible);
}

int MasonryLayout::columnCount() const {
    return _columnCount;
}

void MasonryLayout::setColumnCount(int columnCount) {
    columnCount = qBound(2, columnCount, 3);
    if (_columnCount == columnCount) {
        return;
    }
    _columnCount = columnCount;
    if (_persistSettings && !isEmbedded()) {
        QSettings settings;
        settings.setValue(QStringLiteral("layout/columnCount"),
                          _columnCount);
    }
    emit columnCountChanged();
    if (_presentationMode == Columns) {
        _windowTopIndex = qBound(
            0, _windowTopIndex, maximumWindowTopIndex());
        requestRewrap(false);
    }
}

qreal MasonryLayout::density() const {
    return _density;
}

void MasonryLayout::setDensity(qreal density) {
    density = normalizedDensity(_presentationMode, density);
    if (qFuzzyCompare(_density, density)) {
        return;
    }
    _density = density;
    _modeDensities[static_cast<int>(_presentationMode)] = density;
    if (_presentationMode != Masonry) {
        // Density is a user-facing zoom operation. Preserve the old layout's
        // semantic viewport anchor for exactly the rewrap below; unrelated
        // initialization, presentation and model rewraps must not resurrect
        // a stale viewport position.
        _preserveViewportAnchorForNextRewrap = true;
    }
    const int previousTargetHeight = _targetHeight;
    _targetHeight = qRound(density);
    if (_persistSettings && !isEmbedded()) {
        QSettings settings;
        const QString name = presentationModeSettingsName(_presentationMode);
        settings.setValue(QStringLiteral("layout/%1/density").arg(name),
                          _density);
        settings.setValue(
            QStringLiteral("layout/%1/targetExtent").arg(name),
            _targetHeight);
        if (_presentationMode == Masonry) {
            settings.setValue(QStringLiteral("targetHeight"),
                              _targetHeight);
        }
    }
    requestRewrap();
    emit densityChanged();
    emit targetExtentChanged();
    if (previousTargetHeight != _targetHeight) {
        emit targetHeightChanged();
    }
}

int MasonryLayout::targetExtent() const {
    return qRound(_density);
}

void MasonryLayout::setTargetExtent(int targetExtent) {
    setDensity(targetExtent);
}

int MasonryLayout::targetHeight() const {
    return _targetHeight;
}

void MasonryLayout::setTargetHeight(int newTargetHeight) {
    setDensity(newTargetHeight);
}

int MasonryLayout::windowTopIndex() const {
    return _windowTopIndex;
}

void MasonryLayout::setWindowTopIndex(int index) {
    index = qBound(0, index, maximumWindowTopIndex());
    if (_windowTopIndex == index) {
        return;
    }
    const qreal oldContentY = _contentY;
    _windowTopIndex = index;
    _contentY = contentYForWindowTopIndex(index);
    // An explicit window jump is authoritative.  Keep the semantic anchor in
    // sync before rewrap(), otherwise a stale anchor from the previous
    // horizontal viewport can immediately restore the old column.
    _topItem = _windowTopIndex;
    _topItemOffset = _topItem >= 0 && _topItem < logicalBrickCount()
        ? indexGeometry(_topItem).x() - _contentY : 0;
    emit windowTopIndexChanged();
    rewrap(false);
    if (!qFuzzyCompare(oldContentY, _contentY)) {
        emit contentYChanged();
    }
}

QVariantList MasonryLayout::visibleIndexes() const {
    QList<int> indexes = _visibleIndexSet.values();
    std::sort(indexes.begin(), indexes.end());
    QVariantList result;
    result.reserve(indexes.size());
    for (const int index : std::as_const(indexes)) {
        result.append(index);
    }
    return result;
}

QVariantList MasonryLayout::overscanIndexes() const {
    QList<int> indexes = _overscanIndexSet.values();
    std::sort(indexes.begin(), indexes.end());
    QVariantList result;
    result.reserve(indexes.size());
    for (const int index : std::as_const(indexes)) {
        result.append(index);
    }
    return result;
}

QVariantList MasonryLayout::layoutBands() const {
    QVariantList result;
    result.reserve(_layoutBands.size());
    for (int bandIndex = 0; bandIndex < _layoutBands.size(); ++bandIndex) {
        const LayoutBand &band = _layoutBands.at(bandIndex);
        QVariantList indexes;
        indexes.reserve(band.indexes.size());
        for (const int index : band.indexes) {
            indexes.append(index);
        }
        result.append(QVariantMap{
            {QStringLiteral("bandIndex"), bandIndex},
            {QStringLiteral("row"), band.row},
            {QStringLiteral("top"), band.top},
            {QStringLiteral("bottom"), band.bottom},
            {QStringLiteral("indexes"), indexes},
        });
    }
    return result;
}

quint64 MasonryLayout::layoutRevision() const {
    return _layoutRevision;
}

quint64 MasonryLayout::delegateCommitRevision() const {
    return _delegateCommitRevision;
}

qreal MasonryLayout::contentY() const {
    return _contentY;
}

void MasonryLayout::setContentY(qreal newContentY) {
    newContentY = qBound<qreal>(0, newContentY, maximumContentOffset());
    if (_presentationMode == Columns) {
        const int nextTop = windowTopIndexForContentY(newContentY);
        if (nextTop != _windowTopIndex) {
            _windowTopIndex = nextTop;
            emit windowTopIndexChanged();
        }
        setContentYInternal(newContentY);
        _topItem = _windowTopIndex;
        _topItemOffset = _topItem >= 0 && _topItem < logicalBrickCount()
            ? indexGeometry(_topItem).x() - _contentY : 0;
        return;
    }
    setContentYInternal(newContentY);
    constexpr qreal edgeEpsilon = 0.001;
    const qreal probe = qMax(_paddingTop + edgeEpsilon,
                             _contentY + edgeEpsilon);
    const QList<int> leadingIndexes = indexesForVerticalRange(probe, probe);
    if (!leadingIndexes.isEmpty()) {
        _topItem = leadingIndexes.constFirst();
    }
    if (_topItem != -1 && _topItem < logicalBrickCount()) {
        _topItemOffset = indexGeometry(_topItem).y() - _contentY;
//        qDebug() << "set contentY" << newContentY << "top item" << _topItem << "offset" << _topItemOffset;
    }
//    _topItem = _visibleStart;
//    setCurrentIndex(_topItem);
}

void MasonryLayout::setContentYInternal(qreal newContentY) {
    static int depth = 0;
    depth++;
    if (qFuzzyCompare(_contentY, newContentY) || depth > 1) {
//        qDebug() << "skip";
        depth--;
        return;
    }
//    qDebug() << "contentY" << _contentY << "->" << newContentY;
    _contentY = newContentY;
    positionViewport();

    if (!_deferDelegateWindowCommit) {
        updateProperties();
        emit contentYChanged();
    }
    depth--;
}

qreal MasonryLayout::contentHeight() const {
    return _contentHeight;
}

QRectF MasonryLayout::MasonryBrick::geometry() const {
    return QRectF(QPointF(x, y), normalizedSize);
}

QSize MasonryLayout::MasonryBrick::thumbnailSize(int spacing) const {
    if (previewGeometry.isValid() && !previewGeometry.isEmpty()) {
        return ZoinGallery::PixelGrid::snapLogicalRect(
            previewGeometry).toRect().size();
    }
    return ZoinGallery::PixelGrid::snapLogicalRect(
        geometry()).toRect().size() - QSize(spacing, spacing);
}

QAbstractItemModel *MasonryLayout::model() const {
    return _model;
}

void MasonryLayout::setModel(QAbstractItemModel *newModel) {
    if (_model == newModel) {
        return;
    }
    if (_model) {
        disconnect(_model, nullptr, this, nullptr);
    }
    _model = newModel;
    updateModelRoleCache();
    if (_model) {
        connect(_model, &QAbstractItemModel::dataChanged,
                this, &MasonryLayout::onDataChanged);
        connect(_model, &QAbstractItemModel::modelAboutToBeReset,
                this, [this]() {
            const bool preserveReset =
                propertyModelRequestsViewStatePreservation(_model);
            _preserveDecodeQueueForCurrentRebuild = preserveReset;
            if (preserveReset) {
                preserveCurrentItemPositionForNextModelReset();
                preservePendingThumbnailRequestsForModelReset();
            }
            else {
                _preservedPendingThumbnailInfo.clear();
                _skipThumbnailBackfillUntilFlush = false;
            }
            onModelAboutToBeReset();
        });
        connect(_model, &QAbstractItemModel::modelReset,
                this, &MasonryLayout::onModelReset);
        connect(_model, &QAbstractItemModel::layoutAboutToBeChanged,
                this, [this]() {
            prepareForIncrementalModelChange();
        });
        connect(_model, &QAbstractItemModel::layoutChanged,
                this, [this]() { applyIncrementalModelChange(); });
        connect(_model, &QAbstractItemModel::rowsAboutToBeInserted,
                this, [this](const QModelIndex &parent, int first,
                             int last) {
            if (parent.isValid()) {
                return;
            }
            if (propertyModelRequestsViewStatePreservation(_model)) {
                // The metadata for a watcher-added row arrives after this
                // signal. Do not rebuild the old loading row from index zero;
                // request that new row directly when its dimensions arrive.
                _skipThumbnailBackfillUntilFlush = true;
            }
            prepareForIncrementalModelChange(first, last);
        });
        connect(_model, &QAbstractItemModel::rowsInserted,
                this, [this](const QModelIndex &parent) {
            if (!parent.isValid()) {
                applyIncrementalModelChange();
            }
        });
        connect(_model, &QAbstractItemModel::rowsAboutToBeRemoved,
                this, [this](const QModelIndex &parent) {
            if (parent.isValid()) {
                return;
            }
            prepareForIncrementalModelChange();
        });
        connect(_model, &QAbstractItemModel::rowsRemoved,
                this, [this](const QModelIndex &parent) {
            if (!parent.isValid()) {
                applyIncrementalModelChange();
            }
        });
        connect(_model, &QAbstractItemModel::rowsAboutToBeMoved,
                this, [this](const QModelIndex &sourceParent, int, int,
                             const QModelIndex &destinationParent, int) {
            if (!sourceParent.isValid() && !destinationParent.isValid()) {
                prepareForIncrementalModelChange();
            }
        });
        connect(_model, &QAbstractItemModel::rowsMoved,
                this, [this](const QModelIndex &sourceParent, int, int,
                             const QModelIndex &destinationParent, int) {
            if (!sourceParent.isValid() && !destinationParent.isValid()) {
                applyIncrementalModelChange();
            }
        });
    }

    onModelAboutToBeReset();
    onModelReset();
}

int MasonryLayout::currentIndex() const {
    return _currentIndex;
}

void MasonryLayout::updateCurrentImageIndex() {
    _currentImageIndex = 0;
    if (_sparseCatalogRows) {
        for (const int row : materializedModelRows()) {
            if (row >= _currentIndex) {
                break;
            }
            if (brickIsImage(row)) {
                ++_currentImageIndex;
            }
        }
    }
    else {
        for (int i = 0; i < _currentIndex && i < _bricks.size(); i++) {
            if (brickIsImage(i)) {
                ++_currentImageIndex;
            }
        }
    }
    emit currentImageIndexChanged();
}

void MasonryLayout::setCurrentIndex(int newCurrentIndex) {
    const int rowCount = _model ? _model->rowCount() : 0;
    newCurrentIndex = rowCount > 0
        ? qMin(qMax(0, newCurrentIndex), rowCount - 1)
        : -1;
    if (_currentIndex == newCurrentIndex) {
        return;
    }
    _currentIndex = newCurrentIndex;
    emit currentIndexChanged();

    updateCurrentImageIndex();

    if (_quickSearch && !_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }
}

int MasonryLayout::spacing() const {
    return _spacing;
}

void MasonryLayout::setSpacing(int newSpacing) {
    if (_spacing == newSpacing)
        return;
    _spacing = newSpacing;
    emit spacingChanged();
    requestRewrap();
}

QQuickItem *MasonryLayout::currentItem() const {
    const MasonryBrick *brick = brickAt(_currentIndex);
    return brick ? brick->item : nullptr;
}

int MasonryLayout::count() const {
    if (_model) {
        return _model->rowCount();
    }
    return 0;
}

MasonryLayoutQuickSearch *MasonryLayout::quickSearch() const {
    return _quickSearch;
}

bool MasonryLayout::needScroll() const {
    return _needScroll;
}

bool MasonryLayout::listView() const {
    return _listView;
}

void MasonryLayout::setListView(bool isListView) {
    if (_listView == isListView) {
        return;
    }
    _listView = isListView;
    if (_persistSettings) {
        QSettings set;
        set.setValue("listView", isListView);
    }

    for (int i = 0; i < _bricks.size(); i++) {
        if (brickIsFolder(i)) {
            if (!_listView) {
                _bricks[i].originalSize = GridView_Folder;
                _bricks[i].lineBreakAfter = false;
            }
            else {
                const bool folderView = _bricks[i].image
                    && _bricks[i].image->folderView();
                _bricks[i].originalSize = QSize(
                    0, folderView ? 0 : listRowHeight());
                _bricks[i].lineBreakAfter = true;
            }
        }
    }
    requestRewrap();

    emit listViewChanged();
}

int MasonryLayout::imageCount() const {
    return _imageCount;
}

int MasonryLayout::currentImageIndex() const {
    return _currentImageIndex;
}

void MasonryLayout::setCurrentImageIndex(int newCurrentImageIndex) {
    if (_currentImageIndex == newCurrentImageIndex) {
        return;
    }
    for (int i = 0, imageIndex = 0; i < _bricks.size(); i++) {
        if (brickIsImage(i)) {
            if (imageIndex == newCurrentImageIndex) {
                setCurrentIndex(i);
                break;
            }
            imageIndex++;
        }
    }
}

bool MasonryLayout::showTransparentGrid() const {
    return _showTransparentGrid;
}

void MasonryLayout::setShowTransparentGrid(bool newShowTransparentGrid) {
    if (_showTransparentGrid == newShowTransparentGrid) {
        return;
    }

    _showTransparentGrid = newShowTransparentGrid;

    if (_persistSettings) {
        QSettings set;
        set.setValue("showTransparentGrid", _showTransparentGrid);
    }

    emit showTransparentGridChanged();
}

bool MasonryLayout::animateResizing() const {
    return _animateResizing;
}

void MasonryLayout::setAnimateResizing(bool newAnimateResizing) {
    if (_animateResizing == newAnimateResizing) {
        return;
    }

    _animateResizing = newAnimateResizing;

    if (_persistSettings) {
        QSettings set;
        set.setValue("animateResizing", _animateResizing);
    }

    emit animateResizingChanged();
}

qreal MasonryLayout::paddingLeft() const {
    return _paddingLeft;
}

void MasonryLayout::setPaddingLeft(qreal newPaddingLeft) {
    if (qFuzzyCompare(_paddingLeft, newPaddingLeft))
        return;
    _paddingLeft = newPaddingLeft;
    if (_layoutUpdateDepth > 0) {
        _layoutUpdateNeedsPositionViewport = true;
    }
    else {
        positionViewport();
    }
    requestRewrap(false);
    emit paddingLeftChanged();
}

qreal MasonryLayout::paddingRight() const {
    return _paddingRight;
}

void MasonryLayout::setPaddingRight(qreal newPaddingRight) {
    if (qFuzzyCompare(_paddingRight, newPaddingRight))
        return;
    _paddingRight = newPaddingRight;
    requestRewrap(false);
    emit paddingRightChanged();
}

qreal MasonryLayout::paddingTop() const {
    return _paddingTop;
}

void MasonryLayout::setPaddingTop(qreal newPaddingTop) {
    if (qFuzzyCompare(_paddingTop, newPaddingTop))
        return;

    _paddingTop = newPaddingTop;
    _topItemOffset = _paddingTop;
    requestRewrap(false);
    emit paddingTopChanged();
}

qreal MasonryLayout::paddingBottom() const {
    return _paddingBottom;
}

void MasonryLayout::setPaddingBottom(qreal newPaddingBottom) {
    if (qFuzzyCompare(_paddingBottom, newPaddingBottom))
        return;
    _paddingBottom = newPaddingBottom;
    requestRewrap(false);
    emit paddingBottomChanged();
}

qreal MasonryLayout::width() const {
    return qMax(0.0, qIsInf(QQuickItem::width()) ? 0 : QQuickItem::width());
}

int MasonryLayout::listRowHeight() const {
    return _listRowHeight;
}

QVariantList MasonryLayout::currentImageExif() const {
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(_currentIndex);
        return image ? image->exifList() : QVariantList();
    }
    return QVariantList();
}

QQuickItem *MasonryLayout::viewport() const {
    return _viewport;
}
