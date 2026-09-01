#include "MasonryLayout.h"
#include "FileListModel.h"

#include <algorithm>
#include <limits>

namespace {

ImageFile *imageFileFromDeltaIndex(const QModelIndex &index) {
    return index.data(FileListModel::ImageFileRole).value<ImageFile *>();
}

bool deltaModelRequestsViewStatePreservation(QAbstractItemModel *model) {
    const auto *requestModel =
        dynamic_cast<const ZoinGallery::GalleryCatalogSource *>(model);
    return requestModel && requestModel->preserveViewStateOnReset();
}

} // namespace

void MasonryLayout::setContentHeight(qreal newContentHeight) {
    if (qFuzzyCompare(_contentHeight, newContentHeight)) {
        return;
    }
    _contentHeight = newContentHeight;
    if (_viewport) {
        if (_presentationMode == Columns) {
            _viewport->setWidth(_contentHeight);
            _viewport->setHeight(height());
        }
        else {
            _viewport->setWidth(width());
            _viewport->setHeight(_contentHeight);
        }
    }
    emit contentHeightChanged();
    updateNeedScroll();
}

struct MasonryLayout::ModelDelta
{
    int first = -1;
    int last = -1;
    QVector<int> roles;
    bool semanticRolesChanged = false;
    bool pathRoleChanged = false;
    bool textRoleChanged = false;
    bool sizeRoleChanged = false;
    bool imageUrlOnly = false;

    bool roleChanged(int role) const
    {
        return roles.isEmpty() || roles.contains(role);
    }
};

void MasonryLayout::onDataChanged(
    const QModelIndex &topLeft, const QModelIndex &bottomRight,
    const QVector<int> &roles)
{
    auto *catalogSource =
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model);
    if (!catalogSource) {
        return;
    }
    if (!isEmbedded()
        && imageFileFromDeltaIndex(topLeft.parent())
            != catalogSource->rootItem()) {
        return;
    }

    ModelDelta delta;
    delta.first = topLeft.row();
    delta.last = bottomRight.row();
    delta.roles = roles;
    const int count = _sparseCatalogRows ? logicalBrickCount()
                                         : _bricks.size();
    if (delta.first < 0 || delta.last < delta.first || delta.last >= count) {
        return;
    }
    delta.semanticRolesChanged =
        delta.roleChanged(FileListModel::IsImageRole)
        || delta.roleChanged(FileListModel::FolderRole);
    delta.pathRoleChanged = _localPathRole >= 0
        && delta.roleChanged(_localPathRole);
    delta.textRoleChanged = _entryNameRole >= 0
        && delta.roleChanged(_entryNameRole);
    delta.sizeRoleChanged =
        delta.roleChanged(FileListModel::ImageFullSizeRole)
        || (_knownImageSizeRole >= 0
            && delta.roleChanged(_knownImageSizeRole));
    delta.imageUrlOnly = roles.size() == 1
        && roles.contains(FileListModel::ImageIdUrlRole);

    if (_sparseCatalogRows) {
        applySparseModelDelta(delta);
    } else {
        applyMaterializedModelDelta(delta);
    }
}

void MasonryLayout::applySparseModelDelta(const ModelDelta &delta)
{
    for (int row = delta.first; row <= delta.last; ++row) {
        MasonryBrick *existing = brickAt(row);
        if (!existing && !_activeBrickIndexes.contains(row)) {
            continue;
        }
        MasonryBrick incoming = lightweightBrickForModelRow(row);
        if (incoming.modelIdentity.isEmpty()) {
            continue;
        }
        MasonryBrick &target = existing ? *existing : ensureBrickAt(row);
        BrickItem *item = target.item;
        ImageFile *boundImage = item
            ? item->property("model").value<ImageFile *>() : nullptr;
        const bool rebindFacade = item && (target.image || boundImage);
        ImageFile *image = rebindFacade
            ? imageFileFromDeltaIndex(_model->index(row, 0)) : nullptr;
        const quint64 previousRevision = target.modelVisualRevision;
        target = std::move(incoming);
        target.item = item;
        target.image = image;
        target.globalIndex = row;
        target.modelVisualRevision =
            previousRevision == std::numeric_limits<quint64>::max()
            ? 1 : previousRevision + 1;
        applyAnalyticFixedGeometry(target, row);
        if (item) {
            if (boundImage != image) {
                item->setProperty(
                    "model", image ? QVariant::fromValue(image) : QVariant());
            }
            item->setVisualFacadeReady(image != nullptr);
        }
        if (_activeBrickIndexes.contains(row)
            && (!delta.imageUrlOnly || !item
                || !item->visualFacadeReady())) {
            updateVisualSnapshotForIndex(row);
        }
    }
    if (delta.semanticRolesChanged) {
        refreshImageCountFromCatalog();
    }
    if (delta.roleChanged(FileListModel::ImageFullSizeRole)
        || delta.roleChanged(FileListModel::ImageIdUrlRole)) {
        planDeltaThumbnails(delta);
    }
}

