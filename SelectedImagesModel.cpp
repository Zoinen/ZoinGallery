#include "SelectedImagesModel.h"

#include "DecodeManager.h"
#include "PersistentSelectionCache.h"
#include "ThumbnailLoader.h"

#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <utility>

SelectedImagesModel::SelectedImagesModel(
    FileListModel *sourceModel,
    QSharedPointer<ProviderImageStore> providerImageStore, QObject *parent)
    : QAbstractListModel(parent),
      _selectionSourceModel(sourceModel),
      _decodeManager(new DecodeManager(this)),
      _providerImageStore(std::move(providerImageStore)),
      _viewerImageCache(QStringLiteral("selected-viewer-"),
                        _providerImageStore) {
    _decodeManager->setImageCacheMode(
        cacheUsageModeFromInt(_selectionSourceModel->imageCacheMode()));

    connect(_selectionSourceModel, &FileListModel::selectionPathsChanged,
            this, &SelectedImagesModel::syncPathsFromPersistentSelection);
    connect(_selectionSourceModel, &FileListModel::activeSelectionGroupChanged,
            this, [this]() {
                const QString nextGroupId =
                    _selectionSourceModel->activeSelectionGroupId();
                if (nextGroupId != _activeGroupId) {
                    syncFromPersistentSelection();
                }
            });
    connect(_selectionSourceModel, &FileListModel::imageCacheModeChanged, this, [this]() {
        _decodeManager->cancelAllRunners();
        _decodeManager->setImageCacheMode(
            cacheUsageModeFromInt(_selectionSourceModel->imageCacheMode()));
        _imageInfoRequestTimer.start();
    });
    connect(_decodeManager, &DecodeManager::imageInfoReady,
            this, &SelectedImagesModel::onImageInfoAvailable);
    connect(_decodeManager, &DecodeManager::imagesInfoReady,
            this, &SelectedImagesModel::onImagesInfoAvailable);
    connect(_decodeManager, &DecodeManager::imageReady,
            this, &SelectedImagesModel::onImageAvailable);
    connect(_decodeManager, &DecodeManager::viewerRunnerCanceled,
            this, [this](const QString &path) {
                _viewerImageCache.removeIncomplete(path);
            });
    _imageInfoRequestTimer.setSingleShot(true);
    _imageInfoRequestTimer.setInterval(0);
    connect(&_imageInfoRequestTimer, &QTimer::timeout,
            this, &SelectedImagesModel::requestMissingImageInfo);

    syncFromPersistentSelection();
}

int SelectedImagesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : _items.size();
}

QVariant SelectedImagesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= _items.size()) {
        return QVariant();
    }

    ImageFile *item = _items[index.row()];
    switch (role) {
    case FileListModel::ImageIdUrlRole:
        return item->imageIdUrl();
    case FileListModel::SelectedRole:
        return item->isSelected();
    case FileListModel::SelectionGroupIdRole:
        return item->selectionGroupId();
    case FileListModel::SelectionGroupColorRole:
        return item->selectionGroupColor();
    case FileListModel::ImageFileRole:
        return QVariant::fromValue(item);
    case FileListModel::FolderRole:
        return false;
    case FileListModel::IsImageRole:
        return true;
    case FileListModel::ImageFullSizeRole:
        return item->fullSize();
    case FileListModel::FolderViewRole:
        return false;
    case FileListModel::LastModifiedRole:
        return item->lastModified();
    case FileListModel::FileSizeRole:
        return item->fileSize();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> SelectedImagesModel::roleNames() const {
    return _selectionSourceModel->roleNames();
}

ImageFile *SelectedImagesModel::rootItem() const {
    return nullptr;
}

void SelectedImagesModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    _decodeManager->decodeImages(requests);
}

void SelectedImagesModel::cancelAllRunners() {
    _decodeManager->cancelAllRunners();
}

void SelectedImagesModel::cancelAllDecodeRunners() {
    _decodeManager->cancelAllDecodeRunners();
}

