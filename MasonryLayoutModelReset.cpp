#include "MasonryLayout.h"
#include "FileListModel.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QElapsedTimer>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr char DelegateVisualRevisionProperty[] =
    "_zoinGalleryVisualRevision";

ImageFile *imageFileFromResetIndex(const QModelIndex &index) {
    return index.data(FileListModel::ImageFileRole).value<ImageFile *>();
}

} // namespace

void MasonryLayout::updateModelRoleCache() {
    _entryIdRole = -1;
    _sourceIndexRole = -1;
    _localPathRole = -1;
    _entryNameRole = -1;
    _knownImageSizeRole = -1;
    _visualSnapshotRole = -1;
    if (!_model) {
        return;
    }
    const QHash<int, QByteArray> roles = _model->roleNames();
    for (auto role = roles.cbegin(); role != roles.cend(); ++role) {
        if (role.value() == QByteArrayLiteral("entryId")) {
            _entryIdRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("sourceIndex")) {
            _sourceIndexRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("localPath")) {
            _localPathRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("entryName")
                 || role.value() == QByteArrayLiteral("name")) {
            _entryNameRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("knownImageSize")) {
            _knownImageSizeRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("visualSnapshot")) {
            _visualSnapshotRole = role.key();
        }
    }
}

QVariantMap MasonryLayout::visualSnapshotForIndex(int index) const {
    if (!_model || _visualSnapshotRole < 0 || index < 0
        || index >= _model->rowCount()) {
        return {};
    }
    return _model->index(index, 0).data(_visualSnapshotRole).toMap();
}

void MasonryLayout::updateVisualSnapshotForIndex(int index) {
    if (_visualSnapshotRole < 0 || index < 0 ||
        index >= logicalBrickCount()) {
        return;
    }
    MasonryBrick *brick = brickAt(index);
    if (!brick) {
        return;
    }
    BrickItem *item = brick->item;
    if (!item) {
        return;
    }
    item->setVisualRow(visualSnapshotForIndex(index));
    item->setProperty(
        DelegateVisualRevisionProperty,
        QVariant::fromValue(brick->modelVisualRevision));
}

bool MasonryLayout::canUseLightweightRows() const {
    // External catalogs carry enough POD role data to build every strategy's
    // first geometry. Unknown image dimensions deliberately use the same 1:1
    // placeholder as a freshly-created ImageFile; metadata later updates the
    // cheap size role and rewraps the affected rows.
    return _model
        && _entryIdRole >= 0 && _sourceIndexRole >= 0
        && _localPathRole >= 0 && _entryNameRole >= 0
        && _knownImageSizeRole >= 0;
}

MasonryLayout::MasonryBrick MasonryLayout::lightweightBrickForModelRow(
    int row) const {
    MasonryBrick brick;
    populateBrickModelState(brick, row);
    QSize imageSize = brick.modelKnownSize;
    if (imageSize.isEmpty()) {
        if (brick.modelIsFolder && _listView) {
            brick.lineBreakAfter = true;
            imageSize = QSize(0, listRowHeight());
        }
        else {
            imageSize = GridView_Folder.toSize();
        }
    }
    brick.originalSize = imageSize;
    return brick;
}

void MasonryLayout::populateBrickModelState(
    MasonryBrick &brick, int row) const {
    if (!_model || row < 0 || row >= _model->rowCount()) {
        return;
    }
    const QModelIndex modelIndex = _model->index(row, 0);
    brick.modelIsImage = modelIndex.data(
        FileListModel::IsImageRole).toBool();
    brick.modelIsFolder = modelIndex.data(
        FileListModel::FolderRole).toBool();
    if (_entryIdRole >= 0) {
        brick.modelIdentity = modelIndex.data(_entryIdRole).toString();
    }
    if (_sourceIndexRole >= 0) {
        brick.modelSourceIndex = modelIndex.data(_sourceIndexRole).toInt();
    }
    if (_localPathRole >= 0) {
        brick.modelPath = modelIndex.data(_localPathRole).toString();
    }
    if (_entryNameRole >= 0) {
        brick.modelText = modelIndex.data(_entryNameRole).toString();
    }
    if (_knownImageSizeRole >= 0) {
        brick.modelKnownSize = modelIndex.data(
            _knownImageSizeRole).toSize();
    }
    if (brick.image) {
        brick.modelIsImage = brick.image->isImage();
        brick.modelIsFolder = brick.image->isFolder();
        brick.modelSourceIndex = brick.image->index();
        if (brick.modelPath.isEmpty()) {
            brick.modelPath = brick.image->fullPath();
        }
        if (brick.modelIdentity.isEmpty()) {
            brick.modelIdentity = brick.modelPath;
        }
        if (brick.modelText.isEmpty()) {
            brick.modelText = brick.image->text();
        }
        if (brick.image->fullSize().isValid()) {
            brick.modelKnownSize = brick.image->fullSize();
        }
    }
}

