#include "FileListModel.h"
#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QDebug>

FileListModel::FileListModel(QObject *parent)
    : QAbstractListModel(parent) {
    _lastId = 0;
    _lastRequestIndex = -1;

    _generator = new ThreadedThumbnailGenerator(this);

    connect(_generator, &ThreadedThumbnailGenerator::thumbnailInfoReady, this, [&] (const QString &path, QSize fullSize) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
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
            return _items[index.row()]->path;
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
    _fileToItem.clear();
    _imageIdToItem.clear();
    _imagePaths.clear();
    _lastRequestIndex = -1;
    for (int i = 0; i < _items.size(); i++) {
        delete _items[i];
    }
    _items.clear();

    if (path == "Computer") {
        for (const auto &drive : QDir::drives()) {
            ImageFile *item = new ImageFile();
            item->path = QDir::toNativeSeparators(drive.path());
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            _items.append(item);

            if (item->path == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }
    }
    else {
        QDir dir(_root);
        QStringList folders = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System);
        for (const auto &folder : folders) {
            ImageFile *item = new ImageFile();
            item->path = folder;
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            item->iconPath = "qrc:/resources/FolderIcon.svg";
            _items.append(item);

            if (item->path == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            ImageFile *item = new ImageFile();
            item->path = file;
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
    qDebug() << "Request thumbnails" << preferredSize;

    QList<ThumbnailReadRequest> requests;
    requests.reserve(_imagePaths.size());
    for (const QString &path : _imagePaths) {
        requests.append(ThumbnailReadRequest(path, preferredSize));
    }
    _generator->setThumbnailReadQueue(requests);
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

void FileListModel::setNextRequestIndex(int index_) {
    for (int i = qMax(0, index_); i < _items.size(); i++) {
        if (!_items[i]->imageId.isNull()) {
            bool isForward = true;
            if (_lastRequestIndex != -1 && _lastRequestIndex > index_) {
                isForward = false;
            }
             _generator->setNextRequestImage(fullPath(_items[i]->path), isForward);
             _lastRequestIndex = index_;
            break;
        }
    }
}

void FileListModel::addRequestThumbnails(QList<ThumbnailReadRequest> requests) {
//    qDebug() << "Add to queue" << requests.size();
    for (int i = 0; i < requests.size(); i++) {
        requests[i].sourcePath = fullPath(requests[i].sourcePath);
    }
    _generator->requestDecode(requests);
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
