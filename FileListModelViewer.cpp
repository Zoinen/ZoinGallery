#include "FileListModelPrivate.h"

bool FileListModel::isImage(const QString &fileName) {
    return ThumbnailLoader::isFormatSupported(fileName);
}
void FileListModel::openImageDirectly(const QString &path, int width, int height) {
    const QString imagePath = imageSourceAccessEnabled()
        ? normalizePathArgument(path)
        : normalizePathArgumentWithoutFileAccess(path);

    QFileInfo fileInfo(imagePath);
    if ((imageSourceAccessEnabled() && !fileInfo.isFile()) || !isImage(fileInfo.fileName())) {
        emit directOpenFailed(imagePath);
        return;
    }

    const QString folderPath = fileInfo.dir().absolutePath();
    const QString fileName = fileInfo.fileName();
    const QString fullPath = QDir(folderPath).absoluteFilePath(fileName);

    const QString rootKey = fileSystemPathKey(_root);
    const bool keepPendingFolderRefresh =
        fileSystemPathKey(folderPath) == rootKey &&
        (_folderRefreshTimer.isActive() ||
         _folderRefreshPendingAfterDirectOpen ||
         _folderScansInFlight.contains(rootKey) ||
         _folderRescanAfterInFlight.contains(rootKey));
    _folderRefreshTimer.stop();
    ++_folderRefreshGeneration;
    _folderRefreshPendingAfterDirectOpen = keepPendingFolderRefresh;
    _hasCurrentViewerRequest = false;

    const int generation = _directOpen.generation + 1;
    _directOpen = DirectOpenState();
    _directOpen.generation = generation;
    _directOpen.stage = DirectOpenStage::WaitingInfo;
    _directOpen.path = fullPath;
    _directOpen.folderPath = folderPath;
    _directOpen.fileName = fileName;
    _directOpen.viewerSize = QSize(width, height);
    emit directOpenPathChanged();

    cancelSessionRequests();

    int existingIndex = -1;
    if (!_root.isEmpty() && _root != "Computer" &&
        !QString::compare(QDir(_root).absolutePath(), folderPath, Qt::CaseInsensitive)) {
        existingIndex = fileIndex(fileName);
    }

    _directOpen.sameFolder = existingIndex >= 0;
    _directOpen.folderPopulated = _directOpen.sameFolder;
    if (_directOpen.sameFolder) {
        _directOpen.currentIndex = existingIndex;
        _currentViewIndex = existingIndex;
        emit viewerReset();
    }
    else {
        beginResetModel();
        clearModelData(true);
        _root = folderPath;

        ImageFile *item = createFileItem(folderPath, fileName,
                                         imageSourceAccessEnabled() ? fileInfo.lastModified() : QDateTime(),
                                         imageSourceAccessEnabled() ? fileInfo.size() : -1);
        item->setIndex(0);
        _items.append(item);
        if (item->isImage()) {
            _imagePaths.append(item->fullPath());
        }

        _directOpen.currentIndex = 0;
        _currentViewIndex = 0;
        loadSelectionStatesForVisibleItems();
        endResetModel();
    }

    auto itemIt = _fileToItem.find(fullPath);
    if (itemIt != _fileToItem.end() && itemIt.value()->fullSize().isValid() &&
        itemIt.value()->info().imageSize.isValid()) {
        ImageInfo info = itemIt.value()->info();
        info.directOpenGeneration = generation;
        handleDirectOpenImageInfo(info);
    }
    else {
        readImagesInfo({fullPath}, false, generation);
    }
}
QString FileListModel::directOpenPath() const {
    return _directOpen.path;
}

bool FileListModel::isActiveDirectOpenInfo(const ImageInfo &info) const {
    return info.directOpenGeneration &&
           info.directOpenGeneration == _directOpen.generation &&
           _directOpen.stage != DirectOpenStage::None;
}

bool FileListModel::isActiveDirectOpenRequest(const ImageDecodeRequest &request) const {
    return request.info.directOpenGeneration &&
           request.info.directOpenGeneration == _directOpen.generation &&
           _directOpen.stage != DirectOpenStage::None;
}

