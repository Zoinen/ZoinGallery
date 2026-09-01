#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

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

    connect(_decodeManager, &DecodeManager::imageProbeReady, this,
            &ExternalCatalogModel::handleImageProbe);
    connect(_decodeManager, &DecodeManager::imageInfoReady, this,
            &ExternalCatalogModel::handleImageInfo);
    connect(_decodeManager, &DecodeManager::imagesInfoReady, this,
            &ExternalCatalogModel::handleImageInfos);
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
    deleteRetiredItems();
    for (Entry &entry : _entries) {
        delete entry.item;
        entry.item = nullptr;
    }
}

int ExternalCatalogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : logicalRowCount();
}

bool ExternalCatalogModel::sparseCatalog() const {
    return _virtualRowCount >= 0;
}

QVariantList ExternalCatalogModel::materializedRows() const {
    QVariantList rows;
    rows.reserve(_entries.size());
    for (const Entry &entry : _entries) {
        if (entry.loaded && entry.sourceIndex >= 0) {
            rows.append(entry.sourceIndex);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const QVariant &left,
                                           const QVariant &right) {
        return left.toInt() < right.toInt();
    });
    return rows;
}

int ExternalCatalogModel::logicalRowCount() const {
    return _virtualRowCount >= 0 ? _virtualRowCount : _entries.size();
}

const ExternalCatalogModel::Entry *ExternalCatalogModel::entryAt(
    int row) const {
    if (row < 0 || row >= logicalRowCount()) {
        return nullptr;
    }
    if (_virtualRowCount < 0) {
        return &_entries.at(row);
    }
    const auto offset = _sparseRowToOffset.constFind(row);
    if (offset == _sparseRowToOffset.cend()
        || offset.value() < 0 || offset.value() >= _entries.size()) {
        return nullptr;
    }
    return &_entries.at(offset.value());
}

ExternalCatalogModel::Entry *ExternalCatalogModel::entryAt(int row) {
    return const_cast<Entry *>(
        std::as_const(*this).entryAt(row));
}

const ExternalCatalogModel::Entry &ExternalCatalogModel::loadedEntry(
    int row) const {
    const Entry *entry = entryAt(row);
    Q_ASSERT(entry && entry->loaded);
    return *entry;
}

ExternalCatalogModel::Entry &ExternalCatalogModel::loadedEntry(int row) {
    Entry *entry = entryAt(row);
    Q_ASSERT(entry && entry->loaded);
    return *entry;
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
    Entry *entry = const_cast<ExternalCatalogModel *>(this)->entryAt(row);
    if (!entry || !entry->loaded) {
        return nullptr;
    }
    if (entry->item) {
        return entry->item;
    }

    QString folder;
    QString fileName = entry->name;
    if (!entry->localPath.isEmpty()) {
        const QFileInfo pathInfo(entry->localPath);
        folder = pathInfo.absolutePath();
        if (fileName.isEmpty()) {
            fileName = pathInfo.fileName();
        }
    }
    auto *item = new ImageFile(
        const_cast<ExternalCatalogModel *>(this));
    item->initializeExternalCatalogRow(
        folder, fileName, row, entry->directory, entry->image,
        entry->iconPath.isEmpty()
            ? defaultIconPath(entry->directory, entry->image)
            : entry->iconPath,
        entry->selected, _thumbnailProviderName, entry->imageInfo,
        entry->highlightStyle, entry->displayFields,
        entry->metadataDeferred);
    // A retained provider frame belongs to this current catalog entry even
    // when its QObject facade is created only after the first painted frame.
    // Restore that identity before exposing the facade so thumbnail planning
    // cannot mistake an already-published frame for an empty request.
    if (!entry->thumbnailProviderId.isEmpty()) {
        item->setImageId(entry->thumbnailProviderId);
    }
    if (entry->originalSize.isValid()) {
        item->setFullSize(entry->originalSize);
    }
    entry->item = item;
    return item;
}

