#pragma once

#include "ExternalCatalogModel.h"

#include "DecodeManager.h"
#include "ProviderImageStore.h"
#include "ThumbnailMemoryCache.h"
#include "ThumbnailLoader.h"
#include "ViewerImageCache.h"
#include "DecodeSizePolicy.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QTimer>
#include <QTimeZone>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace ZoinGallery {

namespace {

QVariantMap decodeRequestTimingFields(const ImageDecodeRequest &request) {
    QVariantMap fields = MediaTimingTrace::sourceFields(request.info.source);
    fields.insert(QStringLiteral("sourceIdentity"),
                  request.info.sourceIdentity());
    fields.insert(QStringLiteral("targetWidth"), request.targetSize.width());
    fields.insert(QStringLiteral("targetHeight"), request.targetSize.height());
    fields.insert(QStringLiteral("viewer"), request.viewerRequest);
    fields.insert(QStringLiteral("backgroundViewer"),
                  request.backgroundViewerRequest);
    fields.insert(QStringLiteral("highPriority"), request.highPriority);
    fields.insert(QStringLiteral("requestNamespace"),
                  request.requestNamespace);
    return fields;
}

QString normalizedSessionId(QString id) {
    for (QChar &character : id) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            character = QLatin1Char('_');
        }
    }
    return id.isEmpty() ? QStringLiteral("session") : id;
}

qint64 integerValue(const QVariantMap &map, const QString &primary,
                    const QString &fallback, qint64 defaultValue) {
    const QVariant value = map.contains(primary) ? map.value(primary)
                                                  : map.value(fallback);
    bool ok = false;
    const qint64 result = value.toLongLong(&ok);
    return ok ? result : defaultValue;
}

qint64 sourceSizeValue(const QVariantMap &map) {
    if (map.contains(QStringLiteral("sizeKnown")) &&
        !map.value(QStringLiteral("sizeKnown")).toBool()) {
        return -1;
    }
    return integerValue(map, QStringLiteral("size"),
                        QStringLiteral("fileSize"), -1);
}

bool versionMatches(const QString &expectedVersion, qint64 expectedMtimeNs,
                    qint64 expectedSize,
                    const ImageInfo &actual) {
    if (!expectedVersion.isEmpty() &&
        actual.sourceVersionToken != expectedVersion) {
        return false;
    }
    if (expectedSize >= 0 && actual.fileSize >= 0 &&
        expectedSize != actual.fileSize) {
        return false;
    }
    // An external source is decoded from a temporary materialized path. Its
    // filesystem mtime describes the download time, not the authoritative
    // VFS item. The opaque source revision/size checks above are the identity
    // contract for that case; retain the mtime check for ordinary local files.
    if (!actual.source.isValid() && expectedMtimeNs != 0 &&
        actual.lastModified.isValid() &&
        expectedMtimeNs / 1000000 !=
            actual.lastModified.toMSecsSinceEpoch()) {
        return false;
    }
    return true;
}

ImageSourceDescriptor sourceDescriptor(
    const QVariantMap &map, const QString &name, qint64 size,
    qint64 mtimeNs) {
    const QString legacyPath = map.value(
        QStringLiteral("localPath"), map.value(QStringLiteral("path")))
                                   .toString();
    ImageSourceDescriptor source;
    source.resourceId = map.value(QStringLiteral("resourceId")).toString();
    if (source.resourceId.isEmpty()) {
        source.resourceId = legacyPath;
    }
    source.sourceKey = map.value(QStringLiteral("sourceKey")).toString();
    if (source.sourceKey.isEmpty()) {
        source.sourceKey =
            ThumbnailMemoryCache::canonicalSourceIdentity(source.resourceId);
    }
    source.contentVersion =
        map.value(QStringLiteral("contentVersion")).toString();
    if (source.contentVersion.isEmpty() && mtimeNs != 0) {
        source.contentVersion = QString::number(mtimeNs);
    }
    source.versionStrength =
        map.value(QStringLiteral("versionStrength")).toString();
    if (source.versionStrength.isEmpty() && !legacyPath.isEmpty()) {
        // Keep a local row's runtime identity stable while deferred metadata
        // promotes its initially empty content version to the stat tuple.
        source.versionStrength = QStringLiteral("local-stat");
    }
    source.storageClass =
        map.value(QStringLiteral("storageClass")).toString();
    if (source.storageClass.isEmpty() && !legacyPath.isEmpty()) {
        source.storageClass = QStringLiteral("local");
    }
    source.accessProfile =
        map.value(QStringLiteral("accessProfile")).toString();
    if (source.accessProfile.isEmpty() && !legacyPath.isEmpty()) {
        source.accessProfile = QStringLiteral("directLocal");
    }
    source.displayName = name;
    source.mimeType = map.value(QStringLiteral("mimeType")).toString();
    source.size = size;
    return source;
}

