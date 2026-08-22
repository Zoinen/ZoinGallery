#include "ExternalCatalogModel.h"

#include "DecodeManager.h"
#include "ProviderImageStore.h"
#include "ThumbnailMemoryCache.h"
#include "ViewerImageCache.h"
#include "DecodeSizePolicy.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QTimeZone>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace ZoinGallery {

namespace {

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
    if (expectedMtimeNs != 0 && actual.lastModified.isValid() &&
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
    if (source.versionStrength.isEmpty() && !legacyPath.isEmpty() &&
        !source.contentVersion.isEmpty()) {
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
    return fields;
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

ExternalCatalogModel::ExternalCatalogModel(
    QString sessionId, QString thumbnailProviderName,
    QString asyncProviderName,
    QSharedPointer<ProviderImageStore> store,
    QSharedPointer<ThumbnailMemoryCache> thumbnailCache,
    DecodeManager *decodeManager,
    qint64 viewerFitCacheByteBudget,
    qint64 viewerNativeCacheByteBudget,
    QObject *parent)
    : QAbstractListModel(parent),
      _sessionId(normalizedSessionId(std::move(sessionId))),
      _thumbnailProviderName(std::move(thumbnailProviderName)),
      _asyncProviderName(std::move(asyncProviderName)),
      _store(std::move(store)),
      _thumbnailCache(std::move(thumbnailCache)),
      _decodeManager(decodeManager),
      _viewerImageCache(
          QStringLiteral("zg-%1-view-").arg(_sessionId),
          _store, _thumbnailProviderName, _asyncProviderName,
          viewerFitCacheByteBudget, viewerNativeCacheByteBudget) {
    Q_ASSERT(_decodeManager);
    Q_ASSERT(_thumbnailCache);

    connect(_decodeManager, &DecodeManager::imageProbeReady, this,
            &ExternalCatalogModel::handleImageProbe);
    connect(_decodeManager, &DecodeManager::imageInfoReady, this,
            &ExternalCatalogModel::handleImageInfo);
    connect(_decodeManager, &DecodeManager::imagesInfoReady, this,
            [this](const QList<ImageInfo> &infos) {
                for (const ImageInfo &info : infos) {
                    handleImageInfo(info);
                }
            });
    connect(_decodeManager, &DecodeManager::imageReady, this,
            &ExternalCatalogModel::handleImageReady);
    connect(_decodeManager, &DecodeManager::imageReadFailed, this,
            &ExternalCatalogModel::handleImageReadFailed);
    connect(_thumbnailCache.data(),
            &ThumbnailMemoryCache::frameAvailable,
            this, &ExternalCatalogModel::handleThumbnailFrameAvailable);
    connect(_thumbnailCache.data(),
            &ThumbnailMemoryCache::frameEvicted,
            this, &ExternalCatalogModel::handleThumbnailFrameEvicted);
    connect(_thumbnailCache.data(),
            &ThumbnailMemoryCache::requestReleased,
            this, &ExternalCatalogModel::handleThumbnailRequestReleased);
    _nativeDwellTimer.setSingleShot(true);
    connect(&_nativeDwellTimer, &QTimer::timeout, this,
            &ExternalCatalogModel::finishDeferredNativeDwell);
    _backgroundRetryTimer.setSingleShot(true);
    connect(&_backgroundRetryTimer, &QTimer::timeout, this,
            &ExternalCatalogModel::processBackgroundRetries);
}

ExternalCatalogModel::~ExternalCatalogModel() {
    shutdown();
    for (Entry &entry : _entries) {
        delete entry.item;
        entry.item = nullptr;
    }
}

int ExternalCatalogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : _entries.size();
}

QVariant ExternalCatalogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !validRow(index.row())) {
        return {};
    }
    const Entry &entry = _entries.at(index.row());
    switch (role) {
    case FileListModel::ImageIdUrlRole:
        return entry.item->imageIdUrl();
    case FileListModel::SelectedRole:
        return entry.selected;
    case FileListModel::SelectionGroupIdRole:
        return entry.item->selectionGroupId();
    case FileListModel::SelectionGroupColorRole:
        return entry.item->selectionGroupColor();
    case FileListModel::ImageFileRole:
        return QVariant::fromValue(entry.item);
    case FileListModel::FolderRole:
        return entry.directory;
    case FileListModel::IsImageRole:
        return entry.image;
    case FileListModel::ImageFullSizeRole:
        return entry.item->fullSize();
    case FileListModel::FolderViewRole:
        return false;
    case FileListModel::LastModifiedRole:
        return entry.item->lastModified();
    case FileListModel::FileSizeRole:
        return entry.size;
    case EntryIdRole:
        return entry.id;
    case SourceIndexRole:
        return entry.sourceIndex;
    case LocalPathRole:
        return entry.localPath;
    case VersionTokenRole:
        return entry.contentVersion;
    default:
        return {};
    }
}

QHash<int, QByteArray> ExternalCatalogModel::roleNames() const {
    QHash<int, QByteArray> names;
    names[FileListModel::ImageIdUrlRole] = "imageIdUrlRole";
    names[FileListModel::SelectedRole] = "selectedRole";
    names[FileListModel::SelectionGroupIdRole] = "selectionGroupIdRole";
    names[FileListModel::SelectionGroupColorRole] = "selectionGroupColorRole";
    names[FileListModel::ImageFileRole] = "imageFileRole";
    names[FileListModel::FolderRole] = "folderRole";
    names[FileListModel::IsImageRole] = "isImageRole";
    names[FileListModel::ImageFullSizeRole] = "imageFullSizeRole";
    names[FileListModel::LastModifiedRole] = "lastModifiedRole";
    names[FileListModel::FileSizeRole] = "fileSizeRole";
    names[EntryIdRole] = "entryId";
    names[SourceIndexRole] = "sourceIndex";
    names[LocalPathRole] = "localPath";
    names[VersionTokenRole] = "versionToken";
    return names;
}

