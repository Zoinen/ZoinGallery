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

    connect(_generator, &ThreadedThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            MyItem *item = it.value();
            item->image = thumbnail;
            updateImageId(item);
            QModelIndex modelIndex = index(item->index, 0);
            emit dataChanged(modelIndex, modelIndex, QVector<int>({ImageIdRole}));
        }
    });

    connect(_generator, &ThreadedThumbnailGenerator::generationFinished, this, [&] () {
        _generationFinished = true;
        emit generationFinishedChanged();
    });
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int,QByteArray> names;
    names[Qt::DisplayRole] = "displayRole";
    names[Qt::DecorationRole] = "decorationRole";
    names[ImageRole] = "imageRole";
    names[ImageIdRole] = "imageIdRole";
    names[FolderRole] = "folderRole";
    names[ImageResolutionRole] = "imageResolutionRole";

    return names;
}

int FileListModel::rowCount(const QModelIndex &parent) const {
    return _items.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    if (index.row() < _items.size()) {
        if (role == Qt::DisplayRole) {
            return _items[index.row()]->text;
        }
        else if (role == ImageIdRole) {
            return _items[index.row()]->imageId;
        }
        else if (role == FolderRole) {
            return _items[index.row()]->isFolder;
        }
    }
    return QVariant();
}

void FileListModel::prepareToClose() {
    _generator->prepareToClose();
}

void FileListModel::setThumbnailResolution(QSize dimensions, qreal dpr) {
    qDebug() << "Thumbs resolution" << dimensions << dpr;
    _generator->setThumbnailResolution(dimensions, dpr);
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
            MyItem *item = new MyItem();
            item->text = QDir::toNativeSeparators(drive.path());
            item->isFolder = true;
            item->index = _items.size();
            _items.append(item);

            if (item->text == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }
    }
    else {
        QDir dir(_root);
        QStringList folders = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System);
        for (const auto &folder : folders) {
            MyItem *item = new MyItem();
            item->text = folder;
            item->isFolder = true;
            item->index = _items.size();
            _items.append(item);

            if (item->text == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            MyItem *item = new MyItem();
            item->text = file;
            item->isFolder = false;

            if (isImage(file)) {
                updateImageId(item);
                QString path = fullPath(file);
                _fileToItem.insert(path, item);
                _imagePaths.append(path);
            }

            item->index = _items.size();
            _items.append(item);
        }
        _generator->generate(_imagePaths);
        _generationFinished = false;
        emit generationFinishedChanged();
    }
    endResetModel();
    return indexToSelect;
}

void FileListModel::updateThumbnails() {
    _generator->generate(_imagePaths);
    _generationFinished = false;
    emit generationFinishedChanged();
}

QString FileListModel::rootPath() const {
    return _root;
}

QString FileListModel::fullPath(QString fileName) {
    return _root + "/" + fileName;
}

const FileListModel::MyItem *FileListModel::itemForImageId(QString imageId) {
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
             _generator->setNextRequestImage(fullPath(_items[i]->text), isForward);
             _lastRequestIndex = index_;
            break;
        }
    }
}

QString FileListModel::generateNewId() {
    QString id = QString::number(_lastId);
    _lastId++;
    return id;
}

void FileListModel::updateImageId(MyItem *item) {
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
