#include <ZoinGallery/GallerySession.h>

#include "ExternalCatalogModel.h"
#include "GalleryViewModel.h"
#include "ImageFile.h"
#include "ImageModel.h"
#include "LocalFilesystemSource.h"
#include "SelectedImagesModel.h"
#include "ThumbnailMemoryCache.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QSet>

namespace ZoinGallery {

class GallerySession::Private {
public:
    struct PanelViewportState {
        qreal scrollOffset = 0;
        QString cursorEntryId;
    };

    QString sessionId;
    SourceKind sourceKind = ExternalCatalogSource;
    QString thumbnailProviderName;
    ExternalCatalogModel *external = nullptr;
    ::ZoinGallery::LocalFilesystemSource *local = nullptr;
    QString currentPath;
    QString stableCursorEntryId;
    qulonglong catalogRevision = 0;
    qulonglong metadataRevision = 0;
    bool metadataDeferred = false;
    bool metadataCompleted = false;
    bool catalogStreaming = false;
    bool catalogReady = true;
    qulonglong highlightRevision = 0;
    qulonglong selectionRevision = 0;
    qreal panelScrollOffset = 0;
    QString panelViewportCursorEntryId;
    QHash<QString, PanelViewportState> panelViewportStates;
    bool panelViewportStateAvailable = false;
    bool applyingPanelViewportState = false;
    int currentIndex = -1;
    bool viewerOpen = false;
    QString viewerPreviousEntryId;
    QString viewerPreviousReturnEntryId;
    bool viewerPreviousLocked = false;
    QVariantMap viewerPreviousViewport;
    bool shutdown = false;

    QAbstractItemModel *model() const {
        return external ? static_cast<QAbstractItemModel *>(external)
                        : local ? local->model() : nullptr;
    }

