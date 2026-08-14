#include "SelectedImagesModel.h"

#include "DecodeManager.h"
#include "PersistentSelectionCache.h"
#include "ThumbnailLoader.h"

#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {
constexpr int FailedImageWorkRetryInitialMs = 250;
constexpr int FailedImageWorkRetryMaxMs = 5000;
constexpr int FailedImageWorkMaxAttempts = 8;

QString selectedInfoRetryKey(const ImageInfo &info) {
    return QStringLiteral("%1\x1f%2\x1f%3")
        .arg(info.path)
        .arg(info.lastModified.isValid()
                 ? info.lastModified.toMSecsSinceEpoch() : -1)
        .arg(info.fileSize);
}

QString selectedDecodeRetryKey(const ImageDecodeRequest &request) {
    return QStringLiteral("%1\x1f%2\x1f%3x%4\x1f%5\x1f%6\x1f%7\x1f%8")
        .arg(request.info.path)
        .arg(request.viewerRequest ? 1 : 0)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(request.info.lastModified.isValid()
                 ? request.info.lastModified.toMSecsSinceEpoch() : -1)
        .arg(request.info.fileSize)
        .arg(request.checkCache ? 1 : 0)
        .arg(request.fitToViewerRequest ? 1 : 0);
}
}

SelectedImagesModel::SelectedImagesModel(
    FileListModel *sourceModel,
    QSharedPointer<ProviderImageStore> providerImageStore, QObject *parent)
    : SelectedImagesModel(sourceModel, std::move(providerImageStore),
                          nullptr, QString(),
                          QStringLiteral("selected-"),
                          QStringLiteral("zoingallery-thumbnails"),
                          QStringLiteral("zoingallery-async"), parent) {
}

SelectedImagesModel::SelectedImagesModel(
    FileListModel *sourceModel,
    QSharedPointer<ProviderImageStore> providerImageStore,
    DecodeManager *sharedDecodeManager,
    const QString &requestNamespace,
    const QString &imageIdPrefix,
    const QString &thumbnailProviderName,
    const QString &asyncProviderName,
    QObject *parent)
    : SelectedImagesModel(sourceModel, std::move(providerImageStore),
                          sharedDecodeManager, requestNamespace,
                          imageIdPrefix, thumbnailProviderName,
                          asyncProviderName,
                          ViewerImageCache::DefaultFitByteBudget,
                          ViewerImageCache::DefaultNativeByteBudget,
                          parent) {
}

SelectedImagesModel::SelectedImagesModel(
    FileListModel *sourceModel,
    QSharedPointer<ProviderImageStore> providerImageStore,
    DecodeManager *sharedDecodeManager,
    const QString &requestNamespace,
    const QString &imageIdPrefix,
    const QString &thumbnailProviderName,
    const QString &asyncProviderName,
    qint64 viewerFitCacheByteBudget,
    qint64 viewerNativeCacheByteBudget,
    QObject *parent)
    : QAbstractListModel(parent),
      _selectionSourceModel(sourceModel),
      _decodeManager(sharedDecodeManager
                         ? sharedDecodeManager
                         : new DecodeManager(this)),
      _ownsDecodeManager(!sharedDecodeManager),
      _requestNamespace(requestNamespace),
      _imageIdPrefix(imageIdPrefix),
      _thumbnailProviderName(thumbnailProviderName),
      _asyncProviderName(asyncProviderName),
      _providerImageStore(std::move(providerImageStore)),
      _viewerImageCache(imageIdPrefix + QStringLiteral("viewer-"),
                        _providerImageStore,
                        thumbnailProviderName,
                        asyncProviderName,
                        viewerFitCacheByteBudget,
                        viewerNativeCacheByteBudget) {
    if (_ownsDecodeManager) {
        _decodeManager->setImageCacheMode(
            cacheUsageModeFromInt(_selectionSourceModel->imageCacheMode()));
    }

    connect(_selectionSourceModel, &FileListModel::selectionPathsChanged,
            this, &SelectedImagesModel::syncPathsFromPersistentSelection);
    connect(_selectionSourceModel,
            &FileListModel::watchedImageMetadataChanged,
            this, &SelectedImagesModel::refreshWatchedImageMetadata);
    connect(_selectionSourceModel, &FileListModel::activeSelectionGroupChanged,
            this, [this]() {
                const QString nextGroupId =
                    _selectionSourceModel->activeSelectionGroupId();
                if (nextGroupId != _activeGroupId) {
                    syncFromPersistentSelection();
                }
            });
    connect(_selectionSourceModel, &FileListModel::imageCacheModeChanged, this, [this]() {
        cancelSessionRequests();
        _decodeManager->setImageCacheMode(
            cacheUsageModeFromInt(_selectionSourceModel->imageCacheMode()));
        if (_selectionSourceModel->imageSourceAccessEnabled()) {
            scheduleFailedImageWorkRetry();
        } else {
            _failedImageWorkRetryTimer.stop();
        }
        _imageInfoRequestTimer.start();
        if (_hasCurrentViewerRequest) {
            const int viewerIndex = indexForPath(_currentViewerPath);
            if (viewerIndex >= 0) {
                requestViewer(viewerIndex,
                              _currentViewerRequestSize.width(),
                              _currentViewerRequestSize.height());
            }
        }
        emit thumbnailReloadRequested();
    });
    connect(_decodeManager, &DecodeManager::imageInfoReady,
            this, [this](const ImageInfo &info) {
                if (acceptsRequestNamespace(info.requestNamespace)) {
                    onImageInfoAvailable(info);
                }
            });
    connect(_decodeManager, &DecodeManager::imagesInfoReady,
            this, [this](const QList<ImageInfo> &infos) {
                QList<ImageInfo> matching;
                matching.reserve(infos.size());
                for (const ImageInfo &info : infos) {
                    if (acceptsRequestNamespace(info.requestNamespace)) {
                        matching.append(info);
                    }
                }
                if (!matching.isEmpty()) {
                    onImagesInfoAvailable(matching);
                }
            });
    connect(_decodeManager, &DecodeManager::imageReady,
            this, [this](const ImageDecodeRequest &request,
                         const QImage &image,
                         const DecodedImageInfo &decodedInfo) {
                if (acceptsRequestNamespace(request.requestNamespace)) {
                    onImageAvailable(request, image, decodedInfo);
                }
            });
    connect(_decodeManager, &DecodeManager::imageReadFailed,
            this, [this](const ImageDecodeRequest &request) {
                if (!acceptsRequestNamespace(request.requestNamespace)) {
                    return;
                }
                rememberFailedDecodeRequest(request);
            });
    connect(_decodeManager, &DecodeManager::viewerRunnerCanceled,
            this, [this](const QString &path,
                         const QString &requestNamespace) {
                if (acceptsRequestNamespace(requestNamespace)) {
                    _viewerImageCache.removeIncomplete(path);
                }
            });
    _imageInfoRequestTimer.setSingleShot(true);
    _imageInfoRequestTimer.setInterval(0);
    connect(&_imageInfoRequestTimer, &QTimer::timeout,
            this, &SelectedImagesModel::requestMissingImageInfo);
    _failedImageWorkRetryTimer.setSingleShot(true);
    connect(&_failedImageWorkRetryTimer, &QTimer::timeout,
            this, &SelectedImagesModel::retryFailedImageWork);

    syncFromPersistentSelection();
}

int SelectedImagesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : _items.size();
}

QVariant SelectedImagesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= _items.size()) {
        return QVariant();
    }

    ImageFile *item = _items[index.row()];
    switch (role) {
    case FileListModel::ImageIdUrlRole:
        return item->imageIdUrl();
    case FileListModel::SelectedRole:
        return item->isSelected();
    case FileListModel::SelectionGroupIdRole:
        return item->selectionGroupId();
    case FileListModel::SelectionGroupColorRole:
        return item->selectionGroupColor();
    case FileListModel::ImageFileRole:
        return QVariant::fromValue(item);
    case FileListModel::FolderRole:
        return false;
    case FileListModel::IsImageRole:
        return true;
    case FileListModel::ImageFullSizeRole:
        return item->fullSize();
    case FileListModel::FolderViewRole:
        return false;
    case FileListModel::LastModifiedRole:
        return item->lastModified();
    case FileListModel::FileSizeRole:
        return item->fileSize();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> SelectedImagesModel::roleNames() const {
    return _selectionSourceModel->roleNames();
}

ImageFile *SelectedImagesModel::rootItem() const {
    return nullptr;
}

void SelectedImagesModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    QList<ImageDecodeRequest> namespacedRequests = requests;
    for (ImageDecodeRequest &request : namespacedRequests) {
        request.requestNamespace = _requestNamespace;
        request.info.requestNamespace = _requestNamespace;
    }
    _decodeManager->decodeImages(namespacedRequests);
}

void SelectedImagesModel::cancelAllRunners() {
    cancelSessionRequests();
}

void SelectedImagesModel::cancelAllDecodeRunners() {
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllDecodeRunners();
    }
    else {
        _decodeManager->cancelThumbnailRequests(_requestNamespace);
        _decodeManager->cancelViewerRequests(_requestNamespace);
    }
    _failedImageDecodeRequests.clear();
    _failedImageDecodeRetryAttempts.clear();
}

bool SelectedImagesModel::preserveViewStateOnReset() const {
    return _preserveViewStateOnReset;
}

void SelectedImagesModel::cancelAllDecodeViewerRunners() {
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllDecodeViewerRunners();
    }
    else {
        _decodeManager->cancelViewerRequests(_requestNamespace);
    }
    for (auto it = _failedImageDecodeRequests.begin();
         it != _failedImageDecodeRequests.end();) {
        if (it.value().viewerRequest) {
            _failedImageDecodeRetryAttempts.remove(it.key());
            it = _failedImageDecodeRequests.erase(it);
        } else {
            ++it;
        }
    }
}

void SelectedImagesModel::cancelAllDecodeViewerRunnersForViewerClose() {
    cancelAllDecodeViewerRunners();
}

void SelectedImagesModel::prepareToClose() {
    if (_isClosing) {
        return;
    }
    _isClosing = true;
    _imageInfoRequestTimer.stop();
    _failedImageWorkRetryTimer.stop();
    if (_ownsDecodeManager) {
        _decodeManager->prepareToClose();
    }
    else {
        cancelSessionRequests();
    }
}

void SelectedImagesModel::readImagesInfo(
    const QList<QString> &paths, bool isFromEmbeddedView) {
    _decodeManager->readImagesInfo(paths, isFromEmbeddedView, 0, false,
                                   _requestNamespace);
}

void SelectedImagesModel::cancelSessionRequests() {
    if (_requestNamespace.isEmpty()) {
        _decodeManager->cancelAllRunners();
    }
    else {
        _decodeManager->cancelRequests(_requestNamespace);
    }
}

bool SelectedImagesModel::acceptsRequestNamespace(
    const QString &requestNamespace) const {
    return requestNamespace == _requestNamespace;
}

void SelectedImagesModel::configureImageFile(ImageFile *item) const {
    if (item) {
        item->setImageProviderName(_thumbnailProviderName);
    }
}

const ImageFile *SelectedImagesModel::itemForImageId(const QString &imageId) const {
    return _imageIdToItem.value(imageId, nullptr);
}

QImage SelectedImagesModel::viewerForImageId(const QString &imageId) const {
    return _viewerImageCache.viewerImageForId(imageId);
}

QImage SelectedImagesModel::fullSizeViewerForImageId(
    const QString &imageId) const {
    return _viewerImageCache.fullSizeImageForId(imageId);
}

bool SelectedImagesModel::containsPath(const QString &path) const {
    return _pathToItem.contains(path);
}

int SelectedImagesModel::selectedCount() const {
    int count = 0;
    for (const ImageFile *item : _items) {
        count += item->isSelected() ? 1 : 0;
    }
    return count;
}

int SelectedImagesModel::totalPathCount() const {
    return _totalPathCount;
}

int SelectedImagesModel::unavailableCount() const {
    return _unavailableCount;
}

bool SelectedImagesModel::isIndexSelected(int index) const {
    return index >= 0 && index < _items.size() && _items[index]->isSelected();
}

void SelectedImagesModel::setSelection(int index, bool selected) {
    if (index < 0 || index >= _items.size() || _items[index]->isSelected() == selected) {
        return;
    }
    _items[index]->setIsSelected(selected);
    emit dataChanged(this->index(index, 0), this->index(index, 0), {FileListModel::SelectedRole});
    emit panelSelectionChanged();
}

void SelectedImagesModel::toggleSelection(int index) {
    if (index >= 0 && index < _items.size()) {
        setSelection(index, !_items[index]->isSelected());
    }
}

