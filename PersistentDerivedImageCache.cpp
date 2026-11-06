#include "PersistentDerivedImageCache.h"

#include "Decoders/WebpCodec.h"
#include "DisplayColorSpace.h"
#include "StorageLocations.h"
#include "FolderPreviewSelection.h"
#include "src/embed/DecodeSizePolicy.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QCryptographicHash>
#include <QCache>
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace {

using ZoinGallery::boundedPanelThumbnailSize;
using ZoinGallery::MaximumPanelThumbnailEdge;
using ZoinGallery::panelThumbnailCovers;

constexpr quint32 DerivedFileMagic = 0x5a474431; // "ZGD1"
constexpr quint16 DerivedFileVersion = 3;
constexpr quint32 MetadataFileMagic = 0x5a474d31; // "ZGM1"
constexpr quint16 MetadataFileVersion = 2;
qint64 derivedCacheBudget = 512LL * 1024LL * 1024LL; // guarded by cacheMutex
constexpr qint64 MaximumEntryBytes = 64LL * 1024LL * 1024LL;
constexpr qint64 MaximumMetadataEntryBytes = 1024LL * 1024LL;
constexpr float CacheWebpQuality = 82.0F;
constexpr auto DerivedDecoderSchema =
    "decoded-pixels-v1/webp-v1/display-colorspace-v1";
constexpr auto MetadataSchema = "image-metadata-v3";

enum class Artifact : quint8 {
    Invalid = 0,
    Thumbnail = 1,
    ViewerFit = 2,
    Native = 3,
    VideoPoster = 4,
};

struct DerivedKey {
    QString sourceKey;
    QString contentVersion;
    // Empty for durable strong/local-stat revisions. Weak and session
    // revisions include the exact broker resource authority and are routed
    // to the process-session cache instead of the persistent cache root.
    QString authorityResourceId;
    qint64 sourceSize = -1;
    Artifact artifact = Artifact::Invalid;
    QSize targetTier;
    QString transformSchema;
    QString decoderSchema;

    bool isValid() const {
        return !sourceKey.isEmpty() && !contentVersion.isEmpty() &&
            artifact != Artifact::Invalid && artifact != Artifact::Native &&
            targetTier.isValid() && targetTier.width() > 0 &&
            targetTier.height() > 0 && !transformSchema.isEmpty() &&
            !decoderSchema.isEmpty();
    }
};

struct MetadataKey {
    QString sourceKey;
    QString contentVersion;
    QString authorityResourceId;
    qint64 sourceSize = -1;
    QString schema;

    bool isValid() const {
        return !sourceKey.isEmpty() && !contentVersion.isEmpty() &&
            !schema.isEmpty();
    }
};

QDataStream &operator<<(QDataStream &stream, const DerivedKey &key);
QDataStream &operator>>(QDataStream &stream, DerivedKey &key);
QDataStream &operator<<(QDataStream &stream, const MetadataKey &key);
QDataStream &operator>>(QDataStream &stream, MetadataKey &key);

QMutex cacheMutex;
std::atomic<quint64> cacheWriteGeneration{0};
QMutex legacyThumbnailMutex;
QString legacyThumbnailDirectory;
struct LegacyThumbnail {
    QString path;
    int longEdge = 0;
};
QHash<QString, LegacyThumbnail> legacyThumbnails;
QMutex metadataMemoryMutex;
QCache<QByteArray, ImageInfo> metadataMemory(16 * 1024 * 1024);
QMutex lookupRegistryMutex;
QHash<QString, QWeakPointer<PersistentDerivedLookupGate>> lookupRegistry;
qint64 knownPersistentDiskSize = -1;
qint64 knownSessionDiskSize = -1;

QString normalizedStrength(QString strength) {
    strength = strength.trimmed().toLower();
    strength.remove(QLatin1Char('-'));
    strength.remove(QLatin1Char('_'));
    return strength;
}

bool hasPersistentVersionStrength(const ImageDecodeRequest &request) {
    const QString strength = normalizedStrength(
        request.info.source.versionStrength);
    return strength == QStringLiteral("strong") ||
        strength == QStringLiteral("localstat");
}

bool hasPersistentVersionStrength(const ImageInfo &info) {
    const QString strength = normalizedStrength(info.source.versionStrength);
    return strength == QStringLiteral("strong") ||
        strength == QStringLiteral("localstat");
}

bool hasSessionVersionStrength(const ImageDecodeRequest &request) {
    const QString strength = normalizedStrength(
        request.info.source.versionStrength);
    return strength == QStringLiteral("weak") ||
        strength == QStringLiteral("weakremote") ||
        strength == QStringLiteral("session");
}

bool hasSessionVersionStrength(const ImageInfo &info) {
    const QString strength = normalizedStrength(info.source.versionStrength);
    return strength == QStringLiteral("weak") ||
        strength == QStringLiteral("weakremote") ||
        strength == QStringLiteral("session");
}

Artifact artifactForRequest(const ImageDecodeRequest &request) {
    if (request.videoPosterRequest) {
        return Artifact::VideoPoster;
    }
    if (request.viewerRequest) {
        return request.fitToViewerRequest
            ? Artifact::ViewerFit : Artifact::Native;
    }
    return Artifact::Thumbnail;
}

QString versionForRequest(const ImageDecodeRequest &request) {
    const QString descriptorVersion =
        request.info.source.contentVersion;
    const QString requestVersion = request.info.sourceVersionToken;
    if (!descriptorVersion.isEmpty() && !requestVersion.isEmpty() &&
        descriptorVersion != requestVersion) {
        return {};
    }
    return !descriptorVersion.isEmpty()
        ? descriptorVersion : requestVersion;
}

QString transformForRequest(const ImageDecodeRequest &request,
                            Artifact artifact) {
    if (artifact == Artifact::VideoPoster) {
        return QStringLiteral("video-first-frame-poster-1024-display-v5");
    }
    if (artifact == Artifact::ViewerFit) {
        // v4 invalidates full-size Fit entries produced before native JPEG
        // decoding switched from the approximate IDCT. Those pixels may be
        // stored in lossless WebP, but lossless storage cannot repair the
        // already-approximated decoder output.
        return QStringLiteral(
            "viewer-fit-bc-pyramid-v4-linear-half-dither-lossless");
    }
    if (artifact == Artifact::Thumbnail) {
        const QString transform =
            request.thumbnailTransformKey.trimmed();
        return transform.isEmpty()
            ? QStringLiteral("thumbnail-stretch-v1") : transform;
    }
    return {};
}

DerivedKey keyForRequest(const ImageDecodeRequest &request) {
    const Artifact artifact = artifactForRequest(request);
    const bool localPoster = request.videoPosterRequest
        && !request.info.source.isValid();
    const QString localVersion = !request.info.sourceVersionToken.isEmpty()
        ? request.info.sourceVersionToken
        : QStringLiteral("local-stat:%1:%2")
            .arg(request.info.fileSize)
            .arg(request.info.lastModified.isValid()
                     ? QString::number(
                           request.info.lastModified.toMSecsSinceEpoch())
                     : QStringLiteral("unknown"));
    return {
        .sourceKey = localPoster
            ? QDir::cleanPath(QFileInfo(request.info.path).absoluteFilePath())
            : request.info.source.sourceKey,
        .contentVersion = localPoster
            ? localVersion : versionForRequest(request),
        .authorityResourceId = !localPoster
                && hasSessionVersionStrength(request)
            ? request.info.source.resourceId : QString{},
        .sourceSize = localPoster
            ? request.info.fileSize : request.info.source.size,
        .artifact = artifact,
        .targetTier = request.targetSize,
        .transformSchema = transformForRequest(request, artifact),
        .decoderSchema = QString::fromLatin1(DerivedDecoderSchema),
    };
}

QString versionForInfo(const ImageInfo &info) {
    const QString descriptorVersion = info.source.contentVersion;
    const QString requestVersion = info.sourceVersionToken;
    if (!descriptorVersion.isEmpty() && !requestVersion.isEmpty() &&
        descriptorVersion != requestVersion) {
        return {};
    }
    return !descriptorVersion.isEmpty()
        ? descriptorVersion : requestVersion;
}

MetadataKey metadataKeyForInfo(const ImageInfo &info) {
    return {
        .sourceKey = info.source.sourceKey,
        .contentVersion = versionForInfo(info),
        .authorityResourceId = hasSessionVersionStrength(info)
            ? info.source.resourceId : QString{},
        .sourceSize = info.source.size,
        .schema = isVideoPreviewFormat(info.formatHint())
            ? QStringLiteral("video-metadata-v2") : QString::fromLatin1(MetadataSchema),
    };
}

QByteArray metadataMemoryKey(const ImageInfo &info) {
    if (!info.source.isValid()
        || (!hasPersistentVersionStrength(info) && !hasSessionVersionStrength(info))) return {};
    const auto key = metadataKeyForInfo(info);
    if (!key.isValid()) return {};
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    // StorageLocations freezes this choice after first use. Resolving the OS
    // temp/cache path for every hit can itself perform expensive OS queries.
    static const QString storageRoot = ZoinGallery::StorageLocations::cacheRoot();
    stream << storageRoot << key;
    return bytes;
}

void rememberMetadata(const ImageInfo &info, qsizetype serializedBytes) {
    const auto key = metadataMemoryKey(info);
    if (key.isEmpty()) return;
    // Keep derived values only; the caller supplies its current authority.
    auto *stored = new ImageInfo;
    stored->imageSize = info.imageSize;
    stored->orientation = info.orientation;
    stored->exif = info.exif;
    stored->typedFileFields = info.typedFileFields;
    stored->fileFieldsRead = info.fileFieldsRead;
    QMutexLocker locker(&metadataMemoryMutex);
    metadataMemory.insert(key, stored,
        static_cast<int>(serializedBytes + key.size() + sizeof(ImageInfo)));
}

QByteArray serializedKey(const DerivedKey &key) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << key.sourceKey << key.contentVersion
           << key.authorityResourceId << key.sourceSize
           << static_cast<quint8>(key.artifact) << key.targetTier
           << key.transformSchema << key.decoderSchema;
    if (stream.status() != QDataStream::Ok) {
        return {};
    }
    return result;
}

