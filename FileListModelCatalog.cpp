#include "FileListModelPrivate.h"

void FileListModel::startRegularFolderWork() {
    readImagesInfo(_imagePaths, false);
    requestFolderPreviews(_folderImagePaths);
}
void FileListModel::requestFolderPreviews(const QStringList &paths) {
    QStringList requestPaths;
    requestPaths.reserve(paths.size());
    for (const QString &path : paths) {
        auto itemIt = _fileToItem.constFind(path);
        if (itemIt == _fileToItem.constEnd() ||
            !itemIt.value()->isFolder() || requestPaths.contains(path)) {
            continue;
        }
        requestPaths.append(path);
        if (!_folderImagePaths.contains(path)) {
            _folderImagePaths.append(path);
        }
    }
    if (requestPaths.isEmpty()) {
        return;
    }

    ++_nextFolderPreviewGeneration;
    if (_nextFolderPreviewGeneration == 0) {
        ++_nextFolderPreviewGeneration;
    }
    for (const QString &path : std::as_const(requestPaths)) {
        _folderPreviewGenerations.insert(path,
                                         _nextFolderPreviewGeneration);
        _folderPreviewRetryAttempts.remove(path);
    }
    _decodeManager->readFolderList(requestPaths, 16,
                                   _nextFolderPreviewGeneration,
                                   _requestNamespace);
}
void FileListModel::scheduleFolderPreviewRetry(
    const QString &path, quint64 requestGeneration,
    const QString &errorText) {
    const int attempt = _folderPreviewRetryAttempts.value(path, 0);
    if (attempt >= FolderPreviewRetryMaxAttempts) {
        qWarning() << "Keeping existing folder preview after repeated refresh "
                      "failures"
                   << path << errorText << "giving up after" << attempt
                   << "retries";
        _folderPreviewGenerations.remove(path);
        _folderPreviewRetryAttempts.remove(path);
        return;
    }
    const int delayMs = qMin(500 * (1 << qMin(attempt, 3)),
                             FolderWatchRetryMaxMs);
    _folderPreviewRetryAttempts.insert(path, attempt + 1);
    qWarning() << "Keeping existing folder preview after incomplete refresh"
               << path << errorText << "retry in" << delayMs << "ms";

    QTimer::singleShot(delayMs, this,
                       [this, path, requestGeneration]() {
        if (_isClosing || _recursiveViewActive ||
            !sourceReadsEnabled(_fileListCacheMode) ||
            _folderPreviewGenerations.value(path) != requestGeneration) {
            return;
        }
        const auto itemIt = _fileToItem.constFind(path);
        if (itemIt == _fileToItem.constEnd() ||
            !itemIt.value()->isFolder()) {
            return;
        }
        _decodeManager->readFolderList({path}, 16,
                                       requestGeneration,
                                       _requestNamespace);
    });
}

QString FileListModel::rootPath() const {
    return _root;
}

bool FileListModel::preserveViewStateOnReset() const {
    return _preserveViewStateOnReset;
}

ImageInfo FileListModel::imageInfoForPath(const QString &path) const {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    auto itemIt = _fileToItem.constFind(absolutePath);
    if (itemIt != _fileToItem.constEnd() && itemIt.value()->isImage()) {
        return itemIt.value()->info();
    }

    const QString pathKey = fileSystemPathKey(absolutePath);
    for (ImageFile *item : std::as_const(_items)) {
        if (item->isImage() &&
            fileSystemPathKey(item->fullPath()) == pathKey) {
            return item->info();
        }
    }
    return {};
}

const ImageFile *FileListModel::itemForImageId(const QString &imageId) {
    auto it = _imageIdToItem.find(imageId);
    if (it != _imageIdToItem.end()) {
        return *it;
    }
    return nullptr;
}

QString FileListModel::generateNewId() {
    QString id = _imageIdPrefix + QString::number(_lastId);
    _lastId++;
    return id;
}

void FileListModel::readImagesInfo(
    const QList<QString> &paths, bool isFromEmbeddedView,
    int directOpenGeneration, bool highPriority) {
    _decodeManager->readImagesInfo(paths, isFromEmbeddedView,
                                   directOpenGeneration, highPriority,
                                   _requestNamespace);
}

void FileListModel::cancelSessionRequests() {
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllRunners();
    }
    else {
        _decodeManager->cancelRequests(_requestNamespace);
    }
}

bool FileListModel::acceptsRequestNamespace(
    const QString &requestNamespace) const {
    return requestNamespace == _requestNamespace;
}

void FileListModel::configureImageFile(ImageFile *item) const {
    if (item) {
        item->setImageProviderName(_thumbnailProviderName);
    }
}

