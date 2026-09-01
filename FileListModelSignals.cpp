#include "FileListModelPrivate.h"

struct FileListModel::ImageInfoBatchUpdate {
    QList<ImageDecodeRequest> decodeRequests;
    ImageFile *flushItem = nullptr;
    ImageFile *currentViewerMetadataItem = nullptr;
    bool foundCurrentItem = false;
    bool flushTopLevelFallback = false;
    QStringList directSourceVersionChanges;
    QHash<ImageFile *, QList<int>> changedRowsByParent;
};

struct FileListModel::FolderPreviewUpdate {
    QString path;
    QList<FileInfo> sourceRows;
    ImageFile *item = nullptr;
    QModelIndex parentIndex;
    QList<ImageFile *> oldRows;
    QList<ImageFile *> nextRows;
    QList<ImageFile *> removedRows;
    QHash<QString, ImageFile *> oldByPath;
    QSet<ImageFile *> retainedRows;
    QHash<ImageFile *, ImageInfo> metadataUpdates;
    QStringList imageInfoPaths;
    QStringList changedPaths;
    QSet<QString> affectedPaths;
    bool oldFolderView = false;
    bool structureChanged = false;
    bool previousPreserveState = false;
};

void FileListModel::initializeRuntimeSettings() {
    if (qApp) {
        qApp->installEventFilter(this);
    }
    _lastId = 0;
    _currentViewIndex = -1;

    QSettings settings;
    _imageCacheMode = cacheUsageModeFromInt(
        settings.value(ImageCacheModeSettingsKey, static_cast<int>(CacheUsageMode::On)).toInt());
    _fileListCacheMode = cacheUsageModeFromInt(
        settings.value(FileListCacheModeSettingsKey, static_cast<int>(CacheUsageMode::On)).toInt());
    if (_ownsDecodeManager) {
        _decodeManager->setImageCacheMode(_imageCacheMode);
        _decodeManager->setFileListCacheMode(_fileListCacheMode);
    }
    else {
        _imageCacheMode = _decodeManager->imageCacheMode();
        _fileListCacheMode = _decodeManager->fileListCacheMode();
    }
}

