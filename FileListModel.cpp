#include "FileListModel.h"
#include "DecodeManager.h"
#include "NaturalSort.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QDebug>
#include <QSet>
#include <QFileInfo>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QStack>
#include <QStandardPaths>
#include <QDateTime>
#include <QUrl>

#include <chrono>
using namespace std::chrono_literals;

FileListModel::FileListModel(QObject *parent)
    : QAbstractItemModel(parent) {
    _lastId = 0;
    _currentViewIndex = -1;

    _decodeManager = new DecodeManager(this);

    connect(_decodeManager, &DecodeManager::viewerRunnerCanceled, this, [&] (const QString &path) {
        qDebug() << "REMOVE CANCELLED RUNNER???" << path;
        auto it = _viewerImages.find(path);
        if (it != _viewerImages.end()) {
            if (it.value().image.isNull()) {
                qDebug() << "REMOVE CANCELLED RUNNER" << path;
                _viewerImages.remove(path);
            }
        }
        auto fullSizeIt = _fullSizeViewerImages.find(path);
        if (fullSizeIt != _fullSizeViewerImages.end()) {
            if (fullSizeIt.value().image.isNull()) {
                qDebug() << "REMOVE CANCELLED FULL SIZE RUNNER" << path;
                _fullSizeViewerImages.remove(path);
            }
        }
    });

    connect(_decodeManager, &DecodeManager::runningTasksChanged, [&] (const QString &runningTasks, const QStringList &tasksInfo) {
        if (runningTasksDebug()) {
            QFile f(QString("C:\\tmp\\log\\%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
            f.open(QFile::WriteOnly);
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

    connect(_decodeManager, &DecodeManager::imageInfoReady, this, [&] (const ImageInfo &result) {
        if (result.directOpenGeneration && result.directOpenGeneration != _directOpen.generation) {
            return;
        }

        auto it = _fileToItem.find(result.path);
        // qDebug() << "INFO RECEIVED" << result.path << result.imageSize;
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            item->setFullSize(rotateToOrientation(result.imageSize, result.orientation));
            item->setInfo(result);
            if (!result.imageSize.isValid()) {
                handleDirectOpenImageInfo(result);
                return;
            }

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

            if (result.isFromEmbeddedView) {
                decodeImages({imageDecodeRequestFromEmbeddedImageInfo(result)});
            }

            handleDirectOpenImageInfo(result);
        }
        else {
            qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
        }
    });

    connect(_decodeManager, &DecodeManager::imagesInfoReady, this, [&] (const QList<ImageInfo> &results) {
        if (!results.size()) {
            return;
        }
        // qDebug() << "ZZ on DecodeManager::imagesInfoReady" << results.size();
        QList<ImageInfo> currentResults;
        currentResults.reserve(results.size());
        for (const ImageInfo &result : results) {
            if (!result.directOpenGeneration || result.directOpenGeneration == _directOpen.generation) {
                currentResults.append(result);
            }
        }
        if (!currentResults.size()) {
            return;
        }

        QList<ImageDecodeRequest> requests;

        int flushIndex = -1;
        int minIndex = 1000000;
        int maxIndex = -1;
        // TODO: If differents parent come in one signal here it will mess everything up
        QModelIndex parent;

        for (const ImageInfo &result : currentResults) {
            auto it = _fileToItem.find(result.path);
            // qDebug() << "INFO RECEIVED" << result.path << result.imageSize << result.orientation;
            if (it != _fileToItem.end()) {
                ImageFile *item = it.value();
                item->setFullSize(rotateToOrientation(result.imageSize, result.orientation));
                item->setInfo(result);

                minIndex = qMin(minIndex, item->index());
                maxIndex = qMax(maxIndex, item->index());
                parent = indexFromItem(item->imageFileParent());

                if (result.isLast) {
                    flushIndex = item->index();
                }

                if (result.isFromEmbeddedView) {
                    requests.append(imageDecodeRequestFromEmbeddedImageInfo(result));
                }

                handleDirectOpenImageInfo(result);
            }
            else {
                qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
            }
        }

        QModelIndex minModelIndex = index(minIndex, 0, parent);
        QModelIndex maxModelIndex = index(maxIndex, 0, parent);
        if (!minModelIndex.isValid() || !maxModelIndex.isValid()) {
            qDebug() << "Invalid model index" << minModelIndex << maxModelIndex << parent;
            return;
        }
        emit dataChanged(minModelIndex, maxModelIndex, {ImageFullSizeRole});

        if (flushIndex != -1) {
            QModelIndex flushModelIndex = index(flushIndex, 0, parent);
            emit dataChanged(flushModelIndex, flushModelIndex, {TimeToFlushRole});
        }

        if (requests.size()) {
            decodeImages(requests);
        }
    });

    connect(_decodeManager, &DecodeManager::imageReady, this, [&] (const ImageDecodeRequest &request,
                                                                   const QImage &image, const DecodedImageInfo &decodedInfo) {
        if (request.info.directOpenGeneration && request.info.directOpenGeneration != _directOpen.generation) {
            return;
        }

        // image.save(QString("c:/tmp/zg/%1.png").arg(QFileInfo(request.info.path).fileName()));
        // qDebug() << "ZZ IMAGE READEY" << request.info.path << request.info.imageSize << request.targetSize << image.size();
        auto it = _fileToItem.find(request.info.path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (request.viewerRequest) {
                // qDebug() << "ZZ Viewer image came" << request.info.path << isFromCache << image.size() << item->image.size();
            }
            if (decodedInfo.isFromCache && (image.width() <= item->image().width() ||
                                            image.height() <= item->image().height())) {
                handleDirectOpenImageReady(request, image, decodedInfo);
                return;
            }
            if (request.viewerRequest) {
                // qDebug() << "ZZ Viewer image SET";
                QString imageId = generateNewId();
                if (request.targetSize == request.info.imageSize ||
                    request.targetSize == rotateToOrientation(request.info.imageSize, request.info.orientation)) {
                    auto it = _fullSizeViewerImages.find(request.info.path);
                    if (it != _fullSizeViewerImages.end()) {
                        if (it->image.width() > image.width() && it->image.height() > image.height()) {
                            handleDirectOpenImageReady(request, image, decodedInfo);
                            return;
                        }
                    }

                    if (it != _fullSizeViewerImages.end() && decodedInfo.isFromCache) {
                        _fullSizeViewerImages[request.info.path] = ViewerImage{
                            .image = image,
                            .imageId = imageId,
                            .requestedSize = it->requestedSize, // not updating this
                            .decodedInfo = decodedInfo
                        };
                    }
                    else {
                        _fullSizeViewerImages[request.info.path] = ViewerImage{
                            .image = image,
                            .imageId = imageId,
                            .requestedSize = image.size(),
                            .decodedInfo = decodedInfo
                        };
                    }
                    _imageIdToFullSizeViewer[imageId] = request.info.path;
                    if (item->index() == _currentViewIndex) {
                        emit viewerImageIdUrlChanged(QString("image://async/") + imageId, 2);
                    }
                }
                else {
                    auto it = _viewerImages.find(request.info.path);
                    if (it != _viewerImages.end()) {
                        if (it->image.width() > image.width() && it->image.height() > image.height()) {
                            handleDirectOpenImageReady(request, image, decodedInfo);
                            return;
                        }
                    }

                    if (it != _viewerImages.end() && decodedInfo.isFromCache) {
                        _viewerImages[request.info.path] = ViewerImage{
                            .image = image,
                            .imageId = imageId,
                            .requestedSize = it->requestedSize, // not updating this
                            .decodedInfo = decodedInfo
                        };
                    }
                    else {
                        _viewerImages[request.info.path] = ViewerImage{
                            .image = image,
                            .imageId = imageId,
                            .requestedSize = image.size(),
                            .decodedInfo = decodedInfo
                        };
                    }
                    _imageIdToViewer[imageId] = request.info.path;
                    if (item->index() == _currentViewIndex) {
                        emit viewerImageIdUrlChanged(QString("image://thumbnails/") + imageId, 1);
                    }
                }
            }
            else {
                item->setImage(image);
                item->setIsCachedThumbnail(decodedInfo.isFromCache);
                updateImageId(item);
            }

            handleDirectOpenImageReady(request, image, decodedInfo);
        }
        else {
            qDebug() << "Decoded image is not found in model" << request.info.path;
        }
    });

    connect(_decodeManager, &DecodeManager::folderListReady, this, [&] (const QString &path, const QList<FileInfo> &subfiles) {
        if (!subfiles.size()) {
            return;
        }

        _folderImagePaths.removeOne(path);
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();

            QList<ImageFile *> subImages;
            subImages.reserve(subfiles.size());
            QStringList imagePaths;
            imagePaths.reserve(subfiles.size());
            for (int i = 0; i < subfiles.size(); i++) {
                ImageFile *subItem = createFileItem(path, subfiles.at(i).name, subfiles.at(i).lastModified);
                subItem->setImageFileParent(item);
                subItem->setIndex(subImages.size());
                subImages.append(subItem);
                imagePaths.append(QDir(path).absoluteFilePath(subfiles.at(i).name));
            }

            beginInsertRows(indexFromItem(item), 0, subImages.size() - 1);
            item->setSubfiles(subImages);
            endInsertRows();
            if (_folderModels.contains(item->index())) {
                _folderModels[item->index()]->resetModel();
            }

            QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
            emit dataChanged(modelIndex, modelIndex, {FolderViewRole});

            _decodeManager->readImagesInfo(imagePaths, true);
        }
    });
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int,QByteArray> names;
    // names[Qt::DisplayRole] = "displayRole";
    names[ImageIdUrlRole] = "imageIdUrlRole";
    names[SelectedRole] = "selectedRole";
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
    PersistentSelectionCache::dumpDb();
    _decodeManager->prepareToClose();
    qApp->exit(0);
}

int FileListModel::cd(const QString &path, const QString &itemToSelect) {
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();

    _root = path;

    beginResetModel();
    cleanupModelBeforeCd();
    int indexToSelect = populateFolderItems(path, itemToSelect);
    loadSelectionStatesForVisibleItems();
    endResetModel();

    startRegularFolderWork();

    return indexToSelect;
}

int FileListModel::populateFolderItems(const QString &path, const QString &itemToSelect) {
    int indexToSelect = 0;
    if (path == "Computer") {
        for (const auto &drive : QDir::drives()) {
            QString drivePath = drive.path();
            if (drivePath.endsWith("/") && !drivePath.startsWith("/")) {
                drivePath = drivePath.left(drivePath.size() - 1);
            }
            ImageFile *item = new ImageFile(this);
            item->setFileName(drivePath);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/resources/DriveIcon.svg");
            item->setIndex(_items.size());
            _items.append(item);

            if (item->fileName() == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }
    }
    else {
        QDir dir(_root);
        QStringList folders = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System, QDir::NoSort);
        sortNamesNaturally(folders);
        for (const auto &folder : folders) {
            ImageFile *item = new ImageFile(this);
            item->setFolderPath(_root);
            item->setFileName(folder);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/resources/FolderIcon.svg");
            item->setIndex(_items.size());
            _items.append(item);

            QString path = item->fullPath();

            _fileToItem.insert(path, item);
            _folderImagePaths.append(path);

            if (item->fileName() == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        auto files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System, QDir::NoSort);
        sortFileInfosNaturally(files);
        for (const auto &file : files) {
            ImageFile *item = createFileItem(_root, file.fileName(), file.lastModified());
            if (item->isImage()) {
                _imagePaths.append(item->fullPath());
            }
            item->setIndex(_items.size());
            _items.append(item);
        }
    }

    return indexToSelect;
}

void FileListModel::startRegularFolderWork() {
    _decodeManager->readImagesInfo(_imagePaths, false);
    _decodeManager->readFolderList(_folderImagePaths, 16); // TODO: FIX!
}

QString FileListModel::rootPath() const {
    return _root;
}

const ImageFile *FileListModel::itemForImageId(const QString &imageId) {
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
    QString imageId = item->imageIdUrl();
    if (!imageId.isEmpty()) {
        _imageIdToItem.remove(imageId);
    }
    QString newImageId = generateNewId();
    _imageIdToItem.insert(newImageId, item);
    item->setImageId(newImageId);

    QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
    emit dataChanged(modelIndex, modelIndex, {ImageIdUrlRole});
}

ImageFile *FileListModel::createFileItem(const QString &folderPath, const QString &fileName, const QDateTime &lastModified) {
    ImageFile *item = new ImageFile(this);
    item->setFolderPath(folderPath);
    item->setFileName(fileName);
    item->setIsFolder(false);
    ImageInfo info = {
        .path = item->fullPath(),
        .lastModified = lastModified,
    };
    item->setInfo(info);

    if (isImage(item->fileName())) {
        item->setIsImage(true);
        QString lowerFileName = item->fileName().toLower();
        item->setIconPath("qrc:/resources/ImageIcon.svg");
//                updateImageId(item);
        QString path = item->fullPath();
        _fileToItem.insert(path, item);
    }
    else {
        item->setIsImage(false);
        item->setIconPath("qrc:/resources/FileIcon.svg");
    }
    return item;
}

void FileListModel::cleanupModelBeforeCd() {
    cancelAllRunners();
    clearModelData(true);
}

void FileListModel::clearModelData(bool clearViewerData) {
    if (clearViewerData) {
        // Viewer
        _viewerImages.clear();
        _fullSizeViewerImages.clear();
        _imageIdToViewer.clear();
        _imageIdToFullSizeViewer.clear();
        emit viewerReset();
    }
    _currentViewIndex = -1;
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();

    for (auto it = _folderModels.begin(); it != _folderModels.end(); ++it) {
        it.value()->deleteLater();
    }
    _folderModels.clear();

    _fileToItem.clear();
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
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();

    beginResetModel();
    cleanupModelBeforeCd();

    // if (_root == "Computer") {
    //     for (const auto &drive : QDir::drives()) {
    //         ImageFile *item = new ImageFile();
    //         item->fileName = drive.path();
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
        QList<RecursiveFolderInfo> folders = getAllSubfoldersWithNestingLevel(_root);
        for (const auto &folder : folders) {
            QFileInfo info(folder.path);
            // qDebug() << folder.level << info.filePath() << info.fileName();
            ImageFile *item = new ImageFile(this);
            item->setFolderPath(info.dir().absolutePath());
            item->setFileName(info.fileName()); // QString("%1: %2").arg(folder.first).arg(folder.second);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/resources/FolderIcon.svg");
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

    _decodeManager->readImagesInfo(_imagePaths, true);
    _decodeManager->readFolderList(_folderImagePaths, 16);
}

bool FileListModel::isImage(const QString &fileName) {
    return ThumbnailLoader::isFormatSupported(fileName);
}

void FileListModel::openImageDirectly(const QString &path, int width, int height) {
    QString imagePath = path.trimmed();
    if (imagePath.startsWith("\"")) {
        imagePath = imagePath.right(imagePath.size() - 1);
    }
    if (imagePath.endsWith("\"")) {
        imagePath = imagePath.left(imagePath.size() - 1);
    }
    imagePath = imagePath.trimmed();

    QFileInfo fileInfo(imagePath);
    if (!fileInfo.isFile() || !isImage(fileInfo.fileName())) {
        return;
    }

    const QString folderPath = fileInfo.dir().absolutePath();
    const QString fileName = fileInfo.fileName();
    const QString fullPath = QDir(folderPath).absoluteFilePath(fileName);

    const int generation = _directOpen.generation + 1;
    _directOpen = DirectOpenState();
    _directOpen.generation = generation;
    _directOpen.stage = DirectOpenStage::WaitingInfo;
    _directOpen.path = fullPath;
    _directOpen.folderPath = folderPath;
    _directOpen.fileName = fileName;
    _directOpen.viewerSize = QSize(width, height);

    _decodeManager->cancelAllRunners();

    int existingIndex = -1;
    if (!_root.isEmpty() && _root != "Computer" &&
        !QString::compare(QDir(_root).absolutePath(), folderPath, Qt::CaseInsensitive)) {
        existingIndex = fileIndex(fileName);
    }

    _directOpen.sameFolder = existingIndex >= 0;
    if (_directOpen.sameFolder) {
        _directOpen.currentIndex = existingIndex;
        _currentViewIndex = existingIndex;
    }
    else {
        beginResetModel();
        clearModelData(true);
        _root = folderPath;

        ImageFile *item = createFileItem(folderPath, fileName, fileInfo.lastModified());
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
        _decodeManager->readImagesInfo({fullPath}, false, generation);
    }
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
            _directOpen.stage = DirectOpenStage::None;
            return;
        }

        _directOpen.info = result;
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
    if (_directOpen.viewerSize.isValid()) {
        targetSize = targetSize.scaled(_directOpen.viewerSize, Qt::KeepAspectRatio);
    }
    if (!targetSize.isValid()) {
        return;
    }

    auto viewerIt = _viewerImages.find(_directOpen.path);
    if (viewerIt == _viewerImages.end()) {
        _viewerImages[_directOpen.path] = ViewerImage{.requestedSize = targetSize};
    }
    else if (viewerIt->requestedSize.width() < targetSize.width() ||
             viewerIt->requestedSize.height() < targetSize.height()) {
        viewerIt->requestedSize = targetSize;
    }

    ImageInfo info = _directOpen.info;
    info.directOpenGeneration = _directOpen.generation;
    _directOpen.stage = DirectOpenStage::WaitingFitDecode;
    _decodeManager->decodeImages({ImageDecodeRequest{
        .info = info,
        .targetSize = targetSize,
        .viewerRequest = true,
        .checkCache = info.isCached
    }});
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

    auto fullSizeIt = _fullSizeViewerImages.find(_directOpen.path);
    if (fullSizeIt == _fullSizeViewerImages.end()) {
        _fullSizeViewerImages[_directOpen.path] = ViewerImage{.requestedSize = targetSize};
    }
    else if (fullSizeIt->requestedSize.width() < targetSize.width() ||
             fullSizeIt->requestedSize.height() < targetSize.height()) {
        fullSizeIt->requestedSize = targetSize;
    }

    ImageInfo info = _directOpen.info;
    info.directOpenGeneration = _directOpen.generation;
    _directOpen.stage = DirectOpenStage::WaitingFullDecode;
    _decodeManager->decodeImages({ImageDecodeRequest{
        .info = info,
        .targetSize = targetSize,
        .viewerRequest = true,
        .checkCache = info.isCached
    }});
}

bool FileListModel::handleDirectOpenImageReady(const ImageDecodeRequest &request, const QImage &image,
                                               const DecodedImageInfo &decodedInfo) {
    if (!isActiveDirectOpenRequest(request)) {
        return false;
    }

    if (request.info.path == _directOpen.path && _directOpen.stage == DirectOpenStage::WaitingFitDecode) {
        auto itemIt = _fileToItem.find(_directOpen.path);
        if (itemIt != _fileToItem.end() && !image.isNull() && itemIt.value()->imageIdUrl().isEmpty()) {
            itemIt.value()->setImage(image);
            itemIt.value()->setIsCachedThumbnail(decodedInfo.isFromCache);
            updateImageId(itemIt.value());
        }

        if (_directOpen.currentIndex >= 0) {
            emit directOpenReady(_directOpen.currentIndex);
        }
        requestDirectOpenFullSizeDecode();
        return true;
    }

    const bool fullSizeRequest = request.targetSize == request.info.imageSize ||
                                 request.targetSize == rotateToOrientation(request.info.imageSize, request.info.orientation);
    if (request.info.path == _directOpen.path && _directOpen.stage == DirectOpenStage::WaitingFullDecode &&
        fullSizeRequest) {
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

void FileListModel::populateFolderAfterDirectOpenFullDecode() {
    if (_directOpen.stage != DirectOpenStage::WaitingFullDecode) {
        return;
    }

    if (!_directOpen.sameFolder) {
        beginResetModel();
        clearModelData(false);
        _root = _directOpen.folderPath;
        populateFolderItems(_root, _directOpen.fileName);
        loadSelectionStatesForVisibleItems();

        auto targetIt = _fileToItem.find(_directOpen.path);
        if (targetIt != _fileToItem.end()) {
            targetIt.value()->setFullSize(rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation));
            targetIt.value()->setInfo(_directOpen.info);
        }
        endResetModel();

        const int targetIndex = fileIndex(_directOpen.fileName);
        _directOpen.currentIndex = targetIndex >= 0 ? targetIndex : 0;
    }

    _currentViewIndex = _directOpen.currentIndex;

    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt != _fileToItem.end()) {
        ImageFile *item = itemIt.value();
        item->setFullSize(rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation));
        item->setInfo(_directOpen.info);

        auto viewerIt = _viewerImages.find(_directOpen.path);
        if (viewerIt != _viewerImages.end() && !viewerIt->image.isNull() && item->imageIdUrl().isEmpty()) {
            item->setImage(viewerIt->image);
            item->setIsCachedThumbnail(viewerIt->decodedInfo.isFromCache);
            updateImageId(item);
        }

        QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
        if (modelIndex.isValid()) {
            emit dataChanged(modelIndex, modelIndex, {ImageFullSizeRole, ImageIdUrlRole});
        }
    }

    if (_directOpen.currentIndex >= 0) {
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
        _decodeManager->readImagesInfo(pathsNeedingInfo, false, _directOpen.generation);
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
        QSize targetSize = item->fullSize();
        if (_directOpen.viewerSize.isValid()) {
            targetSize = targetSize.scaled(_directOpen.viewerSize, Qt::KeepAspectRatio);
        }
        if (!targetSize.isValid()) {
            continue;
        }

        const QString requestedPath = item->fullPath();
        auto viewerIt = _viewerImages.find(requestedPath);
        if (viewerIt != _viewerImages.end() && !viewerIt->image.isNull() &&
            viewerIt->requestedSize.width() >= targetSize.width() &&
            viewerIt->requestedSize.height() >= targetSize.height()) {
            continue;
        }
        if (viewerIt == _viewerImages.end()) {
            _viewerImages[requestedPath] = ViewerImage{.requestedSize = targetSize};
        }
        else {
            viewerIt->requestedSize = targetSize;
        }

        ImageInfo info = item->info();
        info.directOpenGeneration = _directOpen.generation;
        requests.append(ImageDecodeRequest{
            .info = info,
            .targetSize = targetSize,
            .viewerRequest = true,
            .checkCache = info.isCached
        });
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
    _decodeManager->decodeImages(requests);
}

void FileListModel::finishDirectOpenPriorityWork() {
    if (_directOpen.stage == DirectOpenStage::None) {
        return;
    }

    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    startRegularFolderWork();
}

void FileListModel::emitViewerImagesForCurrentIndex() {
    if (_currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        return;
    }

    const QString requestedPath = _items[_currentViewIndex]->fullPath();
    if (!_items[_currentViewIndex]->imageIdUrl().isEmpty()) {
        emit viewerImageIdUrlChanged(_items[_currentViewIndex]->imageIdUrl(), 0);
    }

    auto viewerIt = _viewerImages.find(requestedPath);
    if (viewerIt != _viewerImages.end() && !viewerIt->image.isNull()) {
        emit viewerImageIdUrlChanged(QString("image://thumbnails/") + viewerIt->imageId, 1);
    }

    auto fullSizeIt = _fullSizeViewerImages.find(requestedPath);
    if (fullSizeIt != _fullSizeViewerImages.end() && !fullSizeIt->image.isNull()) {
        emit viewerImageIdUrlChanged(QString("image://async/") + fullSizeIt->imageId, 2);
    }
}

void FileListModel::requestViewer(int index, int width, int height) {
    _currentViewIndex = index;
    QSize viewerSize(width, height);

    QString requestedPath = _items[index]->fullPath();
    // qDebug() << __FUNCTION__ << viewerSize << requestedPath;
    auto fullSizeIt = _fullSizeViewerImages.find(requestedPath);
    auto it = _viewerImages.find(requestedPath);
    auto itThumbs = _fileToItem.find(requestedPath);
    if (itThumbs != _fileToItem.end()) {
        qDebug() << "Using thumbnail image" << itThumbs.value()->image().size();
        emit viewerImageIdUrlChanged(itThumbs.value()->imageIdUrl(), 0);
    }
    if (it != _viewerImages.end() && !it->image.isNull()) {
        qDebug() << "Using viewer image" << it->requestedSize << it->image.size();
        emit viewerImageIdUrlChanged(QString("image://thumbnails/") + it.value().imageId, 1);
    }
    if (!viewerSize.isValid() && fullSizeIt != _fullSizeViewerImages.end() && !fullSizeIt->image.isNull()) {
        qDebug() << "Using full size viewer image" << fullSizeIt->requestedSize << fullSizeIt->image.size();
        emit viewerImageIdUrlChanged(QString("image://async/") + fullSizeIt.value().imageId, 2);
    }

    int queueSize = 16;
    QList<ImageDecodeRequest> requests;
    int imagesChecked = 0;
    bool hitStart = false;
    bool hitEnd = false;
    for (int counter = 0; imagesChecked < queueSize && !(hitStart && hitEnd); counter++) {
        int i;
        if (!(counter % 2)) { // n, n+1, n+2, ...
            i = index + counter / 2;
        }
        else { // n-1, n-2, n-3, ...
            i = index - (counter + 1) / 2;
        }
        if (i < 0) {
            hitStart = true;
        }
        if (i >= _items.size()) {
            hitEnd = true;
        }
        if (i >= 0 && i < _items.size() && _items[i]->isImage()) {
            imagesChecked++;

            QString requestedPath = _items[i]->fullPath();
            QSize targetSize = _items[i]->fullSize();

            if (viewerSize.isValid()) {
                targetSize = targetSize.scaled(viewerSize, Qt::KeepAspectRatio);

                auto it = _viewerImages.find(requestedPath);
                if (it != _viewerImages.end()) {
                    if (it->requestedSize.width() >= targetSize.width() &&
                        it->requestedSize.height() >= targetSize.height()) {
                        continue;
                    }
                    else {
                        // qDebug() << "ZZ DEC DUE TO SIZE" << targetSize << "from" << it->requestedSize << requestedPath;
                        it->requestedSize = targetSize;
                    }
                }
                else {
                    _viewerImages[requestedPath] = ViewerImage{
                        .requestedSize = targetSize
                    };
                }
            }
            else {
                // qDebug() << "ZZ REQ FULL SIZE" << requestedPath;
                auto it = _fullSizeViewerImages.find(requestedPath);
                if (it != _fullSizeViewerImages.end()) {
                    if (it->requestedSize.width() >= targetSize.width() &&
                        it->requestedSize.height() >= targetSize.height()) {
                        continue;
                    }
                    else {
                        // qDebug() << "ZZ DEC DUE TO FULL SIZE" << targetSize << "from" << it->requestedSize << requestedPath;
                        it->requestedSize = targetSize;
                    }
                }
                else {
                    _fullSizeViewerImages[requestedPath] = ViewerImage{
                        .requestedSize = targetSize
                    };
                }

            }

            requests.append(ImageDecodeRequest{
                .info = _items[i]->info(),
                .targetSize = targetSize,
                .viewerRequest = true,
                .checkCache = _items[i]->info().isCached
            });
        }
    }
    // qDebug() << __FUNCTION__ << "REQUEST FOR DECODE" << requests.size();

    _decodeManager->decodeImages(requests);
}

QImage FileListModel::viewerForImageId(const QString &imageId) {
    auto it = _imageIdToViewer.find(imageId);
    if (it != _imageIdToViewer.end()) {
        QString path = *it;
        return _viewerImages[path].image;
    }

    return QImage();
}

QImage FileListModel::fullSizeViewerForImageId(const QString &imageId) {
    auto it = _imageIdToFullSizeViewer.find(imageId);
    if (it != _imageIdToFullSizeViewer.end()) {
        QString path = *it;
        return _fullSizeViewerImages[path].image;
    }

    return QImage();
}

void FileListModel::cancelAllRunners() {
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    _decodeManager->cancelAllRunners();
}

void FileListModel::cancelAllDecodeRunners() {
    // qDebug() << __FUNCTION__;
    _decodeManager->cancelAllDecodeRunners();
}

void FileListModel::cancelAllDecodeViewerRunners() {
    _decodeManager->cancelAllDecodeViewerRunners();
}

void FileListModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    _decodeManager->decodeImages(requests);
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
    return createIndex(row, column, _sourceRoot->subfiles().at(row));
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
    qDebug() << __FUNCTION__;
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

void FileListModel::setFolderViewImageSize(int width, int height) {
    if (_folderViewImageSize.width() != width || _folderViewImageSize.height() != height) {
        _folderViewImageSize = QSize(width, height);
        qDebug() << "ZZ TARGET SIZE CHANGED" << _folderViewImageSize;
    }
}

void FileListModel::setFolderViewImageCount(int count) {

}

void FileListModel::startScanner() {
    _decodeManager->scan(_root);
}

bool FileListModel::runningTasksDebug() const {
    return _decodeManager->runningTasksDebug();
}

void FileListModel::setRunningTasksDebug(bool isRunningTasksDebug) {
    if (runningTasksDebug() == isRunningTasksDebug) {
        return;
    }
    _decodeManager->setRunningTasksDebug(isRunningTasksDebug);
    emit runningTasksDebugChanged();
}

void FileListModel::dumpCurrentImage() {
    if (_currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        qDebug() << "No valid current image to dump";
        return;
    }
    
    ImageFile *currentItem = _items.at(_currentViewIndex);
    QString imagePath = currentItem->fullPath();
    
    // Try to get full size viewer image first, fall back to regular viewer image
    QImage imageToSave;
    if (_fullSizeViewerImages.contains(imagePath)) {
        imageToSave = _fullSizeViewerImages[imagePath].image;
    } else if (_viewerImages.contains(imagePath)) {
        imageToSave = _viewerImages[imagePath].image;
    }
    
    if (imageToSave.isNull()) {
        qDebug() << "No viewer image available for current index";
        return;
    }
    
    // Get Pictures folder path
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir picturesDir(picturesPath);
    if (!picturesDir.exists()) {
        picturesDir.mkpath(".");
    }
    
    // Create filename with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString filename = QFileInfo(imagePath).baseName() + "_" + timestamp + ".png";
    QString savePath = picturesDir.filePath(filename);
    
    // Save the image
    if (imageToSave.save(savePath, "PNG")) {
        qDebug() << "Image saved to" << savePath;
    } else {
        qDebug() << "Failed to save image to" << savePath;
    }
}

int FileListModel::selectedCount() const {
    int count = 0;
    for (const ImageFile *item : _items) {
        if (item->isSelected()) {
            count++;
        }
    }
    return count;
}

bool FileListModel::isIndexSelected(int index_) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return false;
    }
    return _items[index_]->isSelected();
}

void FileListModel::toggleSelection(int index_) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }
    setSelection(index_, !_items[index_]->isSelected());
}

