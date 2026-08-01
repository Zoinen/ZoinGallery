#ifndef VIEWERIMAGECACHE_H
#define VIEWERIMAGECACHE_H

#include "ImageFile.h"
#include "ProviderImageStore.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QPair>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QString>

class ViewerImageCache {
public:
    struct Entry {
        QImage image;
        QString imageId;
        QSize requestedSize;
        DecodedImageInfo decodedInfo;
        QDateTime sourceLastModified;
        qint64 sourceFileSize = -1;
    };

    struct StoredImage {
        bool accepted = false;
        QString url;
        int level = -1;
    };

    struct RequestPlan {
        QList<QPair<QString, int>> cachedImages;
        QList<ImageDecodeRequest> decodeRequests;
    };

    ViewerImageCache(QString idPrefix,
                     QSharedPointer<ProviderImageStore> providerImageStore);

    static ImageDecodeRequest makeRequest(
        const ImageInfo &info, const QSize &originalSize,
        const QSize &viewerSize = QSize());
    static bool isFullSizeRequest(const ImageDecodeRequest &request);

    RequestPlan planRequest(const QList<ImageFile *> &items, int currentIndex,
                            const QSize &viewerSize, int prefetchCount = 16);
    StoredImage storeDecodedImage(const ImageDecodeRequest &request,
                                  const QImage &image,
                                  const DecodedImageInfo &decodedInfo);

    QList<QPair<QString, int>> cachedImagesForPath(
        const QString &path, bool includeFullSize) const;
    QString bestImageUrl(const ImageFile *item) const;
    QImage viewerImageForId(const QString &imageId) const;
    QImage fullSizeImageForId(const QString &imageId) const;
    Entry entryForPath(const QString &path, bool fullSize) const;
    bool needsDecode(const ImageDecodeRequest &request) const;
    qsizetype viewerImageCount() const;
    qsizetype fullSizeImageCount() const;

    void removeIncomplete(const QString &path);
    void remove(const QString &path);
    void clear();

private:
    static ImageDecodeRequest requestForItem(
        const ImageFile *item, const QSize &viewerSize);
    QList<QPair<QString, int>> cachedImagesForRequest(
        const ImageDecodeRequest &request) const;
    QList<QPair<QString, int>> cachedImagesForPath(
        const QString &path, bool includeFullSize,
        bool presentFullSizeAsFitImage) const;
    static bool satisfies(const Entry &entry, const QSize &targetSize);
    static bool covers(const QSize &size, const QSize &targetSize);
    bool needsDecode(const ImageInfo &info, const QSize &targetSize,
                     bool fullSize) const;
    void removeEntry(const QString &path, bool fullSize);
    void removeEntryLocked(const QString &path, bool fullSize);
    QString nextImageId();

    QString _idPrefix;
    int _lastImageId = 0;
    QSharedPointer<ProviderImageStore> _providerImageStore;
    mutable QReadWriteLock _lock;
    QHash<QString, Entry> _viewerImages;
    QHash<QString, Entry> _fullSizeImages;
    QHash<QString, QString> _viewerIdToPath;
    QHash<QString, QString> _fullSizeIdToPath;
};

#endif // VIEWERIMAGECACHE_H
