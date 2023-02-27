#ifndef THUMBNAILCACHE_H
#define THUMBNAILCACHE_H

#include <QObject>
#include <QCache>

class ThumbnailCache : public QObject {
    Q_OBJECT

public:
    ThumbnailCache(QObject *parent = nullptr);

    void add(const QString &path, const QDateTime &lastModified, const QImage &thumbnail);
    void requestThumbnail(const QString &path, const QDateTime &lastModified);

signals:
    void cachedThumbnailAvailable(const QString &path, const QImage &thumbnail);

private:
    QCache<QString, QByteArray> _cache;
    qint64 _totalSize;
};

#endif // THUMBNAILCACHE_H
