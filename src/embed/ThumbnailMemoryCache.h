#ifndef ZOINGALLERY_THUMBNAILMEMORYCACHE_H
#define ZOINGALLERY_THUMBNAILMEMORYCACHE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QStringList>

class QImage;
class ProviderImageStore;

namespace ZoinGallery {

// Process-local thumbnail storage shared by every GallerySession installed in
// one GalleryRuntime. The provider store owns the actual QImage allocation;
// this class owns its provider ID, byte accounting, compatibility lookup, and
// eviction policy.
class ThumbnailMemoryCache final : public QObject {
    Q_OBJECT

public:
    static constexpr qint64 DefaultByteBudget =
        256LL * 1024LL * 1024LL;
    static constexpr auto DefaultTransformKey = "thumbnail-aspect-v1";

    enum class AcquireState {
        Hit,
        Owner,
        Pending,
    };

    struct Handle {
        QString providerId;
        QSize decodedSize;

        bool isValid() const {
            return !providerId.isEmpty() && decodedSize.isValid();
        }
    };

    struct AcquireResult {
        AcquireState state = AcquireState::Owner;
        Handle handle;
        // For Pending this identifies the admitted owner request this caller
        // coalesced behind. For Owner it is the newly admitted request.
        QSize pendingTargetSize;
        QString pendingTransformKey;
    };

    explicit ThumbnailMemoryCache(
        QSharedPointer<ProviderImageStore> store,
        qint64 byteBudget = DefaultByteBudget,
        QObject *parent = nullptr);
    ~ThumbnailMemoryCache() override;

    static QString canonicalSourceIdentity(const QString &path);

    AcquireResult acquire(
        const QString &ownerId, const QString &sourceIdentity,
        qint64 versionToken, qint64 sourceFileSize,
        const QSize &targetSize,
        const QString &transformKey =
            QString::fromLatin1(DefaultTransformKey));
    Handle lookup(
        const QString &sourceIdentity, qint64 versionToken,
        qint64 sourceFileSize, const QSize &targetSize,
        const QString &transformKey =
            QString::fromLatin1(DefaultTransformKey));
    Handle storeDecoded(
        const QString &ownerId, const QString &sourceIdentity,
        qint64 versionToken, qint64 sourceFileSize,
        const QSize &requestedSize, const QString &transformKey,
        const QImage &image);

    void releaseRequest(
        const QString &ownerId, const QString &sourceIdentity,
        qint64 versionToken, qint64 sourceFileSize,
        const QSize &targetSize,
        const QString &transformKey =
            QString::fromLatin1(DefaultTransformKey),
        bool retryWaiters = true);
    void cancelRequests(const QString &ownerId);
    void clear();

    qint64 byteBudget() const;
    qint64 retainedBytes() const;
    qsizetype frameCount() const;
    qsizetype pendingRequestCount() const;
    quint64 hitCount() const;
    quint64 missCount() const;
    quint64 coalescedRequestCount() const;
    quint64 storeCount() const;
    quint64 evictionCount() const;

signals:
    void frameAvailable(const QString &sourceIdentity,
                        qint64 versionToken, qint64 sourceFileSize,
                        const QSize &requestedSize,
                        const QString &transformKey,
                        const QString &providerId);
    void frameEvicted(const QString &providerId);
    void requestReleased(const QString &sourceIdentity,
                         qint64 versionToken, qint64 sourceFileSize,
                         const QSize &requestedSize,
                         const QString &transformKey,
                         bool retryWaiters);

private:
    struct FrameEntry {
        QString sourceKey;
        QString providerId;
        QSize decodedSize;
        qint64 bytes = 0;
        quint64 lastUse = 0;
    };

    struct PendingEntry {
        QString sourceKey;
        QString sourceIdentity;
        qint64 versionToken = 0;
        qint64 sourceFileSize = -1;
        QSize targetSize;
        QString transformKey;
        QString ownerId;
    };

    static QString normalizedTransformKey(const QString &transformKey);
    static QString sourceKey(const QString &sourceIdentity,
                             qint64 versionToken,
                             qint64 sourceFileSize,
                             const QString &transformKey);
    static QString frameKey(const QString &sourceKey,
                            const QSize &decodedSize);
    static bool decodedFrameCovers(const QSize &available,
                                   const QSize &requested);
    static bool targetBoundsCover(const QSize &available,
                                  const QSize &requested);

    Handle compatibleFrameLocked(const QString &sourceKey,
                                 const QSize &targetSize,
                                 bool touch);
    PendingEntry compatiblePendingLocked(const QString &sourceKey,
                                         const QSize &targetSize) const;
    QString nextProviderIdLocked(const QString &ownerId,
                                 const QString &sourceKey,
                                 qint64 versionToken,
                                 qint64 sourceFileSize,
                                 const QSize &decodedSize);
    QStringList pruneLocked();
    void removeProviderIds(const QStringList &providerIds,
                           bool notifyEviction);

    QSharedPointer<ProviderImageStore> _store;
    mutable QMutex _mutex;
    QHash<QString, FrameEntry> _frames;
    QMultiHash<QString, QString> _sourceFrames;
    QHash<QString, PendingEntry> _pending;
    qint64 _byteBudget = DefaultByteBudget;
    qint64 _retainedBytes = 0;
    quint64 _clock = 0;
    quint64 _serial = 0;
    quint64 _hits = 0;
    quint64 _misses = 0;
    quint64 _coalesced = 0;
    quint64 _stores = 0;
    quint64 _evictions = 0;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_THUMBNAILMEMORYCACHE_H
