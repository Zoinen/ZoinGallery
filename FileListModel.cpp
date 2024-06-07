#include "FileListModel.h"
#include "DecodeManager.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QDebug>
#include <QSet>
#include <QFileInfo>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QStack>

#include <chrono>
using namespace std::chrono_literals;

FileListModel::FileListModel(QObject *parent)
    : QAbstractItemModel(parent) {
    _lastId = 0;
    _currentViewIndex = -1;

    _decodeManager = new DecodeManager(this);

    connect(_decodeManager, &DecodeManager::imageInfoReady, this, [&] (const ImageInfo &result) {
        auto it = _fileToItem.find(result.path);
        // qDebug() << "INFO RECEIVED" << result.path << result.imageSize << result.orientation;
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            item->fullSize = result.imageSize;
            item->exif = result.exif;
            item->lastModified = result.lastModified;
            item->orientation = result.orientation;
            item->mimeType = result.mimeType;

            QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
            if (!modelIndex.isValid()) {
                qDebug() << "Invalid model index" << item->index << item->parent << item->fullPath();
                return;
            }
            QList<int> roles = {ImageFullSizeRole, ExifRole};
            if (result.isLast) {
                roles.append(TimeToFlushRole);
            }
            emit dataChanged(modelIndex, modelIndex, roles);

            if (result.isFromEmbeddedView) {
                QList<ImageDecodeRequest> requests;
                QSize thumbnailSize(result.imageSize.width() * (qreal(_uiTargetHeight) / result.imageSize.height()), _uiTargetHeight);
                requests.append(ImageDecodeRequest{result.path, thumbnailSize, result.orientation});
                decodeImages(requests);
            }
        }
        else {
            qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
        }
    });

    connect(_decodeManager, &DecodeManager::imageReady, this, [&] (const ImageDecodeRequest &request, const QImage &image) {
        // qDebug() << "ZZ IMAGE READEY" << request.path << image.size() << request.orientation;
        auto it = _fileToItem.find(request.path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (request.viewerRequest) {
                QString imageId = generateNewId();
                _viewerImages[request.path] = {image, imageId, (int)_viewerImages.size(), image.size()};
                _imageIdToViewer[imageId] = request.path;
                if (item->index == _currentViewIndex) {
                    emit viewerImageIdChanged(QString("image://thumbnails/") + imageId);
                }
            }
            else {
                item->image = image;
                item->isCachedThumbnail = false; // TODO: DO
                updateImageId(item);

                QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
                emit dataChanged(modelIndex, modelIndex, {ImageIdRole});
            }
        }
        else {
            qDebug() << "Decoded image is not found in model" << request.path;
        }
    });

    connect(_decodeManager, &DecodeManager::folderListReady, this, [&] (const QString &path, int totalImages, const QList<QFileInfo> &result) {
        if (totalImages == -1) {
            return;
        }
        if (!result.size()) {
            return;
        }

        _folderImagePaths.removeOne(path);
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();

            QList<ImageFile *> subImages;
            subImages.reserve(result.size());
            QStringList imagePaths;
            imagePaths.reserve(result.size());
            for (int i = 0; i < result.size(); i++) {
                ImageFile *subItem = createFileItem(path, result.at(i).fileName(), result.at(i).lastModified());
                subItem->parent = item;
                subItem->index = subImages.size();
                subImages.append(subItem);
                imagePaths.append(QDir::toNativeSeparators(result.at(i).absoluteFilePath()));
            }

            beginInsertRows(indexFromItem(item), 0, subImages.size() - 1);
            item->subfiles = subImages;
            endInsertRows();
            if (_folderModels.contains(item->index)) {
                _folderModels[item->index]->resetModel();
            }

            QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
            emit dataChanged(modelIndex, modelIndex, {FolderViewRole});

            _decodeManager->readImagesInfo(imagePaths, true);
        }
    });
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int,QByteArray> names;
    names[Qt::DisplayRole] = "displayRole";
    names[Qt::DecorationRole] = "decorationRole";
    names[ImageRole] = "imageRole";
    names[ImageIdRole] = "imageIdRole";
    names[FolderRole] = "folderRole";
    names[ImageFullSizeRole] = "imageFullSizeRole";

    return names;
}

int FileListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        ImageFile *imageFile = itemFromIndex(parent);
        if (imageFile) {
            return imageFile->subfiles.size();
        }
        return 0;
    }
    return _items.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    ImageFile *imageFile = itemFromIndex(index);
    if (imageFile) {
        if (role == Qt::DisplayRole) {
            return imageFile->fileName;
        }
        else if (role == ImageIdRole) {
            return imageFile->imageId;
        }
        else if (role == FolderRole) {
            return imageFile->isFolder;
        }
        else if (role == ImageRole) {
            return imageFile->image;
        }
        else if (role == ImageFullSizeRole) {
            return imageFile->fullSize;
        }
        else if (role == ImageFileRole) {
            return QVariant::fromValue(imageFile);
        }
        else if (role == FolderViewRole) {
            return imageFile->subfiles.size() != 0;
        }
    }
    return QVariant();
}

QModelIndex FileListModel::index(int row, int column, const QModelIndex &parent) const {
    if (hasIndex(row, column, parent)) {
        if (parent.isValid()) {
            ImageFile *imageFile = itemFromIndex(parent);
            if (row < imageFile->subfiles.size()) {
                return createIndex(row, column, imageFile->subfiles.at(row));
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
    if (!imageFile || !imageFile->parent) {
        return QModelIndex();
    }

    return index(imageFile->parent->index, 0, QModelIndex());
}

int FileListModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

void FileListModel::prepareToClose() {
    _decodeManager->prepareToClose();
    qApp->exit(0);
}

int FileListModel::cd(QString path, QString itemToSelect) {
    _root = path;
    int indexToSelect = 0;

    beginResetModel();
    cleanupModelBeforeCd();

    if (path == "Computer") {
        for (const auto &drive : QDir::drives()) {
            ImageFile *item = new ImageFile();
            item->fileName = QDir::toNativeSeparators(drive.path());
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            item->iconPath = "qrc:/resources/DriveIcon.svg";
            _items.append(item);

            if (item->fileName == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }
    }
    else {
        QDir dir(_root);
        QStringList folders = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System);
        for (const auto &folder : folders) {
            ImageFile *item = new ImageFile();
            item->folderPath = _root;
            item->fileName = folder;
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            item->iconPath = "qrc:/resources/FolderIcon.svg";
            _items.append(item);

            QString path = item->fullPath();

            _fileToItem.insert(path, item);
            _folderImagePaths.append(path);

            if (item->fileName == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        auto files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            ImageFile *item = createFileItem(_root, file.fileName(), file.lastModified());
            if (item->isImage) {
                _imagePaths.append(item->fullPath());
            }
            item->index = _items.size();
            _items.append(item);
        }
    }
    endResetModel();

    _decodeManager->readImagesInfo(_imagePaths, false);
    _decodeManager->readFolderList(_folderImagePaths, 16);

    return indexToSelect;
}

QString FileListModel::rootPath() const {
    return _root;
}

const ImageFile *FileListModel::itemForImageId(QString imageId) {
    auto it = _imageIdToItem.find(imageId);
    if (it != _imageIdToItem.end()) {
        return *it;
    }
    return nullptr;
}

QString FileListModel::generateNewId() {
    QString id = QString::number(_lastId);
    _lastId++;
    return id;
}

void FileListModel::updateImageId(ImageFile *item) {
    QString imageId = item->imageId;
    if (!imageId.isEmpty()) {
        _imageIdToItem.remove(imageId);
    }
    QString newImageId = generateNewId();
    _imageIdToItem.insert(newImageId, item);
    item->imageId = newImageId;
}

ImageFile *FileListModel::createFileItem(const QString &folderPath, const QString &fileName, const QDateTime &lastModified) {
    ImageFile *item = new ImageFile();
    item->folderPath = folderPath;
    item->fileName = fileName;
    item->lastModified = lastModified;
    item->isFolder = false;

    if (isImage(item->fileName)) {
        item->isImage = true;
        QString lowerFileName = item->fileName.toLower();
        item->iconPath = "qrc:/resources/ImageIcon.svg";
//                updateImageId(item);
        QString path = item->fullPath();
        _fileToItem.insert(path, item);
    }
    else {
        item->isImage = false;
        item->iconPath = "qrc:/resources/FileIcon.svg";
    }
    return item;
}

void FileListModel::cleanupModelBeforeCd() {
    cancelAllRunners();

    //Viewer
    _viewerImages.clear();
    _imageIdToViewer.clear();
    _currentViewIndex = -1;

    for (auto it = _folderModels.begin(); it != _folderModels.end(); ++it) {
        it.value()->deleteLater();
    }
    _folderModels.clear();

    _fileToItem.clear();
    _imageIdToItem.clear();
    _folderImagePaths.clear();
    _imagePaths.clear();
    for (int i = 0; i < _items.size(); i++) {
        for (int j = 0; j < _items[i]->subfiles.size(); j++) {
            delete _items[i]->subfiles[j];
        }
        delete _items[i];
    }
    _items.clear();
}

ImageFile *FileListModel::itemFromIndex(const QModelIndex &index) {
    return static_cast<ImageFile*>(index.internalPointer());
}

QModelIndex FileListModel::indexFromItem(const ImageFile *item) const {
    if (!item) {
        return QModelIndex();
    }
    return index(item->index, 0, indexFromItem(item->parent));
}

QAbstractItemModel *FileListModel::folderModel(int index_) {
    auto it = _folderModels.find(index_);
    if (it == _folderModels.end()) {
        RootProxyModel *proxy = new RootProxyModel(this);
        proxy->setRoot(_items[index_]);
        proxy->setSourceModel(this);
        _folderModels[index_] = proxy;
        return proxy;
    }
    return *it;
}

struct FolderInfo {
    int level;                     // Nesting level of the folder
    QString path;                  // Absolute path of the folder
    QString lastInGroup;           // String where each character represents a level: '1' for last, '0' for not last

    FolderInfo(int lvl, QString pth, QString lastGroup)
        : level(lvl), path(pth), lastInGroup(lastGroup) {}
};

QList<FolderInfo> getAllSubfoldersWithNestingLevel(const QString &startDir) {
    QList<FolderInfo> allFoldersWithLevels;         // List to store folders with their nesting levels and boolean string
    QStack<FolderInfo> dirs;                        // Stack to manage directories
    dirs.push(FolderInfo(0, startDir, ""));        // Start with the initial directory, marked as last in its (non-existent) group

    while (!dirs.isEmpty()) {
        FolderInfo dirInfo = dirs.pop();

        // Add the current directory to the list
        allFoldersWithLevels.append(dirInfo);

        // Get a list of all subdirectories in the current directory
        QStringList subDirs = QDir(dirInfo.path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (int i = subDirs.count() - 1; i >= 0; --i) {
            QString subDir = subDirs.at(i);
            QString newPath = QDir::toNativeSeparators(QDir(dirInfo.path).filePath(subDir));

            // Determine if this is the last subdirectory in the list
            QString isLast = (i == subDirs.count() - 1) ? "0" : "1";

            // Create a new boolean string for the next level based on the current dir's string
            QString newLastInGroup = dirInfo.lastInGroup + isLast;

            dirs.push(FolderInfo(dirInfo.level + 1, newPath, newLastInGroup));
        }
    }

    return allFoldersWithLevels;
}


void FileListModel::enterRecursiveView() {
    beginResetModel();
    cleanupModelBeforeCd();

    // if (_root == "Computer") {
    //     for (const auto &drive : QDir::drives()) {
    //         ImageFile *item = new ImageFile();
    //         item->fileName = QDir::toNativeSeparators(drive.path());
    //         item->isFolder = true;
    //         item->isImage = false;
    //         item->index = _items.size();
    //         item->iconPath = "qrc:/resources/DriveIcon.svg";
    //         _items.append(item);

    //         if (item->fileName == itemToSelect) {
    //             indexToSelect = _items.size() - 1;
    //         }
    //     }
    // }
    // else
    {
        QList<FolderInfo> folders = getAllSubfoldersWithNestingLevel(_root);
        for (const auto &folder : folders) {
            ImageFile *item = new ImageFile();
            QFileInfo info(folder.path);
            // qDebug() << folder.level << info.filePath() << info.fileName();
            item->folderPath = QDir::toNativeSeparators(info.dir().absolutePath());
            item->nestingInfo = folder.lastInGroup;
            item->fileName = info.fileName(); // QString("%1: %2").arg(folder.first).arg(folder.second);
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            item->iconPath = "qrc:/resources/FolderIcon.svg";
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
    endResetModel();

    _decodeManager->readImagesInfo(_imagePaths, true);
    _decodeManager->readFolderList(_folderImagePaths, 16);
}

bool FileListModel::isImage(QString fileName) {
    return ThumbnailLoader::isJpeg(fileName) || ThumbnailLoader::isRawOrTiff(fileName) || ThumbnailLoader::isImageOther(fileName);
}

void FileListModel::requestViewer(int index, int width, int height) {
    _currentViewIndex = index;

    QSize viewerSize(width, height);
    QString requestedPath = _items[index]->fullPath();
    auto it = _viewerImages.find(requestedPath);
    if (it != _viewerImages.end()) {
        emit viewerImageIdChanged(QString("image://thumbnails/") + it.value().imageId);

        auto thumbnailIt = _fileToItem.find(requestedPath);
        // qDebug() << "INFO RECEIVED" << result.path << result.imageSize << result.orientation;
        if (thumbnailIt != _fileToItem.end()) {
            ImageFile *item = thumbnailIt.value();

            QSize targetSize = item->fullSize;
            if (viewerSize.isValid()) {
                targetSize = targetSize.scaled(viewerSize, Qt::KeepAspectRatio);
            }

            if (it.value().requestedSize == targetSize) {
                return;
            }
        }
    }

//    qDebug() << "Request thumbnails" << index << viewerSize;

    int queueSize = 16;

    QList<ImageDecodeRequest> requests;
    for (int i = index; i < _items.size(); i++) {
        if (requests.size() >= queueSize) {
            break;
        }
        if (_items[i]->isImage) {
            QSize targetSize = _items[i]->fullSize;
            if (viewerSize.isValid()) {
                targetSize = targetSize.scaled(viewerSize, Qt::KeepAspectRatio);
            }
            ImageDecodeRequest request{_items[i]->fullPath(), targetSize, _items[i]->orientation, true};
            requests.append(request);
        }
    }
    int backwardInsertIndex = 2;
    for (int i = index - 1; i >= 0; i--) {
        if (requests.size() >= queueSize * 1.5) {
            break;
        }
        if (_items[i]->isImage) {
            if (backwardInsertIndex >= requests.count()) {
                backwardInsertIndex = requests.count();
            }
            QSize targetSize = _items[i]->fullSize;
            if (viewerSize.isValid()) {
                targetSize = targetSize.scaled(viewerSize, Qt::KeepAspectRatio);
            }
            ImageDecodeRequest request{_items[i]->fullPath(), targetSize, _items[i]->orientation, true};
            requests.insert(backwardInsertIndex, request);
            backwardInsertIndex += 2;
        }
    }

    _decodeManager->decodeImages(requests);
}

QImage FileListModel::viewerForImageId(QString imageId) {
    auto it = _imageIdToViewer.find(imageId);
    if (it != _imageIdToViewer.end()) {
        QString path = *it;
        return _viewerImages[path].image;
    }
    return QImage();
}

void FileListModel::cancelAllRunners() {
    _decodeManager->cancelAllRunners();
}

void FileListModel::cancelAllDecodeRunners() {
    _decodeManager->cancelAllDecodeRunners();
}

void FileListModel::cancelAllDecodeViewerRunners() {
    _decodeManager->cancelAllDecodeViewerRunners();
}

void FileListModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    _decodeManager->decodeImages(requests);
}

int FileListModel::fileIndex(QString fileName) const {
    for (int i = 0; i < _items.size(); i++) {
        if (!_items[i]->fileName.compare(fileName, Qt::CaseInsensitive)) {
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
    _sourceRoot = root;
}

void RootProxyModel::setSourceModel(QAbstractItemModel *sourceModel) {
    QAbstractProxyModel::setSourceModel(sourceModel);

    connect(sourceModel, &QAbstractItemModel::dataChanged, this,
            [&] (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles = QList<int>()) {
        if (FileListModel::itemFromIndex(topLeft.parent()) == _sourceRoot) {
            emit dataChanged(mapFromSource(topLeft), mapFromSource(bottomRight), roles);
        }
    });
}

QModelIndex RootProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (!_sourceRoot || !sourceModel()) {
        return QModelIndex();
    }
    return createIndex(row, column, _sourceRoot->subfiles[row]);
}

QModelIndex RootProxyModel::parent(const QModelIndex &child) const {
    return QModelIndex();
}

int RootProxyModel::rowCount(const QModelIndex &parent) const {
    if (!_sourceRoot || !sourceModel()) {
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
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->cancelAllRunners();
}

void RootProxyModel::cancelAllDecodeRunners() {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->cancelAllDecodeRunners();
}

void RootProxyModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->decodeImages(requests);
}

void RootProxyModel::resetModel() {
    beginResetModel();
    endResetModel();
}

int FileListModel::uiTargetHeight() const {
    return _uiTargetHeight;
}

void FileListModel::setUiTargetHeight(int newUiTargetHeight) {
    if (_uiTargetHeight == newUiTargetHeight) {
        return;
    }
    _uiTargetHeight = newUiTargetHeight;
    emit uiTargetHeightChanged();
}
