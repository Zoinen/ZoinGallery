#include "ExternalDirectoryPreviews.h"
#include "ExternalCatalogModel.h"
#include "FolderPreviewSelection.h"
#include <ZoinGallery/MediaTimingTrace.h>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>

namespace ZoinGallery {

ExternalDirectoryPreviews::ExternalDirectoryPreviews(ExternalCatalogModel *catalog,
    QSharedPointer<DirectoryPreviewProvider> provider, QSharedPointer<DirectoryPreviewCache> cache,
    QSharedPointer<DirectoryPreviewScheduler> scheduler)
    : QObject(catalog), _catalog(catalog), _provider(std::move(provider)),
      _cache(std::move(cache)), _scheduler(std::move(scheduler))
{
    const auto changed = [this] {
        if (_scheduled) return;
        _scheduled = true;
        QTimer::singleShot(0, this, [this] { _scheduled = false; synchronize(); });
    };
    connect(catalog, &QAbstractItemModel::modelReset, this, changed);
    connect(catalog, &QAbstractItemModel::dataChanged, this, changed);
    connect(catalog, &QAbstractItemModel::rowsRemoved, this, changed);
    connect(catalog, &QAbstractItemModel::rowsInserted, this, changed);
    connect(catalog, &QAbstractItemModel::rowsMoved, this, changed);
    connect(catalog, &QAbstractItemModel::layoutChanged, this, changed);
    connect(_scheduler.data(), &DirectoryPreviewScheduler::capacityAvailable, this, changed);
}
ExternalDirectoryPreviews::~ExternalDirectoryPreviews() {
    // ExternalCatalogModel::shutdown clears us before its Entry storage is
    // destroyed. QObject destroys children later, after that storage is gone.
    Q_ASSERT(_records.isEmpty());
}
void ExternalDirectoryPreviews::clear() {
    _clearing = true;
    _demand.clear();
    for (const auto &id : _records.keys()) retire(id);
    for (auto &entry : _catalog->_entries) {
        entry.directoryPreviewState = DirectoryPreviewState::Unknown;
        if (entry.directory) publish(entry.id);
    }
    _clearing = false;
}
void ExternalDirectoryPreviews::retire(const QString &id) {
    const auto record = _records.take(id);
    if (!record) return;
    capture(record);
    ++record->serial;
    if (record->cancel) record->cancel->cancel();
    record->lease.reset();
    if (record->model) { record->model->shutdown(); record->model->deleteLater(); }
    publish(id);
}
ExternalCatalogModel *ExternalDirectoryPreviews::model(const QString &id) const {
    const auto record = _records.value(id);
    const auto *entry = _catalog->entryAt(_catalog->rowForEntryId(id));
    return record && entry && entry->directorySource.isValid()
        && record->source.sourceKey == entry->directorySource.sourceKey ? record->model : nullptr;
}
void ExternalDirectoryPreviews::demand(const QList<int> &rows) { _demand = rows; synchronize(); }
void ExternalDirectoryPreviews::setCacheMode(int mode) {
    if (_cacheMode == mode) return;
    _cacheMode = mode;
    if (mode == 1) for (const auto &record : _records) capture(record);
    if (mode == 2) {
        for (const auto &record : _records) {
            ++record->serial;
            if (record->cancel) record->cancel->cancel();
            record->cancel.reset();
            record->model->suspendPreviewReads(true);
            record->lease.reset();
            record->settled = false;
        }
    }
    synchronize();
}

void ExternalDirectoryPreviews::invalidateSources(const QVariantList &entries, bool replacingCatalog) {
    // Revoke reads before row publication can rebind an existing grid. The
    // last rendered pixels remain usable while new authority is acquired.
    QHash<QString, DirectorySourceDescriptor> incoming;
    for (const auto &value : entries) {
        const auto map = value.toMap();
        const auto descriptor = map.value(QStringLiteral("directorySource")).toMap();
        incoming.insert(map.value(QStringLiteral("entryId")).toString(),
            {descriptor.value(QStringLiteral("resourceId")).toString(),
             descriptor.value(QStringLiteral("sourceKey")).toString(),
             descriptor.value(QStringLiteral("version")).toString()});
    }
    for (auto it = _records.begin(); it != _records.end(); ++it) {
        const auto source = incoming.constFind(it.key());
        if (source == incoming.cend() ? !replacingCatalog : *source == it.value()->source) continue;
        const auto &record = it.value();
        if (!record->cancel && !record->lease && record->model->_previewReadsSuspended) continue;
        capture(record);
        ++record->serial;
        if (record->cancel) record->cancel->cancel();
        record->cancel.reset();
        record->model->suspendPreviewReads(true);
        record->lease.reset();
        record->settled = false;
        record->queuedNs = 0;
        if (MediaTimingTrace::enabled()) MediaTimingTrace::event(QStringLiteral("qt.directory.cancelled"), {
            {QStringLiteral("benchmarkTraceId"), _catalog->property("directoryTraceId")},
            {QStringLiteral("entryId"), it.key()}, {QStringLiteral("sourceKey"), record->source.sourceKey},
            {QStringLiteral("reason"), QStringLiteral("catalog-authority-replaced")}});
    }
}

void ExternalDirectoryPreviews::synchronize() {
    if (_catalog->_shutdown || _clearing) return;
    QSet<QString> visible;
    QStringList ordered;
    for (int row : _demand) {
        const auto *entry = _catalog->entryAt(row);
        if (entry && entry->directory && entry->directorySource.isValid() && !visible.contains(entry->id)) {
            visible.insert(entry->id);
            ordered.append(entry->id);
        }
    }
    for (const auto &id : _records.keys()) {
        const auto record = _records.value(id);
        const auto *entry = _catalog->entryAt(_catalog->rowForEntryId(id));
        if (!entry) {
            if (_cacheMode == 0) { retire(id); continue; }
            // Keep render state across navigation, but never retain authority
            // to start reads from a catalog that is no longer displayed.
            if (record->demanded || record->cancel || record->lease) {
                capture(record);
                ++record->serial;
                if (record->cancel) record->cancel->cancel();
                record->cancel.reset();
                record->model->suspendPreviewReads(true);
                record->lease.reset();
            }
            record->demanded = false;
            record->settled = false;
            continue;
        }
        if (!entry->directorySource.isValid() || record->source.sourceKey != entry->directorySource.sourceKey) { retire(id); continue; }
        const bool sourceChanged = !(record->source == entry->directorySource);
        if (sourceChanged || (record->demanded && !visible.contains(id))) {
            capture(record);
            ++record->serial;
            if (record->cancel) record->cancel->cancel();
            record->cancel.reset();
            record->model->suspendPreviewReads(true);
            record->settled = false;
            record->lease.reset();
            record->queuedNs = 0;
        }
        record->source = entry->directorySource;
        record->demanded = visible.contains(id);
    }
    for (const auto &id : ordered) {
        auto record = _records.value(id);
        if (!record) {
            record = QSharedPointer<Record>::create();
            record->source = _catalog->entryAt(_catalog->rowForEntryId(id))->directorySource;
            record->model = new ExternalCatalogModel(
                _catalog->_sessionId + QStringLiteral("-folder-%1").arg(++_clock),
                _catalog->_thumbnailProviderName, _catalog->_asyncProviderName,
                _catalog->_store, _catalog->_thumbnailCache, _catalog->_decodeManager, 0, 0, this);
            _records.insert(id, record);
            restore(id, record);
            connect(record->model, &QAbstractItemModel::dataChanged, this, [this, id, record] {
                if (_records.value(id) == record) capture(record);
            });
            publish(id);
        }
        record->demanded = true;
        record->touched = ++_clock;
        if (_cacheMode != 2 && !record->settled && !record->cancel) request(id, record);
    }
    QList<QString> inactive;
    for (auto it = _records.cbegin(); it != _records.cend(); ++it) {
        if (it.value()->demanded) continue;
        inactive.append(it.key());
    }
    const auto evict = [this](QList<QString> &ids, int limit) {
        std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) { return _records.value(a)->touched < _records.value(b)->touched; });
        while (ids.size() > limit) retire(ids.takeFirst());
    };
    evict(inactive, _cacheMode == 0 ? 0 : 32);
}