QVariantMap ExternalCatalogModel::visualSnapshot(int row) const {
    if (row < 0 || row >= logicalRowCount()) {
        return {};
    }
    const Entry *entry = entryAt(row);
    if (!entry || !entry->loaded) {
        return {
            {QStringLiteral("valid"), false},
            {QStringLiteral("sourceIndex"), row},
        };
    }
    QString imageIdUrl;
    if (!entry->thumbnailProviderId.isEmpty()) {
        imageIdUrl = QStringLiteral("image://") + _thumbnailProviderName
            + QLatin1Char('/') + entry->thumbnailProviderId;
    }
    else if (entry->item) {
        imageIdUrl = entry->item->imageIdUrl();
    }
    QVariantMap visualFields = entry->displayFields;
    const QFileInfo nameInfo(entry->name);
    if (!visualFields.contains(QStringLiteral("displayBaseName"))) {
        QString baseName = entry->directory
            ? entry->name : nameInfo.completeBaseName();
        if (baseName.isEmpty()) {
            baseName = entry->name;
        }
        visualFields.insert(QStringLiteral("displayBaseName"), baseName);
    }
    if (!visualFields.contains(QStringLiteral("displayExtension"))) {
        visualFields.insert(QStringLiteral("displayExtension"),
                            entry->directory ? QString() : nameInfo.suffix());
    }
    if (!visualFields.contains(QStringLiteral("sizeText"))) {
        const qint64 effectiveSize = entry->size >= 0
            ? entry->size : entry->imageInfo.fileSize;
        visualFields.insert(QStringLiteral("sizeText"),
                            snapshotFileSize(effectiveSize));
    }
    if (!visualFields.contains(QStringLiteral("mtimeText"))) {
        visualFields.insert(
            QStringLiteral("mtimeText"),
            entry->imageInfo.lastModified.isValid()
                ? QLocale().toString(entry->imageInfo.lastModified,
                                     QLocale::ShortFormat)
                : QString());
    }
    if (!visualFields.contains(QStringLiteral("modeText"))) {
        visualFields.insert(QStringLiteral("modeText"), QString());
    }
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("entryId"), entry->id},
        {QStringLiteral("sourceIndex"), entry->sourceIndex},
        {QStringLiteral("localPath"), entry->localPath},
        {QStringLiteral("text"), entry->name},
        {QStringLiteral("isFolder"), entry->directory},
        {QStringLiteral("isImage"), entry->image},
        {QStringLiteral("isSelected"), entry->selected},
        {QStringLiteral("iconPath"), entry->iconPath},
        {QStringLiteral("iconKey"), entry->iconKey},
        {QStringLiteral("highlightStyle"), entry->highlightStyle},
        {QStringLiteral("displayFields"), visualFields},
        {QStringLiteral("imageIdUrl"), imageIdUrl},
    };
}