void MasonryLayout::updateMaterializedBrickState(const ModelDelta &delta)
{
    if (!delta.semanticRolesChanged && !delta.pathRoleChanged
        && !delta.textRoleChanged && !delta.sizeRoleChanged) {
        return;
    }
    for (int row = delta.first; row <= delta.last; ++row) {
        const QModelIndex modelIndex = _model->index(row, 0);
        MasonryBrick &brick = _bricks[row];
        if (delta.semanticRolesChanged) {
            brick.modelIsImage = modelIndex.data(
                FileListModel::IsImageRole).toBool();
            brick.modelIsFolder = modelIndex.data(
                FileListModel::FolderRole).toBool();
        }
        if (delta.pathRoleChanged) {
            brick.modelPath = modelIndex.data(_localPathRole).toString();
        }
        if (delta.textRoleChanged) {
            brick.modelText = modelIndex.data(_entryNameRole).toString();
        }
        if (delta.sizeRoleChanged) {
            brick.modelKnownSize = modelIndex.data(
                FileListModel::ImageFullSizeRole).toSize();
        }
        if (!canUseLightweightRows()
            || (!delta.semanticRolesChanged && !delta.sizeRoleChanged)) {
            continue;
        }
        QSize layoutSize = brick.modelKnownSize;
        bool lineBreakAfter = false;
        if (layoutSize.isEmpty()) {
            if (brick.modelIsFolder && _listView) {
                lineBreakAfter = true;
                layoutSize = QSize(0, listRowHeight());
            } else {
                layoutSize = GridView_Folder.toSize();
            }
        }
        brick.originalSize = layoutSize;
        brick.lineBreakAfter = lineBreakAfter;
    }
    if (delta.semanticRolesChanged) {
        refreshImageCountFromCatalog();
    }
}