void FileListModel::handleDirectOpenImageInfo(const ImageInfo &result) {
    if (!isActiveDirectOpenInfo(result)) {
        return;
    }

    if (_directOpen.stage == DirectOpenStage::WaitingInfo && result.path == _directOpen.path) {
        if (!result.imageSize.isValid()) {
            qWarning() << "Direct open metadata is invalid" << result.path << result.imageSize;
            return;
        }

        ImageInfo directOpenInfo = result;
        auto itemIt = _fileToItem.find(result.path);
        if (itemIt != _fileToItem.end()) {
            if (directOpenInfo.fileSize < 0) {
                directOpenInfo.fileSize = itemIt.value()->fileSize();
            }
            if (!directOpenInfo.lastModified.isValid()) {
                directOpenInfo.lastModified = itemIt.value()->lastModified();
            }
        }

        _directOpen.info = directOpenInfo;
        _directOpen.info.directOpenGeneration = _directOpen.generation;
        requestDirectOpenFitDecode();
        return;
    }

    if (_directOpen.stage == DirectOpenStage::WaitingNeighborInfo &&
        _directOpen.pendingNeighborInfoPaths.contains(result.path)) {
        _directOpen.pendingNeighborInfoPaths.remove(result.path);
        if (_directOpen.pendingNeighborInfoPaths.isEmpty()) {
            requestDirectOpenNeighborDecodes();
        }
    }
}

void FileListModel::requestDirectOpenFitDecode() {
    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt == _fileToItem.end()) {
        return;
    }

    ImageFile *item = itemIt.value();
    QSize targetSize = item->fullSize();
    if (!targetSize.isValid()) {
        targetSize = rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation);
    }
    ImageInfo info = _directOpen.info;
    info.directOpenGeneration = _directOpen.generation;
    const ImageDecodeRequest request = ViewerImageCache::makeRequest(
        info, targetSize, _directOpen.viewerSize);
    if (!request.targetSize.isValid()) {
        return;
    }

    _directOpen.stage = DirectOpenStage::WaitingFitDecode;
    decodeImages({request});
}

void FileListModel::requestDirectOpenFullSizeDecode() {
    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt == _fileToItem.end()) {
        return;
    }

    ImageFile *item = itemIt.value();
    QSize targetSize = item->fullSize();
    if (!targetSize.isValid()) {
        targetSize = rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation);
    }
    if (!targetSize.isValid()) {
        return;
    }

    ImageInfo info = _directOpen.info;
    info.directOpenGeneration = _directOpen.generation;
    const ImageDecodeRequest request =
        ViewerImageCache::makeRequest(info, targetSize);
    _directOpen.stage = DirectOpenStage::WaitingFullDecode;
    decodeImages({request});
}

bool FileListModel::handleDirectOpenImageReady(const ImageDecodeRequest &request, const QImage &image,
                                               const DecodedImageInfo &decodedInfo) {
    if (!isActiveDirectOpenRequest(request)) {
        return false;
    }
    if (image.isNull()) {
        // The source-only retry owns this exact stage. Advancing here would
        // make the preserved request inactive and silently lose it.
        return true;
    }

    if (request.info.path == _directOpen.path && _directOpen.stage == DirectOpenStage::WaitingFitDecode) {
        auto itemIt = _fileToItem.find(_directOpen.path);
        if (itemIt != _fileToItem.end() && !image.isNull() && itemIt.value()->imageIdUrl().isEmpty()) {
            itemIt.value()->setImage(image, request.info);
            itemIt.value()->setIsCachedThumbnail(decodedInfo.isFromCache);
            updateImageId(itemIt.value());
        }

        if (ViewerImageCache::isFullSizeRequest(request)) {
            _directOpen.stage = DirectOpenStage::WaitingFullDecode;
            populateFolderAfterDirectOpenFullDecode();
            requestDirectOpenNeighbors();
        }
        else {
            if (_directOpen.currentIndex >= 0) {
                _directOpen.readyEmitted = true;
                emit directOpenReady(_directOpen.currentIndex);
            }
            requestDirectOpenFullSizeDecode();
        }
        return true;
    }

    if (request.info.path == _directOpen.path && _directOpen.stage == DirectOpenStage::WaitingFullDecode &&
        ViewerImageCache::isFullSizeRequest(request)) {
        populateFolderAfterDirectOpenFullDecode();
        requestDirectOpenNeighbors();
        return true;
    }

    if (_directOpen.stage == DirectOpenStage::WaitingNeighborDecode &&
        _directOpen.pendingNeighborDecodePaths.contains(request.info.path)) {
        _directOpen.pendingNeighborDecodePaths.remove(request.info.path);
        if (_directOpen.pendingNeighborDecodePaths.isEmpty()) {
            finishDirectOpenPriorityWork();
        }
        return true;
    }

    return false;
}

