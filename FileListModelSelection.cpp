#include "FileListModelPrivate.h"

void FileListModel::beginSelectionPreview() {
    if (_selectionPreviewActive) {
        return;
    }

    _selectionPreviewActive = true;
    _selectionPreviewSnapshot.clear();
    for (ImageFile *item : _items) {
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        if (!_selectionPreviewSnapshot.contains(containerKey)) {
            _selectionPreviewSnapshot.insert(
                containerKey, _selectionStates[containerKey].selectedGroups);
        }
    }
}

void FileListModel::previewSelectionRange(int anchorIndex, int targetIndex, bool selected, bool includeTarget) {
    if (anchorIndex < 0 || anchorIndex >= _items.size() || targetIndex < 0 || targetIndex >= _items.size()) {
        return;
    }
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedGroups = it.value();
    }
    syncVisibleItemSelection();

    int first = qMin(anchorIndex, targetIndex);
    int last = qMax(anchorIndex, targetIndex);
    if (!includeTarget) {
        if (targetIndex > anchorIndex) {
            last = targetIndex - 1;
        }
        else if (targetIndex < anchorIndex) {
            first = targetIndex + 1;
        }
        else {
            emitSelectionDataChanged();
            return;
        }
    }

    QList<int> indexes;
    for (int i = first; i <= last; i++) {
        indexes.append(i);
    }
    mutateSelectionForIndexes(indexes, selected);
    emitSelectionDataChanged();
}

void FileListModel::previewSelectionIndexes(const QVariantList &indexes, int mode) {
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedGroups = it.value();
    }
    syncVisibleItemSelection();

    QList<int> intIndexes;
    intIndexes.reserve(indexes.size());
    for (const QVariant &indexValue : indexes) {
        bool ok = false;
        const int index_ = indexValue.toInt(&ok);
        if (ok) {
            intIndexes.append(index_);
        }
    }

    if (mode == SelectionPreviewReplace) {
        for (int i = 0; i < _items.size(); i++) {
            setSelectionInState(i, false);
        }
        mutateSelectionForIndexes(intIndexes, true);
    }
    else if (mode == SelectionPreviewToggle) {
        for (int index_ : intIndexes) {
            if (index_ < 0 || index_ >= _items.size()) {
                continue;
            }

            const ImageFile *item = _items[index_];
            const QString containerKey = selectionContainerForItem(item);
            const QString itemKey = selectionItemKey(item);
            const bool wasSelected = _selectionPreviewSnapshot.value(containerKey).contains(itemKey);
            setSelectionInState(index_, !wasSelected);
        }
    }
    else {
        mutateSelectionForIndexes(intIndexes, mode != SelectionPreviewDeselect);
    }
    emitSelectionDataChanged();
}

void FileListModel::commitSelectionPreview(const QString &description) {
    if (!_selectionPreviewActive) {
        return;
    }

    QSet<QString> changedContainers;
    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        if (_selectionStates[it.key()].selectedGroups != it.value()) {
            changedContainers.insert(it.key());
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, description, _selectionPreviewSnapshot[containerKey]);
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::cancelSelectionPreview() {
    if (!_selectionPreviewActive) {
        return;
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedGroups = it.value();
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    syncVisibleItemSelection();
    emitSelectionDataChanged();
}

QVariantList FileListModel::selectionHistoryForIndex(int index_) const {
    QVariantList result;
    const QString containerKey = selectionContainerForIndex(index_);
    const auto stateIt = _selectionStates.constFind(containerKey);
    if (stateIt == _selectionStates.constEnd()) {
        return result;
    }

    const auto &history = stateIt->history;
    for (int i = 0; i < history.size(); i++) {
        QVariantMap row;
        row["index"] = i;
        row["description"] = history[i].description;
        row["timestamp"] = history[i].timestamp.toString("yyyy-MM-dd hh:mm:ss");
        row["selectedCount"] = history[i].selectedCount;
        row["current"] = i == stateIt->historyIndex;
        result.append(row);
    }
    return result;
}

int FileListModel::selectionHistoryIndexForIndex(int index_) const {
    const QString containerKey = selectionContainerForIndex(index_);
    const auto stateIt = _selectionStates.constFind(containerKey);
    return stateIt == _selectionStates.constEnd() ? -1 : stateIt->historyIndex;
}

QString FileListModel::selectionContainerForIndex(int index_) const {
    if (index_ >= 0 && index_ < _items.size()) {
        return selectionContainerForItem(_items[index_]);
    }
    return PersistentSelectionCache::normalizeContainerKey(_root);
}

void FileListModel::selectionHistoryBack(int index_) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    const int historyIndex = _selectionStates[containerKey].historyIndex;
    if (historyIndex > 0) {
        applySelectionHistoryState(containerKey, historyIndex - 1);
    }
}

void FileListModel::selectionHistoryForward(int index_) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    const int historyIndex = _selectionStates[containerKey].historyIndex;
    if (historyIndex >= 0 && historyIndex < _selectionStates[containerKey].history.size() - 1) {
        applySelectionHistoryState(containerKey, historyIndex + 1);
    }
}

