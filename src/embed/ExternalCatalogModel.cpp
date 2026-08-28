#include "ExternalCatalogModel.h"

#include "DecodeManager.h"
#include "ProviderImageStore.h"
#include "ThumbnailMemoryCache.h"
#include "ViewerImageCache.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QTimer>
#include <QTimeZone>

#include <algorithm>
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

bool versionMatches(qint64 expectedMtimeNs, qint64 expectedSize,
                    const ImageInfo &actual) {
    if (expectedMtimeNs != 0 &&
        actual.sourceVersionToken != expectedMtimeNs) {
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
      _thumbnailProviderName(thumbnailProviderName.trimmed()),
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
            [this](const ImageDecodeRequest &request) {
                if (request.requestNamespace != _sessionId) {
                    return;
                }
                if (request.viewerRequest) {
                    _pendingViewerRequests.remove(viewerRequestKey(request));
                }
                else {
                    releaseFailedThumbnailRequest(request);
                }
            });
    connect(_thumbnailCache.data(),
            &ThumbnailMemoryCache::frameAvailable,
            this, &ExternalCatalogModel::handleThumbnailFrameAvailable);
    connect(_thumbnailCache.data(),
            &ThumbnailMemoryCache::frameEvicted,
            this, &ExternalCatalogModel::handleThumbnailFrameEvicted);
    connect(_thumbnailCache.data(),
            &ThumbnailMemoryCache::requestReleased,
            this, &ExternalCatalogModel::handleThumbnailRequestReleased);
}

ExternalCatalogModel::~ExternalCatalogModel() {
    shutdown();
    deleteRetiredItems();
    for (Entry &entry : _entries) {
        delete entry.item;
        entry.item = nullptr;
    }
}

int ExternalCatalogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : _entries.size();
}

QString snapshotFileSize(qint64 size) {
    if (size < 0) {
        return {};
    }
    static constexpr qint64 KB = 1024;
    static constexpr qint64 MB = 1024 * KB;
    static constexpr qint64 GB = 1024 * MB;
    static constexpr qint64 TB = 1024 * GB;
    if (size < KB) {
        return QString::number(size) + QStringLiteral(" Bytes");
    }
    if (size < MB) {
        return QString::number(size / static_cast<double>(KB), 'f', 2)
            + QStringLiteral(" KB");
    }
    if (size < GB) {
        return QString::number(size / static_cast<double>(MB), 'f', 2)
            + QStringLiteral(" MB");
    }
    if (size < TB) {
        return QString::number(size / static_cast<double>(GB), 'f', 2)
            + QStringLiteral(" GB");
    }
    return QString::number(size / static_cast<double>(TB), 'f', 2)
        + QStringLiteral(" TB");
}

ImageFile *ExternalCatalogModel::ensureItem(int row) const {
    if (!validRow(row)) {
        return nullptr;
    }
    Entry &entry = const_cast<Entry &>(_entries.at(row));
    if (entry.item) {
        return entry.item;
    }

    QString folder;
    QString fileName = entry.name;
    if (!entry.localPath.isEmpty()) {
        const QFileInfo pathInfo(entry.localPath);
        folder = pathInfo.absolutePath();
        if (fileName.isEmpty()) {
            fileName = pathInfo.fileName();
        }
    }
    auto *item = new ImageFile(
        const_cast<ExternalCatalogModel *>(this));
    item->initializeExternalCatalogRow(
        folder, fileName, row, entry.directory, entry.image,
        entry.iconPath.isEmpty()
            ? defaultIconPath(entry.directory, entry.image)
            : entry.iconPath,
        entry.selected, _thumbnailProviderName, entry.imageInfo,
        entry.highlightStyle, entry.displayFields,
        entry.metadataDeferred);
    // A retained provider frame belongs to this current catalog entry even
    // when its QObject facade is created only after the first painted frame.
    // Restore that identity before exposing the facade so thumbnail planning
    // cannot mistake an already-published frame for an empty request.
    if (!entry.thumbnailProviderId.isEmpty()) {
        item->setImageId(entry.thumbnailProviderId);
    }
    if (entry.originalSize.isValid()) {
        item->setFullSize(entry.originalSize);
    }
    entry.item = item;
    return item;
}

QVariantMap ExternalCatalogModel::visualSnapshot(int row) const {
    if (!validRow(row)) {
        return {};
    }
    const Entry &entry = _entries.at(row);
    QString imageIdUrl;
    if (!entry.thumbnailProviderId.isEmpty()) {
        imageIdUrl = QStringLiteral("image://") + _thumbnailProviderName
            + QLatin1Char('/') + entry.thumbnailProviderId;
    }
    else if (entry.item) {
        imageIdUrl = entry.item->imageIdUrl();
    }
    QVariantMap visualFields = entry.displayFields;
    const QFileInfo nameInfo(entry.name);
    if (!visualFields.contains(QStringLiteral("displayBaseName"))) {
        QString baseName = entry.directory
            ? entry.name : nameInfo.completeBaseName();
        if (baseName.isEmpty()) {
            baseName = entry.name;
        }
        visualFields.insert(QStringLiteral("displayBaseName"), baseName);
    }
    if (!visualFields.contains(QStringLiteral("displayExtension"))) {
        visualFields.insert(QStringLiteral("displayExtension"),
                            entry.directory ? QString() : nameInfo.suffix());
    }
    if (!visualFields.contains(QStringLiteral("sizeText"))) {
        const qint64 effectiveSize = entry.size >= 0
            ? entry.size : entry.imageInfo.fileSize;
        visualFields.insert(QStringLiteral("sizeText"),
                            snapshotFileSize(effectiveSize));
    }
    if (!visualFields.contains(QStringLiteral("mtimeText"))) {
        visualFields.insert(
            QStringLiteral("mtimeText"),
            entry.imageInfo.lastModified.isValid()
                ? QLocale().toString(entry.imageInfo.lastModified,
                                     QLocale::ShortFormat)
                : QString());
    }
    if (!visualFields.contains(QStringLiteral("modeText"))) {
        visualFields.insert(QStringLiteral("modeText"), QString());
    }
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("entryId"), entry.id},
        {QStringLiteral("sourceIndex"), entry.sourceIndex},
        {QStringLiteral("localPath"), entry.localPath},
        {QStringLiteral("text"), entry.name},
        {QStringLiteral("isFolder"), entry.directory},
        {QStringLiteral("isImage"), entry.image},
        {QStringLiteral("isSelected"), entry.selected},
        {QStringLiteral("iconPath"), entry.iconPath},
        {QStringLiteral("highlightStyle"), entry.highlightStyle},
        {QStringLiteral("displayFields"), visualFields},
        {QStringLiteral("imageIdUrl"), imageIdUrl},
    };
}

