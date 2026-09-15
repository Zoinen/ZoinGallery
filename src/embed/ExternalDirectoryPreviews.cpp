#include "ExternalDirectoryPreviews.h"
#include "ExternalCatalogModel.h"
#include "FolderPreviewSelection.h"
#include <ZoinGallery/MediaTimingTrace.h>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>

namespace ZoinGallery {

void ExternalCatalogModel::configureDirectoryPreviews(QSharedPointer<DirectoryPreviewProvider> provider, QSharedPointer<QThreadPool> pool) {
    if (!_directoryPreviews && provider && pool) _directoryPreviews = new ExternalDirectoryPreviews(this, std::move(provider), std::move(pool));
}
void ExternalCatalogModel::requestDirectoryPreviews(const QList<int> &rows) { if (_directoryPreviews) _directoryPreviews->demand(rows); }
void ExternalCatalogModel::clearDirectoryPreviews() { if (_directoryPreviews) _directoryPreviews->clear(); }
void ExternalCatalogModel::setDirectoryCacheMode(int mode) { if (_directoryPreviews) _directoryPreviews->setCacheMode(mode); }
void ExternalCatalogModel::invalidatePreviewPixels() {
    cancelAllRunners();
    clearDirectoryPreviews();
    _viewerImageCache.clear();
    for (auto &entry : _entries) clearPublishedImage(entry);
    if (rowCount()) emit dataChanged(index(0), index(rowCount() - 1), {FileListModel::ImageFullSizeRole});
}
QAbstractItemModel *ExternalCatalogModel::directoryPreviewModel(int row) const { return _directoryPreviews ? _directoryPreviews->model(entryIdAt(row)) : nullptr; }
bool ExternalCatalogModel::directoryPreviewAvailable(int row) const { return _directoryPreviews && _directoryPreviews->available(entryIdAt(row)); }

ExternalDirectoryPreviews::ExternalDirectoryPreviews(ExternalCatalogModel *catalog,
    QSharedPointer<DirectoryPreviewProvider> provider, QSharedPointer<QThreadPool> pool)
    : QObject(catalog), _catalog(catalog), _provider(std::move(provider)), _pool(std::move(pool))
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
}
ExternalDirectoryPreviews::~ExternalDirectoryPreviews() { clear(); }
void ExternalDirectoryPreviews::clear() {
    _clearing = true;
    _demand.clear();
    for (const auto &id : _records.keys()) retire(id);
    _clearing = false;
}
void ExternalDirectoryPreviews::retire(const QString &id) {
    const auto record = _records.take(id);
    if (!record) return;
    ++record->serial;
    if (record->cancel) record->cancel->cancel();
    if (record->model) { record->model->shutdown(); record->model->deleteLater(); }
    publish(id);
}
bool ExternalDirectoryPreviews::available(const QString &id) const { const auto r = _records.value(id); return r && r->available; }
ExternalCatalogModel *ExternalDirectoryPreviews::model(const QString &id) const { const auto r = _records.value(id); return r ? r->model : nullptr; }
void ExternalDirectoryPreviews::demand(const QList<int> &rows) { _demand = rows; synchronize(); }
void ExternalDirectoryPreviews::setCacheMode(int mode) {
    if (_cacheMode == mode) return;
    _cacheMode = mode;
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

void ExternalDirectoryPreviews::synchronize() {
    if (_catalog->_shutdown || _clearing) return;
    QSet<QString> visible;
    for (int row : _demand) {
        const auto *entry = _catalog->entryAt(row);
        if (entry && entry->directory && entry->directorySource.isValid()) visible.insert(entry->id);
    }
    for (const auto &id : _records.keys()) {
        const auto record = _records.value(id);
        const auto *entry = _catalog->entryAt(_catalog->rowForEntryId(id));
        if (!entry) {
            if (_cacheMode == 0) { retire(id); continue; }
            // Keep render state across navigation, but never retain authority
            // to start reads from a catalog that is no longer displayed.
            if (record->demanded || record->cancel || record->lease) {
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
        if (!entry->directorySource.isValid()) { retire(id); continue; }
        const bool sourceChanged = !(record->source == entry->directorySource);
        if (sourceChanged || (record->demanded && !visible.contains(id))) {
            ++record->serial;
            if (record->cancel) record->cancel->cancel();
            record->cancel.reset();
            record->model->suspendPreviewReads(true);
            record->settled = false;
            if (sourceChanged) record->lease.reset();
        }
        record->source = entry->directorySource;
        record->demanded = visible.contains(id);
    }
    for (const auto &id : visible) {
        auto record = _records.value(id);
        if (!record) {
            record = QSharedPointer<Record>::create();
            record->source = _catalog->entryAt(_catalog->rowForEntryId(id))->directorySource;
            record->model = new ExternalCatalogModel(
                _catalog->_sessionId + QStringLiteral("-folder-%1").arg(++_clock),
                _catalog->_thumbnailProviderName, _catalog->_asyncProviderName,
                _catalog->_store, _catalog->_thumbnailCache, _catalog->_decodeManager, 0, 0, this);
            _records.insert(id, record);
        }
        record->demanded = true;
        record->touched = ++_clock;
        if (_cacheMode != 2 && !record->settled && !record->cancel) request(id, record);
    }
    QList<QString> inactive, history;
    for (auto it = _records.cbegin(); it != _records.cend(); ++it) {
        if (it.value()->demanded) continue;
        (_catalog->rowForEntryId(it.key()) < 0 ? history : inactive).append(it.key());
    }
    const auto evict = [this](QList<QString> &ids, int limit) {
        std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) { return _records.value(a)->touched < _records.value(b)->touched; });
        while (ids.size() > limit) retire(ids.takeFirst());
    };
    evict(inactive, _cacheMode == 0 ? 0 : 32);
    evict(history, 128);
}

void ExternalDirectoryPreviews::publish(const QString &id) {
    if (_catalog->_shutdown) return;
    const int row = _catalog->rowForEntryId(id);
    auto *entry = _catalog->entryAt(row);
    if (!entry) return;
    if (entry->item) entry->item->setIsFolderView(available(id));
    emit _catalog->dataChanged(_catalog->index(row), _catalog->index(row), {FileListModel::FolderViewRole});
}

void ExternalDirectoryPreviews::request(const QString &id, const QSharedPointer<Record> &record) {
    // Admission happens on the GUI thread shared by both panels. Never put a
    // scroll history into QThreadPool's unbounded queue: retain only current
    // demand and retry once a shared directory worker is free.
    if (_pool->activeThreadCount() >= _pool->maxThreadCount()) {
        if (!_retryScheduled) {
            _retryScheduled = true;
            QTimer::singleShot(50, this, [this] { _retryScheduled = false; synchronize(); });
        }
        return;
    }
    record->cancel = QSharedPointer<ImageSourceCancellation>::create();
    const auto cancel = record->cancel;
    const auto source = record->source;
    const auto serial = ++record->serial;
    auto *watcher = new QFutureWatcher<DirectoryPreviewResult>(this);
    connect(watcher, &QFutureWatcher<DirectoryPreviewResult>::finished, this, [this, watcher, record, id, serial, source] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (_catalog->_shutdown || _records.value(id) != record || record->serial != serial || !record->demanded || !(record->source == source)) {
            MediaTimingTrace::event(QStringLiteral("qt.directory.stale_result"), {{QStringLiteral("entryId"), id}});
            return;
        }
        record->cancel.reset();
        record->settled = true;
        MediaTimingTrace::event(QStringLiteral("qt.directory.completed"), {{QStringLiteral("entryId"), id}, {QStringLiteral("images"), result.entries.size()}, {QStringLiteral("error"), result.error}});
        if (!result.error.isEmpty()) return;
        QVariantList entries;
        for (const auto &value : result.entries) {
            auto entry = value.toMap();
            const auto name = entry.value(QStringLiteral("name")).toString();
            entry.insert(QStringLiteral("entryId"), id + QChar(0x1f) + name);
            entry.insert(QStringLiteral("index"), entries.size());
            entry.insert(QStringLiteral("isImage"), true);
            entry.insert(QStringLiteral("isDir"), false);
            entries.append(entry);
        }
        record->model->applyCatalog(entries, false, true);
        record->lease = result.lease;
        record->model->suspendPreviewReads(false);
        record->available = !entries.isEmpty();
        publish(id);
    });
    watcher->setFuture(QtConcurrent::run(_pool.data(), [provider = _provider, source, cancel] {
        DirectoryPreviewResult result;
        if (cancel->isCanceled()) { result.error = QStringLiteral("cancelled"); return result; }
        const auto listing = provider->enumerate(source, cancel);
        if (!listing.error.isEmpty()) { result.error = listing.error; return result; }
        if (cancel->isCanceled()) { result.error = QStringLiteral("cancelled"); return result; }
        const auto names = selectFolderPreviewNames(listing.names);
        if (names.isEmpty()) return result;
        return provider->resolve(source, listing, names, cancel);
    }));
}
}
