#ifndef PERSISTENTCACHE_H
#define PERSISTENTCACHE_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QDataStream>

class PersistentCache : public QObject {
    Q_OBJECT

public:
    PersistentCache();

    void add(const QString &path, const QDateTime &lastModified, const QImage &image, const QVariantMap &exif);
    void requestThumbnail(const QString &path, const QDateTime &lastModified);
    void loadDb();
    void dumpDb();

signals:
    void cachedThumbnailAvailable(const QString &path, const QImage &thumbnail);

private:
    void loadChunkFile();

    struct ThumbnailLocation {
        uint16_t chunkFileIndex;
        uint64_t offsetInChunk;
        uint64_t thumbnailSize;
    };

    struct ThumbnailInfo {
        QDateTime lastModified;
        ThumbnailLocation location;
        QVariantMap exif;
    };

    QHash<QString, ThumbnailInfo> _db;

    QFile _currentChunkFile;
    QByteArray _currentChunkFileData;
    uint16_t _currentChunkFileIndex = 0;
    uint64_t _currentChunkFileSize = 0;
    int _currentChunkFileLastThumbnail = 0;

    friend QDataStream& operator<<(QDataStream& out, const ThumbnailLocation& obj);
    friend QDataStream& operator>>(QDataStream& in, ThumbnailLocation& obj);

    friend QDataStream& operator<<(QDataStream& out, const ThumbnailInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, ThumbnailInfo& obj);
};

#endif // PERSISTENTCACHE_H