void ExternalCatalogModel::retireItemAfterReset(ImageFile *item) {
    if (!item) {
        return;
    }
    _retiredItems.insert(item);
    connect(item, &QObject::destroyed, this, [this, item]() {
        _retiredItems.remove(item);
    });
    item->deleteLater();
}

void ExternalCatalogModel::deleteRetiredItems() {
    const QSet<ImageFile *> retired = std::exchange(
        _retiredItems, QSet<ImageFile *>{});
    for (ImageFile *item : retired) {
        delete item;
    }
}

QVariant ExternalCatalogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !validRow(index.row())) {
        return {};
    }
    const Entry &entry = _entries.at(index.row());
    ImageFile *item = nullptr;
    switch (role) {
    case FileListModel::ImageIdUrlRole:
        item = ensureItem(index.row());
        return item ? item->imageIdUrl() : QString();
    case FileListModel::SelectedRole:
        return entry.selected;
    case FileListModel::SelectionGroupIdRole:
        item = ensureItem(index.row());
        return item ? item->selectionGroupId() : QString();
    case FileListModel::SelectionGroupColorRole:
        item = ensureItem(index.row());
        return item ? item->selectionGroupColor() : QColor();
    case FileListModel::ImageFileRole:
        return QVariant::fromValue(ensureItem(index.row()));
    case FileListModel::FolderRole:
        return entry.directory;
    case FileListModel::IsImageRole:
        return entry.image;
    case FileListModel::ImageFullSizeRole:
        return entry.originalSize;
    case FileListModel::FolderViewRole:
        return false;
    case FileListModel::LastModifiedRole:
        return entry.imageInfo.lastModified;
    case FileListModel::FileSizeRole:
        return entry.size;
    case EntryIdRole:
        return entry.id;
    case SourceIndexRole:
        return entry.sourceIndex;
    case LocalPathRole:
        return entry.localPath;
    case VersionTokenRole:
        return entry.mtimeNs;
    case EntryNameRole:
        return entry.name;
    case KnownImageSizeRole:
        return entry.originalSize;
    case VisualSnapshotRole:
        // This POD-only role is the first-frame facade for embedded views.
        // It never constructs an ImageFile and contains only the current
        // catalog row; MasonryLayout may therefore paint a complete reset
        // before lazily materializing QObject-backed thumbnail state.
        return visualSnapshot(index.row());
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
    names[EntryNameRole] = "entryName";
    names[KnownImageSizeRole] = "knownImageSize";
    names[VisualSnapshotRole] = "visualSnapshot";
    return names;
}

bool ExternalCatalogModel::catalogMatches(
    const QVariantList &values, bool *carriesAppearance,
    bool metadataDeferred) const {
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
            id = !localPath.isEmpty()
                ? localPath
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
            : (!metadataDeferred && !directory
               && FileListModel::isImage(name));
        const qint64 mtimeNs = integerValue(
            map, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
        const qint64 size = integerValue(
            map, QStringLiteral("size"), QStringLiteral("fileSize"), -1);
        const QVariantMap normalizedDisplayFields = catalogDisplayFields(
            map, metadataDeferred);

        if (current.id != id || current.sourceIndex != sourceIndex ||
            current.name != name || current.localPath != localPath ||
            current.directory != directory || current.image != image ||
            current.mtimeNs != mtimeNs || current.size != size ||
            current.displayFields != normalizedDisplayFields) {
            return false;
        }
    }
    return true;
}