void FileListModel::jumpSelectionHistory(int index_, int historyIndex) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    applySelectionHistoryState(containerKey, historyIndex);
}

QVariantList FileListModel::selectionGroups() const {
    QVariantList result;
    const QString activeGroupId = PersistentSelectionCache::activeSelectionGroupId();
    const QList<PersistentSelectionCache::SelectionGroup> groups =
        PersistentSelectionCache::selectionGroups();
    const QHash<QString, int> storedCounts =
        PersistentSelectionCache::selectedCountsByGroup();
    result.reserve(groups.size());
    for (const auto &group : groups) {
        const int storedCount = storedCounts.value(group.id);
        const int availableCount =
            _availableSelectionCounts.value(group.id);
        result.append(QVariantMap{
            {QStringLiteral("id"), group.id},
            {QStringLiteral("name"), group.name},
            {QStringLiteral("color"), QColor(group.color)},
            {QStringLiteral("count"), availableCount},
            {QStringLiteral("storedCount"), storedCount},
            {QStringLiteral("unavailableCount"),
             qMax(0, storedCount - availableCount)},
            {QStringLiteral("active"), group.id == activeGroupId},
            {QStringLiteral("isDefault"), group.isDefault},
        });
    }
    return result;
}

QString FileListModel::activeSelectionGroupId() const {
    return PersistentSelectionCache::activeSelectionGroupId();
}

QString FileListModel::activeSelectionGroupName() const {
    const QString activeGroupId = activeSelectionGroupId();
    for (const auto &group : PersistentSelectionCache::selectionGroups()) {
        if (group.id == activeGroupId) {
            return group.name;
        }
    }
    return QStringLiteral("Yellow");
}

QColor FileListModel::activeSelectionGroupColor() const {
    return QColor(PersistentSelectionCache::colorForGroup(activeSelectionGroupId()));
}

int FileListModel::totalSelectedCount() const {
    int count = 0;
    for (auto it = _availableSelectionCounts.constBegin();
         it != _availableSelectionCounts.constEnd(); ++it) {
        count += it.value();
    }
    return count;
}

bool FileListModel::canAddSelectionGroup() const {
    return PersistentSelectionCache::selectionGroups().size() < 8;
}

QString FileListModel::addSelectionGroup() {
    const QString groupId = PersistentSelectionCache::addSelectionGroup();
    if (!groupId.isEmpty()) {
        emit selectionGroupsChanged();
        emit activeSelectionGroupChanged();
    }
    return groupId;
}

void FileListModel::activateSelectionGroup(const QString &groupId) {
    if (PersistentSelectionCache::setActiveSelectionGroupId(groupId)) {
        emit selectionGroupsChanged();
        emit activeSelectionGroupChanged();
    }
}

