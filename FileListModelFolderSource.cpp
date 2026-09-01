#include "FileListModelPrivate.h"

int FileListModel::cd(const QString &path, const QString &itemToSelect) {
    _folderRefreshTimer.stop();
    _folderWatchRetryTimer.stop();
    ++_folderRefreshGeneration;
    _folderRefreshPendingAfterDirectOpen = false;
    _folderRescanAfterInFlight.clear();
    _folderWatchRetryDelayMs = 500;
    _recursiveViewActive = false;
    const QString canceledDirectOpenPath =
        _directOpen.stage != DirectOpenStage::None &&
                !_directOpen.readyEmitted
            ? _directOpen.path : QString();
    const bool hadDirectOpenPath = !_directOpen.path.isEmpty();
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.path.clear();
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    if (hadDirectOpenPath) {
        emit directOpenPathChanged();
    }
    if (!canceledDirectOpenPath.isEmpty()) {
        emit directOpenFailed(canceledDirectOpenPath);
    }

    _root = path;

    beginResetModel();
    cleanupModelBeforeCd();
    int indexToSelect = populateFolderItems(path, itemToSelect);
    loadSelectionStatesForVisibleItems();
    endResetModel();

    startRegularFolderWork();
    configureFolderWatcher();
    scheduleFolderRefresh();

    return indexToSelect;
}
int FileListModel::populateFolderItems(const QString &path, const QString &itemToSelect) {
    int indexToSelect = 0;
    QList<FileInfo> entries;
    if (!folderEntries(path, entries)) {
        return indexToSelect;
    }

    if (path == "Computer") {
        for (const FileInfo &drive : entries) {
            ImageFile *item = new ImageFile(this);
            configureImageFile(item);
            item->setFileName(drive.name);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/ZoinGallery/resources/DriveIcon.svg");
            item->setInfo(ImageInfo{
                .path = item->fullPath(),
                .lastModified = drive.lastModified,
                .fileSize = 0,
            });
            item->setIndex(_items.size());
            _items.append(item);

            if (item->fileName() == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }
    }
    else {
        QList<FileInfo> folders;
        QList<FileInfo> files;
        for (const FileInfo &entry : entries) {
            (entry.isDirectory ? folders : files).append(entry);
        }
        sortFileInfosNaturally(folders);
        sortFileInfosNaturally(files);

        for (const FileInfo &folder : folders) {
            ImageFile *item = new ImageFile(this);
            configureImageFile(item);
            item->setFolderPath(_root);
            item->setFileName(folder.name);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/ZoinGallery/resources/FolderIcon.svg");
            item->setInfo(ImageInfo{
                .path = item->fullPath(),
                .lastModified = folder.lastModified,
                .fileSize = 0,
            });
            item->setIndex(_items.size());
            _items.append(item);

            QString path = item->fullPath();

            _fileToItem.insert(path, item);
            _folderImagePaths.append(path);

            if (item->fileName() == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        for (const FileInfo &file : files) {
            ImageFile *item = createFileItem(_root, file.name, file.lastModified, file.fileSize);
            if (item->isImage()) {
                _imagePaths.append(item->fullPath());
            }
            item->setIndex(_items.size());
            _items.append(item);
        }
    }

    return indexToSelect;
}
bool FileListModel::folderEntries(const QString &path, QList<FileInfo> &entries) {
    if (cacheReadsEnabled(_fileListCacheMode)) {
        FolderInfo cachedFolder;
        if (PersistentFolderCache::retrieveFolder(path, cachedFolder)) {
            entries = cachedFolder.subfiles;
            return true;
        }
    }

    if (!sourceReadsEnabled(_fileListCacheMode)) {
        return false;
    }

    entries = readFolderEntries(path);
    if (cacheWritesEnabled(_fileListCacheMode)) {
        PersistentFolderCache::storeFolder(FolderInfo{path, entries});
    }
    return true;
}

QList<FileInfo> FileListModel::readFolderEntries(const QString &path) const {
    QList<FileInfo> entries;
    if (path == "Computer") {
        const auto drives = QDir::drives();
        entries.reserve(drives.size());
        for (const QFileInfo &drive : drives) {
            QString drivePath = drive.path();
            if (drivePath.endsWith("/") && !drivePath.startsWith("/")) {
                drivePath.chop(1);
            }
            entries.append(FileInfo{
                .name = drivePath,
                .lastModified = drive.lastModified(),
                .fileSize = 0,
                .isDirectory = true,
            });
        }
        return entries;
    }

    QDir dir(path);
    const auto sourceEntries = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System, QDir::NoSort);
    entries.reserve(sourceEntries.size());
    for (const QFileInfo &entry : sourceEntries) {
        entries.append(FileInfo{
            .name = entry.fileName(),
            .lastModified = entry.lastModified(),
            .fileSize = entry.isDir() ? 0 : entry.size(),
            .isDirectory = entry.isDir(),
        });
    }
    return entries;
}

void FileListModel::configureFolderWatcher() {
    const QStringList watchedDirectories = _fileSystemWatcher.directories();
    QString desiredPath;
    QStringList desiredFiles;
    bool desiredPathIsRoot = false;
    const bool watcherEnabled =
        !_isClosing && !_recursiveViewActive && !_root.isEmpty() &&
        sourceReadsEnabled(_fileListCacheMode) &&
        _root != QStringLiteral("Computer");
    if (watcherEnabled) {
        const QFileInfo rootInfo(_root);
        if (rootInfo.isDir() && rootInfo.isReadable()) {
            desiredPath = rootInfo.absoluteFilePath();
            desiredPathIsRoot = true;
#ifdef Q_OS_WIN
            // Qt's Windows backend shares one native directory handle for the
            // directory and its watched files. Adding one representative file
            // upgrades that handle to include LAST_WRITE and SIZE changes for
            // the whole directory, without creating O(number of images)
            // watches (which is especially costly on kqueue-based platforms).
            for (const ImageFile *item : std::as_const(_items)) {
                if (!item->isFolder() && QFileInfo(item->fullPath()).isFile()) {
                    desiredFiles.append(item->fullPath());
                    break;
                }
            }
#endif
        }
        else {
            // QFileSystemWatcher drops a path after it is removed. Keep an
            // eye on the nearest existing ancestor so a rename/recreate can
            // restore the root watch without clearing the current gallery.
            QString candidate = rootInfo.absolutePath();
            QSet<QString> visited;
            while (!candidate.isEmpty()) {
                const QString candidateKey = fileSystemPathKey(candidate);
                if (visited.contains(candidateKey)) {
                    break;
                }
                visited.insert(candidateKey);
                const QFileInfo candidateInfo(candidate);
                if (candidateInfo.isDir() && candidateInfo.isReadable()) {
                    desiredPath = candidateInfo.absoluteFilePath();
                    break;
                }
                const QString parent = candidateInfo.absolutePath();
                if (fileSystemPathKey(parent) == candidateKey) {
                    break;
                }
                candidate = parent;
            }
        }
    }

    const bool directoryAlreadyWatched =
        watchedDirectories.size() == 1 && !desiredPath.isEmpty() &&
        fileSystemPathKey(watchedDirectories.first()) ==
            fileSystemPathKey(desiredPath);
    if (!directoryAlreadyWatched && !watchedDirectories.isEmpty()) {
        _fileSystemWatcher.removePaths(watchedDirectories);
    }
    bool directoryWatchReady = directoryAlreadyWatched;
    if (!directoryAlreadyWatched && !desiredPath.isEmpty()) {
        directoryWatchReady = _fileSystemWatcher.addPath(desiredPath);
        if (!directoryWatchReady) {
            qWarning() << "Could not watch folder for changes" << desiredPath;
        }
    }

    const QStringList watchedFiles = _fileSystemWatcher.files();
    QHash<QString, QString> watchedFilesByKey;
    for (const QString &file : watchedFiles) {
        watchedFilesByKey.insert(fileSystemPathKey(file), file);
    }
    QSet<QString> desiredFileKeys;
    QStringList filesToAdd;
    bool representativeFileWatchReady = desiredFiles.isEmpty();
    for (const QString &file : std::as_const(desiredFiles)) {
        const QString key = fileSystemPathKey(file);
        desiredFileKeys.insert(key);
        if (watchedFilesByKey.contains(key)) {
            representativeFileWatchReady = true;
        }
        else {
            filesToAdd.append(file);
        }
    }
    QStringList filesToRemove;
    for (auto it = watchedFilesByKey.constBegin();
         it != watchedFilesByKey.constEnd(); ++it) {
        if (!desiredFileKeys.contains(it.key())) {
            filesToRemove.append(it.value());
        }
    }
    if (!filesToRemove.isEmpty()) {
        _fileSystemWatcher.removePaths(filesToRemove);
    }
    if (!filesToAdd.isEmpty()) {
        const QStringList failedFiles = _fileSystemWatcher.addPaths(filesToAdd);
        if (!failedFiles.isEmpty()) {
            qWarning() << "Could not watch some files for changes"
                       << failedFiles.size();
        }
        else {
            representativeFileWatchReady = true;
        }
    }

    bool supplementalContentWatchReady = true;
#ifdef Q_OS_LINUX
    supplementalContentWatchReady =
        configureLinuxFolderContentWatcher(
            desiredPathIsRoot && directoryWatchReady
                ? desiredPath : QString());
#endif

    if (watcherEnabled && desiredPathIsRoot && directoryWatchReady &&
        representativeFileWatchReady && supplementalContentWatchReady) {
        _folderWatchRetryTimer.stop();
    }
    else if (watcherEnabled) {
        scheduleFolderWatchRetry();
    }
}

#ifdef Q_OS_LINUX
bool FileListModel::configureLinuxFolderContentWatcher(
    const QString &path) {
    if (path.isEmpty()) {
        clearLinuxFolderContentWatcher();
        return true;
    }
    if (_linuxFolderWatchDescriptor >= 0 &&
        fileSystemPathKey(_linuxFolderWatchPath) ==
            fileSystemPathKey(path)) {
        return true;
    }

    clearLinuxFolderContentWatcher();
    if (_linuxFolderWatchFd < 0) {
        _linuxFolderWatchFd =
            ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (_linuxFolderWatchFd < 0) {
            qWarning() << "Could not initialize supplemental inotify watcher"
                       << QString::fromStdString(
                              std::system_category().message(errno));
            return false;
        }
        _linuxFolderWatchNotifier = new QSocketNotifier(
            _linuxFolderWatchFd, QSocketNotifier::Read, this);
        connect(_linuxFolderWatchNotifier, &QSocketNotifier::activated,
                this, [this]() { readLinuxFolderContentEvents(); });
    }

    constexpr uint32_t contentMask =
        IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB | IN_CREATE | IN_DELETE |
        IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF |
        IN_UNMOUNT;
    _linuxFolderWatchDescriptor = ::inotify_add_watch(
        _linuxFolderWatchFd, QFile::encodeName(path).constData(),
        contentMask);
    if (_linuxFolderWatchDescriptor < 0) {
        qWarning() << "Could not watch folder content with inotify" << path
                   << QString::fromStdString(
                          std::system_category().message(errno));
        return false;
    }
    _linuxFolderWatchPath = path;
    return true;
}

void FileListModel::clearLinuxFolderContentWatcher() {
    if (_linuxFolderWatchFd >= 0 &&
        _linuxFolderWatchDescriptor >= 0) {
        (void)::inotify_rm_watch(_linuxFolderWatchFd,
                                 _linuxFolderWatchDescriptor);
        // inotify queues IN_IGNORED for an explicitly removed watch. Drain it
        // before adding the next root so a recycled watch descriptor cannot
        // make that new watch look as if it had just been removed.
        alignas(inotify_event) char discard[4096];
        for (;;) {
            const ssize_t bytesRead =
                ::read(_linuxFolderWatchFd, discard, sizeof(discard));
            if (bytesRead > 0) {
                continue;
            }
            if (bytesRead < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }
    _linuxFolderWatchDescriptor = -1;
    _linuxFolderWatchPath.clear();
}

void FileListModel::resetLinuxFolderContentWatcher() {
    if (_linuxFolderWatchNotifier) {
        _linuxFolderWatchNotifier->setEnabled(false);
        _linuxFolderWatchNotifier->deleteLater();
        _linuxFolderWatchNotifier = nullptr;
    }
    if (_linuxFolderWatchFd >= 0) {
        (void)::close(_linuxFolderWatchFd);
        _linuxFolderWatchFd = -1;
    }
    _linuxFolderWatchDescriptor = -1;
    _linuxFolderWatchPath.clear();
}

void FileListModel::readLinuxFolderContentEvents() {
    if (_linuxFolderWatchFd < 0) {
        return;
    }

    alignas(inotify_event) char buffer[64 * 1024];
    bool refreshRequested = false;
    bool watchWasRemoved = false;
    bool watcherResetRequired = false;

    for (;;) {
        const ssize_t bytesRead =
            ::read(_linuxFolderWatchFd, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            const int readError = errno;
            if (readError == EINTR) {
                continue;
            }
            if (readError == EAGAIN || readError == EWOULDBLOCK) {
                break;
            }
            qWarning() << "Could not read inotify events"
                       << QString::fromStdString(
                              std::system_category().message(readError));
            watcherResetRequired = true;
            refreshRequested = true;
            break;
        }
        if (bytesRead == 0) {
            qWarning() << "Unexpected end of supplemental inotify event stream";
            watcherResetRequired = true;
            refreshRequested = true;
            break;
        }

        const char *cursor = buffer;
        const char *const end = buffer + bytesRead;
        while (cursor + sizeof(inotify_event) <= end) {
            const auto *event =
                reinterpret_cast<const inotify_event *>(cursor);
            const size_t eventSize = sizeof(inotify_event) + event->len;
            if (cursor + eventSize > end) {
                break;
            }
            if (event->wd == _linuxFolderWatchDescriptor ||
                (event->mask & IN_Q_OVERFLOW)) {
                refreshRequested = true;
            }
            if (event->mask & IN_Q_OVERFLOW) {
                // The overflow can discard the IN_IGNORED event for an
                // automatically removed watch. Recreate the whole inotify
                // instance rather than trusting the cached descriptor.
                watcherResetRequired = true;
            }
            if (event->wd == _linuxFolderWatchDescriptor &&
                (event->mask & (IN_IGNORED | IN_DELETE_SELF |
                                IN_MOVE_SELF | IN_UNMOUNT))) {
                watchWasRemoved = true;
            }
            cursor += eventSize;
        }
    }

    if (watcherResetRequired) {
        resetLinuxFolderContentWatcher();
        configureFolderWatcher();
    }
    else if (watchWasRemoved) {
        clearLinuxFolderContentWatcher();
        configureFolderWatcher();
    }
    if (refreshRequested) {
        scheduleFolderRefresh();
    }
}
#endif

void FileListModel::scheduleFolderRefresh() {
    if (_isClosing || _recursiveViewActive ||
        _root.isEmpty() || _root == QStringLiteral("Computer") ||
        !sourceReadsEnabled(_fileListCacheMode)) {
        return;
    }
    ++_folderRefreshGeneration;
    if (_directOpen.stage != DirectOpenStage::None) {
        _folderRefreshPendingAfterDirectOpen = true;
        _folderRefreshTimer.stop();
        return;
    }
    _folderRefreshTimer.start(FolderRefreshDebounceMs);
}

void FileListModel::scheduleFolderWatchRetry() {
    if (_isClosing || _recursiveViewActive || _root.isEmpty() ||
        _root == QStringLiteral("Computer") ||
        !sourceReadsEnabled(_fileListCacheMode) ||
        _folderWatchRetryTimer.isActive()) {
        return;
    }
    _folderWatchRetryTimer.start(_folderWatchRetryDelayMs);
    _folderWatchRetryDelayMs =
        qMin(_folderWatchRetryDelayMs * 2, FolderWatchRetryMaxMs);
}

void FileListModel::refreshWatchedFolder() {
    if (_isClosing || _recursiveViewActive ||
        _root.isEmpty() || _root == QStringLiteral("Computer") ||
        !sourceReadsEnabled(_fileListCacheMode)) {
        return;
    }

    const QString path = QFileInfo(_root).absoluteFilePath();
    const QString pathKey = fileSystemPathKey(path);
    if (_folderScansInFlight.contains(pathKey)) {
        _folderRescanAfterInFlight.insert(pathKey);
        return;
    }

    if (_directOpen.stage != DirectOpenStage::None) {
        _folderRefreshPendingAfterDirectOpen = true;
        return;
    }

    const quint64 generation = _folderRefreshGeneration;
    _folderRefreshPendingAfterDirectOpen = false;
    _folderScansInFlight.insert(pathKey);
    _folderRescanAfterInFlight.remove(pathKey);
    auto *scanWatcher = new QFutureWatcher<FolderScanResult>(this);
    connect(scanWatcher, &QFutureWatcher<FolderScanResult>::finished,
            this, [this, scanWatcher, generation, pathKey]() {
        const FolderScanResult result = scanWatcher->result();
        scanWatcher->deleteLater();
        _folderScansInFlight.remove(pathKey);
        const bool rescanRequested =
            _folderRescanAfterInFlight.remove(pathKey);
        if (_isClosing || _recursiveViewActive) {
            return;
        }
        const bool isCurrentRoot =
            fileSystemPathKey(result.path) == fileSystemPathKey(_root);
        if (!isCurrentRoot) {
            return;
        }
        if (_directOpen.stage != DirectOpenStage::None) {
            _folderRefreshPendingAfterDirectOpen = true;
            return;
        }
        if (generation != _folderRefreshGeneration || rescanRequested) {
            _folderRefreshTimer.start(FolderRefreshDebounceMs);
            return;
        }
        if (result.status != FolderScanStatus::Success) {
            // Keep the model intact for a transient share, permission, or
            // enumeration failure. A recovery watch plus bounded retries
            // will apply only the next complete snapshot.
            qWarning() << "Ignoring incomplete folder refresh"
                       << result.path << result.errorText;
            configureFolderWatcher();
            scheduleFolderWatchRetry();
            return;
        }
        _folderWatchRetryTimer.stop();
        _folderWatchRetryDelayMs = 500;
        if (cacheWritesEnabled(_fileListCacheMode)) {
            PersistentFolderCache::storeFolder(
                FolderInfo{result.path, result.entries});
        }
        reconcileFolderEntries(result.entries);
        retryFailedImageWork();
        configureFolderWatcher();
    });
    scanWatcher->setFuture(QtConcurrent::run([path]() {
        FolderScanResult result;
        result.path = path;
        const QFileInfo rootInfo(path);
        if (!rootInfo.isDir() || !rootInfo.isReadable()) {
            result.status = FolderScanStatus::RootUnavailable;
            result.errorText = QStringLiteral("Folder is unavailable");
            return result;
        }

        std::error_code scanError;
        std::filesystem::directory_iterator iterator(
            nativeFileSystemPath(path), scanError);
        const std::filesystem::directory_iterator end;
        if (scanError) {
            result.status = FolderScanStatus::EnumerationError;
            result.errorText = QString::fromStdString(
                scanError.message());
            return result;
        }
        while (iterator != end) {
            const std::filesystem::directory_entry sourceEntry = *iterator;
            std::error_code metadataError;
            const std::filesystem::file_status linkStatus =
                sourceEntry.symlink_status(metadataError);
            if (metadataError) {
                result.status = FolderScanStatus::EnumerationError;
                result.errorText = QString::fromStdString(
                    metadataError.message());
                result.entries.clear();
                return result;
            }

            const bool sourceIsSymlink =
                std::filesystem::is_symlink(linkStatus);
            const bool sourceIsDirectory =
                std::filesystem::is_directory(linkStatus);
            const bool sourceIsRegularFile =
                std::filesystem::is_regular_file(linkStatus);
            std::filesystem::file_time_type sourceWriteTime;
            std::uintmax_t sourceFileSize = 0;
            if (!sourceIsSymlink) {
                sourceWriteTime =
                    sourceEntry.last_write_time(metadataError);
                if (!metadataError && sourceIsRegularFile) {
                    sourceFileSize = sourceEntry.file_size(metadataError);
                }
                if (metadataError) {
                    result.status = FolderScanStatus::EnumerationError;
                    result.errorText = QString::fromStdString(
                        metadataError.message());
                    result.entries.clear();
                    return result;
                }
            }

            const QFileInfo entry(qStringFromNativeFileSystemPath(
                sourceEntry.path()));
            const bool entryExists = entry.exists() || entry.isSymLink();
            const bool entryIsDirectory = entry.isDir();
            const QDateTime entryLastModified = entry.lastModified();
            const qint64 entryFileSize =
                entryIsDirectory ? 0 : entry.size();

            std::error_code verificationError;
            const std::filesystem::file_status verifiedStatus =
                sourceEntry.symlink_status(verificationError);
            bool metadataChangedDuringScan = verificationError ||
                verifiedStatus.type() != linkStatus.type() || !entryExists;
            if (!metadataChangedDuringScan && !sourceIsSymlink) {
                const std::filesystem::file_time_type verifiedWriteTime =
                    sourceEntry.last_write_time(verificationError);
                metadataChangedDuringScan = verificationError ||
                    verifiedWriteTime != sourceWriteTime ||
                    entryIsDirectory != sourceIsDirectory ||
                    !entryLastModified.isValid();
                if (!metadataChangedDuringScan && sourceIsRegularFile) {
                    const std::uintmax_t verifiedFileSize =
                        sourceEntry.file_size(verificationError);
                    metadataChangedDuringScan = verificationError ||
                        verifiedFileSize != sourceFileSize ||
                        entryFileSize < 0 ||
                        static_cast<std::uintmax_t>(entryFileSize) !=
                            sourceFileSize;
                }
            }
            if (metadataChangedDuringScan) {
                result.status = FolderScanStatus::EnumerationError;
                result.errorText = QStringLiteral(
                    "Folder entry changed while reading metadata: %1")
                    .arg(entry.absoluteFilePath());
                result.entries.clear();
                return result;
            }
            result.entries.append(FileInfo{
                .name = entry.fileName(),
                .lastModified = entryLastModified,
                .fileSize = entryFileSize,
                .isDirectory = entryIsDirectory,
            });
            iterator.increment(scanError);
            if (scanError) {
                result.status = FolderScanStatus::EnumerationError;
                result.errorText = QString::fromStdString(
                    scanError.message());
                result.entries.clear();
                return result;
            }
        }
        const QFileInfo rootAfterMetadata(path);
        if (!rootAfterMetadata.isDir() || !rootAfterMetadata.isReadable()) {
            result.status = FolderScanStatus::RootUnavailable;
            result.entries.clear();
            result.errorText =
                QStringLiteral("Folder disappeared while reading metadata");
            return result;
        }
        result.status = FolderScanStatus::Success;
        return result;
    }));
}

bool FileListModel::isCurrentFileVersion(
    const ImageFile *item, const ImageInfo &info) const {
    if (!item) {
        return false;
    }
    if (!sourceReadsEnabled(_imageCacheMode)) {
        // In cache-only mode there is deliberately no source fallback. The
        // cached frame remains the best available version even if the folder
        // listing was produced from newer source metadata.
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

void FileListModel::emitThumbnailInfoFlush() {
    for (int row = _items.size() - 1; row >= 0; --row) {
        if (!_items.at(row)->isImage()) {
            continue;
        }
        const QModelIndex flushIndex = index(row, 0);
        if (flushIndex.isValid()) {
            emit dataChanged(flushIndex, flushIndex, {TimeToFlushRole});
        }
        return;
    }
}

void FileListModel::refreshCurrentViewerAfterMetadata(ImageFile *item) {
    if (!item || !_hasCurrentViewerRequest ||
        _currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        return;
    }
    if (_items.at(_currentViewIndex) != item) {
        return;
    }
    const ViewerImageCache::RequestPlan requestPlan =
        _viewerImageCache.planRequest(_items, _currentViewIndex,
                                      _currentViewerRequestSize, 1);
    if (!requestPlan.decodeRequests.isEmpty()) {
        decodeImages(requestPlan.decodeRequests);
    }
}

void FileListModel::rememberFailedImageInfo(const ImageInfo &info) {
    if (!sourceReadsEnabled(_imageCacheMode)) {
        return;
    }
    const auto itemIt = _fileToItem.constFind(info.path);
    if (itemIt == _fileToItem.constEnd() || !itemIt.value()->isImage()) {
        return;
    }
    ImageInfo sourceRetry = info;
    sourceRetry.lastModified = itemIt.value()->lastModified();
    sourceRetry.fileSize = itemIt.value()->fileSize();
    const QString retryKey = infoRetryKey(sourceRetry);
    const auto existingRetry = _failedImageInfoRequests.constFind(retryKey);
    if (existingRetry != _failedImageInfoRequests.constEnd()) {
        sourceRetry.highPriority =
            sourceRetry.highPriority || existingRetry->highPriority;
    }
    _failedImageInfoRequests.insert(retryKey, sourceRetry);
    scheduleFailedImageWorkRetry();
}

void FileListModel::rememberFailedDecodeRequest(
    const ImageDecodeRequest &request) {
    if (!sourceReadsEnabled(_imageCacheMode)) {
        return;
    }
    const auto itemIt = _fileToItem.constFind(request.info.path);
    if (itemIt == _fileToItem.constEnd() || !itemIt.value()->isImage() ||
        !isCurrentFileVersion(itemIt.value(), request.info)) {
        return;
    }
    ImageDecodeRequest sourceRetry = request;
    sourceRetry.checkCache = false;
    const QString retryKey = decodeRetryKey(sourceRetry);
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

void FileListModel::scheduleFailedImageWorkRetry() {
    if (_isClosing || !sourceReadsEnabled(_imageCacheMode) ||
        _failedImageWorkRetryTimer.isActive()) {
        return;
    }
    _failedImageWorkRetryTimer.start(_failedImageWorkRetryDelayMs);
    _failedImageWorkRetryDelayMs = qMin(
        _failedImageWorkRetryDelayMs * 2, FailedImageWorkRetryMaxMs);
}

void FileListModel::retryFailedImageWork() {
    _failedImageWorkRetryTimer.stop();
    if (!sourceReadsEnabled(_imageCacheMode) ||
        (_failedImageInfoRequests.isEmpty() &&
         _failedImageDecodeRequests.isEmpty())) {
        return;
    }

    const QHash<QString, ImageInfo> pendingInfo =
        std::exchange(_failedImageInfoRequests, {});
    QHash<int, QStringList> topLevelInfoPaths;
    QHash<int, QStringList> embeddedInfoPaths;
    QHash<int, QStringList> highTopLevelInfoPaths;
    QHash<int, QStringList> highEmbeddedInfoPaths;
    QList<int> topLevelRows;
    bool abandonDirectPriority = false;
    for (auto pendingIt = pendingInfo.constBegin();
         pendingIt != pendingInfo.constEnd(); ++pendingIt) {
        ImageInfo info = pendingIt.value();
        QString attemptKey = pendingIt.key();
        const auto itemIt = _fileToItem.constFind(info.path);
        if (itemIt == _fileToItem.constEnd() || !itemIt.value()->isImage()) {
            _failedImageInfoRetryAttempts.remove(attemptKey);
            continue;
        }
        const bool activeDirectRetry =
            info.directOpenGeneration && isActiveDirectOpenInfo(info);
        if (info.directOpenGeneration &&
            !activeDirectRetry) {
            if (info.directOpenGeneration != _directOpen.generation ||
                _directOpen.stage != DirectOpenStage::None) {
                _failedImageInfoRetryAttempts.remove(attemptKey);
                continue;
            }
            // A cached direct-open tier may have completed the priority state
            // while its parallel source read failed. Keep the quality retry as
            // ordinary model work for the same still-current generation.
            info.directOpenGeneration = 0;
        }
        const QString effectiveAttemptKey = infoRetryKey(info);
        int attempts = _failedImageInfoRetryAttempts.value(
            attemptKey,
            _failedImageInfoRetryAttempts.value(effectiveAttemptKey, 0));
        if (effectiveAttemptKey != attemptKey) {
            _failedImageInfoRetryAttempts.remove(attemptKey);
            attemptKey = effectiveAttemptKey;
        }
        if (activeDirectRetry &&
            attempts >= FailedImageWorkMaxAttempts) {
            abandonDirectPriority = true;
            _failedImageInfoRetryAttempts.remove(attemptKey);
            info.directOpenGeneration = 0;
            attemptKey = infoRetryKey(info);
            attempts = _failedImageInfoRetryAttempts.value(attemptKey, 0);
        }
        _failedImageInfoRetryAttempts.insert(
            attemptKey,
            qMin(attempts + 1, FailedImageWorkMaxAttempts));
        QHash<int, QStringList> &pathsByGeneration =
            info.isFromEmbeddedView
                ? (info.highPriority ? highEmbeddedInfoPaths
                                     : embeddedInfoPaths)
                : (info.highPriority ? highTopLevelInfoPaths
                                     : topLevelInfoPaths);
        pathsByGeneration[info.directOpenGeneration].append(info.path);
        if (!info.isFromEmbeddedView &&
            !info.directOpenGeneration &&
            !itemIt.value()->imageFileParent()) {
            topLevelRows.append(itemIt.value()->index());
        }
    }

    if (!topLevelRows.isEmpty()) {
        std::sort(topLevelRows.begin(), topLevelRows.end());
        topLevelRows.erase(
            std::unique(topLevelRows.begin(), topLevelRows.end()),
            topLevelRows.end());
        const bool previousPreserveState = _preserveViewStateOnReset;
        _preserveViewStateOnReset = true;
        int spanStart = topLevelRows.first();
        int previousRow = spanStart;
        for (qsizetype i = 1; i < topLevelRows.size(); ++i) {
            const int row = topLevelRows.at(i);
            if (row != previousRow + 1) {
                emit dataChanged(index(spanStart, 0),
                                 index(previousRow, 0),
                                 {LastModifiedRole, FileSizeRole});
                spanStart = row;
            }
            previousRow = row;
        }
        emit dataChanged(index(spanStart, 0), index(previousRow, 0),
                         {LastModifiedRole, FileSizeRole});
        _preserveViewStateOnReset = previousPreserveState;
    }
    for (auto it = topLevelInfoPaths.constBegin();
         it != topLevelInfoPaths.constEnd(); ++it) {
        QStringList paths = it.value();
        paths.removeDuplicates();
        readImagesInfo(paths, false, it.key());
    }
    for (auto it = embeddedInfoPaths.constBegin();
         it != embeddedInfoPaths.constEnd(); ++it) {
        QStringList paths = it.value();
        paths.removeDuplicates();
        readImagesInfo(paths, true, it.key());
    }
    for (auto it = highTopLevelInfoPaths.constBegin();
         it != highTopLevelInfoPaths.constEnd(); ++it) {
        QStringList paths = it.value();
        paths.removeDuplicates();
        readImagesInfo(paths, false, it.key(), true);
    }
    for (auto it = highEmbeddedInfoPaths.constBegin();
         it != highEmbeddedInfoPaths.constEnd(); ++it) {
        QStringList paths = it.value();
        paths.removeDuplicates();
        readImagesInfo(paths, true, it.key(), true);
    }

    const QHash<QString, ImageDecodeRequest> pendingDecode =
        std::exchange(_failedImageDecodeRequests, {});
    QList<ImageDecodeRequest> decodeRetries;
    for (auto pendingIt = pendingDecode.constBegin();
         pendingIt != pendingDecode.constEnd(); ++pendingIt) {
        ImageDecodeRequest request = pendingIt.value();
        QString attemptKey = pendingIt.key();
        const auto itemIt = _fileToItem.constFind(request.info.path);
        if (itemIt == _fileToItem.constEnd() ||
            !itemIt.value()->isImage() ||
            !isCurrentFileVersion(itemIt.value(), request.info)) {
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            continue;
        }
        const bool activeDirectRetry =
            request.info.directOpenGeneration &&
            isActiveDirectOpenRequest(request);
        if (request.info.directOpenGeneration &&
            !activeDirectRetry) {
            if (request.info.directOpenGeneration !=
                    _directOpen.generation ||
                _directOpen.stage != DirectOpenStage::None) {
                _failedImageDecodeRetryAttempts.remove(attemptKey);
                continue;
            }
            request.info.directOpenGeneration = 0;
        }
        const QString effectiveAttemptKey = decodeRetryKey(request);
        int attempts = _failedImageDecodeRetryAttempts.value(
            attemptKey,
            _failedImageDecodeRetryAttempts.value(effectiveAttemptKey, 0));
        if (effectiveAttemptKey != attemptKey) {
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            attemptKey = effectiveAttemptKey;
        }
        if (activeDirectRetry &&
            attempts >= FailedImageWorkMaxAttempts) {
            abandonDirectPriority = true;
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            request.info.directOpenGeneration = 0;
            attemptKey = decodeRetryKey(request);
            attempts = _failedImageDecodeRetryAttempts.value(attemptKey, 0);
        }
        if (request.viewerRequest) {
            if (!_viewerImageCache.needsDecode(request)) {
                _failedImageDecodeRetryAttempts.remove(attemptKey);
                continue;
            }
        }
        else if (itemIt.value()->imageMatchesSource(request.info) &&
                 !itemIt.value()->isCachedThumbnail() &&
                 itemIt.value()->image().width() >=
                     request.targetSize.width() &&
                 itemIt.value()->image().height() >=
                     request.targetSize.height()) {
            _failedImageDecodeRetryAttempts.remove(attemptKey);
            continue;
        }
        _failedImageDecodeRetryAttempts.insert(
            attemptKey,
            qMin(attempts + 1, FailedImageWorkMaxAttempts));
        decodeRetries.append(request);
    }
    if (!decodeRetries.isEmpty()) {
        decodeImages(decodeRetries);
    }
    if (abandonDirectPriority &&
        _directOpen.stage != DirectOpenStage::None) {
        qWarning() << "Giving up direct-open priority after repeated source "
                      "failures"
                   << _directOpen.path;
        finishDirectOpenPriorityWork();
    }
}
