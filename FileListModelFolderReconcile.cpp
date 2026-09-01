#include "FileListModelPrivate.h"

struct FileListModel::FolderReconcileTransaction {
    QList<ImageFile *> nextItems;
    QList<ImageFile *> removedItems;
    QSet<ImageFile *> retainedItems;
    QHash<ImageFile *, ImageInfo> metadataUpdates;
    QStringList addedImagePaths;
    QStringList changedImagePaths;
    QStringList addedFolderPaths;
    QStringList changedFolderPaths;
    QSet<QString> affectedPaths;
    QSet<QString> pendingFolderKeys;
    bool structureChanged = false;
    int previousCurrentIndex = -1;
    ImageFile *previousCurrentItem = nullptr;
    QString directOpenPath;
};

QList<FileInfo> FileListModel::sortedFolderEntries(
    const QList<FileInfo> &sourceEntries) const {
    QList<FileInfo> folders;
    QList<FileInfo> files;
    folders.reserve(sourceEntries.size());
    files.reserve(sourceEntries.size());
    for (const FileInfo &entry : sourceEntries) {
        (entry.isDirectory ? folders : files).append(entry);
    }
    sortFileInfosNaturally(folders);
    sortFileInfosNaturally(files);
    folders.append(files);
    return folders;
}

ImageFile *FileListModel::createReconciledFolderItem(
    const FileInfo &entry) {
    auto *item = new ImageFile(this);
    configureImageFile(item);
    item->setFolderPath(_root);
    item->setFileName(entry.name);
    item->setIsFolder(true);
    item->setIsImage(false);
    item->setIconPath(
        QStringLiteral("qrc:/ZoinGallery/resources/FolderIcon.svg"));
    item->setInfo(ImageInfo{
        .path = item->fullPath(),
        .lastModified = entry.lastModified,
        .fileSize = 0,
    });
    return item;
}

void FileListModel::buildFolderReconcileRows(
    FolderReconcileTransaction *transaction,
    const QList<FileInfo> &entries) {
    QHash<QString, ImageFile *> existingByPath;
    existingByPath.reserve(_items.size());
    for (ImageFile *item : std::as_const(_items)) {
        existingByPath.insert(fileSystemPathKey(item->fullPath()), item);
    }
    transaction->nextItems.reserve(entries.size());
    for (const FileInfo &entry : entries) {
        const QString path = QDir(_root).absoluteFilePath(entry.name);
        ImageFile *item = existingByPath.value(
            fileSystemPathKey(path), nullptr);
        if (item && (item->isFolder() != entry.isDirectory
                     || item->fullPath() != path)) {
            item = nullptr;
        }
        if (item) {
            transaction->retainedItems.insert(item);
            ImageInfo info = item->info();
            if (info.path != path || info.lastModified != entry.lastModified
                || info.fileSize != entry.fileSize) {
                info.path = path;
                info.lastModified = entry.lastModified;
                info.fileSize = entry.fileSize;
                transaction->metadataUpdates.insert(item, info);
                (item->isImage() ? transaction->changedImagePaths
                                 : transaction->changedFolderPaths)
                    .append(path);
            }
        } else if (entry.isDirectory) {
            item = createReconciledFolderItem(entry);
            transaction->addedFolderPaths.append(item->fullPath());
            transaction->affectedPaths.insert(item->fullPath());
        } else {
            item = createFileItem(
                _root, entry.name, entry.lastModified, entry.fileSize);
            if (item->isImage()) {
                transaction->addedImagePaths.append(item->fullPath());
            }
            transaction->affectedPaths.insert(item->fullPath());
        }
        transaction->nextItems.append(item);
    }

    for (ImageFile *item : std::as_const(_items)) {
        if (!transaction->retainedItems.contains(item)) {
            transaction->removedItems.append(item);
            transaction->affectedPaths.insert(item->fullPath());
        }
    }
    transaction->structureChanged =
        transaction->nextItems.size() != _items.size();
    for (int row = 0;
         !transaction->structureChanged && row < _items.size(); ++row) {
        transaction->structureChanged =
            transaction->nextItems.at(row) != _items.at(row);
    }
}