bool ExternalCatalogModel::catalogMatches(
    const QVariantList &values, bool *carriesAppearance) const {
    if (carriesAppearance) {
        *carriesAppearance = false;
    }
    if (values.size() != _entries.size()) {
        return false;
    }

    QSet<QString> seenIds;
    for (int row = 0; row < values.size(); ++row) {
        const QVariantMap map = values.at(row).toMap();
        if (carriesAppearance
            && map.contains(QStringLiteral("highlightStyle"))) {
            *carriesAppearance = true;
        }
        const Entry &current = _entries.at(row);
        const int sourceIndex =
            map.value(QStringLiteral("index"), row).toInt();
        const QString name = map.value(QStringLiteral("name")).toString();
        const QString localPath = map.value(
            QStringLiteral("localPath"), map.value(QStringLiteral("path")))
                                      .toString();
        QString id = map.value(QStringLiteral("entryId")).toString();
        if (id.isEmpty()) {
            const QString fallbackSource =
                map.value(QStringLiteral("sourceKey")).toString();
            id = !fallbackSource.isEmpty()
                ? fallbackSource
                : !localPath.isEmpty() ? localPath
                : QStringLiteral("row:%1:%2").arg(sourceIndex).arg(name);
        }
        if (seenIds.contains(id)) {
            id += QStringLiteral("#%1").arg(row);
        }
        seenIds.insert(id);
        const bool directory = map.value(
            QStringLiteral("isDir"), map.value(QStringLiteral("directory")))
                                   .toBool();
        const bool image = map.contains(QStringLiteral("isImage"))
            ? map.value(QStringLiteral("isImage")).toBool()
            : (!directory && FileListModel::isImage(name));
        const qint64 mtimeNs = integerValue(
            map, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
        const qint64 size = sourceSizeValue(map);
        const ImageSourceDescriptor source =
            sourceDescriptor(map, name, size, mtimeNs);
        const QVariantMap normalizedDisplayFields = displayFields(map);

        if (current.id != id || current.sourceIndex != sourceIndex ||
            current.name != name || current.localPath != localPath ||
            current.source.resourceId != source.resourceId ||
            current.source.sourceKey != source.sourceKey ||
            current.contentVersion != source.contentVersion ||
            current.source.versionStrength != source.versionStrength ||
            current.source.storageClass != source.storageClass ||
            current.source.accessProfile != source.accessProfile ||
            current.source.mimeType != source.mimeType ||
            current.directory != directory || current.image != image ||
            current.mtimeNs != mtimeNs || current.size != size ||
            current.displayFields != normalizedDisplayFields) {
            return false;
        }
    }
    return true;
}

bool ExternalCatalogModel::applyCatalog(const QVariantList &values) {
    if (_shutdown) {
        return false;
    }
    // Go may advance its authoritative catalog revision for semantic fields
    // that Gallery does not consume (for example IsCached). Advance the
    // session revision without tearing down identical image objects, queued
    // work, textures, or viewer state.
    bool hasAppearance = false;
    if (catalogMatches(values, &hasAppearance)) {
        return !hasAppearance || applyAppearance(values);
    }

    const QString activeViewerEntryId = _viewerEntryId;
    const QSize activeViewerViewportSize = _viewerViewportSize;
    bool activeViewerWasImage = false;
    const int activeViewerRow = rowForEntryId(activeViewerEntryId);
    if (validRow(activeViewerRow)) {
        const Entry &entry = _entries.at(activeViewerRow);
        activeViewerWasImage = entry.image;
    }
    invalidateNativeDwell();

    // A catalog refresh often changes one row or presentation-only fields.
    // Preserve immutable-source work and completed progressive state for
    // every exact source revision that survives the diff; slow reads must not
    // restart merely because another entry changed.
    QSet<QString> oldSourceWorkKeys;
    QSet<QString> oldSourceIdentities;
    for (const Entry &entry : std::as_const(_entries)) {
        if (!entry.image || entry.sourceIdentity.isEmpty()) {
            continue;
        }
        oldSourceWorkKeys.insert(sourceWorkKey(entry.source, entry.size));
        oldSourceIdentities.insert(entry.sourceIdentity);
    }
    const bool catalogProbeWasRequested = _catalogProbeRequested;
    const bool catalogMetadataWasRequested = _catalogMetadataRequested;
    const bool catalogFitWasStarted = _catalogFitStarted;

    QHash<QString, Entry> previous;
    previous.reserve(_entries.size());
    for (const Entry &entry : std::as_const(_entries)) {
        previous.insert(entry.id, entry);
    }

    QList<Entry> next;
    next.reserve(values.size());
    QSet<QString> seenIds;
    QSet<QString> nextViewerSources;

    beginResetModel();
    for (int row = 0; row < values.size(); ++row) {
        const QVariantMap map = values.at(row).toMap();
        Entry entry;
        entry.sourceIndex = map.value(QStringLiteral("index"), row).toInt();
        entry.name = map.value(QStringLiteral("name")).toString();
        entry.localPath = map.value(QStringLiteral("localPath"),
                                    map.value(QStringLiteral("path"))).toString();
        entry.id = map.value(QStringLiteral("entryId")).toString();
        if (entry.id.isEmpty()) {
            const QString fallbackSource =
                map.value(QStringLiteral("sourceKey")).toString();
            entry.id = !fallbackSource.isEmpty()
                ? fallbackSource
                : !entry.localPath.isEmpty() ? entry.localPath
                : QStringLiteral("row:%1:%2").arg(entry.sourceIndex).arg(entry.name);
        }
        if (seenIds.contains(entry.id)) {
            entry.id += QStringLiteral("#%1").arg(row);
        }
        seenIds.insert(entry.id);
        entry.directory = map.value(QStringLiteral("isDir"),
                                    map.value(QStringLiteral("directory"))).toBool();
        entry.image = map.contains(QStringLiteral("isImage"))
            ? map.value(QStringLiteral("isImage")).toBool()
            : (!entry.directory && FileListModel::isImage(entry.name));
        entry.selected = map.value(QStringLiteral("selected")).toBool();
        entry.mtimeNs = integerValue(map, QStringLiteral("mtimeNs"),
                                     QStringLiteral("mtimeNanos"), 0);
        entry.size = sourceSizeValue(map);
        entry.source = sourceDescriptor(
            map, entry.name, entry.size, entry.mtimeNs);
        entry.contentVersion = entry.source.contentVersion;
        entry.sourceIdentity = entry.source.runtimeIdentity();
        entry.displayFields = displayFields(map);

        Entry old = previous.take(entry.id);
        entry.item = old.item ? old.item : new ImageFile(this);
        const bool sourceChanged = old.item &&
            (old.source.resourceId != entry.source.resourceId ||
             old.source.sourceKey != entry.source.sourceKey ||
             old.contentVersion != entry.contentVersion ||
             old.source.versionStrength != entry.source.versionStrength ||
             old.source.accessProfile != entry.source.accessProfile ||
             old.source.storageClass != entry.source.storageClass ||
             old.source.mimeType != entry.source.mimeType ||
             old.size != entry.size || old.image != entry.image);
        if (sourceChanged) {
            _viewerImageCache.remove(old.sourceIdentity);
            clearPublishedImage(old);
            entry.item->setFullSize({});
        }
        else if (old.item) {
            entry.thumbnailProviderId = old.thumbnailProviderId;
            entry.thumbnailRequestedSize = old.thumbnailRequestedSize;
            entry.thumbnailTransformKey = old.thumbnailTransformKey;
        }

        const QFileInfo pathInfo(entry.localPath);
        const QString folder = entry.localPath.isEmpty()
            ? QString()
            : pathInfo.absolutePath();
        const QString fileName = !entry.name.isEmpty()
            ? entry.name
            : pathInfo.fileName();
        entry.item->setFolderPath(folder);
        entry.item->setFileName(fileName);
        entry.item->setIndex(row);
        entry.item->setIsFolder(entry.directory);
        entry.item->setIsImage(entry.image);
        QString iconPath = defaultIconPath(entry.directory, entry.image);
        if (map.contains(QStringLiteral("highlightStyle"))) {
            const QVariantMap style = map.value(
                QStringLiteral("highlightStyle")).toMap();
            entry.item->setHighlightStyle(style);
            const QString styledIcon = style.value(
                QStringLiteral("icon")).toString();
            if (!styledIcon.isEmpty()) {
                iconPath = styledIcon;
            }
        }
        entry.item->setIconPath(iconPath);
        entry.item->setIsSelected(entry.selected);
        entry.item->setImageProviderName(_thumbnailProviderName);

        ImageInfo info = old.item && !sourceChanged
            ? entry.item->info() : ImageInfo{};
        info.path = entry.sourceIdentity;
        info.source = entry.source;
        info.requestNamespace = _sessionId;
        info.sourceVersionToken = entry.contentVersion;
        if (entry.mtimeNs != 0) {
            info.lastModified = QDateTime::fromMSecsSinceEpoch(
                entry.mtimeNs / 1000000, QTimeZone::UTC);
        }
        if (entry.size >= 0) {
            info.fileSize = entry.size;
        }
        entry.item->setInfo(info);
        // Apply host-preformatted columns after metadata, so rows with a
        // partial displayFields map derive their missing values only once.
        entry.item->setDisplayFields(entry.displayFields);
        if (entry.image) {
            nextViewerSources.insert(sourceRevisionKey(
                entry.sourceIdentity, entry.contentVersion, entry.size));
        }
        next.append(entry);
    }

    QSet<QString> retainedSourceIdentities;
    for (const Entry &entry : std::as_const(next)) {
        if (entry.image && oldSourceWorkKeys.contains(
                sourceWorkKey(entry.source, entry.size))) {
            retainedSourceIdentities.insert(entry.sourceIdentity);
        }
    }
    QSet<QString> invalidatedSourceIdentities = oldSourceIdentities;
    invalidatedSourceIdentities.subtract(retainedSourceIdentities);
    _decodeManager->cancelSourceRequests(
        _sessionId, invalidatedSourceIdentities);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(
            _sessionId, invalidatedSourceIdentities);
    }

    for (Entry &entry : previous) {
        const QString viewerSource = sourceRevisionKey(
            entry.sourceIdentity, entry.contentVersion, entry.size);
        const bool sourceStillPresent = entry.image &&
            nextViewerSources.contains(viewerSource);
        if (!sourceStillPresent) {
            _viewerImageCache.remove(entry.sourceIdentity);
        }
        clearPublishedImage(entry);
        delete entry.item;
    }

    _entries = std::move(next);
    _idToRow.clear();
    _sourceToRow.clear();
    _sourceEntryIds.clear();
    _providerEntryIds.clear();
    for (int row = 0; row < _entries.size(); ++row) {
        const Entry &entry = _entries.at(row);
        _idToRow.insert(entry.id, row);
        if (!entry.sourceIdentity.isEmpty()) {
            _sourceToRow.insert(entry.sourceIdentity, row);
        }
        if (!entry.sourceIdentity.isEmpty()) {
            _sourceEntryIds.insert(entry.sourceIdentity, entry.id);
        }
        if (!entry.thumbnailProviderId.isEmpty()) {
            _providerEntryIds.insert(entry.thumbnailProviderId, entry.id);
        }
    }

    const auto retainedVersion = [this, &retainedSourceIdentities](
                                     const QString &sourceIdentity,
                                     const QString &version) {
        if (!retainedSourceIdentities.contains(sourceIdentity)) {
            return false;
        }
        const int row = _sourceToRow.value(sourceIdentity, -1);
        return validRow(row) &&
            _entries.at(row).contentVersion == version;
    };
    for (auto it = _metadataPendingVersions.begin();
         it != _metadataPendingVersions.end();) {
        if (!retainedVersion(it.key(), it.value())) {
            it = _metadataPendingVersions.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _metadataResolvedPaths.begin();
         it != _metadataResolvedPaths.end();) {
        if (!retainedSourceIdentities.contains(*it)) {
            it = _metadataResolvedPaths.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _probePendingVersions.begin();
         it != _probePendingVersions.end();) {
        if (!retainedVersion(it.key(), it.value())) {
            it = _probePendingVersions.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _probeResolvedVersions.begin();
         it != _probeResolvedVersions.end();) {
        if (!retainedVersion(it.key(), it.value())) {
            it = _probeResolvedVersions.erase(it);
        }
        else {
            ++it;
        }
    }

    QSet<QString> retainedProbeKeys;
    QSet<QString> retainedRetryKeys;
    QStringList retainedRequestPrefixes;
    QSet<QString> retainedEntryIds;
    for (const Entry &entry : std::as_const(_entries)) {
        retainedEntryIds.insert(entry.id);
        if (!retainedSourceIdentities.contains(entry.sourceIdentity)) {
            continue;
        }
        retainedProbeKeys.insert(entry.source.cacheKey());
        retainedRetryKeys.insert(
            entry.sourceIdentity + QChar(0x1f) + entry.contentVersion);
        retainedRequestPrefixes.append(sourceRevisionKey(
            entry.sourceIdentity, entry.contentVersion, entry.size) +
            QChar(0x1f));
    }
    const auto requestRetained = [&retainedRequestPrefixes](
                                     const QString &key) {
        return std::any_of(
            retainedRequestPrefixes.cbegin(),
            retainedRequestPrefixes.cend(),
            [&key](const QString &prefix) {
                return key.startsWith(prefix);
            });
    };
    for (auto it = _pendingViewerRequests.begin();
         it != _pendingViewerRequests.end();) {
        it = requestRetained(*it)
            ? std::next(it) : _pendingViewerRequests.erase(it);
    }
    for (auto it = _pendingThumbnailRequests.begin();
         it != _pendingThumbnailRequests.end();) {
        if (!requestRetained(it.key())) {
            it = _pendingThumbnailRequests.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _catalogFitPendingKeys.begin();
         it != _catalogFitPendingKeys.end();) {
        it = requestRetained(*it)
            ? std::next(it) : _catalogFitPendingKeys.erase(it);
    }
    for (auto it = _catalogFitResolvedSources.begin();
         it != _catalogFitResolvedSources.end();) {
        it = retainedSourceIdentities.contains(*it)
            ? std::next(it) : _catalogFitResolvedSources.erase(it);
    }
    for (auto it = _catalogFitWaitingMetadata.begin();
         it != _catalogFitWaitingMetadata.end();) {
        it = retainedSourceIdentities.contains(*it)
            ? std::next(it) : _catalogFitWaitingMetadata.erase(it);
    }
    for (auto it = _probeRetryableSources.begin();
         it != _probeRetryableSources.end();) {
        it = retainedProbeKeys.contains(*it)
            ? std::next(it) : _probeRetryableSources.erase(it);
    }
    for (auto it = _probeRetryAttempts.begin();
         it != _probeRetryAttempts.end();) {
        if (!retainedProbeKeys.contains(it.key())) {
            it = _probeRetryAttempts.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _probeRetryNotBeforeMs.begin();
         it != _probeRetryNotBeforeMs.end();) {
        if (!retainedProbeKeys.contains(it.key())) {
            it = _probeRetryNotBeforeMs.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _metadataRetryAttempts.begin();
         it != _metadataRetryAttempts.end();) {
        if (!retainedRetryKeys.contains(it.key())) {
            it = _metadataRetryAttempts.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _metadataRetryScheduled.begin();
         it != _metadataRetryScheduled.end();) {
        it = retainedRetryKeys.contains(*it)
            ? std::next(it) : _metadataRetryScheduled.erase(it);
    }
    for (auto it = _viewerPlans.begin(); it != _viewerPlans.end();) {
        if (!retainedEntryIds.contains(it.key())) {
            it = _viewerPlans.erase(it);
        }
        else {
            ++it;
        }
    }
    const auto decodeRetryRetained = [&requestRetained](
                                         const QString &key) {
        const qsizetype separator = key.indexOf(QLatin1Char(':'));
        return separator >= 0 &&
            requestRetained(key.mid(separator + 1));
    };
    for (auto it = _sourceDecodeRetryAttempts.begin();
         it != _sourceDecodeRetryAttempts.end();) {
        if (!decodeRetryRetained(it.key())) {
            it = _sourceDecodeRetryAttempts.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto it = _sourceDecodeRetryScheduled.begin();
         it != _sourceDecodeRetryScheduled.end();) {
        it = decodeRetryRetained(*it)
            ? std::next(it) : _sourceDecodeRetryScheduled.erase(it);
    }

    // Row-number queues cannot survive reordering, but exact-revision
    // pending/resolved sets above can. Rebuild bounded passes from row zero;
    // their pumps skip retained work instead of reopening it.
    _metadataVisibleRows.clear();
    _metadataOverscanRows.clear();
    _metadataUrgentRows.clear();
    _metadataAdHocRows.clear();
    _catalogMetadataCursor = 0;
    _metadataPumpScheduled = false;
    _catalogMetadataRequested = catalogMetadataWasRequested;
    _probeVisibleRows.clear();
    _probeOverscanRows.clear();
    _probeUrgentRows.clear();
    _catalogProbeCursor = 0;
    _probePumpScheduled = false;
    _catalogProbeRequested = catalogProbeWasRequested;
    _probePassComplete = catalogProbeWasRequested &&
        _probePendingVersions.isEmpty() && probeBarrierReached();
    _catalogFitRows.clear();
    _catalogFitPumpScheduled = false;
    _catalogFitStarted = false;
    const bool resumeCatalogFit = catalogFitWasStarted ||
        _probePassComplete;
    if (!retainedEntryIds.contains(_deferredNativeEntryId)) {
        _deferredNativeEntryId.clear();
        invalidateNativeDwell();
    }
    if (_entries.isEmpty()) {
        _cursorRow = -1;
    } else {
        _cursorRow = qBound(0, _cursorRow, _entries.size() - 1);
    }
    endResetModel();

    if (_catalogProbeRequested && !_probePassComplete) {
        scheduleProbePump();
    }
    if (_catalogMetadataRequested) {
        scheduleMetadataPump();
    }
    // Preserve a previously requested Fit pass across the diff, but never
    // restart full-source work ahead of a newly established probe barrier.
    // The final handleImageProbe() completion resumes it below.
    if (resumeCatalogFit &&
        (!_catalogProbeRequested || _probePassComplete)) {
        beginCatalogFitPass();
    }

    const int refreshedViewerRow = rowForEntryId(activeViewerEntryId);
    const bool viewerStillAvailable = activeViewerWasImage &&
        validRow(refreshedViewerRow) &&
        _entries.at(refreshedViewerRow).image;
    if (!viewerStillAvailable) {
        clearViewer();
    }
    else {
        _viewerEntryId = activeViewerEntryId;
        _viewerViewportSize = activeViewerViewportSize;
        // Resume the target implied by the last viewport. Exact-revision
        // work and frames survived the diff; only changed sources are
        // replanned.
        notifyViewerImageUrlChanged();
        scheduleViewerDecode();
    }

    return true;
}

bool ExternalCatalogModel::applyAppearance(const QVariantList &values) {
    if (_shutdown) {
        return false;
    }
    QList<int> changedRows;
    changedRows.reserve(values.size());
    for (const QVariant &value : values) {
        const QVariantMap map = value.toMap();
        const int row = rowForEntryId(map.value(QStringLiteral("entryId")).toString());
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = _entries[row];
        const QVariantMap style = map.value(QStringLiteral("highlightStyle")).toMap();
        const QString styledIcon = style.value(QStringLiteral("icon")).toString();
        const QString fallbackIcon = defaultIconPath(
            entry.directory, entry.image);
        const QString iconPath = styledIcon.isEmpty()
            ? fallbackIcon : styledIcon;
        if (entry.item->highlightStyle() == style
            && entry.item->iconPath() == iconPath) {
            continue;
        }
        entry.item->setHighlightStyle(style);
        entry.item->setIconPath(iconPath);
        changedRows.append(row);
    }

    // Host appearance snapshots normally cover the whole catalog. Emitting
    // one signal per row made every QML proxy/layout observer repeat its own
    // work thousands of times. Preserve precise ranges for sparse updates,
    // but collapse adjacent changed rows into one notification.
    std::sort(changedRows.begin(), changedRows.end());
    changedRows.erase(std::unique(changedRows.begin(), changedRows.end()),
                      changedRows.end());
    for (qsizetype offset = 0; offset < changedRows.size();) {
        const int first = changedRows.at(offset);
        int last = first;
        ++offset;
        while (offset < changedRows.size()
               && changedRows.at(offset) == last + 1) {
            last = changedRows.at(offset);
            ++offset;
        }
        emit dataChanged(index(first, 0), index(last, 0),
                         {FileListModel::ImageFileRole});
    }
    return true;
}

bool ExternalCatalogModel::applyState(
    const QString &cursorEntryId, int cursorIndex,
    const QStringList &selectedEntryIds, bool updateSelection) {
    if (_shutdown) {
        return false;
    }
    if (updateSelection) {
        const QSet<QString> selected(selectedEntryIds.begin(),
                                     selectedEntryIds.end());
        int firstChanged = -1;
        int lastChanged = -1;
        for (int row = 0; row < _entries.size(); ++row) {
            Entry &entry = _entries[row];
            const bool shouldSelect = selected.contains(entry.id);
            if (entry.selected == shouldSelect) {
                continue;
            }
            entry.selected = shouldSelect;
            entry.item->setIsSelected(shouldSelect);
            firstChanged = firstChanged < 0 ? row : firstChanged;
            lastChanged = row;
        }
        if (firstChanged >= 0) {
            emit dataChanged(index(firstChanged), index(lastChanged),
                             {FileListModel::SelectedRole});
        }
    }

    int newCursor = rowForEntryId(cursorEntryId);
    if (newCursor < 0 && validRow(cursorIndex)) {
        newCursor = cursorIndex;
    }
    if (newCursor < 0 && !_entries.isEmpty()) {
        newCursor = 0;
    }
    _cursorRow = newCursor;
    return true;
}

QString ExternalCatalogModel::entryIdAt(int row) const {
    return validRow(row) ? _entries.at(row).id : QString();
}

QString ExternalCatalogModel::entryNameAt(int row) const {
    return validRow(row) ? _entries.at(row).name : QString();
}

QString ExternalCatalogModel::localPathAt(int row) const {
    return validRow(row) ? _entries.at(row).localPath : QString();
}

bool ExternalCatalogModel::isImageAt(int row) const {
    return validRow(row) && _entries.at(row).image;
}

bool ExternalCatalogModel::isDirectoryAt(int row) const {
    return validRow(row) && _entries.at(row).directory;
}

int ExternalCatalogModel::sourceIndexAt(int row) const {
    return validRow(row) ? _entries.at(row).sourceIndex : -1;
}

QSize ExternalCatalogModel::imageOriginalSizeAt(int row) const {
    return validRow(row) && _entries.at(row).item
        ? _entries.at(row).item->fullSize() : QSize();
}

int ExternalCatalogModel::rowForEntryId(const QString &entryId) const {
    return _idToRow.value(entryId, -1);
}

int ExternalCatalogModel::cursorRow() const {
    return _cursorRow;
}

void ExternalCatalogModel::ensurePreviews() {
    if (_shutdown) {
        return;
    }
    const bool hasExpensiveSource = std::any_of(
        _entries.cbegin(), _entries.cend(), [](const Entry &entry) {
            return entry.image && entry.source.isValid() &&
                expensiveSource(entry.source);
        });
    if (!hasExpensiveSource) {
        // Direct-local catalogs retain the existing viewport-driven path.
        // A catalog-wide embedded probe has no network latency to hide and
        // would only contend with visible metadata/thumbnail work.
        return;
    }
    // Probe admission remains bounded; setting the catalog flag does not
    // enqueue thousands of runners. Renderer-supplied visible/overscan rows
    // are drained ahead of this cursor by pumpProbeRequests().
    _catalogProbeRequested = true;
    scheduleProbePump();
}

void ExternalCatalogModel::resetExternalSource() {
    if (_shutdown) {
        return;
    }
    applyCatalog({});
    _cursorRow = -1;
}

QString ExternalCatalogModel::viewerImageUrlAt(int row) const {
    if (!validRow(row)) {
        return {};
    }
    const Entry &entry = _entries.at(row);
    if (entry.id == _viewerEntryId) {
        const auto sources = viewerImageSourcesAt(row);
        return sources.isEmpty() ? QString() : sources.constLast().first;
    }
    return entry.item ? entry.item->imageIdUrl() : QString();
}

QString ExternalCatalogModel::bestViewerImageUrlAt(int row) const {
    const auto sources = viewerImageSourcesAt(row);
    return sources.isEmpty() ? QString() : sources.constLast().first;
}

QList<QPair<QString, int>> ExternalCatalogModel::viewerImageSourcesAt(
    int row) const {
    if (!validRow(row)) {
        return {};
    }

    QSize viewerSize = _viewerViewportSize;
    const Entry &entry = _entries.at(row);
    const auto plan = _viewerPlans.constFind(entry.id);
    if (plan != _viewerPlans.constEnd()) {
        viewerSize = plan->viewportSize;
    }
    return _viewerImageCache.imageSources(
        entry.item, viewerSize,
        [this](ImageDecodeRequest &request) {
            stabilizeViewerFitRequest(request);
        });
}

void ExternalCatalogModel::requestViewer(
    int row, const QSize &viewportSize) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image ||
        !viewportSize.isValid()) {
        clearViewer();
        return;
    }

    const QString entryId = _entries.at(row).id;
    const bool entryChanged = _viewerEntryId != entryId;
    const bool viewportChanged = _viewerViewportSize != viewportSize;
    if (entryChanged || viewportChanged) {
        invalidateNativeDwell();
    }
    _viewerViewportSize = viewportSize;
    const bool nativeRequest = viewportSize.isEmpty();
    if (!nativeRequest) {
        _lastViewerFitViewportSize = viewportSize;
        _deferredNativeEntryId.clear();
    }
    if (entryChanged || viewportChanged) {
        // Supplemental swipe plans are meaningful only for the viewport and
        // presentation mode which created them.  In particular, a native
        // 0x0 plan must never make a later Fit swipe select level 2, and an
        // older Fit size must not advertise an undersized transition frame.
        _viewerPlans.clear();
    }
    if (entryChanged) {
        _viewerEntryId = entryId;
        notifyViewerImageUrlChanged();
    }
    const int prefetchCount = nativeRequest ? 5 : 16;
    _viewerPlans.insert(entryId, {viewportSize, prefetchCount});
    if (nativeRequest) {
        // First guarantee a usable Fit base for current +/-2. Native pixels
        // are admitted only after a separate dwell below, so a quick swipe or
        // resize cannot spend bandwidth on a frame the user never examines.
        _deferredNativeEntryId = entryId;
        scheduleViewerDecodeAt(
            row, _lastViewerFitViewportSize, prefetchCount);
        tryScheduleDeferredNative();
        return;
    }
    scheduleViewerDecodeAt(row, viewportSize, prefetchCount);
}

void ExternalCatalogModel::requestViewerAt(
    int row, const QSize &viewportSize) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image ||
        !viewportSize.isValid()) {
        return;
    }

    const QString entryId = _entries.at(row).id;
    const bool nativeRequest = viewportSize.isEmpty();
    const int prefetchCount = entryId == _viewerEntryId
        ? (nativeRequest ? 5 : 16) : 1;
    if (!_viewerPlans.contains(entryId) &&
        _viewerPlans.size() >= 3) {
        const ViewerPlan activePlan = _viewerPlans.value(_viewerEntryId);
        _viewerPlans.clear();
        if (!_viewerEntryId.isEmpty() &&
            activePlan.viewportSize.isValid()) {
            _viewerPlans.insert(_viewerEntryId, activePlan);
        }
    }
    _viewerPlans.insert(entryId, {viewportSize, prefetchCount});
    if (nativeRequest && entryId == _viewerEntryId) {
        if (_deferredNativeEntryId != entryId) {
            invalidateNativeDwell();
        }
        _deferredNativeEntryId = entryId;
        scheduleViewerDecodeAt(
            row, _lastViewerFitViewportSize, prefetchCount);
        tryScheduleDeferredNative();
    }
    else {
        scheduleViewerDecodeAt(row, viewportSize, prefetchCount);
    }
}

void ExternalCatalogModel::setViewerIndex(int row) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image) {
        clearViewer();
        return;
    }

    const QString entryId = _entries.at(row).id;
    if (_viewerEntryId == entryId) {
        scheduleViewerDecode();
        return;
    }

    invalidateNativeDwell();
    _viewerEntryId = entryId;
    _viewerPlans.clear();
    if (_viewerViewportSize.isValid()) {
        const int prefetchCount = _viewerViewportSize.isEmpty() ? 5 : 16;
        _viewerPlans.insert(
            entryId, {_viewerViewportSize, prefetchCount});
    }
    notifyViewerImageUrlChanged();
    scheduleViewerDecode();
}

void ExternalCatalogModel::clearViewer() {
    const bool hadViewer = !_viewerEntryId.isEmpty();
    _decodeManager->cancelViewerRequests(_sessionId);
    _pendingViewerRequests.clear();
    _deferredNativeEntryId.clear();
    invalidateNativeDwell();
    _viewerEntryId.clear();
    _viewerViewportSize = {};
    _viewerPlans.clear();
    if (hadViewer) {
        notifyViewerImageUrlChanged();
    }
}

qsizetype ExternalCatalogModel::viewerFitFrameCount() const {
    return _viewerImageCache.viewerImageCount();
}

qsizetype ExternalCatalogModel::viewerNativeFrameCount() const {
    return _viewerImageCache.fullSizeImageCount();
}

qint64 ExternalCatalogModel::viewerFitRetainedBytes() const {
    return _viewerImageCache.fitRetainedBytes();
}

qint64 ExternalCatalogModel::viewerNativeRetainedBytes() const {
    return _viewerImageCache.nativeRetainedBytes();
}

qint64 ExternalCatalogModel::viewerFitByteBudget() const {
    return _viewerImageCache.fitByteBudget();
}

qint64 ExternalCatalogModel::viewerNativeByteBudget() const {
    return _viewerImageCache.nativeByteBudget();
}

qsizetype ExternalCatalogModel::metadataPendingRequestCount() const {
    return _metadataPendingVersions.size();
}

qsizetype ExternalCatalogModel::metadataPeakPendingRequestCount() const {
    return _metadataPeakPending;
}

quint64 ExternalCatalogModel::metadataSubmittedBatchCount() const {
    return _metadataSubmittedBatches;
}

void ExternalCatalogModel::decodeImages(
    const QList<ImageDecodeRequest> &requests) {
    if (_shutdown) {
        return;
    }

    QList<ImageDecodeRequest> scopedRequests;
    scopedRequests.reserve(requests.size());
    for (ImageDecodeRequest request : requests) {
        if (!request.targetSize.isValid() ||
            request.targetSize.width() <= 0 ||
            request.targetSize.height() <= 0) {
            continue;
        }
        const int row = _sourceToRow.value(
            request.info.sourceIdentity(), -1);
        if (!validRow(row) || !_entries.at(row).image) {
            continue;
        }

        Entry &entry = _entries[row];
        if (expensiveSource(entry.source) &&
            _probeRetryableSources.contains(entry.source.cacheKey())) {
            enqueueProbeRows({row}, request.highPriority);
            scheduleProbePump();
        }
        if (_catalogProbeRequested && !_probePassComplete &&
            expensiveSource(entry.source) && !probeResolvedFor(entry)) {
            enqueueProbeRows({row}, request.highPriority);
            scheduleProbePump();
            continue;
        }
        request.requestNamespace = _sessionId;
        request.info.requestNamespace = _sessionId;
        request.info.source = entry.source;
        request.info.path = entry.sourceIdentity;
        request.info.sourceVersionToken = entry.contentVersion;
        // External artifacts use the opaque host identity/revision and the
        // stable target tier. Weak/session revisions are rejected by the
        // derived cache itself and continue directly to the source.
        request.checkCache = true;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = true;
        request.thumbnailTransformKey = thumbnailTransformKey(request);
        request.targetSize = stableDecodeTarget(
            request.targetSize,
            entry.item ? entry.item->fullSize() : QSize(),
            entry.thumbnailRequestedSize,
            DecodeSizeFamily::Thumbnail,
            expensiveSource(entry.source));
        if (entry.thumbnailRequestedSize.isValid() &&
            (entry.thumbnailRequestedSize != request.targetSize ||
             entry.thumbnailTransformKey != request.thumbnailTransformKey)) {
            ImageDecodeRequest superseded;
            superseded.info = entry.item->info();
            superseded.targetSize = entry.thumbnailRequestedSize;
            superseded.thumbnailTransformKey =
                entry.thumbnailTransformKey;
            // A row has one current presentation target. Keep global owner
            // work alive for cache reuse, but do not let an obsolete local
            // coalesced record block a later return to that target.
            _pendingThumbnailRequests.remove(
                thumbnailRequestKey(superseded));
        }
        entry.thumbnailRequestedSize = request.targetSize;
        entry.thumbnailTransformKey = request.thumbnailTransformKey;

        const QString key = thumbnailRequestKey(request);
        if (_pendingThumbnailRequests.contains(key)) {
            continue;
        }
        const ThumbnailMemoryCache::AcquireResult cached =
            _thumbnailCache->acquire(
                _sessionId, entry.sourceIdentity, entry.contentVersion,
                entry.size, request.targetSize,
                request.thumbnailTransformKey);
        if (cached.state == ThumbnailMemoryCache::AcquireState::Hit) {
            attachThumbnail(row, cached.handle.providerId);
            continue;
        }
        _pendingThumbnailRequests.insert(key, PendingThumbnailRequest{
            .owner = cached.state ==
                ThumbnailMemoryCache::AcquireState::Owner,
            .admittedTargetSize = cached.pendingTargetSize.isValid()
                ? cached.pendingTargetSize : request.targetSize,
            .admittedTransformKey =
                cached.pendingTransformKey.isEmpty()
                    ? request.thumbnailTransformKey
                    : cached.pendingTransformKey,
        });
        if (cached.state == ThumbnailMemoryCache::AcquireState::Owner) {
            scopedRequests.append(request);
        }
    }
    if (!scopedRequests.isEmpty()) {
        _decodeManager->decodeImages(scopedRequests);
    }
}

void ExternalCatalogModel::requestImageMetadata(
    const QList<int> &rows, bool highPriority, bool catalogWide) {
    if (_shutdown) {
        return;
    }

    const auto normalizedRows = [this](const QList<int> &requested) {
        QList<int> result;
        result.reserve(requested.size());
        QSet<int> seen;
        for (const int row : requested) {
            if (!validRow(row) || seen.contains(row)) {
                continue;
            }
            seen.insert(row);
            result.append(row);
        }
        return result;
    };

    const QList<int> normalized = normalizedRows(rows);

    // Masonry sends an additional empty catalogWide request after its
    // visible and overscan requests. Do not let that marker erase the active
    // overscan window.
    if (!catalogWide || !rows.isEmpty()) {
        if (highPriority) {
            _metadataVisibleRows = normalized;
        }
        else {
            _metadataOverscanRows = normalized;
        }
        enqueueProbeRows(normalized, highPriority);
    }
    // Every renderer pass starts with its visible request. Fixed modes never
    // send the trailing catalogWide marker, so this pauses (without losing
    // the scan cursor) any Masonry background walk after a mode switch.
    // Masonry sends catalogWide synchronously before the zero-delay pump and
    // therefore re-enables the same generation without churn.
    _catalogMetadataRequested = catalogWide;
    if (catalogWide) {
        // A direct-local catalog keeps the historical viewport-first
        // metadata pipeline.  Starting an otherwise empty probe pass here
        // would call beginCatalogFitPass(), emit a catalog-wide dataChanged,
        // and let that follow-up layout pass revoke the Masonry metadata
        // lease.  The embedded-first barrier is exclusively a policy for
        // sources whose bytes are expensive to obtain.
        _catalogProbeRequested = std::any_of(
            _entries.cbegin(), _entries.cend(), [](const Entry &entry) {
                return entry.image && entry.source.isValid() &&
                    expensiveSource(entry.source);
            });
    }
    scheduleProbePump();
    const bool hasProbeEligibleRow = std::any_of(
        normalized.cbegin(), normalized.cend(), [this](int row) {
            return validRow(row) && probeResolvedFor(_entries.at(row));
        });
    if (!_catalogProbeRequested || _probePassComplete ||
        hasProbeEligibleRow) {
        scheduleMetadataPump();
    }
}

void ExternalCatalogModel::cancelAllRunners() {
    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    resetProgressivePipeline();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _sourceDecodeRetryAttempts.clear();
    _sourceDecodeRetryScheduled.clear();
    _backgroundDecodeRetries.clear();
    _backgroundMetadataRetries.clear();
    _backgroundRetryTimer.stop();
    invalidateNativeDwell();
}

void ExternalCatalogModel::cancelAllDecodeRunners() {
    // MasonryLayout calls this before replacing DPR/geometry-specific tiles.
    // Expensive source reads/materializations remain reusable across a small
    // layout or DPR change. Their stale presentation subscriber is forgotten
    // below, while completion may still populate a covering tier. Local files
    // retain the historical aggressive cancellation policy.
    const bool hasExpensiveSource = std::any_of(
        _entries.cbegin(), _entries.cend(), [](const Entry &entry) {
            return entry.image && expensiveSource(entry.source);
        });
    if (!hasExpensiveSource) {
        _decodeManager->cancelThumbnailRequests(_sessionId);
        if (_thumbnailCache) {
            _thumbnailCache->cancelRequests(_sessionId);
        }
    }
    // When expensive work remains alive, its shared-cache Owner admission must
    // remain alive with it. Dropping only that admission would let the same
    // stable tier become Owner again and launch a duplicate decode after every
    // geometry/DPR lease change. This map is merely the stale local waiter;
    // the completing owner will still publish into ThumbnailMemoryCache.
    _pendingThumbnailRequests.clear();
}

bool ExternalCatalogModel::preserveViewStateOnReset() const {
    return true;
}

void ExternalCatalogModel::shutdown() {
    if (_shutdown) {
        return;
    }
    _shutdown = true;
    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    resetProgressivePipeline();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _sourceDecodeRetryAttempts.clear();
    _sourceDecodeRetryScheduled.clear();
    _backgroundDecodeRetries.clear();
    _backgroundMetadataRetries.clear();
    _backgroundRetryTimer.stop();
    _viewerPlans.clear();
    clearViewer();
    _viewerImageCache.clear();
    for (Entry &entry : _entries) {
        clearPublishedImage(entry);
    }
}

void ExternalCatalogModel::handleImageInfo(const ImageInfo &info) {
    if (_shutdown || info.requestNamespace != _sessionId) {
        return;
    }
    const QString sourceIdentity = info.sourceIdentity();
    const auto pending = _metadataPendingVersions.constFind(sourceIdentity);
    if (pending != _metadataPendingVersions.cend() &&
        pending.value() == info.sourceVersionToken) {
        _metadataPendingVersions.remove(sourceIdentity);
    }
    const int row = _sourceToRow.value(sourceIdentity, -1);
    if (!validRow(row)) {
        scheduleMetadataPump();
        return;
    }
    Entry &entry = _entries[row];
    if (!entry.image || entry.contentVersion != info.sourceVersionToken ||
        !sourceAuthorityMatches(entry.source, info.source)) {
        scheduleMetadataPump();
        return;
    }
    if (info.sourceAccessFailed) {
        // Network/offline/timeout is not evidence that this immutable source
        // lacks metadata. Release the admission slot and retry with bounded
        // backoff; an explicit viewer/visible request may still retry sooner.
        const bool background = !info.highPriority;
        scheduleMetadataRetry(sourceIdentity, entry.contentVersion,
                              entry.source.resourceId, background);
        scheduleMetadataPump();
        return;
    }
    // A corrupt/unsupported image still completed its metadata probe. Keep
    // that terminal result for this catalog generation so synchronous
    // dataChanged/re-layout cycles cannot enqueue the same failed header
    // read forever. A stale completion from an older catalog version never
    // marks the replacement entry resolved.
    _metadataResolvedPaths.insert(sourceIdentity);
    const QString retryKey = sourceIdentity + QChar(0x1f) +
        entry.contentVersion;
    _metadataRetryAttempts.remove(retryKey);
    _metadataRetryScheduled.remove(retryKey);
    _backgroundMetadataRetries.remove(retryKey);
    if (!versionMatches(entry.contentVersion, entry.mtimeNs,
                        entry.size, info)) {
        scheduleMetadataPump();
        return;
    }
    ImageInfo currentInfo = info;
    if (entry.mtimeNs != 0) {
        currentInfo.lastModified = QDateTime::fromMSecsSinceEpoch(
            entry.mtimeNs / 1000000, QTimeZone::UTC);
    }
    currentInfo.source = entry.source;
    currentInfo.path = entry.sourceIdentity;
    currentInfo.sourceVersionToken = entry.contentVersion;
    if (entry.size >= 0) {
        currentInfo.fileSize = entry.size;
    }
    entry.item->setInfo(currentInfo);
    entry.item->setFullSize(
        rotateToOrientation(currentInfo.imageSize, currentInfo.orientation));
    QList<int> roles{FileListModel::ImageFullSizeRole};
    if (info.isLast) {
        roles.append(FileListModel::TimeToFlushRole);
    }
    emit dataChanged(index(row), index(row), roles);
    if (_catalogFitWaitingMetadata.remove(sourceIdentity) &&
        !_catalogFitResolvedSources.contains(sourceIdentity)) {
        _catalogFitRows.prepend(row);
        scheduleCatalogFitPump();
    }
    const auto plans = _viewerPlans;
    for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
        const int centerRow = rowForEntryId(plan.key());
        if (validRow(centerRow)) {
            scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                   plan->prefetchCount);
        }
    }
    scheduleMetadataPump();
}