void FileListModel::setSelection(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }

    const QString containerKey = selectionContainerForItem(_items[index_]);
    ensureSelectionStateLoaded(containerKey);
    const QSet<QString> previousSelection = _selectionStates[containerKey].selectedNames;
    if (!setSelectionInState(index_, selected)) {
        return;
    }

    pushSelectionHistory(containerKey, selected ? QString("Select %1").arg(_items[index_]->fileName())
                                               : QString("Deselect %1").arg(_items[index_]->fileName()),
                         previousSelection);
    emitSelectionDataChanged(index_, index_);
}

void FileListModel::invertSelection() {
    QHash<QString, QSet<QString>> previousSelections;
    QSet<QString> changedContainers;

    for (int i = 0; i < _items.size(); i++) {
        const QString containerKey = selectionContainerForItem(_items[i]);
        ensureSelectionStateLoaded(containerKey);
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, _selectionStates[containerKey].selectedNames);
        }

        if (setSelectionInState(i, !_items[i]->isSelected())) {
            changedContainers.insert(containerKey);
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, "Invert selection", previousSelections[containerKey]);
    }
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged();
    }
}

void FileListModel::setAllSelection(bool selected) {
    QHash<QString, QSet<QString>> previousSelections;
    QSet<QString> changedContainers;

    for (int i = 0; i < _items.size(); i++) {
        const QString containerKey = selectionContainerForItem(_items[i]);
        ensureSelectionStateLoaded(containerKey);
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, _selectionStates[containerKey].selectedNames);
        }

        if (setSelectionInState(i, selected)) {
            changedContainers.insert(containerKey);
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, selected ? "Select all" : "Deselect all", previousSelections[containerKey]);
    }
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged();
    }
}