void FileListModel::updateImageId(ImageFile *item) {
    const QString imageId = item->imageIdUrl().section('/', -1);
    if (!imageId.isEmpty()) {
        _imageIdToItem.remove(imageId);
        _providerImageStore->remove(imageId);
    }
    const QString newImageId = generateNewId();
    _imageIdToItem.insert(newImageId, item);
    _providerImageStore->publish(newImageId, item->image());
    item->setImageId(newImageId);

    QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
    emit dataChanged(modelIndex, modelIndex, {ImageIdUrlRole});
}

ImageFile *FileListModel::createFileItem(const QString &folderPath, const QString &fileName,
                                         const QDateTime &lastModified, qint64 fileSize) {
    ImageFile *item = new ImageFile(this);
    configureImageFile(item);
    item->setFolderPath(folderPath);
    item->setFileName(fileName);
    item->setIsFolder(false);
    ImageInfo info = {
        .path = item->fullPath(),
        .lastModified = lastModified,
        .fileSize = fileSize,
    };
    item->setInfo(info);

    if (isImage(item->fileName())) {
        item->setIsImage(true);
        QString lowerFileName = item->fileName().toLower();
        item->setIconPath("qrc:/ZoinGallery/resources/ImageIcon.svg");
//                updateImageId(item);
        QString path = item->fullPath();
        _fileToItem.insert(path, item);
    }
    else {
        item->setIsImage(false);
        item->setIconPath("qrc:/ZoinGallery/resources/FileIcon.svg");
    }
    return item;
}

void FileListModel::cleanupModelBeforeCd() {
    cancelAllRunners();
    clearModelData(true);
}

void FileListModel::clearModelData(bool clearViewerData,
                                   bool clearFailedImageWork) {
    if (clearViewerData) {
        // Viewer
        _viewerImageCache.clear();
        emit viewerReset();
    }
    _currentViewIndex = -1;
    _currentViewerRequestSize = QSize();
    _hasCurrentViewerRequest = false;
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();

    for (auto it = _folderModels.begin(); it != _folderModels.end(); ++it) {
        it.value()->setRoot(nullptr);
        it.value()->deleteLater();
    }
    _folderModels.clear();

    _fileToItem.clear();
    _folderPreviewGenerations.clear();
    _folderPreviewRetryAttempts.clear();
    if (clearFailedImageWork) {
        _failedImageInfoRequests.clear();
        _failedImageDecodeRequests.clear();
        _failedImageInfoRetryAttempts.clear();
        _failedImageDecodeRetryAttempts.clear();
        _failedImageWorkRetryTimer.stop();
        _failedImageWorkRetryDelayMs = FailedImageWorkRetryInitialMs;
    }
    _providerImageStore->remove(_imageIdToItem.keys());
    _imageIdToItem.clear();
    _folderImagePaths.clear();
    _imagePaths.clear();
    for (int i = 0; i < _items.size(); i++) {
        delete _items[i];
    }
    _items.clear();
}

ImageDecodeRequest FileListModel::imageDecodeRequestFromEmbeddedImageInfo(const ImageInfo &info) const {
    QSize thumbnailSize = _folderViewImageSize;
    QSize resultSize = rotateToOrientation(info.imageSize, info.orientation);
    if (!_folderViewImageSize.width()) { // CalcLayoutSingleRow
        thumbnailSize = QSize(resultSize.width() * (qreal(_folderViewImageSize.height()) / resultSize.height()), _folderViewImageSize.height());
        // qDebug() << "ZZ CalcLayoutSingleRow" << result.path << thumbnailSize;
    }
    else { // CalcLayoutGrid
        thumbnailSize = resultSize.scaled(_folderViewImageSize, Qt::KeepAspectRatio);
        // qDebug() << "ZZ CalcLayoutGrid" << _folderViewImageSize << result.path << thumbnailSize << result.imageSize << _folderViewImageSize << result.orientation;
    }
    return ImageDecodeRequest{
        .info = info,
        .targetSize = thumbnailSize,
        .viewerRequest = false,
        .checkCache = info.isCached
    };
}

ImageFile *FileListModel::itemFromIndex(const QModelIndex &index) {
    return static_cast<ImageFile*>(index.internalPointer());
}

QModelIndex FileListModel::indexFromItem(const ImageFile *item) const {
    if (!item) {
        return QModelIndex();
    }
    return index(item->index(), 0, indexFromItem(item->imageFileParent()));
}