void ExternalCatalogModel::handleImageReady(
    const ImageDecodeRequest &request, const QImage &image,
    const DecodedImageInfo &decodedInfo) {
    if (request.requestNamespace != _sessionId) {
        return;
    }
    const int authorityRow = _sourceToRow.value(
        request.info.sourceIdentity(), -1);
    if (!validRow(authorityRow) ||
        !sourceAuthorityMatches(_entries.at(authorityRow).source,
                                request.info.source)) {
        return;
    }
    if (request.viewerRequest) {
        const QString retryKey = QStringLiteral("viewer:") +
            viewerRequestKey(request);
        _sourceDecodeRetryAttempts.remove(retryKey);
        _sourceDecodeRetryScheduled.remove(retryKey);
        _backgroundDecodeRetries.remove(retryKey);
        _pendingViewerRequests.remove(viewerRequestKey(request));
        completeCatalogFitRequest(request);
    }
    else {
        const QString retryKey = QStringLiteral("thumbnail:") +
            thumbnailRequestKey(request);
        _sourceDecodeRetryAttempts.remove(retryKey);
        _sourceDecodeRetryScheduled.remove(retryKey);
        _pendingThumbnailRequests.remove(thumbnailRequestKey(request));
    }
    if (_shutdown) {
        return;
    }
    if (image.isNull()) {
        if (!request.viewerRequest) {
            releaseFailedThumbnailRequest(request);
        }
        return;
    }
    const int row = _sourceToRow.value(
        request.info.sourceIdentity(), -1);
    if (!validRow(row)) {
        return;
    }
    Entry &entry = _entries[row];
    if (!entry.image ||
        !sourceAuthorityMatches(entry.source, request.info.source) ||
        !versionMatches(entry.contentVersion, entry.mtimeNs,
                        entry.size, request.info)) {
        return;
    }
    if (request.viewerRequest) {
        const ViewerImageCache::StoredImage stored =
            _viewerImageCache.storeDecodedImage(request, image, decodedInfo);
        if (stored.presentable) {
            emit viewerSourceAtChanged(row);
            if (entry.id == _viewerEntryId) {
                notifyViewerImageUrlChanged();
            }
        }
        if (request.fitToViewerRequest) {
            tryScheduleDeferredNative();
        }
        return;
    }
    if (_thumbnailCache) {
        // Normally MasonryLayout records the desired geometry before the
        // asynchronous result arrives. Keep direct/test-injected results and
        // legacy callers presentable too, without replacing a newer target
        // established while an older decode was still running.
        if (!entry.thumbnailRequestedSize.isValid()) {
            entry.thumbnailRequestedSize = request.targetSize;
            entry.thumbnailTransformKey = thumbnailTransformKey(request);
        }
        _thumbnailCache->storeDecoded(
            _sessionId, entry.sourceIdentity, entry.contentVersion,
            entry.size, request.targetSize,
            thumbnailTransformKey(request), image);
    }
}