void FileListModel::setSameKindSelection(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }

    ImageFile *currentItem = _items[index_];
    const QString currentContainer = selectionContainerForItem(currentItem);
    ensureSelectionStateLoaded(currentContainer);
    const QSet<QString> previousSelection = _selectionStates[currentContainer].selectedNames;
    const QString currentSuffix = QFileInfo(currentItem->fileName()).suffix().toLower();
    bool changed = false;

    for (int i = 0; i < _items.size(); i++) {
        ImageFile *item = _items[i];
        if (selectionContainerForItem(item) != currentContainer) {
            continue;
        }

        bool matches = false;
        if (currentContainer == "Computer" && currentItem->folderPath().isEmpty()) {
            matches = item->folderPath().isEmpty();
        }
        else if (currentItem->isFolder()) {
            matches = item->isFolder();
        }
        else if (!item->isFolder()) {
            matches = QFileInfo(item->fileName()).suffix().toLower() == currentSuffix;
        }

        if (matches && setSelectionInState(i, selected)) {
            changed = true;
        }
    }

    if (changed) {
        pushSelectionHistory(currentContainer, sameKindDescription(index_, selected), previousSelection);
        emitSelectionDataChanged();
    }
}

QVariantList FileListModel::dragIndexesForIndex(int index_) const {
    QVariantList result;
    if (index_ < 0 || index_ >= _items.size()) {
        return result;
    }

    if (_items[index_]->isSelected()) {
        for (int i = 0; i < _items.size(); i++) {
            if (_items[i]->isSelected()) {
                result.append(i);
            }
        }
    }
    else {
        result.append(index_);
    }
    return result;
}

