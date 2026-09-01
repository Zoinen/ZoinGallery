#include "FileListModelPrivate.h"

FileListModel::FileListModel(
    QSharedPointer<ProviderImageStore> providerImageStore, QObject *parent)
    : FileListModel(std::move(providerImageStore), nullptr, QString(),
                    QStringLiteral("main-"),
                    QStringLiteral("zoingallery-thumbnails"),
                    QStringLiteral("zoingallery-async"), parent) {
}
FileListModel::FileListModel(
    QSharedPointer<ProviderImageStore> providerImageStore,
    DecodeManager *sharedDecodeManager,
    const QString &requestNamespace,
    const QString &imageIdPrefix,
    const QString &thumbnailProviderName,
    const QString &asyncProviderName,
    QObject *parent)
    : FileListModel(std::move(providerImageStore), sharedDecodeManager,
                    requestNamespace, imageIdPrefix,
                    thumbnailProviderName, asyncProviderName,
                    ViewerImageCache::DefaultFitByteBudget,
                    ViewerImageCache::DefaultNativeByteBudget,
                    parent) {
}

FileListModel::FileListModel(
    QSharedPointer<ProviderImageStore> providerImageStore,
    DecodeManager *sharedDecodeManager,
    const QString &requestNamespace,
    const QString &imageIdPrefix,
    const QString &thumbnailProviderName,
    const QString &asyncProviderName,
    qint64 viewerFitCacheByteBudget,
    qint64 viewerNativeCacheByteBudget,
    QObject *parent)
    : QAbstractItemModel(parent),
      _providerImageStore(std::move(providerImageStore)),
      _viewerImageCache(imageIdPrefix + QStringLiteral("viewer-"),
                        _providerImageStore,
                        thumbnailProviderName,
                        asyncProviderName,
                        viewerFitCacheByteBudget,
                        viewerNativeCacheByteBudget),
      _decodeManager(sharedDecodeManager
                         ? sharedDecodeManager
                         : new DecodeManager(this)),
      _ownsDecodeManager(!sharedDecodeManager),
      _requestNamespace(requestNamespace),
      _imageIdPrefix(imageIdPrefix),
      _thumbnailProviderName(thumbnailProviderName),
      _asyncProviderName(asyncProviderName) {
    initializeRuntimeSettings();
    connectRuntimeStatusSignals();
    connectImageInfoSignal();
    connectImageInfosSignal();
    connectImageReadySignal();
    connectFolderListReadySignal();
    connectImageReadFailedSignal();
    connectFolderListFailedSignal();
    configureMaintenanceTimers();
}

FileListModel::~FileListModel() {
#ifdef Q_OS_LINUX
    resetLinuxFolderContentWatcher();
#endif
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int,QByteArray> names;
    // names[Qt::DisplayRole] = "displayRole";
    names[ImageIdUrlRole] = "imageIdUrlRole";
    names[SelectedRole] = "selectedRole";
    names[SelectionGroupIdRole] = "selectionGroupIdRole";
    names[SelectionGroupColorRole] = "selectionGroupColorRole";
    names[ImageFileRole] = "imageFileRole";
    names[FolderRole] = "folderRole";
    names[IsImageRole] = "isImageRole";
    names[LastModifiedRole] = "lastModifiedRole";
    names[FileSizeRole] = "fileSizeRole";
    return names;
}

int FileListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        ImageFile *imageFile = itemFromIndex(parent);
        if (imageFile) {
            return imageFile->subfiles().size();
        }
        return 0;
    }
    return _items.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    ImageFile *imageFile = itemFromIndex(index);
    if (imageFile) {
        if (role == ImageIdUrlRole) {
            return imageFile->imageIdUrl();
        }
        else if (role == FolderRole) {
            return imageFile->isFolder();
        }
        else if (role == IsImageRole) {
            return imageFile->isImage();
        }
        else if (role == ImageFullSizeRole) {
            return imageFile->fullSize();
        }
        else if (role == ImageFileRole) {
            return QVariant::fromValue(imageFile);
        }
        else if (role == FolderViewRole) {
            return imageFile->subfiles().size() != 0;
        }
        else if (role == SelectedRole) {
            return imageFile->isSelected();
        }
        else if (role == SelectionGroupIdRole) {
            return imageFile->selectionGroupId();
        }
        else if (role == SelectionGroupColorRole) {
            return imageFile->selectionGroupColor();
        }
        else if (role == LastModifiedRole) {
            return imageFile->lastModified();
        }
        else if (role == FileSizeRole) {
            return imageFile->fileSize();
        }
    }
    return QVariant();
}

QModelIndex FileListModel::index(int row, int column, const QModelIndex &parent) const {
    if (hasIndex(row, column, parent)) {
        if (parent.isValid()) {
            ImageFile *imageFile = itemFromIndex(parent);
            if (row < imageFile->subfiles().size()) {
                return createIndex(row, column, imageFile->subfiles().at(row));
            }
        }
        else if (row < _items.size()) {
            return createIndex(row, column, _items.at(row));
        }
    }

    // Invalid index, root element
    return QModelIndex();
}

QModelIndex FileListModel::parent(const QModelIndex &child) const {
    ImageFile *imageFile = itemFromIndex(child);
    if (!imageFile || !imageFile->imageFileParent()) {
        return QModelIndex();
    }

    return index(imageFile->imageFileParent()->index(), 0, QModelIndex());
}

int FileListModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

void FileListModel::prepareToClose() {
    shutdown();
}

void FileListModel::shutdown() {
    qInfo() << "[Shutdown] FileListModel::shutdown begin"
            << "alreadyClosing" << _isClosing
            << "items" << _items.size()
            << "viewerImages" << _viewerImageCache.viewerImageCount()
            << "fullSizeViewerImages"
            << _viewerImageCache.fullSizeImageCount();
    if (_isClosing) {
        qInfo() << "[Shutdown] FileListModel::shutdown already complete";
        return;
    }
    _isClosing = true;
    _folderRefreshTimer.stop();
    _folderWatchRetryTimer.stop();
    _failedImageWorkRetryTimer.stop();
    const QStringList watchedDirectories = _fileSystemWatcher.directories();
    if (!watchedDirectories.isEmpty()) {
        _fileSystemWatcher.removePaths(watchedDirectories);
    }
    const QStringList watchedFiles = _fileSystemWatcher.files();
    if (!watchedFiles.isEmpty()) {
        _fileSystemWatcher.removePaths(watchedFiles);
    }

    qInfo() << "[Shutdown] FileListModel::shutdown dumping selection cache";
    _selectionSaveTimer.stop();
    PersistentSelectionCache::dumpDb();
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    if (_ownsDecodeManager) {
        qInfo() << "[Shutdown] FileListModel::shutdown stopping owned decode manager";
        _decodeManager->prepareToClose();
    }
    else {
        qInfo() << "[Shutdown] FileListModel::shutdown canceling session work"
                << _requestNamespace;
        cancelSessionRequests();
    }
    qInfo() << "[Shutdown] FileListModel::shutdown end";
}