void ExternalCatalogModel::handleImageReadFailed(
    const ImageDecodeRequest &request) {
    if (_shutdown || request.requestNamespace != _sessionId) {
        return;
    }
    const int authorityRow = _sourceToRow.value(
        request.info.sourceIdentity(), -1);
    if (!validRow(authorityRow) ||
        !sourceAuthorityMatches(_entries.at(authorityRow).source,
                                request.info.source)) {
        return;
    }
    if (request.viewerRequest) {
        const QString key = viewerRequestKey(request);
        _pendingViewerRequests.remove(key);
        if (request.sourceAccessFailed) {
            // Free the bounded Fit slot but do not mark this immutable source
            // complete: a transport timeout says nothing about its pixels.
            _catalogFitPendingKeys.remove(key);
            scheduleCatalogFitPump();
            scheduleSourceDecodeRetry(request);
        }
        else {
            completeCatalogFitRequest(request);
        }
        return;
    }

    releaseFailedThumbnailRequest(request);
    if (request.sourceAccessFailed && request.highPriority) {
        scheduleSourceDecodeRetry(request);
    }
}

void ExternalCatalogModel::releaseFailedThumbnailRequest(
    const ImageDecodeRequest &request, bool retryWaiters) {
    const QString requestKey = thumbnailRequestKey(request);
    _pendingThumbnailRequests.remove(requestKey);
    if (!_thumbnailCache) {
        return;
    }

    // requestReleased normally wakes coalesced sessions so they can elect a
    // new owner after cancellation. A decode failure is different: waking the
    // visible-item planner synchronously would submit the same corrupt frame
    // forever. The shared retryWaiters=false reason clears admission state in
    // every session without allowing cross-session failure ping-pong.
    // The shared admission key uses the host-authoritative source size. A
    // materialized file may discover an actual QFile size while the host
    // descriptor intentionally remains unknown (-1); mixing those values
    // would leave the original single-flight owner pending forever.
    const qint64 admittedSourceSize = request.info.source.isValid()
        ? request.info.source.size : request.info.fileSize;
    _thumbnailCache->releaseRequest(
        _sessionId, request.info.sourceIdentity(),
        request.info.sourceVersionToken, admittedSourceSize,
        request.targetSize, thumbnailTransformKey(request), retryWaiters);
}