bool FileListModel::renameSelectionGroup(const QString &groupId, const QString &name) {
    if (!PersistentSelectionCache::renameSelectionGroup(groupId, name)) {
        return false;
    }
    emit selectionGroupsChanged();
    if (groupId == activeSelectionGroupId()) {
        emit activeSelectionGroupChanged();
    }
    return true;
}

bool FileListModel::removeSelectionGroup(const QString &groupId) {
    const bool wasActive = groupId == activeSelectionGroupId();
    if (!PersistentSelectionCache::removeSelectionGroup(groupId)) {
        return false;
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _selectionStates.clear();
    loadSelectionStatesForVisibleItems();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
    if (wasActive) {
        emit activeSelectionGroupChanged();
    }
    return true;
}

int FileListModel::copyActiveSelectionGroupPaths() const {
    const QString activeGroupId =
        PersistentSelectionCache::activeSelectionGroupId();
    QStringList paths;
    const auto selectedFiles =
        PersistentSelectionCache::selectedFilesByAdditionDate();
    paths.reserve(selectedFiles.size());
    for (const auto &selectedFile : selectedFiles) {
        if (selectedFile.groupId == activeGroupId) {
            paths.append(selectedFile.path);
        }
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || paths.isEmpty()) {
        return 0;
    }
    clipboard->setText(paths.join(QLatin1Char('\n')));
    return paths.size();
}

bool FileListModel::canUndoSelectionGroupMove() const {
    return _lastSelectionGroupMove.isValid();
}

QVariantMap FileListModel::selectionGroupMoveError(
    const QString &title, const QString &message,
    const QString &skipPath) const {
    return {
        {QStringLiteral("success"), false},
        {QStringLiteral("title"), title},
        {QStringLiteral("message"), message},
        {QStringLiteral("skippable"), !skipPath.isEmpty()},
        {QStringLiteral("skipPath"), skipPath},
    };
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolder() {
    return moveActiveSelectionGroupToCurrentFolderImpl({}, false);
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolderSkipping(
    const QStringList &skippedPaths) {
    return moveActiveSelectionGroupToCurrentFolderImpl(skippedPaths, false);
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolderSkippingAll(
    const QStringList &skippedPaths) {
    return moveActiveSelectionGroupToCurrentFolderImpl(skippedPaths, true);
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolderImpl(
    const QStringList &skippedPaths, bool skipAllItemErrors) {
    if (_root.isEmpty() || _root == QStringLiteral("Computer")) {
        return selectionGroupMoveError(
            QStringLiteral("Choose a folder"),
            QStringLiteral("The current address is not a filesystem folder. "
                           "Open the destination folder and try again."));
    }

    const QString groupId = activeSelectionGroupId();
    PersistentSelectionCache::SelectionGroup activeGroup;
    bool foundGroup = false;
    for (const auto &group : PersistentSelectionCache::selectionGroups()) {
        if (group.id == groupId) {
            activeGroup = group;
            foundGroup = true;
            break;
        }
    }
    if (!foundGroup) {
        return selectionGroupMoveError(
            QStringLiteral("Selection group not found"),
            QStringLiteral("The active selection group no longer exists."));
    }
    const QString folderName = activeGroup.name.trimmed();
    const bool invalidWindowsName =
        folderName.endsWith(QLatin1Char('.')) ||
        folderName.endsWith(QLatin1Char(' ')) ||
        folderName.contains(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")));
    if (folderName.isEmpty() || folderName == QStringLiteral(".") ||
        folderName == QStringLiteral("..") || invalidWindowsName) {
        return selectionGroupMoveError(
            QStringLiteral("Invalid folder name"),
            QStringLiteral("“%1” cannot be used as a folder name. Rename the "
                           "selection group and try again.").arg(activeGroup.name));
    }

    QDir currentDirectory(_root);
    const QFileInfo currentDirectoryInfo(currentDirectory.absolutePath());
    if (!currentDirectoryInfo.exists() || !currentDirectoryInfo.isDir()) {
        return selectionGroupMoveError(
            QStringLiteral("Destination unavailable"),
            QStringLiteral("The current folder “%1” cannot be found.")
                .arg(QDir::toNativeSeparators(_root)));
    }

    const QString destinationFolder =
        currentDirectory.absoluteFilePath(folderName);
    if (QFileInfo::exists(destinationFolder)) {
        return selectionGroupMoveError(
            QStringLiteral("Folder already exists"),
            QStringLiteral("“%1” already exists in the current folder. Rename "
                           "the selection group or remove the existing folder, "
                           "then retry.").arg(folderName));
    }

    QList<PersistentSelectionCache::SelectedFile> selectedFiles;
    for (const auto &selectedFile :
         PersistentSelectionCache::selectedFilesByAdditionDate()) {
        if (selectedFile.groupId == groupId) {
            selectedFiles.append(selectedFile);
        }
    }
    if (selectedFiles.isEmpty()) {
        return selectionGroupMoveError(
            QStringLiteral("Selection group is empty"),
            QStringLiteral("There are no images to move."));
    }

    QSet<QString> normalizedSkippedPaths;
    for (const QString &skippedPath : skippedPaths) {
        if (!skippedPath.isEmpty()) {
            normalizedSkippedPaths.insert(
                QFileInfo(skippedPath).absoluteFilePath());
        }
    }
    QList<PersistentSelectionCache::SelectedFile> candidateFiles;
    candidateFiles.reserve(selectedFiles.size());
    for (const auto &selectedFile : std::as_const(selectedFiles)) {
        if (!normalizedSkippedPaths.contains(
                QFileInfo(selectedFile.path).absoluteFilePath())) {
            candidateFiles.append(selectedFile);
        }
    }
    if (candidateFiles.isEmpty()) {
        return selectionGroupMoveError(
            QStringLiteral("No images left to move"),
            QStringLiteral("Every image in this group was skipped. Cancel and "
                           "start the move again to retry skipped images."));
    }

    QList<PersistentSelectionCache::SelectedFile> filesToMove;
    filesToMove.reserve(candidateFiles.size());
    QHash<QString, QString> originalToMovedPath;
    QSet<QString> destinationNames;
    for (const auto &selectedFile : std::as_const(candidateFiles)) {
        const QFileInfo sourceInfo(selectedFile.path);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            if (skipAllItemErrors) {
                continue;
            }
            return selectionGroupMoveError(
                QStringLiteral("Source image not found"),
                QStringLiteral("“%1” cannot be found. Retry after restoring "
                               "it, or skip it and move the remaining images.")
                    .arg(QDir::toNativeSeparators(selectedFile.path)),
                selectedFile.path);
        }
        const QString destinationNameKey =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
            sourceInfo.fileName().toCaseFolded();
#else
            sourceInfo.fileName();
#endif
        if (destinationNames.contains(destinationNameKey)) {
            if (skipAllItemErrors) {
                continue;
            }
            return selectionGroupMoveError(
                QStringLiteral("Duplicate image names"),
                QStringLiteral("More than one selected image is named “%1”. "
                               "Skip this copy or cancel the move.")
                    .arg(sourceInfo.fileName()),
                selectedFile.path);
        }
        destinationNames.insert(destinationNameKey);
        originalToMovedPath.insert(
            selectedFile.path,
            QDir(destinationFolder).absoluteFilePath(sourceInfo.fileName()));
        filesToMove.append(selectedFile);
    }
    if (filesToMove.isEmpty()) {
        return selectionGroupMoveError(
            QStringLiteral("No images could be moved"),
            QStringLiteral("All remaining images were unavailable or "
                           "conflicted with another selected image. Nothing "
                           "was changed."));
    }

    if (!currentDirectory.mkdir(folderName)) {
        return selectionGroupMoveError(
            QStringLiteral("Could not create folder"),
            QStringLiteral("The folder “%1” could not be created in “%2”. "
                           "Check permissions and available space, then retry.")
                .arg(folderName,
                     QDir::toNativeSeparators(currentDirectory.absolutePath())));
    }

    QStringList movedOriginalPaths;
    auto rollbackMoves = [&]() {
        for (auto it = movedOriginalPaths.crbegin();
             it != movedOriginalPaths.crend(); ++it) {
            QFile::rename(originalToMovedPath.value(*it), *it);
        }
        QDir(currentDirectory.absolutePath()).rmdir(folderName);
    };

    for (const auto &selectedFile : std::as_const(filesToMove)) {
        const QString movedPath = originalToMovedPath.value(selectedFile.path);
        if (QFileInfo::exists(movedPath) ||
            !QFile::rename(selectedFile.path, movedPath)) {
            if (skipAllItemErrors) {
                originalToMovedPath.remove(selectedFile.path);
                continue;
            }
            rollbackMoves();
            return selectionGroupMoveError(
                QStringLiteral("Could not move image"),
                QStringLiteral("“%1” could not be moved. The operation was "
                               "cancelled and previously moved images were "
                               "returned to their original locations. Retry or "
                               "skip this image.")
                    .arg(QDir::toNativeSeparators(selectedFile.path)),
                selectedFile.path);
        }
        movedOriginalPaths.append(selectedFile.path);
    }
    if (movedOriginalPaths.isEmpty()) {
        QDir(currentDirectory.absolutePath()).rmdir(folderName);
        return selectionGroupMoveError(
            QStringLiteral("No images could be moved"),
            QStringLiteral("Every remaining image failed to move. Nothing "
                           "was changed."));
    }

    const bool removedGroup = activeGroup.isDefault
        ? PersistentSelectionCache::removeSelectionGroupForMove(groupId)
        : PersistentSelectionCache::removeSelectionGroup(groupId);
    if (!removedGroup) {
        rollbackMoves();
        return selectionGroupMoveError(
            QStringLiteral("Could not remove selection group"),
            QStringLiteral("The images were returned to their original "
                           "locations. Retry the operation."));
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _selectionStates.clear();
    loadSelectionStatesForVisibleItems();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
    emit activeSelectionGroupChanged();

    const bool previouslyUndoable = canUndoSelectionGroupMove();
    _lastSelectionGroupMove = {
        .group = activeGroup,
        .selectedFiles = selectedFiles,
        .destinationFolder = destinationFolder,
        .originalToMovedPath = originalToMovedPath,
    };
    if (!previouslyUndoable) {
        emit canUndoSelectionGroupMoveChanged();
    }

    QSet<QString> invalidatedFolders{_root};
    for (const auto &selectedFile : std::as_const(selectedFiles)) {
        invalidatedFolders.insert(QFileInfo(selectedFile.path).absolutePath());
    }
    PersistentFolderCache::removeFolders(invalidatedFolders.values());
    scheduleFolderRefresh();
    const qsizetype movedCount = movedOriginalPaths.size();
    const qsizetype skippedCount = selectedFiles.size() - movedCount;
    return {
        {QStringLiteral("success"), true},
        {QStringLiteral("message"),
         QStringLiteral("Moved %1 image%2 to “%3”.%4")
             .arg(movedCount)
             .arg(movedCount == 1 ? QString() : QStringLiteral("s"))
             .arg(folderName)
             .arg(skippedCount == 0
                      ? QString()
                      : QStringLiteral(" Skipped %1 image%2.")
                            .arg(skippedCount)
                            .arg(skippedCount == 1
                                     ? QString() : QStringLiteral("s")))},
    };
}

QVariantMap FileListModel::undoLastSelectionGroupMove() {
    if (!_lastSelectionGroupMove.isValid()) {
        return selectionGroupMoveError(
            QStringLiteral("Nothing to undo"),
            QStringLiteral("There is no group move to undo."));
    }

    const SelectionGroupMoveAction action = _lastSelectionGroupMove;
    for (auto it = action.originalToMovedPath.constBegin();
         it != action.originalToMovedPath.constEnd(); ++it) {
        if (!QFileInfo::exists(it.value())) {
            return selectionGroupMoveError(
                QStringLiteral("Moved image not found"),
                QStringLiteral("“%1” cannot be found. Nothing was changed.")
                    .arg(QDir::toNativeSeparators(it.value())));
        }
        if (QFileInfo::exists(it.key())) {
            return selectionGroupMoveError(
                QStringLiteral("Original path is occupied"),
                QStringLiteral("“%1” already exists. Remove or rename that "
                               "file, then retry undo.")
                    .arg(QDir::toNativeSeparators(it.key())));
        }
        if (!QFileInfo(it.key()).dir().exists()) {
            return selectionGroupMoveError(
                QStringLiteral("Original folder not found"),
                QStringLiteral("The original folder for “%1” no longer "
                               "exists. Recreate it, then retry undo.")
                    .arg(QDir::toNativeSeparators(it.key())));
        }
    }

    QStringList restoredOriginalPaths;
    auto rollbackUndo = [&]() {
        for (auto it = restoredOriginalPaths.crbegin();
             it != restoredOriginalPaths.crend(); ++it) {
            QFile::rename(*it, action.originalToMovedPath.value(*it));
        }
    };
    for (auto it = action.originalToMovedPath.constBegin();
         it != action.originalToMovedPath.constEnd(); ++it) {
        if (!QFile::rename(it.value(), it.key())) {
            rollbackUndo();
            return selectionGroupMoveError(
                QStringLiteral("Could not restore image"),
                QStringLiteral("“%1” could not be returned to its original "
                               "location. No selection changes were made.")
                    .arg(QDir::toNativeSeparators(it.value())));
        }
        restoredOriginalPaths.append(it.key());
    }

    if (!PersistentSelectionCache::restoreSelectionGroup(
            action.group, action.selectedFiles, true)) {
        rollbackUndo();
        return selectionGroupMoveError(
            QStringLiteral("Could not restore selection group"),
            QStringLiteral("The files were returned to the moved folder. A "
                           "group with the same name or color may already exist."));
    }

    QDir().rmdir(action.destinationFolder);
    _lastSelectionGroupMove.clear();
    emit canUndoSelectionGroupMoveChanged();
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _selectionStates.clear();
    loadSelectionStatesForVisibleItems();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
    emit activeSelectionGroupChanged();
    QSet<QString> invalidatedFolders{_root, action.destinationFolder};
    for (const auto &selectedFile : action.selectedFiles) {
        invalidatedFolders.insert(QFileInfo(selectedFile.path).absolutePath());
    }
    PersistentFolderCache::removeFolders(invalidatedFolders.values());
    scheduleFolderRefresh();

    return {
        {QStringLiteral("success"), true},
        {QStringLiteral("message"),
         QStringLiteral("Restored “%1” and returned %2 image%3.")
             .arg(action.group.name)
             .arg(action.originalToMovedPath.size())
             .arg(action.originalToMovedPath.size() == 1
                      ? QString() : QStringLiteral("s"))},
    };
}

QString FileListModel::selectionContainerForItem(const ImageFile *item) const {
    if (!item) {
        return PersistentSelectionCache::normalizeContainerKey(_root);
    }
    if (_root == "Computer" && item->folderPath().isEmpty()) {
        return "Computer";
    }
    return PersistentSelectionCache::normalizeContainerKey(item->folderPath());
}

QString FileListModel::selectionItemKey(const ImageFile *item) const {
    return item ? item->fileName() : QString();
}

QString FileListModel::selectionGroupForItem(const ImageFile *item) const {
    if (!item) {
        return QString();
    }
    const QString containerKey = selectionContainerForItem(item);
    const auto stateIt = _selectionStates.constFind(containerKey);
    return stateIt == _selectionStates.constEnd()
        ? QString()
        : stateIt->selectedGroups.value(selectionItemKey(item));
}

void FileListModel::ensureSelectionStateLoaded(const QString &containerKey) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    if (!_selectionStates.contains(normalizedKey)) {
        _selectionStates.insert(normalizedKey, PersistentSelectionCache::retrieveContainer(normalizedKey));
    }
}

void FileListModel::loadSelectionStatesForVisibleItems() {
    for (ImageFile *item : _items) {
        ensureSelectionStateLoaded(selectionContainerForItem(item));
    }
    syncVisibleItemSelection();
}

void FileListModel::syncVisibleItemSelection() {
    for (ImageFile *item : _items) {
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        const QString groupId =
            _selectionStates[containerKey].selectedGroups.value(selectionItemKey(item));
        item->setIsSelected(!groupId.isEmpty());
        item->setSelectionGroupId(groupId);
        item->setSelectionGroupColor(groupId.isEmpty()
            ? QColor()
            : QColor(PersistentSelectionCache::colorForGroup(groupId)));
    }
}

void FileListModel::emitSelectionDataChanged(int firstIndex, int lastIndex,
                                             bool persistentChange) {
    if (!_items.isEmpty()) {
        if (firstIndex < 0 || lastIndex < 0) {
            firstIndex = 0;
            lastIndex = _items.size() - 1;
        }
        firstIndex = qBound(0, firstIndex, _items.size() - 1);
        lastIndex = qBound(0, lastIndex, _items.size() - 1);
        if (firstIndex > lastIndex) {
            std::swap(firstIndex, lastIndex);
        }
        emit dataChanged(index(firstIndex, 0), index(lastIndex, 0),
                         {SelectedRole, SelectionGroupIdRole, SelectionGroupColorRole});
    }
    emit selectionChanged();
    if (persistentChange) {
        const QStringList changedPaths = _pendingSelectionPaths.values();
        _pendingSelectionPaths.clear();
        if (changedPaths.isEmpty() || changedPaths.size() > 32) {
            refreshAvailableSelectionCounts();
        }
        else {
            updateAvailableSelectionCounts(changedPaths);
        }
        emit selectionPathsChanged(changedPaths);
        emit selectionGroupsChanged();
    }
}

void FileListModel::refreshAvailableSelectionCounts() {
    _availableSelectionCounts.clear();
    _availableSelectedPathGroups.clear();
    const auto selectedFiles =
        PersistentSelectionCache::selectedFilesByAdditionDate();
    for (const auto &selectedFile : selectedFiles) {
        const QFileInfo fileInfo(selectedFile.path);
        if (!fileInfo.isFile() ||
            !ThumbnailLoader::isFormatSupported(fileInfo.fileName())) {
            continue;
        }
        const QString path = fileInfo.absoluteFilePath();
        _availableSelectedPathGroups.insert(path, selectedFile.groupId);
        _availableSelectionCounts[selectedFile.groupId]++;
    }
}

void FileListModel::updateAvailableSelectionCounts(
    const QStringList &paths) {
    for (const QString &changedPath : paths) {
        const QFileInfo fileInfo(changedPath);
        const QString path = fileInfo.absoluteFilePath();
        const QString previousGroup =
            _availableSelectedPathGroups.take(path);
        if (!previousGroup.isEmpty()) {
            const int nextCount =
                _availableSelectionCounts.value(previousGroup) - 1;
            if (nextCount > 0) {
                _availableSelectionCounts.insert(previousGroup, nextCount);
            }
            else {
                _availableSelectionCounts.remove(previousGroup);
            }
        }

        PersistentSelectionCache::SelectedFile selectedFile;
        if (!PersistentSelectionCache::selectedFile(path, selectedFile) ||
            !fileInfo.isFile() ||
            !ThumbnailLoader::isFormatSupported(fileInfo.fileName())) {
            continue;
        }
        _availableSelectedPathGroups.insert(path, selectedFile.groupId);
        _availableSelectionCounts[selectedFile.groupId]++;
    }
}

void FileListModel::pushSelectionHistory(const QString &containerKey, const QString &description,
                                         const QHash<QString, QString> &previousSelectedGroups) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    ensureSelectionStateLoaded(normalizedKey);
    auto &state = _selectionStates[normalizedKey];
    QSet<QString> changedNames(previousSelectedGroups.keyBegin(),
                               previousSelectedGroups.keyEnd());
    changedNames.unite(QSet<QString>(state.selectedGroups.keyBegin(),
                                     state.selectedGroups.keyEnd()));
    for (const QString &name : std::as_const(changedNames)) {
        if (previousSelectedGroups.value(name) !=
            state.selectedGroups.value(name)) {
            _pendingSelectionPaths.insert(
                normalizedKey == QStringLiteral("Computer")
                    ? name
                    : QDir(normalizedKey).absoluteFilePath(name));
        }
    }
    PersistentSelectionCache::appendHistoryEntry(
        state, description, previousSelectedGroups);
    PersistentSelectionCache::storeContainer(normalizedKey, state, false);
    _selectionSaveTimer.start();
    emit selectionHistoryChanged();
}

void FileListModel::mutateSelectionForIndexes(const QList<int> &indexes, bool selected) {
    for (int index_ : indexes) {
        setSelectionInState(index_, selected);
    }
}

bool FileListModel::setSelectionInState(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return false;
    }

    ImageFile *item = _items[index_];
    const QString containerKey = selectionContainerForItem(item);
    ensureSelectionStateLoaded(containerKey);
    auto &selectedGroups = _selectionStates[containerKey].selectedGroups;
    const QString itemKey = selectionItemKey(item);
    const QString previousGroup = selectedGroups.value(itemKey);
    const QString nextGroup = selected ? activeSelectionGroupId() : QString();
    if (previousGroup == nextGroup) {
        return false;
    }

    if (selected) {
        selectedGroups.insert(itemKey, nextGroup);
    }
    else {
        selectedGroups.remove(itemKey);
    }
    item->setIsSelected(!nextGroup.isEmpty());
    item->setSelectionGroupId(nextGroup);
    item->setSelectionGroupColor(nextGroup.isEmpty()
        ? QColor()
        : QColor(PersistentSelectionCache::colorForGroup(nextGroup)));
    return true;
}

void FileListModel::applySelectionHistoryState(const QString &containerKey, int historyIndex) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    ensureSelectionStateLoaded(normalizedKey);
    auto &state = _selectionStates[normalizedKey];
    if (historyIndex < 0 || historyIndex >= state.history.size()) {
        return;
    }

    const QHash<QString, QString> previousSelection = state.selectedGroups;
    if (!PersistentSelectionCache::applyHistoryIndex(state, historyIndex)) {
        return;
    }
    QSet<QString> changedNames(previousSelection.keyBegin(),
                               previousSelection.keyEnd());
    changedNames.unite(QSet<QString>(state.selectedGroups.keyBegin(),
                                     state.selectedGroups.keyEnd()));
    for (const QString &name : std::as_const(changedNames)) {
        if (previousSelection.value(name) != state.selectedGroups.value(name)) {
            _pendingSelectionPaths.insert(
                normalizedKey == QStringLiteral("Computer")
                    ? name
                    : QDir(normalizedKey).absoluteFilePath(name));
        }
    }
    PersistentSelectionCache::storeContainer(normalizedKey, state, false);
    _selectionSaveTimer.start();
    syncVisibleItemSelection();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
}

QString FileListModel::sameKindDescription(int index_, bool selected) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return selected ? "Select matching items" : "Deselect matching items";
    }

    ImageFile *item = _items[index_];
    QString target;
    if (selectionContainerForItem(item) == "Computer" && item->folderPath().isEmpty()) {
        target = "drives";
    }
    else if (item->isFolder()) {
        target = "folders";
    }
    else {
        const QString suffix = QFileInfo(item->fileName()).suffix();
        target = suffix.isEmpty() ? "extensionless files" : QString(".%1 files").arg(suffix);
    }
    return QString("%1 %2").arg(selected ? "Select" : "Deselect", target);
}
