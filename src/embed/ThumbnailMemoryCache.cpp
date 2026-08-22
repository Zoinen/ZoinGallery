#include "ThumbnailMemoryCache.h"

#include "ProviderImageStore.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>

#include <algorithm>
#include <limits>
#include <utility>

namespace ZoinGallery {

ThumbnailMemoryCache::ThumbnailMemoryCache(
    QSharedPointer<ProviderImageStore> store, qint64 byteBudget,
    QObject *parent)
    : QObject(parent),
      _store(std::move(store)),
      _byteBudget(qMax<qint64>(0, byteBudget)) {
}

ThumbnailMemoryCache::~ThumbnailMemoryCache() {
    clear();
}

QString ThumbnailMemoryCache::canonicalSourceIdentity(const QString &path) {
    if (path.isEmpty()) {
        return {};
    }
    // f4 already provides a platform-local absolute path. Normalizing it
    // lexically keeps aliases such as `dir/./file` cache-equivalent without a
    // synchronous realpath/stat call for every row in a 10k-entry catalog.
    // The version and source-size tokens below still prevent stale reuse.
    const QFileInfo info(path);
    return QDir::cleanPath(info.absoluteFilePath());
}

QString ThumbnailMemoryCache::normalizedTransformKey(
    const QString &transformKey) {
    const QString normalized = transformKey.trimmed();
    return normalized.isEmpty()
        ? QString::fromLatin1(DefaultTransformKey) : normalized;
}

QString ThumbnailMemoryCache::sourceKey(
    const QString &sourceIdentity, const QString &versionToken,
    qint64 sourceFileSize, const QString &transformKey) {
    return sourceIdentity + QChar(0x1f) +
        versionToken + QChar(0x1f) +
        QString::number(sourceFileSize) + QChar(0x1f) +
        normalizedTransformKey(transformKey);
}

QString ThumbnailMemoryCache::frameKey(
    const QString &sourceKeyValue, const QSize &decodedSize) {
    return sourceKeyValue + QChar(0x1f) +
        QString::number(decodedSize.width()) + QLatin1Char('x') +
        QString::number(decodedSize.height());
}

bool ThumbnailMemoryCache::decodedFrameCovers(
    const QSize &available, const QSize &requested) {
    // MasonryLayout resolves fit/crop policy into an exact, source-aspect
    // decoded QSize before submitting work. Transform keys isolate genuinely
    // different pixel-processing families; presentation-only crop versus fit
    // shares thumbnail-aspect-v1. Both dimensions must cover the requested
    // pixel tier.
    return available.isValid() && requested.isValid() &&
        available.width() >= requested.width() &&
        available.height() >= requested.height();
}

bool ThumbnailMemoryCache::targetBoundsCover(
    const QSize &available, const QSize &requested) {
    return available.isValid() && requested.isValid() &&
        available.width() >= requested.width() &&
        available.height() >= requested.height();
}

ThumbnailMemoryCache::Handle ThumbnailMemoryCache::compatibleFrameLocked(
    const QString &sourceKeyValue, const QSize &targetSize, bool touch) {
    QString bestKey;
    qint64 bestArea = std::numeric_limits<qint64>::max();
    const QList<QString> candidates = _sourceFrames.values(sourceKeyValue);
    for (const QString &candidateKey : candidates) {
        const auto it = _frames.constFind(candidateKey);
        if (it == _frames.constEnd() ||
            !decodedFrameCovers(it->decodedSize, targetSize)) {
            continue;
        }
        const qint64 area = qint64(it->decodedSize.width()) *
            qint64(it->decodedSize.height());
        if (area < bestArea) {
            bestArea = area;
            bestKey = candidateKey;
        }
    }
    if (bestKey.isEmpty()) {
        return {};
    }
    auto it = _frames.find(bestKey);
    if (touch) {
        it->lastUse = ++_clock;
    }
    return {it->providerId, it->decodedSize};
}

ThumbnailMemoryCache::PendingEntry
ThumbnailMemoryCache::compatiblePendingLocked(
    const QString &sourceKeyValue, const QSize &targetSize) const {
    PendingEntry best;
    qint64 bestArea = std::numeric_limits<qint64>::max();
    for (auto it = _pending.cbegin(); it != _pending.cend(); ++it) {
        if (it->sourceKey == sourceKeyValue &&
            targetBoundsCover(it->targetSize, targetSize)) {
            const qint64 area = qint64(it->targetSize.width()) *
                qint64(it->targetSize.height());
            if (area < bestArea) {
                best = it.value();
                bestArea = area;
            }
        }
    }
    return best;
}

ThumbnailMemoryCache::AcquireResult ThumbnailMemoryCache::acquire(
    const QString &ownerId, const QString &sourceIdentity,
    const QString &versionToken, qint64 sourceFileSize, const QSize &targetSize,
    const QString &transformKey) {
    const QString canonicalSource = sourceIdentity;
    if (canonicalSource.isEmpty() || !targetSize.isValid() ||
        targetSize.width() <= 0 || targetSize.height() <= 0) {
        return {AcquireState::Pending, {}};
    }

    QMutexLocker locker(&_mutex);
    const QString normalizedTransform =
        normalizedTransformKey(transformKey);
    const QString sourceKeyValue = sourceKey(
        canonicalSource, versionToken, sourceFileSize,
        normalizedTransform);
    const Handle cached = compatibleFrameLocked(
        sourceKeyValue, targetSize, true);
    if (cached.isValid()) {
        ++_hits;
        return {AcquireState::Hit, cached};
    }
    const PendingEntry compatiblePending = compatiblePendingLocked(
        sourceKeyValue, targetSize);
    if (compatiblePending.targetSize.isValid()) {
        ++_coalesced;
        return {AcquireState::Pending, {},
                compatiblePending.targetSize,
                compatiblePending.transformKey};
    }

    const QString requestKey = frameKey(sourceKeyValue, targetSize);
    _pending.insert(requestKey, PendingEntry{
        .sourceKey = sourceKeyValue,
        .sourceIdentity = canonicalSource,
        .versionToken = versionToken,
        .sourceFileSize = sourceFileSize,
        .targetSize = targetSize,
        .transformKey = normalizedTransform,
        .ownerId = ownerId,
    });
    ++_misses;
    return {AcquireState::Owner, {}, targetSize, normalizedTransform};
}

ThumbnailMemoryCache::Handle ThumbnailMemoryCache::lookup(
    const QString &sourceIdentity, const QString &versionToken,
    qint64 sourceFileSize, const QSize &targetSize,
    const QString &transformKey) {
    const QString canonicalSource = sourceIdentity;
    if (canonicalSource.isEmpty() || !targetSize.isValid()) {
        return {};
    }
    QMutexLocker locker(&_mutex);
    return compatibleFrameLocked(
        sourceKey(canonicalSource, versionToken, sourceFileSize,
                  transformKey),
        targetSize, true);
}

QString ThumbnailMemoryCache::nextProviderIdLocked(
    const QString &ownerId, const QString &sourceKeyValue,
    const QString &versionToken,
    qint64 sourceFileSize, const QSize &decodedSize) {
    QString ownerToken = ownerId;
    for (QChar &character : ownerToken) {
        if (!character.isLetterOrNumber() &&
            character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            character = QLatin1Char('_');
        }
    }
    if (ownerToken.isEmpty()) {
        ownerToken = QStringLiteral("runtime");
    }
    return QStringLiteral("zg-thumb-%1-%2-v%3-s%4-%5x%6-%7")
        .arg(ownerToken)
        .arg(QString::number(qHash(sourceKeyValue), 16))
        .arg(versionToken)
        .arg(sourceFileSize)
        .arg(decodedSize.width())
        .arg(decodedSize.height())
        .arg(++_serial);
}

QStringList ThumbnailMemoryCache::pruneLocked() {
    QStringList evicted;
    // Keep one oversized most-recent frame so a single unusual thumbnail can
    // still be presented. Every subsequent insertion evicts it normally.
    while (_frames.size() > 1 && _retainedBytes > _byteBudget) {
        auto victim = _frames.end();
        for (auto it = _frames.begin(); it != _frames.end(); ++it) {
            if (victim == _frames.end() ||
                it->lastUse < victim->lastUse) {
                victim = it;
            }
        }
        if (victim == _frames.end()) {
            break;
        }
        _retainedBytes = qMax<qint64>(0,
            _retainedBytes - victim->bytes);
        _sourceFrames.remove(victim->sourceKey, victim.key());
        evicted.append(victim->providerId);
        _frames.erase(victim);
        ++_evictions;
    }
    return evicted;
}

ThumbnailMemoryCache::Handle ThumbnailMemoryCache::storeDecoded(
    const QString &ownerId, const QString &sourceIdentity,
    const QString &versionToken, qint64 sourceFileSize,
    const QSize &requestedSize, const QString &transformKey,
    const QImage &image) {
    const QString canonicalSource = sourceIdentity;
    if (canonicalSource.isEmpty() || image.isNull() ||
        !image.size().isValid()) {
        releaseRequest(ownerId, sourceIdentity, versionToken,
                       sourceFileSize, requestedSize, transformKey,
                       false);
        return {};
    }

    QStringList evicted;
    Handle result;
    QSize completedRequestedSize = requestedSize;
    const QString normalizedTransform =
        normalizedTransformKey(transformKey);
    QString completedTransform = normalizedTransform;
    {
        QMutexLocker locker(&_mutex);
        const QString sourceKeyValue = sourceKey(
            canonicalSource, versionToken, sourceFileSize,
            normalizedTransform);
        const QString requestKey = frameKey(sourceKeyValue, requestedSize);
        const auto pendingIt = _pending.constFind(requestKey);
        if (pendingIt != _pending.constEnd() &&
            (ownerId.isEmpty() || pendingIt->ownerId == ownerId)) {
            // Snapshot the admitted request before erasing it. The decoded
            // frame may preserve source aspect and therefore be smaller than
            // this bounding box in one dimension; exact coalesced waiters
            // still need to know which request produced the provider frame.
            completedRequestedSize = pendingIt->targetSize;
            completedTransform = pendingIt->transformKey;
            _pending.remove(requestKey);
        }

        // A larger compatible result may have completed while this decode was
        // in flight. Reuse it rather than retaining a redundant tier.
        result = compatibleFrameLocked(
            sourceKeyValue, image.size(), true);
        if (!result.isValid()) {
            const QString exactKey = frameKey(sourceKeyValue, image.size());
            auto existing = _frames.find(exactKey);
            if (existing != _frames.end()) {
                existing->lastUse = ++_clock;
                result = {existing->providerId, existing->decodedSize};
            }
            else {
                const QString providerId = nextProviderIdLocked(
                    ownerId, sourceKeyValue, versionToken, sourceFileSize,
                    image.size());
                const qint64 bytes = qMax<qint64>(
                    0, static_cast<qint64>(image.sizeInBytes()));
                _frames.insert(exactKey, FrameEntry{
                    .sourceKey = sourceKeyValue,
                    .providerId = providerId,
                    .decodedSize = image.size(),
                    .bytes = bytes,
                    .lastUse = ++_clock,
                });
                _sourceFrames.insert(sourceKeyValue, exactKey);
                _retainedBytes += bytes;
                ++_stores;
                // Publish before exposing the handle outside the cache lock;
                // a concurrent lookup must never observe a provider ID whose
                // pixels have not reached ProviderImageStore yet.
                if (_store) {
                    _store->publish(providerId, image);
                }
                result = {providerId, image.size()};
                evicted = pruneLocked();
            }
        }
    }

    removeProviderIds(evicted, true);
    if (result.isValid()) {
        emit frameAvailable(canonicalSource, versionToken,
                            sourceFileSize, completedRequestedSize,
                            completedTransform, result.providerId);
    }
    return result;
}

void ThumbnailMemoryCache::releaseRequest(
    const QString &ownerId, const QString &sourceIdentity,
    const QString &versionToken, qint64 sourceFileSize, const QSize &targetSize,
    const QString &transformKey, bool retryWaiters) {
    const QString canonicalSource = sourceIdentity;
    PendingEntry released;
    bool didRelease = false;
    {
        QMutexLocker locker(&_mutex);
        const QString requestKey = frameKey(
            sourceKey(canonicalSource, versionToken, sourceFileSize,
                      transformKey),
            targetSize);
        const auto it = _pending.constFind(requestKey);
        if (it != _pending.constEnd() &&
            (ownerId.isEmpty() || it->ownerId == ownerId)) {
            released = it.value();
            _pending.remove(requestKey);
            didRelease = true;
        }
    }
    if (didRelease) {
        emit requestReleased(released.sourceIdentity,
                             released.versionToken,
                             released.sourceFileSize,
                             released.targetSize,
                             released.transformKey,
                             retryWaiters);
    }
}

void ThumbnailMemoryCache::cancelRequests(const QString &ownerId) {
    cancelRequests(ownerId, {});
}

void ThumbnailMemoryCache::cancelRequests(
    const QString &ownerId,
    const QSet<QString> &sourceIdentities) {
    QList<PendingEntry> released;
    {
        QMutexLocker locker(&_mutex);
        for (auto it = _pending.begin(); it != _pending.end();) {
            if (it->ownerId == ownerId &&
                (sourceIdentities.isEmpty() ||
                 sourceIdentities.contains(it->sourceIdentity))) {
                released.append(it.value());
                it = _pending.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    for (const PendingEntry &entry : std::as_const(released)) {
        emit requestReleased(entry.sourceIdentity, entry.versionToken,
                             entry.sourceFileSize, entry.targetSize,
                             entry.transformKey, true);
    }
}

void ThumbnailMemoryCache::removeProviderIds(
    const QStringList &providerIds, bool notifyEviction) {
    if (_store && !providerIds.isEmpty()) {
        _store->remove(providerIds);
    }
    if (notifyEviction) {
        for (const QString &providerId : providerIds) {
            emit frameEvicted(providerId);
        }
    }
}

void ThumbnailMemoryCache::clear() {
    QStringList providerIds;
    {
        QMutexLocker locker(&_mutex);
        providerIds.reserve(_frames.size());
        for (const FrameEntry &entry : std::as_const(_frames)) {
            providerIds.append(entry.providerId);
        }
        _frames.clear();
        _sourceFrames.clear();
        _pending.clear();
        _retainedBytes = 0;
    }
    removeProviderIds(providerIds, true);
}

qint64 ThumbnailMemoryCache::byteBudget() const {
    return _byteBudget;
}

qint64 ThumbnailMemoryCache::retainedBytes() const {
    QMutexLocker locker(&_mutex);
    return _retainedBytes;
}

qsizetype ThumbnailMemoryCache::frameCount() const {
    QMutexLocker locker(&_mutex);
    return _frames.size();
}

qsizetype ThumbnailMemoryCache::pendingRequestCount() const {
    QMutexLocker locker(&_mutex);
    return _pending.size();
}

quint64 ThumbnailMemoryCache::hitCount() const {
    QMutexLocker locker(&_mutex);
    return _hits;
}

quint64 ThumbnailMemoryCache::missCount() const {
    QMutexLocker locker(&_mutex);
    return _misses;
}

quint64 ThumbnailMemoryCache::coalescedRequestCount() const {
    QMutexLocker locker(&_mutex);
    return _coalesced;
}

quint64 ThumbnailMemoryCache::storeCount() const {
    QMutexLocker locker(&_mutex);
    return _stores;
}

quint64 ThumbnailMemoryCache::evictionCount() const {
    QMutexLocker locker(&_mutex);
    return _evictions;
}

} // namespace ZoinGallery