ImageFile *MasonryLayout::materializeImageForIndex(int index) {
    if (!_model || index < 0 || index >= logicalBrickCount()
        || index >= _model->rowCount()) {
        return nullptr;
    }
    MasonryBrick &brick = ensureBrickAt(index);
    if (!brick.image) {
        brick.image = imageFileFromResetIndex(_model->index(index, 0));
        if (brick.image) {
            brick.modelIsImage = brick.image->isImage();
            brick.modelIsFolder = brick.image->isFolder();
            brick.modelSourceIndex = brick.image->index();
            if (brick.modelPath.isEmpty()) {
                brick.modelPath = brick.image->fullPath();
            }
            if (brick.modelIdentity.isEmpty()) {
                brick.modelIdentity = brick.modelPath;
            }
            if (brick.modelText.isEmpty()) {
                brick.modelText = brick.image->text();
            }
            if (brick.image->fullSize().isValid()) {
                brick.modelKnownSize = brick.image->fullSize();
            }
            if (_quickSearch) {
                _quickSearch->updateItemText(index);
            }
        }
    }
    return brick.image;
}

bool MasonryLayout::brickIsImage(int index) const {
    const MasonryBrick *brick = brickAt(index);
    if (brick) {
        return brick->image ? brick->image->isImage()
                            : brick->modelIsImage;
    }
    return _sparseCatalogRows && _model && index >= 0 &&
        index < _model->rowCount() &&
        _model->index(index, 0).data(FileListModel::IsImageRole).toBool();
}

bool MasonryLayout::brickIsFolder(int index) const {
    const MasonryBrick *brick = brickAt(index);
    if (brick) {
        return brick->image ? brick->image->isFolder()
                            : brick->modelIsFolder;
    }
    return _sparseCatalogRows && _model && index >= 0 &&
        index < _model->rowCount() &&
        _model->index(index, 0).data(FileListModel::FolderRole).toBool();
}

QString MasonryLayout::brickPath(int index) const {
    const MasonryBrick *brick = brickAt(index);
    if (brick) {
        return brick->image ? brick->image->fullPath()
                            : brick->modelPath;
    }
    if (_sparseCatalogRows && _model && _localPathRole >= 0 &&
        index >= 0 && index < _model->rowCount()) {
        return _model->index(index, 0).data(_localPathRole).toString();
    }
    return {};
}

void MasonryLayout::prepareForIncrementalModelChange(
    int insertedFirst, int insertedLast) {
    if (_incrementalModelChangeDepth++ > 0) {
        return;
    }
    _incrementalModelPreviousCount = logicalBrickCount();
    _incrementalInsertedFirst = insertedFirst;
    _incrementalInsertedLast = insertedLast;
    preserveCurrentItemPositionForNextModelReset();

    // Older loading-row entries did not retain their identity explicitly.
    // Fill it while their old indexes are still valid so the queue can be
    // remapped rather than canceled after an insert/remove/move.
    for (MasonryBrick &pendingBrick : _currentLoadingRow) {
        if (!pendingBrick.image && pendingBrick.globalIndex >= 0 &&
            pendingBrick.globalIndex < _bricks.size()) {
            pendingBrick.image = _bricks[pendingBrick.globalIndex].image;
        }
    }
}