void FileListModel::populateFolderAfterDirectOpenFullDecode(bool notifyReady) {
    if (_directOpen.stage != DirectOpenStage::WaitingFullDecode) {
        return;
    }

    if (!_directOpen.sameFolder) {
        beginResetModel();
        clearModelData(false, false);
        _root = _directOpen.folderPath;
        _recursiveViewActive = false;
        populateFolderItems(_root, _directOpen.fileName);
        loadSelectionStatesForVisibleItems();

        auto targetIt = _fileToItem.find(_directOpen.path);
        if (targetIt != _fileToItem.end()) {
            targetIt.value()->setFullSize(rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation));
            targetIt.value()->setInfo(_directOpen.info);
        }
        endResetModel();
        _directOpen.folderPopulated = true;

        int targetIndex = -1;
        const QString targetPathKey = fileSystemPathKey(_directOpen.path);
        for (ImageFile *item : std::as_const(_items)) {
            if (item->isImage() &&
                fileSystemPathKey(item->fullPath()) == targetPathKey) {
                targetIndex = item->index();
                break;
            }
        }
        configureFolderWatcher();
        scheduleFolderRefresh();
        if (targetIndex < 0) {
            const QString vanishedPath = _directOpen.path;
            _directOpen.currentIndex = -1;
            _currentViewIndex = -1;
            finishDirectOpenPriorityWork();
            if (!vanishedPath.isEmpty() &&
                _directOpen.path == vanishedPath) {
                _directOpen.path.clear();
                emit directOpenPathChanged();
                emit directOpenFailed(vanishedPath);
            }
            emit viewerReset();
            return;
        }
        _directOpen.currentIndex = targetIndex;
    }

    _directOpen.folderPopulated = true;

    _currentViewIndex = _directOpen.currentIndex;

    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt != _fileToItem.end()) {
        ImageFile *item = itemIt.value();
        item->setFullSize(rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation));
        item->setInfo(_directOpen.info);

        const ViewerImageCache::Entry viewerEntry =
            _viewerImageCache.entryForPath(_directOpen.path, false);
        if (!viewerEntry.image.isNull() && item->imageIdUrl().isEmpty()) {
            ImageInfo thumbnailInfo = item->info();
            thumbnailInfo.lastModified = viewerEntry.sourceLastModified;
            thumbnailInfo.fileSize = viewerEntry.sourceFileSize;
            item->setImage(viewerEntry.image, thumbnailInfo);
            item->setIsCachedThumbnail(
                viewerEntry.decodedInfo.isFromCache);
            updateImageId(item);
        }

        QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
        if (modelIndex.isValid()) {
            emit dataChanged(modelIndex, modelIndex, {ImageFullSizeRole, ImageIdUrlRole});
        }
    }

    if (notifyReady && _directOpen.currentIndex >= 0) {
        _directOpen.readyEmitted = true;
        emit directOpenReady(_directOpen.currentIndex);
        emitViewerImagesForCurrentIndex();
    }
}