void SelectedImagesModel::cancelAllDecodeViewerRunners() {
    _decodeManager->cancelAllDecodeViewerRunners();
}

void SelectedImagesModel::prepareToClose() {
    _imageInfoRequestTimer.stop();
    _decodeManager->prepareToClose();
}

const ImageFile *SelectedImagesModel::itemForImageId(const QString &imageId) const {
    return _imageIdToItem.value(imageId, nullptr);
}

QImage SelectedImagesModel::viewerForImageId(const QString &imageId) const {
    return _viewerImageCache.viewerImageForId(imageId);
}

QImage SelectedImagesModel::fullSizeViewerForImageId(
    const QString &imageId) const {
    return _viewerImageCache.fullSizeImageForId(imageId);
}

bool SelectedImagesModel::containsPath(const QString &path) const {
    return _pathToItem.contains(path);
}

int SelectedImagesModel::selectedCount() const {
    int count = 0;
    for (const ImageFile *item : _items) {
        count += item->isSelected() ? 1 : 0;
    }
    return count;
}

int SelectedImagesModel::totalPathCount() const {
    return _totalPathCount;
}

int SelectedImagesModel::unavailableCount() const {
    return _unavailableCount;
}

bool SelectedImagesModel::isIndexSelected(int index) const {
    return index >= 0 && index < _items.size() && _items[index]->isSelected();
}

void SelectedImagesModel::setSelection(int index, bool selected) {
    if (index < 0 || index >= _items.size() || _items[index]->isSelected() == selected) {
        return;
    }
    _items[index]->setIsSelected(selected);
    emit dataChanged(this->index(index, 0), this->index(index, 0), {FileListModel::SelectedRole});
    emit panelSelectionChanged();
}

void SelectedImagesModel::toggleSelection(int index) {
    if (index >= 0 && index < _items.size()) {
        setSelection(index, !_items[index]->isSelected());
    }
}