QAbstractItemModel *FileListModel::folderModel(int index_) {
    if (index_ < 0 || index_ >= _items.size() ||
        !_items.at(index_)->isFolder()) {
        return nullptr;
    }

    ImageFile *folder = _items.at(index_);
    auto it = _folderModels.find(folder);
    if (it == _folderModels.end()) {
        RootProxyModel *proxy = new RootProxyModel(this);
        proxy->setRoot(folder);
        proxy->setSourceModel(this);
        _folderModels.insert(folder, proxy);
        return proxy;
    }
    return *it;
}

struct RecursiveFolderInfo {
    int level;                     // Nesting level of the folder
    QString path;                  // Absolute path of the folder
    QString lastInGroup;           // String where each character represents a level: '1' for last, '0' for not last

    RecursiveFolderInfo(int lvl, QString pth, QString lastGroup)
        : level(lvl), path(pth), lastInGroup(lastGroup) {}
};

QList<RecursiveFolderInfo> getAllSubfoldersWithNestingLevel(const QString &startDir) {
    QList<RecursiveFolderInfo> allFoldersWithLevels;         // List to store folders with their nesting levels and boolean string
    QStack<RecursiveFolderInfo> dirs;                        // Stack to manage directories
    dirs.push(RecursiveFolderInfo(0, startDir, ""));        // Start with the initial directory, marked as last in its (non-existent) group

    while (!dirs.isEmpty()) {
        RecursiveFolderInfo dirInfo = dirs.pop();

        // Add the current directory to the list
        allFoldersWithLevels.append(dirInfo);

        // Get a list of all subdirectories in the current directory
        QStringList subDirs = QDir(dirInfo.path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
        sortNamesNaturally(subDirs);
        for (int i = subDirs.count() - 1; i >= 0; --i) {
            QString subDir = subDirs.at(i);
            QString newPath = QDir(dirInfo.path).filePath(subDir);

            // Determine if this is the last subdirectory in the list
            QString isLast = (i == subDirs.count() - 1) ? "0" : "1";

            // Create a new boolean string for the next level based on the current dir's string
            QString newLastInGroup = dirInfo.lastInGroup + isLast;

            dirs.push(RecursiveFolderInfo(dirInfo.level + 1, newPath, newLastInGroup));
        }
    }

    return allFoldersWithLevels;
}


void FileListModel::enterRecursiveView() {
    if (!fileListSourceAccessEnabled()) {
        return;
    }

    _folderRefreshTimer.stop();
    _folderWatchRetryTimer.stop();
    _folderRefreshGeneration++;
    _folderRefreshPendingAfterDirectOpen = false;
    _folderRescanAfterInFlight.clear();
    _recursiveViewActive = true;
    configureFolderWatcher();

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

    beginResetModel();
    cleanupModelBeforeCd();

    // if (_root == "Computer") {
    //     for (const auto &drive : QDir::drives()) {
    //         ImageFile *item = new ImageFile();
    //         item->fileName = drive.path();
    //         item->isFolder = true;
    //         item->isImage = false;
    //         item->index = _items.size();
    //         item->iconPath = "qrc:/ZoinGallery/resources/DriveIcon.svg";
    //         _items.append(item);

    //         if (item->fileName == itemToSelect) {
    //             indexToSelect = _items.size() - 1;
    //         }
    //     }
    // }
    // else
    {
        QList<RecursiveFolderInfo> folders = getAllSubfoldersWithNestingLevel(_root);
        for (const auto &folder : folders) {
            QFileInfo info(folder.path);
            // qDebug() << folder.level << info.filePath() << info.fileName();
            ImageFile *item = new ImageFile(this);
            configureImageFile(item);
            item->setFolderPath(info.dir().absolutePath());
            item->setFileName(info.fileName()); // QString("%1: %2").arg(folder.first).arg(folder.second);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/ZoinGallery/resources/FolderIcon.svg");
            item->setInfo(ImageInfo{
                .path = item->fullPath(),
                .lastModified = info.lastModified(),
                .fileSize = 0,
            });
            item->setIndex(_items.size());
            item->setNestingInfo(folder.lastInGroup);
            _items.append(item);

            QString path = item->fullPath();

            _fileToItem.insert(path, item);
            _folderImagePaths.append(path);
        }

        /*auto files = QDir(_root).entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            ImageFile *item = createFileItem(_root, file.fileName(), file.lastModified());
            if (item->isImage) {
                _imagePaths.append(item->fullPath());
            }
            item->index = _items.size();
            _items.append(item);
        }*/
    }
    loadSelectionStatesForVisibleItems();
    endResetModel();

    readImagesInfo(_imagePaths, true);
    requestFolderPreviews(_folderImagePaths);
}