QString cacheDirectoryPath() {
    return QDir(ZoinGallery::StorageLocations::cacheRoot())
        .filePath(QStringLiteral("zg_derived_v1"));
}

QString sessionCacheDirectoryPath() {
    static QTemporaryDir directory(
        QDir(QDir::tempPath()).filePath(
            QStringLiteral("zoin-gallery-derived-XXXXXX")));
    return directory.isValid() ? directory.path() : QString{};
}

QString cacheDirectoryPath(const DerivedKey &key) {
    return key.authorityResourceId.isEmpty()
        ? cacheDirectoryPath() : sessionCacheDirectoryPath();
}

QString cacheFilePath(const DerivedKey &key) {
    const QByteArray bytes = serializedKey(key);
    if (bytes.isEmpty()) {
        return {};
    }
    const QString name = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString directory = cacheDirectoryPath(key);
    return directory.isEmpty()
        ? QString{}
        : QDir(directory).filePath(name + QStringLiteral(".zgd"));
}

QString reusableThumbnailPath(DerivedKey key) {
    if (key.artifact != Artifact::Thumbnail) {
        return {};
    }
    // A zero target reserves a filename for the reusable frame. Its header
    // still records the real request size and the actual encoded dimensions.
    key.targetTier = QSize(0, 0);
    return cacheFilePath(key);
}

QByteArray serializedMetadataKey(const MetadataKey &key) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << key.sourceKey << key.contentVersion
           << key.authorityResourceId << key.sourceSize << key.schema;
    return stream.status() == QDataStream::Ok ? result : QByteArray{};
}