void FileListModel::captureFolderReconcileState(
    FolderReconcileTransaction *transaction) {
    _preserveViewStateOnReset = true;
    transaction->previousCurrentIndex = _currentViewIndex;
    transaction->previousCurrentItem =
        _currentViewIndex >= 0 && _currentViewIndex < _items.size()
        ? _items.at(_currentViewIndex) : nullptr;
    transaction->directOpenPath = _directOpen.path;
    for (const QString &path : std::as_const(_folderImagePaths)) {
        transaction->pendingFolderKeys.insert(fileSystemPathKey(path));
    }
}

void FileListModel::seedFolderReconcileSelection(
    const FolderReconcileTransaction &transaction) {
    for (ImageFile *item : transaction.nextItems) {
        if (transaction.retainedItems.contains(item)) {
            continue;
        }
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        const QString groupId = _selectionStates[containerKey]
            .selectedGroups.value(selectionItemKey(item));
        item->setIsSelected(!groupId.isEmpty());
        item->setSelectionGroupId(groupId);
        item->setSelectionGroupColor(
            groupId.isEmpty() ? QColor()
                              : QColor(PersistentSelectionCache::colorForGroup(
                                    groupId)));
    }
}

int FileListModel::folderIndexForPath(const QString &path) const {
    if (path.isEmpty()) {
        return -1;
    }
    const QString key = fileSystemPathKey(path);
    for (int row = 0; row < _items.size(); ++row) {
        if (fileSystemPathKey(_items.at(row)->fullPath()) == key) {
            return row;
        }
    }
    return -1;
}

void FileListModel::reindexFolderItemsFrom(int first) {
    for (int row = qMax(0, first); row < _items.size(); ++row) {
        _items.at(row)->setIndex(row);
    }
}

void FileListModel::remapFolderTrackedIndexes(
    const FolderReconcileTransaction &transaction) {
    const int currentItemIndex = transaction.previousCurrentItem
        ? _items.indexOf(transaction.previousCurrentItem) : -1;
    if (currentItemIndex >= 0) {
        _currentViewIndex = currentItemIndex;
    } else if (_items.isEmpty()) {
        _currentViewIndex = -1;
    } else if (transaction.previousCurrentIndex >= 0) {
        _currentViewIndex = qBound(
            0, transaction.previousCurrentIndex, _items.size() - 1);
    }
    if (!transaction.directOpenPath.isEmpty()) {
        _directOpen.currentIndex = folderIndexForPath(
            transaction.directOpenPath);
    }
}

void FileListModel::removeMissingFolderRows(
    FolderReconcileTransaction *transaction) {
    const QSet<ImageFile *> nextItemSet(
        transaction->nextItems.begin(), transaction->nextItems.end());
    for (int last = _items.size() - 1; last >= 0;) {
        if (nextItemSet.contains(_items.at(last))) {
            --last;
            continue;
        }
        int first = last;
        while (first > 0 && !nextItemSet.contains(_items.at(first - 1))) {
            --first;
        }
        beginRemoveRows(QModelIndex(), first, last);
        for (int row = last; row >= first; --row) {
            _items.removeAt(row);
        }
        reindexFolderItemsFrom(first);
        remapFolderTrackedIndexes(*transaction);
        endRemoveRows();
        last = first - 1;
    }
}

