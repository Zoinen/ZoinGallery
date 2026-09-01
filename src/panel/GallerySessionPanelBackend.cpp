#include <ZoinGallery/GallerySessionPanelBackend.h>

#include <ZoinGallery/GalleryCatalogModel.h>
#include <ZoinGallery/GallerySession.h>

#include "FileListModel.h"

namespace ZoinGallery {

GallerySessionPanelBackend::GallerySessionPanelBackend(
    GallerySession *session, QObject *parent)
    : GalleryPanelBackend(parent), _session(session),
      _catalog(new GalleryCatalogModel(this)) {
    _catalog->setSession(session);
    _catalog->setSourceModel(session ? session->model() : nullptr);
    if (!session) {
        return;
    }
    connect(session, &GallerySession::currentIndexChanged,
            this, &GalleryPanelBackend::currentIndexChanged);
    connect(session, &GallerySession::catalogRevisionChanged,
            this, [this] {
                emit catalogRevisionChanged();
                emit catalogChanged();
            });
    connect(session, &GallerySession::selectionRevisionChanged,
            this, &GalleryPanelBackend::selectionRevisionChanged);
    connect(session, &GallerySession::currentPathChanged,
            this, &GalleryPanelBackend::currentPathChanged);
    connect(session, &GallerySession::catalogReadyChanged,
            this, &GalleryPanelBackend::catalogReadyChanged);
    connect(session, &GallerySession::panelScrollOffsetChanged,
            this, &GalleryPanelBackend::panelViewportChanged);
    connect(session, &GallerySession::panelViewportCursorEntryIdChanged,
            this, &GalleryPanelBackend::panelViewportChanged);
    connect(session, &GallerySession::panelViewportStateAvailableChanged,
            this, &GalleryPanelBackend::panelViewportChanged);
    connect(session, &QObject::destroyed, this, [this] {
        _catalog->setSourceModel(nullptr);
        _catalog->setSession(nullptr);
        _session = nullptr;
        emit catalogChanged();
        emit currentIndexChanged();
    });
}

GallerySession *GallerySessionPanelBackend::session() const {
    return _session;
}

GalleryCatalogModel *GallerySessionPanelBackend::catalogModel() const {
    return _catalog;
}

int GallerySessionPanelBackend::currentIndex() const {
    return _session ? _session->currentIndex() : -1;
}

qulonglong GallerySessionPanelBackend::catalogRevision() const {
    return _session ? _session->catalogRevision() : 0;
}

qulonglong GallerySessionPanelBackend::selectionRevision() const {
    return _session ? _session->selectionRevision() : 0;
}

QString GallerySessionPanelBackend::entryIdAt(int index) const {
    return _session ? _session->entryIdAt(index) : QString();
}

int GallerySessionPanelBackend::indexForEntryId(
    const QString &entryId) const {
    return _session ? _session->indexForEntryId(entryId) : -1;
}

int GallerySessionPanelBackend::sourceIndexAt(int index) const {
    return _session ? _session->sourceIndexAt(index) : -1;
}

bool GallerySessionPanelBackend::isSelectedAt(int index) const {
    return _session && _session->isSelectedAt(index);
}

QString GallerySessionPanelBackend::entryNameAt(int index) const {
    return _session ? _session->entryNameAt(index) : QString();
}

bool GallerySessionPanelBackend::isImageAt(int index) const {
    return _session && _session->isImageAt(index);
}

QVariantMap GallerySessionPanelBackend::highlightStyleAt(int index) const {
    return _session ? _session->highlightStyleAt(index) : QVariantMap{};
}

QString GallerySessionPanelBackend::currentPath() const {
    return _session ? _session->currentPath() : QString();
}

bool GallerySessionPanelBackend::catalogReady() const {
    return !_session || _session->catalogReady();
}

qreal GallerySessionPanelBackend::panelScrollOffset() const {
    return _session ? _session->panelScrollOffset() : 0;
}

void GallerySessionPanelBackend::setPanelScrollOffset(qreal offset) {
    if (_session) {
        _session->setPanelScrollOffset(offset);
    }
}

QString GallerySessionPanelBackend::panelViewportCursorEntryId() const {
    return _session ? _session->panelViewportCursorEntryId() : QString();
}

void GallerySessionPanelBackend::setPanelViewportCursorEntryId(
    const QString &entryId) {
    if (_session) {
        _session->setPanelViewportCursorEntryId(entryId);
    }
}

bool GallerySessionPanelBackend::panelViewportStateAvailable() const {
    return _session && _session->panelViewportStateAvailable();
}

void GallerySessionPanelBackend::ensurePreviews() {
    if (_session) {
        _session->ensurePreviews();
    }
}

bool GallerySessionPanelBackend::canDragEntries() const {
    return _session && _session->localSource()
        && qobject_cast<FileListModel *>(_session->fileListModel());
}

bool GallerySessionPanelBackend::canDropIntoDirectories() const {
    return canDragEntries();
}

bool GallerySessionPanelBackend::canPreviewDirectories() const {
    return canDragEntries();
}

GalleryDragDescriptor GallerySessionPanelBackend::prepareDrag(
    int index, bool singleItemOnly, int previewLimit) const {
    auto *files = _session
        ? qobject_cast<FileListModel *>(_session->fileListModel()) : nullptr;
    const int sourceIndex = _session ? _session->sourceIndexAt(index) : -1;
    if (!files || sourceIndex < 0) {
        return {};
    }
    return galleryDragDescriptorFromVariants(
        files->dragUrlsForIndex(sourceIndex, singleItemOnly),
        files->dragPreviewItemsForIndex(
            sourceIndex, previewLimit, singleItemOnly));
}

GalleryFileOperationResult
GallerySessionPanelBackend::finalizeExternalDrag(
    const QVariantList &urls, Qt::DropAction action) {
    auto *files = _session
        ? qobject_cast<FileListModel *>(_session->fileListModel()) : nullptr;
    return files
        ? galleryFileOperationResultFromVariant(
              files->finalizeExternalDrag(urls, action))
        : GalleryFileOperationResult{};
}

void GallerySessionPanelBackend::configureNativeDragCursors(
    QObject *dragSource) {
    if (auto *files = _session
            ? qobject_cast<FileListModel *>(_session->fileListModel())
            : nullptr) {
        files->configureNativeDragCursors(dragSource);
    }
}

GalleryFileOperationResult
GallerySessionPanelBackend::dropUrlsIntoDirectory(
    const QVariantList &urls, int directoryIndex,
    Qt::DropAction action) {
    auto *files = _session
        ? qobject_cast<FileListModel *>(_session->fileListModel()) : nullptr;
    if (!files || !_session || !_session->isDirectoryAt(directoryIndex)) {
        return {};
    }
    return galleryFileOperationResultFromVariant(
        files->dropUrlsIntoFolder(
            urls, _session->localPathAt(directoryIndex), action));
}

QAbstractItemModel *GallerySessionPanelBackend::directoryPreviewModel(
    int index) {
    auto *files = _session
        ? qobject_cast<FileListModel *>(_session->fileListModel()) : nullptr;
    const int sourceIndex = _session ? _session->sourceIndexAt(index) : -1;
    return files && sourceIndex >= 0
        ? files->folderModel(sourceIndex) : nullptr;
}

bool GallerySessionPanelBackend::remoteAuthoritative() const {
    return _session
        && _session->sourceKind() == GallerySession::ExternalCatalogSource;
}

void GallerySessionPanelBackend::activateIndex(int index) {
    if (_session) {
        _session->activateIndex(index);
    }
}

void GallerySessionPanelBackend::applySelectionIntent(
    const QStringList &selectedEntryIds,
    const QStringList &deselectedEntryIds) {
    if (_session) {
        _session->applySelectionIntent(selectedEntryIds,
                                       deselectedEntryIds);
    }
}

} // namespace ZoinGallery