QVariantMap displayFields(const QVariantMap &map) {
    QVariantMap fields = map.value(
        QStringLiteral("displayFields")).toMap();
    static const QStringList directKeys{
        QStringLiteral("displayBaseName"),
        QStringLiteral("displayExtension"),
        QStringLiteral("sizeText"),
        QStringLiteral("mtimeText"),
        QStringLiteral("modeText"),
        QStringLiteral("isHidden"),
    };
    for (const QString &key : directKeys) {
        if (map.contains(key) && !fields.contains(key)) {
            fields.insert(key, map.value(key));
        }
    }
    if (map.contains(QStringLiteral("mtime"))
        && !fields.contains(QStringLiteral("mtimeText"))) {
        fields.insert(QStringLiteral("mtimeText"),
                      map.value(QStringLiteral("mtime")));
    }
    if (map.contains(QStringLiteral("mode"))
        && !fields.contains(QStringLiteral("modeText"))) {
        fields.insert(QStringLiteral("modeText"),
                      map.value(QStringLiteral("mode")));
    }
    return fields;
}

QVariantMap catalogDisplayFields(const QVariantMap &map,
                                 bool metadataDeferred) {
    if (!metadataDeferred) {
        return displayFields(map);
    }
    QVariantMap fields = map.value(QStringLiteral("displayFields")).toMap();
    // Hidden state is authoritative in the base directory phase and affects
    // first-frame opacity. Keep it alongside the two name presentation fields;
    // every genuinely deferred field stays absent until its exact chunk.
    for (const QString &key : {QStringLiteral("displayBaseName"),
                               QStringLiteral("displayExtension"),
                               QStringLiteral("isHidden")}) {
        const auto value = map.constFind(key);
        if (value != map.cend() && !fields.contains(key)) {
            fields.insert(key, *value);
        }
    }
    return fields;
}

bool carriesDisplayFields(const QVariantMap &map) {
    if (map.contains(QStringLiteral("displayFields"))) {
        return true;
    }
    static const QStringList directKeys{
        QStringLiteral("displayBaseName"),
        QStringLiteral("displayExtension"),
        QStringLiteral("sizeText"),
        QStringLiteral("mtimeText"),
        QStringLiteral("modeText"),
        QStringLiteral("isHidden"),
    };
    return map.contains(QStringLiteral("mtime"))
        || map.contains(QStringLiteral("mode"))
        || std::any_of(directKeys.cbegin(), directKeys.cend(),
                       [&map](const QString &key) {
                           return map.contains(key);
                       });
}

QString thumbnailTransformKey(const ImageDecodeRequest &request) {
    return request.thumbnailTransformKey.trimmed().isEmpty()
        ? QString::fromLatin1(
              ThumbnailMemoryCache::DefaultTransformKey)
        : request.thumbnailTransformKey.trimmed();
}

QString normalizedThumbnailTransformKey(const QString &transformKey) {
    ImageDecodeRequest request;
    request.thumbnailTransformKey = transformKey;
    return thumbnailTransformKey(request);
}

QString defaultIconPath(bool directory, bool image) {
    return directory
        ? QStringLiteral("qrc:/ZoinGallery/resources/FolderIcon.svg")
        : image
            ? QStringLiteral("qrc:/ZoinGallery/resources/ImageIcon.svg")
            : QStringLiteral("qrc:/ZoinGallery/resources/FileIcon.svg");
}

constexpr auto EmbeddedProvisionalTransform =
    "embedded-provisional-v1";
constexpr int CatalogThumbnailLongEdge = 384;
constexpr int NativeDwellMs = 450;

bool expensiveSource(const ImageSourceDescriptor &source) {
    return source.accessProfile != QStringLiteral("directLocal") ||
        (source.storageClass != QStringLiteral("local") &&
         !source.storageClass.isEmpty());
}

QString sourceRevisionKey(const QString &sourceIdentity,
                          const QString &contentVersion,
                          qint64 sourceSize) {
    return sourceIdentity + QChar(0x1f) + contentVersion + QChar(0x1f) +
        QString::number(sourceSize);
}

QString sourceWorkKey(const ImageSourceDescriptor &source,
                      qint64 sourceSize) {
    // A strong content revision may stay reusable across reconnects, while an
    // in-flight read handle never is. Progressive/pending work therefore also
    // binds to the exact broker authority and access contract.
    return sourceRevisionKey(source.sourceKey, source.contentVersion,
                             sourceSize) + QChar(0x1f) +
        source.resourceId + QChar(0x1f) + source.versionStrength +
        QChar(0x1f) + source.accessProfile +
        QChar(0x1f) + source.storageClass + QChar(0x1f) +
        source.mimeType + QChar(0x1f) + source.displayName;
}

bool sourceAuthorityMatches(const ImageSourceDescriptor &expected,
                            const ImageSourceDescriptor &actual) {
    // Legacy/test-injected local completions predate source descriptors.
    if (!actual.isValid()) {
        return true;
    }
    return expected.isValid() && actual.isValid() &&
        expected.resourceId == actual.resourceId &&
        expected.sourceKey == actual.sourceKey &&
        expected.contentVersion == actual.contentVersion &&
        expected.versionStrength == actual.versionStrength &&
        expected.accessProfile == actual.accessProfile &&
        expected.storageClass == actual.storageClass &&
        expected.mimeType == actual.mimeType;
}

} // namespace


} // namespace ZoinGallery
