#include "LocalFilesystemSource.h"

#include "DecodeManager.h"
#include "FileListModel.h"
#include "GalleryViewModel.h"
#include "ImageFile.h"
#include "ImageModel.h"
#include "ProviderImageStore.h"
#include "SelectedImagesModel.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFileInfo>
#include <QVariantList>

namespace ZoinGallery {

namespace {
QString providerIdPrefix(QString sessionId, const QString &kind) {
    for (QChar &character : sessionId) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            character = QLatin1Char('-');
        }
    }
    return QStringLiteral("zg-%1-%2-").arg(sessionId, kind);
}
}

LocalFilesystemSource::LocalFilesystemSource(
    const QString &sessionId, const QString &thumbnailProviderName,
    const QString &asyncProviderName,
    const QSharedPointer<::ProviderImageStore> &store,
    ::DecodeManager *decodeManager,
    qint64 viewerFitCacheByteBudget,
    qint64 viewerNativeCacheByteBudget,
    QObject *parent)
    : QObject(parent), _sessionId(sessionId) {
    const QString requestPrefix =
        QStringLiteral("zoingallery.local.%1").arg(sessionId);
    _fileListModel = new FileListModel(
        store, decodeManager, requestPrefix + QStringLiteral(".catalog"),
        providerIdPrefix(sessionId, QStringLiteral("catalog")),
        thumbnailProviderName, asyncProviderName,
        viewerFitCacheByteBudget, viewerNativeCacheByteBudget, this);
    _galleryViewModel = new GalleryViewModel(_fileListModel, this);
    _selectedImagesModel = new SelectedImagesModel(
        _fileListModel, store, decodeManager,
        requestPrefix + QStringLiteral(".selection"),
        providerIdPrefix(sessionId, QStringLiteral("selection")),
        thumbnailProviderName, asyncProviderName,
        viewerFitCacheByteBudget, viewerNativeCacheByteBudget, this);
    _imageModel = new ImageModel(_galleryViewModel);

    const auto notifyCatalog = [this] {
        if (!_shutdown) {
            emit catalogChanged();
        }
    };
    connect(_galleryViewModel, &QAbstractItemModel::modelReset,
            this, notifyCatalog);
    connect(_galleryViewModel, &QAbstractItemModel::layoutChanged,
            this, notifyCatalog);
    connect(_galleryViewModel, &QAbstractItemModel::rowsInserted,
            this, [notifyCatalog](const QModelIndex &, int, int) {
                notifyCatalog();
            });
    connect(_galleryViewModel, &QAbstractItemModel::rowsRemoved,
            this, [notifyCatalog](const QModelIndex &, int, int) {
                notifyCatalog();
            });
    connect(_fileListModel, &FileListModel::selectionChanged,
            this, &LocalFilesystemSource::selectionChanged);
    connect(_fileListModel, &FileListModel::viewerImageIdUrlChanged,
            this, [this](const QString &url, int level) {
                const QUrl next(url);
                if (_viewerSource != next || _viewerSourceLevel != level) {
                    _viewerSource = next;
                    _viewerSourceLevel = level;
                    emit viewerSourceChanged();
                }
            });
    connect(_fileListModel, &FileListModel::viewerImageCacheChanged,
            this, [this](int sourceIndex) {
                const int viewIndex = _galleryViewModel
                    ? _galleryViewModel->mapFromSourceRow(sourceIndex) : -1;
                if (viewIndex >= 0) {
                    emit viewerSourceAtChanged(viewIndex);
                }
            });
    connect(_fileListModel, &FileListModel::viewerReset,
            this, &LocalFilesystemSource::clearViewer);
}

QAbstractItemModel *LocalFilesystemSource::model() const {
    return _galleryViewModel;
}

FileListModel *LocalFilesystemSource::fileListModel() const {
    return _fileListModel;
}

GalleryViewModel *LocalFilesystemSource::galleryViewModel() const {
    return _galleryViewModel;
}

SelectedImagesModel *LocalFilesystemSource::selectedImagesModel() const {
    return _selectedImagesModel;
}

ImageModel *LocalFilesystemSource::imageModel() const {
    return _imageModel;
}

QString LocalFilesystemSource::currentPath() const {
    return _currentPath;
}

int LocalFilesystemSource::cd(
    const QString &path, const QString &itemToSelect) {
    if (_shutdown) {
        return -1;
    }
    const int sourceIndex = _fileListModel->cd(path, itemToSelect);
    const QString nextPath = _fileListModel->rootPath();
    if (_currentPath != nextPath) {
        _currentPath = nextPath;
        emit currentPathChanged();
    }
    const int viewIndex = _galleryViewModel->mapFromSourceRow(sourceIndex);
    // Keep the monolithic standalone navigation contract when the requested
    // source row is hidden by selected-only filtering: choose the nearest
    // visible proxy row, rather than jumping unconditionally to the first.
    return viewIndex >= 0
        ? viewIndex
        : _galleryViewModel->nearestVisibleRow(sourceIndex);
}

ImageFile *LocalFilesystemSource::itemAt(int viewIndex) const {
    if (!_galleryViewModel || viewIndex < 0 ||
        viewIndex >= _galleryViewModel->rowCount()) {
        return nullptr;
    }
    return _galleryViewModel->index(viewIndex, 0)
        .data(FileListModel::ImageFileRole).value<ImageFile *>();
}