QString metadataCacheFilePath(const MetadataKey &key) {
    const QByteArray bytes = serializedMetadataKey(key);
    if (bytes.isEmpty()) {
        return {};
    }
    const QString name = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString directory = key.authorityResourceId.isEmpty()
        ? cacheDirectoryPath() : sessionCacheDirectoryPath();
    return directory.isEmpty()
        ? QString{}
        : QDir(directory).filePath(name + QStringLiteral(".zgm"));
}

QString lookupKey(const ImageDecodeRequest &request) {
    const DerivedKey key = keyForRequest(request);
    if (key.isValid()) {
        const QString path = cacheFilePath(key);
        // Navigation cancels lookup and read stages together for one viewer.
        // A different viewer must wait on its own lookup, not a canceled one.
        return request.viewerRequest && !request.requestNamespace.isEmpty()
            ? path + QChar(0x1f) + request.requestNamespace : path;
    }
    // Local files still use the legacy path/stat cache.  Video requests for
    // those files must join the same cache-before-source gate as derived
    // external sources; otherwise the legacy cache runner can hit while a
    // second VideoThumbnailRunner starts QMediaPlayer in parallel.  The
    // Source validation is deliberately limited to path + mtime + size,
    // which are the exact fields PersistentImageCache validates on retrieval.
    // The output shape is part of the gate identity so a panel tile and a
    // viewer request for the same source cannot consume each other's result.
    if (request.info.source.isValid() || request.info.path.isEmpty()) {
        return {};
    }
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << QStringLiteral("legacy-video-cache-lookup-v2")
           << request.info.path << request.info.lastModified
           << request.info.fileSize << request.info.thumbnailKind
           << request.targetSize << request.viewerRequest
           << request.fitToViewerRequest << request.expandToCacheResolution
           << request.thumbnailTransformKey;
    if (stream.status() != QDataStream::Ok) {
        return {};
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool sameArtifactRevision(const DerivedKey &left, const DerivedKey &right) {
    return left.sourceKey == right.sourceKey &&
        left.contentVersion == right.contentVersion &&
        left.authorityResourceId == right.authorityResourceId &&
        left.sourceSize == right.sourceSize &&
        left.artifact == right.artifact &&
        left.transformSchema == right.transformSchema &&
        left.decoderSchema == right.decoderSchema;
}

bool keysEqual(const DerivedKey &left, const DerivedKey &right) {
    return sameArtifactRevision(left, right) && left.targetTier == right.targetTier;
}

bool servesThumbnailRequest(const DerivedKey &stored, const QSize &storedPixels,
                            const DerivedKey &request) {
    // An exact-size request can finish with fewer pixels, for example when
    // the source only contains a small embedded preview.
    const bool aspectPreserved =
        request.transformSchema == QStringLiteral("thumbnail-aspect-v1") ||
        request.transformSchema == QStringLiteral("video-contact-sheet-2x2-display-v6");
    return sameArtifactRevision(stored, request) &&
        (stored.targetTier == request.targetTier ||
         panelThumbnailCovers(storedPixels, request.targetTier, aspectPreserved));
}

bool metadataKeysEqual(const MetadataKey &left, const MetadataKey &right) {
    return left.sourceKey == right.sourceKey &&
        left.contentVersion == right.contentVersion &&
        left.authorityResourceId == right.authorityResourceId &&
        left.sourceSize == right.sourceSize &&
        left.schema == right.schema;
}

bool readImageHeader(QDataStream &stream, DerivedKey &key, QSize &storedPixelSize) {
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    stream >> magic >> version;
    if (magic != DerivedFileMagic || version != DerivedFileVersion) {
        return false;
    }
    stream >> key >> storedPixelSize;
    return stream.status() == QDataStream::Ok && key.isValid() &&
        key.decoderSchema == QString::fromLatin1(DerivedDecoderSchema) &&
        !storedPixelSize.isEmpty();
}

bool parsePreparedEntry(const QByteArray &entry, DerivedKey &key,
                        QSize &storedPixelSize, QByteArray &encodedImage) {
    if (entry.isEmpty() || entry.size() > MaximumEntryBytes) {
        return false;
    }
    QBuffer buffer;
    buffer.setData(entry);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return false;
    }
    QDataStream stream(&buffer);
    if (!readImageHeader(stream, key, storedPixelSize)) {
        return false;
    }
    stream >> encodedImage;
    return stream.status() == QDataStream::Ok &&
        !encodedImage.isEmpty() && encodedImage.size() <= MaximumEntryBytes;
}

bool readCachedImageHeader(const QString &path, DerivedKey &key, QSize &pixels) {
    if (path.isEmpty()) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumEntryBytes) {
        return false;
    }
    QDataStream stream(&file);
    return readImageHeader(stream, key, pixels);
}

QString legacyThumbnailPath(const DerivedKey &requested) {
    if (!requested.authorityResourceId.isEmpty()) return {};
    const QString directory = cacheDirectoryPath();
    QMutexLocker locker(&legacyThumbnailMutex);
    if (legacyThumbnailDirectory != directory) {
        ZoinGallery::MediaTimingTrace::Span span(
            QStringLiteral("qt.gallery.thumbnail_cache.legacy_index"));
        legacyThumbnails.clear();
        // Old builds hashed the requested dimensions into the filename. Read
        // just the headers once on a cache worker to recover the revision keys.
        const QDir cache(directory);
        const auto files = cache.entryList({QStringLiteral("*.zgd")},
                                          QDir::Files, QDir::NoSort);
        for (const QString &file : files) {
            const QString path = cache.filePath(file);
            DerivedKey stored;
            QSize pixels;
            if (!readCachedImageHeader(path, stored, pixels)
                || stored.artifact != Artifact::Thumbnail
                || path != cacheFilePath(stored)) continue;
            auto &candidate = legacyThumbnails[reusableThumbnailPath(stored)];
            const int longEdge = qMax(pixels.width(), pixels.height());
            if (longEdge > candidate.longEdge) candidate = {path, longEdge};
        }
        legacyThumbnailDirectory = directory;
        span.set(QStringLiteral("revisions"), legacyThumbnails.size());
    }
    return legacyThumbnails.value(reusableThumbnailPath(requested)).path;
}