void SelectedImagesModel::replaceSelection(int index) {
    if (index < 0 || index >= _items.size()) {
        return;
    }
    bool changed = false;
    for (int i = 0; i < _items.size(); i++) {
        const bool selected = i == index;
        if (_items[i]->isSelected() != selected) {
            _items[i]->setIsSelected(selected);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::selectRange(int anchorIndex, int targetIndex, bool preserveExisting) {
    if (anchorIndex < 0 || targetIndex < 0 ||
        anchorIndex >= _items.size() || targetIndex >= _items.size()) {
        return;
    }

    const int first = qMin(anchorIndex, targetIndex);
    const int last = qMax(anchorIndex, targetIndex);
    bool changed = false;
    for (int i = 0; i < _items.size(); i++) {
        const bool selected = (i >= first && i <= last) || (preserveExisting && _items[i]->isSelected());
        if (_items[i]->isSelected() != selected) {
            _items[i]->setIsSelected(selected);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::clearSelection() {
    bool changed = false;
    for (ImageFile *item : _items) {
        if (item->isSelected()) {
            item->setIsSelected(false);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::setAllSelection(bool selected) {
    bool changed = false;
    for (ImageFile *item : _items) {
        if (item->isSelected() != selected) {
            item->setIsSelected(selected);
            changed = true;
        }
    }
    if (changed) {
        emitSelectionDataChanged();
    }
}

void SelectedImagesModel::removeFromCollection(int index) {
    if (index >= 0 && index < _items.size()) {
        _selectionSourceModel->setPathSelection(_items[index]->fullPath(), false);
    }
}

int SelectedImagesModel::mapToSourceRow(int viewRow) const {
    return viewRow >= 0 && viewRow < _items.size() ? viewRow : -1;
}

int SelectedImagesModel::mapFromSourceRow(int sourceRow) const {
    return mapToSourceRow(sourceRow);
}

QVariantList SelectedImagesModel::mapToSourceRows(const QVariantList &viewRows) const {
    QVariantList result;
    result.reserve(viewRows.size());
    for (const QVariant &value : viewRows) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0 && row < _items.size()) {
            result.append(row);
        }
    }
    return result;
}

QVariantList SelectedImagesModel::viewerPrefetchSourceRows(
    int currentViewRow, int imageCount) const {
    QVariantList result;
    if (currentViewRow < 0 || currentViewRow >= _items.size() ||
        imageCount <= 0) {
        return result;
    }

    result.reserve(qMin(imageCount, _items.size()));
    bool hitStart = false;
    bool hitEnd = false;
    for (int counter = 0;
         result.size() < imageCount && !(hitStart && hitEnd);
         ++counter) {
        const int row = counter % 2 == 0
            ? currentViewRow + counter / 2
            : currentViewRow - (counter + 1) / 2;
        if (row < 0) {
            hitStart = true;
        }
        if (row >= _items.size()) {
            hitEnd = true;
        }
        if (row >= 0 && row < _items.size() && _items.at(row)->isImage()) {
            result.append(row);
        }
    }
    return result;
}

QVariantList SelectedImagesModel::sourceRowsForViewRange(int anchorViewRow, int targetViewRow,
                                                         bool includeTarget) const {
    QVariantList result;
    if (anchorViewRow < 0 || targetViewRow < 0 ||
        anchorViewRow >= _items.size() || targetViewRow >= _items.size()) {
        return result;
    }

    int first = qMin(anchorViewRow, targetViewRow);
    int last = qMax(anchorViewRow, targetViewRow);
    if (!includeTarget) {
        if (targetViewRow > anchorViewRow) {
            last--;
        }
        else if (targetViewRow < anchorViewRow) {
            first++;
        }
        else {
            return result;
        }
    }
    for (int row = first; row <= last; row++) {
        result.append(row);
    }
    return result;
}

void SelectedImagesModel::beginSelectionPreview() {
    if (_selectionPreviewActive) {
        return;
    }
    _selectionPreviewActive = true;
    _selectionPreviewSnapshot.clear();
    for (int i = 0; i < _items.size(); i++) {
        if (_items[i]->isSelected()) {
            _selectionPreviewSnapshot.insert(_items[i]->fullPath());
        }
    }
}

void SelectedImagesModel::previewSelectionIndexes(const QVariantList &indexes, int mode) {
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    QSet<int> affectedIndexes;
    for (const QVariant &value : indexes) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0 && row < _items.size()) {
            affectedIndexes.insert(row);
        }
    }

    constexpr int Add = 0;
    constexpr int Deselect = 1;
    constexpr int Replace = 2;
    constexpr int Toggle = 3;
    for (int i = 0; i < _items.size(); i++) {
        bool selected = _selectionPreviewSnapshot.contains(
            _items[i]->fullPath());
        if (mode == Replace) {
            selected = affectedIndexes.contains(i);
        }
        else if (mode == Toggle && affectedIndexes.contains(i)) {
            selected = !selected;
        }
        else if (mode == Add && affectedIndexes.contains(i)) {
            selected = true;
        }
        else if (mode == Deselect && affectedIndexes.contains(i)) {
            selected = false;
        }
        _items[i]->setIsSelected(selected);
    }
    emitSelectionDataChanged();
}

void SelectedImagesModel::commitSelectionPreview(const QString &description) {
    Q_UNUSED(description)
    if (!_selectionPreviewActive) {
        return;
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
}

void SelectedImagesModel::cancelSelectionPreview() {
    if (!_selectionPreviewActive) {
        return;
    }
    for (int i = 0; i < _items.size(); i++) {
        _items[i]->setIsSelected(_selectionPreviewSnapshot.contains(
            _items[i]->fullPath()));
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    emitSelectionDataChanged();
}

QVariantList SelectedImagesModel::dragIndexesForIndex(int index, bool singleItemOnly) const {
    QVariantList result;
    if (index < 0 || index >= _items.size()) {
        return result;
    }

    if (!singleItemOnly && _items[index]->isSelected()) {
        for (int i = 0; i < _items.size(); i++) {
            if (_items[i]->isSelected()) {
                result.append(i);
            }
        }
    }
    else {
        result.append(index);
    }
    return result;
}

QVariantList SelectedImagesModel::dragUrlsForIndex(int index, bool singleItemOnly) const {
    QVariantList result;
    for (const QVariant &indexValue : dragIndexesForIndex(index, singleItemOnly)) {
        const int itemIndex = indexValue.toInt();
        if (itemIndex >= 0 && itemIndex < _items.size()) {
            result.append(QUrl::fromLocalFile(_items[itemIndex]->fullPath()));
        }
    }
    return result;
}

QVariantMap SelectedImagesModel::dragPreviewItemsForIndex(int index, int limit,
                                                          bool singleItemOnly) const {
    QVariantMap result;
    QVariantList items;
    const QVariantList indexes = dragIndexesForIndex(index, singleItemOnly);
    const int cappedCount = limit < 0 ? indexes.size() : qMin(limit, indexes.size());
    for (int i = 0; i < cappedCount; i++) {
        const int itemIndex = indexes[i].toInt();
        if (itemIndex < 0 || itemIndex >= _items.size()) {
            continue;
        }
        const ImageFile *item = _items[itemIndex];
        items.append(QVariantMap{
            {"index", itemIndex},
            {"text", item->text()},
            {"imageIdUrl", item->imageIdUrl()},
            {"iconPath", item->iconPath()},
            {"isImage", true},
            {"isFolder", false},
            {"fullPath", item->fullPath()},
        });
    }
    result["items"] = items;
    result["totalCount"] = indexes.size();
    result["remainingCount"] = qMax(0, indexes.size() - items.size());
    return result;
}

void SelectedImagesModel::requestViewer(int index, int width, int height) {
    requestViewerInOrder(index, {}, width, height);
}

void SelectedImagesModel::requestViewerInOrder(
    int index, const QVariantList &orderedSourceRows,
    int width, int height) {
    if (index < 0 || index >= _items.size()) {
        return;
    }

    ImageFile *item = _items[index];
    _currentViewerPath = item->fullPath();
    _currentViewerRequestSize = QSize(width, height);
    _hasCurrentViewerRequest = true;
    if (!item->imageIdUrl().isEmpty()) {
        emit viewerImageIdUrlChanged(item->imageIdUrl(), 0);
    }

    QList<ImageFile *> prioritizedItems;
    prioritizedItems.reserve(orderedSourceRows.size());
    for (const QVariant &rowValue : orderedSourceRows) {
        bool ok = false;
        const int sourceRow = rowValue.toInt(&ok);
        if (!ok || sourceRow < 0 || sourceRow >= _items.size()) {
            continue;
        }
        ImageFile *prioritizedItem = _items.at(sourceRow);
        if (prioritizedItem && prioritizedItem->isImage() &&
            !prioritizedItems.contains(prioritizedItem)) {
            prioritizedItems.append(prioritizedItem);
        }
    }
    if (!prioritizedItems.isEmpty() &&
        prioritizedItems.first() != item) {
        prioritizedItems.removeAll(item);
        prioritizedItems.prepend(item);
    }

    const ViewerImageCache::RequestPlan requestPlan =
        prioritizedItems.isEmpty()
        ? _viewerImageCache.planRequest(
              _items, index, QSize(width, height))
        : _viewerImageCache.planRequest(
              prioritizedItems, 0, QSize(width, height),
              prioritizedItems.size());
    for (const auto &[url, level] : requestPlan.cachedImages) {
        emit viewerImageIdUrlChanged(url, level);
    }
    decodeImages(requestPlan.decodeRequests);
}

void SelectedImagesModel::requestViewerAt(
    int index, int width, int height) {
    if (index < 0 || index >= _items.size() ||
        !_items.at(index)->isImage()) {
        return;
    }

    const QList<ImageFile *> targetItems{_items.at(index)};
    const ViewerImageCache::RequestPlan requestPlan =
        _viewerImageCache.planRequest(
            targetItems, 0, QSize(width, height), 1);
    decodeImages(requestPlan.decodeRequests);
}

QString SelectedImagesModel::bestViewerImageUrlForIndex(int index) const {
    if (index < 0 || index >= _items.size()) {
        return {};
    }
    const QSize viewerSize =
        _hasCurrentViewerRequest && _currentViewerRequestSize.isValid()
        ? _currentViewerRequestSize : QSize();
    const auto sources =
        _viewerImageCache.imageSources(_items[index], viewerSize);
    return sources.isEmpty() ? QString() : sources.constLast().first;
}

QString SelectedImagesModel::preparedViewerImageUrlForIndex(
    int index, int width, int height) const {
    if (index < 0 || index >= _items.size()) {
        return {};
    }

    const bool fitRequest = width > 0 && height > 0;
    const int requiredLevel = fitRequest ? 1 : 2;
    const auto sources = _viewerImageCache.imageSources(
        _items.at(index), QSize(width, height));
    for (auto it = sources.crbegin(); it != sources.crend(); ++it) {
        if (it->second == requiredLevel) {
            return it->first;
        }
    }
    return {};
}

QSize SelectedImagesModel::viewerImageOriginalSizeForIndex(int index) const {
    return index >= 0 && index < _items.size() && _items.at(index)
        ? _items.at(index)->fullSize() : QSize();
}

QColor SelectedImagesModel::selectionGroupColorForIndex(int index) const {
    return index >= 0 && index < _items.size()
        ? _items[index]->selectionGroupColor()
        : QColor();
}

void SelectedImagesModel::syncFromPersistentSelection(
    bool preserveTransientState) {
    const QList<PersistentSelectionCache::SelectedFile> selectedFiles =
        PersistentSelectionCache::selectedFilesByAdditionDate();
    const QString activeGroupId = _selectionSourceModel->activeSelectionGroupId();
    _activeGroupId = activeGroupId;

    QSet<QString> retainedPaths;
    QSet<QString> nextActiveGroupPaths;
    QHash<QString, QDateTime> nextSelectionAddedAt;
    QList<ImageFile *> nextItems;
    nextItems.reserve(selectedFiles.size());
    int nextTotalPathCount = 0;
    for (const PersistentSelectionCache::SelectedFile &selectedFile : selectedFiles) {
        const QString normalizedPath =
            QFileInfo(selectedFile.path).absoluteFilePath();
        nextSelectionAddedAt.insert(normalizedPath, selectedFile.addedAt);
        const bool belongsToActiveGroup =
            selectedFile.groupId == activeGroupId;
        if (belongsToActiveGroup) {
            nextTotalPathCount++;
            nextActiveGroupPaths.insert(normalizedPath);
        }

        const QFileInfo fileInfo(selectedFile.path);
        if (!fileInfo.isFile() || !ThumbnailLoader::isFormatSupported(fileInfo.fileName())) {
            continue;
        }

        const QString path = fileInfo.absoluteFilePath();
        retainedPaths.insert(path);
        ImageFile *item = _pathToItem.value(path, nullptr);
        if (!item) {
            item = new ImageFile(this);
            configureImageFile(item);
            item->setFolderPath(fileInfo.absolutePath());
            item->setFileName(fileInfo.fileName());
            item->setIsImage(true);
            item->setIsFolder(false);
            item->setIconPath("qrc:/ZoinGallery/resources/ImageIcon.svg");
            item->setInfo(ImageInfo{
                .path = path,
                .lastModified = fileInfo.lastModified(),
                .fileSize = fileInfo.size(),
            });
            _pathToItem.insert(path, item);
        }
        item->setSelectionGroupId(selectedFile.groupId);
        item->setSelectionGroupColor(
            QColor(PersistentSelectionCache::colorForGroup(selectedFile.groupId)));
        if (belongsToActiveGroup) {
            nextItems.append(item);
        }
    }

    QList<ImageFile *> removedItems;
    for (auto it = _pathToItem.begin(); it != _pathToItem.end();) {
        if (!retainedPaths.contains(it.key())) {
            ImageFile *item = it.value();
            const QString imageId = item->imageIdUrl().section('/', -1);
            _imageIdToItem.remove(imageId);
            _providerImageStore->remove(imageId);
            _viewerImageCache.remove(item->fullPath());
            removedItems.append(item);
            it = _pathToItem.erase(it);
        }
        else {
            ++it;
        }
    }

    if (!preserveTransientState) {
        _selectionPreviewActive = false;
        _selectionPreviewSnapshot.clear();
    }
    _activeGroupPaths = nextActiveGroupPaths;
    _selectionAddedAt = std::move(nextSelectionAddedAt);
    _totalPathCount = nextTotalPathCount;
    _unavailableCount = qMax(0, nextTotalPathCount - nextItems.size());
    const bool previousPreserveState = _preserveViewStateOnReset;
    _preserveViewStateOnReset = preserveTransientState;
    beginResetModel();
    _items = nextItems;
    for (ImageFile *item : std::as_const(_pathToItem)) {
        item->setIndex(-1);
    }
    for (int i = 0; i < _items.size(); i++) {
        _items[i]->setIndex(i);
    }
    endResetModel();
    _preserveViewStateOnReset = previousPreserveState;
    if (!_currentViewerPath.isEmpty() &&
        indexForPath(_currentViewerPath) == -1) {
        _currentViewerPath.clear();
        _currentViewerRequestSize = QSize();
        _hasCurrentViewerRequest = false;
        emit viewerReset();
    }
    emit activeGroupContentsChanged();

    for (ImageFile *item : removedItems) {
        item->deleteLater();
    }
    emit panelSelectionChanged();
    bool hasMissingActiveInfo = false;
    for (const ImageFile *item : std::as_const(_items)) {
        if (!item->fullSize().isValid()) {
            hasMissingActiveInfo = true;
            break;
        }
    }
    if (hasMissingActiveInfo) {
        // Let MasonryLayout attach to the model before image metadata starts
        // arriving. Its normal ImageFullSizeRole/TimeToFlushRole path then
        // schedules thumbnail decoding exactly like the main gallery.
        _imageInfoRequestTimer.start();
    }
}

void SelectedImagesModel::syncPathsFromPersistentSelection(
    const QStringList &paths) {
    if (paths.isEmpty()) {
        return;
    }
    if (paths.size() > 1) {
        bool affectsSelectedModel = false;
        for (const QString &changedPath : paths) {
            const QString path = QFileInfo(changedPath).absoluteFilePath();
            PersistentSelectionCache::SelectedFile selectedFile;
            if (_pathToItem.contains(path) ||
                _activeGroupPaths.contains(path) ||
                PersistentSelectionCache::selectedFile(path, selectedFile)) {
                affectsSelectedModel = true;
                break;
            }
        }
        if (affectsSelectedModel) {
            // A single preserving reset avoids rebuilding Masonry once per
            // changed row while retaining every unchanged ImageFile, decoded
            // frame, viewer entry, selection preview and pending decode.
            syncFromPersistentSelection(true);
        }
        return;
    }

    bool contentsChanged = false;
    bool panelSelectionChangedValue = false;
    bool needsImageInfo = false;
    for (const QString &changedPath : paths) {
        const QFileInfo pathInfo(changedPath);
        const QString path = pathInfo.absoluteFilePath();
        const bool wasActive = _activeGroupPaths.contains(path);

        PersistentSelectionCache::SelectedFile selectedFile;
        const bool isSelected =
            PersistentSelectionCache::selectedFile(path, selectedFile);
        const bool isActive =
            isSelected && selectedFile.groupId == _activeGroupId;
        if (isSelected) {
            _selectionAddedAt.insert(path, selectedFile.addedAt);
        }
        else {
            _selectionAddedAt.remove(path);
        }
        if (wasActive != isActive) {
            if (isActive) {
                _activeGroupPaths.insert(path);
            }
            else {
                _activeGroupPaths.remove(path);
            }
            contentsChanged = true;
        }

        const bool isAvailableImage =
            isSelected && pathInfo.isFile() &&
            ThumbnailLoader::isFormatSupported(pathInfo.fileName());
        ImageFile *item = _pathToItem.value(path, nullptr);
        int activeRow = item ? item->index() : -1;
        if (activeRow < 0 || activeRow >= _items.size() ||
            _items.value(activeRow) != item) {
            activeRow = -1;
        }

        if (isAvailableImage && !item) {
            item = new ImageFile(this);
            configureImageFile(item);
            item->setFolderPath(pathInfo.absolutePath());
            item->setFileName(pathInfo.fileName());
            item->setIsImage(true);
            item->setIsFolder(false);
            item->setIconPath(QStringLiteral(
                "qrc:/ZoinGallery/resources/ImageIcon.svg"));
            item->setInfo(ImageInfo{
                .path = path,
                .lastModified = pathInfo.lastModified(),
                .fileSize = pathInfo.size(),
            });
            _pathToItem.insert(path, item);
        }

        if (item && isSelected) {
            item->setSelectionGroupId(selectedFile.groupId);
            item->setSelectionGroupColor(QColor(
                PersistentSelectionCache::colorForGroup(
                    selectedFile.groupId)));
        }

        if (isActive && isAvailableImage && activeRow == -1) {
            int row = 0;
            while (row < _items.size()) {
                const QString existingPath = _items[row]->fullPath();
                const QDateTime existingAddedAt =
                    _selectionAddedAt.value(existingPath);
                if (selectedFile.addedAt != existingAddedAt
                    ? selectedFile.addedAt < existingAddedAt
                    : QString::compare(path, existingPath,
                                       Qt::CaseInsensitive) < 0) {
                    break;
                }
                ++row;
            }
            beginInsertRows(QModelIndex(), row, row);
            _items.insert(row, item);
            for (int i = row; i < _items.size(); ++i) {
                _items[i]->setIndex(i);
            }
            endInsertRows();
            contentsChanged = true;
            panelSelectionChangedValue = true;
            needsImageInfo |= !item->fullSize().isValid();
        }
        else if ((!isActive || !isAvailableImage) && activeRow != -1) {
            beginRemoveRows(QModelIndex(), activeRow, activeRow);
            _items.removeAt(activeRow);
            item->setIndex(-1);
            for (int i = activeRow; i < _items.size(); ++i) {
                _items[i]->setIndex(i);
            }
            endRemoveRows();
            contentsChanged = true;
            panelSelectionChangedValue = true;
        }

        if (item && (!isSelected || !isAvailableImage)) {
            const QString imageId =
                item->imageIdUrl().section(QLatin1Char('/'), -1);
            _imageIdToItem.remove(imageId);
            _providerImageStore->remove(imageId);
            _viewerImageCache.remove(item->fullPath());
            _pathToItem.remove(path);
            item->deleteLater();
        }
    }

    _totalPathCount = _activeGroupPaths.size();
    _unavailableCount = qMax(0, _totalPathCount - _items.size());
    if (!_currentViewerPath.isEmpty() &&
        indexForPath(_currentViewerPath) == -1) {
        _currentViewerPath.clear();
        _currentViewerRequestSize = QSize();
        _hasCurrentViewerRequest = false;
        emit viewerReset();
    }
    if (contentsChanged) {
        emit activeGroupContentsChanged();
    }
    if (panelSelectionChangedValue) {
        emit panelSelectionChanged();
    }
    if (needsImageInfo) {
        _imageInfoRequestTimer.start();
    }
}

void SelectedImagesModel::refreshWatchedImageMetadata(
    const QStringList &paths) {
    QStringList imageInfoPaths;
    QList<int> changedRows;
    for (const QString &changedPath : paths) {
        const QString path = QFileInfo(changedPath).absoluteFilePath();
        ImageFile *item = _pathToItem.value(path, nullptr);
        if (!item) {
            continue;
        }

        const ImageInfo sourceInfo =
            _selectionSourceModel->imageInfoForPath(path);
        if (sourceInfo.path.isEmpty() ||
            (item->lastModified() == sourceInfo.lastModified &&
             item->fileSize() == sourceInfo.fileSize)) {
            continue;
        }

        ImageInfo itemInfo = item->info();
        itemInfo.lastModified = sourceInfo.lastModified;
        itemInfo.fileSize = sourceInfo.fileSize;
        item->setInfo(itemInfo);
        imageInfoPaths.append(path);
        if (item->index() >= 0 && item->index() < _items.size() &&
            _items.at(item->index()) == item) {
            changedRows.append(item->index());
        }
    }

    std::sort(changedRows.begin(), changedRows.end());
    changedRows.erase(std::unique(changedRows.begin(), changedRows.end()),
                      changedRows.end());
    if (!changedRows.isEmpty()) {
        const bool previousPreserveState = _preserveViewStateOnReset;
        _preserveViewStateOnReset = true;
        int spanStart = changedRows.first();
        int previousRow = spanStart;
        for (qsizetype i = 1; i < changedRows.size(); ++i) {
            const int row = changedRows.at(i);
            if (row != previousRow + 1) {
                emit dataChanged(index(spanStart, 0), index(previousRow, 0),
                                 {FileListModel::LastModifiedRole,
                                  FileListModel::FileSizeRole});
                spanStart = row;
            }
            previousRow = row;
        }
        emit dataChanged(index(spanStart, 0), index(previousRow, 0),
                         {FileListModel::LastModifiedRole,
                          FileListModel::FileSizeRole});
        _preserveViewStateOnReset = previousPreserveState;
    }

    imageInfoPaths.removeDuplicates();
    if (!imageInfoPaths.isEmpty()) {
        readImagesInfo(imageInfoPaths, false);
    }
}

void SelectedImagesModel::requestMissingImageInfo() {
    QList<QString> paths;
    for (const ImageFile *item : _items) {
        if (!item->fullSize().isValid()) {
            paths.append(item->fullPath());
        }
    }
    if (!paths.isEmpty()) {
        readImagesInfo(paths, false);
    }
}

ImageFile *SelectedImagesModel::applyImageInfo(const ImageInfo &info) {
    ImageFile *item = _pathToItem.value(QFileInfo(info.path).absoluteFilePath(), nullptr);
    if (!item) {
        return nullptr;
    }
    if (!info.imageSize.isValid()) {
        rememberFailedImageInfo(info);
        return nullptr;
    }
    if (!isCurrentFileVersion(item, info)) {
        return nullptr;
    }

    _failedImageInfoRequests.remove(selectedInfoRetryKey(info));
    _failedImageInfoRetryAttempts.remove(selectedInfoRetryKey(info));
    _failedImageWorkRetryDelayMs = FailedImageWorkRetryInitialMs;

    ImageInfo itemInfo = info;
    if (itemInfo.fileSize < 0) {
        itemInfo.fileSize = item->fileSize();
    }
    if (!itemInfo.lastModified.isValid()) {
        itemInfo.lastModified = item->lastModified();
    }
    item->setInfo(itemInfo);
    item->setFullSize(rotateToOrientation(itemInfo.imageSize, itemInfo.orientation));
    return item;
}

bool SelectedImagesModel::isCurrentFileVersion(
    const ImageFile *item, const ImageInfo &info) const {
    if (!item) {
        return false;
    }
    if (!_selectionSourceModel->imageSourceAccessEnabled()) {
        return true;
    }
    if (info.lastModified.isValid() && item->lastModified().isValid() &&
        info.lastModified != item->lastModified()) {
        return false;
    }
    if (info.fileSize >= 0 && item->fileSize() >= 0 &&
        info.fileSize != item->fileSize()) {
        return false;
    }
    return true;
}

void SelectedImagesModel::refreshCurrentViewerAfterMetadata(ImageFile *item) {
    if (!item || !_hasCurrentViewerRequest ||
        item->fullPath() != _currentViewerPath) {
        return;
    }
    const int itemIndex = indexForPath(item->fullPath());
    if (itemIndex < 0) {
        return;
    }
    const ViewerImageCache::RequestPlan requestPlan =
        _viewerImageCache.planRequest(_items, itemIndex,
                                      _currentViewerRequestSize, 1);
    for (const auto &[url, level] : requestPlan.cachedImages) {
        emit viewerImageIdUrlChanged(url, level);
    }
    if (!requestPlan.decodeRequests.isEmpty()) {
        decodeImages(requestPlan.decodeRequests);
    }
}

void SelectedImagesModel::emitThumbnailInfoFlush() {
    if (_items.isEmpty()) {
        return;
    }
    const QModelIndex flushIndex = index(_items.size() - 1, 0);
    if (flushIndex.isValid()) {
        emit dataChanged(flushIndex, flushIndex,
                         {FileListModel::TimeToFlushRole});
    }
}

void SelectedImagesModel::rememberFailedImageInfo(const ImageInfo &info) {
    ImageFile *item = _pathToItem.value(
        QFileInfo(info.path).absoluteFilePath(), nullptr);
    if (!item || !_selectionSourceModel->imageSourceAccessEnabled()) {
        return;
    }
    ImageInfo sourceRetry = info;
    sourceRetry.lastModified = item->lastModified();
    sourceRetry.fileSize = item->fileSize();
    _failedImageInfoRequests.insert(selectedInfoRetryKey(sourceRetry),
                                    sourceRetry);
    scheduleFailedImageWorkRetry();
}

void SelectedImagesModel::rememberFailedDecodeRequest(
    const ImageDecodeRequest &request) {
    ImageFile *item = _pathToItem.value(
        QFileInfo(request.info.path).absoluteFilePath(), nullptr);
    if (!item || !_selectionSourceModel->imageSourceAccessEnabled() ||
        !isCurrentFileVersion(item, request.info)) {
        return;
    }
    ImageDecodeRequest sourceRetry = request;
    sourceRetry.checkCache = false;
    const QString retryKey = selectedDecodeRetryKey(sourceRetry);
    const auto existingRetry = _failedImageDecodeRequests.constFind(retryKey);
    if (existingRetry != _failedImageDecodeRequests.constEnd()) {
        sourceRetry.highPriority =
            sourceRetry.highPriority || existingRetry->highPriority;
        if (existingRetry->viewerGeneration >
            sourceRetry.viewerGeneration) {
            sourceRetry.viewerGeneration = existingRetry->viewerGeneration;
            sourceRetry.viewerPriorityOrdinal =
                existingRetry->viewerPriorityOrdinal;
        }
        else if (existingRetry->viewerGeneration ==
                     sourceRetry.viewerGeneration &&
                 existingRetry->viewerPriorityOrdinal >= 0 &&
                 (sourceRetry.viewerPriorityOrdinal < 0 ||
                  existingRetry->viewerPriorityOrdinal <
                      sourceRetry.viewerPriorityOrdinal)) {
            sourceRetry.viewerPriorityOrdinal =
                existingRetry->viewerPriorityOrdinal;
        }
    }
    _failedImageDecodeRequests.insert(retryKey, sourceRetry);
    scheduleFailedImageWorkRetry();
}

void SelectedImagesModel::scheduleFailedImageWorkRetry() {
    if (!_selectionSourceModel->imageSourceAccessEnabled() ||
        (_failedImageInfoRequests.isEmpty() &&
         _failedImageDecodeRequests.isEmpty()) ||
        _failedImageWorkRetryTimer.isActive()) {
        return;
    }
    _failedImageWorkRetryTimer.start(_failedImageWorkRetryDelayMs);
    _failedImageWorkRetryDelayMs = qMin(
        _failedImageWorkRetryDelayMs * 2, FailedImageWorkRetryMaxMs);
}

void SelectedImagesModel::retryFailedImageWork() {
    _failedImageWorkRetryTimer.stop();
    if (!_selectionSourceModel->imageSourceAccessEnabled()) {
        return;
    }

    const QHash<QString, ImageInfo> pendingInfo =
        std::exchange(_failedImageInfoRequests, {});
    QStringList infoPaths;
    QList<int> infoRows;
    for (auto pendingIt = pendingInfo.constBegin();
         pendingIt != pendingInfo.constEnd(); ++pendingIt) {
        const QString attemptKey = pendingIt.key();
        const ImageInfo &info = pendingIt.value();
        ImageFile *item = _pathToItem.value(
            QFileInfo(info.path).absoluteFilePath(), nullptr);
        if (!item || !isCurrentFileVersion(item, info)) {
            _failedImageInfoRetryAttempts.remove(attemptKey);
            continue;
        }
        const int attempts =
            _failedImageInfoRetryAttempts.value(attemptKey, 0);
        _failedImageInfoRetryAttempts.insert(
            attemptKey,
            qMin(attempts + 1, FailedImageWorkMaxAttempts));
        infoPaths.append(item->fullPath());
        if (item->index() >= 0 && item->index() < _items.size() &&
            _items.at(item->index()) == item) {
            infoRows.append(item->index());
        }
    }
    infoPaths.removeDuplicates();
    if (!infoPaths.isEmpty()) {
        std::sort(infoRows.begin(), infoRows.end());
        infoRows.erase(std::unique(infoRows.begin(), infoRows.end()),
                       infoRows.end());
        if (!infoRows.isEmpty()) {
            const bool previousPreserveState = _preserveViewStateOnReset;
            _preserveViewStateOnReset = true;
            int spanStart = infoRows.first();
            int previousRow = spanStart;
            for (qsizetype i = 1; i < infoRows.size(); ++i) {
                const int row = infoRows.at(i);
                if (row != previousRow + 1) {
                    emit dataChanged(
                        index(spanStart, 0), index(previousRow, 0),
                        {FileListModel::LastModifiedRole,
                         FileListModel::FileSizeRole});
                    spanStart = row;
                }
                previousRow = row;
            }
            emit dataChanged(
                index(spanStart, 0), index(previousRow, 0),
                {FileListModel::LastModifiedRole,
                 FileListModel::FileSizeRole});
            _preserveViewStateOnReset = previousPreserveState;
        }
        readImagesInfo(infoPaths, false);
    }

    const QHash<QString, ImageDecodeRequest> pendingDecode =
        std::exchange(_failedImageDecodeRequests, {});
    QList<ImageDecodeRequest> decodeRequests;
    for (auto pendingIt = pendingDecode.constBegin();
         pendingIt != pendingDecode.constEnd(); ++pendingIt) {
        const QString attemptKey = pendingIt.key();
        const ImageDecodeRequest &request = pendingIt.value();
        ImageFile *item = _pathToItem.value(
            QFileInfo(request.info.path).absoluteFilePath(), nullptr);
        if (!item || !isCurrentFileVersion(item, request.info)) {
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            continue;
        }
        const int attempts =
            _failedImageDecodeRetryAttempts.value(attemptKey, 0);
        if (request.viewerRequest &&
            !_viewerImageCache.needsDecode(request)) {
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            continue;
        }
        if (!request.viewerRequest &&
            item->imageMatchesSource(request.info) &&
            !item->isCachedThumbnail() &&
            item->image().width() >= request.targetSize.width() &&
            item->image().height() >= request.targetSize.height()) {
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            continue;
        }
        _failedImageDecodeRetryAttempts.insert(
            attemptKey,
            qMin(attempts + 1, FailedImageWorkMaxAttempts));
        decodeRequests.append(request);
    }
    if (!decodeRequests.isEmpty()) {
        decodeImages(decodeRequests);
    }
}

void SelectedImagesModel::onImageInfoAvailable(const ImageInfo &info) {
    ImageFile *item = applyImageInfo(info);
    if (!item) {
        if (info.isLast) {
            emitThumbnailInfoFlush();
        }
        return;
    }
    if (item->index() < 0 || item->index() >= _items.size() ||
        _items[item->index()] != item) {
        return;
    }
    const QModelIndex modelIndex = index(item->index(), 0);
    if (modelIndex.isValid()) {
        QList<int> roles{FileListModel::ImageFullSizeRole};
        if (info.isLast) {
            roles.append(FileListModel::TimeToFlushRole);
        }
        emit dataChanged(modelIndex, modelIndex, roles);
    }
    refreshCurrentViewerAfterMetadata(item);
}

void SelectedImagesModel::onImagesInfoAvailable(const QList<ImageInfo> &results) {
    QList<int> changedRows;
    ImageFile *currentViewerMetadataItem = nullptr;
    bool flushFallback = false;
    for (const ImageInfo &info : results) {
        ImageFile *item = applyImageInfo(info);
        if (item && item->index() >= 0 && item->index() < _items.size() &&
            _items[item->index()] == item) {
            changedRows.append(item->index());
            if (item->fullPath() == _currentViewerPath) {
                currentViewerMetadataItem = item;
            }
        }
        else if (info.isLast) {
            flushFallback = true;
        }
    }
    if (changedRows.isEmpty()) {
        if (flushFallback) {
            emitThumbnailInfoFlush();
        }
        return;
    }

    std::sort(changedRows.begin(), changedRows.end());
    changedRows.erase(std::unique(changedRows.begin(), changedRows.end()),
                      changedRows.end());
    int spanStart = changedRows.first();
    int previousRow = spanStart;
    for (qsizetype i = 1; i < changedRows.size(); ++i) {
        const int row = changedRows.at(i);
        if (row != previousRow + 1) {
            emit dataChanged(index(spanStart, 0), index(previousRow, 0),
                             {FileListModel::ImageFullSizeRole});
            spanStart = row;
        }
        previousRow = row;
    }
    emit dataChanged(index(spanStart, 0), index(previousRow, 0),
                     {FileListModel::ImageFullSizeRole});
    emit dataChanged(index(changedRows.last(), 0),
                     index(changedRows.last(), 0),
                     {FileListModel::TimeToFlushRole});
    refreshCurrentViewerAfterMetadata(currentViewerMetadataItem);
}

void SelectedImagesModel::onImageAvailable(const ImageDecodeRequest &request, const QImage &image,
                                            const DecodedImageInfo &decodedInfo) {
    ImageFile *item = _pathToItem.value(QFileInfo(request.info.path).absoluteFilePath(), nullptr);
    if (!item || !isCurrentFileVersion(item, request.info)) {
        return;
    }
    if (image.isNull()) {
        rememberFailedDecodeRequest(request);
        return;
    }
    if (!decodedInfo.isFromCache) {
        ImageDecodeRequest sourceRetry = request;
        sourceRetry.checkCache = false;
        _failedImageDecodeRequests.remove(
            selectedDecodeRetryKey(sourceRetry));
        _failedImageDecodeRetryAttempts.remove(
            selectedDecodeRetryKey(sourceRetry));
        _failedImageWorkRetryDelayMs = FailedImageWorkRetryInitialMs;
    }
    if (request.viewerRequest) {
        const ViewerImageCache::StoredImage storedImage =
            _viewerImageCache.storeDecodedImage(request, image, decodedInfo);
        if (!storedImage.presentable) {
            return;
        }
        const int itemIndex = indexForPath(item->fullPath());
        if (itemIndex != -1) {
            emit viewerImageCacheChanged(itemIndex);
        }
        if (item->fullPath() == _currentViewerPath) {
            emit viewerImageIdUrlChanged(storedImage.url,
                                         storedImage.level);
        }
        return;
    }
    if (item->imageMatchesSource(request.info) &&
        decodedInfo.isFromCache &&
        (image.width() <= item->image().width() || image.height() <= item->image().height())) {
        return;
    }

    const QString previousId = item->imageIdUrl().section('/', -1);
    if (!previousId.isEmpty()) {
        _imageIdToItem.remove(previousId);
        _providerImageStore->remove(previousId);
    }
    const QString imageId = _imageIdPrefix + QString::number(_lastImageId++);
    item->setImage(image, request.info);
    item->setIsCachedThumbnail(decodedInfo.isFromCache);
    _imageIdToItem.insert(imageId, item);
    _providerImageStore->publish(imageId, image);
    item->setImageId(imageId);

    if (item->index() < 0 || item->index() >= _items.size() ||
        _items[item->index()] != item) {
        return;
    }
    const QModelIndex modelIndex = index(item->index(), 0);
    if (modelIndex.isValid()) {
        emit dataChanged(modelIndex, modelIndex, {FileListModel::ImageIdUrlRole});
    }
}

void SelectedImagesModel::emitSelectionDataChanged() {
    if (!_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(_items.size() - 1, 0), {FileListModel::SelectedRole});
    }
    emit panelSelectionChanged();
}

int SelectedImagesModel::indexForPath(const QString &path) const {
    for (int i = 0; i < _items.size(); ++i) {
        if (_items[i]->fullPath() == path) {
            return i;
        }
    }
    return -1;
}