QList<int> FileListModel::directOpenNeighborIndexes() const {
    QList<int> result;
    if (_directOpen.currentIndex < 0 || _directOpen.currentIndex >= _items.size()) {
        return result;
    }

    for (int i = _directOpen.currentIndex - 1; i >= 0; i--) {
        if (_items[i]->isImage()) {
            result.append(i);
            break;
        }
    }
    for (int i = _directOpen.currentIndex + 1; i < _items.size(); i++) {
        if (_items[i]->isImage()) {
            result.append(i);
            break;
        }
    }
    return result;
}

void FileListModel::requestDirectOpenNeighbors() {
    QList<int> neighborIndexes = directOpenNeighborIndexes();
    QStringList pathsNeedingInfo;
    _directOpen.pendingNeighborInfoPaths.clear();

    for (int index : neighborIndexes) {
        ImageFile *item = _items[index];
        if (!item->fullSize().isValid() || !item->info().imageSize.isValid()) {
            pathsNeedingInfo.append(item->fullPath());
            _directOpen.pendingNeighborInfoPaths.insert(item->fullPath());
        }
    }

    if (!_directOpen.pendingNeighborInfoPaths.isEmpty()) {
        _directOpen.stage = DirectOpenStage::WaitingNeighborInfo;
        readImagesInfo(pathsNeedingInfo, false, _directOpen.generation);
        return;
    }

    requestDirectOpenNeighborDecodes();
}

QList<ImageDecodeRequest> FileListModel::directOpenViewerRequestsForIndexes(const QList<int> &indexes,
                                                                            QSet<QString> *queuedPaths) {
    QList<ImageDecodeRequest> requests;
    for (int index : indexes) {
        if (index < 0 || index >= _items.size() || !_items[index]->isImage()) {
            continue;
        }

        ImageFile *item = _items[index];
        const QString requestedPath = item->fullPath();
        ImageInfo info = item->info();
        info.directOpenGeneration = _directOpen.generation;
        const ImageDecodeRequest request =
            ViewerImageCache::makeRequest(
                info, item->fullSize(), _directOpen.viewerSize);
        if (!request.targetSize.isValid() ||
            !_viewerImageCache.needsDecode(request)) {
            continue;
        }

        requests.append(request);
        if (queuedPaths) {
            queuedPaths->insert(requestedPath);
        }
    }
    return requests;
}

void FileListModel::requestDirectOpenNeighborDecodes() {
    QSet<QString> queuedPaths;
    QList<ImageDecodeRequest> requests = directOpenViewerRequestsForIndexes(directOpenNeighborIndexes(), &queuedPaths);
    if (requests.isEmpty()) {
        finishDirectOpenPriorityWork();
        return;
    }

    _directOpen.pendingNeighborDecodePaths = queuedPaths;
    _directOpen.stage = DirectOpenStage::WaitingNeighborDecode;
    decodeImages(requests);
}

void FileListModel::finishDirectOpenAfterDecodeCancellation(
    int expectedGeneration, bool notifyReady) {
    if (_directOpen.generation != expectedGeneration) {
        return;
    }
    switch (_directOpen.stage) {
    case DirectOpenStage::WaitingFitDecode:
        // Metadata is complete in every decode stage, so the full folder can
        // be materialized even if the first pixels were intentionally canceled.
        _directOpen.stage = DirectOpenStage::WaitingFullDecode;
        [[fallthrough]];
    case DirectOpenStage::WaitingFullDecode:
        populateFolderAfterDirectOpenFullDecode(notifyReady);
        if (_directOpen.generation == expectedGeneration &&
            _directOpen.stage != DirectOpenStage::None) {
            finishDirectOpenPriorityWork();
        }
        break;
    case DirectOpenStage::WaitingNeighborDecode:
        // Neighbor prefetch is optional and must never block watcher refreshes.
        finishDirectOpenPriorityWork();
        break;
    case DirectOpenStage::WaitingInfo:
    case DirectOpenStage::WaitingNeighborInfo:
        // Closing the viewer abandons metadata-only direct work as well. The
        // runner may finish later, but it can no longer defer watcher refreshes.
        if (!notifyReady) {
            finishDirectOpenPriorityWork();
        }
        break;
    default:
        break;
    }
}