    void clearViewer() {
        if (external) {
            external->clearViewer();
        }
        else if (local) {
            local->clearViewer();
        }
    }
};

GallerySession::GallerySession(
    const QString &sessionId, SourceKind sourceKind,
    const QString &thumbnailProviderName,
    const QString &asyncProviderName,
    const QSharedPointer<::ProviderImageStore> &store,
    const QSharedPointer<ThumbnailMemoryCache> &thumbnailCache,
    ::DecodeManager *decodeManager,
    qint64 viewerFitCacheByteBudget,
    qint64 viewerNativeCacheByteBudget,
    QObject *parent)
    : QObject(parent), d(new Private) {
    d->sessionId = sessionId;
    d->sourceKind = sourceKind;
    d->thumbnailProviderName = thumbnailProviderName;

    if (sourceKind == ExternalCatalogSource) {
        d->external = new ExternalCatalogModel(
            sessionId, thumbnailProviderName, asyncProviderName,
            store, thumbnailCache, decodeManager,
            viewerFitCacheByteBudget, viewerNativeCacheByteBudget,
            this);
        connect(d->external,
                &ExternalCatalogModel::viewerImageUrlChanged,
                this, &GallerySession::viewerSourceChanged);
        connect(d->external,
                &ExternalCatalogModel::viewerSourceAtChanged,
                this, &GallerySession::viewerSourceAtChanged);
        return;
    }

    d->local = new ::ZoinGallery::LocalFilesystemSource(
        sessionId, thumbnailProviderName, asyncProviderName,
        store, decodeManager,
        viewerFitCacheByteBudget, viewerNativeCacheByteBudget,
        this);
    connect(d->local,
            &::ZoinGallery::LocalFilesystemSource::currentPathChanged,
            this, [this] {
                const QString path = d->local->currentPath();
                if (d->currentPath != path) {
                    d->currentPath = path;
                    clearViewerPreviousState(true);
                    restorePanelViewportStateForPath(path);
                    emit currentPathChanged();
                }
            });
    connect(d->local,
            &::ZoinGallery::LocalFilesystemSource::catalogChanged,
            this, [this] {
                if (d->shutdown) {
                    return;
                }
                ++d->catalogRevision;
                const int remapped =
                    d->local->rowForEntryId(d->stableCursorEntryId);
                setCurrentIndex(remapped >= 0 ? remapped : d->currentIndex);
                sanitizeViewerPreviousStateForCatalog();
                emit catalogRevisionChanged();
            });
    connect(d->local,
            &::ZoinGallery::LocalFilesystemSource::selectionChanged,
            this, [this] {
                if (!d->shutdown) {
                    ++d->selectionRevision;
                    emit selectionRevisionChanged();
                }
            });
    connect(d->local,
            &::ZoinGallery::LocalFilesystemSource::viewerSourceChanged,
            this, &GallerySession::viewerSourceChanged);
    connect(d->local,
            &::ZoinGallery::LocalFilesystemSource::viewerSourceAtChanged,
            this, &GallerySession::viewerSourceAtChanged);
}

GallerySession::~GallerySession() {
    shutdown();
    delete d;
}

QString GallerySession::sessionId() const {
    return d->sessionId;
}

GallerySession::SourceKind GallerySession::sourceKind() const {
    return d->sourceKind;
}

bool GallerySession::localSource() const {
    return d->sourceKind == LocalFilesystemSource;
}

QAbstractItemModel *GallerySession::model() const {
    return d->model();
}

QObject *GallerySession::fileListModel() const {
    return d->local ? d->local->fileListModel() : nullptr;
}

QObject *GallerySession::galleryViewModel() const {
    return d->local ? d->local->galleryViewModel() : nullptr;
}

QObject *GallerySession::selectedImagesModel() const {
    return d->local ? d->local->selectedImagesModel() : nullptr;
}

QObject *GallerySession::imageModel() const {
    return d->local ? d->local->imageModel() : nullptr;
}

QString GallerySession::currentPath() const {
    return d->currentPath;
}

int GallerySession::currentIndex() const {
    return d->currentIndex;
}

void GallerySession::setCurrentIndex(int index) {
    QAbstractItemModel *catalog = d->model();
    const int count = catalog ? catalog->rowCount() : 0;
    const int bounded = count == 0 ? -1 : qBound(0, index, count - 1);
    const QString nextEntryId = entryIdAt(bounded);
    if (d->currentIndex == bounded) {
        d->stableCursorEntryId = nextEntryId;
        return;
    }
    const bool entryChanged = d->stableCursorEntryId != nextEntryId;
    d->currentIndex = bounded;
    d->stableCursorEntryId = nextEntryId;
    if (d->viewerOpen && entryChanged) {
        if (d->external) {
            d->external->setViewerIndex(bounded);
        }
        else {
            d->clearViewer();
        }
    }
    emit currentIndexChanged();
    emit viewerSourceChanged();
}

QString GallerySession::cursorEntryId() const {
    return entryIdAt(d->currentIndex);
}

qulonglong GallerySession::catalogRevision() const {
    return d->catalogRevision;
}

bool GallerySession::catalogReady() const {
    return d->catalogReady;
}

qulonglong GallerySession::selectionRevision() const {
    return d->selectionRevision;
}

qreal GallerySession::panelScrollOffset() const {
    return d->panelScrollOffset;
}

void GallerySession::setPanelScrollOffset(qreal offset) {
    const qreal bounded = qMax<qreal>(0, offset);
    if (qFuzzyCompare(d->panelScrollOffset, bounded)) {
        return;
    }
    d->panelScrollOffset = bounded;
    emit panelScrollOffsetChanged();
    rememberPanelViewportStateForCurrentPath();
}

QString GallerySession::panelViewportCursorEntryId() const {
    return d->panelViewportCursorEntryId;
}

void GallerySession::setPanelViewportCursorEntryId(const QString &entryId) {
    if (d->panelViewportCursorEntryId == entryId) {
        return;
    }
    d->panelViewportCursorEntryId = entryId;
    emit panelViewportCursorEntryIdChanged();
    rememberPanelViewportStateForCurrentPath();
}

bool GallerySession::panelViewportStateAvailable() const {
    return d->panelViewportStateAvailable;
}

void GallerySession::rememberPanelViewportStateForCurrentPath() {
    if (d->applyingPanelViewportState || d->currentPath.isEmpty()) {
        return;
    }
    constexpr qsizetype MaxRememberedPanelViewports = 512;
    if (!d->panelViewportStates.contains(d->currentPath)
        && d->panelViewportStates.size() >= MaxRememberedPanelViewports) {
        d->panelViewportStates.erase(d->panelViewportStates.begin());
    }
    d->panelViewportStates.insert(d->currentPath, {
        d->panelScrollOffset,
        d->panelViewportCursorEntryId,
    });
    if (!d->panelViewportStateAvailable) {
        d->panelViewportStateAvailable = true;
        emit panelViewportStateAvailableChanged();
    }
}

void GallerySession::restorePanelViewportStateForPath(const QString &path) {
    const auto state = d->panelViewportStates.constFind(path);
    const bool available = state != d->panelViewportStates.cend();
    const qreal nextOffset = available ? state->scrollOffset : 0;
    const QString nextCursor = available ? state->cursorEntryId : QString();

    d->applyingPanelViewportState = true;
    setPanelScrollOffset(nextOffset);
    setPanelViewportCursorEntryId(nextCursor);
    d->applyingPanelViewportState = false;
    if (d->panelViewportStateAvailable != available) {
        d->panelViewportStateAvailable = available;
        emit panelViewportStateAvailableChanged();
    }
}

QString GallerySession::thumbnailProviderName() const {
    return d->thumbnailProviderName;
}

bool GallerySession::viewerOpen() const {
    return d->viewerOpen;
}

void GallerySession::setViewerOpen(bool open) {
    if (d->viewerOpen == open) {
        if (!open) {
            clearViewerPreviousState(false);
            d->clearViewer();
        }
        return;
    }
    d->viewerOpen = open;
    if (!open) {
        clearViewerPreviousState(false);
        d->clearViewer();
        emit viewerSourceChanged();
    }
    emit viewerOpenChanged();
}

QUrl GallerySession::viewerSource() const {
    if (d->external) {
        return QUrl(d->external->viewerImageUrlAt(d->currentIndex));
    }
    return d->local ? d->local->viewerSource() : QUrl();
}

int GallerySession::viewerSourceLevel() const {
    if (d->local) {
        return d->local->viewerSourceLevel();
    }
    if (!d->external) {
        return -1;
    }
    if (!d->viewerOpen) {
        return viewerSource().isEmpty() ? -1 : 0;
    }
    const auto sources = d->external->viewerImageSourcesAt(d->currentIndex);
    return sources.isEmpty() ? -1 : sources.constLast().second;
}

QString GallerySession::viewerPreviousEntryId() const {
    return d->viewerPreviousEntryId;
}

QString GallerySession::viewerPreviousReturnEntryId() const {
    return d->viewerPreviousReturnEntryId;
}

bool GallerySession::viewerPreviousLocked() const {
    return d->viewerPreviousLocked;
}

QVariantMap GallerySession::viewerPreviousViewport() const {
    return d->viewerPreviousViewport;
}

bool GallerySession::shutdownComplete() const {
    return d->shutdown;
}

bool GallerySession::applyExternalCatalog(
    const QVariantList &entries, qulonglong revision,
    const QVariantMap &options) {
    const bool sourceIdentityChanged = options.value(
        QStringLiteral("sourceIdentityChanged")).toBool();
    if (d->shutdown || !d->external
        || (!sourceIdentityChanged && revision < d->catalogRevision)) {
        return false;
    }
    ExternalCatalogApplyContext context = externalCatalogContext(
        entries, options);
    prepareExternalCatalogPath(context, entries, options);
    if (applySameExternalCatalogRevision(context, revision)) {
        return true;
    }
    if (!d->external->applyCatalog(
            entries, context.metadataDeferred,
            !context.pathChanged, context.totalCount)) {
        return false;
    }
    commitExternalCatalogState(context, revision, options);
    reconcileExternalCatalogCursor(context);
    finishExternalCatalogApply(context);
    return true;
}

GallerySession::ExternalCatalogApplyContext
GallerySession::externalCatalogContext(
    const QVariantList &entries, const QVariantMap &options) const {
    ExternalCatalogApplyContext context;
    context.sourceIdentityChanged = options.value(
        QStringLiteral("sourceIdentityChanged")).toBool();
    context.requestedCatalogReady =
        !options.value(QStringLiteral("catalogProvisional")).toBool()
        && !options.value(QStringLiteral("deferCatalogReady")).toBool();
    context.metadataDeferred = options.value(
        QStringLiteral("metadataDeferred")).toBool();
    context.catalogRowsDeferred = options.value(
        QStringLiteral("catalogRowsDeferred")).toBool();
    context.totalCount = context.catalogRowsDeferred
        ? options.value(QStringLiteral("totalCount"), entries.size()).toInt()
        : -1;
    context.metadataRevision = options.value(
        QStringLiteral("metadataRevision"), qulonglong(0)).toULongLong();
    context.previousCursorId = cursorEntryId();
    context.previousIndex = d->currentIndex;
    context.carriesCursor = options.contains(QStringLiteral("cursorIndex"));
    context.requestedCursorIndex = options.value(
        QStringLiteral("cursorIndex"), context.previousIndex).toInt();
    context.requestedCursorId = options.value(
        QStringLiteral("cursorEntryId")).toString();
    context.carriesPath = options.contains(QStringLiteral("currentPath"))
        || options.contains(QStringLiteral("path"));
    return context;
}

void GallerySession::prepareExternalCatalogPath(
    ExternalCatalogApplyContext &context, const QVariantList &entries,
    const QVariantMap &options) {
    if (!context.carriesPath) {
        return;
    }
    const QString path = options.value(
        QStringLiteral("currentPath"), options.value(QStringLiteral("path")))
        .toString();
    const bool actualPathChanged = d->currentPath != path;
    if (!actualPathChanged && !context.sourceIdentityChanged) {
        return;
    }
    context.pathChanged = true;
    if (d->catalogReady) {
        d->catalogReady = false;
        emit catalogReadyChanged();
    }
    d->currentPath = path;
    clearViewerPreviousState(true);
    if (context.sourceIdentityChanged) {
        d->panelViewportStates.clear();
    }
    restorePanelViewportStateForPath(path);
    if (context.carriesCursor) {
        const int logicalCount = context.catalogRowsDeferred
            ? qMax(context.totalCount, entries.size()) : entries.size();
        d->currentIndex = logicalCount > 0
            ? qBound(0, context.requestedCursorIndex, logicalCount - 1) : -1;
        d->stableCursorEntryId = context.requestedCursorId;
    }
    if (actualPathChanged) {
        emit currentPathChanged();
    }
}

bool GallerySession::applySameExternalCatalogRevision(
    const ExternalCatalogApplyContext &context, qulonglong revision) {
    if (context.pathChanged || revision != d->catalogRevision
        || revision == 0) {
        return false;
    }
    const bool alreadyCompleted = context.metadataDeferred
        && context.metadataRevision == d->metadataRevision
        && d->metadataCompleted;
    d->metadataRevision = context.metadataRevision;
    if (!alreadyCompleted) {
        d->metadataDeferred = context.metadataDeferred;
        d->metadataCompleted = false;
    }
    if (d->catalogReady != context.requestedCatalogReady) {
        d->catalogReady = context.requestedCatalogReady;
        emit catalogReadyChanged();
    }
    return true;
}

void GallerySession::commitExternalCatalogState(
    const ExternalCatalogApplyContext &context, qulonglong revision,
    const QVariantMap &options) {
    d->catalogRevision = revision;
    d->metadataRevision = context.metadataRevision;
    d->metadataDeferred = context.metadataDeferred;
    d->metadataCompleted = false;
    d->catalogStreaming = options.value(
        QStringLiteral("catalogStreaming")).toBool();
}

void GallerySession::reconcileExternalCatalogCursor(
    const ExternalCatalogApplyContext &context) {
    if (!context.pathChanged || !context.carriesCursor) {
        const int remapped = d->external->rowForEntryId(
            context.previousCursorId);
        setCurrentIndex(remapped >= 0 ? remapped : context.previousIndex);
        return;
    }
    const int rowCount = d->external->rowCount();
    const int remapped = d->external->rowForEntryId(
        context.requestedCursorId);
    const int target = rowCount > 0
        ? qBound(0, remapped >= 0 ? remapped
                                 : context.requestedCursorIndex,
                 rowCount - 1)
        : -1;
    const QString materializedId = entryIdAt(target);
    const QString targetId = materializedId.isEmpty()
        ? context.requestedCursorId : materializedId;
    const bool changed = context.previousIndex != target
        || context.previousCursorId != targetId;
    d->currentIndex = target;
    d->stableCursorEntryId = targetId;
    if (d->viewerOpen && changed) {
        d->external->setViewerIndex(target);
    }
    if (changed) {
        emit currentIndexChanged();
        emit viewerSourceChanged();
    }
}

void GallerySession::finishExternalCatalogApply(
    const ExternalCatalogApplyContext &context) {
    sanitizeViewerPreviousStateForCatalog();
    const bool readyChanged =
        d->catalogReady != context.requestedCatalogReady;
    d->catalogReady = context.requestedCatalogReady;
    emit catalogRevisionChanged();
    if (readyChanged) {
        emit catalogReadyChanged();
    }
}

bool GallerySession::appendExternalCatalog(
    const QVariantList &entries, qulonglong revision, int offset, bool final) {
    if (d->shutdown || !d->external || !d->catalogStreaming
        || revision != d->catalogRevision || entries.isEmpty()
        || offset != d->external->rowCount()) {
        return false;
    }
    if (!d->external->appendCatalog(entries, d->metadataDeferred)) {
        return false;
    }
    if (final) {
        d->catalogStreaming = false;
        if (!d->catalogReady) {
            d->catalogReady = true;
            emit catalogReadyChanged();
        }
    }
    return true;
}

bool GallerySession::applyExternalCatalogRows(
    const QVariantList &entries, qulonglong revision) {
    if (d->shutdown || !d->external || entries.isEmpty()
        || revision != d->catalogRevision) {
        return false;
    }
    return d->external->applyCatalogRows(entries, d->metadataDeferred);
}

void GallerySession::setExternalCatalogReady(bool ready) {
    if (d->shutdown || !d->external || d->catalogReady == ready) {
        return;
    }
    d->catalogReady = ready;
    emit catalogReadyChanged();
}

bool GallerySession::applyExternalState(
    const QString &entryId, int cursorIndex,
    const QStringList &selectedEntryIds, qulonglong revision) {
    if (d->shutdown || !d->external ||
        revision < d->selectionRevision) {
        return false;
    }
    const bool selectionChanged = revision == 0
        || d->selectionRevision != revision;
    if (!d->external->applyState(
            entryId, cursorIndex, selectedEntryIds, selectionChanged)) {
        return false;
    }
    setCurrentIndex(d->external->cursorRow());
    if (d->selectionRevision != revision) {
        d->selectionRevision = revision;
        emit selectionRevisionChanged();
    }
    return true;
}

bool GallerySession::applyExternalAppearance(
    const QVariantList &entries, qulonglong revision) {
    // highlightRevision is a content fingerprint, not a monotonic sequence;
    // a valid replacement can therefore be numerically smaller.
    if (d->shutdown || !d->external) {
        return false;
    }
    if (!d->external->applyAppearance(entries)) {
        return false;
    }
    d->highlightRevision = revision;
    return true;
}

bool GallerySession::applyExternalMetadata(
    const QVariantList &entries, qulonglong catalogRevision,
    qulonglong metadataRevision, bool final) {
    if (d->shutdown || !d->external || !d->metadataDeferred
        || catalogRevision != d->catalogRevision
        || metadataRevision != d->metadataRevision) {
        return false;
    }
    if (!d->external->applyMetadata(entries)) {
        return false;
    }
    if (final) {
        d->metadataDeferred = false;
        d->metadataCompleted = true;
    }
    return true;
}

bool GallerySession::applyExternalStateDelta(
    const QString &entryId, int cursorIndex,
    const QVariantList &selectionChanges,
    qulonglong baseSelectionRevision, qulonglong revision) {
    if (d->shutdown || !d->external
        || baseSelectionRevision != d->selectionRevision
        || revision < baseSelectionRevision
        || (!selectionChanges.isEmpty()
            && revision == baseSelectionRevision)) {
        return false;
    }
    if (!d->external->applyStateDelta(entryId, cursorIndex,
                                      selectionChanges)) {
        return false;
    }
    setCurrentIndex(d->external->cursorRow());
    if (d->selectionRevision != revision) {
        d->selectionRevision = revision;
        emit selectionRevisionChanged();
    }
    return true;
}

bool GallerySession::applySnapshot(
    const QString &path, const QVariantList &entries,
    qulonglong catalogRevision, qulonglong selectionRevision) {
    QVariantMap options;
    options.insert(QStringLiteral("currentPath"), path);
    if (!applyExternalCatalog(entries, catalogRevision, options)) {
        return false;
    }
    QStringList selectedIds;
    QString cursorId;
    int cursorIndex = d->currentIndex;
    for (int row = 0; row < entries.size(); ++row) {
        const QVariantMap entry = entries.at(row).toMap();
        if (entry.value(QStringLiteral("selected")).toBool()) {
            selectedIds.append(
                entry.value(QStringLiteral("entryId")).toString());
        }
        if (entry.value(QStringLiteral("cursor")).toBool()) {
            cursorId =
                entry.value(QStringLiteral("entryId")).toString();
            cursorIndex = row;
        }
    }
    return applyExternalState(cursorId, cursorIndex, selectedIds,
                              selectionRevision);
}

bool GallerySession::applySelection(
    const QVariantList &selectedEntryIds, const QString &entryId,
    qulonglong revision) {
    QStringList ids;
    ids.reserve(selectedEntryIds.size());
    for (const QVariant &id : selectedEntryIds) {
        ids.append(id.toString());
    }
    return applyExternalState(entryId, d->currentIndex, ids, revision);
}

int GallerySession::cd(
    const QString &path, const QString &itemToSelect) {
    if (d->shutdown || !d->local) {
        return -1;
    }
    const int index = d->local->cd(path, itemToSelect);
    setCurrentIndex(index);
    return d->currentIndex;
}

QString GallerySession::entryIdAt(int index) const {
    return d->external ? d->external->entryIdAt(index)
                       : d->local ? d->local->entryIdAt(index) : QString();
}

int GallerySession::indexForEntryId(const QString &entryId) const {
    if (entryId.isEmpty()) {
        return -1;
    }
    return d->external ? d->external->rowForEntryId(entryId)
                       : d->local ? d->local->rowForEntryId(entryId) : -1;
}

QString GallerySession::entryNameAt(int index) const {
    return d->external ? d->external->entryNameAt(index)
                       : d->local ? d->local->entryNameAt(index) : QString();
}

QString GallerySession::localPathAt(int index) const {
    return d->external ? d->external->localPathAt(index)
                       : d->local ? d->local->localPathAt(index) : QString();
}

bool GallerySession::isImageAt(int index) const {
    return d->external ? d->external->isImageAt(index)
                       : d->local && d->local->isImageAt(index);
}

bool GallerySession::isDirectoryAt(int index) const {
    return d->external ? d->external->isDirectoryAt(index)
                       : d->local && d->local->isDirectoryAt(index);
}

bool GallerySession::isSelectedAt(int index) const {
    QAbstractItemModel *catalog = d->model();
    if (!catalog || index < 0 || index >= catalog->rowCount()) {
        return false;
    }
    const int selectedRole = catalog->roleNames().key(
        QByteArrayLiteral("selectedRole"), -1);
    return selectedRole >= 0
        && catalog->data(catalog->index(index, 0), selectedRole).toBool();
}

QVariantMap GallerySession::highlightStyleAt(int index) const {
    if (d->external) {
        return d->external->highlightStyleAt(index);
    }
    QAbstractItemModel *catalog = d->model();
    if (!catalog || index < 0 || index >= catalog->rowCount()) {
        return {};
    }
    const int imageFileRole = catalog->roleNames().key(
        QByteArrayLiteral("imageFileRole"), -1);
    if (imageFileRole < 0) {
        return {};
    }
    ImageFile *image = qvariant_cast<ImageFile *>(
        catalog->data(catalog->index(index, 0), imageFileRole));
    return image ? image->highlightStyle() : QVariantMap();
}

int GallerySession::sourceIndexAt(int index) const {
    return d->external ? d->external->sourceIndexAt(index)
                       : d->local ? d->local->sourceIndexAt(index) : -1;
}

QSize GallerySession::imageOriginalSizeAt(int index) const {
    return d->external ? d->external->imageOriginalSizeAt(index)
                       : d->local ? d->local->imageOriginalSizeAt(index)
                                  : QSize();
}

int GallerySession::adjacentImageIndex(
    int fromIndex, int direction) const {
    QAbstractItemModel *catalog = d->model();
    const int count = catalog ? catalog->rowCount() : 0;
    if (fromIndex < 0 || fromIndex >= count || direction == 0) {
        return -1;
    }
    const int step = direction < 0 ? -1 : 1;
    for (int row = fromIndex + step; row >= 0 && row < count; row += step) {
        if (isImageAt(row)) {
            return row;
        }
    }
    return fromIndex;
}

QUrl GallerySession::viewerSourceAt(int index) const {
    return d->external ? QUrl(d->external->bestViewerImageUrlAt(index))
                       : d->local ? d->local->viewerSourceAt(index)
                                  : QUrl();
}

QVariantList GallerySession::viewerSourcesAt(int index) const {
    const QList<QPair<QString, int>> sources = d->external
        ? d->external->viewerImageSourcesAt(index)
        : d->local ? d->local->viewerImageSourcesAt(index)
                   : QList<QPair<QString, int>>();
    QVariantList result;
    result.reserve(sources.size());
    for (const auto &[source, level] : sources) {
        result.append(QVariantMap{
            {QStringLiteral("source"), QUrl(source)},
            {QStringLiteral("level"), level},
        });
    }
    return result;
}

void GallerySession::activateIndex(int index) {
    setCurrentIndex(index);
}

void GallerySession::ensurePreviews() {
    if (d->shutdown) {
        return;
    }
    if (d->external) {
        d->external->ensurePreviews();
    }
    else if (d->local) {
        d->local->ensurePreviews();
    }
}

void GallerySession::requestViewer(int width, int height) {
    if (d->shutdown || !d->viewerOpen) {
        return;
    }
    const QSize size(qMax(0, width), qMax(0, height));
    if (d->external) {
        d->external->requestViewer(d->currentIndex, size);
    }
    else if (d->local) {
        d->local->requestViewer(d->currentIndex, size);
    }
}

void GallerySession::requestViewerAt(int index, int width, int height) {
    if (d->shutdown || !d->viewerOpen || !isImageAt(index)) {
        return;
    }
    const QSize size(qMax(0, width), qMax(0, height));
    if (d->external) {
        d->external->requestViewerAt(index, size);
    }
    else if (d->local) {
        d->local->requestViewerAt(index, size);
    }
}

void GallerySession::setViewerPreviousState(
    const QString &previousEntryId,
    const QString &returnEntryId,
    bool locked,
    const QVariantMap &pendingViewport) {
    const QString normalizedReturn = locked ? returnEntryId : QString();
    if (d->viewerPreviousEntryId == previousEntryId
        && d->viewerPreviousReturnEntryId == normalizedReturn
        && d->viewerPreviousLocked == locked
        && d->viewerPreviousViewport == pendingViewport) {
        return;
    }
    d->viewerPreviousEntryId = previousEntryId;
    d->viewerPreviousReturnEntryId = normalizedReturn;
    d->viewerPreviousLocked = locked;
    d->viewerPreviousViewport = pendingViewport;
    emit viewerPreviousStateChanged();
}

void GallerySession::clearViewerPreviousState(bool includeLocked) {
    if (d->viewerPreviousLocked && !includeLocked) {
        return;
    }
    setViewerPreviousState(QString(), QString(), false, QVariantMap());
}

void GallerySession::sanitizeViewerPreviousStateForCatalog() {
    QString previousEntryId = d->viewerPreviousEntryId;
    QString returnEntryId = d->viewerPreviousReturnEntryId;
    QVariantMap pendingViewport = d->viewerPreviousViewport;

    // The baseline forgets a missing ordinary previous image, but a locked
    // identity remains locked and can become valid again after another model
    // refresh.  Stable IDs provide the same behavior without stale indexes.
    if (!d->viewerPreviousLocked && !previousEntryId.isEmpty()
        && indexForEntryId(previousEntryId) < 0) {
        previousEntryId.clear();
    }

    const QString viewportTarget = pendingViewport
        .value(QStringLiteral("targetEntryId")).toString();
    if (!viewportTarget.isEmpty() && indexForEntryId(viewportTarget) < 0) {
        pendingViewport.clear();
    }

    setViewerPreviousState(previousEntryId, returnEntryId,
                           d->viewerPreviousLocked, pendingViewport);
}

namespace {
QVariantMap actionPayload(const GallerySession *session, int index) {
    return {
        {QStringLiteral("entryId"), session->entryIdAt(index)},
        {QStringLiteral("index"), session->sourceIndexAt(index)},
        {QStringLiteral("catalogRevision"), session->catalogRevision()},
    };
}
}

void GallerySession::requestCursor(int index) {
    activateIndex(index);
    if (d->external) {
        emit actionRequested(QStringLiteral("panel.cursor"),
                             actionPayload(this, d->currentIndex));
    }
}

void GallerySession::requestOpen(int index) {
    activateIndex(index);
    if (isImageAt(d->currentIndex)) {
        setViewerOpen(true);
        emit viewerRequested(d->currentIndex, viewerSource());
        return;
    }
    if (d->local && isDirectoryAt(d->currentIndex)) {
        cd(localPathAt(d->currentIndex));
        return;
    }
    if (d->external) {
        emit actionRequested(QStringLiteral("panel.open"),
                             actionPayload(this, d->currentIndex));
    }
}

void GallerySession::requestToggleSelection(int index) {
    activateIndex(index);
    if (d->local) {
        d->local->toggleSelection(d->currentIndex);
        return;
    }
    emit actionRequested(QStringLiteral("panel.toggleSelection"),
                         actionPayload(this, d->currentIndex));
}

void GallerySession::applySelectionIntent(
    const QStringList &selectedEntryIds,
    const QStringList &deselectedEntryIds) {
    if (d->shutdown || !d->local) {
        return;
    }
    for (const QString &entryId : selectedEntryIds) {
        const int index = indexForEntryId(entryId);
        if (index >= 0 && !isSelectedAt(index)) {
            d->local->setSelection(index, true);
        }
    }
    for (const QString &entryId : deselectedEntryIds) {
        const int index = indexForEntryId(entryId);
        if (index >= 0 && isSelectedAt(index)) {
            d->local->setSelection(index, false);
        }
    }
}

void GallerySession::resetExternalSource() {
    if (d->shutdown || !d->external) {
        return;
    }

    const bool hadPath = !d->currentPath.isEmpty();
    const bool hadCursor = d->currentIndex != -1;
    const bool hadCatalogRevision = d->catalogRevision != 0;
    const bool hadSelectionRevision = d->selectionRevision != 0;

    d->external->resetExternalSource();
    d->currentPath.clear();
    d->currentIndex = -1;
    d->stableCursorEntryId.clear();
    d->catalogRevision = 0;
    d->metadataRevision = 0;
    d->metadataDeferred = false;
    d->metadataCompleted = false;
    const bool catalogWasReady = d->catalogReady;
    d->catalogReady = true;
    d->highlightRevision = 0;
    d->selectionRevision = 0;
    clearViewerPreviousState(true);
    d->panelViewportStates.clear();
    restorePanelViewportStateForPath(QString());
    setViewerOpen(false);

    if (hadPath) emit currentPathChanged();
    if (hadCursor) emit currentIndexChanged();
    if (hadCatalogRevision) emit catalogRevisionChanged();
    if (!catalogWasReady) emit catalogReadyChanged();
    if (hadSelectionRevision) emit selectionRevisionChanged();
}

void GallerySession::shutdown() {
    if (d->shutdown) {
        return;
    }
    setViewerOpen(false);
    d->shutdown = true;
    if (d->external) {
        d->external->shutdown();
    }
    else if (d->local) {
        d->local->shutdown();
    }
    emit shutdownCompleteChanged();
}

} // namespace ZoinGallery