// The caller holds cacheMutex. Compare actual pixels: a larger request may
// still have yielded a smaller embedded preview.
bool thumbnailSlotNeedsUpgrade(const QString &path, const DerivedKey &key,
                               const QSize &pixels) {
    DerivedKey previous;
    QSize previousPixels;
    if (!readCachedImageHeader(path, previous, previousPixels) ||
        !sameArtifactRevision(previous, key)) {
        return true;
    }
    const int oldEdge = qMax(previousPixels.width(), previousPixels.height());
    const int newEdge = qMax(pixels.width(), pixels.height());
    if (oldEdge > MaximumPanelThumbnailEdge) {
        // An oversized entry from an older build can be normalized in place.
        return newEdge == MaximumPanelThumbnailEdge;
    }
    return newEdge > oldEdge;
}

bool parseMetadataEntry(const QByteArray &entry, MetadataKey &key,
                        QSize &imageSize, ExifOrientation &orientation,
                        QVariantMap &exif, QVariantMap &typedFileFields,
                        bool &fileFieldsRead) {
    if (entry.isEmpty() || entry.size() > MaximumMetadataEntryBytes) {
        return false;
    }
    QBuffer buffer;
    buffer.setData(entry);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return false;
    }
    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    qint32 orientationValue = 0;
    stream >> magic >> version >> key >> imageSize >> orientationValue
           >> exif >> typedFileFields >> fileFieldsRead;
    if (stream.status() != QDataStream::Ok ||
        magic != MetadataFileMagic || version != MetadataFileVersion ||
        !key.isValid() ||
        (key.schema != QString::fromLatin1(MetadataSchema) && key.schema != QStringLiteral("video-metadata-v2")) ||
        !imageSize.isValid() || imageSize.width() <= 0 ||
        imageSize.height() <= 0 ||
        orientationValue < ExifOrientation::Horizontal ||
        orientationValue > ExifOrientation::Rotate270CW) {
        return false;
    }
    orientation = static_cast<ExifOrientation>(orientationValue);
    return true;
}

QDataStream &operator<<(QDataStream &stream, const DerivedKey &key) {
    stream << key.sourceKey << key.contentVersion
           << key.authorityResourceId << key.sourceSize
           << static_cast<quint8>(key.artifact) << key.targetTier
           << key.transformSchema << key.decoderSchema;
    return stream;
}

QDataStream &operator>>(QDataStream &stream, DerivedKey &key) {
    quint8 artifact = 0;
    stream >> key.sourceKey >> key.contentVersion
           >> key.authorityResourceId >> key.sourceSize >> artifact
           >> key.targetTier >> key.transformSchema >> key.decoderSchema;
    key.artifact = static_cast<Artifact>(artifact);
    return stream;
}

QDataStream &operator<<(QDataStream &stream, const MetadataKey &key) {
    stream << key.sourceKey << key.contentVersion
           << key.authorityResourceId << key.sourceSize << key.schema;
    return stream;
}

QDataStream &operator>>(QDataStream &stream, MetadataKey &key) {
    stream >> key.sourceKey >> key.contentVersion
           >> key.authorityResourceId >> key.sourceSize >> key.schema;
    return stream;
}

void touchFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    file.setFileTime(QDateTime::currentDateTimeUtc(),
                     QFileDevice::FileModificationTime);
}

void pruneExpiredLookupKeysLocked() {
    if (lookupRegistry.size() < 1024) {
        return;
    }
    for (auto it = lookupRegistry.begin(); it != lookupRegistry.end();) {
        if (it.value().isNull()) {
            it = lookupRegistry.erase(it);
        }
        else {
            ++it;
        }
    }
}

qint64 scanDiskSizeLocked(const QString &path) {
    qint64 size = 0;
    if (path.isEmpty()) {
        return 0;
    }
    const QFileInfoList files = QDir(path).entryInfoList(
        {QStringLiteral("*.zgd"), QStringLiteral("*.zgm")}, QDir::Files);
    for (const QFileInfo &file : files) {
        if (file.size() <= 0) {
            continue;
        }
        if (size > std::numeric_limits<qint64>::max() - file.size()) {
            return std::numeric_limits<qint64>::max();
        }
        size += file.size();
    }
    return size;
}

void accountCommittedFileLocked(qint64 &knownSize, qint64 previousSize,
                                qint64 currentSize) {
    if (knownSize < 0) {
        return;
    }
    knownSize = qMax<qint64>(0, knownSize -
        qMax<qint64>(0, previousSize));
    const qint64 added = qMax<qint64>(0, currentSize);
    if (knownSize > std::numeric_limits<qint64>::max() - added) {
        knownSize = std::numeric_limits<qint64>::max();
    }
    else {
        knownSize += added;
    }
}

void pruneDiskCacheLocked(const QString &path, qint64 &knownSize) {
    if (path.isEmpty()) {
        return;
    }
    if (knownSize < 0) {
        knownSize = scanDiskSizeLocked(path);
    }
    if (knownPersistentDiskSize < 0) knownPersistentDiskSize = scanDiskSizeLocked(cacheDirectoryPath());
    if (knownSessionDiskSize < 0) knownSessionDiskSize = scanDiskSizeLocked(sessionCacheDirectoryPath());
    if (knownPersistentDiskSize + knownSessionDiskSize <= derivedCacheBudget) {
        return;
    }
    QDir directory(cacheDirectoryPath());
    QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.zgd"), QStringLiteral("*.zgm")}, QDir::Files);
    files.append(QDir(sessionCacheDirectoryPath()).entryInfoList(
        {QStringLiteral("*.zgd"), QStringLiteral("*.zgm")}, QDir::Files));
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &left, const QFileInfo &right) {
                  return left.lastModified() < right.lastModified();
              });
    for (const QFileInfo &file : files) {
        if (knownPersistentDiskSize + knownSessionDiskSize <= derivedCacheBudget * 9 / 10) {
            break;
        }
        const qint64 fileSize = file.size();
        if (QFile::remove(file.absoluteFilePath())) {
            auto &size = file.absolutePath() == directory.absolutePath()
                ? knownPersistentDiskSize : knownSessionDiskSize;
            size = qMax<qint64>(0, size - qMax<qint64>(0, fileSize));
        }
    }
}