QVariantList FileListModel::dragUrlsForIndex(int index_) const {
    QVariantList result;
    const QVariantList indexes = dragIndexesForIndex(index_);
    for (const QVariant &indexValue : indexes) {
        bool ok = false;
        const int itemIndex = indexValue.toInt(&ok);
        if (ok && itemIndex >= 0 && itemIndex < _items.size()) {
            const ImageFile *item = _items[itemIndex];
            QString fullPath = item->fullPath();
            if (_root == "Computer" && item->folderPath().isEmpty() &&
                    !fullPath.endsWith("/") && !fullPath.endsWith("\\")) {
                fullPath += "/";
            }
            result.append(QUrl::fromLocalFile(fullPath));
        }
    }
    return result;
}

QVariantMap FileListModel::dragPreviewItemsForIndex(int index_, int limit) const {
    QVariantMap result;
    QVariantList items;
    const QVariantList indexes = dragIndexesForIndex(index_);
    const int totalCount = indexes.size();
    const int cappedCount = limit < 0 ? totalCount : qMin(limit, totalCount);

    for (int i = 0; i < cappedCount; i++) {
        bool ok = false;
        const int itemIndex = indexes[i].toInt(&ok);
        if (!ok || itemIndex < 0 || itemIndex >= _items.size()) {
            continue;
        }

        const ImageFile *item = _items[itemIndex];
        QVariantMap previewItem;
        previewItem["index"] = itemIndex;
        previewItem["text"] = item->text();
        previewItem["imageIdUrl"] = item->imageIdUrl();
        previewItem["iconPath"] = item->iconPath();
        previewItem["isImage"] = item->isImage();
        previewItem["isFolder"] = item->isFolder();
        previewItem["fullPath"] = item->fullPath();
        items.append(previewItem);
    }

    result["items"] = items;
    result["totalCount"] = totalCount;
    result["remainingCount"] = qMax(0, totalCount - items.size());
    return result;
}

