#ifndef ZOINGALLERY_IMAGESOURCEPROVIDER_H
#define ZOINGALLERY_IMAGESOURCEPROVIDER_H

#include <QByteArray>
#include <QSharedPointer>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace ZoinGallery {

// Host-owned identity and access information for one immutable source
// revision. Paths returned by materialize() are deliberately absent from the
// descriptor: a temporary backing path is an implementation detail and must
// never become a cache or catalog identity.
struct ImageSourceDescriptor {
    QString resourceId;
    QString sourceKey;
    QString contentVersion;
    QString versionStrength;
    QString storageClass;
    QString accessProfile;
    QString displayName;
    QString mimeType;
    qint64 size = -1;

    bool isValid() const {
        return !resourceId.isEmpty() && !sourceKey.isEmpty();
    }

    QString cacheKey() const {
        return sourceKey + QChar(0x1f) + contentVersion;
    }

    // Access leases and in-flight I/O are bound to one broker authority.
    // They must never coalesce across reconnect/access epochs even when the
    // immutable content revision remains reusable by decoded caches.
    QString accessKey() const {
        return cacheKey() + QChar(0x1f) + resourceId + QChar(0x1f) +
            accessProfile + QChar(0x1f) + storageClass;
    }

    QString runtimeIdentity() const {
        QString strength = versionStrength.trimmed().toLower();
        strength.remove(QLatin1Char('-'));
        strength.remove(QLatin1Char('_'));
        if (strength == QStringLiteral("strong") ||
            strength == QStringLiteral("localstat")) {
            return sourceKey;
        }
        // Weak/session revisions may only reuse decoded pixels within the
        // exact broker authority which observed them.
        return sourceKey + QChar(0x1e) + resourceId;
    }
};

// Cancellation is intentionally independent of QObject affinity. Providers
// are called synchronously from decode workers and may inspect this token from
// any thread while performing a range read or materialization.
class ImageSourceCancellation final {
public:
    void cancel();

    bool isCanceled() const {
        return _canceled.load(std::memory_order_acquire);
    }

private:
    using Callback = std::function<void()>;

    // SharedImageSourceProvider uses these callbacks to aggregate per-runner
    // cancellation without exposing a second cancellation primitive to host
    // providers. Consumers only cancel or inspect their own token.
    quint64 addCallback(Callback callback);
    bool removeCallback(quint64 callbackId);

    friend class SharedImageSourceProvider;

    std::atomic_bool _canceled{false};
    std::mutex _callbackMutex;
    quint64 _nextCallbackId = 1;
    std::unordered_map<quint64, Callback> _callbacks;
};

// A lease pins a provider-owned local backing file. Implementations must make
// destruction safe on an arbitrary worker thread. Hosts that need an event
// loop to release a remote resource should queue that release asynchronously.
class ImageSourceLease {
public:
    virtual ~ImageSourceLease() = default;
    virtual QString localPath() const = 0;
    // Best-effort retained backing size for bounded lease caches. Providers
    // which cannot know it return -1; callers must apply a conservative
    // charge instead of treating unknown as free.
    virtual qint64 retainedBytes() const { return -1; }
};

struct ImageSourceReadResult {
    QByteArray data;
    bool endOfFile = false;
    QString errorString;

    bool succeeded() const {
        return errorString.isEmpty();
    }
};

enum class ImageSourceProbeStatus {
    Unsupported,
    NotFound,
    Found,
    Failed,
};

// Optional provider shortcut for a future bounded embedded-preview pass. The
// normal implementation lives in ZoinGallery and uses readRange(); hosts may
// override probeEmbedded() when their backend already has authoritative
// preview metadata.
struct ImageSourceProbeResult {
    ImageSourceProbeStatus status = ImageSourceProbeStatus::Unsupported;
    QByteArray encodedData;
    QString mimeType;
    QSize pixelSize;
    QString errorString;
};

// Methods are synchronous because DecodeManager invokes them only from worker
// threads. A provider instance is shared by all sessions and must support
// concurrent calls. Returned leases and cancellation tokens are thread-safe
// ownership boundaries; no method is invoked on the GUI thread by the source
// pipeline itself.
class ImageSourceProvider {
public:
    virtual ~ImageSourceProvider() = default;

    virtual ImageSourceReadResult readRange(
        const ImageSourceDescriptor &source, qint64 offset, qint64 length,
        const QSharedPointer<ImageSourceCancellation> &cancellation) = 0;

    virtual QSharedPointer<ImageSourceLease> materialize(
        const ImageSourceDescriptor &source,
        const QSharedPointer<ImageSourceCancellation> &cancellation) = 0;

    virtual ImageSourceProbeResult probeEmbedded(
        const ImageSourceDescriptor &source,
        const QSharedPointer<ImageSourceCancellation> &cancellation) {
        Q_UNUSED(source)
        Q_UNUSED(cancellation)
        return {};
    }
};

// Default adapter used when an embedding does not supply a VFS provider.
// resourceId is interpreted as a local path. External catalogs should still
// provide a stable sourceKey so cache identity remains independent from a
// future materialized path.
class LocalImageSourceProvider final : public ImageSourceProvider {
public:
    ImageSourceReadResult readRange(
        const ImageSourceDescriptor &source, qint64 offset, qint64 length,
        const QSharedPointer<ImageSourceCancellation> &cancellation) override;

    QSharedPointer<ImageSourceLease> materialize(
        const ImageSourceDescriptor &source,
        const QSharedPointer<ImageSourceCancellation> &cancellation) override;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_IMAGESOURCEPROVIDER_H