void FileListModel::finishDirectOpenPriorityWork() {
    if (_directOpen.stage == DirectOpenStage::None) {
        return;
    }

    const bool refreshFolder = _folderRefreshPendingAfterDirectOpen;
    const bool populateProvisionalFolder =
        !_directOpen.folderPopulated && !_directOpen.folderPath.isEmpty();
    const QString failedDirectOpenPath =
        !_directOpen.readyEmitted ? _directOpen.path : QString();
    _folderRefreshPendingAfterDirectOpen = false;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    startRegularFolderWork();
    if (populateProvisionalFolder) {
        configureFolderWatcher();
    }
    if (refreshFolder || populateProvisionalFolder) {
        scheduleFolderRefresh();
    }
    if (!failedDirectOpenPath.isEmpty()) {
        _directOpen.path.clear();
        emit directOpenPathChanged();
        emit directOpenFailed(failedDirectOpenPath);
    }
}

void FileListModel::emitViewerImagesForCurrentIndex() {
    if (_currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        return;
    }

    // Direct-open completion follows the same requested-size filtering as
    // normal viewer navigation. Never republish an older undersized Fit tier
    // merely because it exists in the retained cache.
    const auto sources = viewerImageSourcesForIndex(
        _currentViewIndex, _currentViewerRequestSize);
    for (const auto &[url, level] : sources) {
        emit viewerImageIdUrlChanged(url, level);
    }
}

void FileListModel::requestViewer(int index, int width, int height) {
    requestViewerInOrder(index, {}, width, height);
}

void FileListModel::requestViewerInOrder(
    int index, const QVariantList &orderedSourceRows,
    int width, int height) {
    if (index < 0 || index >= _items.size()) {
        return;
    }

    _currentViewIndex = index;
    _currentViewerRequestSize = QSize(width, height);
    _hasCurrentViewerRequest = true;
    if (!_items[index]->imageIdUrl().isEmpty()) {
        emit viewerImageIdUrlChanged(_items[index]->imageIdUrl(), 0);
    }

    QList<ImageFile *> prioritizedItems;
    prioritizedItems.reserve(orderedSourceRows.size());
    for (const QVariant &rowValue : orderedSourceRows) {
        bool ok = false;
        const int sourceRow = rowValue.toInt(&ok);
        if (!ok || sourceRow < 0 || sourceRow >= _items.size()) {
            continue;
        }
        ImageFile *item = _items.at(sourceRow);
        if (item && item->isImage() &&
            !prioritizedItems.contains(item)) {
            prioritizedItems.append(item);
        }
    }
    if (!prioritizedItems.isEmpty() &&
        prioritizedItems.first() != _items.at(index)) {
        prioritizedItems.removeAll(_items.at(index));
        prioritizedItems.prepend(_items.at(index));
    }

    // The explicit list is already in viewer priority order
    // (current, previous, next, ...). Starting planRequest at its first row
    // consumes that edge-anchored list in exactly the supplied order.
    const ViewerImageCache::RequestPlan requestPlan =
        prioritizedItems.isEmpty()
        ? _viewerImageCache.planRequest(
              _items, index, QSize(width, height))
        : _viewerImageCache.planRequest(
              prioritizedItems, 0, QSize(width, height),
              prioritizedItems.size());
    for (const auto &[url, level] : requestPlan.cachedImages) {
        emit viewerImageIdUrlChanged(url, level);
    }
    decodeImages(requestPlan.decodeRequests);
}

void FileListModel::requestViewerAt(
    int index, int width, int height) {
    if (index < 0 || index >= _items.size() ||
        !_items.at(index)->isImage()) {
        return;
    }

    // A swipe may expose a neighbor before it becomes the authoritative
    // current row. Prepare that one frame for the requested presentation target
    // without replacing the active current row or its ordered prefetch plan.
    const QList<ImageFile *> targetItems{_items.at(index)};
    const ViewerImageCache::RequestPlan requestPlan =
        _viewerImageCache.planRequest(
            targetItems, 0, QSize(width, height), 1);
    decodeImages(requestPlan.decodeRequests);
}