void ExternalCatalogModel::scheduleSourceDecodeRetry(
    const ImageDecodeRequest &request) {
    const QString retryKey =
        (request.viewerRequest ? QStringLiteral("viewer:")
                               : QStringLiteral("thumbnail:")) +
        (request.viewerRequest ? viewerRequestKey(request)
                               : thumbnailRequestKey(request));
    if (_sourceDecodeRetryScheduled.contains(retryKey)) {
        return;
    }
    const int MaxAutomaticAttempts =
        request.backgroundViewerRequest ? 1 : 3;
    const int attempt = _sourceDecodeRetryAttempts.value(retryKey) + 1;
    if (attempt > MaxAutomaticAttempts) {
        return;
    }
    _sourceDecodeRetryAttempts.insert(retryKey, attempt);
    _sourceDecodeRetryScheduled.insert(retryKey);
    const int delayMs = static_cast<int>(qMin<qint64>(
        30000, 500LL << qMin(attempt - 1, 6)));
    if (request.backgroundViewerRequest) {
        _backgroundDecodeRetries.insert(retryKey, BackgroundDecodeRetry{
            .request = request,
            .notBeforeMs = QDateTime::currentMSecsSinceEpoch() + delayMs,
        });
        scheduleBackgroundRetryWake();
        return;
    }
    const QString sourceIdentity = request.info.sourceIdentity();
    const QString contentVersion = request.info.sourceVersionToken;
    const QString resourceId = request.info.source.resourceId;
    const bool viewerRequest = request.viewerRequest;
    const bool backgroundViewerRequest = request.backgroundViewerRequest;
    QTimer::singleShot(
        delayMs, this,
        [this, retryKey, sourceIdentity, contentVersion, resourceId,
         viewerRequest, backgroundViewerRequest]() {
        _sourceDecodeRetryScheduled.remove(retryKey);
        if (_shutdown) {
            return;
        }
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row) ||
            _entries.at(row).contentVersion != contentVersion ||
            _entries.at(row).source.resourceId != resourceId) {
            _sourceDecodeRetryAttempts.remove(retryKey);
            return;
        }
        if (!viewerRequest) {
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageIdUrlRole});
            return;
        }
        if (backgroundViewerRequest && _catalogFitStarted &&
            !_catalogFitResolvedSources.contains(sourceIdentity) &&
            !_catalogFitRows.contains(row)) {
            _catalogFitRows.prepend(row);
            scheduleCatalogFitPump();
        }
        const auto plans = _viewerPlans;
        for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
            const int centerRow = rowForEntryId(plan.key());
            if (validRow(centerRow)) {
                scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                       plan->prefetchCount);
            }
        }
    });
}

void ExternalCatalogModel::clearPublishedImage(Entry &entry) {
    if (!entry.item) {
        return;
    }
    detachThumbnail(entry);
}

void ExternalCatalogModel::attachThumbnail(
    int row, const QString &providerId) {
    if (!validRow(row) || providerId.isEmpty()) {
        return;
    }
    Entry &entry = _entries[row];
    if (entry.thumbnailProviderId == providerId &&
        entry.item && entry.item->imageIdUrl().endsWith(providerId)) {
        return;
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        _providerEntryIds.remove(entry.thumbnailProviderId, entry.id);
    }
    else if (entry.item) {
        // Remove only a legacy per-session publication. Shared cache IDs are
        // owned solely by ThumbnailMemoryCache and remain valid for users in
        // the other panel.
        const QString legacyId = entry.item->imageIdUrl().section(
            QLatin1Char('/'), -1);
        if (!legacyId.isEmpty()) {
            _store->remove(legacyId);
        }
    }
    entry.thumbnailProviderId = providerId;
    _providerEntryIds.insert(providerId, entry.id);
    entry.item->setImage({}, {});
    entry.item->setImageId(providerId);
    emit dataChanged(index(row), index(row),
                     {FileListModel::ImageIdUrlRole});
    emit viewerSourceAtChanged(row);
    if (entry.id == _viewerEntryId &&
        _viewerImageCache.bestImageUrl(entry.item) ==
            entry.item->imageIdUrl()) {
        notifyViewerImageUrlChanged();
    }
}

void ExternalCatalogModel::detachThumbnail(Entry &entry) {
    if (!entry.item) {
        return;
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        _providerEntryIds.remove(entry.thumbnailProviderId, entry.id);
    }
    else {
        const QString legacyId = entry.item->imageIdUrl().section(
            QLatin1Char('/'), -1);
        if (!legacyId.isEmpty()) {
            _store->remove(legacyId);
        }
    }
    entry.thumbnailProviderId.clear();
    entry.item->setImageId({});
    entry.item->setImage({}, {});
}

bool ExternalCatalogModel::adoptCachedThumbnail(int row) {
    if (!_thumbnailCache || !validRow(row)) {
        return false;
    }
    const Entry &entry = _entries.at(row);
    if (!entry.image || !entry.thumbnailRequestedSize.isValid()) {
        return false;
    }
    const ThumbnailMemoryCache::Handle cached = _thumbnailCache->lookup(
        entry.sourceIdentity, entry.contentVersion, entry.size,
        entry.thumbnailRequestedSize, entry.thumbnailTransformKey);
    if (!cached.isValid()) {
        return false;
    }
    attachThumbnail(row, cached.providerId);
    return true;
}

void ExternalCatalogModel::handleThumbnailFrameAvailable(
    const QString &sourceIdentity, const QString &versionToken,
    qint64 sourceFileSize, const QSize &requestedSize,
    const QString &transformKey, const QString &providerId) {
    if (_shutdown) {
        return;
    }
    const QList<QString> entryIds =
        _sourceEntryIds.values(sourceIdentity);
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = _entries[row];
        if (entry.contentVersion != versionToken ||
            (entry.size >= 0 && sourceFileSize >= 0 &&
             entry.size != sourceFileSize)) {
            continue;
        }
        ImageDecodeRequest desired;
        desired.info = entry.item->info();
        desired.targetSize = entry.thumbnailRequestedSize;
        desired.thumbnailTransformKey = entry.thumbnailTransformKey;
        const QString desiredKey = thumbnailRequestKey(desired);
        const QString completedTransform =
            normalizedThumbnailTransformKey(transformKey);
        const auto pending = _pendingThumbnailRequests.constFind(desiredKey);
        const bool exactCurrentCompletion =
            requestedSize == desired.targetSize &&
            completedTransform == thumbnailTransformKey(desired);
        if (pending == _pendingThumbnailRequests.constEnd()) {
            // The exact request owner removes its local admission record in
            // handleImageReady immediately before storeDecoded emits this
            // synchronous completion. A session with no related request must
            // not consume another tier's completion.
            if (!exactCurrentCompletion) {
                continue;
            }
        }
        else {
            const bool completionMatchesAdmission =
                pending->admittedTargetSize == requestedSize &&
                normalizedThumbnailTransformKey(
                    pending->admittedTransformKey) == completedTransform;
            if (!completionMatchesAdmission) {
                // A genuinely active owner for another tier stays pending
                // until its own imageReady. This matters when a small request
                // was admitted first and a later larger request completes
                // sooner.
                continue;
            }
            if (pending->owner) {
                // Only this owner's own imageReady removes the record before
                // its synchronous storeDecoded completion. A frame from a
                // different request/session must never consume a genuinely
                // active owner, even when both happen to use the same tier.
                continue;
            }
            _pendingThumbnailRequests.erase(pending);
        }

        if (exactCurrentCompletion &&
            !providerId.isEmpty()) {
            // The provider is the authoritative result of this exact target,
            // even when an aspect-preserving decoder returns (for example)
            // 24x32 for a 32x32 bounding request. Equal-target waiters in all
            // sessions attach it directly; cross-tier reuse remains strict.
            attachThumbnail(row, providerId);
            continue;
        }
        if (!adoptCachedThumbnail(row) &&
            entry.thumbnailProviderId.isEmpty()) {
            // The completed bounding request did not yield enough pixels for
            // this different tier. Let this waiter acquire and decode its own
            // exact target instead of weakening cache coverage semantics.
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageIdUrlRole});
        }
    }
}

void ExternalCatalogModel::handleThumbnailFrameEvicted(
    const QString &providerId) {
    if (_shutdown || providerId.isEmpty()) {
        return;
    }
    const QList<QString> entryIds =
        _providerEntryIds.values(providerId);
    _providerEntryIds.remove(providerId);
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = _entries[row];
        if (entry.thumbnailProviderId != providerId) {
            continue;
        }
        entry.thumbnailProviderId.clear();
        entry.item->setImageId({});
        entry.item->setImage({}, {});
        emit dataChanged(index(row), index(row),
                         {FileListModel::ImageIdUrlRole});
        emit viewerSourceAtChanged(row);
        if (entry.id == _viewerEntryId) {
            notifyViewerImageUrlChanged();
        }
    }
}