qint64 &knownSizeForKey(const DerivedKey &key) {
    return key.authorityResourceId.isEmpty()
        ? knownPersistentDiskSize : knownSessionDiskSize;
}

void invalidateKnownSizeForKey(const DerivedKey &key) {
    knownSizeForKey(key) = -1;
}

void removeIfUnchanged(const QString &path, const QByteArray &observed,
                       const DerivedKey &key, bool reusable = false) {
    QLockFile entryLock(path + QStringLiteral(".lock"));
    if (reusable && !entryLock.tryLock(0)) {
        return;
    }
    QMutexLocker locker(&cacheMutex);
    QFile current(path);
    if (!current.open(QIODevice::ReadOnly) ||
        current.size() != observed.size() || current.readAll() != observed) {
        return;
    }
    current.close();
    if (QFile::remove(path)) {
        invalidateKnownSizeForKey(key);
    }
}

bool writePreparedEntry(const DerivedKey &key, const QSize &pixels,
                        const QByteArray &entry);

QByteArray encodePreparedImage(const DerivedKey &key, const QImage &image) {
    // Keep the final viewer Fit pixels (including quantization dither).
    const bool lossless = key.artifact == Artifact::ViewerFit;
    const QByteArray encoded = WebpCodec::encode(image, CacheWebpQuality, lossless);
    if (encoded.isEmpty()) {
        return {};
    }
    QByteArray entry;
    QDataStream stream(&entry, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << DerivedFileMagic << DerivedFileVersion
           << key << image.size() << encoded;
    return stream.status() == QDataStream::Ok ? entry : QByteArray{};
}

QImage readCachedImage(const QString &path, const DerivedKey &requested,
                       bool reusable) {
    if (path.isEmpty()) {
        return {};
    }
    QByteArray entry;
    {
        // Release the cache mutex before WebP decode and color conversion.
        QMutexLocker locker(&cacheMutex);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        const qint64 size = file.size();
        if (size <= 0 || size > MaximumEntryBytes) {
            return {};
        }
        entry = file.readAll();
        if (entry.size() != size) {
            return {};
        }
    }

    DerivedKey stored;
    QSize pixels;
    QByteArray encoded;
    if (!parsePreparedEntry(entry, stored, pixels, encoded) ||
        !(reusable ? sameArtifactRevision(stored, requested) : keysEqual(stored, requested))) {
        removeIfUnchanged(path, entry, requested, reusable);
        return {};
    }
    if (reusable && !servesThumbnailRequest(stored, pixels, requested)) {
        return {};
    }
    QImage image = WebpCodec::decode(encoded);
    if (image.isNull() || image.size() != pixels) {
        removeIfUnchanged(path, entry, requested, reusable);
        return {};
    }
    image.setColorSpace(DisplayColorSpace::cacheColorSpace());
    if (stored.artifact == Artifact::Thumbnail) {
        const QSize bounded = boundedPanelThumbnailSize(pixels);
        if (bounded != pixels) {
            image = image.scaled(bounded, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            // Normalize the old reusable slot only when it is used. Legacy
            // exact keys stay read-only and expire under the normal budget.
            if (reusable && !image.isNull()) {
                const QByteArray normalized = encodePreparedImage(stored, image);
                if (!normalized.isEmpty()) {
                    writePreparedEntry(stored, bounded, normalized);
                }
            }
        }
    }
    touchFile(path);
    return DisplayColorSpace::convertImage(image);
}

bool writePreparedEntryLocked(const DerivedKey &key, const QString &path,
                              const QByteArray &entry) {
    const QString directoryPath = cacheDirectoryPath(key);
    if (directoryPath.isEmpty() || path.isEmpty()) {
        return false;
    }

    if (!QDir().mkpath(directoryPath)) {
        return false;
    }
    const qint64 previousSize = QFileInfo(path).size();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(entry) != entry.size() || !file.commit()) {
        file.cancelWriting();
        return false;
    }
    qint64 &knownSize = knownSizeForKey(key);
    accountCommittedFileLocked(knownSize, previousSize,
                               QFileInfo(path).size());
    pruneDiskCacheLocked(directoryPath, knownSize);
    return true;
}

bool writePreparedEntry(const DerivedKey &key, const QSize &pixels,
                        const QByteArray &entry) {
    const QString reusablePath = reusableThumbnailPath(key);
    if (key.artifact == Artifact::Thumbnail) {
        if (reusablePath.isEmpty() || pixels != boundedPanelThumbnailSize(pixels) ||
            !QDir().mkpath(cacheDirectoryPath(key))) {
            return false;
        }
        {
            // Serialize upgrades between processes. If the slot stays busy,
            // skip the write; a different filename would break the one-slot policy.
            QLockFile entryLock(reusablePath + QStringLiteral(".lock"));
            if (!entryLock.tryLock(100)) {
                return false;
            }
            QMutexLocker locker(&cacheMutex);
            if (!thumbnailSlotNeedsUpgrade(reusablePath, key, pixels)) {
                return true;
            }
            if (!writePreparedEntryLocked(key, reusablePath, entry)) {
                return false;
            }
        }
        if (ZoinGallery::MediaTimingTrace::enabled()) {
            ZoinGallery::MediaTimingTrace::event(
                QStringLiteral("qt.gallery.thumbnail_cache.update"),
                {{QStringLiteral("fix"), QStringLiteral("[FIX:thumbnail-cache-cap]")},
                 {QStringLiteral("width"), pixels.width()},
                 {QStringLiteral("height"), pixels.height()}});
        }
        return true;
    }

    QMutexLocker locker(&cacheMutex);
    return writePreparedEntryLocked(key, cacheFilePath(key), entry);
}

qint64 &knownSizeForMetadataKey(const MetadataKey &key) {
    return key.authorityResourceId.isEmpty()
        ? knownPersistentDiskSize : knownSessionDiskSize;
}

void removeMetadataIfUnchanged(const QString &path,
                               const QByteArray &observed,
                               const MetadataKey &key) {
    QMutexLocker locker(&cacheMutex);
    QFile current(path);
    if (!current.open(QIODevice::ReadOnly) ||
        current.size() != observed.size() ||
        current.readAll() != observed) {
        return;
    }
    current.close();
    if (QFile::remove(path)) {
        knownSizeForMetadataKey(key) = -1;
    }
}

bool writeMetadataEntry(const MetadataKey &key, const QByteArray &entry) {
    const QString directoryPath = key.authorityResourceId.isEmpty()
        ? cacheDirectoryPath() : sessionCacheDirectoryPath();
    const QString path = metadataCacheFilePath(key);
    if (directoryPath.isEmpty() || path.isEmpty() || entry.isEmpty() ||
        entry.size() > MaximumMetadataEntryBytes) {
        return false;
    }

    QMutexLocker locker(&cacheMutex);
    if (!QDir().mkpath(directoryPath)) {
        return false;
    }
    const qint64 previousSize = QFileInfo(path).size();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(entry) != entry.size() || !file.commit()) {
        file.cancelWriting();
        return false;
    }
    qint64 &knownSize = knownSizeForMetadataKey(key);
    accountCommittedFileLocked(knownSize, previousSize,
                               QFileInfo(path).size());
    pruneDiskCacheLocked(directoryPath, knownSize);
    return true;
}

} // namespace