bool ExternalCatalogModel::applyCatalog(
    const QVariantList &values, bool metadataDeferred,
    bool checkEquivalentCatalog) {
    if (_shutdown) {
        return false;
    }
    const bool traceCatalog = qEnvironmentVariableIsSet(
        "F4_NAV_BENCHMARK_TRACE");
    QElapsedTimer catalogTimer;
    if (traceCatalog) {
        catalogTimer.start();
    }
    qint64 rowsCompletedNs = 0;
    qint64 leftoversCompletedNs = 0;
    qint64 resetStartedNs = 0;
    // Go may advance its authoritative catalog revision for semantic fields
    // that Gallery does not consume (for example IsCached). Advance the
    // session revision without tearing down identical image objects, queued
    // work, textures, or viewer state.
    bool hasAppearance = false;
    if (checkEquivalentCatalog
        && catalogMatches(values, &hasAppearance, metadataDeferred)) {
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

    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _viewerPlans.clear();

    QList<Entry> next;
    next.resize(values.size());
    QSet<QString> seenIds;
    seenIds.reserve(values.size());
    QSet<QString> nextViewerSources;
    nextViewerSources.reserve(values.size());
    QHash<QString, int> nextIdToRow;
    nextIdToRow.reserve(values.size());
    QHash<QString, int> nextPathToRow;
    nextPathToRow.reserve(values.size());
    QMultiHash<QString, QString> nextSourceEntryIds;
    nextSourceEntryIds.reserve(values.size());
    QMultiHash<QString, QString> nextProviderEntryIds;
    nextProviderEntryIds.reserve(values.size());

    beginResetModel();
    // Once reset begins no observer may consume the old rows. Move their
    // state into the identity map instead of copying every entry before
    // rebuilding a large directory.
    QList<Entry> oldEntries = std::move(_entries);
    _entries.clear();
    QHash<QString, Entry> previous;
    previous.reserve(oldEntries.size());
    for (Entry &entry : oldEntries) {
        previous.insert(entry.id, std::move(entry));
    }
    for (int row = 0; row < values.size(); ++row) {
        const QVariantMap map = values.at(row).toMap();
        Entry &entry = next[row];
        entry.sourceIndex = map.value(QStringLiteral("index"), row).toInt();
        entry.name = map.value(QStringLiteral("name")).toString();
        entry.localPath = map.value(QStringLiteral("localPath"),
                                    map.value(QStringLiteral("path"))).toString();
        entry.id = map.value(QStringLiteral("entryId")).toString();
        if (entry.id.isEmpty()) {
            entry.id = !entry.localPath.isEmpty()
                ? entry.localPath
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
            : (!metadataDeferred && !entry.directory
               && FileListModel::isImage(entry.name));
        entry.selected = map.value(QStringLiteral("selected")).toBool();
        if (metadataDeferred) {
            entry.mtimeNs = 0;
            entry.size = -1;
        }
        else {
            entry.mtimeNs = integerValue(map, QStringLiteral("mtimeNs"),
                                         QStringLiteral("mtimeNanos"), 0);
            entry.size = integerValue(map, QStringLiteral("size"),
                                      QStringLiteral("fileSize"), -1);
        }
        entry.imageInfo.path = entry.localPath;
        entry.imageInfo.requestNamespace = _sessionId;
        entry.imageInfo.sourceVersionToken = entry.mtimeNs;
        entry.imageInfo.lastModified = entry.mtimeNs != 0
            ? QDateTime::fromMSecsSinceEpoch(
                  entry.mtimeNs / 1000000, QTimeZone::UTC)
            : QDateTime{};
        entry.imageInfo.fileSize = entry.size;
        entry.sourceIdentity = entry.localPath.isEmpty()
            ? QString()
            : ThumbnailMemoryCache::canonicalSourceIdentity(entry.localPath);
        entry.displayFields = catalogDisplayFields(map, metadataDeferred);
        entry.metadataDeferred = metadataDeferred;

        Entry old = previous.take(entry.id);
        const bool hadOldEntry = !old.id.isEmpty();
        if (old.item) {
            old.imageInfo = old.item->info();
            if (old.item->fullSize().isValid()) {
                old.originalSize = old.item->fullSize();
            }
        }
        // Materialize ImageFile QObjects only when a delegate, preview, or
        // viewer asks for the row. Large directory swaps can then publish the
        // complete lightweight model while constructing only visible items
        // before the first frame.
        entry.item = old.item;
        entry.highlightStyle = old.highlightStyle;
        const bool sourceChanged = hadOldEntry &&
            (old.localPath != entry.localPath || old.mtimeNs != entry.mtimeNs ||
             old.size != entry.size || old.image != entry.image);
        if (hadOldEntry && !sourceChanged) {
            entry.imageInfo = old.imageInfo;
            entry.originalSize = old.originalSize;
        }
        if (sourceChanged) {
            _viewerImageCache.remove(old.localPath);
            clearPublishedImage(old);
            entry.originalSize = {};
            if (entry.item) {
                entry.item->setFullSize({});
            }
        }
        else if (old.item) {
            entry.thumbnailProviderId = old.thumbnailProviderId;
            entry.thumbnailRequestedSize = old.thumbnailRequestedSize;
            entry.thumbnailTransformKey = old.thumbnailTransformKey;
        }

        QString folder;
        QString fileName = entry.name;
        if (!entry.localPath.isEmpty()) {
            const QFileInfo pathInfo(entry.localPath);
            folder = pathInfo.absolutePath();
            if (fileName.isEmpty()) {
                fileName = pathInfo.fileName();
            }
        }
        entry.iconPath = defaultIconPath(entry.directory, entry.image);
        if (map.contains(QStringLiteral("highlightStyle"))) {
            entry.highlightStyle = map.value(
                QStringLiteral("highlightStyle")).toMap();
            const QString styledIcon = entry.highlightStyle.value(
                QStringLiteral("icon")).toString();
            if (!styledIcon.isEmpty()) {
                entry.iconPath = styledIcon;
            }
        }

        if (entry.item) {
            // A stable entry ID deliberately keeps its ImageFile object so a
            // retained delegate can bind old->new without churn. Update that
            // object through notifying setters: assigning the same QObject to
            // the delegate again is a QML no-op, so silent bulk mutation here
            // would leave its name/icon/columns visually stale after reset.
            entry.item->setFolderPath(folder);
            entry.item->setFileName(fileName);
            entry.item->setIndex(row);
            entry.item->setIsFolder(entry.directory);
            entry.item->setIsImage(entry.image);
            entry.item->setHighlightStyle(entry.highlightStyle);
            entry.item->setIconPath(entry.iconPath);
            entry.item->setIsSelected(entry.selected);
            entry.item->setImageProviderName(_thumbnailProviderName);
            entry.item->setInfo(entry.imageInfo);
            entry.item->setFullSize(entry.originalSize);
            // Apply host-preformatted columns after metadata, so rows with a
            // partial/deferred map derive missing values from current state.
            entry.item->setDisplayFields(entry.displayFields);
        }
        if (entry.image && !entry.localPath.isEmpty()) {
            nextViewerSources.insert(
                entry.localPath + QChar(0x1f) +
                QString::number(entry.mtimeNs) + QChar(0x1f) +
                QString::number(entry.size));
        }
        nextIdToRow.insert(entry.id, row);
        if (!entry.localPath.isEmpty()) {
            nextPathToRow.insert(QDir::cleanPath(entry.localPath), row);
        }
        if (!entry.sourceIdentity.isEmpty()) {
            nextSourceEntryIds.insert(entry.sourceIdentity, entry.id);
        }
        if (!entry.thumbnailProviderId.isEmpty()) {
            nextProviderEntryIds.insert(entry.thumbnailProviderId, entry.id);
        }
    }
    if (traceCatalog) {
        rowsCompletedNs = catalogTimer.nsecsElapsed();
    }

    QList<ImageFile *> retiredAfterReset;
    retiredAfterReset.reserve(previous.size());
    for (Entry &entry : previous) {
        const QString viewerSource =
            entry.localPath + QChar(0x1f) +
            QString::number(entry.mtimeNs) + QChar(0x1f) +
            QString::number(entry.size);
        const bool sourceStillPresent = entry.image &&
            nextViewerSources.contains(viewerSource);
        if (!sourceStillPresent) {
            _viewerImageCache.remove(entry.localPath);
        }
        clearPublishedImage(entry);
        if (entry.item) {
            retiredAfterReset.append(entry.item);
            entry.item = nullptr;
        }
    }
    if (traceCatalog) {
        leftoversCompletedNs = catalogTimer.nsecsElapsed();
    }

    _entries = std::move(next);
    _idToRow = std::move(nextIdToRow);
    _pathToRow = std::move(nextPathToRow);
    _sourceEntryIds = std::move(nextSourceEntryIds);
    _providerEntryIds = std::move(nextProviderEntryIds);
    if (_entries.isEmpty()) {
        _cursorRow = -1;
    } else {
        _cursorRow = qBound(0, _cursorRow, _entries.size() - 1);
    }
    if (traceCatalog) {
        resetStartedNs = catalogTimer.nsecsElapsed();
    }
    endResetModel();
    // modelReset handlers have now rebound every retained visual slot. Keep
    // removed row façades alive until that synchronous hand-off is complete,
    // then reclaim them on the next event-loop turn.
    for (ImageFile *item : std::as_const(retiredAfterReset)) {
        retireItemAfterReset(item);
    }
    if (traceCatalog) {
        const qint64 completedNs = catalogTimer.nsecsElapsed();
        qInfo().nospace()
            << "F4_NAV_BENCHMARK_TRACE catalog.model rowsNs="
            << rowsCompletedNs << " leftoversNs="
            << (leftoversCompletedNs - rowsCompletedNs)
            << " lookupCommitNs="
            << (resetStartedNs - leftoversCompletedNs)
            << " endResetNs=" << (completedNs - resetStartedNs)
            << " totalNs=" << completedNs;
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
        // The refresh canceled this session's queued work. Resume the target
        // implied by the last viewport. The retained in-memory cache supplies
        // the current frame immediately while missing neighbors are replanned.
        notifyViewerImageUrlChanged();
        scheduleViewerDecode();
    }

    return true;
}

bool ExternalCatalogModel::applyMetadata(const QVariantList &values) {
    if (_shutdown) {
        return false;
    }

    // Validate the complete chunk before changing any row. A response can
    // race with a directory replacement; accepting only an exact set of
    // current entry IDs makes such a stale chunk an atomic no-op.
    struct RowUpdate {
        int row = -1;
        QVariantMap value;
    };
    QList<RowUpdate> updates;
    updates.reserve(values.size());
    QSet<int> seenRows;
    for (const QVariant &value : values) {
        const QVariantMap map = value.toMap();
        const QString entryId = map.value(
            QStringLiteral("entryId")).toString();
        const int row = rowForEntryId(entryId);
        if (entryId.isEmpty() || !validRow(row) || seenRows.contains(row)) {
            return false;
        }
        if (map.contains(QStringLiteral("index"))
            && map.value(QStringLiteral("index")).toInt() !=
                _entries.at(row).sourceIndex) {
            return false;
        }
        seenRows.insert(row);
        updates.append({row, map});
    }

    QList<int> changedRows;
    changedRows.reserve(updates.size());
    bool viewerSourceInvalidated = false;
    for (const RowUpdate &update : std::as_const(updates)) {
        Entry &entry = _entries[update.row];
        const QVariantMap &map = update.value;

        const bool oldImage = entry.image;
        const QString oldLocalPath = entry.localPath;
        const QString oldSourceIdentity = entry.sourceIdentity;
        const qint64 oldMtimeNs = entry.mtimeNs;
        const qint64 oldSize = entry.size;
        const QVariantMap oldDisplayFields = entry.displayFields;
        const QVariantMap oldStyle = entry.highlightStyle;
        const QString oldIconPath = entry.iconPath;

        if (map.contains(QStringLiteral("isImage"))) {
            entry.image = map.value(QStringLiteral("isImage")).toBool();
        }
        if (map.contains(QStringLiteral("localPath"))) {
            entry.localPath = map.value(
                QStringLiteral("localPath")).toString();
        }
        if (map.contains(QStringLiteral("mtimeNanos"))
            || map.contains(QStringLiteral("mtimeNs"))) {
            entry.mtimeNs = integerValue(
                map, QStringLiteral("mtimeNs"),
                QStringLiteral("mtimeNanos"), 0);
        }
        if (map.contains(QStringLiteral("size"))
            || map.contains(QStringLiteral("fileSize"))) {
            entry.size = integerValue(
                map, QStringLiteral("size"),
                QStringLiteral("fileSize"), -1);
        }
        if (carriesDisplayFields(map)) {
            const QVariantMap fields = displayFields(map);
            for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
                entry.displayFields.insert(it.key(), it.value());
            }
            entry.metadataDeferred = false;
        }

        const bool sourceChanged = oldImage != entry.image
            || oldLocalPath != entry.localPath
            || oldMtimeNs != entry.mtimeNs || oldSize != entry.size;
        if (sourceChanged) {
            // The minimal catalog can already have queued a header read for
            // the provisional (version 0 / unknown-size) source. Do not let
            // that request suppress the authoritative-version probe, and do
            // not retain a resolved marker from the provisional source.
            if (!oldLocalPath.isEmpty()) {
                const QString oldCleanPath = QDir::cleanPath(oldLocalPath);
                _metadataPendingVersions.remove(oldCleanPath);
                _metadataResolvedPaths.remove(oldCleanPath);
            }
            if (!entry.localPath.isEmpty()) {
                const QString cleanPath = QDir::cleanPath(entry.localPath);
                _metadataPendingVersions.remove(cleanPath);
                _metadataResolvedPaths.remove(cleanPath);
            }
            _viewerImageCache.remove(oldLocalPath);
            clearPublishedImage(entry);
            entry.originalSize = {};
            if (entry.item) {
                entry.item->setFullSize({});
            }
            viewerSourceInvalidated = viewerSourceInvalidated
                || entry.id == _viewerEntryId;
        }

        if (oldLocalPath != entry.localPath) {
            if (!oldLocalPath.isEmpty()) {
                _pathToRow.remove(QDir::cleanPath(oldLocalPath));
            }
            if (!oldSourceIdentity.isEmpty()) {
                _sourceEntryIds.remove(oldSourceIdentity, entry.id);
            }
            entry.sourceIdentity =
                ThumbnailMemoryCache::canonicalSourceIdentity(
                    entry.localPath);
            if (!entry.localPath.isEmpty()) {
                _pathToRow.insert(QDir::cleanPath(entry.localPath),
                                  update.row);
            }
            if (!entry.sourceIdentity.isEmpty()) {
                _sourceEntryIds.insert(entry.sourceIdentity, entry.id);
            }
            if (entry.item) {
                const QFileInfo pathInfo(entry.localPath);
                entry.item->setFolderPath(entry.localPath.isEmpty()
                    ? QString() : pathInfo.absolutePath());
                entry.item->setFileName(!entry.name.isEmpty()
                    ? entry.name : pathInfo.fileName());
            }
        }

        if (sourceChanged && entry.image && !entry.localPath.isEmpty()) {
            if (_metadataLastVisibleRows.contains(update.row)
                || entry.id == _viewerEntryId) {
                requestImageMetadataForRow(update.row, true);
            }
            if (_catalogMetadataRequested) {
                _catalogMetadataCursor = qMin(
                    _catalogMetadataCursor, update.row);
                scheduleMetadataPump();
            }
        }

        ImageInfo imageInfo = sourceChanged ? ImageInfo{}
                                            : entry.imageInfo;
        imageInfo.path = entry.localPath;
        imageInfo.requestNamespace = _sessionId;
        imageInfo.sourceVersionToken = entry.mtimeNs;
        imageInfo.lastModified = entry.mtimeNs != 0
                ? QDateTime::fromMSecsSinceEpoch(
                      entry.mtimeNs / 1000000, QTimeZone::UTC)
                : QDateTime{};
        imageInfo.fileSize = entry.size;
        entry.imageInfo = imageInfo;
        if (entry.item) {
            entry.item->setIsImage(entry.image);
            entry.item->setInfo(entry.imageInfo);
            if (carriesDisplayFields(map)) {
                entry.item->setDisplayFields(entry.displayFields);
            }
        }

        if (map.contains(QStringLiteral("highlightStyle"))) {
            entry.highlightStyle = map.value(
                QStringLiteral("highlightStyle")).toMap();
            const QString styledIcon = entry.highlightStyle.value(
                QStringLiteral("icon")).toString();
            entry.iconPath = styledIcon.isEmpty()
                ? defaultIconPath(entry.directory, entry.image)
                : styledIcon;
            if (entry.item) {
                entry.item->setHighlightStyle(entry.highlightStyle);
                entry.item->setIconPath(entry.iconPath);
            }
        }
        else if (oldImage != entry.image) {
            entry.iconPath = defaultIconPath(
                entry.directory, entry.image);
            if (entry.item) {
                entry.item->setIconPath(entry.iconPath);
            }
        }

        if (sourceChanged || oldDisplayFields != entry.displayFields
            || oldStyle != entry.highlightStyle
            || oldIconPath != entry.iconPath) {
            changedRows.append(update.row);
        }
    }

    std::sort(changedRows.begin(), changedRows.end());
    for (qsizetype offset = 0; offset < changedRows.size();) {
        const int first = changedRows.at(offset);
        int last = first;
        ++offset;
        while (offset < changedRows.size()
               && changedRows.at(offset) == last + 1) {
            last = changedRows.at(offset++);
        }
        emit dataChanged(index(first, 0), index(last, 0),
                          {FileListModel::ImageFileRole,
                           FileListModel::ImageIdUrlRole,
                           FileListModel::IsImageRole,
                           FileListModel::ImageFullSizeRole,
                           FileListModel::LastModifiedRole,
                           FileListModel::FileSizeRole,
                           LocalPathRole,
                           VersionTokenRole,
                           KnownImageSizeRole,
                           VisualSnapshotRole});
    }
    if (viewerSourceInvalidated) {
        notifyViewerImageUrlChanged();
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
        if (entry.highlightStyle == style
            && entry.iconPath == iconPath) {
            continue;
        }
        entry.highlightStyle = style;
        entry.iconPath = iconPath;
        if (entry.item) {
            entry.item->setHighlightStyle(style);
            entry.item->setIconPath(iconPath);
        }
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
            if (entry.item) {
                entry.item->setIsSelected(shouldSelect);
            }
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

bool ExternalCatalogModel::applyStateDelta(
    const QString &cursorEntryId, int cursorIndex,
    const QVariantList &selectionChanges) {
    if (_shutdown) {
        return false;
    }

    QList<QPair<int, bool>> validatedChanges;
    validatedChanges.reserve(selectionChanges.size());
    QSet<int> changedRows;
    for (const QVariant &changeValue : selectionChanges) {
        if (changeValue.metaType().id() != QMetaType::QVariantMap) {
            return false;
        }
        const QVariantMap change = changeValue.toMap();
        if (change.size() != 3
            || !change.contains(QStringLiteral("index"))
            || !change.contains(QStringLiteral("entryId"))
            || !change.contains(QStringLiteral("selected"))) {
            return false;
        }
        bool indexOK = false;
        const int sourceIndex = change.value(QStringLiteral("index"))
                                    .toInt(&indexOK);
        const QVariant entryIdValue = change.value(
            QStringLiteral("entryId"));
        const QVariant selectedValue = change.value(
            QStringLiteral("selected"));
        const QString entryId = entryIdValue.toString();
        const int row = rowForEntryId(entryId);
        if (!indexOK || entryIdValue.metaType().id() != QMetaType::QString
            || entryId.isEmpty()
            || selectedValue.metaType().id() != QMetaType::Bool
            || !validRow(row) || _entries.at(row).sourceIndex != sourceIndex
            || changedRows.contains(row)) {
            return false;
        }
        changedRows.insert(row);
        validatedChanges.push_back({row, selectedValue.toBool()});
    }

    QList<int> updatedRows;
    updatedRows.reserve(validatedChanges.size());
    for (const auto &[row, selected] : validatedChanges) {
        Entry &entry = _entries[row];
        if (entry.selected == selected) {
            continue;
        }
        entry.selected = selected;
        if (entry.item) {
            entry.item->setIsSelected(selected);
        }
        updatedRows.push_back(row);
    }
    std::sort(updatedRows.begin(), updatedRows.end());
    for (qsizetype offset = 0; offset < updatedRows.size();) {
        const int first = updatedRows.at(offset);
        int last = first;
        ++offset;
        while (offset < updatedRows.size()
               && updatedRows.at(offset) == last + 1) {
            last = updatedRows.at(offset);
            ++offset;
        }
        emit dataChanged(index(first), index(last),
                         {FileListModel::SelectedRole});
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
    return validRow(row) ? _entries.at(row).originalSize : QSize();
}

QVariantMap ExternalCatalogModel::highlightStyleAt(int row) const {
    return validRow(row) ? _entries.at(row).highlightStyle : QVariantMap();
}

int ExternalCatalogModel::rowForEntryId(const QString &entryId) const {
    return _idToRow.value(entryId, -1);
}

int ExternalCatalogModel::cursorRow() const {
    return _cursorRow;
}

void ExternalCatalogModel::ensurePreviews() {
    // The renderer supplies its actual visible/overscan rows immediately
    // after this lifecycle hook. Starting a catalog-wide probe here used to
    // put thousands of metadata runners ahead of the restored viewport.
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
    ImageFile *item = ensureItem(row);
    return item ? item->imageIdUrl() : QString();
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
    return _viewerImageCache.imageSources(ensureItem(row), viewerSize);
}

void ExternalCatalogModel::requestViewer(
    int row, const QSize &viewportSize) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image ||
        !viewportSize.isValid()) {
        clearViewer();
        return;
    }

    const bool viewportChanged = _viewerViewportSize != viewportSize;
    _viewerViewportSize = viewportSize;
    const QString entryId = _entries.at(row).id;
    if (_viewerEntryId != entryId || viewportChanged) {
        // Supplemental swipe plans are meaningful only for the viewport and
        // presentation mode which created them.  In particular, a native
        // 0x0 plan must never make a later Fit swipe select level 2, and an
        // older Fit size must not advertise an undersized transition frame.
        _viewerPlans.clear();
    }
    if (_viewerEntryId != entryId) {
        _viewerEntryId = entryId;
        notifyViewerImageUrlChanged();
    }
    _viewerPlans.insert(entryId, {viewportSize, 16});
    scheduleViewerDecodeAt(row, viewportSize, 16);
}

void ExternalCatalogModel::requestViewerAt(
    int row, const QSize &viewportSize) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image ||
        !viewportSize.isValid()) {
        return;
    }

    const QString entryId = _entries.at(row).id;
    const int prefetchCount = entryId == _viewerEntryId ? 16 : 1;
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
    scheduleViewerDecodeAt(row, viewportSize, prefetchCount);
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

    _viewerEntryId = entryId;
    _viewerPlans.clear();
    if (_viewerViewportSize.isValid()) {
        _viewerPlans.insert(entryId, {_viewerViewportSize, 16});
    }
    notifyViewerImageUrlChanged();
    scheduleViewerDecode();
}

