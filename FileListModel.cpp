#include "FileListModel.h"
#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"
#include "ThumbnailCache.h"

#include <QDir>
#include <QDebug>
#include <QSet>
#include <QFileInfo>
#include <QSettings>
#include <QDeadlineTimer>
#include <QGuiApplication>

#include <chrono>
using namespace std::chrono_literals;

FileListModel::FileListModel(QObject *parent)
    : QAbstractItemModel(parent) {
    _lastId = 0;
    _currentViewIndex = -1;

    _generator = new ThreadedThumbnailGenerator(this);

    _thumbnailCache = new ThumbnailCache();
    QThread *thread = new QThread(this);
    _thumbnailCache->moveToThread(thread);

    // TODO: reuse
    connect(_thumbnailCache, &ThumbnailCache::cachedThumbnailAvailable, this, [&] (const QString &path, const QImage &thumbnail) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (item->image.isNull()) {
                item->image = thumbnail;
                updateImageId(item);

                QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
                emit dataChanged(modelIndex, modelIndex, {ImageIdRole});
            }
        }
    });
    connect(this, &FileListModel::addToCache,
            _thumbnailCache, &ThumbnailCache::add);
    connect(this, &FileListModel::requestThumbnailFromCache,
            _thumbnailCache, &ThumbnailCache::requestThumbnail);
    thread->start();
    connect(_generator, &ThreadedThumbnailGenerator::thumbnailInfoReady, this, [&] (const QString &path, QSize fullSize) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
//            qDebug() << "found image" << path;
            ImageFile *item = it.value();
            item->fullSize = fullSize;

            QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
            emit dataChanged(modelIndex, modelIndex, {ImageFullSizeRole});

            QSettings set;
            set.beginGroup("imageCache");
            set.setValue(item->fullPath() + "/FullSize", item->fullSize);
            set.endGroup();

            if (item->parent && item->parent->subfiles.last() == item) {
                emit thumbnailReadFinished(item->parent);
            }
        }
    });

    connect(_generator, &ThreadedThumbnailGenerator::thumbnailReady, this, [&] (const QString &path, const QImage &thumbnail) {
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            item->image = thumbnail;
            updateImageId(item);

            QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
            emit dataChanged(modelIndex, modelIndex, {ImageIdRole});

            emit addToCache(path, item->lastModified, thumbnail);
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

    connect(_generator, &ThreadedThumbnailGenerator::folderListReady, this, [&] (const QString &path, const QList<QFileInfo> &images) {

        QSettings set;
        set.beginGroup("imageCache");
        set.setValue(path + "/FolderView", images.size() != 0);
        QStringList folders;
        for (int i = 0; i < images.size(); i++) {
            folders.append(images.at(i).fileName());
        }
        set.setValue(path + "/SubImages", folders);
        set.endGroup();

        if (!images.size()) {
            return;
        }

        _folderImagePaths.removeOne(path);
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();

            bool contentChanged = item->subfiles.size() != images.size();
            bool dateChanged = false;
            if (!contentChanged) {
                for (int i = 0; i < images.size(); i++) {
                    if (images[i].fileName() != item->subfiles[i]->fileName) {
                        contentChanged = true;
                    }
                    if (images[i].lastModified() != item->subfiles[i]->lastModified) {
                        dateChanged = true;
                    }
                }
            }
            else {
                contentChanged = true;
            }
            // TODO: Cache date too
            if (contentChanged) {
                QList<ImageFile *> subImages;
                subImages.reserve(images.size());
                for (int i = 0; i < images.size(); i++) {
                    ImageFile *subItem = createFileItem(path, images.at(i).fileName(), images.at(i).lastModified());
                    subItem->parent = item;
                    subItem->index = subImages.size();
                    subImages.append(subItem);
                }

                beginInsertRows(indexFromItem(item), 0, subImages.size() - 1);
                item->subfiles = subImages;
                endInsertRows();
                if (_folderModels.contains(item->index)) {
                    _folderModels[item->index]->resetModel();
                }
                //            updateImageId(item);

                QModelIndex modelIndex = index(item->index, 0, indexFromItem(item->parent));
                emit dataChanged(modelIndex, modelIndex, {FolderViewRole});
            }
            else if (dateChanged) {
                RootProxyModel *folderModel_ = static_cast<RootProxyModel *>(folderModel(item->index));
                folderModel_->requestRender();
                folderModel_->resetModel();
            }
        }

    });

    connect(_generator, &ThreadedThumbnailGenerator::decodeFinished, this, [&] () {
        _generationFinished = true;
        emit generationFinishedChanged();
    });

    connect(_generator, &ThreadedThumbnailGenerator::readFinished, this, [&] () {
        emit thumbnailReadFinished(nullptr);
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
    _thumbnailCache->thread()->quit();
    _generator->prepareToClose();

    _thumbnailCache->thread()->wait(QDeadlineTimer(2000ms));
    qApp->exit(0);
}

int FileListModel::cd(QString path, QString itemToSelect) {
    _root = path;
    int indexToSelect = 0;

    beginResetModel();
    _generator->clearRequests();

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
    _imagePaths.clear();
    _folderImagePaths.clear();
    for (int i = 0; i < _items.size(); i++) {
        for (int j = 0; j < _items[i]->subfiles.size(); j++) {
            delete _items[i]->subfiles[j];
        }
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
            item->folderPath = _root;
            item->fileName = folder;
            item->isFolder = true;
            item->isImage = false;
            item->index = _items.size();
            item->iconPath = "qrc:/resources/FolderIcon.svg";
            _items.append(item);

            QString path = item->fullPath();

            QSettings set;
            set.beginGroup("imageCache");
            QVariant folderView = set.value(path + "/FolderView");
            if (folderView.isValid()) {
                item->isFolderView = folderView.toBool();
                QVariant subItems = set.value(path + "/SubImages");
                if (subItems.isValid()) {
                    QStringList images = subItems.toStringList();

                    QList<ImageFile *> subImages;
                    subImages.reserve(images.size());
                    for (int i = 0; i < images.size(); i++) {
                        ImageFile *subItem = createFileItem(path, images.at(i));
                        subItem->parent = item;
                        subItem->index = subImages.size();
                        subImages.append(subItem);
                    }
                    item->subfiles = subImages;
                }
            }
            set.endGroup();

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
    return indexToSelect;
}

void FileListModel::requestThumbnails(QStringList files, QSize preferredSize) {

    QList<ImageReadRequest> requests;
    requests.reserve(files.size());
    for (const QString &path : files) {
        requests.append(ImageReadRequest(path, preferredSize));
    }
    _generator->requestRead(requests);
    _generationFinished = false;
    emit generationFinishedChanged();
}

void FileListModel::requestThumbnails(QSize preferredSize, bool reset) {
//    qDebug() << "Request thumbnails" << preferredSize;

    QList<ImageReadRequest> requests;
    requests.reserve(_imagePaths.size() + _folderImagePaths.size());
    for (const QString &path : _folderImagePaths) {
        requests.append(ImageReadRequest(path, preferredSize, false, true));
    }
    for (const QString &path : _imagePaths) {
        requests.append(ImageReadRequest(path, preferredSize));
    }
    if (reset) {
        _generator->clearRequests();
    }
    _generator->requestRead(requests);
    _generationFinished = false;
    emit generationFinishedChanged();
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
}

ImageFile *FileListModel::createFileItem(const QString &folderPath, const QString &fileName, const QDateTime &lastModified) {
    ImageFile *item = new ImageFile();
    item->folderPath = folderPath;
    item->fileName = fileName;
    item->lastModified = lastModified;
    item->isFolder = false;

    if (isImage(item->fileName)) {
        item->isImage = true;
        item->iconPath = "qrc:/resources/ImageIcon.svg";
//                updateImageId(item);
        QString path = item->fullPath();
        _fileToItem.insert(path, item);

        QSettings set;
        set.beginGroup("imageCache");
        QDateTime savedLastModified = set.value(item->fullPath() + "/LastModified").toDateTime();
        if (lastModified.isValid() && lastModified != savedLastModified) {
            set.setValue(item->fullPath() + "/LastModified", item->lastModified);
        }
        if (savedLastModified == item->lastModified || (!item->lastModified.isValid() && savedLastModified.isValid())) {
            QSize fullSize = set.value(item->fullPath() + "/FullSize").toSize();
            if (fullSize.isValid()) {
                item->fullSize = fullSize;
            }
            if (!item->lastModified.isValid() && savedLastModified.isValid()) {
                item->lastModified = savedLastModified;
            }

            emit requestThumbnailFromCache(path, item->lastModified);
        }
        set.endGroup();
    }
    else {
        item->isImage = false;
        item->iconPath = "qrc:/resources/FileIcon.svg";
    }
    return item;
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

bool FileListModel::isImage(QString fileName) {
    return ThumbnailLoader::isJpeg(fileName) || ThumbnailLoader::isRawOrTiff(fileName) || ThumbnailLoader::isImageOther(fileName);
}

void FileListModel::requestViewer(int index, int width, int height) {
    QString requestedPath = _items[index]->fullPath();
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
            requests.append(ImageReadRequest(_items[i]->fullPath(), viewerSize, true));
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
            requests.insert(backwardInsertIndex, ImageReadRequest(_items[i]->fullPath(), viewerSize, true));
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
    _renderQueued = false;
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

    connect(sourceModel, SIGNAL(thumbnailReadFinished(ImageFile*)),
            this, SIGNAL(thumbnailReadFinished(ImageFile*)));
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

void RootProxyModel::requestThumbnails(QSize preferredSize, bool reset) {
    if (!sourceModel()) {
        return;
    }
    QStringList files;
    for (int i = 0; i < rowCount(); i++) {
        ImageFile *imageFile = index(i).data(FileListModel::ImageFileRole).value<ImageFile *>();

        files.append(imageFile->fullPath());
    }
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->requestThumbnails(files, preferredSize);
}

void RootProxyModel::addRequestThumbnails(QList<ImageReadRequest> requests) {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->addRequestThumbnails(requests);
}

void RootProxyModel::requestRender() {
    _renderQueued = true;
}

bool RootProxyModel::isRenderRequested() const {
    return _renderQueued;
}

void RootProxyModel::renderRequestComplete() {
    _renderQueued = false;
}

ImageFile *RootProxyModel::rootItem() const {
    return _sourceRoot;
}

void RootProxyModel::resetModel() {
    beginResetModel();
    endResetModel();
}
