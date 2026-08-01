#include "PersistentImageCache.h"
#include "DisplayColorSpace.h"
#include "Decoders/WebpCodec.h"

#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>

#include <utility>

namespace {
constexpr int CacheFormatVersion = 8;
constexpr float CacheWebpQuality = 75.0F;

bool sourceMatchesImageInfo(const ImageInfo &imageInfo) {
    if (!imageInfo.lastModified.isValid() || imageInfo.fileSize < 0) {
        return false;
    }
    const QFileInfo fileInfo(imageInfo.path);
    return fileInfo.isFile() &&
           fileInfo.lastModified() == imageInfo.lastModified &&
           fileInfo.size() == imageInfo.fileSize;
}

QString cacheBasePath() {
    const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return tempPath.isEmpty() ? QDir::tempPath() : tempPath;
}

QString cacheDbPath() {
    return QDir(cacheBasePath()).filePath(QString("zg_v%1.db").arg(CacheFormatVersion));
}

QString cacheChunkPath(uint16_t chunkFileIndex) {
    return QDir(cacheBasePath()).filePath(QString("zg_v%1_%2").arg(CacheFormatVersion).arg(chunkFileIndex));
}

bool hasUsableThumbnail(const PersistentImageCache::ThumbnailInfo &info) {
    if (!info.location.thumbnailSize) {
        return false;
    }

    const QFileInfo chunkInfo(cacheChunkPath(info.location.chunkFileIndex));
    return chunkInfo.isFile()
        && info.location.offsetInChunk <= static_cast<quint64>(chunkInfo.size())
        && info.location.thumbnailSize <= static_cast<quint64>(chunkInfo.size()) - info.location.offsetInChunk;
}

}

QHash<QString, PersistentImageCache::ThumbnailInfo> PersistentImageCache::_db;
QReadWriteLock PersistentImageCache::_dbAccess;
QReadWriteLock PersistentImageCache::_currentChunkFileAccess;
QMutex PersistentImageCache::_dbLoadAccess;
uint16_t PersistentImageCache::_currentChunkFileIndex = 0;
bool PersistentImageCache::_dbLoaded = false;
std::atomic<quint64> PersistentImageCache::_generation = 0;


bool PersistentImageCache::hasImage(const QString &path, bool validateSource) {
    loadDb();

    QReadLocker locker(&_dbAccess);
    const auto it = _db.constFind(path);
    if (it == _db.cend() || !it->lastModified.isValid()
        || it->imageSize.width() <= 1
        || it->imageSize.height() <= 1
        || !hasUsableThumbnail(*it)) {
        return false;
    }
    if (!validateSource) {
        return true;
    }
    const QFileInfo fileInfo(path);
    return fileInfo.isFile()
        && it->lastModified == fileInfo.lastModified()
        && it->fileSize >= 0
        && it->fileSize == fileInfo.size()
        && it->imageSize.width() > 1
        && it->imageSize.height() > 1
        && hasUsableThumbnail(*it);
}

