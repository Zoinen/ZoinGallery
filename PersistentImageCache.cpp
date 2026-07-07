#include "PersistentImageCache.h"
#include "DisplayColorSpace.h"

#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QElapsedTimer>
#include <QBuffer>

namespace {
constexpr int CacheFormatVersion = 4;

QString cacheDbPath() {
    return QString("C:/tmp/zg_v%1.db").arg(CacheFormatVersion);
}

QString cacheChunkPath(uint16_t chunkFileIndex) {
    return QString("C:/tmp/zg_v%1_%2").arg(CacheFormatVersion).arg(chunkFileIndex);
}
}

QHash<QString, PersistentImageCache::ThumbnailInfo> PersistentImageCache::_db;
QReadWriteLock PersistentImageCache::_dbAccess;
QReadWriteLock PersistentImageCache::_currentChunkFileAccess;
uint16_t PersistentImageCache::_currentChunkFileIndex = 0;


bool PersistentImageCache::hasImage(const QString &path) {
    // TODO: Check date here somewhere
    _dbAccess.lockForRead();
    auto it = _db.find(path);
    _dbAccess.unlock();
    return it != _db.end();
}

void PersistentImageCache::retrieveImagesInfo(const QStringList &imagePaths, QList<ImageInfo> &outInfoList, QStringList &outNotFound) {
    static bool cacheDbLoaded = false;
    if (!cacheDbLoaded) {
        cacheDbLoaded = true;
        loadDb();
    }

    _dbAccess.lockForRead();
    for (const QString &path : imagePaths) {
        auto it = _db.find(path);
        const QFileInfo fileInfo(path);
        const bool cacheIsCurrent = it != _db.end() && fileInfo.exists() && it->lastModified == fileInfo.lastModified();
        const bool cacheHasUsableSize = it != _db.end() && it->imageSize.width() > 1 && it->imageSize.height() > 1;
        if (cacheIsCurrent && cacheHasUsableSize) {
            outInfoList.append(ImageInfo {
                .path = path,
                .lastModified = it->lastModified,
                .imageSize = it->imageSize,
                .orientation = it->orientation,
                .exif = it->exif,
                .isCached = true,
            });
        }
        else {
            outNotFound.append(path);
        }
    }
    _dbAccess.unlock();
}

QImage PersistentImageCache::retrieveImage(ImageDecodeRequest &request) {
    QImage result;
    _dbAccess.lockForRead();
    auto it = _db.find(request.info.path);
    _dbAccess.unlock();
    if (it != _db.end()) {
        if (!request.info.lastModified.isValid() || request.info.lastModified == it.value().lastModified) {
            QFile currentChunkFile(cacheChunkPath(it.value().location.chunkFileIndex));
            _currentChunkFileAccess.lockForRead();
            if (currentChunkFile.open(QFile::ReadOnly)) {
                if (currentChunkFile.seek(it.value().location.offsetInChunk)) {
                    QByteArray thumbnailData = currentChunkFile.read(it.value().location.thumbnailSize);

                    result = QImage::fromData(thumbnailData, "webp");
                    if (!result.isNull()) {
                        result.setColorSpace(DisplayColorSpace::cacheColorSpace());
                    }
                    if (request.targetSize.isValid() && (request.targetSize.width() < result.width() ||
                                                         request.targetSize.height() < result.height())) { // Never upscale
                        result = result.scaled(request.targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        result.setColorSpace(DisplayColorSpace::cacheColorSpace());
                    }
                    result = DisplayColorSpace::convertImage(result);
                }
                currentChunkFile.close();
            }
            _currentChunkFileAccess.unlock();
        }
    }
    return result;
}

void PersistentImageCache::storeImage(const ImageInfo &imageInfo, const QByteArray &imageData) {
    // return;
    _dbAccess.lockForRead();
    auto it = _db.find(imageInfo.path);
    const bool shouldStore = it == _db.end()
        || it->lastModified != imageInfo.lastModified
        || it->imageSize.width() <= 1
        || it->imageSize.height() <= 1;
    _dbAccess.unlock();

    if (shouldStore) {
        uint64_t thumbnailSize = imageData.size();

        QFile currentChunkFile(cacheChunkPath(_currentChunkFileIndex));
        _currentChunkFileAccess.lockForWrite();
        uint64_t currentChunkFileSize = 0;
        if (currentChunkFile.open(QFile::WriteOnly | QFile::Append)) {
            currentChunkFileSize = currentChunkFile.size();
            currentChunkFile.write(imageData);
            currentChunkFile.close();
        }
        _currentChunkFileAccess.unlock();

        ThumbnailInfo info {
            .lastModified = imageInfo.lastModified,
            .location = ThumbnailLocation {
                .chunkFileIndex = _currentChunkFileIndex,
                .offsetInChunk = currentChunkFileSize,
                .thumbnailSize = thumbnailSize
            },
            .exif = imageInfo.exif,
            .imageSize = imageInfo.imageSize,
            .orientation = imageInfo.orientation
        };

        _dbAccess.lockForWrite();
        _db.insert(imageInfo.path, info);
        _dbAccess.unlock();
    }
}

QByteArray PersistentImageCache::createImageForCache(const QImage &image) {
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);

    QImage scaled = DisplayColorSpace::convertImageToColorSpace(image, DisplayColorSpace::cacheColorSpace());
    if (CACHE_IMAGE_RESOLUTION.width() < image.width() ||
        CACHE_IMAGE_RESOLUTION.height() < image.height()) { // Never upscale
        scaled = image.scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled = DisplayColorSpace::convertImageToColorSpace(scaled, DisplayColorSpace::cacheColorSpace());
    }
    // scaled.invertPixels();
    scaled.save(&buffer, "webp", 50);
    return imageData;
}

QDataStream& operator<<(QDataStream& out, const PersistentImageCache::ThumbnailLocation& obj) {
    out << obj.chunkFileIndex << (quint64)obj.offsetInChunk << (quint64)obj.thumbnailSize;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentImageCache::ThumbnailLocation& obj) {
    quint64 offsetInChunk, thumbnailSize;
    in >> obj.chunkFileIndex >> offsetInChunk >> thumbnailSize;
    obj.offsetInChunk = offsetInChunk;
    obj.thumbnailSize = thumbnailSize;
    return in;
}

QDataStream& operator<<(QDataStream& out, const PersistentImageCache::ThumbnailInfo& obj) {
    out << obj.lastModified << obj.location << obj.exif << obj.imageSize << obj.orientation;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentImageCache::ThumbnailInfo& obj) {
    in >> obj.lastModified >> obj.location >> obj.exif >> obj.imageSize >> obj.orientation;
    return in;
}

void PersistentImageCache::loadDb() {
    _dbAccess.lockForWrite();

    QFile dbFile(cacheDbPath());
    if (dbFile.open(QIODevice::ReadOnly)) {
        QDataStream stream(&dbFile);
        stream >> _db;

        dbFile.close();

        qDebug() << "Loaded DB with" << _db.size() << "entities";
    }

    _dbAccess.unlock();
}

void PersistentImageCache::dumpDb() {
    _dbAccess.lockForWrite();

    QFile dbFile(cacheDbPath());
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << _db;
        dbFile.close();

        qDebug() << "Saved DB with" << _db.size() << "entities";
    }

    _dbAccess.unlock();
}

QStringList PersistentImageCache::getAllImagePaths() const {
    QReadLocker locker(&_dbAccess);
    return _db.keys();
}

PersistentImageCache::ThumbnailInfo PersistentImageCache::getThumbnailInfo(const QString &path) const {
    QReadLocker locker(&_dbAccess);
    return _db.value(path);
}