void FileListModel::placeFolderRows(
    FolderReconcileTransaction *transaction) {
    QSet<ImageFile *> presentItems(_items.begin(), _items.end());
    for (int targetRow = 0;
         targetRow < transaction->nextItems.size();) {
        ImageFile *desired = transaction->nextItems.at(targetRow);
        if (targetRow < _items.size() && _items.at(targetRow) == desired) {
            ++targetRow;
            continue;
        }
        const int existingRow = _items.indexOf(desired);
        if (existingRow >= 0) {
            const int destinationChild = existingRow < targetRow
                ? targetRow + 1 : targetRow;
            const bool moveStarted = beginMoveRows(
                QModelIndex(), existingRow, existingRow,
                QModelIndex(), destinationChild);
            Q_ASSERT(moveStarted);
            if (!moveStarted) {
                qWarning() << "Could not move watched folder row"
                           << existingRow << targetRow << _root;
                break;
            }
            _items.move(existingRow, targetRow);
            reindexFolderItemsFrom(qMin(existingRow, targetRow));
            remapFolderTrackedIndexes(*transaction);
            endMoveRows();
            ++targetRow;
            continue;
        }

        int insertCount = 1;
        while (targetRow + insertCount < transaction->nextItems.size()
               && !presentItems.contains(
                   transaction->nextItems.at(targetRow + insertCount))) {
            ++insertCount;
        }
        beginInsertRows(QModelIndex(), targetRow,
                        targetRow + insertCount - 1);
        for (int offset = 0; offset < insertCount; ++offset) {
            ImageFile *inserted = transaction->nextItems.at(
                targetRow + offset);
            _items.insert(targetRow + offset, inserted);
            presentItems.insert(inserted);
        }
        reindexFolderItemsFrom(targetRow);
        remapFolderTrackedIndexes(*transaction);
        endInsertRows();
        targetRow += insertCount;
    }
}

void FileListModel::rebuildFolderCatalogIndexes(
    const FolderReconcileTransaction &transaction) {
    _fileToItem.clear();
    _imagePaths.clear();
    _folderImagePaths.clear();
    for (int row = 0; row < _items.size(); ++row) {
        ImageFile *item = _items.at(row);
        item->setIndex(row);
        if (item->isFolder()) {
            _fileToItem.insert(item->fullPath(), item);
            const QList<ImageFile *> subfiles = item->subfiles();
            for (int subRow = 0; subRow < subfiles.size(); ++subRow) {
                ImageFile *subfile = subfiles.at(subRow);
                subfile->setIndex(subRow);
                subfile->setImageFileParent(item);
                _fileToItem.insert(subfile->fullPath(), subfile);
            }
            if (transaction.pendingFolderKeys.contains(
                    fileSystemPathKey(item->fullPath()))
                || transaction.addedFolderPaths.contains(item->fullPath())) {
                _folderImagePaths.append(item->fullPath());
            }
        } else if (item->isImage()) {
            _fileToItem.insert(item->fullPath(), item);
            _imagePaths.append(item->fullPath());
        }
    }
}

void FileListModel::detachRemovedFolderModels(
    const FolderReconcileTransaction &transaction) {
    for (ImageFile *removed : transaction.removedItems) {
        if (RootProxyModel *proxy = _folderModels.take(removed)) {
            proxy->setRoot(nullptr);
            proxy->deleteLater();
        }
    }
}

void FileListModel::applyFolderReconcileStructure(
    FolderReconcileTransaction *transaction) {
    if (!transaction->structureChanged) {
        return;
    }
    removeMissingFolderRows(transaction);
    placeFolderRows(transaction);
    Q_ASSERT(_items == transaction->nextItems);
    rebuildFolderCatalogIndexes(*transaction);
    detachRemovedFolderModels(*transaction);
    remapFolderTrackedIndexes(*transaction);
    loadSelectionStatesForVisibleItems();
}

void FileListModel::applyFolderReconcileMetadata(
    const FolderReconcileTransaction &transaction) {
    for (auto it = transaction.metadataUpdates.cbegin();
         it != transaction.metadataUpdates.cend(); ++it) {
        it.key()->setInfo(it.value());
    }
}

void FileListModel::emitFolderReconcileMetadataChanges(
    const FolderReconcileTransaction &transaction) {
    QList<int> changedIndexes;
    changedIndexes.reserve(transaction.metadataUpdates.size());
    for (ImageFile *item : transaction.metadataUpdates.keys()) {
        changedIndexes.append(item->index());
    }
    std::sort(changedIndexes.begin(), changedIndexes.end());
    if (changedIndexes.isEmpty()) {
        return;
    }
    int spanStart = changedIndexes.first();
    int previousRow = spanStart;
    for (qsizetype index = 1; index < changedIndexes.size(); ++index) {
        const int row = changedIndexes.at(index);
        if (row != previousRow + 1) {
            emit dataChanged(this->index(spanStart, 0),
                             this->index(previousRow, 0),
                             {LastModifiedRole, FileSizeRole});
            spanStart = row;
        }
        previousRow = row;
    }
    emit dataChanged(this->index(spanStart, 0),
                     this->index(previousRow, 0),
                     {LastModifiedRole, FileSizeRole});
}