bool ExternalCatalogModel::setEntryHighlightStyle(
    Entry &entry, const QVariantMap &highlightStyle) const {
    const QString iconKey = highlightStyle.value(
        QStringLiteral("iconKey")).toString();
    const QString styledIcon = highlightStyle.value(
        QStringLiteral("icon")).toString();
    const QString iconPath = styledIcon.isEmpty()
        ? defaultIconPath(entry.directory, entry.image) : styledIcon;
    if (entry.highlightStyle == highlightStyle
        && entry.iconPath == iconPath && entry.iconKey == iconKey) {
        return false;
    }
    entry.highlightStyle = highlightStyle;
    entry.iconPath = iconPath;
    entry.iconKey = iconKey;
    if (entry.item) {
        entry.item->setHighlightStyle(entry.highlightStyle);
        entry.item->setIconPath(entry.iconPath);
    }
    return true;
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
    if (!index.isValid() || index.row() < 0
        || index.row() >= logicalRowCount()) {
        return {};
    }
    const Entry *entry = entryAt(index.row());
    if (!entry || !entry->loaded) {
        if (role == SourceIndexRole) {
            return index.row();
        }
        if (role == VisualSnapshotRole) {
            return visualSnapshot(index.row());
        }
        return {};
    }
    ImageFile *item = nullptr;
    switch (role) {
    case FileListModel::ImageIdUrlRole:
        item = ensureItem(index.row());
        return item ? item->imageIdUrl() : QString();
    case FileListModel::SelectedRole:
        return entry->selected;
    case FileListModel::SelectionGroupIdRole:
        item = ensureItem(index.row());
        return item ? item->selectionGroupId() : QString();
    case FileListModel::SelectionGroupColorRole:
        item = ensureItem(index.row());
        return item ? item->selectionGroupColor() : QColor();
    case FileListModel::ImageFileRole:
        return QVariant::fromValue(ensureItem(index.row()));
    case FileListModel::FolderRole:
        return entry->directory;
    case FileListModel::IsImageRole:
        return entry->image;
    case FileListModel::ImageFullSizeRole:
        return entry->originalSize;
    case FileListModel::FolderViewRole:
        return false;
    case FileListModel::LastModifiedRole:
        return entry->imageInfo.lastModified;
    case FileListModel::FileSizeRole:
        return entry->size;
    case EntryIdRole:
        return entry->id;
    case SourceIndexRole:
        return entry->sourceIndex;
    case LocalPathRole:
        return entry->localPath;
    case VersionTokenRole:
        return entry->contentVersion;
    case EntryNameRole:
        return entry->name;
    case KnownImageSizeRole:
        return entry->originalSize;
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
    if (_virtualRowCount >= 0 || values.size() != _entries.size()) {
        return false;
    }

    QSet<QString> seenIds;
    for (int row = 0; row < values.size(); ++row) {
        const QVariantMap map = values.at(row).toMap();
        if (carriesAppearance
            && map.contains(QStringLiteral("highlightStyle"))) {
            *carriesAppearance = true;
        }
        const Entry &current = loadedEntry(row);
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
            : (!metadataDeferred && !directory
               && FileListModel::isImage(name));
        const qint64 mtimeNs = integerValue(
            map, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
        const qint64 size = sourceSizeValue(map);
        const ImageSourceDescriptor source =
            sourceDescriptor(map, name, size, mtimeNs);
        const QVariantMap normalizedDisplayFields = catalogDisplayFields(
            map, metadataDeferred);

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

bool ExternalCatalogModel::parseCatalogEntry(
    const QVariantMap &map, int row, bool metadataDeferred,
    Entry *entry) const {
    if (!entry || row < 0) {
        return false;
    }
    bool indexOK = false;
    const int sourceIndex = map.value(
        QStringLiteral("index"), row).toInt(&indexOK);
    const QString id = map.value(QStringLiteral("entryId")).toString();
    if (!indexOK || sourceIndex != row || id.isEmpty()
        || map.value(QStringLiteral("name")).metaType().id()
            != QMetaType::QString) {
        return false;
    }

    Entry parsed;
    parsed.loaded = true;
    parsed.id = id;
    parsed.sourceIndex = sourceIndex;
    parsed.name = map.value(QStringLiteral("name")).toString();
    parsed.localPath = map.value(
        QStringLiteral("localPath"), map.value(QStringLiteral("path")))
                           .toString();
    parsed.directory = map.value(
        QStringLiteral("isDir"), map.value(QStringLiteral("directory")))
                           .toBool();
    parsed.image = map.contains(QStringLiteral("isImage"))
        ? map.value(QStringLiteral("isImage")).toBool()
        : (!metadataDeferred && !parsed.directory
           && FileListModel::isImage(parsed.name));
    parsed.selected = map.value(QStringLiteral("selected")).toBool();
    if (metadataDeferred) {
        parsed.mtimeNs = 0;
        parsed.size = -1;
    }
    else {
        parsed.mtimeNs = integerValue(
            map, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
        parsed.size = sourceSizeValue(map);
    }
    parsed.source = sourceDescriptor(
        map, parsed.name, parsed.size, parsed.mtimeNs);
    parsed.contentVersion = parsed.source.contentVersion;
    parsed.sourceIdentity = parsed.source.runtimeIdentity();
    parsed.displayFields = catalogDisplayFields(map, metadataDeferred);
    parsed.metadataDeferred = metadataDeferred;
    parsed.imageInfo.path = parsed.sourceIdentity;
    parsed.imageInfo.source = parsed.source;
    parsed.imageInfo.requestNamespace = _sessionId;
    parsed.imageInfo.sourceVersionToken = parsed.contentVersion;
    parsed.imageInfo.lastModified = parsed.mtimeNs != 0
        ? QDateTime::fromMSecsSinceEpoch(
              parsed.mtimeNs / 1000000, QTimeZone::UTC)
        : QDateTime{};
    parsed.imageInfo.fileSize = parsed.size;
    if (map.contains(QStringLiteral("highlightStyle"))) {
        parsed.highlightStyle = map.value(
            QStringLiteral("highlightStyle")).toMap();
    }
    setEntryHighlightStyle(parsed, parsed.highlightStyle);
    *entry = std::move(parsed);
    return true;
}


} // namespace ZoinGallery