void FileListModel::beginSelectionPreview() {
    if (_selectionPreviewActive) {
        return;
    }

    _selectionPreviewActive = true;
    _selectionPreviewSnapshot.clear();
    for (ImageFile *item : _items) {
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        if (!_selectionPreviewSnapshot.contains(containerKey)) {
            _selectionPreviewSnapshot.insert(containerKey, _selectionStates[containerKey].selectedNames);
        }
    }
}

void FileListModel::previewSelectionRange(int anchorIndex, int targetIndex, bool selected, bool includeTarget) {
    if (anchorIndex < 0 || anchorIndex >= _items.size() || targetIndex < 0 || targetIndex >= _items.size()) {
        return;
    }
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedNames = it.value();
    }
    syncVisibleItemSelection();

    int first = qMin(anchorIndex, targetIndex);
    int last = qMax(anchorIndex, targetIndex);
    if (!includeTarget) {
        if (targetIndex > anchorIndex) {
            last = targetIndex - 1;
        }
        else if (targetIndex < anchorIndex) {
            first = targetIndex + 1;
        }
        else {
            emitSelectionDataChanged();
            return;
        }
    }

    QList<int> indexes;
    for (int i = first; i <= last; i++) {
        indexes.append(i);
    }
    mutateSelectionForIndexes(indexes, selected);
    emitSelectionDataChanged();
}

