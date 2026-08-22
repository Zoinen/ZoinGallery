#ifndef ZOINGALLERY_SHAREDIMAGESOURCEPROVIDER_H
#define ZOINGALLERY_SHAREDIMAGESOURCEPROVIDER_H

#include <ZoinGallery/ImageSourceProvider.h>

#include <QHash>
#include <QList>
#include <QMutex>
#include <QWaitCondition>

#include <atomic>

namespace ZoinGallery {

// Runtime-wide proxy which coalesces concurrent materialization of the same
// immutable source revision and reuses an existing provider lease while any
// decode still holds it. Range reads remain under the provider/broker's own
// range-coalescing policy.
class SharedImageSourceProvider final : public ImageSourceProvider {
public:
    explicit SharedImageSourceProvider(
        QSharedPointer<ImageSourceProvider> provider);

    ImageSourceReadResult readRange(
        const ImageSourceDescriptor &source, qint64 offset, qint64 length,
        const QSharedPointer<ImageSourceCancellation> &cancellation) override;
    QSharedPointer<ImageSourceLease> materialize(
        const ImageSourceDescriptor &source,
        const QSharedPointer<ImageSourceCancellation> &cancellation) override;
    ImageSourceProbeResult probeEmbedded(
        const ImageSourceDescriptor &source,
        const QSharedPointer<ImageSourceCancellation> &cancellation) override;

private:
    struct PendingMaterialization {
        QWaitCondition ready;
        bool complete = false;
        QSharedPointer<ImageSourceLease> lease;
        QSharedPointer<ImageSourceCancellation> cancellation =
            QSharedPointer<ImageSourceCancellation>::create();
        std::atomic_int subscribers{0};
    };

    static void releaseSubscriber(
        const QSharedPointer<PendingMaterialization> &pending);
    void touchLease(const QString &key);
    void retainLease(const ImageSourceDescriptor &source,
                     const QString &key,
                     const QSharedPointer<ImageSourceLease> &lease);

    QSharedPointer<ImageSourceProvider> _provider;
    QMutex _mutex;
    // Retaining a small working set bridges metadata, Fit and Native passes.
    // Decoded-image caches own their pixels separately, so an unbounded
    // catalog-sized materialization cache is neither necessary nor desirable.
    QHash<QString, QSharedPointer<ImageSourceLease>> _leases;
    QList<QString> _leaseLru;
    QHash<QString, QString> _latestLeaseKeyBySource;
    QHash<QString, QString> _leaseSourceKeys;
    QHash<QString, qint64> _leaseBytes;
    qint64 _retainedLeaseBytes = 0;
    QHash<QString, QSharedPointer<PendingMaterialization>> _pending;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_SHAREDIMAGESOURCEPROVIDER_H