void ExternalDirectoryPreviews::publish(const QString &id) {
    if (_catalog->_shutdown) return;
    auto *entry = _catalog->entryAt(_catalog->rowForEntryId(id));
    if (!entry) return;
    const auto record = _records.value(id);
    if (record && record->source.sourceKey == entry->directorySource.sourceKey)
        entry->directoryPreviewState = record->state;
    ++entry->directoryPreviewRevision;
    if (entry->item) entry->item->setIsFolderView(entry->directoryPreviewState == DirectoryPreviewState::HasImages);
    if (_publications.isEmpty()) QTimer::singleShot(0, this, [this] { flushPublications(); });
    _publications.insert(id);
}

void ExternalDirectoryPreviews::flushPublications() {
    const auto ids = std::exchange(_publications, {});
    if (_catalog->_shutdown) return;
    QList<int> rows;
    for (const auto &id : ids) {
        const int row = _catalog->rowForEntryId(id);
        if (row < 0) continue;
        rows.append(row);
        if (MediaTimingTrace::enabled()) MediaTimingTrace::event(QStringLiteral("qt.directory.state.published"), {
            {QStringLiteral("benchmarkTraceId"), _catalog->property("directoryTraceId")},
            {QStringLiteral("catalogRevision"), _catalog->property("directoryTraceRevision")},
            {QStringLiteral("sessionId"), _catalog->_sessionId}, {QStringLiteral("entryId"), id},
            {QStringLiteral("state"), int(_catalog->entryAt(row)->directoryPreviewState)}});
    }
    std::sort(rows.begin(), rows.end());
    for (int i = 0; i < rows.size();) {
        const int first = rows[i];
        int last = first;
        while (++i < rows.size() && rows[i] == last + 1) last = rows[i];
        emit _catalog->dataChanged(_catalog->index(first), _catalog->index(last),
            {FileListModel::FolderViewRole, ExternalCatalogModel::VisualSnapshotRole});
    }
}

