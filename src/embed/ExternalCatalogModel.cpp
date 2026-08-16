#include "ExternalCatalogModel.h"

#include "DecodeManager.h"
#include "ProviderImageStore.h"
#include "ThumbnailMemoryCache.h"
#include "ViewerImageCache.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
        return entry.mtimeNs;
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
            : (!directory && FileListModel::isImage(name));
        const qint64 mtimeNs = integerValue(
            map, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
        const qint64 size = integerValue(
            map, QStringLiteral("size"), QStringLiteral("fileSize"), -1);
        const QVariantMap normalizedDisplayFields = displayFields(map);

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

    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _viewerPlans.clear();

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
            : (!entry.directory && FileListModel::isImage(entry.name));
        entry.selected = map.value(QStringLiteral("selected")).toBool();
        entry.mtimeNs = integerValue(map, QStringLiteral("mtimeNs"),
                                     QStringLiteral("mtimeNanos"), 0);
        entry.size = integerValue(map, QStringLiteral("size"),
                                  QStringLiteral("fileSize"), -1);
        entry.sourceIdentity =
            ThumbnailMemoryCache::canonicalSourceIdentity(entry.localPath);
        entry.displayFields = displayFields(map);

        Entry old = previous.take(entry.id);
        entry.item = old.item ? old.item : new ImageFile(this);
        const bool sourceChanged = old.item &&
            (old.localPath != entry.localPath || old.mtimeNs != entry.mtimeNs ||
             old.size != entry.size || old.image != entry.image);
        if (sourceChanged) {
            _viewerImageCache.remove(old.localPath);
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
        info.path = entry.localPath;
        info.requestNamespace = _sessionId;
        info.sourceVersionToken = entry.mtimeNs;
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
            nextViewerSources.insert(
                entry.localPath + QChar(0x1f) +
                QString::number(entry.mtimeNs) + QChar(0x1f) +
                QString::number(entry.size));
        }
        next.append(entry);
    }

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
        delete entry.item;
    }

    _entries = std::move(next);
    _idToRow.clear();
    _pathToRow.clear();
    _sourceEntryIds.clear();
    _providerEntryIds.clear();
    for (int row = 0; row < _entries.size(); ++row) {
        const Entry &entry = _entries.at(row);
        _idToRow.insert(entry.id, row);
        if (!entry.localPath.isEmpty()) {
            _pathToRow.insert(QDir::cleanPath(entry.localPath), row);
        }
        if (!entry.sourceIdentity.isEmpty()) {
            _sourceEntryIds.insert(entry.sourceIdentity, entry.id);
        }
        if (!entry.thumbnailProviderId.isEmpty()) {
            _providerEntryIds.insert(entry.thumbnailProviderId, entry.id);
        }
    }
    if (_entries.isEmpty()) {
        _cursorRow = -1;
    } else {
        _cursorRow = qBound(0, _cursorRow, _entries.size() - 1);
    }
    endResetModel();

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
    return _viewerImageCache.imageSources(entry.item, viewerSize);
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
    entry.item->setInfo(currentInfo);
    entry.item->setFullSize(
        rotateToOrientation(currentInfo.imageSize, currentInfo.orientation));
    QList<int> roles{FileListModel::ImageFullSizeRole};
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
            (entry.item && entry.item->fullSize().isValid())) {
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
            (entry.item && entry.item->fullSize().isValid())) {
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
        if (!candidate.item || !candidate.item->fullSize().isValid()) {
            requestImageMetadataForRow(candidateRow, candidateRow == row);
        }
    }

    ViewerImageCache::RequestPlan plan = _viewerImageCache.planRequest(
        viewerItems(), row, viewportSize, prefetchCount);
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
