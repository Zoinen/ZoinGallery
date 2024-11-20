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
        auto it = _fileToItem.find(result.path);
        // qDebug() << "INFO RECEIVED" << result.path << result.imageSize;
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            item->setFullSize(rotateToOrientation(result.imageSize, result.orientation));
            item->setInfo(result);
            if (!result.imageSize.isValid()) {
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
        QList<ImageDecodeRequest> requests;

        int flushIndex = -1;
        int minIndex = 1000000;
        int maxIndex = -1;
        // TODO: If differents parent come in one signal here it will mess everything up
        QModelIndex parent;

        for (const ImageInfo &result : results) {
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
                return;
            }
            if (request.viewerRequest) {
                // qDebug() << "ZZ Viewer image SET";
                QString imageId = generateNewId();
                if (request.targetSize == request.info.imageSize) {
                    auto it = _fullSizeViewerImages.find(request.info.path);
                    if (it != _fullSizeViewerImages.end()) {
                        if (it->image.width() > image.width() && it->image.height() > image.height()) {
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
    _decodeManager->prepareToClose();
    qApp->exit(0);
}

int FileListModel::cd(const QString &path, const QString &itemToSelect) {
    _root = path;
    int indexToSelect = 0;

    beginResetModel();
    cleanupModelBeforeCd();

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
        QStringList folders = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System);
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

        auto files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            ImageFile *item = createFileItem(_root, file.fileName(), file.lastModified());
            if (item->isImage()) {
                _imagePaths.append(item->fullPath());
            }
            item->setIndex(_items.size());
            _items.append(item);
        }
    }
    endResetModel();

    _decodeManager->readImagesInfo(_imagePaths, false);
    _decodeManager->readFolderList(_folderImagePaths, 16); // TODO: FIX!

    return indexToSelect;
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

    //Viewer
    _viewerImages.clear();
    _fullSizeViewerImages.clear();
    _imageIdToViewer.clear();
    _imageIdToFullSizeViewer.clear();
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
        QStringList subDirs = QDir(dirInfo.path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
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
    endResetModel();

    _decodeManager->readImagesInfo(_imagePaths, true);
    _decodeManager->readFolderList(_folderImagePaths, 16);
}

bool FileListModel::isImage(const QString &fileName) {
    return ThumbnailLoader::isFormatSupported(fileName);
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
        emit viewerImageIdUrlChanged(QString("image://thumbnails/") + itThumbs.value()->imageIdUrl(), 0);
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
