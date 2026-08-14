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
    static constexpr qint64 DefaultFitByteBudget =
        256LL * 1024LL * 1024LL;
    static constexpr qint64 DefaultNativeByteBudget =
        256LL * 1024LL * 1024LL;
    // The pre-split standalone viewer retained every decoded frame until the
    // catalog/cache was cleared. A negative budget selects that historical
    // policy; embedded sessions keep the bounded defaults above.
    static constexpr qint64 UnboundedByteBudget = -1;
    // Compatibility alias for callers which configure both tiers equally.
    static constexpr qint64 DefaultByteBudget = DefaultFitByteBudget;

    struct Entry {
        QImage image;
        QString imageId;
        QSize requestedSize;
        DecodedImageInfo decodedInfo;
        QDateTime sourceLastModified;
        qint64 sourceFileSize = -1;
        qint64 sourceVersionToken = 0;
        // A native-size image can still be part of the Fit predecode
        // sequence when the source is smaller than the viewport. Account it
        // against the Fit budget so it receives the same retention guarantee
        // as a scaled Fit frame.
        bool fitPrepared = false;
    };

    struct StoredImage {
        bool accepted = false;
        // A decoded frame may be worth retaining without being large enough
        // for the request which produced it.  Persistent-cache lookups are
        // the common case: they return a useful 1024px fallback while the
        // source decode covering the viewer target is still in flight.
        // Callers must only publish a level-1/2 viewer tier when this flag is
        // true.
        bool presentable = false;
        QString url;
        int level = -1;
    };

    struct RequestPlan {
        QList<QPair<QString, int>> cachedImages;
        QList<ImageDecodeRequest> decodeRequests;
    };

    ViewerImageCache(
        QString idPrefix,
        QSharedPointer<ProviderImageStore> providerImageStore,
        QString thumbnailProviderName = QStringLiteral("thumbnails"),
        QString asyncProviderName = QStringLiteral("async"),
        qint64 byteBudget = DefaultByteBudget);
    ViewerImageCache(
        QString idPrefix,
        QSharedPointer<ProviderImageStore> providerImageStore,
        QString thumbnailProviderName,
        QString asyncProviderName,
        qint64 fitByteBudget,
        qint64 nativeByteBudget);

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
    QList<QPair<QString, int>> imageSources(
        const ImageFile *item, const QSize &viewerSize = QSize()) const;
    QString bestImageUrl(const ImageFile *item) const;
    QImage viewerImageForId(const QString &imageId) const;
    QImage fullSizeImageForId(const QString &imageId) const;
    Entry entryForPath(const QString &path, bool fullSize) const;
    bool needsDecode(const ImageDecodeRequest &request) const;
    qsizetype viewerImageCount() const;
    qsizetype fullSizeImageCount() const;
    qint64 retainedBytes() const;
    qint64 fitRetainedBytes() const;
    qint64 nativeRetainedBytes() const;
    qint64 byteBudget() const;
    qint64 fitByteBudget() const;
    qint64 nativeByteBudget() const;

    void setByteBudget(qint64 byteBudget);
    void setByteBudgets(qint64 fitByteBudget,
                        qint64 nativeByteBudget);
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
        bool presentFullSizeAsFitImage,
        const QSize &minimumViewerSize = QSize(),
        const QSize &minimumFullSize = QSize()) const;
    static bool satisfies(const Entry &entry, const QSize &targetSize);
    static bool covers(const QSize &size, const QSize &targetSize);
    static qint64 entryByteSize(const Entry &entry);
    static bool usesFitBudget(const Entry &entry, bool fullSize);
    bool needsDecode(const ImageInfo &info, const QSize &targetSize,
                     bool fullSize) const;
    void recordPlannedTargetLocked(const ImageDecodeRequest &request);
    QSize latestPlannedTargetLocked(
        const ImageDecodeRequest &request) const;
    void updateRetentionPlanLocked(
        const QString &currentPath, const QList<QString> &plannedPaths,
        int prefetchCount);
    void pruneToBudgetsLocked();
    void pruneTierToBudgetLocked(bool fitTier);
    void addRetainedBytesLocked(const Entry &entry, bool fullSize);
    void subtractRetainedBytesLocked(const Entry &entry, bool fullSize);
    void markFitPreparedLocked(Entry &entry, bool fullSize);
    void removeEntry(const QString &path, bool fullSize);
    void removeEntryLocked(const QString &path, bool fullSize);
    QString nextImageId(const ImageDecodeRequest &request);

    QString _idPrefix;
    QString _thumbnailProviderName;
    QString _asyncProviderName;
    int _lastImageId = 0;
    QSharedPointer<ProviderImageStore> _providerImageStore;
    mutable QReadWriteLock _lock;
    QHash<QString, Entry> _viewerImages;
    QHash<QString, Entry> _fullSizeImages;
    QHash<QString, QString> _viewerIdToPath;
    QHash<QString, QString> _fullSizeIdToPath;
    QHash<QString, QSize> _latestPlannedFitTargets;
    QHash<QString, QSize> _latestPlannedNativeTargets;
    QHash<QString, int> _retentionRanks;
    QList<QString> _supplementalPaths;
    QString _currentPath;
    int _primaryPrefetchCount = 0;
    bool _hasRetentionPlan = false;
    qint64 _fitRetainedBytes = 0;
    qint64 _nativeRetainedBytes = 0;
    qint64 _fitByteBudget = DefaultFitByteBudget;
    qint64 _nativeByteBudget = DefaultNativeByteBudget;
    bool _boundedRetention = true;
};

#endif // VIEWERIMAGECACHE_H
