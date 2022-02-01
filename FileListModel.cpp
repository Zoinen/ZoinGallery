#include "FileListModel.h"
#include "BatchThumbnailGenerator.h"

#include <QDir>

FileListModel::FileListModel(QObject *parent)
    : QStandardItemModel(parent) {
    QHash<int,QByteArray> names;
    names[Qt::DisplayRole] = "displayRole";
    names[Qt::DecorationRole] = "decorationRole";
    names[ImageRole] = "imageRole";
    names[ImageIdRole] = "imageIdRole";
    setItemRoleNames(names);

    _lastId = 0;

    _generator = new BatchThumbnailGenerator(this);

    connect(_generator, &BatchThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        QStandardItem *item = _fileToItem[path];
        item->setData(thumbnail, ImageRole);
        updateImageId(item);
    });
}

void FileListModel::cd(QString path) {
    _root = path;
    _fileToItem.clear();
    _imageIdToItem.clear();
    clear();

    QDir dir(_root);
    QStringList files = dir.entryList();
    QStringList paths;
    for (const auto &file : files) {
        QStandardItem *item = new QStandardItem();
        updateImageId(item);
        item->setText(file);
        invisibleRootItem()->appendRow(item);

        QString path = fullPath(file);
        _fileToItem.insert(path, item);
        paths.append(path);
    }
    _generator->generate(paths);
}

QString FileListModel::rootPath() const {
    return _root;
}

QString FileListModel::fullPath(QString fileName) {
    return _root + "/" + fileName;
}

QStandardItem *FileListModel::itemForImageId(QString imageId) {
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

void FileListModel::updateImageId(QStandardItem *item) {
    QVariant imageId = item->data(ImageIdRole);
    if (imageId.isValid()) {
        _imageIdToItem.remove(imageId.toString());
    }
    QString newImageId = generateNewId();
    _imageIdToItem.insert(newImageId, item);
    item->setData(newImageId, ImageIdRole);
}
