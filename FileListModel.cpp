#include "FileListModel.h"
#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QDebug>
#include <QSet>

FileListModel::FileListModel(QObject *parent)
    : QAbstractListModel(parent) {
    _lastId = 0;
    _currentViewIndex = -1;

    _generator = new ThreadedThumbnailGenerator(this);

    connect(_generator, &ThreadedThumbnailGenerator::thumbnailInfoReady, this, [&] (const QString &path, QSize fullSize) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
//            qDebug() << "found image" << path;
            ImageFile *item = it.value();
            item->fullSize = fullSize;

            QModelIndex modelIndex = index(item->index, 0);
            emit dataChanged(modelIndex, modelIndex, {ImageFullSizeRole});
        }
    });

    connect(_generator, &ThreadedThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            item->image = thumbnail;
            updateImageId(item);

            QModelIndex modelIndex = index(item->index, 0);
            emit dataChanged(modelIndex, modelIndex, {ImageIdRole});
        }
    });

    connect(_generator, &ThreadedThumbnailGenerator::viewerReady, this, [&] (const QString &path, const QImage &image) {
        QString imageId = generateNewId();
        _viewerImages[path] = {image, imageId, (int)_viewerImages.size()};
        _imageIdToViewer[imageId] = path;
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (item->index == _currentViewIndex) {
                emit viewerImageIdChanged(QString("image://thumbnails/") + imageId);
            }
        }
    });

    connect(_generator, &ThreadedThumbnailGenerator::decodeFinished, this, [&] () {
        _generationFinished = true;
        emit generationFinishedChanged();
    });

    connect(_generator, &ThreadedThumbnailGenerator::readFinished,
            this, &FileListModel::thumbnailReadFinished);
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
    return _items.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    if (index.row() < _items.size()) {
        if (role == Qt::DisplayRole) {
            return _items[index.row()]->fileName;
        }
        else if (role == ImageIdRole) {
            return _items[index.row()]->imageId;
        }
        else if (role == FolderRole) {
            return _items[index.row()]->isFolder;
        }
        else if (role == ImageRole) {
            return _items[index.row()]->image;
        }
        else if (role == ImageFullSizeRole) {
            return _items[index.row()]->fullSize;
        }
        else if (role == ImageFileRole) {
            return QVariant::fromValue(_items[index.row()]);
        }
    }
    return QVariant();
}

void FileListModel::prepareToClose() {
    _generator->prepareToClose();
}

int FileListModel::cd(QString path, QString itemToSelect) {
    _root = path;
    int indexToSelect = 0;

    beginResetModel();
    //Viewer
    _viewerImages.clear();
    _imageIdToViewer.clear();
    _currentViewIndex = -1;

    _fileToItem.clear();
    _imageIdToItem.clear();
    _imagePaths.clear();
    for (int i = 0; i < _items.size(); i++) {
        delete _items[i];
    }
    _items.clear();

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
            item->fileName = folder;
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            item->iconPath = "qrc:/resources/FolderIcon.svg";
            _items.append(item);

            if (item->fileName == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            ImageFile *item = new ImageFile();
            item->fileName = file;
            item->isFolder = false;

            if (isImage(file)) {
                item->isImage = true;
//                updateImageId(item);
                QString path = fullPath(file);
                _fileToItem.insert(path, item);
                _imagePaths.append(path);
            }
            else {
                item->isImage = false;
                item->iconPath = "qrc:/resources/FileIcon.svg";
            }

            item->index = _items.size();
            _items.append(item);
        }
    }
    endResetModel();
    return indexToSelect;
}

void FileListModel::requestThumbnails(QSize preferredSize) {
//    qDebug() << "Request thumbnails" << preferredSize;

    QList<ImageReadRequest> requests;
    requests.reserve(_imagePaths.size());
    for (const QString &path : _imagePaths) {
        requests.append(ImageReadRequest(path, preferredSize));
    }
    _generator->clearRequests();
    _generator->requestRead(requests);
    _generationFinished = false;
    emit generationFinishedChanged();
}

QString FileListModel::rootPath() const {
    return _root;
}

QString FileListModel::fullPath(QString fileName) {
    return _root + "/" + fileName;
}

const ImageFile *FileListModel::itemForImageId(QString imageId) {
    auto it = _imageIdToItem.find(imageId);
    if (it != _imageIdToItem.end()) {
        return *it;
    }
    return nullptr;
}

void FileListModel::addRequestThumbnails(QList<ImageReadRequest> requests) {
//    qDebug() << "Add to queue" << requests.size();
    _generator->requestThumbnailDecode(requests);
    _generationFinished = false;
    emit generationFinishedChanged();
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
//    qDebug() << "image id saved" << item->imageId << item;
}

bool FileListModel::isImage(QString fileName) {
    return ThumbnailLoader::isJpeg(fileName) || ThumbnailLoader::isRawOrTiff(fileName) || ThumbnailLoader::isImageOther(fileName);
}

void FileListModel::requestViewer(int index, int width, int height) {
    QString requestedPath = fullPath(_items[index]->fileName);
    auto it = _viewerImages.find(requestedPath);
    if (it != _viewerImages.end()) {
        emit viewerImageIdChanged(QString("image://thumbnails/") + it.value().imageId);
    }

    QSize viewerSize(width, height);
//    qDebug() << "Request thumbnails" << index << viewerSize;
    _currentViewIndex = index;

    int queueSize = 16;

    QList<ImageReadRequest> requests;
    for (int i = index; i < _items.size(); i++) {
        if (requests.size() >= queueSize) {
            break;
        }
        if (_items[i]->isImage) {
            requests.append(ImageReadRequest(fullPath(_items[i]->fileName), viewerSize, true));
        }
    }
    int backwardInsertIndex = 1;
    for (int i = index; i >= 0; i--) {
        if (requests.size() >= queueSize * 1.5) {
            break;
        }
        if (_items[i]->isImage) {
            if (backwardInsertIndex >= _items.count()) {
                backwardInsertIndex = _items.count();
            }
            requests.insert(backwardInsertIndex, ImageReadRequest(fullPath(_items[i]->fileName), viewerSize, true));
            backwardInsertIndex += 2;
        }
    }

    _generator->requestRead(requests);
}

QImage FileListModel::viewerForImageId(QString imageId) {
    auto it = _imageIdToViewer.find(imageId);
    if (it != _imageIdToViewer.end()) {
        QString path = *it;
        return _viewerImages[path].image;
    }
    return QImage();
}

void FileListModel::invalidateViewerImages() {
    _generator->clearRequests();
}