bool PersistentDerivedImageCache::appliesTo(
    const ImageDecodeRequest &request) {
    return request.info.source.isValid() || request.videoPosterRequest;
}

bool PersistentDerivedImageCache::isEligible(
    const ImageDecodeRequest &request) {
    if (request.videoPosterRequest && !request.info.source.isValid()) {
        if (request.info.path.isEmpty() || !request.info.lastModified.isValid()
            || request.info.fileSize < 0) {
            return false;
        }
        const QFileInfo file(request.info.path);
        return file.isFile() && file.lastModified() == request.info.lastModified
            && file.size() == request.info.fileSize
            && keyForRequest(request).isValid();
    }
    if (!appliesTo(request) ||
        (!hasPersistentVersionStrength(request) &&
         !hasSessionVersionStrength(request))) {
        return false;
    }
    return keyForRequest(request).isValid();
}

bool PersistentDerivedImageCache::hasImage(
    const ImageDecodeRequest &request) {
    if (!isEligible(request)) {
        return false;
    }
    const auto key = keyForRequest(request);
    QMutexLocker locker(&cacheMutex);
    DerivedKey stored;
    QSize pixels;
    if (readCachedImageHeader(reusableThumbnailPath(key), stored, pixels)
        && servesThumbnailRequest(stored, pixels, key)) {
        return true;
    }
    const QFileInfo file(cacheFilePath(key));
    return file.isFile() && file.size() > 0 &&
        file.size() <= MaximumEntryBytes;
}

QImage PersistentDerivedImageCache::retrieveImage(
    const ImageDecodeRequest &request) {
    if (!isEligible(request)) {
        return {};
    }
    const auto key = keyForRequest(request);
    QImage image = readCachedImage(reusableThumbnailPath(key), key, true);
    if (!image.isNull()) return image;
    if (request.viewerThumbnailRequest && key.artifact == Artifact::Thumbnail) {
        return readCachedImage(legacyThumbnailPath(key), key, true);
    }
    return readCachedImage(cacheFilePath(key), key, false);
}

void PersistentDerivedImageCache::storeImage(
    const ImageDecodeRequest &request, const QByteArray &preparedEntry) {
    if (!isEligible(request)) {
        return;
    }
    DerivedKey key;
    QSize storedPixelSize;
    QByteArray encodedImage;
    if (!parsePreparedEntry(preparedEntry, key, storedPixelSize,
                            encodedImage) ||
        !keysEqual(key, keyForRequest(request))) {
        return;
    }
    QImage decoded = WebpCodec::decode(encodedImage);
    if (decoded.isNull() || decoded.size() != storedPixelSize) {
        return;
    }
    writePreparedEntry(key, storedPixelSize, preparedEntry);
}

void PersistentDerivedImageCache::storePreparedImage(
    const ImageInfo &sourceInfo, const QByteArray &preparedEntry) {
    if (!sourceInfo.source.isValid() ||
        (!hasPersistentVersionStrength(sourceInfo) &&
         !hasSessionVersionStrength(sourceInfo))) {
        return;
    }
    DerivedKey key;
    QSize storedPixelSize;
    QByteArray encodedImage;
    if (!parsePreparedEntry(preparedEntry, key, storedPixelSize,
                            encodedImage)) {
        return;
    }
    const QString descriptorVersion = sourceInfo.source.contentVersion;
    const QString requestVersion = sourceInfo.sourceVersionToken;
    if ((descriptorVersion.isEmpty() && requestVersion.isEmpty()) ||
        key.sourceKey != sourceInfo.source.sourceKey ||
        key.sourceSize != sourceInfo.source.size ||
        key.authorityResourceId !=
            (hasSessionVersionStrength(sourceInfo)
                 ? sourceInfo.source.resourceId : QString{}) ||
        (!descriptorVersion.isEmpty() &&
         key.contentVersion != descriptorVersion) ||
        (!requestVersion.isEmpty() &&
         key.contentVersion != requestVersion)) {
        return;
    }
    QImage decoded = WebpCodec::decode(encodedImage);
    if (decoded.isNull() || decoded.size() != storedPixelSize) {
        return;
    }
    writePreparedEntry(key, storedPixelSize, preparedEntry);
}