QString FileListModel::bestViewerImageUrlForIndex(int index) const {
    const auto sources = viewerImageSourcesForIndex(index);
    return sources.isEmpty() ? QString() : sources.constLast().first;
}

QString FileListModel::preparedViewerImageUrlForIndex(
    int index, int width, int height) const {
    if (index < 0 || index >= _items.size()) {
        return {};
    }

    const bool fitRequest = width > 0 && height > 0;
    const int requiredLevel = fitRequest ? 1 : 2;
    const auto sources = _viewerImageCache.imageSources(
        _items.at(index), QSize(width, height));
    for (auto it = sources.crbegin(); it != sources.crend(); ++it) {
        if (it->second == requiredLevel) {
            return it->first;
        }
    }
    return {};
}

QSize FileListModel::viewerImageOriginalSizeForIndex(int index) const {
    return index >= 0 && index < _items.size() && _items.at(index)
        ? _items.at(index)->fullSize() : QSize();
}

QList<QPair<QString, int>> FileListModel::viewerImageSourcesForIndex(
    int index, const QSize &viewerSize) const {
    if (index < 0 || index >= _items.size()) {
        return {};
    }
    QSize effectiveViewerSize = viewerSize;
    if (!effectiveViewerSize.isValid() &&
        _hasCurrentViewerRequest &&
        _currentViewerRequestSize.isValid()) {
        effectiveViewerSize = _currentViewerRequestSize;
    }
    return _viewerImageCache.imageSources(_items[index],
                                          effectiveViewerSize);
}

QImage FileListModel::viewerForImageId(const QString &imageId) {
    return _viewerImageCache.viewerImageForId(imageId);
}

QImage FileListModel::fullSizeViewerForImageId(const QString &imageId) {
    return _viewerImageCache.fullSizeImageForId(imageId);
}

void FileListModel::cancelAllRunners() {
    const QString canceledDirectOpenPath =
        _directOpen.stage != DirectOpenStage::None &&
                !_directOpen.readyEmitted
            ? _directOpen.path : QString();
    const bool hadDirectOpenPath = !_directOpen.path.isEmpty();
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.path.clear();
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    if (hadDirectOpenPath) {
        emit directOpenPathChanged();
    }
    if (!canceledDirectOpenPath.isEmpty()) {
        emit directOpenFailed(canceledDirectOpenPath);
    }
    cancelSessionRequests();
    if (_folderRefreshPendingAfterDirectOpen) {
        _folderRefreshPendingAfterDirectOpen = false;
        scheduleFolderRefresh();
    }
}

void FileListModel::cancelAllDecodeRunners() {
    // qDebug() << __FUNCTION__;
    const int directOpenGeneration = _directOpen.generation;
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllDecodeRunners();
    }
    else {
        // Preserve the original method contract: metadata and folder work are
        // not decode runners, so a session-scoped cancellation must only stop
        // thumbnail and viewer decode stages.
        _decodeManager->cancelThumbnailRequests(_requestNamespace);
        _decodeManager->cancelViewerRequests(_requestNamespace);
    }
    _failedImageDecodeRequests.clear();
    _failedImageDecodeRetryAttempts.clear();
    finishDirectOpenAfterDecodeCancellation(directOpenGeneration, true);
}

void FileListModel::cancelAllDecodeViewerRunners() {
    const int directOpenGeneration = _directOpen.generation;
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllDecodeViewerRunners();
    }
    else {
        _decodeManager->cancelViewerRequests(_requestNamespace);
    }
    for (auto it = _failedImageDecodeRequests.begin();
         it != _failedImageDecodeRequests.end();) {
        if (it.value().viewerRequest) {
            _failedImageDecodeRetryAttempts.remove(it.key());
            it = _failedImageDecodeRequests.erase(it);
        } else {
            ++it;
        }
    }
    finishDirectOpenAfterDecodeCancellation(directOpenGeneration, true);
}