bool MasonryLayout::applySparseTailInsert() {
    if (!_sparseCatalogRows || !_model
        || !_model->property("sparseCatalog").toBool()
        || _incrementalInsertedFirst != _incrementalModelPreviousCount
        || _incrementalInsertedLast != logicalBrickCount() - 1
        || logicalBrickCount() < _incrementalModelPreviousCount) {
        return false;
    }

    const int previousCount = _incrementalModelPreviousCount;
    const int previousCurrentIndex = _currentIndex;
    const QString previousCurrentPath =
        _preserveCurrentItemPositionOnNextModelReset
        ? _preservedCurrentItemFullPath : QString();

    _thumbnailPlanner.reset();
    restorePreservedCurrentItemPosition();
    const int count = logicalBrickCount();
    if (count <= 0) {
        _currentIndex = -1;
        _topItem = 0;
    } else if (_currentIndex < 0 || _currentIndex >= count) {
        _currentIndex = qBound(0, _currentIndex, count - 1);
    }

    requestRewrap(false);
    refreshImageCountFromCatalog();
    if (previousCount != count) {
        emit countChanged();
    }
    if (!_quickSearch->mask().isEmpty()) {
        // Sparse catalogs have no dense brick vector. This call only refreshes
        // already materialized facades and therefore remains viewport-bound.
        _quickSearch->updateItemsText();
    }
    const bool currentIdentityPreserved =
        !previousCurrentPath.isEmpty() && _currentIndex >= 0
        && _currentIndex < count
        && brickPath(_currentIndex) == previousCurrentPath;
    if (!currentIdentityPreserved
        || previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
    return true;
}

struct MasonryLayout::IncrementalModelChangeContext {
    int previousCount = 0;
    int previousCurrentIndex = -1;
    QString previousCurrentPath;
    QList<MasonryBrick> oldBricks;
    QHash<ImageFile *, int> oldIndexes;
    QHash<QString, int> oldIdentityIndexes;
    QList<bool> retained;
};

MasonryLayout::IncrementalModelChangeContext
MasonryLayout::beginIncrementalModelRebuild() {
    _thumbnailPlanner.reset();
    IncrementalModelChangeContext context;
    context.previousCount = _bricks.size();
    context.previousCurrentIndex = _currentIndex;
    context.previousCurrentPath =
        _preserveCurrentItemPositionOnNextModelReset
        ? _preservedCurrentItemFullPath : QString();
    context.oldBricks = std::move(_bricks);
    context.oldIndexes.reserve(context.oldBricks.size());
    context.oldIdentityIndexes.reserve(context.oldBricks.size());
    for (int index = 0; index < context.oldBricks.size(); ++index) {
        const MasonryBrick &brick = context.oldBricks.at(index);
        if (brick.image) {
            context.oldIndexes.insert(brick.image, index);
        }
        if (!brick.modelIdentity.isEmpty()) {
            context.oldIdentityIndexes.insert(brick.modelIdentity, index);
        }
    }
    context.retained = QList<bool>(context.oldBricks.size(), false);
    _bricks.clear();
    return context;
}

void MasonryLayout::rebuildIncrementalBricks(
    IncrementalModelChangeContext *context) {
    if (!_model) {
        return;
    }
    _bricks.reserve(_model->rowCount());
    const bool lightweightRows = canUseLightweightRows();
    for (int row = 0; row < _model->rowCount(); ++row) {
        if (lightweightRows) {
            const QString identity = _model->index(row, 0)
                .data(_entryIdRole).toString();
            const auto oldIt =
                context->oldIdentityIndexes.constFind(identity);
            if (identity.isEmpty()
                || oldIt == context->oldIdentityIndexes.constEnd()) {
                _bricks.append(lightweightBrickForModelRow(row));
                continue;
            }
            context->retained[*oldIt] = true;
            MasonryBrick brick =
                std::move(context->oldBricks[*oldIt]);
            const MasonryBrick modelState =
                lightweightBrickForModelRow(row);
            brick.originalSize = modelState.originalSize;
            brick.lineBreakAfter = modelState.lineBreakAfter;
            brick.modelIdentity = modelState.modelIdentity;
            brick.modelPath = modelState.modelPath;
            brick.modelText = modelState.modelText;
            brick.modelKnownSize = modelState.modelKnownSize;
            brick.modelSourceIndex = modelState.modelSourceIndex;
            brick.modelIsImage = modelState.modelIsImage;
            brick.modelIsFolder = modelState.modelIsFolder;
            _bricks.append(std::move(brick));
            continue;
        }
        ImageFile *imageFile =
            imageFileFromResetIndex(_model->index(row, 0));
        const auto oldIt = context->oldIndexes.constFind(imageFile);
        if (imageFile && oldIt != context->oldIndexes.constEnd()) {
            context->retained[*oldIt] = true;
            MasonryBrick brick =
                std::move(context->oldBricks[*oldIt]);
            populateBrickModelState(brick, row);
            _bricks.append(std::move(brick));
        } else if (imageFile) {
            MasonryBrick brick = brickForImage(imageFile);
            populateBrickModelState(brick, row);
            _bricks.append(std::move(brick));
        }
    }
}

void MasonryLayout::retireRemovedIncrementalBricks(
    IncrementalModelChangeContext *context) {
    for (int index = 0; index < context->oldBricks.size(); ++index) {
        MasonryBrick &brick = context->oldBricks[index];
        if (context->retained.at(index) || !brick.item) {
            continue;
        }
        BrickItem *item = brick.item;
        pushBrickItem(item);
        item->setVisible(false);
        item->setVisualRow({});
        item->setVisualFacadeReady(false);
        item->setProperty("model", QVariant());
        item->setViewSourceIndexes(-1, -1);
    }
}

void MasonryLayout::remapIncrementalLoadingRows() {
    QHash<ImageFile *, int> newIndexes;
    newIndexes.reserve(_bricks.size());
    _activeBrickIndexes.clear();
    for (int index = 0; index < _bricks.size(); ++index) {
        if (_bricks[index].image) {
            newIndexes.insert(_bricks[index].image, index);
        }
        if (_bricks[index].item) {
            _activeBrickIndexes.insert(index);
        }
    }
    for (int index = _currentLoadingRow.size() - 1; index >= 0; --index) {
        const auto newIt = newIndexes.constFind(
            _currentLoadingRow[index].image);
        if (!_currentLoadingRow[index].image
            || newIt == newIndexes.constEnd()) {
            _currentLoadingRow.removeAt(index);
        } else {
            _currentLoadingRow[index].globalIndex = *newIt;
        }
    }
    std::sort(
        _currentLoadingRow.begin(), _currentLoadingRow.end(),
        [](const MasonryBrick &left, const MasonryBrick &right) {
            return left.globalIndex < right.globalIndex;
        });
}

void MasonryLayout::restoreIncrementalViewport(
    const IncrementalModelChangeContext &) {
    restorePreservedCurrentItemPosition();
    if (_bricks.isEmpty()) {
        _currentIndex = -1;
        _topItem = 0;
    } else if (_currentIndex < 0 || _currentIndex >= _bricks.size()) {
        _currentIndex = qBound(0, _currentIndex, _bricks.size() - 1);
    }
    _visibleStart = -1;
    _visibleEnd = -1;
    rewrap(false);
    positionViewport();
}

void MasonryLayout::finishIncrementalModelChange(
    const IncrementalModelChangeContext &context) {
    const int previousImageCount = _imageCount;
    _imageCount = 0;
    for (int index = 0; index < _bricks.size(); ++index) {
        if (brickIsImage(index)) {
            ++_imageCount;
        }
    }
    if (_imageCount != previousImageCount) {
        emit imageCountChanged();
    }
    updateCurrentImageIndex();
    if (context.previousCount != _bricks.size()) {
        emit countChanged();
    }
    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }
    const bool currentIdentityPreserved =
        !context.previousCurrentPath.isEmpty() && _currentIndex >= 0
        && _currentIndex < _bricks.size()
        && brickPath(_currentIndex) == context.previousCurrentPath;
    if (!currentIdentityPreserved
        || context.previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
}

void MasonryLayout::applyIncrementalModelChange() {
    if (_incrementalModelChangeDepth <= 0
        || --_incrementalModelChangeDepth > 0) {
        return;
    }
    if (applySparseTailInsert()) {
        _incrementalInsertedFirst = -1;
        _incrementalInsertedLast = -1;
        return;
    }
    IncrementalModelChangeContext context =
        beginIncrementalModelRebuild();
    rebuildIncrementalBricks(&context);
    retireRemovedIncrementalBricks(&context);
    remapIncrementalLoadingRows();
    restoreIncrementalViewport(context);
    finishIncrementalModelChange(context);
    _incrementalInsertedFirst = -1;
    _incrementalInsertedLast = -1;
}


void MasonryLayout::onModelAboutToBeReset() {
    // `beginResetModel()` and `endResetModel()` run in one GUI-thread stack.
    // Retain each painted delegate in its current visual slot across that
    // interval. Clearing `model`, `viewIndex`, and `sourceIndex` here used to
    // invalidate every QML binding, only to assign all three again a few
    // milliseconds later. Under repeated directory navigation that doubled
    // native-icon cancellation and binding work for every visible row.
    cancelDeferredDelegateMaterialization();
    ++_delegateCatalogGeneration;
    if (_delegateCatalogGeneration == 0) {
        _delegateCatalogGeneration = 1;
    }
    releaseResetSlotItems();
    _lightweightRewrapPending = false;
    ++_lightweightRewrapGeneration;
    _resetSlotPresentationMode = _presentationMode;
    _resetSlotWidth = width();
    _resetSlotHeight = height();
    _resetSlotDensity = _density;
    _resetSlotDelegate = delegateComponent(_presentationMode);
    _resetSlotViewport = _viewport;
    _resetSlotReusePending = _resetSlotDelegate && _viewport;
    QSet<BrickItem *> retainedItems;
    if (_resetSlotReusePending) {
        _resetSlotItems.reserve(_activeBrickIndexes.size());
        retainedItems.reserve(_activeBrickIndexes.size());
        for (const int index : std::as_const(_activeBrickIndexes)) {
            const MasonryBrick *brick = brickAt(index);
            if (!brick || !brick->item) {
                continue;
            }
            BrickItem *item = brick->item;
            _resetSlotItems.insert(index, item);
            _resetSlotModels.insert(
                index, item->property("model").value<ImageFile *>());
            if (_deferDelegateRefreshOnReset && _visualSnapshotRole < 0) {
                // A model without a POD visual snapshot really is refreshed
                // later in polish, so keep its old slot hidden. Snapshot-backed
                // rows are replaced synchronously before endResetModel()
                // returns; hiding and showing them inside that one GUI-thread
                // stack only invalidates the complete delegate tree twice.
                item->setVisible(false);
                item->setVisualFacadeReady(false);
            }
            retainedItems.insert(item);
        }
    }

    _incrementalModelChangeDepth = 0;
    _currentLoadingRow.clear();
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _bricks.clear();
    _sparseBricks.clear();
    _geometryIndex.clear();
    _activeBrickIndexes.clear();
    _thumbnailPlanner.reset();
    const bool hadVisibleIndexes = !_visibleIndexSet.isEmpty();
    const bool hadOverscanIndexes = !_overscanIndexSet.isEmpty();
    _visibleIndexSet.clear();
    _overscanIndexSet.clear();
    if (_preparedResetViewportPending) {
        _currentIndex = _preparedResetCursorIndex;
    }
    else if (!_preserveCurrentItemPositionOnNextModelReset) {
        _currentIndex = 0;
    }
    for (BrickItem *item : _delegatePool.usedItems()) {
        if (retainedItems.contains(item)) {
            continue;
        }
        item->setVisible(false);
        item->setVisualRow({});
        item->setVisualFacadeReady(false);
        item->setProperty("model", QVariant());
        item->setViewSourceIndexes(-1, -1);
    }
    _delegatePool.resetTracking(retainedItems);
    if (hadVisibleIndexes) {
        emit visibleIndexesChanged();
    }
    if (hadOverscanIndexes) {
        emit overscanIndexesChanged();
    }
}

bool MasonryLayout::resetSlotLayoutMatches() const {
    if (!(_resetSlotReusePending
        && _resetSlotDelegate == delegateComponent(_presentationMode)
        && _resetSlotViewport == _viewport
        && _resetSlotPresentationMode == _presentationMode
        && qFuzzyCompare(_resetSlotWidth + 1, width() + 1)
        && qFuzzyCompare(_resetSlotHeight + 1, height() + 1)
        && qFuzzyCompare(_resetSlotDensity + 1, _density + 1))) {
        return false;
    }
    for (auto slot = _resetSlotItems.cbegin();
         slot != _resetSlotItems.cend(); ++slot) {
        BrickItem *item = slot.value();
        const QPointer<ImageFile> expected = _resetSlotModels.value(
            slot.key());
        const bool validSnapshotSlot = _visualSnapshotRole >= 0
            && item && item->visualRow().value(
                QStringLiteral("valid")).toBool()
            && item->viewIndex() == slot.key();
        if (!item || item->parentItem() != _viewport
            || (_visualSnapshotRole >= 0 && !validSnapshotSlot)
            || (_visualSnapshotRole < 0
                && (expected.isNull()
                    || item->property("model").value<ImageFile *>()
                        != expected.data()))) {
            return false;
        }
    }
    return true;
}

void MasonryLayout::releaseResetSlotItems(bool clearBindings) {
    for (BrickItem *item : std::as_const(_resetSlotItems)) {
        if (!item) {
            continue;
        }
        item->stopGeometryAnimation();
        item->setVisible(false);
        if (clearBindings) {
            item->setVisualRow({});
            item->setVisualFacadeReady(false);
            item->setProperty("model", QVariant());
            item->setViewSourceIndexes(-1, -1);
        }
        _delegatePool.release(item);
    }
    _resetSlotItems.clear();
    _resetSlotModels.clear();
    _resetSlotReusePending = false;
    _resetSlotDelegate = nullptr;
    _resetSlotViewport = nullptr;
}

void MasonryLayout::scheduleDeferredDelegateRefresh() {
    cancelDeferredDelegateMaterialization();
    _delegateRefreshPending = true;
    const quint64 generation = ++_delegateRefreshGeneration;
    polish();
    update();
    // Polish is tied to a renderable window. Keep a zero-delay fallback for
    // offscreen/windowless embedders and tests; whichever path runs first
    // consumes the same generation and the other becomes a no-op.
    QTimer::singleShot(0, this, [this, generation]() {
        if (_delegateRefreshPending
            && generation == _delegateRefreshGeneration) {
            flushDeferredDelegateRefresh();
        }
    });
}

void MasonryLayout::flushDeferredDelegateRefresh() {
    if (!_delegateRefreshPending) {
        return;
    }
    _delegateRefreshPending = false;
    ++_delegateRefreshGeneration;
    const bool snapshotOnly = _visualSnapshotRole >= 0;
    _visualSnapshotRefresh = snapshotOnly;
    updateProperties(false);
    _visualSnapshotRefresh = false;
    if (snapshotOnly) {
        scheduleDeferredDelegateMaterialization();
    }
}

void MasonryLayout::cancelDeferredDelegateMaterialization() {
    _delegateMaterializationPending = false;
    _delegateMaterializationRows.clear();
    _delegateMaterializationEntries.clear();
    _delegateMaterializationCursor = 0;
    _delegateSynchronizedGeneration.store(0, std::memory_order_release);
    if (_delegateFrameSwappedConnection) {
        disconnect(_delegateFrameSwappedConnection);
        _delegateFrameSwappedConnection = {};
    }
    if (_delegateAfterSynchronizingConnection) {
        disconnect(_delegateAfterSynchronizingConnection);
        _delegateAfterSynchronizingConnection = {};
    }
}

void MasonryLayout::scheduleDeferredDelegateMaterialization() {
    cancelDeferredDelegateMaterialization();
    _delegateMaterializationPending = true;
    const quint64 generation = ++_delegateRefreshGeneration;

    QList<int> orderedRows;
    orderedRows.reserve(_activeBrickIndexes.size());
    if (_activeBrickIndexes.contains(_currentIndex)) {
        orderedRows.append(_currentIndex);
    }
    for (const int row : std::as_const(_activeBrickIndexes)) {
        if (row != _currentIndex && brickIsImage(row)) {
            orderedRows.append(row);
        }
    }
    for (const int row : std::as_const(_activeBrickIndexes)) {
        if (row != _currentIndex && !brickIsImage(row)) {
            orderedRows.append(row);
        }
    }
    _delegateMaterializationRows.reserve(orderedRows.size());
    _delegateMaterializationEntries.reserve(orderedRows.size());
    for (const int row : std::as_const(orderedRows)) {
        enqueueDeferredDelegateMaterialization(row);
    }

    if (QQuickWindow *quickWindow = window()) {
        // A frameSwapped from an already-in-flight old scene is not proof
        // that the new snapshot was painted. Arm the swap only after the
        // render thread has synchronized at least once since this generation
        // was installed.
        _delegateAfterSynchronizingConnection = connect(
            quickWindow, &QQuickWindow::afterSynchronizing, this,
            [this, generation]() {
                _delegateSynchronizedGeneration.store(
                    generation, std::memory_order_release);
            }, Qt::DirectConnection);
        _delegateFrameSwappedConnection = connect(
            quickWindow, &QQuickWindow::frameSwapped, this,
            [this, generation]() {
                if (_delegateSynchronizedGeneration.load(
                        std::memory_order_acquire) == generation) {
                    beginDeferredDelegateMaterialization(generation);
                }
            }, Qt::QueuedConnection);
        // A hidden/offscreen surface may never swap. This fallback affects no
        // visible first frame and keeps tests/lifecycle users from retaining
        // old QObject facades indefinitely.
        QPointer<QQuickWindow> guardedWindow(quickWindow);
        QTimer::singleShot(100, this, [this, generation, guardedWindow]() {
            if (!guardedWindow || !guardedWindow->isVisible()
                || !guardedWindow->isExposed()) {
                beginDeferredDelegateMaterialization(generation);
            }
        });
    }
    else {
        QTimer::singleShot(0, this, [this, generation]() {
            beginDeferredDelegateMaterialization(generation);
        });
    }
}

void MasonryLayout::enqueueDeferredDelegateMaterialization(int row) {
    if (!_delegateMaterializationPending || row < 0
        || row >= logicalBrickCount()) {
        return;
    }
    const MasonryBrick *brick = brickAt(row);
    if (!brick) {
        return;
    }
    const QString entryId = brick->modelIdentity;
    if (_delegateMaterializationEntries.value(row) == entryId
        && _delegateMaterializationEntries.contains(row)) {
        return;
    }
    _delegateMaterializationEntries.insert(row, entryId);
    _delegateMaterializationRows.append(qMakePair(row, entryId));
}

void MasonryLayout::beginDeferredDelegateMaterialization(
    quint64 generation) {
    if (!_delegateMaterializationPending
        || generation != _delegateRefreshGeneration) {
        return;
    }
    if (_delegateFrameSwappedConnection) {
        disconnect(_delegateFrameSwappedConnection);
        _delegateFrameSwappedConnection = {};
    }
    if (_delegateAfterSynchronizingConnection) {
        disconnect(_delegateAfterSynchronizingConnection);
        _delegateAfterSynchronizingConnection = {};
    }
    materializeDeferredDelegateBatch(generation);
}

void MasonryLayout::materializeDeferredDelegateBatch(
    quint64 generation) {
    if (!_delegateMaterializationPending
        || generation != _delegateRefreshGeneration) {
        return;
    }
    QElapsedTimer budget;
    budget.start();
    int completed = 0;
    QSet<int> thumbnailRows;
    while (_delegateMaterializationCursor
               < _delegateMaterializationRows.size()
           && completed < 4 && budget.nsecsElapsed() < 1'000'000) {
        const auto [row, expectedEntryId] =
            _delegateMaterializationRows.at(
                _delegateMaterializationCursor++);
        const auto queuedEntry = _delegateMaterializationEntries.constFind(
            row);
        if (queuedEntry != _delegateMaterializationEntries.cend()
            && *queuedEntry == expectedEntryId) {
            _delegateMaterializationEntries.remove(row);
        }
        ++completed;
        MasonryBrick *brick = brickAt(row);
        if (!brick || !_activeBrickIndexes.contains(row)
            || brick->modelIdentity != expectedEntryId) {
            continue;
        }
        BrickItem *item = brick->item;
        if (!item || item->viewIndex() != row
            || item->visualRow().value(
                QStringLiteral("entryId")).toString() != expectedEntryId) {
            continue;
        }
        ImageFile *image = materializeImageForIndex(row);
        if (image && item->property("model").value<ImageFile *>() != image) {
            item->setProperty("model", QVariant::fromValue(image));
        }
        item->setVisualFacadeReady(image != nullptr);
        if (image && _overscanIndexSet.contains(row)) {
            thumbnailRows.insert(row);
        }
    }
    if (!thumbnailRows.isEmpty()) {
        // Thumbnail planning was deliberately suppressed before the first
        // swap. Resume it only for facades admitted by this bounded batch;
        // already-materialized rows bypass the pending-queue gate above.
        planViewportThumbnails(thumbnailRows);
    }
    if (_delegateMaterializationCursor
        >= _delegateMaterializationRows.size()) {
        _delegateMaterializationPending = false;
        _delegateMaterializationRows.clear();
        _delegateMaterializationEntries.clear();
        _delegateMaterializationCursor = 0;
        return;
    }
    // Yield between small batches. A repeated navigation reset increments the
    // generation and cancels this current-model-only queue before it can bind
    // a facade to the replacement catalog.
    QTimer::singleShot(8, this, [this, generation]() {
        materializeDeferredDelegateBatch(generation);
    });
}

void MasonryLayout::scheduleLightweightRewrap() {
    if (_lightweightRewrapPending) {
        return;
    }
    _lightweightRewrapPending = true;
    const quint64 generation = ++_lightweightRewrapGeneration;
    // Header readers finish independently and their historical isLast marker
    // follows submission order, not completion order. Gate relayout to at
    // most one pass per frame instead of treating that marker as a barrier.
    QTimer::singleShot(16, this, [this, generation]() {
        if (generation != _lightweightRewrapGeneration) {
            return;
        }
        _lightweightRewrapPending = false;
        if (_presentationMode != Masonry || !canUseLightweightRows()) {
            return;
        }
        rewrap(false);
        // Rewrap can change the exact target tier without changing the set of
        // overscan indexes, so explicitly re-plan that bounded window.
        planViewportThumbnails(_overscanIndexSet);
    });
}

void MasonryLayout::flushLightweightRewrap() {
    if (_lightweightRewrapPending) {
        _lightweightRewrapPending = false;
        ++_lightweightRewrapGeneration;
    }
    rewrap(false);
    planViewportThumbnails(_overscanIndexSet);
}

struct MasonryLayout::ModelResetTrace {
    bool enabled = false;
    QElapsedTimer timer;
    qint64 rowsBuiltNs = 0;
    qint64 modelSignalStartedNs = 0;
    qint64 modelSignalCompletedNs = 0;
    qint64 rewrapCompletedNs = 0;
    qint64 viewportCompletedNs = 0;
    qint64 countCompletedNs = 0;
};

void MasonryLayout::rebuildBricksAfterModelReset() {
    _sparseCatalogRows =
        _model && _model->property("sparseCatalog").toBool();
    _sparseBricks.clear();
    if (!_model) {
        return;
    }
    const bool lightweightRows = canUseLightweightRows();
    if (_sparseCatalogRows && lightweightRows) {
        _sparseBricks.reserve(qMin(_model->rowCount(), 128));
        return;
    }
    _bricks.reserve(_model->rowCount());
    for (int row = 0; row < _model->rowCount(); ++row) {
        MasonryBrick brick;
        if (lightweightRows) {
            brick = lightweightBrickForModelRow(row);
        } else {
            ImageFile *imageFile =
                imageFileFromResetIndex(_model->index(row, 0));
            if (!imageFile) {
                continue;
            }
            brick = brickForImage(imageFile);
            populateBrickModelState(brick, row);
        }
        _bricks.append(std::move(brick));
    }
}

void MasonryLayout::restoreCursorAfterModelReset() {
    restorePreservedCurrentItemPosition();
    const int rowCount = logicalBrickCount();
    if (rowCount <= 0) {
        _currentIndex = -1;
    } else if (_currentIndex < 0 || _currentIndex >= rowCount) {
        _currentIndex = qBound(0, _currentIndex, rowCount - 1);
    }
}

bool MasonryLayout::commitModelResetLayout() {
    const bool synchronousSnapshotRefresh =
        _deferDelegateRefreshOnReset && _visualSnapshotRole >= 0;
    if (synchronousSnapshotRefresh) {
        _delegateRefreshPending = false;
        ++_delegateRefreshGeneration;
        _visualSnapshotRefresh = true;
    } else if (_deferDelegateRefreshOnReset) {
        scheduleDeferredDelegateRefresh();
    }
    if (_layoutUpdateDepth > 0) {
        requestRewrap(false);
        if (synchronousSnapshotRefresh) {
            _layoutUpdateModelResetSnapshotRefresh = true;
        }
        return synchronousSnapshotRefresh;
    }
    rewrap(false);
    if (synchronousSnapshotRefresh) {
        _visualSnapshotRefresh = false;
        scheduleDeferredDelegateMaterialization();
        update();
    }
    return synchronousSnapshotRefresh;
}

void MasonryLayout::finishModelResetViewport() {
    restorePendingThumbnailRequestsAfterModelReset();
    if (_preserveDecodeQueueForCurrentRebuild) {
        _skipThumbnailBackfillUntilFlush = true;
    }
    _preserveDecodeQueueForCurrentRebuild = false;
    // rewrap() positions the viewport from the replacement geometry. During
    // an atomic presentation/catalog transaction that rewrap is deliberately
    // deferred until endLayoutUpdate(); touching the outgoing geometry here
    // would create an intermediate viewport and a second delegate commit.
    if (_layoutUpdateDepth == 0) {
        positionViewport();
    }
}

void MasonryLayout::refreshImageCountAfterModelReset() {
    _imageCount = 0;
    if (_sparseCatalogRows) {
        for (const int row : materializedModelRows()) {
            if (brickIsImage(row)) {
                ++_imageCount;
            }
        }
    } else {
        for (int row = 0; row < _bricks.size(); ++row) {
            if (brickIsImage(row)) {
                ++_imageCount;
            }
        }
    }
    emit imageCountChanged();
}

void MasonryLayout::finishModelResetSignals(
    int previousCurrentIndex, const QString &preservedCurrentPath) {
    updateCurrentImageIndex();
    emit countChanged();
    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }
    const bool currentIdentityPreserved =
        !preservedCurrentPath.isEmpty() && _currentIndex >= 0
        && _currentIndex < logicalBrickCount()
        && brickPath(_currentIndex) == preservedCurrentPath;
    if (!currentIdentityPreserved
        || previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
}

void MasonryLayout::traceModelReset(
    const ModelResetTrace &trace) const {
    if (!trace.enabled) {
        return;
    }
    const qint64 completedNs = trace.timer.nsecsElapsed();
    qInfo().nospace()
        << "F4_NAV_BENCHMARK_TRACE masonry.reset rowsNs="
        << trace.rowsBuiltNs << " preSignalNs="
        << (trace.modelSignalStartedNs - trace.rowsBuiltNs)
        << " modelSignalNs="
        << (trace.modelSignalCompletedNs - trace.modelSignalStartedNs)
        << " rewrapNs="
        << (trace.rewrapCompletedNs - trace.modelSignalCompletedNs)
        << " viewportNs="
        << (trace.viewportCompletedNs - trace.rewrapCompletedNs)
        << " countNs="
        << (trace.countCompletedNs - trace.viewportCompletedNs)
        << " tailNs=" << (completedNs - trace.countCompletedNs)
        << " totalNs=" << completedNs;
    ZoinGallery::MediaTimingTrace::event(
        QStringLiteral("qt.gallery.masonry.reset"), {
            {QStringLiteral("rowsNs"), trace.rowsBuiltNs},
            {QStringLiteral("modelSignalNs"),
             trace.modelSignalCompletedNs
                 - trace.modelSignalStartedNs},
            {QStringLiteral("rewrapNs"),
             trace.rewrapCompletedNs
                 - trace.modelSignalCompletedNs},
            {QStringLiteral("viewportNs"),
             trace.viewportCompletedNs
                 - trace.rewrapCompletedNs},
            {QStringLiteral("countNs"),
             trace.countCompletedNs
                 - trace.viewportCompletedNs},
            {QStringLiteral("durationNs"), completedNs},
        });
}

void MasonryLayout::onModelReset() {
    ModelResetTrace trace;
    trace.enabled = qEnvironmentVariableIsSet(
        "F4_NAV_BENCHMARK_TRACE");
    if (trace.enabled) {
        trace.timer.start();
    }
    const int previousCurrentIndex = _currentIndex;
    const QString preservedCurrentPath =
        _preserveCurrentItemPositionOnNextModelReset
        ? _preservedCurrentItemFullPath : QString();
    rebuildBricksAfterModelReset();
    trace.rowsBuiltNs =
        trace.enabled ? trace.timer.nsecsElapsed() : 0;
    restoreCursorAfterModelReset();
    if (!resetSlotLayoutMatches()) {
        releaseResetSlotItems();
    }
    trace.modelSignalStartedNs =
        trace.enabled ? trace.timer.nsecsElapsed() : 0;
    emit modelChanged();
    trace.modelSignalCompletedNs =
        trace.enabled ? trace.timer.nsecsElapsed() : 0;
    commitModelResetLayout();
    trace.rewrapCompletedNs =
        trace.enabled ? trace.timer.nsecsElapsed() : 0;
    finishModelResetViewport();
    trace.viewportCompletedNs =
        trace.enabled ? trace.timer.nsecsElapsed() : 0;
    refreshImageCountAfterModelReset();
    finishModelResetSignals(
        previousCurrentIndex, preservedCurrentPath);
    trace.countCompletedNs =
        trace.enabled ? trace.timer.nsecsElapsed() : 0;
    traceModelReset(trace);
}


void MasonryLayout::restorePreservedCurrentItemPosition() {
    if (!_preserveCurrentItemPositionOnNextModelReset) {
        return;
    }

    _preserveCurrentItemPositionOnNextModelReset = false;
    const int rowCount = logicalBrickCount();
    const auto findPath = [this, rowCount](const QString &path) {
        if (path.isEmpty()) {
            return -1;
        }
        if (_sparseCatalogRows) {
            for (const int row : materializedModelRows()) {
                if (brickPath(row) == path) {
                    return row;
                }
            }
            return -1;
        }
        for (int row = 0; row < rowCount; ++row) {
            if (brickPath(row) == path) {
                return row;
            }
        }
        return -1;
    };
    bool restored = false;
    if (!_preservedCurrentItemFullPath.isEmpty()) {
        const int row = findPath(_preservedCurrentItemFullPath);
        if (row >= 0) {
            _currentIndex = row;
            restored = true;
        }
    }
    if (!restored && rowCount > 0) {
        _currentIndex = qBound(0, _preservedCurrentFallbackIndex,
                               rowCount - 1);
    }

    bool restoredViewportAnchor = false;
    if (!_preservedViewportAnchorFullPath.isEmpty()) {
        const int row = findPath(_preservedViewportAnchorFullPath);
        if (row >= 0) {
            _topItem = row;
            _topItemOffset = _preservedViewportAnchorOffset;
            restoredViewportAnchor = true;
        }
    }
    if (!restoredViewportAnchor && rowCount > 0 &&
        _preservedViewportAnchorFallbackIndex >= 0) {
        _topItem = qBound(0, _preservedViewportAnchorFallbackIndex,
                          rowCount - 1);
        _topItemOffset = _preservedViewportAnchorOffset;
    }

    _preservedCurrentItemFullPath.clear();
    _preservedCurrentFallbackIndex = -1;
    _preservedViewportAnchorFullPath.clear();
    _preservedViewportAnchorFallbackIndex = -1;
    _preservedViewportAnchorOffset = 0;
}