QByteArray PersistentDerivedImageCache::createImageForCache(
    const ImageDecodeRequest &request, const QImage &image) {
    if (!isEligible(request) || image.isNull()) {
        return {};
    }
    const DerivedKey key = keyForRequest(request);
    QSize pixels = image.size();
    if (key.artifact == Artifact::Thumbnail) {
        pixels = boundedPanelThumbnailSize(pixels);
        const QString path = reusableThumbnailPath(key);
        QMutexLocker locker(&cacheMutex);
        if (path.isEmpty() || !thumbnailSlotNeedsUpgrade(path, key, pixels)) {
            return {};
        }
    }
    const QImage bounded = pixels == image.size() ? image
        : image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage converted = DisplayColorSpace::convertImageToColorSpace(
        bounded, DisplayColorSpace::cacheColorSpace());
    if (converted.isNull()) {
        return {};
    }
    return encodePreparedImage(key, converted);
}

quint64 PersistentDerivedImageCache::writeGeneration() {
    return cacheWriteGeneration.load();
}

void PersistentDerivedImageCache::storeViewerFitFrame(
    const ImageDecodeRequest &request, const QImage &image, quint64 generation) {
    if (generation != writeGeneration() || image.isNull() || !isEligible(request)) return;
    const DerivedKey key = keyForRequest(request);
    if (key.artifact != Artifact::ViewerFit || hasImage(request)) return;
    ZoinGallery::MediaTimingTrace::Span span(
        QStringLiteral("qt.gallery.viewer_fit_cache.write"),
        ZoinGallery::MediaTimingTrace::sourceFields(request.info.source));
    const QByteArray entry = createImageForCache(request, image);
    QMutexLocker locker(&cacheMutex);
    const bool stored = generation == writeGeneration() && !entry.isEmpty()
        && writePreparedEntryLocked(key, cacheFilePath(key), entry);
    span.set(QStringLiteral("ok"), stored);
}

bool PersistentDerivedImageCache::retrieveMetadata(ImageInfo &info) {
    QList<ImageInfo> hits;
    QList<ImageInfo> misses;
    retrieveMetadataBatch({info}, hits, misses);
    if (hits.size() != 1) {
        return false;
    }
    info = std::move(hits.front());
    return true;
}

bool PersistentDerivedImageCache::retrieveMemoryMetadata(ImageInfo &info) {
    const auto key = metadataMemoryKey(info);
    if (key.isEmpty()) return false;
    QMutexLocker locker(&metadataMemoryMutex);
    const auto *cached = metadataMemory.object(key);
    if (!cached) return false;
    info.imageSize = cached->imageSize;
    info.orientation = cached->orientation;
    info.exif = cached->exif;
    info.typedFileFields = cached->typedFileFields;
    info.fileFieldsRead = cached->fileFieldsRead;
    info.fileSize = info.source.size;
    info.isCached = true;
    return true;
}

void PersistentDerivedImageCache::clearMemoryMetadata() {
    QMutexLocker locker(&metadataMemoryMutex);
    metadataMemory.clear();
}

