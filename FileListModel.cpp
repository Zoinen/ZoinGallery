#include "FileListModel.h"
#include "BatchThumbnailGenerator.h"
#include "ThreadedThumbnailGenerator.h"
#include "ThreadPoolThumbnailGenerator.h"

#include <QDir>

FileListModel::FileListModel(QObject *parent)
    : QAbstractListModel(parent) {
    _lastId = 0;

    _generator = new BatchThumbnailGenerator(this);

    connect(_generator, &BatchThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        MyItem *item = _fileToItem[path];
        item->image = thumbnail;
        updateImageId(item);
    });

    _generator2 = new ThreadedThumbnailGenerator(this);

    connect(_generator2, &ThreadedThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        MyItem *item = _fileToItem[path];
        item->image = thumbnail;
        updateImageId(item);
    });

    _generator3 = new ThreadPoolThumbnailGenerator(this);

    connect(_generator3, &ThreadPoolThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        MyItem *item = _fileToItem[path];
        item->image = thumbnail;
        updateImageId(item);
    });
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int,QByteArray> names;
    names[Qt::DisplayRole] = "displayRole";
    names[Qt::DecorationRole] = "decorationRole";
    names[ImageRole] = "imageRole";
    names[ImageIdRole] = "imageIdRole";

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
    }
    return QVariant();
}

void FileListModel::cd(QString path) {
    _root = path;

    QDir dir(_root);
    QStringList files = dir.entryList();

    beginInsertRows(QModelIndex(), 0, files.size() - 1);
    _fileToItem.clear();
    _imageIdToItem.clear();
    // memory leak here
    _items.clear();

    QStringList paths;
    for (const auto &file : qAsConst(files)) {
        MyItem *item = new MyItem;
        _items.append(item);
        updateImageId(item);
        item->text = file;

        QString path = fullPath(file);
        _fileToItem.insert(path, item);
        paths.append(path);
    }
    _generator2->generate(paths);
    endInsertRows();
}

QString FileListModel::rootPath() const {
    return _root;
}

QString FileListModel::fullPath(QString fileName) {
    return _root + "/" + fileName;
}

const FileListModel::MyItem *FileListModel::itemForImageId(QString imageId) {
    qDebug() << "image id requested" << imageId;
    auto it = _imageIdToItem.find(imageId);
    if (it != _imageIdToItem.end()) {
        qDebug() << "serving" << *it;
        return *it;
    }
    return nullptr;
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
    qDebug() << "image id saved" << item->imageId << item;
}