void ExternalCatalogModel::clearViewer() {
    const bool hadViewer = !_viewerEntryId.isEmpty();
    _decodeManager->cancelViewerRequests(_sessionId);
    _pendingViewerRequests.clear();
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
        const int row = _pathToRow.value(
            QDir::cleanPath(request.info.path), -1);
        if (!validRow(row) || !_entries.at(row).image) {
            continue;
        }

        Entry &entry = _entries[row];
        ImageFile *item = ensureItem(row);
        if (!item) {
            continue;
        }
        request.requestNamespace = _sessionId;
        request.info.requestNamespace = _sessionId;
        request.info.sourceVersionToken = entry.mtimeNs;
        // The legacy cache stores only millisecond versions. External f4
        // entries carry nanoseconds, so decode exactly the physical masonry
        // rect and do not create an unverifiable 1024px cache frame.
        request.checkCache = false;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = false;
        request.thumbnailTransformKey = thumbnailTransformKey(request);
        if (entry.thumbnailRequestedSize.isValid() &&
            (entry.thumbnailRequestedSize != request.targetSize ||
             entry.thumbnailTransformKey != request.thumbnailTransformKey)) {
            ImageDecodeRequest superseded;
            superseded.info = item->info();
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
                _sessionId, entry.sourceIdentity, entry.mtimeNs,
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

    // Masonry sends an additional empty catalogWide request after its
    // visible and overscan requests. Do not let that marker erase the active
    // overscan window.
    if (!catalogWide || !rows.isEmpty()) {
        if (highPriority) {
            _metadataVisibleRows = normalizedRows(rows);
            _metadataLastVisibleRows = QSet<int>(
                _metadataVisibleRows.cbegin(),
                _metadataVisibleRows.cend());
        }
        else {
            _metadataOverscanRows = normalizedRows(rows);
        }
    }
    // Every renderer pass starts with its visible request. Fixed modes never
    // send the trailing catalogWide marker, so this pauses (without losing
    // the scan cursor) any Masonry background walk after a mode switch.
    // Masonry sends catalogWide synchronously before the zero-delay pump and
    // therefore re-enables the same generation without churn.
    _catalogMetadataRequested = catalogWide;
    scheduleMetadataPump();
}

void ExternalCatalogModel::cancelAllRunners() {
    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
}

void ExternalCatalogModel::cancelAllDecodeRunners() {
    // MasonryLayout calls this before replacing DPR/geometry-specific tiles.
    // Cancel only this panel's thumbnails; viewer prefetch and the other f4
    // panel continue independently on the shared runtime.
    _decodeManager->cancelThumbnailRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
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
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _viewerPlans.clear();
    clearViewer();
    _viewerImageCache.clear();
    deleteRetiredItems();
    for (Entry &entry : _entries) {
        clearPublishedImage(entry);
    }
}

void ExternalCatalogModel::handleImageInfo(const ImageInfo &info) {
    if (_shutdown || info.requestNamespace != _sessionId) {
        return;
    }
    const QString cleanPath = QDir::cleanPath(info.path);
    const auto pending = _metadataPendingVersions.constFind(cleanPath);
    if (pending != _metadataPendingVersions.cend() &&
        pending.value() == info.sourceVersionToken) {
        _metadataPendingVersions.remove(cleanPath);
    }
    scheduleMetadataPump();
    const int row = _pathToRow.value(cleanPath, -1);
    if (!validRow(row)) {
        return;
    }
    Entry &entry = _entries[row];
    if (!entry.image || entry.mtimeNs != info.sourceVersionToken) {
        return;
    }
    // A corrupt/unsupported image still completed its metadata probe. Keep
    // that terminal result for this catalog generation so synchronous
    // dataChanged/re-layout cycles cannot enqueue the same failed header
    // read forever. A stale completion from an older catalog version never
    // marks the replacement entry resolved.
    _metadataResolvedPaths.insert(cleanPath);
    if (!versionMatches(entry.mtimeNs, entry.size, info)) {
        return;
    }
    ImageInfo currentInfo = info;
    if (entry.mtimeNs != 0) {
        currentInfo.lastModified = QDateTime::fromMSecsSinceEpoch(
            entry.mtimeNs / 1000000, QTimeZone::UTC);
    }
    currentInfo.sourceVersionToken = entry.mtimeNs;
    if (entry.size >= 0) {
        currentInfo.fileSize = entry.size;
    }
    entry.imageInfo = currentInfo;
    entry.originalSize = rotateToOrientation(
        currentInfo.imageSize, currentInfo.orientation);
    if (entry.item) {
        entry.item->setInfo(entry.imageInfo);
        entry.item->setFullSize(entry.originalSize);
    }
    QList<int> roles{FileListModel::ImageFullSizeRole,
                     KnownImageSizeRole};
    if (info.isLast) {
        roles.append(FileListModel::TimeToFlushRole);
    }
    emit dataChanged(index(row), index(row), roles);
    const auto plans = _viewerPlans;
    for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
        const int centerRow = rowForEntryId(plan.key());
        if (validRow(centerRow)) {
            scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                   plan->prefetchCount);
        }
    }
}