void FileListModel::previewSelectionIndexes(const QVariantList &indexes, int mode) {
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedNames = it.value();
    }
    syncVisibleItemSelection();

    QList<int> intIndexes;
    intIndexes.reserve(indexes.size());
    for (const QVariant &indexValue : indexes) {
        bool ok = false;
        const int index_ = indexValue.toInt(&ok);
        if (ok) {
            intIndexes.append(index_);
        }
    }

    if (mode == SelectionPreviewReplace) {
        for (int i = 0; i < _items.size(); i++) {
            setSelectionInState(i, false);
        }
        mutateSelectionForIndexes(intIndexes, true);
    }
    else if (mode == SelectionPreviewToggle) {
        for (int index_ : intIndexes) {
            if (index_ < 0 || index_ >= _items.size()) {
                continue;
            }

            const ImageFile *item = _items[index_];
            const QString containerKey = selectionContainerForItem(item);
            const QString itemKey = selectionItemKey(item);
            const bool wasSelected = _selectionPreviewSnapshot.value(containerKey).contains(itemKey);
            setSelectionInState(index_, !wasSelected);
        }
    }
    else {
        mutateSelectionForIndexes(intIndexes, mode != SelectionPreviewDeselect);
    }
    emitSelectionDataChanged();
}

void FileListModel::commitSelectionPreview(const QString &description) {
    if (!_selectionPreviewActive) {
        return;
    }

    QSet<QString> changedContainers;
    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        if (_selectionStates[it.key()].selectedNames != it.value()) {
            changedContainers.insert(it.key());
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, description, _selectionPreviewSnapshot[containerKey]);
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged();
    }
}

