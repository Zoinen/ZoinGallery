#ifndef ZOINGALLERY_GALLERYRUNTIME_H
#define ZOINGALLERY_GALLERYRUNTIME_H

#include <QObject>
#include <QSharedPointer>
#include <QString>

#include <ZoinGallery/ImageSourceProvider.h>

class QQmlEngine;

namespace ZoinGallery {

class GallerySession;

struct RuntimeOptions {
    QString providerPrefix = QStringLiteral("zoingallery");
    // Empty values derive collision-resistant names from providerPrefix.
    QString thumbnailProviderName;
    QString asyncProviderName;
    QString storageNamespace = QStringLiteral("embedded");
    // Positive values bound the shared pool. A non-positive value preserves
    // ZoinGallery standalone's historical QThread::idealThreadCount policy.
    int maxDecodeThreads = 4;
    // Async image-provider tasks only snapshot/crop already decoded frames.
    // Embedded mode keeps this auxiliary pool small; non-positive preserves
    // the standalone provider's historical Qt ideal-thread-count default.
    int maxImageProviderThreads = 2;
    // External panel thumbnails share one version-aware process-local LRU.
    // The cache is independent of session/provider IDs, so the two f4 panels
    // and presentation-mode switches can reuse a compatible decoded frame.
    qint64 thumbnailCacheByteBudget = 256LL * 1024LL * 1024LL;
    // Viewer frames are split into two independently bounded tiers. Fit
    // frames form the navigation predecode sequence; native frames may be
    // hundreds of MiB and must not evict that sequence. These embedded
    // defaults cap two external panel sessions at 1 GiB in aggregate, apart
    // from each panel's one pinned current frame when it alone exceeds a
    // tier's budget.
    qint64 viewerFitCacheByteBudget = 256LL * 1024LL * 1024LL;
    qint64 viewerNativeCacheByteBudget = 256LL * 1024LL * 1024LL;
    // A negative value for either viewer budget restores standalone's
    // historical retain-until-clear behavior. Embedded hosts should keep
    // both values non-negative so stale plans and excess frames are bounded.
    bool persistentCache = false;
    // Shared source access for external catalogs. When omitted, resourceId is
    // interpreted as a local path by LocalImageSourceProvider. Calls happen
    // synchronously on decode workers and may overlap across sessions.
    QSharedPointer<ImageSourceProvider> imageSourceProvider;
};

class GalleryRuntime final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString thumbnailProviderName READ thumbnailProviderName CONSTANT)
    Q_PROPERTY(QString asyncProviderName READ asyncProviderName CONSTANT)
    Q_PROPERTY(int decodeWorkerCount READ decodeWorkerCount CONSTANT)
    Q_PROPERTY(qint64 thumbnailCacheByteBudget READ thumbnailCacheByteBudget CONSTANT)
    Q_PROPERTY(qint64 thumbnailCacheRetainedBytes READ thumbnailCacheRetainedBytes)
    Q_PROPERTY(qsizetype thumbnailCacheFrameCount READ thumbnailCacheFrameCount)
    Q_PROPERTY(qsizetype thumbnailCachePendingRequestCount READ thumbnailCachePendingRequestCount)

public:
    static GalleryRuntime *install(
        QQmlEngine *engine, const RuntimeOptions &options = RuntimeOptions());
    static void registerTypes();

    ~GalleryRuntime() override;

    GallerySession *createExternalSession(
        const QString &sessionId, QObject *parent = nullptr);
    GallerySession *createSession(
        const QString &sessionId, QObject *parent = nullptr);

    QString thumbnailProviderName() const;
    QString asyncProviderName() const;
    QString storageNamespace() const;
    int decodeWorkerCount() const;
    qint64 thumbnailCacheByteBudget() const;
    qint64 thumbnailCacheRetainedBytes() const;
    qsizetype thumbnailCacheFrameCount() const;
    qsizetype thumbnailCachePendingRequestCount() const;
    quint64 thumbnailCacheHitCount() const;
    quint64 thumbnailCacheMissCount() const;
    quint64 thumbnailCacheCoalescedRequestCount() const;
    quint64 thumbnailCacheStoreCount() const;
    quint64 thumbnailCacheEvictionCount() const;

    Q_INVOKABLE void shutdown();

private:
    class Private;
    GalleryRuntime(QQmlEngine *engine, const RuntimeOptions &options);
    void asyncProviderDestroyed();
    Private *d;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYRUNTIME_H
