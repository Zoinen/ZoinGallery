#ifndef PERSISTENTIMAGECACHERUNNERS_H
#define PERSISTENTIMAGECACHERUNNERS_H

#include "DecodeManager.h"

struct ThumbnailLocation {
    uint16_t chunkFileIndex;
    uint64_t offsetInChunk;
    uint64_t thumbnailSize;
};

struct CachedImageInfo : public ImageInfo {
    ThumbnailLocation location;
};


class PersistentImageCacheAddRunner : public Runner {
    Q_OBJECT

public:
    RunnerType type() override { return RunnerType::PersistentImageCacheAdd; }
    void addToCache(const QString &path, const QDateTime &lastModified, const QImage &image, const QVariantMap &exif);
};


class PersistentImageCacheRetrieveRunner : public Runner {
    Q_OBJECT

public:
    RunnerType type() override { return RunnerType::PersistentImageCacheRetrieve; }
    void requestFromCache(const QString &path, const QDateTime &lastModified);

signals:
    void cachedThumbnailAvailable(const QString &path, const QImage &thumbnail);
};

#endif // PERSISTENTIMAGECACHERUNNERS_H
