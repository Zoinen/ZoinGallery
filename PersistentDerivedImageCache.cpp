#include "PersistentDerivedImageCache.h"

#include "Decoders/WebpCodec.h"
#include "DisplayColorSpace.h"
#include "StorageLocations.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr quint32 DerivedFileMagic = 0x5a474431; // "ZGD1"
constexpr quint16 DerivedFileVersion = 3;
constexpr quint32 MetadataFileMagic = 0x5a474d31; // "ZGM1"
constexpr quint16 MetadataFileVersion = 1;
constexpr qint64 DerivedCacheBudget = 512LL * 1024LL * 1024LL;
constexpr qint64 DerivedCachePruneTarget =
    DerivedCacheBudget * 9 / 10;
constexpr qint64 MaximumEntryBytes = 64LL * 1024LL * 1024LL;
constexpr qint64 MaximumMetadataEntryBytes = 1024LL * 1024LL;
constexpr float CacheWebpQuality = 82.0F;
constexpr auto DerivedDecoderSchema =
    "decoded-pixels-v1/webp-v1/display-colorspace-v1";
constexpr auto MetadataSchema = "image-metadata-v1";

enum class Artifact : quint8 {
    Invalid = 0,
    Thumbnail = 1,
    ViewerFit = 2,
    Native = 3,
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
    if (artifact == Artifact::ViewerFit) {
        return QStringLiteral("viewer-fit-v1");
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
    return {
        .sourceKey = request.info.source.sourceKey,
        .contentVersion = versionForRequest(request),
        .authorityResourceId = hasSessionVersionStrength(request)
            ? request.info.source.resourceId : QString{},
        .sourceSize = request.info.source.size,
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
        .schema = QString::fromLatin1(MetadataSchema),
    };
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
    if (!key.isValid()) {
        return {};
    }
    return cacheFilePath(key);
}

bool keysEqual(const DerivedKey &left, const DerivedKey &right) {
    return left.sourceKey == right.sourceKey &&
        left.contentVersion == right.contentVersion &&
        left.authorityResourceId == right.authorityResourceId &&
        left.sourceSize == right.sourceSize &&
        left.artifact == right.artifact &&
        left.targetTier == right.targetTier &&
        left.transformSchema == right.transformSchema &&
        left.decoderSchema == right.decoderSchema;
}

bool metadataKeysEqual(const MetadataKey &left, const MetadataKey &right) {
    return left.sourceKey == right.sourceKey &&
        left.contentVersion == right.contentVersion &&
        left.authorityResourceId == right.authorityResourceId &&
        left.sourceSize == right.sourceSize &&
        left.schema == right.schema;
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
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    stream >> magic >> version >> key >> storedPixelSize >> encodedImage;
    return stream.status() == QDataStream::Ok &&
        magic == DerivedFileMagic && version == DerivedFileVersion &&
        key.isValid() &&
        key.decoderSchema == QString::fromLatin1(DerivedDecoderSchema) &&
        storedPixelSize.isValid() && !encodedImage.isEmpty() &&
        encodedImage.size() <= MaximumEntryBytes;
}

bool parseMetadataEntry(const QByteArray &entry, MetadataKey &key,
                        QSize &imageSize, ExifOrientation &orientation,
                        QVariantMap &exif) {
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
    stream >> magic >> version >> key >> imageSize >> orientationValue >> exif;
    if (stream.status() != QDataStream::Ok ||
        magic != MetadataFileMagic || version != MetadataFileVersion ||
        !key.isValid() ||
        key.schema != QString::fromLatin1(MetadataSchema) ||
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
    if (knownSize <= DerivedCacheBudget) {
        return;
    }
    QDir directory(path);
    QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.zgd"), QStringLiteral("*.zgm")}, QDir::Files);
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &left, const QFileInfo &right) {
                  return left.lastModified() < right.lastModified();
              });
    for (const QFileInfo &file : files) {
        if (knownSize <= DerivedCachePruneTarget) {
            break;
        }
        const qint64 fileSize = file.size();
        if (QFile::remove(file.absoluteFilePath())) {
            knownSize = qMax<qint64>(
                0, knownSize - qMax<qint64>(0, fileSize));
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
                       const DerivedKey &key) {
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

bool writePreparedEntry(const DerivedKey &key, const QByteArray &entry) {
    const QString directoryPath = cacheDirectoryPath(key);
    const QString path = cacheFilePath(key);
    if (directoryPath.isEmpty() || path.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&cacheMutex);
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
    return request.info.source.isValid();
}

bool PersistentDerivedImageCache::isEligible(
    const ImageDecodeRequest &request) {
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
    QMutexLocker locker(&cacheMutex);
    const QFileInfo file(cacheFilePath(keyForRequest(request)));
    return file.isFile() && file.size() > 0 &&
        file.size() <= MaximumEntryBytes;
}

QImage PersistentDerivedImageCache::retrieveImage(
    const ImageDecodeRequest &request) {
    if (!isEligible(request)) {
        return {};
    }
    const DerivedKey requestedKey = keyForRequest(request);
    const QString path = cacheFilePath(requestedKey);

    QByteArray entry;
    {
        // Serialize filesystem mutation and the short read only. WebP decode
        // and colorspace conversion are deliberately outside this global
        // critical section so unrelated cache hits can progress in parallel.
        QMutexLocker locker(&cacheMutex);
        const QFileInfo fileInfo(path);
        if (!fileInfo.isFile() || fileInfo.size() <= 0 ||
            fileInfo.size() > MaximumEntryBytes) {
            return {};
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        entry = file.readAll();
        if (entry.size() != fileInfo.size()) {
            return {};
        }
    }

    DerivedKey storedKey;
    QSize storedPixelSize;
    QByteArray encodedImage;
    if (!parsePreparedEntry(entry, storedKey, storedPixelSize,
                            encodedImage) ||
        !keysEqual(requestedKey, storedKey) ||
        !storedPixelSize.isValid() || encodedImage.isEmpty() ||
        encodedImage.size() > MaximumEntryBytes) {
        removeIfUnchanged(path, entry, requestedKey);
        return {};
    }

    QImage result = WebpCodec::decode(encodedImage);
    if (result.isNull() || result.size() != storedPixelSize) {
        removeIfUnchanged(path, entry, requestedKey);
        return {};
    }
    result.setColorSpace(DisplayColorSpace::cacheColorSpace());
    result = DisplayColorSpace::convertImage(result);
    touchFile(path);
    return result;
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
    const QString path = cacheFilePath(key);
    if (path.isEmpty()) {
        return;
    }

    QImage decoded = WebpCodec::decode(encodedImage);
    if (decoded.isNull() || decoded.size() != storedPixelSize) {
        return;
    }
    writePreparedEntry(key, preparedEntry);
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
    const QString path = cacheFilePath(key);
    if (path.isEmpty()) {
        return;
    }
    QImage decoded = WebpCodec::decode(encodedImage);
    if (decoded.isNull() || decoded.size() != storedPixelSize) {
        return;
    }
    writePreparedEntry(key, preparedEntry);
}

QByteArray PersistentDerivedImageCache::createImageForCache(
    const ImageDecodeRequest &request, const QImage &image) {
    if (!isEligible(request) || image.isNull()) {
        return {};
    }
    QImage converted = DisplayColorSpace::convertImageToColorSpace(
        image, DisplayColorSpace::cacheColorSpace());
    if (converted.isNull()) {
        return {};
    }
    const QByteArray encoded = WebpCodec::encode(converted,
                                                  CacheWebpQuality);
    if (encoded.isEmpty()) {
        return {};
    }
    QByteArray entry;
    QDataStream stream(&entry, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << DerivedFileMagic << DerivedFileVersion
           << keyForRequest(request) << converted.size() << encoded;
    return stream.status() == QDataStream::Ok ? entry : QByteArray{};
}

bool PersistentDerivedImageCache::retrieveMetadata(ImageInfo &info) {
    if (!info.source.isValid() ||
        (!hasPersistentVersionStrength(info) &&
         !hasSessionVersionStrength(info))) {
        return false;
    }
    const MetadataKey requestedKey = metadataKeyForInfo(info);
    if (!requestedKey.isValid()) {
        return false;
    }
    const QString path = metadataCacheFilePath(requestedKey);
    QByteArray entry;
    {
        QMutexLocker locker(&cacheMutex);
        const QFileInfo fileInfo(path);
        if (!fileInfo.isFile() || fileInfo.size() <= 0 ||
            fileInfo.size() > MaximumMetadataEntryBytes) {
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        entry = file.readAll();
        if (entry.size() != fileInfo.size()) {
            return false;
        }
    }

    MetadataKey storedKey;
    QSize imageSize;
    ExifOrientation orientation = ExifOrientation::Horizontal;
    QVariantMap exif;
    if (!parseMetadataEntry(entry, storedKey, imageSize, orientation, exif) ||
        !metadataKeysEqual(requestedKey, storedKey)) {
        removeMetadataIfUnchanged(path, entry, requestedKey);
        return false;
    }
    info.imageSize = imageSize;
    info.orientation = orientation;
    info.exif = std::move(exif);
    info.fileSize = info.source.size;
    info.isCached = true;
    touchFile(path);
    return true;
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
           << info.exif;
    if (stream.status() != QDataStream::Ok ||
        entry.size() > MaximumMetadataEntryBytes) {
        return;
    }
    writeMetadataEntry(key, entry);
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
    if (!isEligible(request) || !request.checkCache) {
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
    if (!isEligible(request) || !request.checkCache) {
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
