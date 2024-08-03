#include "CacheViewer.h"

ImageInfoModel::ImageInfoModel(QObject *parent)
    : QAbstractListModel(parent) {
    refresh();
}

ImageInfoModel::~ImageInfoModel() {
    for (auto tempFile : m_tempFiles) {
        delete tempFile;
    }
}

int ImageInfoModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);
    return m_filteredPaths.size();
}

QVariant ImageInfoModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_filteredPaths.size())
        return QVariant();

    const QString &path = m_filteredPaths.at(index.row());
    PersistentImageCache::ThumbnailInfo info = m_cache.getThumbnailInfo(path);

    switch (role) {
    case PathRole:
        return path;
    case LastModifiedRole:
        return info.lastModified;
    case ThumbnailSizeRole:
        return info.location.thumbnailSize;
    case ImageSizeRole:
        return info.imageSize;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageInfoModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[PathRole] = "path";
    roles[LastModifiedRole] = "lastModified";
    roles[ThumbnailSizeRole] = "thumbnailSize";
    roles[ImageSizeRole] = "imageSize";
    return roles;
}

void ImageInfoModel::refresh() {
    beginResetModel();
    m_imagePaths = m_cache.getAllImagePaths();
    applyFilter();
    endResetModel();
}

void ImageInfoModel::setFilter(const QString &filter) {
    m_filter = filter;
    applyFilter();
}

void ImageInfoModel::applyFilter() {
    beginResetModel();
    if (m_filter.isEmpty()) {
        m_filteredPaths = m_imagePaths;
    } else {
        m_filteredPaths.clear();
        for (const QString &path : std::as_const(m_imagePaths)) {
            if (path.contains(m_filter, Qt::CaseInsensitive)) {
                m_filteredPaths.append(path);
            }
        }
    }
    endResetModel();
    // emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, 0));
}

QUrl ImageInfoModel::retrieveImage(int index) {
    ImageDecodeRequest request;
    request.info.path = m_filteredPaths.at(index);
    QImage image = m_cache.retrieveImage(request);

    QTemporaryFile *tempFile = new QTemporaryFile(QDir::tempPath() + "/XXXXXX.png");
    if (tempFile->open()) {
        image.save(tempFile->fileName(), "PNG", 100);
        QUrl fileUrl = QUrl::fromLocalFile(tempFile->fileName());
        m_tempFiles.append(QPointer<QTemporaryFile>(tempFile));  // Wrap the pointer in QPointer
        return fileUrl;
    }
    delete tempFile;
    return QUrl();
}