void PersistentDerivedImageCache::retrieveMetadataBatch(
    const QList<ImageInfo> &candidates, QList<ImageInfo> &hits,
    QList<ImageInfo> &misses) {
    struct MetadataRead {
        ImageInfo info;
        MetadataKey key;
        QString path;
        QByteArray entry;
        bool eligible = false;
        bool memoryHit = false;
    };

    hits.clear();
    misses.clear();
    hits.reserve(candidates.size());
    misses.reserve(candidates.size());

    QList<MetadataRead> reads;
    reads.reserve(candidates.size());
    for (const ImageInfo &candidate : candidates) {
        MetadataRead read{.info = candidate};
        if (retrieveMemoryMetadata(read.info)) {
            read.memoryHit = true;
            reads.append(std::move(read));
            continue;
        }
        if (candidate.source.isValid() &&
            (hasPersistentVersionStrength(candidate) ||
             hasSessionVersionStrength(candidate))) {
            read.key = metadataKeyForInfo(candidate);
            read.eligible = read.key.isValid();
            if (read.eligible) {
                read.path = metadataCacheFilePath(read.key);
                read.eligible = !read.path.isEmpty();
            }
        }
        reads.append(std::move(read));
    }

    // The old per-runner path acquired this mutex, opened one manifest, then
    // opened it again merely to update its LRU timestamp. A catalog hit now
    // performs all short reads under one lock and touches each already-open
    // file before releasing it. Parsing remains outside the global lock.
    {
        QMutexLocker locker(&cacheMutex);
        const QDateTime touchedAt = QDateTime::currentDateTimeUtc();
        for (MetadataRead &read : reads) {
            if (!read.eligible) {
                continue;
            }
            QFile file(read.path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const qint64 expectedSize = file.size();
            if (expectedSize <= 0 ||
                expectedSize > MaximumMetadataEntryBytes) {
                continue;
            }
            read.entry = file.readAll();
            if (read.entry.size() != expectedSize) {
                read.entry.clear();
                continue;
            }
            file.setFileTime(touchedAt,
                             QFileDevice::FileModificationTime);
        }
    }

    for (MetadataRead &read : reads) {
        if (read.memoryHit) {
            hits.append(std::move(read.info));
            continue;
        }
        if (!read.eligible || read.entry.isEmpty()) {
            misses.append(std::move(read.info));
            continue;
        }

        MetadataKey storedKey;
        QSize imageSize;
        ExifOrientation orientation = ExifOrientation::Horizontal;
        QVariantMap exif;
        QVariantMap typedFileFields;
        bool fileFieldsRead = false;
        if (!parseMetadataEntry(read.entry, storedKey, imageSize,
                                orientation, exif, typedFileFields,
                                fileFieldsRead) ||
            !metadataKeysEqual(read.key, storedKey)) {
            removeMetadataIfUnchanged(read.path, read.entry, read.key);
            misses.append(std::move(read.info));
            continue;
        }
        read.info.imageSize = imageSize;
        read.info.orientation = orientation;
        read.info.exif = std::move(exif);
        read.info.typedFileFields = std::move(typedFileFields);
        read.info.fileFieldsRead = fileFieldsRead;
        read.info.fileSize = read.info.source.size;
        read.info.isCached = true;
        rememberMetadata(read.info, read.entry.size());
        hits.append(std::move(read.info));
    }
}

void PersistentDerivedImageCache::storeMetadata(const ImageInfo &info) {
    if (!info.source.isValid() || !info.imageSize.isValid() ||
        info.imageSize.width() <= 0 || info.imageSize.height() <= 0 ||
        info.orientation < ExifOrientation::Horizontal ||
        info.orientation > ExifOrientation::Rotate270CW ||
        (!hasPersistentVersionStrength(info) &&
         !hasSessionVersionStrength(info))) {
        return;
    }
    const MetadataKey key = metadataKeyForInfo(info);
    if (!key.isValid()) {
        return;
    }
    QByteArray entry;
    QDataStream stream(&entry, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << MetadataFileMagic << MetadataFileVersion << key
           << info.imageSize << static_cast<qint32>(info.orientation)
           << info.exif << info.typedFileFields << info.fileFieldsRead;
    if (stream.status() != QDataStream::Ok ||
        entry.size() > MaximumMetadataEntryBytes) {
        return;
    }
    rememberMetadata(info, entry.size());
    writeMetadataEntry(key, entry);
}

qint64 PersistentDerivedImageCache::byteBudget() {
    QMutexLocker locker(&cacheMutex);
    return derivedCacheBudget;
}

void PersistentDerivedImageCache::setByteBudget(qint64 bytes) {
    QMutexLocker locker(&cacheMutex);
    derivedCacheBudget = qBound<qint64>(64LL * 1024 * 1024, bytes, 64LL * 1024 * 1024 * 1024);
    pruneDiskCacheLocked(cacheDirectoryPath(), knownPersistentDiskSize);
}

qint64 PersistentDerivedImageCache::cacheSize() {
    const qint64 persistent = persistentCacheSize();
    const qint64 session = sessionCacheSize();
    if (persistent > std::numeric_limits<qint64>::max() - session) {
        return std::numeric_limits<qint64>::max();
    }
    return persistent + session;
}

qint64 PersistentDerivedImageCache::persistentCacheSize() {
    QMutexLocker locker(&cacheMutex);
    knownPersistentDiskSize = scanDiskSizeLocked(cacheDirectoryPath());
    return knownPersistentDiskSize;
}

qint64 PersistentDerivedImageCache::sessionCacheSize() {
    QMutexLocker locker(&cacheMutex);
    knownSessionDiskSize = scanDiskSizeLocked(
        sessionCacheDirectoryPath());
    return knownSessionDiskSize;
}

void PersistentDerivedImageCache::clear() {
    QMutexLocker locker(&cacheMutex);
    ++cacheWriteGeneration;
    clearMemoryMetadata();
    {
        QMutexLocker indexLocker(&legacyThumbnailMutex);
        legacyThumbnailDirectory.clear();
        legacyThumbnails.clear();
    }
    QDir directory(cacheDirectoryPath());
    if (directory.exists() && !directory.removeRecursively()) {
        knownPersistentDiskSize = -1;
    }
    else {
        knownPersistentDiskSize = 0;
    }
    QDir sessionDirectory(sessionCacheDirectoryPath());
    if (sessionDirectory.exists() &&
        !sessionDirectory.removeRecursively()) {
        knownSessionDiskSize = -1;
    }
    else {
        knownSessionDiskSize = 0;
    }
}

void PersistentDerivedImageCache::clearSession() {
    QMutexLocker locker(&cacheMutex);
    ++cacheWriteGeneration;
    clearMemoryMetadata();
    QDir directory(sessionCacheDirectoryPath());
    if (directory.exists() && !directory.removeRecursively()) {
        knownSessionDiskSize = -1;
        return;
    }
    knownSessionDiskSize = 0;
}

PersistentDerivedImageCache::LookupGate
PersistentDerivedImageCache::beginLookup(
    const ImageDecodeRequest &request) {
    if (!request.checkCache) {
        return {};
    }
    const QString key = lookupKey(request);
    if (key.isEmpty()) {
        return {};
    }
    QMutexLocker locker(&lookupRegistryMutex);
    pruneExpiredLookupKeysLocked();
    if (const LookupGate existing = lookupRegistry.value(key).toStrongRef()) {
        return existing;
    }
    const LookupGate gate = LookupGate::create();
    lookupRegistry.insert(key, gate.toWeakRef());
    return gate;
}

PersistentDerivedImageCache::LookupGate
PersistentDerivedImageCache::joinLookup(
    const ImageDecodeRequest &request) {
    if (!request.checkCache) {
        return {};
    }
    const QString key = lookupKey(request);
    if (key.isEmpty()) {
        return {};
    }
    QMutexLocker locker(&lookupRegistryMutex);
    return lookupRegistry.value(key).toStrongRef();
}

void PersistentDerivedImageCache::completeLookup(
    const LookupGate &gate, bool cacheHit) {
    if (!gate) {
        return;
    }
    QMutexLocker locker(&gate->mutex);
    gate->cacheHit = cacheHit;
    gate->completed = true;
    gate->completedCondition.wakeAll();
}

bool PersistentDerivedImageCache::waitForLookup(
    const LookupGate &gate,
    const QSharedPointer<ZoinGallery::ImageSourceCancellation>
        &cancellation) {
    if (!gate) {
        return false;
    }
    QMutexLocker locker(&gate->mutex);
    while (!gate->completed &&
           (!cancellation || !cancellation->isCanceled())) {
        gate->completedCondition.wait(&gate->mutex, 25);
    }
    return gate->completed && gate->cacheHit;
}