void PersistentImageCache::retrieveImagesInfo(const QStringList &imagePaths, QList<ImageInfo> &outInfoList,
                                              QStringList &outNotFound, bool validateSource) {
    loadDb();

    QReadLocker locker(&_dbAccess);
    for (const QString &path : imagePaths) {
        const auto it = _db.constFind(path);
        bool cacheIsCurrent = it != _db.cend() && it->lastModified.isValid();
        if (cacheIsCurrent && validateSource) {
            const QFileInfo fileInfo(path);
            cacheIsCurrent = fileInfo.isFile()
                && it->lastModified == fileInfo.lastModified()
                && it->fileSize >= 0
                && it->fileSize == fileInfo.size();
        }
        const bool cacheHasUsableSize = it != _db.cend()
            && it->imageSize.width() > 1 && it->imageSize.height() > 1;
        if (cacheIsCurrent && cacheHasUsableSize && hasUsableThumbnail(*it)) {
            outInfoList.append(ImageInfo {
                .path = path,
                .lastModified = it->lastModified,
                .fileSize = it->fileSize,
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
}

QImage PersistentImageCache::retrieveImage(ImageDecodeRequest &request, bool validateRequestVersion) {
    loadDb();

    ThumbnailInfo info;
    {
        QReadLocker locker(&_dbAccess);
        const auto it = _db.constFind(request.info.path);
        if (it == _db.cend() || !hasUsableThumbnail(*it)) {
            return {};
        }
        info = *it;
    }

    if (validateRequestVersion && request.info.lastModified.isValid()
        && request.info.lastModified != info.lastModified) {
        return {};
    }
    if (validateRequestVersion && request.info.fileSize >= 0
        && request.info.fileSize != info.fileSize) {
        return {};
    }

    QByteArray thumbnailData;
    {
        QReadLocker locker(&_currentChunkFileAccess);
        QFile currentChunkFile(cacheChunkPath(info.location.chunkFileIndex));
        if (!currentChunkFile.open(QFile::ReadOnly)
            || !currentChunkFile.seek(info.location.offsetInChunk)) {
            return {};
        }
        thumbnailData = currentChunkFile.read(info.location.thumbnailSize);
    }
    if (thumbnailData.size() != static_cast<qsizetype>(info.location.thumbnailSize)) {
        return {};
    }

    QImage result = WebpCodec::decode(thumbnailData);
    if (result.isNull()) {
        return {};
    }
    result.setColorSpace(DisplayColorSpace::cacheColorSpace());
    if (request.targetSize.isValid() && (request.targetSize.width() < result.width() ||
                                         request.targetSize.height() < result.height())) { // Never upscale
        result = result.scaled(request.targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        result.setColorSpace(DisplayColorSpace::cacheColorSpace());
    }
    return DisplayColorSpace::convertImage(result);
}

void PersistentImageCache::storeImage(const ImageInfo &imageInfo, const QByteArray &imageData) {
    if (imageData.isEmpty() || !imageInfo.lastModified.isValid()
        || imageInfo.fileSize < 0 || imageInfo.imageSize.width() <= 1
        || imageInfo.imageSize.height() <= 1
        || !sourceMatchesImageInfo(imageInfo)) {
        return;
    }

    const quint64 generation = _generation.load();
    loadDb();
    {
        QReadLocker locker(&_dbAccess);
        const auto it = _db.constFind(imageInfo.path);
        if (it != _db.cend() && it->lastModified == imageInfo.lastModified
            && it->fileSize == imageInfo.fileSize
            && it->imageSize == imageInfo.imageSize && hasUsableThumbnail(*it)) {
            return;
        }
    }

    quint64 offset = 0;
    {
        QWriteLocker locker(&_currentChunkFileAccess);
        if (generation != _generation.load()) {
            return;
        }
        QFile currentChunkFile(cacheChunkPath(_currentChunkFileIndex));
        if (!currentChunkFile.open(QFile::WriteOnly | QFile::Append)) {
            return;
        }
        offset = currentChunkFile.size();
        if (currentChunkFile.write(imageData) != imageData.size()) {
            currentChunkFile.resize(offset);
            return;
        }
    }

    const ThumbnailInfo info {
        .lastModified = imageInfo.lastModified,
        .fileSize = imageInfo.fileSize,
        .location = ThumbnailLocation {
            .chunkFileIndex = _currentChunkFileIndex,
            .offsetInChunk = offset,
            .thumbnailSize = static_cast<quint64>(imageData.size())
        },
        .exif = imageInfo.exif,
        .imageSize = imageInfo.imageSize,
        .orientation = imageInfo.orientation
    };

    QWriteLocker locker(&_dbAccess);
    if (generation != _generation.load() ||
        !sourceMatchesImageInfo(imageInfo)) {
        return;
    }
    _db.insert(imageInfo.path, info);
}

QByteArray PersistentImageCache::createImageForCache(const QImage &image) {
    if (image.isNull()) {
        return {};
    }

    QImage scaled = DisplayColorSpace::convertImageToColorSpace(image, DisplayColorSpace::cacheColorSpace());
    if (CACHE_IMAGE_RESOLUTION.width() < image.width() ||
        CACHE_IMAGE_RESOLUTION.height() < image.height()) { // Never upscale
        scaled = image.scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled = DisplayColorSpace::convertImageToColorSpace(scaled, DisplayColorSpace::cacheColorSpace());
    }
    if (scaled.isNull()) {
        return {};
    }
    return WebpCodec::encode(scaled, CacheWebpQuality);
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
    out << obj.lastModified << obj.fileSize << obj.location << obj.exif
        << obj.imageSize << obj.orientation;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentImageCache::ThumbnailInfo& obj) {
    in >> obj.lastModified >> obj.fileSize >> obj.location >> obj.exif
       >> obj.imageSize >> obj.orientation;
    return in;
}

void PersistentImageCache::loadDb() {
    QMutexLocker loadLocker(&_dbLoadAccess);
    if (_dbLoaded) {
        return;
    }

    QWriteLocker dbLocker(&_dbAccess);
    QFile dbFile(cacheDbPath());
    if (dbFile.open(QIODevice::ReadOnly)) {
        QDataStream stream(&dbFile);
        stream >> _db;
        if (stream.status() == QDataStream::Ok) {
            for (const ThumbnailInfo &info : std::as_const(_db)) {
                _currentChunkFileIndex = qMax(_currentChunkFileIndex, info.location.chunkFileIndex);
            }
            qDebug() << "Loaded DB with" << _db.size() << "entities";
        }
        else {
            _db.clear();
            qWarning() << "Ignoring unreadable image cache DB" << cacheDbPath();
        }
    }
    _dbLoaded = true;
}

void PersistentImageCache::dumpDb() {
    loadDb();

    QReadLocker locker(&_dbAccess);
    if (_db.isEmpty()) {
        QFile::remove(cacheDbPath());
        return;
    }
    QFile dbFile(cacheDbPath());
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << _db;
        if (stream.status() == QDataStream::Ok) {
            qDebug() << "Saved DB with" << _db.size() << "entities";
        }
        else {
            qWarning() << "Failed to save image cache DB" << cacheDbPath();
        }
    }
}

qint64 PersistentImageCache::cacheSize() {
    loadDb();

    qint64 size = 0;
    {
        QReadLocker locker(&_currentChunkFileAccess);
        const QDir cacheDir(cacheBasePath());
        const QString chunkPattern = QString("zg_v%1_*").arg(CacheFormatVersion);
        const auto chunks = cacheDir.entryInfoList({chunkPattern}, QDir::Files, QDir::Name);
        for (const QFileInfo &chunk : chunks) {
            size += chunk.size();
        }
    }

    qint64 serializedDbSize = 0;
    {
        QReadLocker locker(&_dbAccess);
        if (!_db.isEmpty()) {
            QByteArray serializedDb;
            QDataStream stream(&serializedDb, QIODevice::WriteOnly);
            stream << _db;
            if (stream.status() == QDataStream::Ok) {
                serializedDbSize = serializedDb.size();
            }
        }
    }
    return size + qMax(serializedDbSize, QFileInfo(cacheDbPath()).size());
}

QString PersistentImageCache::cacheLocation() {
    return QDir::toNativeSeparators(cacheBasePath());
}

void PersistentImageCache::clear() {
    loadDb();
    _generation.fetch_add(1);

    QWriteLocker chunkLocker(&_currentChunkFileAccess);
    QWriteLocker dbLocker(&_dbAccess);
    _db.clear();
    _currentChunkFileIndex = 0;

    QDir cacheDir(cacheBasePath());
    QFile::remove(cacheDbPath());
    const QString chunkPattern = QString("zg_v%1_*").arg(CacheFormatVersion);
    const auto chunks = cacheDir.entryInfoList({chunkPattern}, QDir::Files, QDir::Name);
    for (const QFileInfo &chunk : chunks) {
        QFile::remove(chunk.absoluteFilePath());
    }
}

QStringList PersistentImageCache::getAllImagePaths() const {
    loadDb();
    QReadLocker locker(&_dbAccess);
    return _db.keys();
}

PersistentImageCache::ThumbnailInfo PersistentImageCache::getThumbnailInfo(const QString &path) const {
    loadDb();
    QReadLocker locker(&_dbAccess);
    return _db.value(path);
}