void ExternalCatalogModel::handleImageReady(
    const ImageDecodeRequest &request, const QImage &image,
    const DecodedImageInfo &decodedInfo) {
    if (request.requestNamespace != _sessionId) {
        return;
    }
    if (request.viewerRequest) {
        _pendingViewerRequests.remove(viewerRequestKey(request));
    }
    else {
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
    const int row = _pathToRow.value(QDir::cleanPath(request.info.path), -1);
    if (!validRow(row)) {
        return;
    }
    Entry &entry = _entries[row];
    if (!entry.image ||
        !versionMatches(entry.mtimeNs, entry.size, request.info)) {
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
            _sessionId, entry.sourceIdentity, entry.mtimeNs,
            entry.size, request.targetSize,
            thumbnailTransformKey(request), image);
    }
}

void ExternalCatalogModel::releaseFailedThumbnailRequest(
    const ImageDecodeRequest &request) {
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
    _thumbnailCache->releaseRequest(
        _sessionId, request.info.path,
        request.info.sourceVersionToken, request.info.fileSize,
        request.targetSize, thumbnailTransformKey(request), false);
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
    ImageFile *item = ensureItem(row);
    if (!item) {
        return;
    }
    if (entry.thumbnailProviderId == providerId &&
        item->imageIdUrl().endsWith(providerId)) {
        return;
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        _providerEntryIds.remove(entry.thumbnailProviderId, entry.id);
    }
    else {
        // Remove only a legacy per-session publication. Shared cache IDs are
        // owned solely by ThumbnailMemoryCache and remain valid for users in
        // the other panel.
        const QString legacyId = item->imageIdUrl().section(
            QLatin1Char('/'), -1);
        if (!legacyId.isEmpty()) {
            _store->remove(legacyId);
        }
    }
    entry.thumbnailProviderId = providerId;
    _providerEntryIds.insert(providerId, entry.id);
    item->setImage({}, {});
    item->setImageId(providerId);
    emit dataChanged(index(row), index(row),
                     {FileListModel::ImageIdUrlRole});
    emit viewerSourceAtChanged(row);
    if (entry.id == _viewerEntryId &&
        _viewerImageCache.bestImageUrl(item) == item->imageIdUrl()) {
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
        entry.sourceIdentity, entry.mtimeNs, entry.size,
        entry.thumbnailRequestedSize, entry.thumbnailTransformKey);
    if (!cached.isValid()) {
        return false;
    }
    attachThumbnail(row, cached.providerId);
    return true;
}

