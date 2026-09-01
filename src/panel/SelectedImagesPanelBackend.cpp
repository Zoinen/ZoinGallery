#include <ZoinGallery/SelectedImagesPanelBackend.h>

#include <ZoinGallery/GalleryCatalogModel.h>

#include "FileListModel.h"
#include "ImageFile.h"
#include "SelectedImagesModel.h"

#include <QAbstractItemModel>

#include <algorithm>
#include <utility>

namespace ZoinGallery {

SelectedImagesPanelBackend::SelectedImagesPanelBackend(QObject *parent)
    : GalleryPanelBackend(parent),
      _catalog(new GalleryCatalogModel(this)) {}

QAbstractItemModel *SelectedImagesPanelBackend::sourceModel() const {
    return _source;
}

void SelectedImagesPanelBackend::setSourceModel(QAbstractItemModel *model) {
    auto *selected = qobject_cast<SelectedImagesModel *>(model);
    if (_source == selected) {
        return;
    }
    disconnectSource();
    _source = selected;
    _catalog->setSourceModel(selected);
    rebuildIdentityIndex();
    _currentIndex = _source && _source->rowCount() > 0 ? 0 : -1;
    _cursorEntryId = entryIdAt(_currentIndex);
    ++_catalogRevision;
    connectSource();
    emit sourceModelChanged();
    emit catalogRevisionChanged();
    emit catalogChanged();
    emit currentIndexChanged();
}

GalleryCatalogModel *SelectedImagesPanelBackend::catalogModel() const {
    return _catalog;
}

int SelectedImagesPanelBackend::currentIndex() const {
    return _currentIndex;
}

qulonglong SelectedImagesPanelBackend::catalogRevision() const {
    return _catalogRevision;
}

qulonglong SelectedImagesPanelBackend::selectionRevision() const {
    return _selectionRevision;
}

QString SelectedImagesPanelBackend::entryIdAt(int index) const {
    const ImageFile *item = itemAt(index);
    return item ? item->fullPath() : QString();
}

int SelectedImagesPanelBackend::indexForEntryId(
    const QString &entryId) const {
    return _rowByEntryId.value(entryId, -1);
}

int SelectedImagesPanelBackend::sourceIndexAt(int index) const {
    return _source ? _source->mapToSourceRow(index) : -1;
}

bool SelectedImagesPanelBackend::isSelectedAt(int index) const {
    return _source && _source->isIndexSelected(index);
}

QString SelectedImagesPanelBackend::entryNameAt(int index) const {
    const ImageFile *item = itemAt(index);
    return item ? item->text() : QString();
}

bool SelectedImagesPanelBackend::isImageAt(int index) const {
    return itemAt(index) != nullptr;
}

QVariantMap SelectedImagesPanelBackend::highlightStyleAt(int index) const {
    const ImageFile *item = itemAt(index);
    return item ? item->highlightStyle() : QVariantMap{};
}

qreal SelectedImagesPanelBackend::panelScrollOffset() const {
    return _scrollOffset;
}

void SelectedImagesPanelBackend::setPanelScrollOffset(qreal offset) {
    const qreal bounded = std::max<qreal>(0, offset);
    if (qFuzzyCompare(_scrollOffset, bounded)) {
        return;
    }
    _scrollOffset = bounded;
    _viewportStateAvailable = true;
    emit panelViewportChanged();
}

QString SelectedImagesPanelBackend::panelViewportCursorEntryId() const {
    return _viewportCursorEntryId;
}

void SelectedImagesPanelBackend::setPanelViewportCursorEntryId(
    const QString &entryId) {
    if (_viewportCursorEntryId == entryId) {
        return;
    }
    _viewportCursorEntryId = entryId;
    _viewportStateAvailable = true;
    emit panelViewportChanged();
}

bool SelectedImagesPanelBackend::panelViewportStateAvailable() const {
    return _viewportStateAvailable;
}

bool SelectedImagesPanelBackend::canRemoveEntries() const {
    return _source != nullptr;
}

bool SelectedImagesPanelBackend::canDragEntries() const {
    return _source != nullptr;
}

GalleryDragDescriptor SelectedImagesPanelBackend::prepareDrag(
    int index, bool singleItemOnly, int previewLimit) const {
    return _source
        ? galleryDragDescriptorFromVariants(
              _source->dragUrlsForIndex(index, singleItemOnly),
              _source->dragPreviewItemsForIndex(
                  index, previewLimit, singleItemOnly))
        : GalleryDragDescriptor{};
}

GalleryFileOperationResult
SelectedImagesPanelBackend::finalizeExternalDrag(
    const QVariantList &urls, Qt::DropAction action) {
    return _source
        ? galleryFileOperationResultFromVariant(
              _source->finalizeExternalDrag(urls, action))
        : GalleryFileOperationResult{};
}

void SelectedImagesPanelBackend::configureNativeDragCursors(
    QObject *dragSource) {
    if (_source) {
        _source->configureNativeDragCursors(dragSource);
    }
}

void SelectedImagesPanelBackend::removeEntry(int index) {
    if (_source) {
        _source->removeFromCollection(index);
    }
}

bool SelectedImagesPanelBackend::remoteAuthoritative() const {
    return false;
}

void SelectedImagesPanelBackend::activateIndex(int index) {
    setCurrentIndex(index);
}

void SelectedImagesPanelBackend::applySelectionIntent(
    const QStringList &selectedEntryIds,
    const QStringList &deselectedEntryIds) {
    if (!_source) {
        return;
    }
    QList<int> selected;
    QList<int> deselected;
    selected.reserve(selectedEntryIds.size());
    deselected.reserve(deselectedEntryIds.size());
    for (const QString &entryId : selectedEntryIds) {
        const int row = indexForEntryId(entryId);
        if (row >= 0) {
            selected.append(row);
        }
    }
    for (const QString &entryId : deselectedEntryIds) {
        const int row = indexForEntryId(entryId);
        if (row >= 0) {
            deselected.append(row);
        }
    }
    _source->applySelectionChanges(selected, deselected);
}

ImageFile *SelectedImagesPanelBackend::itemAt(int index) const {
    if (!_source || index < 0 || index >= _source->rowCount()) {
        return nullptr;
    }
    return _source->data(_source->index(index, 0),
                         FileListModel::ImageFileRole)
        .value<ImageFile *>();
}

void SelectedImagesPanelBackend::disconnectSource() {
    for (const QMetaObject::Connection &connection :
         std::as_const(_connections)) {
        disconnect(connection);
    }
    _connections.clear();
}

void SelectedImagesPanelBackend::connectSource() {
    if (!_source) {
        return;
    }
    const auto changed = [this] { handleCatalogChange(); };
    _connections.append(connect(
        _source, &QAbstractItemModel::modelReset, this, changed));
    _connections.append(connect(
        _source, &QAbstractItemModel::rowsInserted, this,
        [changed](const QModelIndex &, int, int) { changed(); }));
    _connections.append(connect(
        _source, &QAbstractItemModel::rowsRemoved, this,
        [changed](const QModelIndex &, int, int) { changed(); }));
    _connections.append(connect(
        _source, &QAbstractItemModel::layoutChanged, this,
        [changed](const QList<QPersistentModelIndex> &,
                  QAbstractItemModel::LayoutChangeHint) { changed(); }));
    _connections.append(connect(
        _source, &SelectedImagesModel::panelSelectionChanged,
        this, [this] {
            ++_selectionRevision;
            emit selectionRevisionChanged();
        }));
    _connections.append(connect(
        _source, &QObject::destroyed, this, [this] {
            disconnectSource();
            _source = nullptr;
            _catalog->setSourceModel(nullptr);
            _rowByEntryId.clear();
            _currentIndex = -1;
            _cursorEntryId.clear();
            emit sourceModelChanged();
            emit catalogChanged();
            emit currentIndexChanged();
        }));
}

void SelectedImagesPanelBackend::rebuildIdentityIndex() {
    _rowByEntryId.clear();
    if (!_source) {
        return;
    }
    _rowByEntryId.reserve(_source->rowCount());
    for (int row = 0; row < _source->rowCount(); ++row) {
        const QString id = entryIdAt(row);
        if (!id.isEmpty()) {
            _rowByEntryId.insert(id, row);
        }
    }
}

void SelectedImagesPanelBackend::handleCatalogChange() {
    const QString stableId = _cursorEntryId;
    rebuildIdentityIndex();
    const int next = _source && _source->rowCount() > 0
        ? std::max(0, indexForEntryId(stableId)) : -1;
    const bool cursorChanged = _currentIndex != next;
    _currentIndex = next;
    _cursorEntryId = entryIdAt(next);
    ++_catalogRevision;
    emit catalogRevisionChanged();
    emit catalogChanged();
    if (cursorChanged) {
        emit currentIndexChanged();
    }
}

void SelectedImagesPanelBackend::setCurrentIndex(int index) {
    const int count = _source ? _source->rowCount() : 0;
    const int bounded = count > 0 ? std::clamp(index, 0, count - 1) : -1;
    if (_currentIndex == bounded) {
        _cursorEntryId = entryIdAt(bounded);
        return;
    }
    _currentIndex = bounded;
    _cursorEntryId = entryIdAt(bounded);
    emit currentIndexChanged();
}

} // namespace ZoinGallery