void ExternalCatalogModel::handleThumbnailRequestReleased(
    const QString &sourceIdentity, const QString &versionToken,
    qint64 sourceFileSize, const QSize &requestedSize,
    const QString &transformKey, bool retryWaiters) {
    if (_shutdown) {
        return;
    }
    const QList<QString> entryIds =
        _sourceEntryIds.values(sourceIdentity);
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = _entries[row];
        if (entry.contentVersion != versionToken ||
            (entry.size >= 0 && sourceFileSize >= 0 &&
             entry.size != sourceFileSize) ||
            !entry.thumbnailRequestedSize.isValid()) {
            continue;
        }
        ImageDecodeRequest desired;
        desired.info = entry.item->info();
        desired.targetSize = entry.thumbnailRequestedSize;
        desired.thumbnailTransformKey = entry.thumbnailTransformKey;
        const QString desiredKey = thumbnailRequestKey(desired);
        const auto pending = _pendingThumbnailRequests.constFind(desiredKey);
        if (pending == _pendingThumbnailRequests.constEnd() ||
            pending->admittedTargetSize != requestedSize ||
            normalizedThumbnailTransformKey(
                pending->admittedTransformKey) !=
                normalizedThumbnailTransformKey(transformKey)) {
            continue;
        }
        _pendingThumbnailRequests.erase(pending);
        if (entry.thumbnailProviderId.isEmpty() && retryWaiters) {
            // The viewport planner treats an empty ImageIdUrlRole update as a
            // request to re-plan visible/overscan work after owner cancel.
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageIdUrlRole});
        }
    }
}

void ExternalCatalogModel::enqueueProbeRows(
    const QList<int> &rows, bool highPriority) {
    QList<int> &queue = highPriority ? _probeVisibleRows
                                     : _probeOverscanRows;
    for (const int row : rows) {
        if (!validRow(row)) {
            continue;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image || !entry.source.isValid() ||
            !expensiveSource(entry.source)) {
            continue;
        }
        const QString cacheKey = entry.source.cacheKey();
        if (_probeRetryableSources.contains(cacheKey)) {
            // A transient transport failure is not a negative-cache entry.
            // Retry only after visibility/user intent, never merely because a
            // layout emitted dataChanged again.
            if (_probeRetryNotBeforeMs.value(cacheKey) >
                QDateTime::currentMSecsSinceEpoch()) {
                continue;
            }
            _probeResolvedVersions.remove(entry.sourceIdentity);
            _probeRetryableSources.remove(cacheKey);
        }
        if (!queue.contains(row)) {
            queue.append(row);
        }
    }
}

bool ExternalCatalogModel::probeResolvedFor(const Entry &entry) const {
    return !entry.image || !entry.source.isValid() ||
        !expensiveSource(entry.source) ||
        _probeResolvedVersions.value(entry.sourceIdentity) ==
            entry.contentVersion;
}

bool ExternalCatalogModel::probeBarrierReached() const {
    for (const Entry &entry : _entries) {
        if (entry.image && entry.source.isValid() &&
            expensiveSource(entry.source) &&
            !probeResolvedFor(entry)) {
            return false;
        }
    }
    return true;
}

void ExternalCatalogModel::scheduleProbePump() {
    const bool retryWaiting = !_probeUrgentRows.isEmpty() ||
        !_probeVisibleRows.isEmpty() || !_probeOverscanRows.isEmpty();
    if (_shutdown || _probePumpScheduled ||
        (_probePassComplete && !retryWaiting)) {
        return;
    }
    _probePumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        _probePumpScheduled = false;
        pumpProbeRequests();
    });
}

void ExternalCatalogModel::pumpProbeRequests() {
    const bool retryWaiting = !_probeUrgentRows.isEmpty() ||
        !_probeVisibleRows.isEmpty() || !_probeOverscanRows.isEmpty();
    if (_shutdown || (_probePassComplete && !retryWaiting)) {
        return;
    }

    qsizetype available = probeRequestLimit() -
        _probePendingVersions.size();
    if (available <= 0) {
        return;
    }
    const bool foregroundWaiting = !_probeUrgentRows.isEmpty() ||
        !_probeVisibleRows.isEmpty();
    if (!foregroundWaiting &&
        _probePendingVersions.size() > probeRefillLowWatermark()) {
        return;
    }

    QList<ImageProbeRequest> highRequests;
    QList<ImageProbeRequest> backgroundRequests;
    const auto appendRow = [this, &available](
                               int row, bool highPriority,
                               QList<ImageProbeRequest> &requests) {
        if (available <= 0 || !validRow(row)) {
            return;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image || !entry.source.isValid() ||
            probeResolvedFor(entry) ||
            _probePendingVersions.contains(entry.sourceIdentity)) {
            return;
        }
        _probePendingVersions.insert(
            entry.sourceIdentity, entry.contentVersion);
        requests.append(ImageProbeRequest{
            .source = entry.source,
            .requestNamespace = _sessionId,
            .highPriority = highPriority,
        });
        --available;
    };
    const auto drain = [&appendRow, &available](
                           QList<int> &rows, bool highPriority,
                           QList<ImageProbeRequest> &requests) {
        while (available > 0 && !rows.isEmpty()) {
            appendRow(rows.takeFirst(), highPriority, requests);
        }
    };

    drain(_probeUrgentRows, true, highRequests);
    drain(_probeVisibleRows, true, highRequests);
    drain(_probeOverscanRows, false, backgroundRequests);
    if (_catalogProbeRequested) {
        while (available > 0 && _catalogProbeCursor < _entries.size()) {
            appendRow(_catalogProbeCursor++, false, backgroundRequests);
        }
    }

    if (!highRequests.isEmpty()) {
        _decodeManager->probeImages(highRequests);
    }
    if (!backgroundRequests.isEmpty()) {
        _decodeManager->probeImages(backgroundRequests);
    }

    if (_catalogProbeRequested &&
        _catalogProbeCursor >= _entries.size() &&
        _probePendingVersions.isEmpty() && probeBarrierReached()) {
        _probePassComplete = true;
        beginCatalogFitPass();
    }
}

void ExternalCatalogModel::handleImageProbe(
    const ImageProbeResult &result) {
    if (_shutdown || result.request.requestNamespace != _sessionId) {
        return;
    }
    const QString sourceIdentity =
        result.request.source.runtimeIdentity();
    const QString version = result.request.source.contentVersion;
    const auto pending = _probePendingVersions.constFind(sourceIdentity);
    if (pending == _probePendingVersions.cend() ||
        pending.value() != version) {
        return;
    }
    const int authorityRow = _sourceToRow.value(sourceIdentity, -1);
    if (!validRow(authorityRow) ||
        !sourceAuthorityMatches(_entries.at(authorityRow).source,
                                result.request.source)) {
        return;
    }
    _probePendingVersions.remove(sourceIdentity);
    _probeResolvedVersions.insert(sourceIdentity, version);
    const QString cacheKey = result.request.source.cacheKey();
    if (result.status == ImageSourceProbeStatus::Failed) {
        _probeRetryableSources.insert(cacheKey);
        const int attempt = qMin(
            7, _probeRetryAttempts.value(cacheKey) + 1);
        _probeRetryAttempts.insert(cacheKey, attempt);
        const qint64 delayMs = qMin<qint64>(
            30000, 500LL << qMin(attempt - 1, 6));
        _probeRetryNotBeforeMs.insert(
            cacheKey, QDateTime::currentMSecsSinceEpoch() + delayMs);
        if (result.request.highPriority && attempt <= 3) {
            const QString resourceId =
                result.request.source.resourceId;
            QTimer::singleShot(
                static_cast<int>(delayMs), this,
                [this, sourceIdentity, version, resourceId, cacheKey]() {
                if (_shutdown ||
                    !_probeRetryableSources.contains(cacheKey)) {
                    return;
                }
                const int row = _sourceToRow.value(sourceIdentity, -1);
                if (!validRow(row)) {
                    return;
                }
                const Entry &entry = _entries.at(row);
                if (entry.contentVersion != version ||
                    entry.source.resourceId != resourceId) {
                    return;
                }
                enqueueProbeRows({row}, true);
                scheduleProbePump();
            });
        }
    }
    else {
        _probeRetryableSources.remove(cacheKey);
        _probeRetryAttempts.remove(cacheKey);
        _probeRetryNotBeforeMs.remove(cacheKey);
    }

    const QList<QString> entryIds =
        _sourceEntryIds.values(sourceIdentity);
    ThumbnailMemoryCache::Handle provisional;
    if (result.found() && _thumbnailCache) {
        provisional = _thumbnailCache->storeDecoded(
            _sessionId, sourceIdentity, version,
            result.request.source.size, result.preview.size(),
            QString::fromLatin1(EmbeddedProvisionalTransform),
            result.preview);
    }
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = _entries[row];
        if (entry.contentVersion != version) {
            continue;
        }
        if (result.sourceSize.isValid()) {
            ImageInfo info = entry.item->info();
            info.source = entry.source;
            info.path = entry.sourceIdentity;
            info.sourceVersionToken = entry.contentVersion;
            info.imageSize = result.sourceSize;
            if (result.orientation >= ExifOrientation::Horizontal &&
                result.orientation <= ExifOrientation::Rotate270CW) {
                info.orientation =
                    static_cast<ExifOrientation>(result.orientation);
            }
            entry.item->setInfo(info);
            entry.item->setFullSize(rotateToOrientation(
                info.imageSize, info.orientation));
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageFullSizeRole});
        }
        if (provisional.isValid() &&
            entry.thumbnailProviderId.isEmpty()) {
            attachThumbnail(row, provisional.providerId);
        }
    }

    const bool completedCatalogProbePass = _catalogProbeRequested &&
        _catalogProbeCursor >= _entries.size() &&
        _probePendingVersions.isEmpty() && probeBarrierReached();
    if (completedCatalogProbePass) {
        // Set the barrier before replanning the viewer. Its retained 16-item
        // intent may now expand from current-only to neighbor prefetch.
        _probePassComplete = true;
    }

    // An explicit viewer request waits only for its own bounded probe; it may
    // now bypass the catalog barrier and materialize current Fit.
    const auto plans = _viewerPlans;
    for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
        const int centerRow = rowForEntryId(plan.key());
        if (validRow(centerRow)) {
            scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                   plan->prefetchCount);
        }
    }
    scheduleMetadataPump();
    scheduleProbePump();
    if (completedCatalogProbePass) {
        beginCatalogFitPass();
    }
}

void ExternalCatalogModel::resetProgressivePipeline() {
    _probePendingVersions.clear();
    _probeResolvedVersions.clear();
    _probeRetryableSources.clear();
    _probeRetryAttempts.clear();
    _probeRetryNotBeforeMs.clear();
    _probeVisibleRows.clear();
    _probeOverscanRows.clear();
    _probeUrgentRows.clear();
    _catalogProbeCursor = 0;
    _catalogProbeRequested = false;
    _probePumpScheduled = false;
    _probePassComplete = false;
    _catalogFitRows.clear();
    _catalogFitPendingKeys.clear();
    _catalogFitResolvedSources.clear();
    _catalogFitWaitingMetadata.clear();
    _catalogFitStarted = false;
    _catalogFitPumpScheduled = false;
    _deferredNativeEntryId.clear();
}

void ExternalCatalogModel::beginCatalogFitPass() {
    if (_shutdown || _catalogFitStarted) {
        return;
    }
    _catalogFitStarted = true;
    QSet<int> seen;
    const auto append = [this, &seen](int row) {
        if (validRow(row) && _entries.at(row).image &&
            !seen.contains(row)) {
            seen.insert(row);
            _catalogFitRows.append(row);
        }
    };
    const int viewerRow = rowForEntryId(_viewerEntryId);
    for (const int row : viewerCandidateRows(viewerRow, 16)) {
        append(row);
    }
    for (const int row : std::as_const(_metadataVisibleRows)) {
        if (validRow(row) && expensiveSource(_entries.at(row).source)) {
            append(row);
        }
    }
    for (const int row : std::as_const(_metadataOverscanRows)) {
        if (validRow(row) && expensiveSource(_entries.at(row).source)) {
            append(row);
        }
    }
    for (int row = 0; row < _entries.size(); ++row) {
        if (expensiveSource(_entries.at(row).source)) {
            append(row);
        }
    }

    if (!_entries.isEmpty()) {
        // Wake rows which intentionally deferred their normal thumbnail
        // decode while pass 1 was still filling provisional frames.
        emit dataChanged(index(0), index(_entries.size() - 1),
                         {FileListModel::ImageIdUrlRole});
    }
    scheduleMetadataPump();
    scheduleCatalogFitPump();
    tryScheduleDeferredNative();
}

ImageDecodeRequest ExternalCatalogModel::catalogFitRequestForRow(
    int row) const {
    if (!validRow(row)) {
        return {};
    }
    const Entry &entry = _entries.at(row);
    if (!entry.image || !entry.item ||
        !entry.item->fullSize().isValid()) {
        return {};
    }
    ImageDecodeRequest request = ViewerImageCache::makeRequest(
        entry.item->info(), entry.item->fullSize(),
        _lastViewerFitViewportSize);
    stabilizeViewerFitRequest(request);
    request.requestNamespace = _sessionId;
    request.info.requestNamespace = _sessionId;
    request.info.source = entry.source;
    request.info.path = entry.sourceIdentity;
    request.info.sourceVersionToken = entry.contentVersion;
    request.viewerRequest = true;
    request.backgroundViewerRequest = true;
    request.fitToViewerRequest = true;
    request.checkCache = true;
    request.expandToCacheResolution = false;
    request.storeInPersistentCache = true;
    return request;
}