void FileListModel::cancelSelectionPreview() {
    if (!_selectionPreviewActive) {
        return;
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedNames = it.value();
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    syncVisibleItemSelection();
    emitSelectionDataChanged();
}

QVariantList FileListModel::selectionHistoryForIndex(int index_) const {
    QVariantList result;
    const QString containerKey = selectionContainerForIndex(index_);
    const auto stateIt = _selectionStates.constFind(containerKey);
    if (stateIt == _selectionStates.constEnd()) {
        return result;
    }

    const auto &history = stateIt->history;
    for (int i = 0; i < history.size(); i++) {
        QVariantMap row;
        row["index"] = i;
        row["description"] = history[i].description;
        row["timestamp"] = history[i].timestamp.toString("yyyy-MM-dd hh:mm:ss");
        row["selectedCount"] = history[i].selectedNames.size();
        row["current"] = i == stateIt->historyIndex;
        result.append(row);
    }
    return result;
}

int FileListModel::selectionHistoryIndexForIndex(int index_) const {
    const QString containerKey = selectionContainerForIndex(index_);
    const auto stateIt = _selectionStates.constFind(containerKey);
    return stateIt == _selectionStates.constEnd() ? -1 : stateIt->historyIndex;
}

QString FileListModel::selectionContainerForIndex(int index_) const {
    if (index_ >= 0 && index_ < _items.size()) {
        return selectionContainerForItem(_items[index_]);
    }
    return PersistentSelectionCache::normalizeContainerKey(_root);
}

void FileListModel::selectionHistoryBack(int index_) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    const int historyIndex = _selectionStates[containerKey].historyIndex;
    if (historyIndex > 0) {
        applySelectionHistoryState(containerKey, historyIndex - 1);
    }
}