void SelectedImagesModel::replaceSelection(int index) {
    if (index < 0 || index >= _items.size()) {
        return;
    }
    bool changed = false;
    for (int i = 0; i < _items.size(); i++) {
        const bool selected = i == index;
        if (_items[i]->isSelected() != selected) {
            _items[i]->setIsSelected(selected);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::selectRange(int anchorIndex, int targetIndex, bool preserveExisting) {
    if (anchorIndex < 0 || targetIndex < 0 ||
        anchorIndex >= _items.size() || targetIndex >= _items.size()) {
        return;
    }

    const int first = qMin(anchorIndex, targetIndex);
    const int last = qMax(anchorIndex, targetIndex);
    bool changed = false;
    for (int i = 0; i < _items.size(); i++) {
        const bool selected = (i >= first && i <= last) || (preserveExisting && _items[i]->isSelected());
        if (_items[i]->isSelected() != selected) {
            _items[i]->setIsSelected(selected);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::clearSelection() {
    bool changed = false;
    for (ImageFile *item : _items) {
        if (item->isSelected()) {
            item->setIsSelected(false);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::setAllSelection(bool selected) {
    bool changed = false;
    for (ImageFile *item : _items) {
        if (item->isSelected() != selected) {
            item->setIsSelected(selected);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::removeFromCollection(int index) {
    if (index >= 0 && index < _items.size()) {
        _selectionSourceModel->setPathSelection(_items[index]->fullPath(), false);
    }
}

int SelectedImagesModel::mapToSourceRow(int viewRow) const {
    return viewRow >= 0 && viewRow < _items.size() ? viewRow : -1;
}

int SelectedImagesModel::mapFromSourceRow(int sourceRow) const {
    return mapToSourceRow(sourceRow);
}

QVariantList SelectedImagesModel::mapToSourceRows(const QVariantList &viewRows) const {
    QVariantList result;
    result.reserve(viewRows.size());
    for (const QVariant &value : viewRows) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0 && row < _items.size()) {
            result.append(row);
        }
    }
    return result;
}

QVariantList SelectedImagesModel::sourceRowsForViewRange(int anchorViewRow, int targetViewRow,
                                                         bool includeTarget) const {
    QVariantList result;
    if (anchorViewRow < 0 || targetViewRow < 0 ||
        anchorViewRow >= _items.size() || targetViewRow >= _items.size()) {
        return result;
    }

    int first = qMin(anchorViewRow, targetViewRow);
    int last = qMax(anchorViewRow, targetViewRow);
    if (!includeTarget) {
        if (targetViewRow > anchorViewRow) {
            last--;
        }
        else if (targetViewRow < anchorViewRow) {
            first++;
        }
        else {
            return result;
        }
    }
    for (int row = first; row <= last; row++) {
        result.append(row);
    }
    return result;
}

void SelectedImagesModel::beginSelectionPreview() {
    if (_selectionPreviewActive) {
        return;
    }
    _selectionPreviewActive = true;
    _selectionPreviewSnapshot.clear();
    for (int i = 0; i < _items.size(); i++) {
        if (_items[i]->isSelected()) {
            _selectionPreviewSnapshot.insert(i);
        }
    }
}

void SelectedImagesModel::previewSelectionIndexes(const QVariantList &indexes, int mode) {
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    QSet<int> affectedIndexes;
    for (const QVariant &value : indexes) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0 && row < _items.size()) {
            affectedIndexes.insert(row);
        }
    }

    constexpr int Add = 0;
    constexpr int Deselect = 1;
    constexpr int Replace = 2;
    constexpr int Toggle = 3;
    for (int i = 0; i < _items.size(); i++) {
        bool selected = _selectionPreviewSnapshot.contains(i);
        if (mode == Replace) {
            selected = affectedIndexes.contains(i);
        }
        else if (mode == Toggle && affectedIndexes.contains(i)) {
            selected = !selected;
        }
        else if (mode == Add && affectedIndexes.contains(i)) {
            selected = true;
        }
        else if (mode == Deselect && affectedIndexes.contains(i)) {
            selected = false;
        }
        _items[i]->setIsSelected(selected);
    }
    emitSelectionDataChanged();
}

void SelectedImagesModel::commitSelectionPreview(const QString &description) {
    Q_UNUSED(description)
    if (!_selectionPreviewActive) {
        return;
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
}

void SelectedImagesModel::cancelSelectionPreview() {
    if (!_selectionPreviewActive) {
        return;
    }
    for (int i = 0; i < _items.size(); i++) {
        _items[i]->setIsSelected(_selectionPreviewSnapshot.contains(i));
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    emitSelectionDataChanged();
}

QVariantList SelectedImagesModel::dragIndexesForIndex(int index, bool singleItemOnly) const {
    QVariantList result;
    if (index < 0 || index >= _items.size()) {
        return result;
    }

    if (!singleItemOnly && _items[index]->isSelected()) {
        for (int i = 0; i < _items.size(); i++) {
            if (_items[i]->isSelected()) {
                result.append(i);
            }
        }
    }
    else {
        result.append(index);
    }
    return result;
}

QVariantList SelectedImagesModel::dragUrlsForIndex(int index, bool singleItemOnly) const {
    QVariantList result;
    for (const QVariant &indexValue : dragIndexesForIndex(index, singleItemOnly)) {
        const int itemIndex = indexValue.toInt();
        if (itemIndex >= 0 && itemIndex < _items.size()) {
            result.append(QUrl::fromLocalFile(_items[itemIndex]->fullPath()));
        }
    }
    return result;
}

QVariantMap SelectedImagesModel::dragPreviewItemsForIndex(int index, int limit,
                                                          bool singleItemOnly) const {
    QVariantMap result;
    QVariantList items;
    const QVariantList indexes = dragIndexesForIndex(index, singleItemOnly);
    const int cappedCount = limit < 0 ? indexes.size() : qMin(limit, indexes.size());
    for (int i = 0; i < cappedCount; i++) {
        const int itemIndex = indexes[i].toInt();
        if (itemIndex < 0 || itemIndex >= _items.size()) {
            continue;
        }
        const ImageFile *item = _items[itemIndex];
        items.append(QVariantMap{
            {"index", itemIndex},
            {"text", item->text()},
            {"imageIdUrl", item->imageIdUrl()},
            {"iconPath", item->iconPath()},
            {"isImage", true},
            {"isFolder", false},
            {"fullPath", item->fullPath()},
        });
    }
    result["items"] = items;
    result["totalCount"] = indexes.size();
    result["remainingCount"] = qMax(0, indexes.size() - items.size());
    return result;
}

void SelectedImagesModel::requestViewer(int index, int width, int height) {
    if (index < 0 || index >= _items.size()) {
        return;
    }

    ImageFile *item = _items[index];
    _currentViewerPath = item->fullPath();
    if (!item->imageIdUrl().isEmpty()) {
        emit viewerImageIdUrlChanged(item->imageIdUrl(), 0);
    }

    const ViewerImageCache::RequestPlan requestPlan =
        _viewerImageCache.planRequest(_items, index, QSize(width, height));
    for (const auto &[url, level] : requestPlan.cachedImages) {
        emit viewerImageIdUrlChanged(url, level);
    }
    _decodeManager->decodeImages(requestPlan.decodeRequests);
}

QString SelectedImagesModel::bestViewerImageUrlForIndex(int index) const {
    return index >= 0 && index < _items.size()
        ? _viewerImageCache.bestImageUrl(_items[index])
        : QString();
}

QColor SelectedImagesModel::selectionGroupColorForIndex(int index) const {
    return index >= 0 && index < _items.size()
        ? _items[index]->selectionGroupColor()
        : QColor();
}

void SelectedImagesModel::syncFromPersistentSelection() {
    const QList<PersistentSelectionCache::SelectedFile> selectedFiles =
        PersistentSelectionCache::selectedFilesByAdditionDate();
    const QString activeGroupId = _selectionSourceModel->activeSelectionGroupId();
    _activeGroupId = activeGroupId;

    QSet<QString> retainedPaths;
    QSet<QString> nextActiveGroupPaths;
    QHash<QString, QDateTime> nextSelectionAddedAt;
    QList<ImageFile *> nextItems;
    nextItems.reserve(selectedFiles.size());
    int nextTotalPathCount = 0;
    for (const PersistentSelectionCache::SelectedFile &selectedFile : selectedFiles) {
        const QString normalizedPath =
            QFileInfo(selectedFile.path).absoluteFilePath();
        nextSelectionAddedAt.insert(normalizedPath, selectedFile.addedAt);
        const bool belongsToActiveGroup =
            selectedFile.groupId == activeGroupId;
        if (belongsToActiveGroup) {
            nextTotalPathCount++;
            nextActiveGroupPaths.insert(normalizedPath);
        }

        const QFileInfo fileInfo(selectedFile.path);
        if (!fileInfo.isFile() || !ThumbnailLoader::isFormatSupported(fileInfo.fileName())) {
            continue;
        }

        const QString path = fileInfo.absoluteFilePath();
        retainedPaths.insert(path);
        ImageFile *item = _pathToItem.value(path, nullptr);
        if (!item) {
            item = new ImageFile(this);
            item->setFolderPath(fileInfo.absolutePath());
            item->setFileName(fileInfo.fileName());
            item->setIsImage(true);
            item->setIsFolder(false);
            item->setIconPath("qrc:/resources/ImageIcon.svg");
            item->setInfo(ImageInfo{
                .path = path,
                .lastModified = fileInfo.lastModified(),
                .fileSize = fileInfo.size(),
            });
            _pathToItem.insert(path, item);
        }
        item->setSelectionGroupId(selectedFile.groupId);
        item->setSelectionGroupColor(
            QColor(PersistentSelectionCache::colorForGroup(selectedFile.groupId)));
        if (belongsToActiveGroup) {
            nextItems.append(item);
        }
    }

    QList<ImageFile *> removedItems;
    for (auto it = _pathToItem.begin(); it != _pathToItem.end();) {
        if (!retainedPaths.contains(it.key())) {
            ImageFile *item = it.value();
            const QString imageId = item->imageIdUrl().section('/', -1);
            _imageIdToItem.remove(imageId);
            _providerImageStore->remove(imageId);
            _viewerImageCache.remove(item->fullPath());
            removedItems.append(item);
            it = _pathToItem.erase(it);
        }
        else {
            ++it;
        }
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _activeGroupPaths = nextActiveGroupPaths;
    _selectionAddedAt = std::move(nextSelectionAddedAt);
    _totalPathCount = nextTotalPathCount;
    _unavailableCount = qMax(0, nextTotalPathCount - nextItems.size());
    beginResetModel();
    _items = nextItems;
    for (ImageFile *item : std::as_const(_pathToItem)) {
        item->setIndex(-1);
    }
    for (int i = 0; i < _items.size(); i++) {
        _items[i]->setIndex(i);
    }
    endResetModel();
    if (!_currentViewerPath.isEmpty() &&
        indexForPath(_currentViewerPath) == -1) {
        _currentViewerPath.clear();
        emit viewerReset();
    }
    emit activeGroupContentsChanged();

    for (ImageFile *item : removedItems) {
        item->deleteLater();
    }
    emit panelSelectionChanged();
    bool hasMissingActiveInfo = false;
    for (const ImageFile *item : std::as_const(_items)) {
        if (!item->fullSize().isValid()) {
            hasMissingActiveInfo = true;
            break;
        }
    }
    if (hasMissingActiveInfo) {
        // Let MasonryLayout attach to the model before image metadata starts
        // arriving. Its normal ImageFullSizeRole/TimeToFlushRole path then
        // schedules thumbnail decoding exactly like the main gallery.
        _imageInfoRequestTimer.start();
    }
}

void SelectedImagesModel::syncPathsFromPersistentSelection(
    const QStringList &paths) {
    // MasonryLayout rebuilds after structural row changes. A single path is
    // cheap and truly incremental; one reset is faster for a large batch than
    // rebuilding the layout once per inserted/removed row.
    constexpr qsizetype IncrementalPathLimit = 1;
    if (paths.isEmpty() || paths.size() > IncrementalPathLimit) {
        syncFromPersistentSelection();
        return;
    }

    bool contentsChanged = false;
    bool panelSelectionChangedValue = false;
    bool needsImageInfo = false;
    for (const QString &changedPath : paths) {
        const QFileInfo pathInfo(changedPath);
        const QString path = pathInfo.absoluteFilePath();
        const bool wasActive = _activeGroupPaths.contains(path);

        PersistentSelectionCache::SelectedFile selectedFile;
        const bool isSelected =
            PersistentSelectionCache::selectedFile(path, selectedFile);
        const bool isActive =
            isSelected && selectedFile.groupId == _activeGroupId;
        if (isSelected) {
            _selectionAddedAt.insert(path, selectedFile.addedAt);
        }
        else {
            _selectionAddedAt.remove(path);
        }
        if (wasActive != isActive) {
            if (isActive) {
                _activeGroupPaths.insert(path);
            }
            else {
                _activeGroupPaths.remove(path);
            }
            contentsChanged = true;
        }

        const bool isAvailableImage =
            isSelected && pathInfo.isFile() &&
            ThumbnailLoader::isFormatSupported(pathInfo.fileName());
        ImageFile *item = _pathToItem.value(path, nullptr);
        int activeRow = item ? item->index() : -1;
        if (activeRow < 0 || activeRow >= _items.size() ||
            _items.value(activeRow) != item) {
            activeRow = -1;
        }

        if (isAvailableImage && !item) {
            item = new ImageFile(this);
            item->setFolderPath(pathInfo.absolutePath());
            item->setFileName(pathInfo.fileName());
            item->setIsImage(true);
            item->setIsFolder(false);
            item->setIconPath(QStringLiteral(
                "qrc:/resources/ImageIcon.svg"));
            item->setInfo(ImageInfo{
                .path = path,
                .lastModified = pathInfo.lastModified(),
                .fileSize = pathInfo.size(),
            });
            _pathToItem.insert(path, item);
        }

        if (item && isSelected) {
            item->setSelectionGroupId(selectedFile.groupId);
            item->setSelectionGroupColor(QColor(
                PersistentSelectionCache::colorForGroup(
                    selectedFile.groupId)));
        }

        if (isActive && isAvailableImage && activeRow == -1) {
            int row = 0;
            while (row < _items.size()) {
                const QString existingPath = _items[row]->fullPath();
                const QDateTime existingAddedAt =
                    _selectionAddedAt.value(existingPath);
                if (selectedFile.addedAt != existingAddedAt
                    ? selectedFile.addedAt < existingAddedAt
                    : QString::compare(path, existingPath,
                                       Qt::CaseInsensitive) < 0) {
                    break;
                }
                ++row;
            }
            beginInsertRows(QModelIndex(), row, row);
            _items.insert(row, item);
            for (int i = row; i < _items.size(); ++i) {
                _items[i]->setIndex(i);
            }
            endInsertRows();
            contentsChanged = true;
            panelSelectionChangedValue = true;
            needsImageInfo |= !item->fullSize().isValid();
        }
        else if ((!isActive || !isAvailableImage) && activeRow != -1) {
            beginRemoveRows(QModelIndex(), activeRow, activeRow);
            _items.removeAt(activeRow);
            item->setIndex(-1);
            for (int i = activeRow; i < _items.size(); ++i) {
                _items[i]->setIndex(i);
            }
            endRemoveRows();
            contentsChanged = true;
            panelSelectionChangedValue = true;
        }

        if (item && (!isSelected || !isAvailableImage)) {
            const QString imageId =
                item->imageIdUrl().section(QLatin1Char('/'), -1);
            _imageIdToItem.remove(imageId);
            _providerImageStore->remove(imageId);
            _viewerImageCache.remove(item->fullPath());
            _pathToItem.remove(path);
            item->deleteLater();
        }
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _totalPathCount = _activeGroupPaths.size();
    _unavailableCount = qMax(0, _totalPathCount - _items.size());
    if (!_currentViewerPath.isEmpty() &&
        indexForPath(_currentViewerPath) == -1) {
        _currentViewerPath.clear();
        emit viewerReset();
    }
    if (contentsChanged) {
        emit activeGroupContentsChanged();
    }
    if (panelSelectionChangedValue) {
        emit panelSelectionChanged();
    }
    if (needsImageInfo) {
        _imageInfoRequestTimer.start();
    }
}

void SelectedImagesModel::requestMissingImageInfo() {
    QList<QString> paths;
    for (const ImageFile *item : _items) {
        if (!item->fullSize().isValid()) {
            paths.append(item->fullPath());
        }
    }
    if (!paths.isEmpty()) {
        _decodeManager->readImagesInfo(paths, false);
    }
}

ImageFile *SelectedImagesModel::applyImageInfo(const ImageInfo &info) {
    ImageFile *item = _pathToItem.value(QFileInfo(info.path).absoluteFilePath(), nullptr);
    if (!item || !info.imageSize.isValid()) {
        return nullptr;
    }

    ImageInfo itemInfo = info;
    if (itemInfo.fileSize < 0) {
        itemInfo.fileSize = item->fileSize();
    }
    if (!itemInfo.lastModified.isValid()) {
        itemInfo.lastModified = item->lastModified();
    }
    item->setInfo(itemInfo);
    item->setFullSize(rotateToOrientation(itemInfo.imageSize, itemInfo.orientation));
    return item;
}

void SelectedImagesModel::onImageInfoAvailable(const ImageInfo &info) {
    ImageFile *item = applyImageInfo(info);
    if (!item) {
        return;
    }
    if (item->index() < 0 || item->index() >= _items.size() ||
        _items[item->index()] != item) {
        return;
    }
    const QModelIndex modelIndex = index(item->index(), 0);
    if (modelIndex.isValid()) {
        QList<int> roles{FileListModel::ImageFullSizeRole};
        if (info.isLast) {
            roles.append(FileListModel::TimeToFlushRole);
        }
        emit dataChanged(modelIndex, modelIndex, roles);
    }
}

void SelectedImagesModel::onImagesInfoAvailable(const QList<ImageInfo> &results) {
    int firstRow = _items.size();
    int lastRow = -1;
    for (const ImageInfo &info : results) {
        ImageFile *item = applyImageInfo(info);
        if (item && item->index() >= 0 && item->index() < _items.size() &&
            _items[item->index()] == item) {
            firstRow = qMin(firstRow, item->index());
            lastRow = qMax(lastRow, item->index());
        }
    }
    if (lastRow < firstRow) {
        return;
    }

    emit dataChanged(index(firstRow, 0), index(lastRow, 0),
                     {FileListModel::ImageFullSizeRole});
    emit dataChanged(index(lastRow, 0), index(lastRow, 0),
                     {FileListModel::TimeToFlushRole});
}

void SelectedImagesModel::onImageAvailable(const ImageDecodeRequest &request, const QImage &image,
                                            const DecodedImageInfo &decodedInfo) {
    ImageFile *item = _pathToItem.value(QFileInfo(request.info.path).absoluteFilePath(), nullptr);
    if (!item || image.isNull()) {
        return;
    }
    if (request.viewerRequest) {
        const ViewerImageCache::StoredImage storedImage =
            _viewerImageCache.storeDecodedImage(request, image, decodedInfo);
        if (!storedImage.accepted) {
            return;
        }
        const int itemIndex = indexForPath(item->fullPath());
        if (itemIndex != -1) {
            emit viewerImageCacheChanged(itemIndex);
        }
        if (item->fullPath() == _currentViewerPath) {
            emit viewerImageIdUrlChanged(storedImage.url,
                                         storedImage.level);
        }
        return;
    }
    if (decodedInfo.isFromCache &&
        (image.width() <= item->image().width() || image.height() <= item->image().height())) {
        return;
    }

    const QString previousId = item->imageIdUrl().section('/', -1);
    if (!previousId.isEmpty()) {
        _imageIdToItem.remove(previousId);
        _providerImageStore->remove(previousId);
    }
    const QString imageId = QString("selected-%1").arg(_lastImageId++);
    item->setImage(image);
    item->setIsCachedThumbnail(decodedInfo.isFromCache);
    _imageIdToItem.insert(imageId, item);
    _providerImageStore->publish(imageId, image);
    item->setImageId(imageId);

    if (item->index() < 0 || item->index() >= _items.size() ||
        _items[item->index()] != item) {
        return;
    }
    const QModelIndex modelIndex = index(item->index(), 0);
    if (modelIndex.isValid()) {
        emit dataChanged(modelIndex, modelIndex, {FileListModel::ImageIdUrlRole});
    }
}

void SelectedImagesModel::emitSelectionDataChanged() {
    if (!_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(_items.size() - 1, 0), {FileListModel::SelectedRole});
    }
    emit panelSelectionChanged();
}

int SelectedImagesModel::indexForPath(const QString &path) const {
    for (int i = 0; i < _items.size(); ++i) {
        if (_items[i]->fullPath() == path) {
            return i;
        }
    }
    return -1;
}