void FileListModel::connectRuntimeStatusSignals() {
    connect(_decodeManager, &DecodeManager::viewerRunnerCanceled, this,
            [this](const QString &path, const QString &requestNamespace) {
        if (acceptsRequestNamespace(requestNamespace)) {
            _viewerImageCache.removeIncomplete(path);
        }
    });

    // A reusable session can be destroyed while the runtime-owned shared
    // DecodeManager keeps running for another panel.  Give this functor the
    // model as its QObject context so Qt disconnects it before `this` dies.
    connect(_decodeManager, &DecodeManager::runningTasksChanged, this,
            [this] (const QString &runningTasks,
                    const QStringList &tasksInfo) {
        if (runningTasksDebug()) {
            QFile f(QString("C:\\tmp\\log\\%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
            (void)f.open(QFile::WriteOnly);
            f.write(runningTasks.toLatin1() + "\n");
            QByteArray ba;
            for (QString task : tasksInfo) {
                ba.append(task.toUtf8());
                ba.append("\n");
            }
            f.write(ba);
        }
        emit runningTasksChanged(runningTasks, tasksInfo);
    });
}

void FileListModel::connectImageInfoSignal() {
    connect(_decodeManager, &DecodeManager::imageInfoReady, this, [&] (const ImageInfo &result) {
        if (!acceptsRequestNamespace(result.requestNamespace)) {
            return;
        }
        if (result.directOpenGeneration && result.directOpenGeneration != _directOpen.generation) {
            return;
        }
        auto it = _fileToItem.find(result.path);
        // qDebug() << "INFO RECEIVED" << result.path << result.imageSize;
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (!item->isImage()) {
                if (result.isLast && !result.isFromEmbeddedView) {
                    emitThumbnailInfoFlush();
                }
                return;
            }
            if (!result.imageSize.isValid()) {
                rememberFailedImageInfo(result);
                if (result.isLast && !result.isFromEmbeddedView) {
                    emitThumbnailInfoFlush();
                }
                handleDirectOpenImageInfo(result);
                return;
            }
            if (!isCurrentFileVersion(item, result) &&
                !isActiveDirectOpenInfo(result)) {
                if (result.isLast && !result.isFromEmbeddedView) {
                    emitThumbnailInfoFlush();
                }
                return;
            }
            _failedImageInfoRequests.remove(infoRetryKey(result));
            _failedImageInfoRetryAttempts.remove(infoRetryKey(result));
            _failedImageWorkRetryDelayMs = FailedImageWorkRetryInitialMs;
            ImageInfo itemInfo = result;
            if (itemInfo.fileSize < 0) {
                itemInfo.fileSize = item->fileSize();
            }
            if (!itemInfo.lastModified.isValid()) {
                itemInfo.lastModified = item->lastModified();
            }
            const bool directSourceVersionChanged =
                isActiveDirectOpenInfo(result) &&
                (item->lastModified() != itemInfo.lastModified ||
                 item->fileSize() != itemInfo.fileSize);
            item->setFullSize(rotateToOrientation(itemInfo.imageSize, itemInfo.orientation));
            item->setInfo(itemInfo);
            QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
            if (!modelIndex.isValid()) {
                qDebug() << "Invalid model index" << item->index() << item->imageFileParent() << item->fullPath();
                return;
            }
            QList<int> roles = {ImageFullSizeRole};
            if (result.isLast) {
                roles.append(TimeToFlushRole);
            }
            emit dataChanged(modelIndex, modelIndex, roles);
            if (directSourceVersionChanged) {
                emit watchedImageMetadataChanged({item->fullPath()});
            }
            refreshCurrentViewerAfterMetadata(item);

            if (result.isFromEmbeddedView) {
                decodeImages({imageDecodeRequestFromEmbeddedImageInfo(itemInfo)});
            }

            handleDirectOpenImageInfo(itemInfo);
        }
        else {
            qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
            if (result.isLast && !result.isFromEmbeddedView) {
                emitThumbnailInfoFlush();
            }
        }
    });
}

void FileListModel::connectImageInfosSignal() {
    connect(_decodeManager, &DecodeManager::imagesInfoReady, this,
            &FileListModel::handleImageInfosReady);
}

QList<ImageInfo> FileListModel::acceptedImageInfos(
    const QList<ImageInfo> &results) const {
    QList<ImageInfo> accepted;
    accepted.reserve(results.size());
    for (const ImageInfo &result : results) {
        if (acceptsRequestNamespace(result.requestNamespace)
            && (!result.directOpenGeneration
                || result.directOpenGeneration == _directOpen.generation)) {
            accepted.append(result);
        }
    }
    return accepted;
}

void FileListModel::handleImageInfosReady(
    const QList<ImageInfo> &results) {
    if (results.isEmpty()) {
        return;
    }
    const QList<ImageInfo> currentResults = acceptedImageInfos(results);
    if (currentResults.isEmpty()) {
        return;
    }

    ImageInfoBatchUpdate update;
    for (const ImageInfo &result : currentResults) {
        applyImageInfoBatchResult(&update, result);
    }
    if (!update.foundCurrentItem) {
        if (update.flushTopLevelFallback) {
            emitThumbnailInfoFlush();
        }
        return;
    }
    emitImageInfoBatchRowChanges(update);
    finishImageInfoBatch(&update);
}

void FileListModel::applyImageInfoBatchResult(
    ImageInfoBatchUpdate *update, const ImageInfo &result) {
    auto itemIt = _fileToItem.find(result.path);
    if (itemIt == _fileToItem.end()) {
        qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
        update->flushTopLevelFallback |=
            result.isLast && !result.isFromEmbeddedView;
        return;
    }
    ImageFile *item = itemIt.value();
    if (!item->isImage()) {
        update->flushTopLevelFallback |=
            result.isLast && !result.isFromEmbeddedView;
        return;
    }
    if (!result.imageSize.isValid()) {
        rememberFailedImageInfo(result);
        update->flushTopLevelFallback |=
            result.isLast && !result.isFromEmbeddedView;
        handleDirectOpenImageInfo(result);
        return;
    }
    if (!isCurrentFileVersion(item, result)
        && !isActiveDirectOpenInfo(result)) {
        update->flushTopLevelFallback |=
            result.isLast && !result.isFromEmbeddedView;
        return;
    }

    _failedImageInfoRequests.remove(infoRetryKey(result));
    _failedImageInfoRetryAttempts.remove(infoRetryKey(result));
    _failedImageWorkRetryDelayMs = FailedImageWorkRetryInitialMs;
    update->foundCurrentItem = true;
    ImageInfo itemInfo = result;
    if (itemInfo.fileSize < 0) {
        itemInfo.fileSize = item->fileSize();
    }
    if (!itemInfo.lastModified.isValid()) {
        itemInfo.lastModified = item->lastModified();
    }
    const bool sourceVersionChanged = isActiveDirectOpenInfo(result)
        && (item->lastModified() != itemInfo.lastModified
            || item->fileSize() != itemInfo.fileSize);
    item->setFullSize(rotateToOrientation(
        itemInfo.imageSize, itemInfo.orientation));
    item->setInfo(itemInfo);
    if (sourceVersionChanged) {
        update->directSourceVersionChanges.append(item->fullPath());
    }
    update->changedRowsByParent[item->imageFileParent()].append(item->index());
    if (result.isLast) {
        update->flushItem = item;
    }
    if (result.isFromEmbeddedView) {
        update->decodeRequests.append(
            imageDecodeRequestFromEmbeddedImageInfo(itemInfo));
    }
    handleDirectOpenImageInfo(itemInfo);
    if (_currentViewIndex >= 0 && _currentViewIndex < _items.size()
        && _items.at(_currentViewIndex) == item) {
        update->currentViewerMetadataItem = item;
    }
}

void FileListModel::emitImageInfoBatchRowChanges(
    const ImageInfoBatchUpdate &update) {
    for (auto rowsIt = update.changedRowsByParent.cbegin();
         rowsIt != update.changedRowsByParent.cend(); ++rowsIt) {
        QList<int> rows = rowsIt.value();
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        const QModelIndex parentIndex = indexFromItem(rowsIt.key());
        int spanStart = -1;
        int previousRow = -2;
        for (const int row : std::as_const(rows)) {
            if (spanStart < 0) {
                spanStart = row;
            } else if (row != previousRow + 1) {
                emit dataChanged(index(spanStart, 0, parentIndex),
                                 index(previousRow, 0, parentIndex),
                                 {ImageFullSizeRole});
                spanStart = row;
            }
            previousRow = row;
        }
        if (spanStart >= 0) {
            emit dataChanged(index(spanStart, 0, parentIndex),
                             index(previousRow, 0, parentIndex),
                             {ImageFullSizeRole});
        }
    }
}

void FileListModel::finishImageInfoBatch(ImageInfoBatchUpdate *update) {
    if (update->flushItem) {
        const QModelIndex parentIndex = indexFromItem(
            update->flushItem->imageFileParent());
        const QModelIndex flushIndex = index(
            update->flushItem->index(), 0, parentIndex);
        emit dataChanged(flushIndex, flushIndex, {TimeToFlushRole});
    } else if (update->flushTopLevelFallback) {
        emitThumbnailInfoFlush();
    }
    refreshCurrentViewerAfterMetadata(update->currentViewerMetadataItem);

    update->directSourceVersionChanges.removeDuplicates();
    if (!update->directSourceVersionChanges.isEmpty()) {
        emit watchedImageMetadataChanged(
            update->directSourceVersionChanges);
    }
    if (!update->decodeRequests.isEmpty()) {
        decodeImages(update->decodeRequests);
    }
}
void FileListModel::connectImageReadySignal() {
    connect(_decodeManager, &DecodeManager::imageReady, this, [&] (const ImageDecodeRequest &request,
                                                                   const QImage &image, const DecodedImageInfo &decodedInfo) {
        if (!acceptsRequestNamespace(request.requestNamespace)) {
            return;
        }
        if (request.info.directOpenGeneration && request.info.directOpenGeneration != _directOpen.generation) {
            return;
        }
        if (image.isNull()) {
            rememberFailedDecodeRequest(request);
            handleDirectOpenImageReady(request, image, decodedInfo);
            return;
        }
        // image.save(QString("c:/tmp/zg/%1.png").arg(QFileInfo(request.info.path).fileName()));
        // qDebug() << "ZZ IMAGE READEY" << request.info.path << request.info.imageSize << request.targetSize << image.size();
        auto it = _fileToItem.find(request.info.path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (!item->isImage() ||
                !isCurrentFileVersion(item, request.info)) {
                return;
            }
            if (!decodedInfo.isFromCache) {
                ImageDecodeRequest sourceRetry = request;
                sourceRetry.checkCache = false;
                _failedImageDecodeRequests.remove(
                    decodeRetryKey(sourceRetry));
                _failedImageDecodeRetryAttempts.remove(
                    decodeRetryKey(sourceRetry));
                _failedImageWorkRetryDelayMs =
                    FailedImageWorkRetryInitialMs;
            }
            if (request.viewerRequest) {
                // qDebug() << "ZZ Viewer image came" << request.info.path << isFromCache << image.size() << item->image.size();
            }
            if (!request.viewerRequest &&
                item->imageMatchesSource(request.info) &&
                decodedInfo.isFromCache &&
                (image.width() <= item->image().width() ||
                 image.height() <= item->image().height())) {
                handleDirectOpenImageReady(request, image, decodedInfo);
                return;
            }
            if (request.viewerRequest) {
                const ViewerImageCache::StoredImage storedImage =
                    _viewerImageCache.storeDecodedImage(request, image,
                                                        decodedInfo);
                if (storedImage.presentable) {
                    emit viewerImageCacheChanged(item->index());
                    if (item->index() == _currentViewIndex) {
                        emit viewerImageIdUrlChanged(storedImage.url,
                                                     storedImage.level);
                    }
                }
            }
            else {
                item->setImage(image, request.info);
                item->setIsCachedThumbnail(decodedInfo.isFromCache);
                updateImageId(item);
            }

            handleDirectOpenImageReady(request, image, decodedInfo);
        }
        else {
            qDebug() << "Decoded image is not found in model" << request.info.path;
        }
    });
}

void FileListModel::connectFolderListReadySignal() {
    connect(_decodeManager, &DecodeManager::folderListReady, this,
            &FileListModel::handleFolderListReady);
}

void FileListModel::handleFolderListReady(
    const QString &path, const QList<FileInfo> &subfiles,
    bool isFromCache, quint64 requestGeneration) {
    FolderPreviewUpdate update;
    if (!beginFolderPreviewUpdate(path, subfiles, isFromCache,
                                  requestGeneration, &update)) {
        return;
    }
    buildFolderPreviewRows(&update);
    applyFolderPreviewMetadata(&update);
    reconcileFolderPreviewRows(&update);
    bindFolderPreviewRows(&update);
    retireFolderPreviewRows(&update);
    emitFolderPreviewMetadataChanges(update);
    finishFolderPreviewUpdate(&update);
}

bool FileListModel::beginFolderPreviewUpdate(
    const QString &path, const QList<FileInfo> &subfiles,
    bool isFromCache, quint64 requestGeneration,
    FolderPreviewUpdate *update) {
    const auto generationIt = _folderPreviewGenerations.constFind(path);
    if (!update || generationIt == _folderPreviewGenerations.constEnd()
        || generationIt.value() != requestGeneration) {
        return false;
    }
    _folderPreviewGenerations.remove(path);
    _folderPreviewRetryAttempts.remove(path);
    _folderImagePaths.removeOne(path);

    const auto itemIt = _fileToItem.find(path);
    if (itemIt == _fileToItem.end() || !itemIt.value()->isFolder()) {
        return false;
    }
    update->path = path;
    update->sourceRows = subfiles;
    update->item = itemIt.value();
    ImageInfo folderInfo = update->item->info();
    folderInfo.isCached = isFromCache;
    update->item->setInfo(folderInfo);
    update->parentIndex = indexFromItem(update->item);
    update->oldRows = update->item->subfiles();
    update->oldFolderView = update->item->folderView();
    update->oldByPath.reserve(update->oldRows.size());
    for (ImageFile *oldRow : std::as_const(update->oldRows)) {
        update->oldByPath.insert(
            fileSystemPathKey(oldRow->fullPath()), oldRow);
    }
    return true;
}

void FileListModel::buildFolderPreviewRows(FolderPreviewUpdate *update) {
    update->nextRows.reserve(update->sourceRows.size());
    for (const FileInfo &sourceRow : std::as_const(update->sourceRows)) {
        const QString rowPath = QDir(update->path).absoluteFilePath(
            sourceRow.name);
        ImageFile *row = update->oldByPath.value(
            fileSystemPathKey(rowPath), nullptr);
        if (row && row->fullPath() != rowPath) {
            row = nullptr;
        }
        if (row) {
            update->retainedRows.insert(row);
            ImageInfo info = row->info();
            if (info.lastModified != sourceRow.lastModified
                || info.fileSize != sourceRow.fileSize) {
                info.lastModified = sourceRow.lastModified;
                info.fileSize = sourceRow.fileSize;
                update->metadataUpdates.insert(row, info);
                update->imageInfoPaths.append(rowPath);
                update->changedPaths.append(rowPath);
            }
            if (!row->fullSize().isValid() || row->imageIdUrl().isEmpty()) {
                update->imageInfoPaths.append(rowPath);
            }
        } else {
            row = createFileItem(update->path, sourceRow.name,
                                 sourceRow.lastModified,
                                 sourceRow.fileSize);
            update->imageInfoPaths.append(rowPath);
            update->affectedPaths.insert(rowPath);
        }
        update->nextRows.append(row);
    }

    for (ImageFile *oldRow : std::as_const(update->oldRows)) {
        if (!update->retainedRows.contains(oldRow)) {
            update->removedRows.append(oldRow);
        }
    }
    update->structureChanged =
        update->oldRows.size() != update->nextRows.size();
    for (int row = 0;
         !update->structureChanged && row < update->oldRows.size(); ++row) {
        update->structureChanged =
            update->oldRows.at(row) != update->nextRows.at(row);
    }
}

void FileListModel::applyFolderPreviewMetadata(
    FolderPreviewUpdate *update) {
    update->previousPreserveState = _preserveViewStateOnReset;
    _preserveViewStateOnReset = true;
    for (auto it = update->metadataUpdates.cbegin();
         it != update->metadataUpdates.cend(); ++it) {
        it.key()->setInfo(it.value());
    }
}

void FileListModel::reconcileFolderPreviewRows(
    FolderPreviewUpdate *update) {
    if (!update->structureChanged) {
        return;
    }
    update->item->beginSubfilesModelUpdate();
    QList<ImageFile *> workingRows = update->oldRows;
    removeMissingFolderPreviewRows(update, &workingRows);
    placeFolderPreviewRows(update, &workingRows);
    Q_ASSERT(workingRows == update->nextRows);
    update->item->endSubfilesModelUpdate();
}

void FileListModel::removeMissingFolderPreviewRows(
    FolderPreviewUpdate *update, QList<ImageFile *> *workingRows) {
    const QSet<ImageFile *> nextRowSet(
        update->nextRows.begin(), update->nextRows.end());
    for (int last = workingRows->size() - 1; last >= 0;) {
        if (nextRowSet.contains(workingRows->at(last))) {
            --last;
            continue;
        }
        int first = last;
        while (first > 0
               && !nextRowSet.contains(workingRows->at(first - 1))) {
            --first;
        }
        beginRemoveRows(update->parentIndex, first, last);
        workingRows->erase(
            workingRows->begin() + first, workingRows->begin() + last + 1);
        update->item->setSubfiles(*workingRows);
        for (int row = first; row < workingRows->size(); ++row) {
            workingRows->at(row)->setIndex(row);
        }
        endRemoveRows();
        last = first - 1;
    }
}

void FileListModel::placeFolderPreviewRows(
    FolderPreviewUpdate *update, QList<ImageFile *> *workingRows) {
    QSet<ImageFile *> presentRows(workingRows->begin(), workingRows->end());
    for (int targetRow = 0; targetRow < update->nextRows.size();) {
        ImageFile *desired = update->nextRows.at(targetRow);
        if (targetRow < workingRows->size()
            && workingRows->at(targetRow) == desired) {
            ++targetRow;
            continue;
        }
        const int existingRow = workingRows->indexOf(desired);
        if (existingRow >= 0) {
            const int destinationChild = existingRow < targetRow
                ? targetRow + 1 : targetRow;
            const bool moveStarted = beginMoveRows(
                update->parentIndex, existingRow, existingRow,
                update->parentIndex, destinationChild);
            Q_ASSERT(moveStarted);
            if (!moveStarted) {
                qWarning() << "Could not move folder preview row"
                           << existingRow << targetRow << update->path;
                break;
            }
            workingRows->move(existingRow, targetRow);
            update->item->setSubfiles(*workingRows);
            const int changedFirst = qMin(existingRow, targetRow);
            const int changedLast = qMax(existingRow, targetRow);
            for (int row = changedFirst; row <= changedLast; ++row) {
                workingRows->at(row)->setIndex(row);
            }
            endMoveRows();
            ++targetRow;
            continue;
        }

        int insertCount = 1;
        while (targetRow + insertCount < update->nextRows.size()
               && !presentRows.contains(
                   update->nextRows.at(targetRow + insertCount))) {
            ++insertCount;
        }
        beginInsertRows(update->parentIndex, targetRow,
                        targetRow + insertCount - 1);
        for (int offset = 0; offset < insertCount; ++offset) {
            ImageFile *inserted = update->nextRows.at(targetRow + offset);
            workingRows->insert(targetRow + offset, inserted);
            presentRows.insert(inserted);
        }
        update->item->setSubfiles(*workingRows);
        for (int row = targetRow; row < workingRows->size(); ++row) {
            workingRows->at(row)->setIndex(row);
        }
        endInsertRows();
        targetRow += insertCount;
    }
}

void FileListModel::bindFolderPreviewRows(FolderPreviewUpdate *update) {
    for (int row = 0; row < update->nextRows.size(); ++row) {
        ImageFile *item = update->nextRows.at(row);
        item->setImageFileParent(update->item);
        item->setIndex(row);
        _fileToItem.insert(item->fullPath(), item);
    }
}

void FileListModel::retireFolderPreviewRows(FolderPreviewUpdate *update) {
    for (ImageFile *removed : std::as_const(update->removedRows)) {
        update->affectedPaths.insert(removed->fullPath());
        if (_fileToItem.value(removed->fullPath()) == removed) {
            _fileToItem.remove(removed->fullPath());
        }
        const QString imageId = removed->imageIdUrl().section('/', -1);
        if (!imageId.isEmpty()) {
            _imageIdToItem.remove(imageId);
            _providerImageStore->remove(imageId);
        }
        _viewerImageCache.remove(removed->fullPath());
        removed->deleteLater();
    }
}

void FileListModel::emitFolderPreviewMetadataChanges(
    const FolderPreviewUpdate &update) {
    if (update.metadataUpdates.isEmpty()) {
        return;
    }
    QList<int> changedRows;
    changedRows.reserve(update.metadataUpdates.size());
    for (ImageFile *changedItem : update.metadataUpdates.keys()) {
        changedRows.append(changedItem->index());
    }
    std::sort(changedRows.begin(), changedRows.end());
    int spanStart = changedRows.first();
    int previousRow = spanStart;
    for (qsizetype index = 1; index < changedRows.size(); ++index) {
        const int row = changedRows.at(index);
        if (row != previousRow + 1) {
            emit dataChanged(
                this->index(spanStart, 0, update.parentIndex),
                this->index(previousRow, 0, update.parentIndex),
                {LastModifiedRole, FileSizeRole});
            spanStart = row;
        }
        previousRow = row;
    }
    emit dataChanged(this->index(spanStart, 0, update.parentIndex),
                     this->index(previousRow, 0, update.parentIndex),
                     {LastModifiedRole, FileSizeRole});
}

void FileListModel::finishFolderPreviewUpdate(
    FolderPreviewUpdate *update) {
    if (update->oldFolderView != update->item->folderView()) {
        const QModelIndex itemIndex = index(
            update->item->index(), 0,
            indexFromItem(update->item->imageFileParent()));
        emit dataChanged(itemIndex, itemIndex, {FolderViewRole});
    }
    _preserveViewStateOnReset = update->previousPreserveState;

    update->changedPaths.removeDuplicates();
    if (!update->changedPaths.isEmpty()) {
        emit watchedImageMetadataChanged(update->changedPaths);
    }
    if (!update->affectedPaths.isEmpty()) {
        const QStringList affectedPaths = update->affectedPaths.values();
        updateAvailableSelectionCounts(affectedPaths);
        emit selectionChanged();
        emit selectionPathsChanged(affectedPaths);
        emit selectionGroupsChanged();
    }
    update->imageInfoPaths.removeDuplicates();
    if (!update->imageInfoPaths.isEmpty()) {
        readImagesInfo(update->imageInfoPaths, true);
    }
}
void FileListModel::connectImageReadFailedSignal() {
    connect(_decodeManager, &DecodeManager::imageReadFailed, this,
            [this](const ImageDecodeRequest &request) {
        if (!acceptsRequestNamespace(request.requestNamespace)) {
            return;
        }
        if (request.info.directOpenGeneration &&
            request.info.directOpenGeneration != _directOpen.generation) {
            return;
        }
        rememberFailedDecodeRequest(request);
        handleDirectOpenImageReady(request, QImage(), DecodedImageInfo());
    });
}

void FileListModel::connectFolderListFailedSignal() {
    connect(_decodeManager, &DecodeManager::folderListFailed, this,
            [this](const QString &path, const QString &errorText,
                   quint64 requestGeneration) {
        const auto generationIt = _folderPreviewGenerations.constFind(path);
        if (generationIt == _folderPreviewGenerations.constEnd() ||
            generationIt.value() != requestGeneration) {
            return;
        }
        scheduleFolderPreviewRetry(path, requestGeneration, errorText);
    });
}

void FileListModel::configureMaintenanceTimers() {
    _selectionSaveTimer.setSingleShot(true);
    _selectionSaveTimer.setInterval(200);
    connect(&_selectionSaveTimer, &QTimer::timeout, this, []() {
        PersistentSelectionCache::dumpDb();
    });

    _folderRefreshTimer.setSingleShot(true);
    _folderRefreshTimer.setInterval(FolderRefreshDebounceMs);
    connect(&_folderRefreshTimer, &QTimer::timeout,
            this, &FileListModel::refreshWatchedFolder);
    _folderWatchRetryTimer.setSingleShot(true);
    connect(&_folderWatchRetryTimer, &QTimer::timeout, this, [this]() {
        if (_isClosing || _recursiveViewActive) {
            return;
        }
        configureFolderWatcher();
        scheduleFolderRefresh();
    });
    _failedImageWorkRetryTimer.setSingleShot(true);
    connect(&_failedImageWorkRetryTimer, &QTimer::timeout,
            this, &FileListModel::retryFailedImageWork);
    connect(&_fileSystemWatcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString &path) {
        if (_isClosing || _recursiveViewActive) {
            return;
        }
        const QString changedKey = fileSystemPathKey(path);
        const QString rootKey = fileSystemPathKey(_root);
        if (changedKey == rootKey) {
            scheduleFolderRefresh();
            return;
        }

        // When the root disappears, configureFolderWatcher() watches its
        // nearest available ancestor. An ancestor event may mean that the
        // original folder has been recreated or a disconnected share is back.
        const QString rootParentKey =
            fileSystemPathKey(QFileInfo(_root).absolutePath());
        if (changedKey == rootParentKey ||
            !_fileSystemWatcher.directories().contains(_root)) {
            configureFolderWatcher();
            scheduleFolderRefresh();
        }
    });
    connect(&_fileSystemWatcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString &path) {
        if (!_isClosing && !_recursiveViewActive &&
            fileSystemPathKey(QFileInfo(path).absolutePath()) ==
                fileSystemPathKey(_root)) {
            scheduleFolderRefresh();
        }
    });
    refreshAvailableSelectionCounts();
}
