#include <ZoinGallery/GalleryPanelController.h>

#include <ZoinGallery/GalleryCatalogModel.h>
#include <ZoinGallery/GalleryDragPreviewModel.h>
#include <ZoinGallery/GalleryPanelBackend.h>
#include <ZoinGallery/GallerySession.h>
#include <ZoinGallery/GallerySessionPanelBackend.h>
#include "GalleryQuickSearchIndex.h"

#include <QAbstractItemModel>
#include <QModelIndex>

#include <algorithm>
#include <utility>

namespace ZoinGallery {
namespace {

bool rangeContains(int first, int last, int value) {
    return first >= 0 && value >= first && value <= last;
}

} // namespace

GalleryPanelController::GalleryPanelController(QObject *parent)
    : QObject(parent),
      _quickSearch(std::make_unique<GalleryQuickSearchIndex>()),
      _dragPreviewModel(std::make_unique<GalleryDragPreviewModel>(this)) {}

GalleryPanelController::~GalleryPanelController() {
    disconnectBackend();
}

GallerySession *GalleryPanelController::session() const {
    return _session;
}

void GalleryPanelController::setSession(GallerySession *session) {
    if (_session == session && (_ownedBackend || !session)) {
        return;
    }

    _session = session;
    if (_ownedBackend) {
        disconnectBackend();
        delete _ownedBackend;
        _ownedBackend = nullptr;
        _backend = nullptr;
    }
    if (session) {
        _ownedBackend = new GallerySessionPanelBackend(session, this);
        _backend = _ownedBackend;
        connectBackend();
    } else {
        setVisualCursorIndex(-1);
    }
    emit sessionChanged();
    emit backendChanged();
    emit currentIndexChanged();
}

GalleryPanelBackend *GalleryPanelController::backend() const {
    return _backend;
}

void GalleryPanelController::setBackend(GalleryPanelBackend *backend) {
    if (_backend == backend && !_ownedBackend) {
        return;
    }

    disconnectBackend();
    if (_ownedBackend) {
        delete _ownedBackend;
        _ownedBackend = nullptr;
    }
    const bool hadSession = !_session.isNull();
    _session = nullptr;
    _backend = backend;
    connectBackend();
    if (hadSession) {
        emit sessionChanged();
    }
    emit backendChanged();
    emit currentIndexChanged();
}

GalleryCatalogModel *GalleryPanelController::catalogModel() const {
    return _backend ? _backend->catalogModel() : nullptr;
}

int GalleryPanelController::currentIndex() const {
    return _backend ? _backend->currentIndex() : -1;
}

int GalleryPanelController::visualCursorIndex() const {
    return _visualCursorIndex;
}

qulonglong GalleryPanelController::localRevision() const {
    return _localRevision;
}

bool GalleryPanelController::cursorIntentPending() const {
    return _pendingCursorIndex >= 0;
}

int GalleryPanelController::selectionVisualRevision() const {
    return _selectionVisualRevision;
}

QString GalleryPanelController::cursorEntryId() const {
    return _backend ? _backend->entryIdAt(currentIndex()) : QString();
}

QString GalleryPanelController::currentPath() const {
    return _backend ? _backend->currentPath() : QString();
}

bool GalleryPanelController::catalogReady() const {
    return !_backend || _backend->catalogReady();
}

qulonglong GalleryPanelController::catalogRevision() const {
    return _backend ? _backend->catalogRevision() : 0;
}

qulonglong GalleryPanelController::selectionRevision() const {
    return _backend ? _backend->selectionRevision() : 0;
}

qreal GalleryPanelController::panelScrollOffset() const {
    return _backend ? _backend->panelScrollOffset() : 0;
}

void GalleryPanelController::setPanelScrollOffset(qreal offset) {
    if (_backend) {
        _backend->setPanelScrollOffset(offset);
    }
}

QString GalleryPanelController::panelViewportCursorEntryId() const {
    return _backend
        ? _backend->panelViewportCursorEntryId() : QString();
}

void GalleryPanelController::setPanelViewportCursorEntryId(
    const QString &entryId) {
    if (_backend) {
        _backend->setPanelViewportCursorEntryId(entryId);
    }
}

bool GalleryPanelController::panelViewportStateAvailable() const {
    return _backend && _backend->panelViewportStateAvailable();
}

QString GalleryPanelController::quickSearchQuery() const {
    return _quickSearch ? _quickSearch->query() : QString();
}

bool GalleryPanelController::quickSearchActive() const {
    return _quickSearch && !_quickSearch->query().isEmpty();
}

int GalleryPanelController::quickSearchMatchCount() const {
    return _quickSearch ? _quickSearch->matchCount() : 0;
}

int GalleryPanelController::quickSearchRevision() const {
    return _quickSearchRevision;
}

bool GalleryPanelController::canRemoveEntries() const {
    return _backend && _backend->canRemoveEntries();
}

bool GalleryPanelController::dragEnabled() const {
    return _backend && _backend->canDragEntries();
}

bool GalleryPanelController::directoryDropEnabled() const {
    return _backend && _backend->canDropIntoDirectories();
}

bool GalleryPanelController::directoryPreviewEnabled() const {
    return _backend && _backend->canPreviewDirectories();
}

QVariantList GalleryPanelController::dragUrls() const {
    return _dragUrls;
}

QAbstractItemModel *GalleryPanelController::dragPreviewModel() const {
    return _dragPreviewModel.get();
}

int GalleryPanelController::dragPreviewRemainingCount() const {
    return std::max(0, _dragPreviewTotalCount
                           - _dragPreviewModel->rowCount());
}

QString GalleryPanelController::entryIdAt(int index) const {
    return _backend ? _backend->entryIdAt(index) : QString();
}

int GalleryPanelController::indexForEntryId(const QString &entryId) const {
    return _backend ? _backend->indexForEntryId(entryId) : -1;
}

int GalleryPanelController::sourceIndexAt(int index) const {
    return _backend ? _backend->sourceIndexAt(index) : -1;
}

QString GalleryPanelController::entryNameAt(int index) const {
    return _backend ? _backend->entryNameAt(index) : QString();
}

bool GalleryPanelController::isImageAt(int index) const {
    return _backend && _backend->isImageAt(index);
}

bool GalleryPanelController::isSelectedAt(int index) const {
    return _backend && _backend->isSelectedAt(index);
}

QVariantMap GalleryPanelController::highlightStyleAt(int index) const {
    return _backend ? _backend->highlightStyleAt(index) : QVariantMap{};
}

void GalleryPanelController::activateIndex(int index) {
    if (_backend) {
        _backend->activateIndex(index);
    }
}

bool GalleryPanelController::prepareDrag(
    int index, bool singleItemOnly, int previewLimit) {
    if (!_backend || !_backend->canDragEntries()) {
        clearDragPayload();
        return false;
    }
    GalleryDragDescriptor descriptor = _backend->prepareDrag(
        index, singleItemOnly, std::max(0, previewLimit));
    if (descriptor.previewItems.size() > previewLimit) {
        descriptor.previewItems.resize(previewLimit);
    }
    _dragUrls = descriptor.urls;
    _dragPreviewTotalCount = std::max(
        {descriptor.totalCount,
         static_cast<int>(descriptor.previewItems.size()),
         static_cast<int>(descriptor.urls.size())});
    _dragPreviewModel->setItems(descriptor.previewItems);
    emit dragPayloadChanged();
    return !_dragUrls.isEmpty();
}

void GalleryPanelController::configureNativeDragCursors(
    QObject *dragSource) {
    if (_backend && _backend->canDragEntries()) {
        _backend->configureNativeDragCursors(dragSource);
    }
}

void GalleryPanelController::finishExternalDrag(int dropAction) {
    if (_backend && _backend->canDragEntries()) {
        const GalleryFileOperationResult result =
            _backend->finalizeExternalDrag(
                _dragUrls, static_cast<Qt::DropAction>(dropAction));
        reportFileOperationFailure(result);
    }
    clearDragPayload();
}

int GalleryPanelController::dropUrlsIntoDirectory(
    const QVariantList &urls, int directoryIndex, int dropAction) {
    if (!_backend || !_backend->canDropIntoDirectories()) {
        return Qt::IgnoreAction;
    }
    const GalleryFileOperationResult result =
        _backend->dropUrlsIntoDirectory(
            urls, directoryIndex,
            static_cast<Qt::DropAction>(dropAction));
    if (!result.success) {
        reportFileOperationFailure(result);
        return Qt::IgnoreAction;
    }
    return result.action;
}

void GalleryPanelController::removeEntry(int index) {
    if (_backend && _backend->canRemoveEntries()) {
        _backend->removeEntry(index);
    }
}

QAbstractItemModel *GalleryPanelController::directoryPreviewModelAt(
    int index) {
    return _backend && _backend->canPreviewDirectories()
        ? _backend->directoryPreviewModel(index) : nullptr;
}

void GalleryPanelController::ensurePreviews() {
    if (_backend) {
        _backend->ensurePreviews();
    }
}

bool GalleryPanelController::setQuickSearchQuery(const QString &query) {
    if (!_quickSearch) {
        return query.isEmpty();
    }
    _quickSearch->setModel(catalogModel());
    const QString previous = _quickSearch->query();
    const bool accepted = _quickSearch->setQuery(query);
    if (previous != _quickSearch->query()) {
        ++_quickSearchRevision;
        emit quickSearchChanged();
    }
    return accepted;
}

void GalleryPanelController::clearQuickSearch() {
    setQuickSearchQuery(QString());
}

int GalleryPanelController::quickSearchNext(
    bool forward, bool forceMove, bool wrap) {
    if (!_quickSearch || !_backend) {
        return currentIndex();
    }
    const int target = _quickSearch->nextMatch(
        visualCursorIndex(), forward, forceMove, wrap);
    if (target >= 0 && target != visualCursorIndex()) {
        requestCursor(target);
    }
    return target;
}

QVariantMap GalleryPanelController::quickSearchMatchAt(int index) const {
    if (!_quickSearch) {
        return {};
    }
    const GalleryQuickSearchMatch match = _quickSearch->matchAt(index);
    if (match.index < 0) {
        return {};
    }
    return {{QStringLiteral("start"), match.utf16Start},
            {QStringLiteral("length"), match.utf16Length}};
}

QVariantMap GalleryPanelController::quickSearchMatchForEntry(
    const QString &entryId) const {
    if (!_quickSearch || !_backend || entryId.isEmpty()) {
        return {};
    }
    return quickSearchMatchAt(_backend->indexForEntryId(entryId));
}

int GalleryPanelController::quickSearchIndexedRowCount() const {
    return _quickSearch ? _quickSearch->indexedRowCount() : 0;
}

int GalleryPanelController::quickSearchLastVisitedRowCount() const {
    return _quickSearch ? _quickSearch->lastVisitedRowCount() : 0;
}

bool GalleryPanelController::requestCursor(int index, bool deferCommit) {
    GalleryCatalogModel *catalog = catalogModel();
    const int rowCount = catalog ? catalog->rowCount() : 0;
    if (!_backend || index < 0 || index >= rowCount) {
        return false;
    }

    const bool wasPending = cursorIntentPending();
    bumpLocalRevision();
    _pendingCursorIndex = index;
    _pendingCursorEntryId = _backend->entryIdAt(index);
    _pendingCursorRevision = _localRevision;
    _cursorIntentDeferred = deferCommit;
    setVisualCursorIndex(index);
    _backend->activateIndex(index);
    if (!wasPending) {
        emit cursorIntentPendingChanged();
    }

    emit cursorIntentRequested(_pendingCursorEntryId, index,
                               _backend->sourceIndexAt(index),
                               _backend->catalogRevision(), _localRevision,
                               deferCommit);

    if (!_backend->remoteAuthoritative() && !deferCommit) {
        acknowledgeCursor(index, _localRevision);
    }
    return true;
}

bool GalleryPanelController::commitPendingCursor() {
    if (!_backend || _pendingCursorIndex < 0) {
        return false;
    }
    _cursorIntentDeferred = false;
    emit cursorIntentRequested(_pendingCursorEntryId, _pendingCursorIndex,
                               _backend->sourceIndexAt(_pendingCursorIndex),
                               _backend->catalogRevision(),
                               _pendingCursorRevision, false);
    if (!_backend->remoteAuthoritative()) {
        acknowledgeCursor(_pendingCursorIndex, _pendingCursorRevision);
    }
    return true;
}

void GalleryPanelController::cancelPendingCursor() {
    const bool wasPending = cursorIntentPending();
    _pendingCursorIndex = -1;
    _pendingCursorEntryId.clear();
    _pendingCursorRevision = 0;
    _cursorIntentDeferred = false;
    if (wasPending) {
        emit cursorIntentPendingChanged();
    }
    setVisualCursorIndex(currentIndex());
}

void GalleryPanelController::acknowledgeCursor(
    int index, qulonglong localRevision) {
    if (_pendingCursorIndex < 0 || localRevision < _pendingCursorRevision) {
        return;
    }
    if (index != _pendingCursorIndex) {
        return;
    }
    _pendingCursorIndex = -1;
    _pendingCursorEntryId.clear();
    _pendingCursorRevision = 0;
    _cursorIntentDeferred = false;
    emit cursorIntentPendingChanged();
    setVisualCursorIndex(index);
}

bool GalleryPanelController::effectiveSelected(
    const QString &entryId, bool authoritative) const {
    const auto pending = _selectionPreview.constFind(entryId);
    if (pending != _selectionPreview.cend()) {
        return pending.value();
    }
    const auto awaiting = _selectionAwaiting.constFind(entryId);
    return awaiting == _selectionAwaiting.cend()
        ? authoritative : awaiting.value();
}

void GalleryPanelController::beginSelectionGesture(bool add) {
    _selectionGestureActive = true;
    _selectionAdds = add;
    _selectionPreview.clear();
    _selectionRangeFirst = -1;
    _selectionRangeLast = -1;
    ++_selectionVisualRevision;
    emit selectionVisualRevisionChanged();
}

void GalleryPanelController::previewSelectionRange(int first, int last) {
    GalleryCatalogModel *catalog = catalogModel();
    if (!_selectionGestureActive || !catalog || catalog->rowCount() <= 0) {
        return;
    }
    if (first < 0 || last < 0) {
        if (_selectionRangeFirst < 0 && _selectionPreview.isEmpty()) {
            return;
        }
        _selectionPreview.clear();
        _selectionRangeFirst = -1;
        _selectionRangeLast = -1;
        ++_selectionVisualRevision;
        emit selectionVisualRevisionChanged();
        return;
    } else {
        first = std::clamp(first, 0, catalog->rowCount() - 1);
        last = std::clamp(last, 0, catalog->rowCount() - 1);
        if (first > last) {
            std::swap(first, last);
        }
    }
    if (first == _selectionRangeFirst && last == _selectionRangeLast) {
        return;
    }

    const int oldFirst = _selectionRangeFirst;
    const int oldLast = _selectionRangeLast;
    const auto entryIdAt = [this, catalog](int row) {
        const QString entryId = _backend->entryIdAt(row);
        const QString name = catalog->data(
            catalog->index(row, 0), GalleryCatalogModel::NameRole).toString();
        return name == QStringLiteral("..") ? QString() : entryId;
    };
    if (oldFirst >= 0) {
        for (int row = oldFirst; row <= oldLast; ++row) {
            if (!rangeContains(first, last, row)) {
                _selectionPreview.remove(entryIdAt(row));
            }
        }
    }
    for (int row = first; row <= last; ++row) {
        if (rangeContains(oldFirst, oldLast, row)) {
            continue;
        }
        const QString entryId = entryIdAt(row);
        if (!entryId.isEmpty()) {
            const bool base = effectiveSelected(
                entryId, _backend->isSelectedAt(row));
            if (base == _selectionAdds) {
                _selectionPreview.remove(entryId);
            } else {
                _selectionPreview.insert(entryId, _selectionAdds);
            }
        }
    }
    _selectionRangeFirst = first;
    _selectionRangeLast = last;
    ++_selectionVisualRevision;
    emit selectionVisualRevisionChanged();
}

void GalleryPanelController::toggleSelectionAt(int index) {
    GalleryCatalogModel *catalog = catalogModel();
    if (!_selectionGestureActive || !_backend || !catalog
        || index < 0 || index >= catalog->rowCount()) {
        return;
    }
    const QString entryId = _backend->entryIdAt(index);
    const QString name = catalog->data(
        catalog->index(index, 0), GalleryCatalogModel::NameRole).toString();
    if (entryId.isEmpty() || name == QStringLiteral("..")) {
        return;
    }
    const bool authoritative = _backend->isSelectedAt(index);
    _selectionPreview.insert(
        entryId, !effectiveSelected(entryId, authoritative));
    ++_selectionVisualRevision;
    emit selectionVisualRevisionChanged();
}

bool GalleryPanelController::commitSelectionGesture() {
    if (!_selectionGestureActive || _selectionPreview.isEmpty() || !_backend) {
        cancelSelectionGesture();
        return false;
    }

    QStringList selectedEntryIds;
    QStringList deselectedEntryIds;
    selectedEntryIds.reserve(_selectionPreview.size());
    deselectedEntryIds.reserve(_selectionPreview.size());
    for (auto change = _selectionPreview.cbegin();
         change != _selectionPreview.cend(); ++change) {
        (change.value() ? selectedEntryIds : deselectedEntryIds)
            .append(change.key());
        _selectionAwaiting.insert(change.key(), change.value());
    }
    _selectionPreview.clear();
    _selectionGestureActive = false;
    _selectionRangeFirst = -1;
    _selectionRangeLast = -1;
    bumpLocalRevision();
    ++_selectionVisualRevision;
    emit selectionVisualRevisionChanged();

    _backend->applySelectionIntent(selectedEntryIds, deselectedEntryIds);
    emit selectionIntentRequested(selectedEntryIds, deselectedEntryIds,
                                  _backend->catalogRevision(),
                                  _localRevision);
    return true;
}

void GalleryPanelController::cancelSelectionGesture() {
    const bool changed = _selectionGestureActive
        || !_selectionPreview.isEmpty();
    _selectionGestureActive = false;
    _selectionPreview.clear();
    _selectionRangeFirst = -1;
    _selectionRangeLast = -1;
    if (changed) {
        ++_selectionVisualRevision;
        emit selectionVisualRevisionChanged();
    }
}

void GalleryPanelController::connectBackendCursorSignals() {
    _backendConnections.append(connect(
        _backend, &GalleryPanelBackend::currentIndexChanged,
        this, [this] {
            emit currentIndexChanged();
            reconcileAuthoritativeCursor();
        }));
    _backendConnections.append(connect(
        _backend, &GalleryPanelBackend::catalogRevisionChanged,
        this, [this] {
            emit catalogRevisionChanged();
            invalidateQuickSearch();
            if (_pendingCursorIndex >= 0) {
                const int remapped =
                    _backend->indexForEntryId(_pendingCursorEntryId);
                if (remapped >= 0) {
                    _pendingCursorIndex = remapped;
                    setVisualCursorIndex(remapped);
                } else {
                    cancelPendingCursor();
                }
            } else {
                setVisualCursorIndex(currentIndex());
            }
            _selectionPreview.clear();
            _selectionAwaiting.clear();
            ++_selectionVisualRevision;
            emit selectionVisualRevisionChanged();
        }));
}

void GalleryPanelController::connectCatalogInvalidationSignals() {
    GalleryCatalogModel *catalog = _backend->catalogModel();
    if (!catalog) {
        return;
    }
    const auto invalidate = [this] { invalidateQuickSearch(); };
    _backendConnections.append(connect(
        catalog, &QAbstractItemModel::modelReset, this, invalidate));
    _backendConnections.append(connect(
        catalog, &QAbstractItemModel::rowsInserted, this,
        [invalidate](const QModelIndex &, int, int) { invalidate(); }));
    _backendConnections.append(connect(
        catalog, &QAbstractItemModel::rowsRemoved, this,
        [invalidate](const QModelIndex &, int, int) { invalidate(); }));
    _backendConnections.append(connect(
        catalog, &QAbstractItemModel::layoutChanged, this,
        [invalidate](const QList<QPersistentModelIndex> &,
                     QAbstractItemModel::LayoutChangeHint) {
            invalidate();
        }));
    _backendConnections.append(connect(
        catalog, &QAbstractItemModel::dataChanged, this,
        [invalidate](const QModelIndex &, const QModelIndex &,
                     const QList<int> &roles) {
            if (roles.isEmpty()
                || roles.contains(GalleryCatalogModel::NameRole)) {
                invalidate();
            }
        }));
}

void GalleryPanelController::connectBackendSelectionSignal() {
    _backendConnections.append(connect(
        _backend, &GalleryPanelBackend::selectionRevisionChanged,
        this, [this] {
            emit selectionRevisionChanged();
            bool changed = false;
            for (auto intent = _selectionAwaiting.begin();
                 intent != _selectionAwaiting.end();) {
                const int index = _backend->indexForEntryId(intent.key());
                if (index < 0
                    || _backend->isSelectedAt(index) == intent.value()) {
                    intent = _selectionAwaiting.erase(intent);
                    changed = true;
                } else {
                    ++intent;
                }
            }
            if (changed) {
                ++_selectionVisualRevision;
                emit selectionVisualRevisionChanged();
            }
        }));
}

void GalleryPanelController::connectBackendLifecycleSignals() {
    _backendConnections.append(connect(
        _backend, &GalleryPanelBackend::currentPathChanged,
        this, &GalleryPanelController::currentPathChanged));
    _backendConnections.append(connect(
        _backend, &GalleryPanelBackend::catalogReadyChanged,
        this, &GalleryPanelController::catalogReadyChanged));
    _backendConnections.append(connect(
        _backend, &GalleryPanelBackend::panelViewportChanged,
        this, &GalleryPanelController::panelViewportChanged));
    _backendConnections.append(connect(
        _backend, &QObject::destroyed, this, [this] {
            _backend = nullptr;
            _ownedBackend = nullptr;
            _backendConnections.clear();
            cancelPendingCursor();
            clearDragPayload();
            emit backendChanged();
            emit currentIndexChanged();
        }));
}

void GalleryPanelController::connectBackend() {
    if (!_backend) {
        setVisualCursorIndex(-1);
        invalidateQuickSearch();
        return;
    }
    _quickSearch->setModel(_backend->catalogModel());
    connectBackendCursorSignals();
    connectCatalogInvalidationSignals();
    connectBackendSelectionSignal();
    connectBackendLifecycleSignals();
    setVisualCursorIndex(_backend->currentIndex());
}


void GalleryPanelController::disconnectBackend() {
    for (const QMetaObject::Connection &connection :
         std::as_const(_backendConnections)) {
        disconnect(connection);
    }
    _backendConnections.clear();
    cancelPendingCursor();
    _selectionPreview.clear();
    _selectionAwaiting.clear();
    invalidateQuickSearch();
    clearDragPayload();
}

void GalleryPanelController::reconcileAuthoritativeCursor() {
    if (!_backend) {
        setVisualCursorIndex(-1);
        return;
    }
    if (_pendingCursorIndex < 0) {
        setVisualCursorIndex(_backend->currentIndex());
        return;
    }
    // The session applies local cursor movement synchronously. A later remote
    // scene may still carry the previous cursor; keep painting the pending
    // stable entry until protocol acknowledgment explicitly resolves it.
    if (!_backend->remoteAuthoritative()
        && _backend->currentIndex() == _pendingCursorIndex
        && !_cursorIntentDeferred) {
        acknowledgeCursor(_pendingCursorIndex, _pendingCursorRevision);
    }
}

void GalleryPanelController::setVisualCursorIndex(int index) {
    if (_visualCursorIndex == index) {
        return;
    }
    _visualCursorIndex = index;
    emit visualCursorIndexChanged();
}

void GalleryPanelController::bumpLocalRevision() {
    ++_localRevision;
    emit localRevisionChanged();
}

void GalleryPanelController::invalidateQuickSearch() {
    if (!_quickSearch) {
        return;
    }
    const bool hadState = !_quickSearch->query().isEmpty()
        || _quickSearch->indexedRowCount() > 0;
    _quickSearch->invalidate();
    _quickSearch->setModel(catalogModel());
    if (hadState) {
        ++_quickSearchRevision;
        emit quickSearchChanged();
    }
}

void GalleryPanelController::clearDragPayload() {
    const bool changed = !_dragUrls.isEmpty()
        || _dragPreviewTotalCount != 0
        || _dragPreviewModel->rowCount() != 0;
    _dragUrls.clear();
    _dragPreviewTotalCount = 0;
    _dragPreviewModel->clear();
    if (changed) {
        emit dragPayloadChanged();
    }
}

void GalleryPanelController::reportFileOperationFailure(
    const GalleryFileOperationResult &result) {
    if (result.success) {
        return;
    }
    emit fileOperationFailed(
        result.title.isEmpty()
            ? QStringLiteral("File operation failed") : result.title,
        result.message.isEmpty()
            ? QStringLiteral("The requested operation could not be completed.")
            : result.message);
}

} // namespace ZoinGallery
