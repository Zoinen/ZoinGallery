#include "StandardFileListModel.h"
#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QDebug>

StandardFileListModel::StandardFileListModel(QObject *parent)
    : QStandardItemModel(parent) {
    QHash<int,QByteArray> names;
    names[Qt::DisplayRole] = "displayRole";
    names[Qt::DecorationRole] = "decorationRole";
    names[ImageRole] = "imageRole";
    names[ImageIdRole] = "imageIdRole";
    names[FolderRole] = "folderRole";
    names[ImageResolutionRole] = "imageResolutionRole";
    setItemRoleNames(names);

    _lastId = 0;
    _lastRequestIndex = -1;

    _generator = new ThreadedThumbnailGenerator(this);

    connect(_generator, &ThreadedThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail, QSize fullSize) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            QStandardItem *item = it.value();
            item->setData(thumbnail, ImageRole);
            item->setData(thumbnail.size(), ImageResolutionRole);
            updateImageId(item);
        }
    });

    connect(_generator, &ThreadedThumbnailGenerator::generationFinished, this, [&] () {
        _generationFinished = true;
        emit generationFinishedChanged();
    });
}

void StandardFileListModel::prepareToClose() {
    _generator->prepareToClose();
}

void StandardFileListModel::setThumbnailResolution(QSize dimensions, qreal dpr) {
    qDebug() << "Thumbs resolution" << dimensions << dpr;
    _generator->setThumbnailResolution(dimensions, dpr);
}

void StandardFileListModel::cd(QString path) {
    _root = path;
    _fileToItem.clear();
    _imageIdToItem.clear();
    _imagePaths.clear();
    _lastRequestIndex = -1;
    clear();

    if (path == "Computer") {
        for (const auto &drive : QDir::drives()) {
            QStandardItem *item = new QStandardItem();
            item->setText(QDir::toNativeSeparators(drive.path()));
            item->setData(true, FolderRole);
            invisibleRootItem()->appendRow(item);
        }
    }
    else {
        QDir dir(_root);
        QStringList folders = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System);
        for (const auto &folder : folders) {
            QStandardItem *item = new QStandardItem();
            item->setText(folder);
            item->setData(true, FolderRole);
            invisibleRootItem()->appendRow(item);
        }

        QStringList files = dir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            QStandardItem *item = new QStandardItem();
            item->setText(file);

            if (isImage(file)) {
                updateImageId(item);
                QString path = fullPath(file);
                _fileToItem.insert(path, item);
                _imagePaths.append(path);
            }

            invisibleRootItem()->appendRow(item);
        }
        _generator->generate(_imagePaths);
        _generationFinished = false;
        emit generationFinishedChanged();
    }
}

void StandardFileListModel::updateThumbnails() {
    _generator->generate(_imagePaths);
    _generationFinished = false;
    emit generationFinishedChanged();
}

QString StandardFileListModel::rootPath() const {
    return _root;
}

QString StandardFileListModel::fullPath(QString fileName) {
    return _root + "/" + fileName;
}

QStandardItem *StandardFileListModel::itemForImageId(QString imageId) {
    auto it = _imageIdToItem.find(imageId);
    if (it != _imageIdToItem.end()) {
        return *it;
    }
    return nullptr;
}

void StandardFileListModel::setNextRequestIndex(int index_) {
    for (int i = index_; i < rowCount(); i++) {
        if (index(i, 0).data(ImageIdRole).isValid()) {
            bool isForward = true;
            if (_lastRequestIndex != -1 && _lastRequestIndex > index_) {
                isForward = false;
            }
             _generator->setNextRequestImage(fullPath(index(i, 0).data().toString()), isForward);
             _lastRequestIndex = index_;
            break;
        }
    }
}

QString StandardFileListModel::generateNewId() {
    QString id = QString::number(_lastId);
    _lastId++;
    return id;
}

void StandardFileListModel::updateImageId(QStandardItem *item) {
    QVariant imageId = item->data(ImageIdRole);
    if (imageId.isValid()) {
        _imageIdToItem.remove(imageId.toString());
    }
    QString newImageId = generateNewId();
    _imageIdToItem.insert(newImageId, item);
    item->setData(newImageId, ImageIdRole);
//    qDebug() << "--- update image id for" << item->text();
}

bool StandardFileListModel::isImage(QString fileName) {
    return ThumbnailLoader::isJpeg(fileName) || ThumbnailLoader::isRawOrTiff(fileName) || ThumbnailLoader::isImageOther(fileName);
}