void FileListModel::cancelAllDecodeViewerRunnersForViewerClose() {
    const int directOpenGeneration = _directOpen.generation;
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllDecodeViewerRunners();
    }
    else {
        // Closing the viewer must not throw away already queued thumbnail or
        // metadata work for the same standalone/embedded catalog.  The old
        // monolithic model canceled viewer prefetch only.
        _decodeManager->cancelViewerRequests(_requestNamespace);
    }
    for (auto it = _failedImageDecodeRequests.begin();
         it != _failedImageDecodeRequests.end();) {
        if (it.value().viewerRequest) {
            _failedImageDecodeRetryAttempts.remove(it.key());
            it = _failedImageDecodeRequests.erase(it);
        } else {
            ++it;
        }
    }
    finishDirectOpenAfterDecodeCancellation(directOpenGeneration, false);
}

void FileListModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    QList<ImageDecodeRequest> namespacedRequests = requests;
    for (ImageDecodeRequest &request : namespacedRequests) {
        request.requestNamespace = _requestNamespace;
        request.info.requestNamespace = _requestNamespace;
    }
    _decodeManager->decodeImages(namespacedRequests);
}

int FileListModel::fileIndex(const QString &fileName) const {
    for (int i = 0; i < _items.size(); i++) {
        if (!_items[i]->fileName().compare(fileName, Qt::CaseInsensitive)) {
            return i;
        }
    }
    return -1;
}

RootProxyModel::RootProxyModel(QObject *parent)
    : QAbstractProxyModel(parent) {
    _sourceRoot = nullptr;
}

void RootProxyModel::setRoot(ImageFile *root) {
    if (_sourceRoot == root) {
        return;
    }
    if (_sourceResetActive) {
        _sourceRoot = root;
        return;
    }
    beginResetModel();
    _sourceRoot = root;
    endResetModel();
}

void RootProxyModel::setSourceModel(QAbstractItemModel *sourceModel) {
    if (QAbstractProxyModel::sourceModel() == sourceModel) {
        return;
    }
    if (QAbstractProxyModel::sourceModel()) {
        disconnect(QAbstractProxyModel::sourceModel(), nullptr,
                   this, nullptr);
    }
    _sourceResetActive = false;
    _sourceInsertActive = false;
    _sourceRemoveActive = false;
    _sourceMoveActive = false;
    QAbstractProxyModel::setSourceModel(sourceModel);

    if (!sourceModel) {
        return;
    }

    // QAbstractProxyModel already resets itself together with its source. Drop
    // the raw root identity inside that transaction so clearModelData() can
    // destroy the ImageFile without starting a nested proxy reset.
    connect(sourceModel, &QAbstractItemModel::modelAboutToBeReset,
            this, [this]() {
        _sourceResetActive = true;
        _sourceRoot = nullptr;
    });
    connect(sourceModel, &QAbstractItemModel::modelReset,
            this, [this]() { _sourceResetActive = false; });

    connect(sourceModel, &QAbstractItemModel::dataChanged, this,
            [&] (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles = QList<int>()) {
        if (_sourceRoot &&
            FileListModel::itemFromIndex(topLeft.parent()) == _sourceRoot) {
            emit dataChanged(mapFromSource(topLeft), mapFromSource(bottomRight), roles);
        }
    });

    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeInserted,
            this, [this](const QModelIndex &parent, int first, int last) {
        _sourceInsertActive = false;
        if (!_sourceRoot ||
            FileListModel::itemFromIndex(parent) != _sourceRoot) {
            return;
        }
        beginInsertRows(QModelIndex(), first, last);
        _sourceInsertActive = true;
    });
    connect(sourceModel, &QAbstractItemModel::rowsInserted,
            this, [this](const QModelIndex &, int, int) {
        if (!_sourceInsertActive) {
            return;
        }
        _sourceInsertActive = false;
        endInsertRows();
    });

    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, [this](const QModelIndex &parent, int first, int last) {
        _sourceRemoveActive = false;
        if (!parent.isValid() && _sourceRoot &&
            _sourceRoot->index() >= first && _sourceRoot->index() <= last) {
            // The top-level folder itself is leaving the source model. Make
            // this flat proxy empty while the old index still identifies that
            // folder; after the mutation the same row may belong to another
            // item.
            beginResetModel();
            _sourceRoot = nullptr;
            endResetModel();
            return;
        }
        if (!_sourceRoot ||
            FileListModel::itemFromIndex(parent) != _sourceRoot) {
            return;
        }
        beginRemoveRows(QModelIndex(), first, last);
        _sourceRemoveActive = true;
    });
    connect(sourceModel, &QAbstractItemModel::rowsRemoved,
            this, [this](const QModelIndex &, int, int) {
        if (!_sourceRemoveActive) {
            return;
        }
        _sourceRemoveActive = false;
        endRemoveRows();
    });

    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeMoved,
            this, [this](const QModelIndex &sourceParent, int sourceFirst,
                         int sourceLast,
                         const QModelIndex &destinationParent,
                         int destinationChild) {
        _sourceMoveActive = false;
        if (!_sourceRoot ||
            FileListModel::itemFromIndex(sourceParent) != _sourceRoot ||
            FileListModel::itemFromIndex(destinationParent) != _sourceRoot) {
            return;
        }
        _sourceMoveActive = beginMoveRows(
            QModelIndex(), sourceFirst, sourceLast,
            QModelIndex(), destinationChild);
    });
    connect(sourceModel, &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &, int, int,
                         const QModelIndex &, int) {
        if (!_sourceMoveActive) {
            return;
        }
        _sourceMoveActive = false;
        endMoveRows();
    });
}

