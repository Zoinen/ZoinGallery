#include "SharedImageSourceProvider.h"

#include <QMutexLocker>

#include <utility>

namespace ZoinGallery {

namespace {

// Leases for external sources retain disk-backed spools, not decoded pixels.
// Keep enough of a large phone-camera folder to bridge the foreground
// thumbnail and background Fit passes without downloading the first visible
// sources again after the catalog walk has displaced them.
constexpr qsizetype RetainedLeaseCapacity = 256;
constexpr qint64 RetainedLeaseByteBudget = 1024LL * 1024LL * 1024LL;
constexpr qint64 UnknownLeaseCharge = 128LL * 1024LL * 1024LL;

} // namespace

SharedImageSourceProvider::SharedImageSourceProvider(
    QSharedPointer<ImageSourceProvider> provider)
    : _provider(std::move(provider)) {
}

ImageSourceReadResult SharedImageSourceProvider::readRange(
    const ImageSourceDescriptor &source, qint64 offset, qint64 length,
    const QSharedPointer<ImageSourceCancellation> &cancellation) {
    if (!_provider) {
        return {.errorString = QStringLiteral("no image source provider")};
    }
    return _provider->readRange(source, offset, length, cancellation);
}

QSharedPointer<ImageSourceLease> SharedImageSourceProvider::materialize(
    const ImageSourceDescriptor &source,
    const QSharedPointer<ImageSourceCancellation> &cancellation) {
    if (!_provider || !source.isValid() ||
        (cancellation && cancellation->isCanceled())) {
        return {};
    }

    const QString key = source.accessKey();
    for (;;) {
        QSharedPointer<PendingMaterialization> pending;
        bool owner = false;
        {
            QMutexLocker locker(&_mutex);
            if (const QSharedPointer<ImageSourceLease> cached =
                    _leases.value(key)) {
                touchLease(key);
                return cached;
            }
            pending = _pending.value(key);
            if (!pending) {
                pending = QSharedPointer<PendingMaterialization>::create();
                _pending.insert(key, pending);
                owner = true;
            }
            pending->subscribers.fetch_add(1, std::memory_order_relaxed);
        }

        quint64 callbackId = 0;
        if (cancellation) {
            callbackId = cancellation->addCallback([pending]() {
                releaseSubscriber(pending);
            });
        }
        const auto unsubscribe = [&]() {
            if (!cancellation ||
                (callbackId != 0 &&
                 cancellation->removeCallback(callbackId))) {
                releaseSubscriber(pending);
            }
        };

        if (owner) {
            // The operation token is canceled only when every subscriber has
            // canceled. In particular, canceling the runner which happened
            // to create the flight does not abort a second session's read.
            const QSharedPointer<ImageSourceLease> lease =
                _provider->materialize(source, pending->cancellation);
            {
                QMutexLocker locker(&_mutex);
                pending->lease = lease;
                pending->complete = true;
                if (lease) {
                    retainLease(source, key, lease);
                }
                _pending.remove(key);
                pending->ready.wakeAll();
            }
            unsubscribe();
            return cancellation && cancellation->isCanceled()
                ? QSharedPointer<ImageSourceLease>() : lease;
        }

        {
            QMutexLocker locker(&_mutex);
            while (!pending->complete &&
                   !(cancellation && cancellation->isCanceled())) {
                pending->ready.wait(&_mutex, 25);
            }
        }
        const bool callerCanceled =
            cancellation && cancellation->isCanceled();
        const bool retryCanceledFlight =
            !callerCanceled && !pending->lease &&
            pending->cancellation->isCanceled();
        const QSharedPointer<ImageSourceLease> lease = pending->lease;
        unsubscribe();
        if (callerCanceled) {
            return {};
        }
        if (lease) {
            return lease;
        }
        if (!retryCanceledFlight) {
            return {};
        }
        // All original subscribers canceled just as this caller arrived.
        // Start a fresh flight instead of inheriting their canceled token.
    }
}

void SharedImageSourceProvider::releaseSubscriber(
    const QSharedPointer<PendingMaterialization> &pending) {
    if (pending->subscribers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        pending->cancellation->cancel();
    }
}

void SharedImageSourceProvider::touchLease(const QString &key) {
    _leaseLru.removeAll(key);
    _leaseLru.append(key);
}

void SharedImageSourceProvider::retainLease(
    const ImageSourceDescriptor &source, const QString &key,
    const QSharedPointer<ImageSourceLease> &lease) {
    const QString previous = _latestLeaseKeyBySource.value(source.sourceKey);
    if (!previous.isEmpty() && previous != key) {
        _retainedLeaseBytes -= _leaseBytes.take(previous);
        _leases.remove(previous);
        _leaseLru.removeAll(previous);
        _leaseSourceKeys.remove(previous);
    }
    const qint64 reportedBytes = lease->retainedBytes();
    const qint64 retainedBytes = reportedBytes >= 0
        ? reportedBytes
        : source.size >= 0 ? source.size : UnknownLeaseCharge;
    if (retainedBytes > RetainedLeaseByteBudget) {
        // The active caller still owns the returned lease. Avoid pinning one
        // oversized spool in the cross-pass cache after that caller finishes.
        _latestLeaseKeyBySource.remove(source.sourceKey);
        return;
    }
    _latestLeaseKeyBySource.insert(source.sourceKey, key);
    _leaseSourceKeys.insert(key, source.sourceKey);
    _leaseBytes.insert(key, retainedBytes);
    _retainedLeaseBytes += retainedBytes;
    _leases.insert(key, lease);
    touchLease(key);
    while (_leaseLru.size() > RetainedLeaseCapacity ||
           _retainedLeaseBytes > RetainedLeaseByteBudget) {
        const QString evicted = _leaseLru.takeFirst();
        _retainedLeaseBytes -= _leaseBytes.take(evicted);
        _leases.remove(evicted);
        const QString evictedSource = _leaseSourceKeys.take(evicted);
        if (_latestLeaseKeyBySource.value(evictedSource) == evicted) {
            _latestLeaseKeyBySource.remove(evictedSource);
        }
    }
}

ImageSourceProbeResult SharedImageSourceProvider::probeEmbedded(
    const ImageSourceDescriptor &source,
    const QSharedPointer<ImageSourceCancellation> &cancellation) {
    return _provider
        ? _provider->probeEmbedded(source, cancellation)
        : ImageSourceProbeResult{};
}

} // namespace ZoinGallery