void FileListModel::selectionHistoryForward(int index_) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    const int historyIndex = _selectionStates[containerKey].historyIndex;
    if (historyIndex >= 0 && historyIndex < _selectionStates[containerKey].history.size() - 1) {
        applySelectionHistoryState(containerKey, historyIndex + 1);
    }
}

void FileListModel::jumpSelectionHistory(int index_, int historyIndex) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    applySelectionHistoryState(containerKey, historyIndex);
}

QString FileListModel::selectionContainerForItem(const ImageFile *item) const {
    if (!item) {
        return PersistentSelectionCache::normalizeContainerKey(_root);
    }
    if (_root == "Computer" && item->folderPath().isEmpty()) {
        return "Computer";
    }
    return PersistentSelectionCache::normalizeContainerKey(item->folderPath());
}

QString FileListModel::selectionItemKey(const ImageFile *item) const {
    return item ? item->fileName() : QString();
}

void FileListModel::ensureSelectionStateLoaded(const QString &containerKey) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    if (!_selectionStates.contains(normalizedKey)) {
        _selectionStates.insert(normalizedKey, PersistentSelectionCache::retrieveContainer(normalizedKey));
    }
}

void FileListModel::loadSelectionStatesForVisibleItems() {
    for (ImageFile *item : _items) {
        ensureSelectionStateLoaded(selectionContainerForItem(item));
    }
    syncVisibleItemSelection();
}

void FileListModel::syncVisibleItemSelection() {
    for (ImageFile *item : _items) {
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        item->setIsSelected(_selectionStates[containerKey].selectedNames.contains(selectionItemKey(item)));
    }
}

void FileListModel::emitSelectionDataChanged(int firstIndex, int lastIndex) {
    if (!_items.isEmpty()) {
        if (firstIndex < 0 || lastIndex < 0) {
            firstIndex = 0;
            lastIndex = _items.size() - 1;
        }
        firstIndex = qBound(0, firstIndex, _items.size() - 1);
        lastIndex = qBound(0, lastIndex, _items.size() - 1);
        if (firstIndex > lastIndex) {
            std::swap(firstIndex, lastIndex);
        }
        emit dataChanged(index(firstIndex, 0), index(lastIndex, 0), {SelectedRole});
    }
    emit selectionChanged();
}

void FileListModel::pushSelectionHistory(const QString &containerKey, const QString &description,
                                         const QSet<QString> &previousSelectedNames) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    ensureSelectionStateLoaded(normalizedKey);
    auto &state = _selectionStates[normalizedKey];
    if (state.historyIndex < state.history.size() - 1) {
        state.history.resize(state.historyIndex + 1);
    }
    if (state.history.isEmpty()) {
        state.history.append(PersistentSelectionCache::HistoryEntry{
            .description = "Initial state",
            .timestamp = QDateTime::currentDateTime(),
            .selectedNames = previousSelectedNames
        });
    }
    state.history.append(PersistentSelectionCache::HistoryEntry{
        .description = description,
        .timestamp = QDateTime::currentDateTime(),
        .selectedNames = state.selectedNames
    });
    state.historyIndex = state.history.size() - 1;
    PersistentSelectionCache::storeContainer(normalizedKey, state);
    emit selectionHistoryChanged();
}

void FileListModel::mutateSelectionForIndexes(const QList<int> &indexes, bool selected) {
    for (int index_ : indexes) {
        setSelectionInState(index_, selected);
    }
}

bool FileListModel::setSelectionInState(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return false;
    }

    ImageFile *item = _items[index_];
    const QString containerKey = selectionContainerForItem(item);
    ensureSelectionStateLoaded(containerKey);
    auto &selectedNames = _selectionStates[containerKey].selectedNames;
    const QString itemKey = selectionItemKey(item);
    const bool wasSelected = selectedNames.contains(itemKey);
    if (wasSelected == selected) {
        return false;
    }

    if (selected) {
        selectedNames.insert(itemKey);
    }
    else {
        selectedNames.remove(itemKey);
    }
    item->setIsSelected(selected);
    return true;
}

void FileListModel::applySelectionHistoryState(const QString &containerKey, int historyIndex) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    ensureSelectionStateLoaded(normalizedKey);
    auto &state = _selectionStates[normalizedKey];
    if (historyIndex < 0 || historyIndex >= state.history.size()) {
        return;
    }

    state.historyIndex = historyIndex;
    state.selectedNames = state.history[historyIndex].selectedNames;
    PersistentSelectionCache::storeContainer(normalizedKey, state);
    syncVisibleItemSelection();
    emitSelectionDataChanged();
    emit selectionHistoryChanged();
}

QString FileListModel::sameKindDescription(int index_, bool selected) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return selected ? "Select matching items" : "Deselect matching items";
    }

    ImageFile *item = _items[index_];
    QString target;
    if (selectionContainerForItem(item) == "Computer" && item->folderPath().isEmpty()) {
        target = "drives";
    }
    else if (item->isFolder()) {
        target = "folders";
    }
    else {
        const QString suffix = QFileInfo(item->fileName()).suffix();
        target = suffix.isEmpty() ? "extensionless files" : QString(".%1 files").arg(suffix);
    }
    return QString("%1 %2").arg(selected ? "Select" : "Deselect", target);
}