void ExternalCatalogModel::handleThumbnailFrameAvailable(
    const QString &sourceIdentity, qint64 versionToken,
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
        if (entry.mtimeNs != versionToken ||
            (entry.size >= 0 && sourceFileSize >= 0 &&
             entry.size != sourceFileSize)) {
            continue;
        }
        ImageFile *item = ensureItem(row);
        if (!item) {
            continue;
        }
        ImageDecodeRequest desired;
        desired.info = item->info();
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
        if (entry.item) {
            entry.item->setImageId({});
            entry.item->setImage({}, {});
        }
        emit dataChanged(index(row), index(row),
                         {FileListModel::ImageIdUrlRole});
        emit viewerSourceAtChanged(row);
        if (entry.id == _viewerEntryId) {
            notifyViewerImageUrlChanged();
        }
    }
}

void ExternalCatalogModel::handleThumbnailRequestReleased(
    const QString &sourceIdentity, qint64 versionToken,
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
        if (entry.mtimeNs != versionToken ||
            (entry.size >= 0 && sourceFileSize >= 0 &&
             entry.size != sourceFileSize) ||
            !entry.thumbnailRequestedSize.isValid()) {
            continue;
        }
        ImageDecodeRequest desired;
        ImageFile *item = ensureItem(row);
        if (!item) {
            continue;
        }
        desired.info = item->info();
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
    scheduleMetadataPump();
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
        if (!entry.image || entry.localPath.isEmpty() ||
            entry.originalSize.isValid()) {
            return false;
        }
        const QString cleanPath = QDir::cleanPath(entry.localPath);
        return !_metadataPendingVersions.contains(cleanPath) &&
            !_metadataResolvedPaths.contains(cleanPath);
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
        if (!entry.image || entry.localPath.isEmpty() ||
            entry.originalSize.isValid()) {
            return;
        }
        const QString cleanPath = QDir::cleanPath(entry.localPath);
        if (_metadataPendingVersions.contains(cleanPath) ||
            _metadataResolvedPaths.contains(cleanPath)) {
            return;
        }
        _metadataPendingVersions.insert(cleanPath, entry.mtimeNs);
        requests.append({entry.localPath, entry.mtimeNs});
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

    // A viewer's current image and the actual visible viewport always enter
    // DecodeManager before overscan or the background Masonry aspect scan.
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

void ExternalCatalogModel::resetMetadataPlanner() {
    _metadataPendingVersions.clear();
    _metadataResolvedPaths.clear();
    _metadataVisibleRows.clear();
    _metadataLastVisibleRows.clear();
    _metadataOverscanRows.clear();
    _metadataUrgentRows.clear();
    _metadataAdHocRows.clear();
    _catalogMetadataCursor = 0;
    _metadataPeakPending = 0;
    _metadataSubmittedBatches = 0;
    _catalogMetadataRequested = false;
    _metadataPumpScheduled = false;
}

void ExternalCatalogModel::scheduleViewerDecode() {
    const int row = rowForEntryId(_viewerEntryId);
    if (_shutdown || !validRow(row) || !_viewerViewportSize.isValid()) {
        return;
    }
    _viewerPlans.insert(_viewerEntryId, {_viewerViewportSize, 16});
    scheduleViewerDecodeAt(row, _viewerViewportSize, 16);
}

void ExternalCatalogModel::scheduleViewerDecodeAt(
    int row, const QSize &viewportSize, int prefetchCount) {
    if (_shutdown || !validRow(row) || !_entries.at(row).image ||
        !viewportSize.isValid() || prefetchCount <= 0) {
        return;
    }

    const QList<int> candidates = viewerCandidateRows(row, prefetchCount);
    for (const int candidateRow : candidates) {
        const Entry &candidate = _entries.at(candidateRow);
        if (!candidate.originalSize.isValid()) {
            requestImageMetadataForRow(candidateRow, candidateRow == row);
        }
    }

    ViewerImageCache::RequestPlan plan = _viewerImageCache.planRequest(
        viewerItems(row, prefetchCount), row, viewportSize, prefetchCount);
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
        const int requestRow = _pathToRow.value(
            QDir::cleanPath(request.info.path), -1);
        if (validRow(requestRow)) {
            request.info.sourceVersionToken =
                _entries.at(requestRow).mtimeNs;
        }
        // External catalogs carry an opaque nanosecond version. Until the
        // persistent cache stores that token, only the version-aware in-memory
        // viewer cache can prove a hit is current.
        request.checkCache = false;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = false;
        const QString key = viewerRequestKey(request);
        if (_pendingViewerRequests.contains(key)) {
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

QList<ImageFile *> ExternalCatalogModel::viewerItems(
    int centerRow, int prefetchCount) const {
    QList<ImageFile *> result;
    result.resize(_entries.size());
    for (const int row : viewerCandidateRows(centerRow, prefetchCount)) {
        result[row] = ensureItem(row);
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
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4x%5\x1f%6")
        .arg(request.info.path)
        .arg(request.info.sourceVersionToken)
        .arg(request.info.fileSize)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(request.fitToViewerRequest ? 1 : 0);
}

QString ExternalCatalogModel::thumbnailRequestKey(
    const ImageDecodeRequest &request) const {
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4x%5\x1f%6")
        .arg(request.info.path)
        .arg(request.info.sourceVersionToken)
        .arg(request.info.fileSize)
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
        .arg(entry.mtimeNs)
        .arg(entry.size)
        .arg(++_nextImageSerial);
}

} // namespace ZoinGallery
