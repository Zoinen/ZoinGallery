#include "ExternalDirectoryPreviews.h"
#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {
void ExternalCatalogModel::configureDirectoryPreviews(QSharedPointer<DirectoryPreviewProvider> provider,
    QSharedPointer<QThreadPool> pool, QSharedPointer<DirectoryPreviewCache> cache,
    QSharedPointer<DirectoryPreviewScheduler> scheduler) {
    if (_directoryPreviews || !provider || !pool) return;
    _directoryPreviewCache = cache ? std::move(cache) : QSharedPointer<DirectoryPreviewCache>::create();
    if (!scheduler) scheduler = QSharedPointer<DirectoryPreviewScheduler>::create(std::move(pool));
    _directoryPreviews = new ExternalDirectoryPreviews(this, std::move(provider), _directoryPreviewCache, std::move(scheduler));
}
void ExternalCatalogModel::requestDirectoryPreviews(const QList<int> &rows) {
    if (_directoryPreviews) {
        _directoryPreviews->demand(_thumbnailsEnabled ? rows : QList<int>{});
    }
}
void ExternalCatalogModel::clearDirectoryPreviews() {
    if (_directoryPreviewCache) _directoryPreviewCache->clear();
    if (_directoryPreviews) _directoryPreviews->clear();
}
void ExternalCatalogModel::setDirectoryCacheMode(int mode) { if (_directoryPreviews) _directoryPreviews->setCacheMode(mode); }
void ExternalCatalogModel::invalidatePreviewPixels() {
    cancelAllRunners();
    clearDirectoryPreviews();
    _viewerImageCache.clear();
    for (auto &entry : _entries) clearPublishedImage(entry);
    if (rowCount()) emit dataChanged(index(0), index(rowCount() - 1), {FileListModel::ImageFullSizeRole});
}
QAbstractItemModel *ExternalCatalogModel::directoryPreviewModel(int row) const { return _directoryPreviews ? _directoryPreviews->model(entryIdAt(row)) : nullptr; }
bool ExternalCatalogModel::directoryPreviewAvailable(int row) const {
    const auto *entry = entryAt(row);
    return entry && entry->directoryPreviewState == DirectoryPreviewState::HasImages;
}

void ExternalCatalogModel::prepareDirectoryPreviewStates(const QVariantList &values) {
    _incomingDirectoryStates.clear();
    if (!_directoryPreviews) return;
    const auto started = MediaTimingTrace::enabled() ? MediaTimingTrace::monotonicNanoseconds() : 0;
    QStringList keys;
    keys.reserve(values.size());
    for (const auto &value : values) {
        const auto map = value.toMap();
        if (!map.value(QStringLiteral("isDir"), map.value(QStringLiteral("directory"))).toBool()) continue;
        const auto descriptor = map.value(QStringLiteral("directorySource")).toMap();
        const QString key = descriptor.value(QStringLiteral("sourceKey")).toString();
        if (!key.isEmpty() && !descriptor.value(QStringLiteral("resourceId")).toString().isEmpty()) {
            keys.append(key);
            // Cache Off still preserves the current catalog's presentation
            // during sorting; it cannot restore a departed catalog.
            if (!_directoryPreviews->usesCache()) {
                const auto *old = entryAt(rowForEntryId(map.value(QStringLiteral("entryId")).toString()));
                if (old && old->directorySource.sourceKey == key)
                    _incomingDirectoryStates.insert(key, old->directoryPreviewState);
            }
        }
    }
    if (_directoryPreviews->usesCache()) _incomingDirectoryStates = _directoryPreviewCache->states(keys);
    if (!started) return;
    const auto elapsed = MediaTimingTrace::monotonicNanoseconds() - started;
    QVariantList restored;
    for (const auto &value : values) {
        const auto map = value.toMap();
        const auto key = map.value(QStringLiteral("directorySource")).toMap().value(QStringLiteral("sourceKey")).toString();
        if (key.isEmpty()) continue;
        restored.append(QVariantMap{{QStringLiteral("entryId"), map.value(QStringLiteral("entryId"))},
            {QStringLiteral("sourceKey"), key},
            {QStringLiteral("state"), int(_incomingDirectoryStates.value(key, DirectoryPreviewState::Unknown))}});
    }
    MediaTimingTrace::event(QStringLiteral("qt.directory.snapshots.restored"), {
        {QStringLiteral("sessionId"), _sessionId}, {QStringLiteral("folders"), keys.size()},
        {QStringLiteral("benchmarkTraceId"), property("directoryTraceId")},
        {QStringLiteral("catalogRevision"), property("directoryTraceRevision")},
        {QStringLiteral("states"), restored},
        {QStringLiteral("hits"), _incomingDirectoryStates.size()}, {QStringLiteral("tier"), QStringLiteral("ram")},
        {QStringLiteral("durationNs"), elapsed}});
}

void ExternalDirectoryPreviews::capture(const QSharedPointer<Record> &record) {
    if (_clearing || !usesCache() || record->state == DirectoryPreviewState::Unknown) return;
    DirectoryPreviewSnapshot snapshot;
    snapshot.state = record->state;
    for (const auto &entry : record->model->_entries) {
        snapshot.children.append({entry.name, entry.sourceIdentity, entry.contentVersion,
            entry.size, entry.mtimeNs, entry.originalSize, entry.thumbnailRequestedSize,
            entry.thumbnailTransformKey, entry.thumbnailKind});
    }
    _cache->store(record->source.sourceKey, std::move(snapshot));
}

void ExternalDirectoryPreviews::restore(const QString &id, const QSharedPointer<Record> &record) {
    if (!usesCache()) return;
    const auto snapshot = _cache->snapshot(record->source.sourceKey);
    record->state = snapshot.state;
    auto *model = record->model;
    // Build a read-disabled presentation model without parsing a source
    // descriptor or probing metadata. Geometry and pixel keys come from RAM.
    model->suspendPreviewReads(true);
    model->beginResetModel();
    for (const auto &child : snapshot.children) {
        ExternalCatalogModel::Entry entry;
        entry.id = id + QChar(0x1f) + child.name;
        entry.sourceIndex = model->_entries.size();
        entry.loaded = true;
        entry.name = child.name;
        entry.image = true;
        entry.thumbnailKind = child.thumbnailKind;
        entry.sourceIdentity = child.sourceIdentity;
        entry.contentVersion = child.contentVersion;
        entry.size = child.size;
        entry.mtimeNs = child.mtimeNs;
        entry.originalSize = child.originalSize;
        entry.metadataSettled = entry.originalSize.isValid();
        entry.imageInfo.path = child.sourceIdentity;
        entry.imageInfo.requestNamespace = model->_sessionId;
        entry.imageInfo.sourceVersionToken = child.contentVersion;
        entry.imageInfo.fileSize = child.size;
        entry.imageInfo.thumbnailKind = child.thumbnailKind;
        entry.imageInfo.imageSize = child.originalSize;
        entry.thumbnailRequestedSize = child.thumbnailSize;
        entry.thumbnailTransformKey = child.thumbnailTransformKey;
        model->_idToRow.insert(entry.id, entry.sourceIndex);
        model->_sourceToRow.insert(entry.sourceIdentity, entry.sourceIndex);
        model->_sourceEntryIds.insert(entry.sourceIdentity, entry.id);
        model->_entries.append(std::move(entry));
    }
    model->endResetModel();
}
}
