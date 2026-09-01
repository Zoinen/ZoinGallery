#include "FileListModelPrivate.h"

void FileListModel::setFolderViewImageSize(int width, int height) {
    if (_folderViewImageSize.width() != width || _folderViewImageSize.height() != height) {
        _folderViewImageSize = QSize(width, height);
        qDebug() << "ZZ TARGET SIZE CHANGED" << _folderViewImageSize;
    }
}

void FileListModel::setFolderViewImageCount(int count) {

}

void FileListModel::startScanner() {
    _decodeManager->scan(_root, _requestNamespace);
}

bool FileListModel::runningTasksDebug() const {
    return _decodeManager->runningTasksDebug();
}

void FileListModel::setRunningTasksDebug(bool isRunningTasksDebug) {
    if (runningTasksDebug() == isRunningTasksDebug) {
        return;
    }
    _decodeManager->setRunningTasksDebug(isRunningTasksDebug);
    emit runningTasksDebugChanged();
}
int FileListModel::imageCacheMode() const {
    return static_cast<int>(_imageCacheMode);
}

void FileListModel::setImageCacheMode(int mode) {
    const CacheUsageMode newMode = cacheUsageModeFromInt(mode);
    if (_imageCacheMode == newMode) {
        return;
    }

    cancelSessionRequests();
    _imageCacheMode = newMode;
    _decodeManager->setImageCacheMode(newMode);
    QSettings().setValue(ImageCacheModeSettingsKey, static_cast<int>(newMode));
    emit imageCacheModeChanged();
    reloadPanelForCacheModeChange();
}

int FileListModel::fileListCacheMode() const {
    return static_cast<int>(_fileListCacheMode);
}

void FileListModel::setFileListCacheMode(int mode) {
    const CacheUsageMode newMode = cacheUsageModeFromInt(mode);
    if (_fileListCacheMode == newMode) {
        return;
    }

    cancelSessionRequests();
    _fileListCacheMode = newMode;
    _decodeManager->setFileListCacheMode(newMode);
    QSettings().setValue(FileListCacheModeSettingsKey, static_cast<int>(newMode));
    emit fileListCacheModeChanged();
    reloadPanelForCacheModeChange();
}

bool FileListModel::imageSourceAccessEnabled() const {
    return sourceReadsEnabled(_imageCacheMode);
}

bool FileListModel::fileListSourceAccessEnabled() const {
    return sourceReadsEnabled(_fileListCacheMode);
}

qint64 FileListModel::imageCacheSize() const {
    return _imageCacheSize;
}

QString FileListModel::imageCacheLocation() const {
    return PersistentImageCache::cacheLocation();
}

qint64 FileListModel::fileListCacheSize() const {
    return _fileListCacheSize;
}

QString FileListModel::fileListCacheLocation() const {
    return PersistentFolderCache::cacheLocation();
}

void FileListModel::clearImageCache() {
    cancelAllRunners();
    PersistentImageCache::clear();
    reloadPanelForCacheModeChange();
    refreshCacheInfo();
}

void FileListModel::clearFileListCache() {
    cancelAllRunners();
    PersistentFolderCache::clear();
    reloadPanelForCacheModeChange();
    refreshCacheInfo();
}

void FileListModel::refreshCacheInfo() {
    const qint64 imageSize = PersistentImageCache::cacheSize();
    const qint64 fileListSize = PersistentFolderCache::cacheSize();
    if (_imageCacheSize == imageSize && _fileListCacheSize == fileListSize) {
        return;
    }
    _imageCacheSize = imageSize;
    _fileListCacheSize = fileListSize;
    emit cacheInfoChanged();
}

QString FileListModel::itemNameToPreserve() const {
    if (_currentViewIndex >= 0 && _currentViewIndex < _items.size()) {
        return _items.at(_currentViewIndex)->fileName();
    }
    return _items.isEmpty() ? QString() : _items.first()->fileName();
}

void FileListModel::reloadPanelForCacheModeChange() {
    if (_root.isEmpty() || _isClosing) {
        return;
    }
    const QString itemToSelect = itemNameToPreserve();
    const int sourceIndex = cd(_root, itemToSelect);
    emit panelReloaded(sourceIndex);
}

void FileListModel::dumpCurrentImage() {
    if (_currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        qDebug() << "No valid current image to dump";
        return;
    }

    ImageFile *currentItem = _items.at(_currentViewIndex);
    QString imagePath = currentItem->fullPath();

    // Try to get full size viewer image first, fall back to regular viewer image
    QImage imageToSave;
    imageToSave =
        _viewerImageCache.entryForPath(imagePath, true).image;
    if (imageToSave.isNull()) {
        imageToSave =
            _viewerImageCache.entryForPath(imagePath, false).image;
    }

    if (imageToSave.isNull()) {
        qDebug() << "No viewer image available for current index";
        return;
    }

    // Get Pictures folder path
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir picturesDir(picturesPath);
    if (!picturesDir.exists()) {
        picturesDir.mkpath(".");
    }

    // Create filename with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString filename = QFileInfo(imagePath).baseName() + "_" + timestamp + ".png";
    QString savePath = picturesDir.filePath(filename);

    // Save the image
    if (imageToSave.save(savePath, "PNG")) {
        qDebug() << "Image saved to" << savePath;
    } else {
        qDebug() << "Failed to save image to" << savePath;
    }
}