void ExternalCatalogModel::stabilizeViewerFitRequest(
    ImageDecodeRequest &request) const {
    if (!request.fitToViewerRequest || !request.targetSize.isValid()) {
        return;
    }
    const int row = _sourceToRow.value(
        request.info.sourceIdentity(), -1);
    if (!validRow(row)) {
        return;
    }
    const Entry &entry = _entries.at(row);
    const QSize previous = _viewerImageCache.entryForPath(
        entry.sourceIdentity, false).requestedSize;
    request.targetSize = stableDecodeTarget(
        request.targetSize,
        entry.item ? entry.item->fullSize() : QSize(), previous,
        DecodeSizeFamily::ViewerFit, expensiveSource(entry.source));
}

void ExternalCatalogModel::scheduleCatalogFitPump() {
    if (_shutdown || !_catalogFitStarted ||
        _catalogFitPumpScheduled) {
        return;
    }
    _catalogFitPumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        _catalogFitPumpScheduled = false;
        pumpCatalogFitRequests();
    });
}

void ExternalCatalogModel::pumpCatalogFitRequests() {
    if (_shutdown || !_catalogFitStarted) {
        return;
    }
    qsizetype available = catalogFitRequestLimit() -
        _catalogFitPendingKeys.size();
    if (available <= 0) {
        return;
    }

    QList<ImageDecodeRequest> requests;
    while (available > 0 && !_catalogFitRows.isEmpty()) {
        const int row = _catalogFitRows.takeFirst();
        if (!validRow(row)) {
            continue;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image ||
            _catalogFitResolvedSources.contains(entry.sourceIdentity)) {
            continue;
        }
        if (!entry.item || !entry.item->fullSize().isValid()) {
            if (_metadataResolvedPaths.contains(entry.sourceIdentity)) {
                _catalogFitResolvedSources.insert(entry.sourceIdentity);
            }
            else {
                _catalogFitWaitingMetadata.insert(entry.sourceIdentity);
                requestImageMetadataForRow(row, false);
            }
            continue;
        }

        ImageDecodeRequest request = catalogFitRequestForRow(row);
        if (!request.targetSize.isValid() ||
            !_viewerImageCache.needsDecode(request)) {
            _catalogFitResolvedSources.insert(entry.sourceIdentity);
            continue;
        }
        const QString key = viewerRequestKey(request);
        _catalogFitPendingKeys.insert(key);
        if (!_pendingViewerRequests.contains(key)) {
            _pendingViewerRequests.insert(key);
            requests.append(request);
        }
        --available;
    }
    if (!requests.isEmpty()) {
        _decodeManager->decodeImages(requests);
    }
}

void ExternalCatalogModel::completeCatalogFitRequest(
    const ImageDecodeRequest &request) {
    const QString key = viewerRequestKey(request);
    if (!_catalogFitPendingKeys.remove(key)) {
        return;
    }
    _catalogFitResolvedSources.insert(request.info.sourceIdentity());
    scheduleCatalogFitPump();
    tryScheduleDeferredNative();
}

bool ExternalCatalogModel::viewerFitWindowReady(
    int row, int count, const QSize &viewportSize) const {
    if (!viewportSize.isValid() || viewportSize.isEmpty()) {
        return false;
    }
    const QList<int> candidates = viewerCandidateRows(row, count);
    if (candidates.isEmpty()) {
        return false;
    }
    for (const int candidateRow : candidates) {
        if (!validRow(candidateRow)) {
            continue;
        }
        const Entry &entry = _entries.at(candidateRow);
        if (!entry.item || !entry.item->fullSize().isValid()) {
            return false;
        }
        ImageDecodeRequest fit = ViewerImageCache::makeRequest(
            entry.item->info(), entry.item->fullSize(), viewportSize);
        stabilizeViewerFitRequest(fit);
        if (!fit.targetSize.isValid() ||
            _viewerImageCache.needsDecode(fit)) {
            return false;
        }
    }
    return true;
}

void ExternalCatalogModel::tryScheduleDeferredNative() {
    if (_shutdown || _deferredNativeEntryId.isEmpty()) {
        return;
    }
    const int row = rowForEntryId(_deferredNativeEntryId);
    if (!validRow(row) || _viewerEntryId != _deferredNativeEntryId ||
        !_viewerViewportSize.isEmpty()) {
        _deferredNativeEntryId.clear();
        invalidateNativeDwell();
        return;
    }
    constexpr int NativeWindow = 5; // current +/-2 images
    if (!viewerFitWindowReady(
            row, NativeWindow, _lastViewerFitViewportSize)) {
        if (_nativeDwellTimer.isActive()) {
            invalidateNativeDwell();
        }
        return;
    }
    if (_nativeDwellTimer.isActive() &&
        _scheduledNativeDwellGeneration == _nativeDwellGeneration &&
        _scheduledNativeDwellEntryId == _deferredNativeEntryId &&
        _scheduledNativeDwellFitViewportSize ==
            _lastViewerFitViewportSize) {
        return;
    }
    _scheduledNativeDwellGeneration = _nativeDwellGeneration;
    _scheduledNativeDwellEntryId = _deferredNativeEntryId;
    _scheduledNativeDwellFitViewportSize = _lastViewerFitViewportSize;
    _nativeDwellTimer.start(NativeDwellMs);
}

void ExternalCatalogModel::finishDeferredNativeDwell() {
    constexpr int NativeWindow = 5; // current +/-2 images
    const QString entryId = _scheduledNativeDwellEntryId;
    const QSize fitViewportSize = _scheduledNativeDwellFitViewportSize;
    const quint64 generation = _scheduledNativeDwellGeneration;
    _scheduledNativeDwellEntryId.clear();
    _scheduledNativeDwellFitViewportSize = {};
    if (_shutdown || generation != _nativeDwellGeneration ||
        entryId.isEmpty() || entryId != _deferredNativeEntryId ||
        entryId != _viewerEntryId || !_viewerViewportSize.isEmpty() ||
        fitViewportSize != _lastViewerFitViewportSize) {
        return;
    }
    const int row = rowForEntryId(entryId);
    if (!validRow(row) ||
        !viewerFitWindowReady(row, NativeWindow, fitViewportSize)) {
        return;
    }
    _deferredNativeEntryId.clear();
    const QSize nativeSentinel(0, 0);
    _viewerPlans.insert(entryId, {nativeSentinel, NativeWindow});
    scheduleViewerDecodeAt(row, nativeSentinel, NativeWindow);
}

void ExternalCatalogModel::invalidateNativeDwell() {
    ++_nativeDwellGeneration;
    _nativeDwellTimer.stop();
    _scheduledNativeDwellGeneration = 0;
    _scheduledNativeDwellEntryId.clear();
    _scheduledNativeDwellFitViewportSize = {};
}

void ExternalCatalogModel::requestImageMetadataForRow(
    int row, bool highPriority) {
    if (_shutdown || !validRow(row)) {
        return;
    }
    QList<int> &queue = highPriority ? _metadataUrgentRows
                                     : _metadataAdHocRows;
    if (!queue.contains(row)) {
        queue.append(row);
    }
    if (!probeResolvedFor(_entries.at(row))) {
        QList<int> &probeQueue = highPriority
            ? _probeUrgentRows : _probeOverscanRows;
        if (!probeQueue.contains(row)) {
            probeQueue.append(row);
        }
        scheduleProbePump();
    }
    // Before the catalog barrier only an explicit high-priority viewer row
    // may materialize. Masonry visible/overscan requests stay in their queues
    // until every bounded probe has produced an outcome.
    const Entry &entry = _entries.at(row);
    const bool explicitCurrentViewer = highPriority &&
        entry.id == _viewerEntryId;
    if (!_catalogProbeRequested || _probePassComplete ||
        (explicitCurrentViewer && probeResolvedFor(entry))) {
        scheduleMetadataPump();
    }
}

void ExternalCatalogModel::scheduleMetadataPump() {
    if (_shutdown || _metadataPumpScheduled) {
        return;
    }
    _metadataPumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        _metadataPumpScheduled = false;
        pumpMetadataRequests();
    });
}

void ExternalCatalogModel::pumpMetadataRequests() {
    if (_shutdown) {
        return;
    }

    const auto rowNeedsMetadata = [this](int row) {
        if (!validRow(row)) {
            return false;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image || !entry.source.isValid() ||
            (entry.item && entry.item->fullSize().isValid())) {
            return false;
        }
        return !_metadataPendingVersions.contains(entry.sourceIdentity) &&
            !_metadataResolvedPaths.contains(entry.sourceIdentity);
    };
    const auto hasEligibleRow = [&rowNeedsMetadata](const QList<int> &rows) {
        return std::any_of(rows.cbegin(), rows.cend(), rowNeedsMetadata);
    };

    const qsizetype pendingCount = _metadataPendingVersions.size();
    const bool foregroundWaiting =
        hasEligibleRow(_metadataUrgentRows) ||
        hasEligibleRow(_metadataVisibleRows);
    // Replenishing one catalog item after every completion would make each
    // request an `isLast` batch and repeatedly flush/rewrap Masonry. Keep the
    // pipeline half full, then admit one useful batch back to the hard limit.
    // A newly visible row bypasses this watermark and takes the first slot
    // released by background work.
    if (!foregroundWaiting &&
        pendingCount > metadataRefillLowWatermark()) {
        return;
    }

    qsizetype available = metadataRequestLimit() - pendingCount;
    if (available <= 0) {
        return;
    }

    QList<DecodeManager::VersionedImageInfoRequest> highRequests;
    QList<DecodeManager::VersionedImageInfoRequest> backgroundRequests;
    highRequests.reserve(available);
    backgroundRequests.reserve(available);

    const auto appendRow = [this, &available](
                               int row,
                               QList<DecodeManager::VersionedImageInfoRequest>
                                   &requests) {
        if (available <= 0 || !validRow(row)) {
            return;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image || !entry.source.isValid() ||
            (entry.item && entry.item->fullSize().isValid())) {
            return;
        }
        if (_metadataPendingVersions.contains(entry.sourceIdentity) ||
            _metadataResolvedPaths.contains(entry.sourceIdentity)) {
            return;
        }
        _metadataPendingVersions.insert(
            entry.sourceIdentity, entry.contentVersion);
        requests.append({entry.sourceIdentity, entry.contentVersion,
                         entry.source});
        --available;
    };

    const auto drain = [&appendRow, &available](
                           QList<int> &rows,
                           QList<DecodeManager::VersionedImageInfoRequest>
                               &requests) {
        while (available > 0 && !rows.isEmpty()) {
            appendRow(rows.takeFirst(), requests);
        }
    };
    const auto drainCurrentViewerProbeEligible =
        [this, &appendRow, &available](
            QList<int> &rows,
            QList<DecodeManager::VersionedImageInfoRequest> &requests) {
        int candidates = rows.size();
        while (available > 0 && candidates-- > 0 && !rows.isEmpty()) {
            const int row = rows.takeFirst();
            if (!validRow(row) ||
                _entries.at(row).id != _viewerEntryId ||
                !probeResolvedFor(_entries.at(row))) {
                rows.append(row);
                continue;
            }
            appendRow(row, requests);
        }
    };

    // The strict first-pass barrier has exactly one exception: the explicitly
    // opened current viewer image after its own bounded probe. Visible Masonry
    // rows, supplemental swipe plans, neighbors and ad-hoc background work
    // remain queued until every catalog probe has produced an outcome.
    if (_catalogProbeRequested && !_probePassComplete) {
        drainCurrentViewerProbeEligible(_metadataUrgentRows, highRequests);
        if (!highRequests.isEmpty()) {
            ++_metadataSubmittedBatches;
            _decodeManager->readVersionedImagesInfo(
                highRequests, false, true, _sessionId);
        }
        return;
    }
    drain(_metadataUrgentRows, highRequests);
    drain(_metadataVisibleRows, highRequests);
    drain(_metadataAdHocRows, backgroundRequests);
    drain(_metadataOverscanRows, backgroundRequests);

    if (_catalogMetadataRequested) {
        while (available > 0 && _catalogMetadataCursor < _entries.size()) {
            appendRow(_catalogMetadataCursor++, backgroundRequests);
        }
    }

    _metadataPeakPending = qMax(
        _metadataPeakPending,
        static_cast<qsizetype>(_metadataPendingVersions.size()));
    if (!highRequests.isEmpty()) {
        ++_metadataSubmittedBatches;
        _decodeManager->readVersionedImagesInfo(
            highRequests, false, true, _sessionId);
    }
    if (!backgroundRequests.isEmpty()) {
        ++_metadataSubmittedBatches;
        _decodeManager->readVersionedImagesInfo(
            backgroundRequests, false, false, _sessionId);
    }
}