void MasonryLayout::refreshImageCountFromCatalog()
{
    const int previousImageCount = _imageCount;
    _imageCount = 0;
    if (_sparseCatalogRows) {
        for (const int row : materializedModelRows()) {
            if (_model->index(row, 0)
                    .data(FileListModel::IsImageRole).toBool()) {
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
    if (_imageCount != previousImageCount) {
        emit imageCountChanged();
    }
    updateCurrentImageIndex();
}

void MasonryLayout::updateVisualRowsForDelta(const ModelDelta &delta)
{
    if (_visualSnapshotRole < 0) {
        return;
    }
    for (int row = delta.first; row <= delta.last; ++row) {
        quint64 &revision = _bricks[row].modelVisualRevision;
        revision = revision == std::numeric_limits<quint64>::max()
            ? 1 : revision + 1;
    }
    for (int row = delta.first; row <= delta.last; ++row) {
        if (!_activeBrickIndexes.contains(row)) {
            continue;
        }
        BrickItem *item = _bricks[row].item;
        if (!delta.imageUrlOnly || !item || !item->visualFacadeReady()) {
            updateVisualSnapshotForIndex(row);
        }
    }
}

void MasonryLayout::planDeltaThumbnails(const ModelDelta &delta)
{
    QSet<int> candidates;
    for (int row = delta.first; row <= delta.last; ++row) {
        if (_overscanIndexSet.contains(row)) {
            candidates.insert(row);
        }
    }
    planViewportThumbnails(candidates);
}

bool MasonryLayout::handleLightweightMasonryDelta(const ModelDelta &delta)
{
    if (_presentationMode != Masonry || !canUseLightweightRows()) {
        return false;
    }
    if (delta.semanticRolesChanged || delta.sizeRoleChanged) {
        if (delta.roles.isEmpty()
            || delta.roles.contains(FileListModel::CachedMetadataBatchRole)) {
            flushLightweightRewrap();
        } else {
            scheduleLightweightRewrap();
        }
    }
    if (delta.roleChanged(FileListModel::ImageFullSizeRole)
        || delta.roleChanged(FileListModel::ImageIdUrlRole)) {
        planDeltaThumbnails(delta);
    }
    return true;
}

bool MasonryLayout::handleFixedLayoutDelta(const ModelDelta &delta)
{
    if (_presentationMode == Masonry) {
        return false;
    }
    if (delta.roleChanged(FileListModel::ImageFullSizeRole)) {
        for (int row = delta.first; row <= delta.last; ++row) {
            if (_bricks[row].image
                && _bricks[row].image->fullSize().isValid()) {
                _bricks[row].originalSize =
                    _bricks[row].image->fullSize();
            }
        }
    }
    if (delta.roleChanged(FileListModel::ImageFullSizeRole)
        || delta.roleChanged(FileListModel::ImageIdUrlRole)) {
        planDeltaThumbnails(delta);
    }
    return true;
}

void MasonryLayout::applyMaterializedModelDelta(const ModelDelta &delta)
{
    updateMaterializedBrickState(delta);
    updateVisualRowsForDelta(delta);
    if (handleLightweightMasonryDelta(delta)
        || handleFixedLayoutDelta(delta)) {
        return;
    }
    handleMasonryDelta(delta);
}

void MasonryLayout::applyMasonryImageSizeDelta(const ModelDelta &delta)
{
    const bool cachedMetadataBatch = delta.roles.contains(
        FileListModel::CachedMetadataBatchRole);
    bool animateLayoutChange = !cachedMetadataBatch;
    if (animateLayoutChange) {
        animateLayoutChange = false;
        for (int row = delta.first; row <= delta.last; ++row) {
            if (_bricks[row].image
                && _bricks[row].image->fullSize().isValid()
                && !_bricks[row].image->info().isCached) {
                animateLayoutChange = true;
                break;
            }
        }
    }
    const int changedIndexes = delta.last - delta.first;
    if (isEmbedded() || _skipThumbnailBackfillUntilFlush
        || (changedIndexes > 1 && _currentLoadingRow.isEmpty())) {
        for (int row = delta.first; row <= delta.last; ++row) {
            if (_bricks[row].image
                && _bricks[row].image->fullSize().isValid()) {
                _bricks[row].originalSize =
                    _bricks[row].image->fullSize();
            }
        }
        rewrap(animateLayoutChange);
        if (!isEmbedded()) {
            planDeltaThumbnails(delta);
        }
        return;
    }

    for (int row = delta.first; row <= delta.last; ++row) {
        pushToCurrentRow(row, animateLayoutChange);
    }
    if (!isEmbedded()
        && deltaModelRequestsViewStatePreservation(_model)) {
        planDeltaThumbnails(delta);
    }
}

void MasonryLayout::handleMasonryDelta(const ModelDelta &delta)
{
    if (delta.roleChanged(FileListModel::ImageIdUrlRole)) {
        planDeltaThumbnails(delta);
    }
    if (!isEmbedded() && deltaModelRequestsViewStatePreservation(_model)
        && (delta.roles.contains(FileListModel::LastModifiedRole)
            || delta.roles.contains(FileListModel::FileSizeRole))) {
        for (int row = delta.first; row <= delta.last; ++row) {
            if (_bricks[row].image && _bricks[row].image->isImage()) {
                _skipThumbnailBackfillUntilFlush = true;
                break;
            }
        }
    }
    if (delta.roles.contains(FileListModel::ImageFullSizeRole)) {
        applyMasonryImageSizeDelta(delta);
    }
    if (delta.roles.contains(FileListModel::FolderViewRole)) {
        const QSize folderSize = _listView ? QSize(0, 0)
                                           : GridView_Folder.toSize();
        if (_bricks[delta.first].originalSize != folderSize) {
            _bricks[delta.first].originalSize = folderSize;
            rewrap(!_bricks[delta.first].image
                   || !_bricks[delta.first].image->info().isCached);
        }
    }
    if (delta.roles.contains(FileListModel::TimeToFlushRole)) {
        const bool animateLayoutChange =
            !delta.roles.contains(FileListModel::CachedMetadataBatchRole)
            && (!_bricks[delta.first].image
                || !_bricks[delta.first].image->info().isCached);
        onThumbnailReadFinished(animateLayoutChange);
    }
}

#include <QThread>
void MasonryLayout::pushToCurrentRow(int index, bool animate) {
    bool flushMode = index >= _bricks.count();
    if (!_currentLoadingRow.count() || index - _currentLoadingRow.last().globalIndex > 1) {
        int lastIndex = -1;
        if (_currentLoadingRow.count()) {
            lastIndex = _currentLoadingRow.last().globalIndex;
        }
        if (_skipThumbnailBackfillUntilFlush) {
            lastIndex = index - 1;
        }
        int indexToInsert = _currentLoadingRow.count();
        for (int i = index - 1; i >= 0; i--) {
            if (i > lastIndex) {
               // qDebug() << "adding index" << i << "from" << index << i << lastIndex;
                _currentLoadingRow.insert(indexToInsert, _bricks[i]);
                _currentLoadingRow[indexToInsert].globalIndex = i;
            }
            else {
                break;
            }
        }
    }
    if (!flushMode) {
        _currentLoadingRow.append(MasonryBrick {
            .originalSize = _bricks[index].image->fullSize(),
            .image = _bricks[index].image,
        });
                                  // (_bricks[index].image->fullSize().width(), _bricks[index].image->fullSize().height()));
        _currentLoadingRow.last().globalIndex = index;
    }

    // qDebug() << "==";
    // for (int k = 0; k < _currentLoadingRow.count(); k++) {
    //     qDebug() << "In row" << _currentLoadingRow[k].globalIndex;
    // }
    // qDebug() << "==";

    calcLayout(_currentLoadingRow, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, 0, layoutMode());
    if (_currentLoadingRow.last().row > 0 || flushMode) {
        // qDebug() << "//// pushing" << _currentLoadingRow.first().globalIndex << "-" << _currentLoadingRow.last().globalIndex << flushMode << _currentLoadingRow.size();
        // qDebug() << "REWRAP";
        QList<int> requestsIndexes;
        for (int i = 0; i < _currentLoadingRow.size(); i++) {
            // qDebug() << "i" << i << _currentLoadingRow[i].globalIndex << _currentLoadingRow[i].row << _currentLoadingRow.last().row;
            if (_currentLoadingRow[i].row != _currentLoadingRow.last().row || flushMode) {
                int updIndex = _currentLoadingRow[i].globalIndex;
                if (_bricks[updIndex].image && _bricks[updIndex].image->fullSize().isValid()) {
                    // qDebug() << "Full size is valid, updating" << updIndex;
                    _bricks[updIndex].originalSize = _bricks[updIndex].image->fullSize();
                    if (_bricks[updIndex].image->isImage()) {
                        _bricks[updIndex].image->setIsShowAsImage(true);
                    }
                    // When pushing single item that fills the whole row we need to add a line break
                    if (!flushMode && !_bricks[updIndex].column && i == _currentLoadingRow.size() - 2) {
                        // qDebug() << "Last in row, forcing line break" << updIndex << "at" << i;
                        _bricks[updIndex].temporaryLineBreakAfter = true;

                        for (int delIndex = 0; delIndex <= i; delIndex++) {
                            _currentLoadingRow.removeFirst();
                        }
                        requestsIndexes.append(updIndex);
                        break;
                    }
                    requestsIndexes.append(updIndex);
                }
            }
            else {
                if (i) {
                    int updIndex = _currentLoadingRow[i - 1].globalIndex;
                    if (updIndex >= 0) {
                        // qDebug() << "Second line break source" << updIndex << ", removing 0 to" << i - 1;
                        _bricks[updIndex].temporaryLineBreakAfter = true;
                    }
                }

                for (int delIndex = 0; delIndex < i; delIndex++) {
                    _currentLoadingRow.removeFirst();
                }
                break;
            }
        }
        if (flushMode) {
            _currentLoadingRow.clear();
        }
        rewrap(animate);

        QList<ImageDecodeRequest> requests;
        // qDebug() << "-------------- 2" << _currentLoadingRow.last().row << flushMode;
        for (int i = 0; i < requestsIndexes.size(); i++) {
            int index = requestsIndexes[i];
            if (!_overscanIndexSet.contains(index)) {
                continue;
            }
            planThumbnailForIndex(
                index, index >= _visibleStart && index <= _visibleEnd,
                requests);
            // qDebug() << "decode2 " << requests.last().info.path << requests.last().targetSize;
        }
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model)
            ->decodeImages(requests);
    }
}

void MasonryLayout::onThumbnailReadFinished(bool animate) {
    if (_bricks.count() && _currentLoadingRow.size()) {
        pushToCurrentRow(_bricks.count(), animate);
    }
    _skipThumbnailBackfillUntilFlush = false;
}

MasonryLayout::MasonryBrick MasonryLayout::brickForImage(
    ImageFile *imageFile) const {
    QSize imageSize = imageFile ? imageFile->fullSize() : QSize();
    bool lineBreakAfter = false;
    if (imageFile && imageSize.isEmpty()) {
        if (imageFile->isFolder() && _listView) {
            lineBreakAfter = true;
            imageSize = QSize(0, imageFile->folderView() ? 0
                                                        : listRowHeight());
        }
        else {
            imageSize = GridView_Folder.toSize();
        }
    }
    return MasonryBrick{
        .originalSize = imageSize,
        .lineBreakAfter = lineBreakAfter,
        .image = imageFile,
    };
}