QModelIndex RootProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (!_sourceRoot || !sourceModel() || parent.isValid() ||
        row < 0 || row >= _sourceRoot->subfiles().size() || column != 0) {
        return QModelIndex();
    }
    return createIndex(row, column, _sourceRoot->subfiles().at(row));
}

QModelIndex RootProxyModel::parent(const QModelIndex &child) const {
    return QModelIndex();
}

int RootProxyModel::rowCount(const QModelIndex &parent) const {
    if (!_sourceRoot || !sourceModel() || parent.isValid()) {
        return 0;
    }
    return sourceModel()->rowCount(sourceModel()->indexFromItem(_sourceRoot));
}

int RootProxyModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

QModelIndex RootProxyModel::mapToSource(const QModelIndex &proxyIndex) const {
    if (!_sourceRoot || !sourceModel()) {
        return QModelIndex();
    }

    return sourceModel()->index(proxyIndex.row(), proxyIndex.column(), sourceModel()->indexFromItem(_sourceRoot));
}

QModelIndex RootProxyModel::mapFromSource(const QModelIndex &sourceIndex) const {
    if (!_sourceRoot || !sourceModel()) {
        return QModelIndex();
    }

    if (sourceIndex.parent() != sourceModel()->indexFromItem(_sourceRoot)) {
        return QModelIndex();
    }

    return index(sourceIndex.row());
}

FileListModel *RootProxyModel::sourceModel() const {
    return static_cast<FileListModel *>(QAbstractProxyModel::sourceModel());
}

ImageFile *RootProxyModel::rootItem() const {
    return _sourceRoot;
}

void RootProxyModel::cancelAllRunners() {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ZoinGallery::GalleryCatalogSource *>(sourceModel())
        ->cancelAllRunners();
}

void RootProxyModel::cancelAllDecodeRunners() {
    if (!sourceModel()) {
        return;
    }
    qDebug() << __FUNCTION__;
    dynamic_cast<ZoinGallery::GalleryCatalogSource *>(sourceModel())
        ->cancelAllDecodeRunners();
}

bool RootProxyModel::preserveViewStateOnReset() const {
    return sourceModel() && sourceModel()->preserveViewStateOnReset();
}

void RootProxyModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ZoinGallery::GalleryCatalogSource *>(sourceModel())
        ->decodeImages(requests);
}