void ExternalCatalogModel::scheduleMetadataRetry(
    const QString &sourceIdentity, const QString &contentVersion,
    const QString &resourceId, bool background) {
    const QString retryKey = sourceIdentity + QChar(0x1f) +
        contentVersion;
    if (_metadataRetryScheduled.contains(retryKey)) {
        return;
    }
    const int MaxAutomaticAttempts = background ? 1 : 3;
    const int attempt = _metadataRetryAttempts.value(retryKey) + 1;
    if (attempt > MaxAutomaticAttempts) {
        return;
    }
    _metadataRetryAttempts.insert(retryKey, attempt);
    _metadataRetryScheduled.insert(retryKey);
    const int delayMs = static_cast<int>(qMin<qint64>(
        30000, 500LL << qMin(attempt - 1, 6)));
    if (background) {
        _backgroundMetadataRetries.insert(retryKey,
                                          BackgroundMetadataRetry{
            .sourceIdentity = sourceIdentity,
            .contentVersion = contentVersion,
            .resourceId = resourceId,
            .notBeforeMs = QDateTime::currentMSecsSinceEpoch() + delayMs,
        });
        scheduleBackgroundRetryWake();
        return;
    }
    QTimer::singleShot(delayMs, this,
                       [this, sourceIdentity, contentVersion, resourceId,
                        retryKey]() {
        _metadataRetryScheduled.remove(retryKey);
        if (_shutdown) {
            return;
        }
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row)) {
            _metadataRetryAttempts.remove(retryKey);
            return;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image || entry.contentVersion != contentVersion ||
            entry.source.resourceId != resourceId ||
            _metadataResolvedPaths.contains(sourceIdentity) ||
            _metadataPendingVersions.contains(sourceIdentity)) {
            return;
        }
        if (!_metadataAdHocRows.contains(row)) {
            _metadataAdHocRows.append(row);
        }
        scheduleMetadataPump();
    });
}

void ExternalCatalogModel::scheduleBackgroundRetryWake() {
    if (_shutdown) {
        return;
    }
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (const BackgroundMetadataRetry &retry :
         std::as_const(_backgroundMetadataRetries)) {
        earliest = qMin(earliest, retry.notBeforeMs);
    }
    for (const BackgroundDecodeRetry &retry :
         std::as_const(_backgroundDecodeRetries)) {
        earliest = qMin(earliest, retry.notBeforeMs);
    }
    if (earliest == std::numeric_limits<qint64>::max()) {
        _backgroundRetryTimer.stop();
        return;
    }
    const int delayMs = static_cast<int>(qBound<qint64>(
        qint64{0}, earliest - QDateTime::currentMSecsSinceEpoch(),
        qint64{30000}));
    if (_backgroundRetryTimer.isActive() &&
        _backgroundRetryTimer.remainingTime() <= delayMs) {
        return;
    }
    _backgroundRetryTimer.start(delayMs);
}

void ExternalCatalogModel::processBackgroundRetries() {
    if (_shutdown) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = _backgroundMetadataRetries.begin();
         it != _backgroundMetadataRetries.end();) {
        if (it->notBeforeMs > now) {
            ++it;
            continue;
        }
        const QString retryKey = it.key();
        const BackgroundMetadataRetry retry = it.value();
        it = _backgroundMetadataRetries.erase(it);
        _metadataRetryScheduled.remove(retryKey);
        const int row = _sourceToRow.value(retry.sourceIdentity, -1);
        if (!validRow(row)) {
            _metadataRetryAttempts.remove(retryKey);
            continue;
        }
        const Entry &entry = _entries.at(row);
        if (!entry.image ||
            entry.contentVersion != retry.contentVersion ||
            entry.source.resourceId != retry.resourceId ||
            _metadataResolvedPaths.contains(retry.sourceIdentity) ||
            _metadataPendingVersions.contains(retry.sourceIdentity)) {
            continue;
        }
        if (!_metadataAdHocRows.contains(row)) {
            _metadataAdHocRows.append(row);
        }
    }

    for (auto it = _backgroundDecodeRetries.begin();
         it != _backgroundDecodeRetries.end();) {
        if (it->notBeforeMs > now) {
            ++it;
            continue;
        }
        const QString retryKey = it.key();
        const ImageDecodeRequest request = it->request;
        it = _backgroundDecodeRetries.erase(it);
        _sourceDecodeRetryScheduled.remove(retryKey);
        const QString sourceIdentity = request.info.sourceIdentity();
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row) ||
            _entries.at(row).contentVersion !=
                request.info.sourceVersionToken ||
            _entries.at(row).source.resourceId !=
                request.info.source.resourceId) {
            _sourceDecodeRetryAttempts.remove(retryKey);
            continue;
        }
        if (_catalogFitStarted &&
            !_catalogFitResolvedSources.contains(sourceIdentity) &&
            !_catalogFitRows.contains(row)) {
            _catalogFitRows.prepend(row);
        }
    }

    scheduleMetadataPump();
    scheduleCatalogFitPump();
    const auto plans = _viewerPlans;
    for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
        const int centerRow = rowForEntryId(plan.key());
        if (validRow(centerRow)) {
            scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                   plan->prefetchCount);
        }
    }
    scheduleBackgroundRetryWake();
}

void ExternalCatalogModel::resetMetadataPlanner() {
    _metadataPendingVersions.clear();
    _metadataResolvedPaths.clear();
    _metadataVisibleRows.clear();
    _metadataOverscanRows.clear();
    _metadataUrgentRows.clear();
    _metadataAdHocRows.clear();
    _catalogMetadataCursor = 0;
    _metadataPeakPending = 0;
    _metadataSubmittedBatches = 0;
    _catalogMetadataRequested = false;
    _metadataPumpScheduled = false;
    _metadataRetryAttempts.clear();
    _metadataRetryScheduled.clear();
    _backgroundMetadataRetries.clear();
    if (_backgroundDecodeRetries.isEmpty()) {
        _backgroundRetryTimer.stop();
    }
}

void ExternalCatalogModel::scheduleViewerDecode() {
    const int row = rowForEntryId(_viewerEntryId);
    if (_shutdown || !validRow(row) || !_viewerViewportSize.isValid()) {
        return;
    }
    const bool nativeRequest = _viewerViewportSize.isEmpty();
    const int prefetchCount = nativeRequest ? 5 : 16;
    _viewerPlans.insert(
        _viewerEntryId, {_viewerViewportSize, prefetchCount});
    if (nativeRequest) {
        _deferredNativeEntryId = _viewerEntryId;
        scheduleViewerDecodeAt(
            row, _lastViewerFitViewportSize, prefetchCount);
        tryScheduleDeferredNative();
        return;
    }
    scheduleViewerDecodeAt(row, _viewerViewportSize, prefetchCount);
}

void ExternalCatalogModel::scheduleViewerDecodeAt(
    int row, const QSize &viewportSize, int prefetchCount) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image ||
        !viewportSize.isValid() || prefetchCount <= 0) {
        return;
    }

    const bool probeBarrierActive =
        _catalogProbeRequested && !_probePassComplete;
    const int effectivePrefetchCount = probeBarrierActive ? 1 : prefetchCount;
    const bool explicitCurrentPlan = _entries.at(row).id == _viewerEntryId;
    const QList<int> candidates = viewerCandidateRows(
        row, effectivePrefetchCount);
    for (const int candidateRow : candidates) {
        const Entry &candidate = _entries.at(candidateRow);
        if (!candidate.item || !candidate.item->fullSize().isValid()) {
            requestImageMetadataForRow(
                candidateRow,
                candidateRow == row && explicitCurrentPlan);
        }
    }

    ViewerImageCache::RequestPlan plan = _viewerImageCache.planRequest(
        viewerItems(), row, viewportSize, effectivePrefetchCount,
        [this](ImageDecodeRequest &request) {
            stabilizeViewerFitRequest(request);
        });
    if (!plan.cachedImages.isEmpty()) {
        emit viewerSourceAtChanged(row);
        if (_entries.at(row).id == _viewerEntryId) {
            notifyViewerImageUrlChanged();
        }
    }

    QList<ImageDecodeRequest> requests;
    requests.reserve(plan.decodeRequests.size());
    for (ImageDecodeRequest request : std::as_const(plan.decodeRequests)) {
        request.requestNamespace = _sessionId;
        request.info.requestNamespace = _sessionId;
        const int requestRow = _sourceToRow.value(
            request.info.sourceIdentity(), -1);
        if (validRow(requestRow)) {
            const Entry &sourceEntry = _entries.at(requestRow);
            request.info.source = sourceEntry.source;
            request.info.path = sourceEntry.sourceIdentity;
            request.info.sourceVersionToken =
                sourceEntry.contentVersion;
        }
        // Fit artifacts are safe to persist by opaque source revision. Native
        // frames remain RAM-only and are produced only for the deferred
        // current +/-2 window.
        request.checkCache = request.fitToViewerRequest;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = request.fitToViewerRequest;
        const QString key = viewerRequestKey(request);
        if (_pendingViewerRequests.contains(key)) {
            if (!_catalogFitPendingKeys.contains(key)) {
                continue;
            }
            // Upgrade the interactive subscriber. The background decode may
            // still finish, but shared materialization prevents a second
            // download and this request enters the viewer priority band.
            requests.append(request);
            continue;
        }
        _pendingViewerRequests.insert(key);
        requests.append(request);
    }
    if (!requests.isEmpty()) {
        _decodeManager->decodeImages(requests);
    }
}

QList<int> ExternalCatalogModel::viewerCandidateRows(
    int row, int count) const {
    QList<int> result;
    if (!validRow(row) || count <= 0) {
        return result;
    }

    bool hitStart = false;
    bool hitEnd = false;
    for (int counter = 0;
         result.size() < count && !(hitStart && hitEnd); ++counter) {
        const int candidate = counter % 2 == 0
            ? row + counter / 2
            : row - (counter + 1) / 2;
        if (candidate < 0) {
            hitStart = true;
        }
        if (candidate >= _entries.size()) {
            hitEnd = true;
        }
        if (validRow(candidate) && _entries.at(candidate).image) {
            result.append(candidate);
        }
    }
    return result;
}

QList<ImageFile *> ExternalCatalogModel::viewerItems() const {
    QList<ImageFile *> result;
    result.reserve(_entries.size());
    for (const Entry &entry : _entries) {
        result.append(entry.item);
    }
    return result;
}

void ExternalCatalogModel::notifyViewerImageUrlChanged() {
    const int row = rowForEntryId(_viewerEntryId);
    const QString nextUrl = validRow(row)
        ? bestViewerImageUrlAt(row) : QString();
    if (_lastViewerImageUrl == nextUrl) {
        return;
    }
    _lastViewerImageUrl = nextUrl;
    emit viewerImageUrlChanged();
}

QString ExternalCatalogModel::viewerRequestKey(
    const ImageDecodeRequest &request) const {
    const qint64 sourceSize = request.info.source.isValid()
        ? request.info.source.size : request.info.fileSize;
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4x%5\x1f%6")
        .arg(request.info.sourceIdentity())
        .arg(request.info.sourceVersionToken)
        .arg(sourceSize)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(request.fitToViewerRequest ? 1 : 0);
}

QString ExternalCatalogModel::thumbnailRequestKey(
    const ImageDecodeRequest &request) const {
    const qint64 sourceSize = request.info.source.isValid()
        ? request.info.source.size : request.info.fileSize;
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4x%5\x1f%6")
        .arg(request.info.sourceIdentity())
        .arg(request.info.sourceVersionToken)
        .arg(sourceSize)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(thumbnailTransformKey(request));
}

bool ExternalCatalogModel::validRow(int row) const {
    return row >= 0 && row < _entries.size();
}

QString ExternalCatalogModel::nextImageId(const Entry &entry) {
    return QStringLiteral("%1-%2-v%3-s%4-%5")
        .arg(_sessionId)
        .arg(QString::number(qHash(entry.id), 16))
        .arg(entry.contentVersion)
        .arg(entry.size)
        .arg(++_nextImageSerial);
}

} // namespace ZoinGallery