QString LocalFilesystemSource::entryIdAt(int viewIndex) const {
    return localPathAt(viewIndex);
}

QString LocalFilesystemSource::entryNameAt(int viewIndex) const {
    const ImageFile *item = itemAt(viewIndex);
    return item ? item->fileName() : QString();
}

QString LocalFilesystemSource::localPathAt(int viewIndex) const {
    const ImageFile *item = itemAt(viewIndex);
    return item ? item->fullPath() : QString();
}

bool LocalFilesystemSource::isImageAt(int viewIndex) const {
    const ImageFile *item = itemAt(viewIndex);
    return item && item->isImage();
}

bool LocalFilesystemSource::isDirectoryAt(int viewIndex) const {
    const ImageFile *item = itemAt(viewIndex);
    return item && item->isFolder();
}

int LocalFilesystemSource::sourceIndexAt(int viewIndex) const {
    return _galleryViewModel
        ? _galleryViewModel->mapToSourceRow(viewIndex) : -1;
}

QSize LocalFilesystemSource::imageOriginalSizeAt(int viewIndex) const {
    const ImageFile *item = itemAt(viewIndex);
    return item ? item->fullSize() : QSize();
}

int LocalFilesystemSource::rowForEntryId(const QString &entryId) const {
    if (entryId.isEmpty() || !_galleryViewModel) {
        return -1;
    }
    const QString cleanEntryId = QDir::cleanPath(entryId);
    for (int row = 0; row < _galleryViewModel->rowCount(); ++row) {
        if (QDir::cleanPath(localPathAt(row)) == cleanEntryId) {
            return row;
        }
    }
    return -1;
}

void LocalFilesystemSource::toggleSelection(int viewIndex) {
    const int sourceIndex = sourceIndexAt(viewIndex);
    if (!_shutdown && sourceIndex >= 0) {
        _fileListModel->toggleSelection(sourceIndex);
    }
}

void LocalFilesystemSource::setSelection(int viewIndex, bool selected) {
    const int sourceIndex = sourceIndexAt(viewIndex);
    if (_fileListModel && sourceIndex >= 0) {
        _fileListModel->setSelection(sourceIndex, selected);
    }
}

void LocalFilesystemSource::ensurePreviews() {
    // FileListModel requests metadata as part of cd() and watcher refreshes;
    // MasonryLayout requests only the currently visible decode sizes.
}

void LocalFilesystemSource::requestViewer(
    int viewIndex, const QSize &size) {
    const int sourceIndex = sourceIndexAt(viewIndex);
    if (_shutdown || sourceIndex < 0) {
        return;
    }
    _viewerViewportSize = size;
    const auto immediateSources =
        _fileListModel->viewerImageSourcesForIndex(sourceIndex, size);
    if (!immediateSources.isEmpty() &&
        (_viewerSource != QUrl(immediateSources.constLast().first) ||
         _viewerSourceLevel != immediateSources.constLast().second)) {
        _viewerSource = QUrl(immediateSources.constLast().first);
        _viewerSourceLevel = immediateSources.constLast().second;
        emit viewerSourceChanged();
    }
    _fileListModel->requestViewerInOrder(
        sourceIndex,
        _galleryViewModel->viewerPrefetchSourceRows(viewIndex),
        size.width(), size.height());
}

void LocalFilesystemSource::requestViewerAt(
    int targetViewIndex, const QSize &size) {
    const int targetSourceIndex = sourceIndexAt(targetViewIndex);
    if (_shutdown || targetSourceIndex < 0 ||
        !isImageAt(targetViewIndex)) {
        return;
    }

    _viewerViewportSize = size;
    // Prepare only the transition row. FileListModel treats this as a
    // supplemental request, so the authoritative current row and its
    // ordered predecode sequence stay untouched while a rapid second swipe
    // is waiting for the host cursor round-trip.
    _fileListModel->requestViewerAt(
        targetSourceIndex, size.width(), size.height());
}

void LocalFilesystemSource::clearViewer() {
    if (_fileListModel && !_shutdown) {
        _fileListModel->cancelAllDecodeViewerRunnersForViewerClose();
    }
    if (!_viewerSource.isEmpty()) {
        _viewerSource = QUrl();
        _viewerSourceLevel = -1;
        emit viewerSourceChanged();
    }
    _viewerViewportSize = {};
}

QUrl LocalFilesystemSource::viewerSource() const {
    return _viewerSource;
}

int LocalFilesystemSource::viewerSourceLevel() const {
    return _viewerSourceLevel;
}

QUrl LocalFilesystemSource::viewerSourceAt(int viewIndex) const {
    const auto sources = viewerImageSourcesAt(viewIndex);
    return sources.isEmpty() ? QUrl() : QUrl(sources.constLast().first);
}

QList<QPair<QString, int>> LocalFilesystemSource::viewerImageSourcesAt(
    int viewIndex) const {
    const int sourceIndex = sourceIndexAt(viewIndex);
    return !_fileListModel || sourceIndex < 0
        ? QList<QPair<QString, int>>()
        : _fileListModel->viewerImageSourcesForIndex(
              sourceIndex, _viewerViewportSize);
}

void LocalFilesystemSource::shutdown() {
    if (_shutdown) {
        return;
    }
    clearViewer();
    _shutdown = true;
    _selectedImagesModel->prepareToClose();
    _fileListModel->shutdown();
}

} // namespace ZoinGallery
