#ifndef PERSISTENTDERIVEDIMAGECACHE_H
#define PERSISTENTDERIVEDIMAGECACHE_H

#include "ImageFile.h"

#include <QMutex>
#include <QSharedPointer>
#include <QWaitCondition>

class PersistentDerivedLookupGate final {
public:
    PersistentDerivedLookupGate() = default;

private:
    friend class PersistentDerivedImageCache;

    QMutex mutex;
    QWaitCondition completedCondition;
    bool completed = false;
    bool cacheHit = false;
};

// Disk cache for pixels derived from opaque external sources. Durable source
// revisions use the persistent cache root; weak/session revisions use an
// authority-keyed process-session directory. Unlike the legacy path/stat
// cache, every entry is addressed exclusively by host-provided identity and
// revision. The class is intentionally internal to ZoinGallery;
// PersistentImageCache is the public integration point used by decode runners.
class PersistentDerivedImageCache final {
public:
    using LookupGate = QSharedPointer<PersistentDerivedLookupGate>;

    static bool appliesTo(const ImageDecodeRequest &request);
    static bool isEligible(const ImageDecodeRequest &request);
    static bool hasImage(const ImageDecodeRequest &request);
    static QImage retrieveImage(const ImageDecodeRequest &request);
    static void storeImage(const ImageDecodeRequest &request,
                           const QByteArray &preparedEntry);
    static void storePreparedImage(const ImageInfo &sourceInfo,
                                   const QByteArray &preparedEntry);
    static QByteArray createImageForCache(const ImageDecodeRequest &request,
                                          const QImage &image);
    // Small source metadata manifest keyed by the same immutable revision and
    // authority rules as derived pixels. This lets a cold catalog compute its
    // Fit key before opening a slow source and therefore reach a persisted
    // derived hit without a full materialization.
    static bool retrieveMetadata(ImageInfo &info);
    static void storeMetadata(const ImageInfo &info);

    static qint64 cacheSize();
    static qint64 persistentCacheSize();
    static qint64 sessionCacheSize();
    static void clear();
    // Clears only artifacts whose revisions are not safe beyond the current
    // broker/process authority. Primarily useful for lifecycle tests; the
    // session directory is also removed automatically when the process exits.
    static void clearSession();

    // DecodeManager currently admits cache and source runners in parallel.
    // These gates let the source runner wait for the preceding derived-cache
    // lookup, so a proven cache hit never opens or materializes a VFS source.
    static LookupGate beginLookup(const ImageDecodeRequest &request);
    static LookupGate joinLookup(const ImageDecodeRequest &request);
    static void completeLookup(const LookupGate &gate, bool cacheHit);
    static bool waitForLookup(
        const LookupGate &gate,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation);
};

#endif // PERSISTENTDERIVEDIMAGECACHE_H