void ExternalDirectoryPreviews::request(const QString &id, const QSharedPointer<Record> &record) {
    if (!record->queuedNs) record->queuedNs = MediaTimingTrace::monotonicNanoseconds();
    if (!_scheduler->acquire()) return;
    record->cancel = QSharedPointer<ImageSourceCancellation>::create();
    const auto cancel = record->cancel;
    const auto source = record->source;
    const auto serial = ++record->serial;
    QVariantMap traceFields;
    if (MediaTimingTrace::enabled()) traceFields = {{QStringLiteral("sessionId"), _catalog->_sessionId},
        {QStringLiteral("benchmarkTraceId"), _catalog->property("directoryTraceId")},
        {QStringLiteral("catalogRevision"), _catalog->property("directoryTraceRevision")},
        {QStringLiteral("entryId"), id}, {QStringLiteral("sourceKey"), source.sourceKey},
        {QStringLiteral("resourceId"), source.resourceId}, {QStringLiteral("observation"), source.version},
        {QStringLiteral("serial"), serial}, {QStringLiteral("state"), int(record->state)},
        {QStringLiteral("queueNs"), MediaTimingTrace::monotonicNanoseconds() - record->queuedNs}};
    MediaTimingTrace::event(QStringLiteral("qt.directory.request"), traceFields);
    auto *watcher = new QFutureWatcher<DirectoryPreviewResult>(_scheduler.data());
    connect(watcher, &QFutureWatcher<DirectoryPreviewResult>::finished, this, [this, watcher, record, id, serial, source, traceFields] {
        auto result = watcher->result();
        if (_catalog->_shutdown || _records.value(id) != record || record->serial != serial || !record->demanded || !(record->source == source)) {
            MediaTimingTrace::event(QStringLiteral("qt.directory.stale_result"), traceFields);
            return;
        }
        record->cancel.reset();
        record->settled = true;
        MediaTimingTrace::event(QStringLiteral("qt.directory.completed"), MediaTimingTrace::mergedFields(traceFields,
            {{QStringLiteral("images"), result.entries.size()}, {QStringLiteral("error"), result.error}}));
        if (!result.error.isEmpty()) return;
        QVariantList entries;
        for (const auto &value : result.entries.mid(0, 16)) {
            auto entry = value.toMap();
            const auto name = entry.value(QStringLiteral("name")).toString();
            entry.insert(QStringLiteral("entryId"), id + QChar(0x1f) + name);
            entry.insert(QStringLiteral("index"), entries.size());
            entry.insert(QStringLiteral("isImage"), true);
            if (isVideoPreviewFormat(name)) {
                entry.insert(QStringLiteral("thumbnailKind"),
                             QStringLiteral("video"));
            }
            entry.insert(QStringLiteral("isDir"), false);
            entries.append(entry);
        }
        record->model->applyCatalog(entries, false, true);
        record->lease = result.lease;
        record->model->suspendPreviewReads(false);
        record->state = entries.isEmpty() ? DirectoryPreviewState::Empty : DirectoryPreviewState::HasImages;
        capture(record);
        publish(id);
    });
    // Completion also releases the slot if the consumer panel was destroyed.
    connect(watcher, &QFutureWatcher<DirectoryPreviewResult>::finished,
        _scheduler.data(), [scheduler = _scheduler.data(), watcher] {
            watcher->deleteLater();
            scheduler->release();
        });
    record->queuedNs = 0;
    watcher->setFuture(QtConcurrent::run(_scheduler->pool(), [provider = _provider, source, cancel, traceFields] {
        DirectoryPreviewResult result;
        if (cancel->isCanceled()) { result.error = QStringLiteral("cancelled"); return result; }
        MediaTimingTrace::Span enumerateSpan(QStringLiteral("qt.directory.enumerate"), traceFields);
        const auto listing = provider->enumerate(source, cancel);
        enumerateSpan.finish({{QStringLiteral("files"), listing.names.size()}, {QStringLiteral("error"), listing.error}});
        if (!listing.error.isEmpty()) { result.error = listing.error; return result; }
        if (cancel->isCanceled()) { result.error = QStringLiteral("cancelled"); return result; }
        const auto names = selectFolderPreviewNames(listing.names);
        if (names.isEmpty()) return result;
        MediaTimingTrace::Span resolveSpan(QStringLiteral("qt.directory.resolve"), traceFields);
        result = provider->resolve(source, listing, names, cancel);
        resolveSpan.finish({{QStringLiteral("selected"), names.size()}, {QStringLiteral("error"), result.error}});
        return result;
    }));
}
}
