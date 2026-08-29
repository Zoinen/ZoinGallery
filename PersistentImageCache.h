#ifndef PERSISTENTIMAGECACHE_H
#define PERSISTENTIMAGECACHE_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QDataStream>
#include <QMutex>
#include <QReadWriteLock>

#include <atomic>

#include "ImageFile.h"

class PersistentImageCache {
public:
    struct ThumbnailLocation {
        uint16_t chunkFileIndex;
        uint64_t offsetInChunk;
        uint64_t thumbnailSize;
    };

    struct ThumbnailInfo {
        QDateTime lastModified;
        qint64 fileSize = -1;
        ThumbnailLocation location;
        QVariantMap exif;
        QSize imageSize;
        ExifOrientation orientation = ExifOrientation::Horizontal;
    };

    static bool hasImage(const QString &path, bool validateSource = true);
    static bool hasImage(const ImageDecodeRequest &request);
    static void retrieveImagesInfo(const QStringList &imagePaths, QList<ImageInfo> &outInfoList,
                                   QStringList &outNotFound, bool validateSource = true);
    static QImage retrieveImage(ImageDecodeRequest &request, bool validateRequestVersion = true);
    static void storeImage(const ImageInfo &imageInfo, const QByteArray &imageData);
    static void storeImage(const ImageDecodeRequest &request,
                           const QByteArray &imageData);
    static QByteArray createImageForCache(const QImage &image);
    static QByteArray createImageForCache(const ImageDecodeRequest &request,
                                          const QImage &image);

    static void loadDb();
    static void dumpDb();
    static qint64 cacheSize();
    static QString cacheLocation();
    static void clear();

    QStringList getAllImagePaths() const;

    ThumbnailInfo getThumbnailInfo(const QString &path) const;

private:
    static uint16_t _currentChunkFileIndex;
    static QHash<QString, ThumbnailInfo> _db;
    static QReadWriteLock _dbAccess;
    static QReadWriteLock _currentChunkFileAccess;
    static QMutex _dbLoadAccess;
    static bool _dbLoaded;
    static std::atomic<quint64> _generation;

    friend QDataStream& operator<<(QDataStream& out, const ThumbnailLocation& obj);
    friend QDataStream& operator>>(QDataStream& in, ThumbnailLocation& obj);

    friend QDataStream& operator<<(QDataStream& out, const ThumbnailInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, ThumbnailInfo& obj);
};

#endif // PERSISTENTIMAGECACHE_H
