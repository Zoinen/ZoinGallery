#include "PersistentImageCache.h"

#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>

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
        if (it != _db.end()) {
            outInfoList.append(ImageInfo{
                .path = path,
                .lastModified = it->lastModified,
                .imageSize = it->imageSize,
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
        if (request.info.lastModified == it.value().lastModified) {
            QFile currentChunkFile(QString("C:/tmp/zg_%1").arg(it.value().location.chunkFileIndex));
            _currentChunkFileAccess.lockForRead();
            if (currentChunkFile.open(QFile::ReadOnly)) {
                if (currentChunkFile.seek(it.value().location.offsetInChunk)) {
                    QByteArray thumbnailData = currentChunkFile.read(it.value().location.thumbnailSize);

                    QImage cachedImage = QImage::fromData(thumbnailData, "webp");
                    if (request.targetSize.width() < cachedImage.width() ||
                        request.targetSize.height() < cachedImage.height()) { // Never upscale
                        result = cachedImage.scaled(request.targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    }
                }
                currentChunkFile.close();
            }
            _currentChunkFileAccess.unlock();
        }
    }
    return result;
}

void PersistentImageCache::storeImage(const ImageInfo &imageInfo, const QImage &image) {
    // return;
    _dbAccess.lockForRead();
    auto it = _db.find(imageInfo.path);
    _dbAccess.unlock();

    // TODO: Update when date changes
    if (it == _db.end()) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);

        QImage scaled = image;
        if (CACHE_IMAGE_RESOLUTION.width() < image.width() ||
            CACHE_IMAGE_RESOLUTION.height() < image.height()) { // Never upscale
            scaled = image.scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        scaled.invertPixels();
        scaled.save(&buffer, "webp", 10);

        uint64_t thumbnailSize = bytes.size();

        QFile currentChunkFile(QString("C:/tmp/zg_%1").arg(_currentChunkFileIndex));
        _currentChunkFileAccess.lockForWrite();
        uint64_t currentChunkFileSize = 0;
        if (currentChunkFile.open(QFile::WriteOnly | QFile::Append)) {
            currentChunkFileSize = currentChunkFile.size();
            currentChunkFile.write(bytes);
            currentChunkFile.close();
        }
        _currentChunkFileAccess.unlock();

        ThumbnailInfo info{imageInfo.lastModified, {_currentChunkFileIndex, currentChunkFileSize, thumbnailSize}, imageInfo.exif, imageInfo.imageSize};

        _dbAccess.lockForWrite();
        _db.insert(imageInfo.path, info);
        _dbAccess.unlock();
    }
}

QDataStream& operator<<(QDataStream& out, const PersistentImageCache::ThumbnailLocation& obj) {
    out << obj.chunkFileIndex << obj.offsetInChunk << obj.thumbnailSize;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentImageCache::ThumbnailLocation& obj) {
    in >> obj.chunkFileIndex >> obj.offsetInChunk >> obj.thumbnailSize;
    return in;
}

QDataStream& operator<<(QDataStream& out, const PersistentImageCache::ThumbnailInfo& obj) {
    out << obj.lastModified << obj.location << obj.exif << obj.imageSize;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentImageCache::ThumbnailInfo& obj) {
    in >> obj.lastModified >> obj.location >> obj.exif >> obj.imageSize;
    return in;
}

void PersistentImageCache::loadDb() {
    _dbAccess.lockForWrite();

    QFile dbFile("C:/tmp/zg.db");
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

    QFile dbFile("C:/tmp/zg.db");
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << _db;
        dbFile.close();

        qDebug() << "Saved DB with" << _db.size() << "entities";
    }

    _dbAccess.unlock();
}