void FileListModel::removeFolderItemDecodedState(ImageFile *item) {
    const QString imageId = item->imageIdUrl().section('/', -1);
    if (!imageId.isEmpty()) {
        _imageIdToItem.remove(imageId);
        _providerImageStore->remove(imageId);
    }
    _viewerImageCache.remove(item->fullPath());
}

void FileListModel::cleanupRemovedFolderItems(
    const FolderReconcileTransaction &transaction) {
    for (ImageFile *item : transaction.removedItems) {
        _folderPreviewGenerations.remove(item->fullPath());
        _folderPreviewRetryAttempts.remove(item->fullPath());
        removeFolderItemDecodedState(item);
        for (ImageFile *subfile : item->subfiles()) {
            removeFolderItemDecodedState(subfile);
        }
    }
    for (ImageFile *item : transaction.removedItems) {
        item->deleteLater();
    }
}

void FileListModel::finishFolderReconcile(
    FolderReconcileTransaction *transaction) {
    if (transaction->previousCurrentItem
        && (_currentViewIndex < 0 || _currentViewIndex >= _items.size()
            || _items.at(_currentViewIndex)
                != transaction->previousCurrentItem)) {
        emit viewerReset();
    }
    transaction->addedImagePaths.removeDuplicates();
    transaction->changedImagePaths.removeDuplicates();
    if (!transaction->changedImagePaths.isEmpty()) {
        emit watchedImageMetadataChanged(transaction->changedImagePaths);
    }
    if (!transaction->addedImagePaths.isEmpty()) {
        readImagesInfo(transaction->addedImagePaths, false, 0, true);
    }
    if (!transaction->changedImagePaths.isEmpty()) {
        readImagesInfo(transaction->changedImagePaths, false);
    }

    QStringList previewPaths = transaction->addedFolderPaths;
    previewPaths.append(transaction->changedFolderPaths);
    previewPaths.removeDuplicates();
    QStringList cacheInvalidations = previewPaths;
    for (ImageFile *removed : transaction->removedItems) {
        if (removed->isFolder()) {
            cacheInvalidations.append(removed->fullPath());
        }
    }
    cacheInvalidations.removeDuplicates();
    if (!cacheInvalidations.isEmpty()) {
        PersistentFolderCache::removeFolders(cacheInvalidations);
    }
    if (!previewPaths.isEmpty()) {
        requestFolderPreviews(previewPaths);
    }
    if (!transaction->affectedPaths.isEmpty()) {
        const QStringList changedPaths = transaction->affectedPaths.values();
        updateAvailableSelectionCounts(changedPaths);
        emit selectionChanged();
        emit selectionPathsChanged(changedPaths);
        emit selectionGroupsChanged();
    }
    qDebug() << "Reconciled watched folder" << _root
             << "added" << transaction->addedImagePaths.size()
                    + transaction->addedFolderPaths.size()
             << "removed" << transaction->removedItems.size()
             << "metadata" << transaction->metadataUpdates.size();
    _preserveViewStateOnReset = false;
}

void FileListModel::reconcileFolderEntries(
    const QList<FileInfo> &sourceEntries) {
    const QList<FileInfo> entries = sortedFolderEntries(sourceEntries);
    FolderReconcileTransaction transaction;
    buildFolderReconcileRows(&transaction, entries);
    if (!transaction.structureChanged
        && transaction.metadataUpdates.isEmpty()) {
        return;
    }
    captureFolderReconcileState(&transaction);
    seedFolderReconcileSelection(transaction);
    applyFolderReconcileStructure(&transaction);
    applyFolderReconcileMetadata(transaction);
    emitFolderReconcileMetadataChanges(transaction);
    cleanupRemovedFolderItems(transaction);
    finishFolderReconcile(&transaction);
}
