#ifndef PERSISTENTIMAGECACHE_H
#define PERSISTENTIMAGECACHE_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QDataStream>
#include <QReadWriteLock>

#include "ImageFile.h"

class PersistentImageCache {
public:
    static bool hasImage(const QString &path);
    static void retrieveImagesInfo(const QStringList &imagePaths, QList<ImageInfo> &outInfoList, QStringList &outNotFound);
    static QImage retrieveImage(ImageDecodeRequest &request);
    static void storeImage(const ImageInfo &imageInfo, const QImage &image);

    static void loadDb();
    static void dumpDb();

private:
    struct ThumbnailLocation {
        uint16_t chunkFileIndex;
        uint64_t offsetInChunk;
        uint64_t thumbnailSize;
    };

    struct ThumbnailInfo {
        QDateTime lastModified;
        ThumbnailLocation location;
        QVariantMap exif;
        QSize imageSize;
    };

    static uint16_t _currentChunkFileIndex;
    static QHash<QString, ThumbnailInfo> _db;
    static QReadWriteLock _dbAccess;
    static QReadWriteLock _currentChunkFileAccess;

    friend QDataStream& operator<<(QDataStream& out, const ThumbnailLocation& obj);
    friend QDataStream& operator>>(QDataStream& in, ThumbnailLocation& obj);

    friend QDataStream& operator<<(QDataStream& out, const ThumbnailInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, ThumbnailInfo& obj);
};

#endif // PERSISTENTIMAGECACHE_H
