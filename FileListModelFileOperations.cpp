#include "FileListModelPrivate.h"

int FileListModel::selectedCount() const {
    int count = 0;
    for (const ImageFile *item : _items) {
        if (item->isSelected()) {
            count++;
        }
    }
    return count;
}
bool FileListModel::isIndexSelected(int index_) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return false;
    }
    return _items[index_]->isSelected();
}
QColor FileListModel::selectionGroupColorForIndex(int index_) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return QColor();
    }
    return _items[index_]->selectionGroupColor();
}

void FileListModel::toggleSelection(int index_) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }
    setSelection(index_, !_items[index_]->isSelected());
}

void FileListModel::setSelection(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }

    const QString containerKey = selectionContainerForItem(_items[index_]);
    ensureSelectionStateLoaded(containerKey);
    const QHash<QString, QString> previousSelection =
        _selectionStates[containerKey].selectedGroups;
    if (!setSelectionInState(index_, selected)) {
        return;
    }

    pushSelectionHistory(containerKey, selected ? QString("Select %1").arg(_items[index_]->fileName())
                                               : QString("Deselect %1").arg(_items[index_]->fileName()),
                         previousSelection);
    emitSelectionDataChanged(index_, index_, true);
}

void FileListModel::setPathSelection(const QString &path, bool selected) {
    const QFileInfo fileInfo(path);
    const QString containerKey = PersistentSelectionCache::normalizeContainerKey(fileInfo.absolutePath());
    ensureSelectionStateLoaded(containerKey);
    auto &state = _selectionStates[containerKey];
    const QHash<QString, QString> previousSelection = state.selectedGroups;
    const QString previousGroup = state.selectedGroups.value(fileInfo.fileName());
    const QString nextGroup = selected ? activeSelectionGroupId() : QString();
    if (previousGroup == nextGroup) {
        return;
    }

    if (selected) {
        state.selectedGroups.insert(fileInfo.fileName(), nextGroup);
    }
    else {
        state.selectedGroups.remove(fileInfo.fileName());
    }
    pushSelectionHistory(containerKey,
                         selected ? QString("Select %1").arg(fileInfo.fileName())
                                  : QString("Deselect %1").arg(fileInfo.fileName()),
                         previousSelection);
    syncVisibleItemSelection();
    emitSelectionDataChanged(-1, -1, true);
}

void FileListModel::invertSelection() {
    QHash<QString, QHash<QString, QString>> previousSelections;
    QSet<QString> changedContainers;

    for (int i = 0; i < _items.size(); i++) {
        const QString containerKey = selectionContainerForItem(_items[i]);
        ensureSelectionStateLoaded(containerKey);
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, _selectionStates[containerKey].selectedGroups);
        }

        if (setSelectionInState(i, !_items[i]->isSelected())) {
            changedContainers.insert(containerKey);
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, "Invert selection", previousSelections[containerKey]);
    }
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::setAllSelection(bool selected) {
    QHash<QString, QHash<QString, QString>> previousSelections;
    QSet<QString> changedContainers;

    for (int i = 0; i < _items.size(); i++) {
        const QString containerKey = selectionContainerForItem(_items[i]);
        ensureSelectionStateLoaded(containerKey);
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, _selectionStates[containerKey].selectedGroups);
        }

        if (setSelectionInState(i, selected)) {
            changedContainers.insert(containerKey);
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, selected ? "Select all" : "Deselect all", previousSelections[containerKey]);
    }
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::setSameKindSelection(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }

    ImageFile *currentItem = _items[index_];
    const QString currentContainer = selectionContainerForItem(currentItem);
    ensureSelectionStateLoaded(currentContainer);
    const QHash<QString, QString> previousSelection =
        _selectionStates[currentContainer].selectedGroups;
    const QString currentSuffix = QFileInfo(currentItem->fileName()).suffix().toLower();
    bool changed = false;

    for (int i = 0; i < _items.size(); i++) {
        ImageFile *item = _items[i];
        if (selectionContainerForItem(item) != currentContainer) {
            continue;
        }

        bool matches = false;
        if (currentContainer == "Computer" && currentItem->folderPath().isEmpty()) {
            matches = item->folderPath().isEmpty();
        }
        else if (currentItem->isFolder()) {
            matches = item->isFolder();
        }
        else if (!item->isFolder()) {
            matches = QFileInfo(item->fileName()).suffix().toLower() == currentSuffix;
        }

        if (matches && setSelectionInState(i, selected)) {
            changed = true;
        }
    }

    if (changed) {
        pushSelectionHistory(currentContainer, sameKindDescription(index_, selected), previousSelection);
        emitSelectionDataChanged(-1, -1, true);
    }
}

QVariantList FileListModel::dragIndexesForIndex(int index_, bool singleItemOnly) const {
    QVariantList result;
    if (index_ < 0 || index_ >= _items.size()) {
        return result;
    }

    if (!singleItemOnly && _items[index_]->isSelected()) {
        for (int i = 0; i < _items.size(); i++) {
            if (_items[i]->isSelected()) {
                result.append(i);
            }
        }
    }
    else {
        result.append(index_);
    }
    return result;
}

QVariantList FileListModel::dragUrlsForIndex(int index_, bool singleItemOnly) const {
    QVariantList result;
    const QVariantList indexes = dragIndexesForIndex(index_, singleItemOnly);
    for (const QVariant &indexValue : indexes) {
        bool ok = false;
        const int itemIndex = indexValue.toInt(&ok);
        if (ok && itemIndex >= 0 && itemIndex < _items.size()) {
            const ImageFile *item = _items[itemIndex];
            QString fullPath = item->fullPath();
            if (_root == "Computer" && item->folderPath().isEmpty() &&
                    !fullPath.endsWith("/") && !fullPath.endsWith("\\")) {
                fullPath += "/";
            }
            result.append(QUrl::fromLocalFile(fullPath));
        }
    }
    return result;
}

QVariantMap FileListModel::dragPreviewItemsForIndex(int index_, int limit, bool singleItemOnly) const {
    QVariantMap result;
    QVariantList items;
    const QVariantList indexes = dragIndexesForIndex(index_, singleItemOnly);
    const int totalCount = indexes.size();
    const int cappedCount = limit < 0 ? totalCount : qMin(limit, totalCount);

    for (int i = 0; i < cappedCount; i++) {
        bool ok = false;
        const int itemIndex = indexes[i].toInt(&ok);
        if (!ok || itemIndex < 0 || itemIndex >= _items.size()) {
            continue;
        }

        const ImageFile *item = _items[itemIndex];
        QVariantMap previewItem;
        previewItem["index"] = itemIndex;
        previewItem["text"] = item->text();
        previewItem["imageIdUrl"] = item->imageIdUrl();
        previewItem["iconPath"] = item->iconPath();
        previewItem["isImage"] = item->isImage();
        previewItem["isFolder"] = item->isFolder();
        previewItem["fullPath"] = item->fullPath();
        items.append(previewItem);
    }

    result["items"] = items;
    result["totalCount"] = totalCount;
    result["remainingCount"] = qMax(0, totalCount - items.size());
    return result;
}

QVariantMap FileListModel::finalizeExternalDrag(
    const QVariantList &urls, int dropAction) {
    const auto action = static_cast<Qt::DropAction>(dropAction);
    if (action != Qt::MoveAction && action != Qt::TargetMoveAction) {
        return {
            {QStringLiteral("success"), true},
            {QStringLiteral("movedCount"), 0},
            {QStringLiteral("failedPaths"), QStringList{}},
        };
    }

    const bool sourceMustDelete = action == Qt::MoveAction;
    QStringList completedPaths;
    QStringList failedPaths;
    QSet<QString> sourceFolders;
    QSet<QString> seenPaths;
    for (const QVariant &urlValue : urls) {
        const QUrl url = urlValue.toUrl();
        if (!url.isLocalFile()) {
            continue;
        }

        const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (path.isEmpty() || seenPaths.contains(path)) {
            continue;
        }
        seenPaths.insert(path);
        const QFileInfo sourceInfo(path);
        sourceFolders.insert(sourceInfo.absolutePath());

        bool completed = !sourceInfo.exists();
        if (!completed && sourceMustDelete) {
            // A MoveAction means the target accepted the data and expects the
            // source to remove its original. TargetMoveAction is different:
            // the target took ownership and the source must not delete it.
            if (sourceInfo.isDir() && !sourceInfo.isSymLink()) {
                if (QDir(path).isRoot()) {
                    qWarning() << "Refusing to remove filesystem root after drag"
                               << path;
                }
                else {
                    completed = QDir(path).removeRecursively();
                }
            }
            else {
                completed = QFile::remove(path);
            }
        }

        if (completed) {
            completedPaths.append(path);
        }
        else if (sourceMustDelete) {
            failedPaths.append(path);
            qWarning() << "Could not remove source after external move" << path;
        }
    }

    removeMovedPathsFromSelection(
        completedPaths, QStringLiteral("Move by drag and drop"));

    if (!sourceFolders.isEmpty()) {
        PersistentFolderCache::removeFolders(sourceFolders.values());
    }
    const QString normalizedRoot =
        PersistentSelectionCache::normalizeContainerKey(_root);
    if (!normalizedRoot.isEmpty() && normalizedRoot != QStringLiteral("Computer") &&
        sourceFolders.contains(normalizedRoot)) {
        scheduleFolderRefresh();
    }

    return {
        {QStringLiteral("success"), failedPaths.isEmpty()},
        {QStringLiteral("movedCount"), completedPaths.size()},
        {QStringLiteral("failedPaths"), failedPaths},
    };
}

void FileListModel::configureNativeDragCursors(QObject *dragSource) {
#ifdef Q_OS_WIN
    if (!dragSource) {
        return;
    }
    QDrag *drag = dragSource->findChild<QDrag *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!drag) {
        qWarning() << "Could not find the active native drag for HiDPI cursors";
        return;
    }

    QScreen *screen = nullptr;
    if (const auto *item = qobject_cast<QQuickItem *>(dragSource)) {
        if (item->window()) {
            screen = item->window()->screen();
        }
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const qreal dpr = qBound(1.0,
        screen ? screen->devicePixelRatio() : 1.0, 4.0);
    const QSizeF previewSize = drag->pixmap().deviceIndependentSize();
    const QPointF hotSpot = drag->hotSpot();
    drag->setDragCursor(
        windowsDragCursorPixmap(dpr, previewSize, hotSpot,
                                Qt::CopyAction),
        Qt::CopyAction);
    drag->setDragCursor(
        windowsDragCursorPixmap(dpr, previewSize, hotSpot,
                                Qt::MoveAction),
        Qt::MoveAction);
    drag->setDragCursor(
        windowsDragCursorPixmap(dpr, previewSize, hotSpot,
                                Qt::IgnoreAction),
        Qt::IgnoreAction);
#else
    Q_UNUSED(dragSource)
#endif
}

bool FileListModel::fileDragActive() const {
    return _fileDragActive;
}

void FileListModel::setFileDragActive(bool active) {
    if (_fileDragActive == active) {
        return;
    }
    _fileDragActive = active;
    emit fileDragActiveChanged();
}

bool FileListModel::eventFilter(QObject *watched, QEvent *event) {
    Q_UNUSED(watched)
    switch (event->type()) {
    case QEvent::DragEnter: {
        const auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        setFileDragActive(dragEvent->mimeData() &&
                          dragEvent->mimeData()->hasUrls());
        break;
    }
    case QEvent::DragMove: {
        const auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (dragEvent->mimeData() && dragEvent->mimeData()->hasUrls()) {
            setFileDragActive(true);
        }
        break;
    }
    case QEvent::DragLeave:
    case QEvent::Drop:
        setFileDragActive(false);
        break;
    default:
        break;
    }
    return false;
}

void FileListModel::removeMovedPathsFromSelection(
    const QStringList &paths, const QString &description) {
    QHash<QString, QHash<QString, QString>> previousSelections;
    QSet<QString> changedContainers;
    for (const QString &path : paths) {
        const QFileInfo pathInfo(path);
        const QString containerKey =
            PersistentSelectionCache::normalizeContainerKey(pathInfo.absolutePath());
        ensureSelectionStateLoaded(containerKey);
        auto &state = _selectionStates[containerKey];
        if (!state.selectedGroups.contains(pathInfo.fileName())) {
            continue;
        }
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, state.selectedGroups);
        }
        state.selectedGroups.remove(pathInfo.fileName());
        changedContainers.insert(containerKey);
    }
    for (const QString &containerKey : std::as_const(changedContainers)) {
        pushSelectionHistory(containerKey, description,
                             previousSelections.value(containerKey));
    }
    if (!changedContainers.isEmpty()) {
        syncVisibleItemSelection();
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::refreshFoldersAfterFileOperation(
    const QSet<QString> &folders) {
    if (folders.isEmpty()) {
        return;
    }
    PersistentFolderCache::removeFolders(folders.values());
    const QString normalizedRoot =
        PersistentSelectionCache::normalizeContainerKey(_root);
    if (!normalizedRoot.isEmpty() && normalizedRoot != QStringLiteral("Computer") &&
        folders.contains(normalizedRoot)) {
        scheduleFolderRefresh();
    }
}

QVariantMap FileListModel::dropUrlsIntoFolder(
    const QVariantList &urls, const QString &destinationFolder,
    int dropAction) {
    const QFileInfo destinationInfo(destinationFolder);
    if (!destinationInfo.exists() || !destinationInfo.isDir()) {
        return fileOperationResult(
            false, QStringLiteral("Folder is unavailable"),
            QStringLiteral("The destination folder could not be found:\n%1")
                .arg(destinationFolder));
    }

    Qt::DropAction action = static_cast<Qt::DropAction>(dropAction);
    if (action != Qt::CopyAction && action != Qt::MoveAction) {
        action = Qt::CopyAction;
    }

    struct PendingItem { QString source; QString destination; };
    QList<PendingItem> pending;
    QSet<QString> seenSources;
    QSet<QString> seenNames;
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif

    for (const QVariant &value : urls) {
        const QUrl url = value.canConvert<QUrl>() ? value.toUrl()
                                                  : QUrl(value.toString());
        if (!url.isLocalFile()) {
            return fileOperationResult(
                false, QStringLiteral("Unsupported item"),
                QStringLiteral("Only local files and folders can be dropped here."));
        }
        const QString source = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (seenSources.contains(source)) {
            continue;
        }
        seenSources.insert(source);
        const QFileInfo sourceInfo(source);
        if ((!sourceInfo.exists() && !sourceInfo.isSymLink()) ||
            (sourceInfo.isDir() && !sourceInfo.isSymLink() && QDir(source).isRoot())) {
            return fileOperationResult(
                false, QStringLiteral("Source is unavailable"),
                QStringLiteral("The source item could not be found or cannot be moved:\n%1")
                    .arg(source));
        }
        const QString name = sourceInfo.fileName();
        bool duplicateName = false;
        for (const QString &seenName : std::as_const(seenNames)) {
            if (seenName.compare(name, sensitivity) == 0) {
                duplicateName = true;
                break;
            }
        }
        if (duplicateName) {
            return fileOperationResult(
                false, QStringLiteral("Duplicate names"),
                QStringLiteral("More than one dropped item is named “%1”.").arg(name));
        }
        seenNames.insert(name);
        const QString target = QDir(destinationFolder).filePath(name);
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            return fileOperationResult(
                false, QStringLiteral("Item already exists"),
                QStringLiteral("An item named “%1” already exists in:\n%2")
                    .arg(name, destinationFolder));
        }
        if (sourceInfo.isDir() && isSameOrChildPath(destinationFolder, source)) {
            return fileOperationResult(
                false, QStringLiteral("Invalid destination"),
                QStringLiteral("A folder cannot be placed inside itself:\n%1")
                    .arg(source));
        }
        pending.append({source, target});
    }

    if (pending.isEmpty()) {
        return fileOperationResult(false, QStringLiteral("Nothing to drop"),
                                   QStringLiteral("No local files or folders were provided."));
    }

    QList<PendingItem> completed;
    QString error;
    for (const PendingItem &item : std::as_const(pending)) {
        const bool ok = action == Qt::MoveAction
            ? movePath(item.source, item.destination, &error)
            : copyPath(item.source, item.destination, &error);
        if (ok) {
            completed.append(item);
            continue;
        }

        QStringList rollbackFailures;
        for (auto it = completed.crbegin(); it != completed.crend(); ++it) {
            bool rolledBack = false;
            if (action == Qt::MoveAction) {
                QString rollbackError;
                rolledBack = movePath(it->destination, it->source, &rollbackError);
            }
            else {
                rolledBack = removePath(it->destination);
            }
            if (!rolledBack) {
                rollbackFailures.append(it->destination);
            }
        }
        if (!rollbackFailures.isEmpty()) {
            error += QStringLiteral("\n\nSome rollback operations also failed:\n%1")
                         .arg(rollbackFailures.join(QLatin1Char('\n')));
        }
        return fileOperationResult(false, QStringLiteral("Drop failed"), error);
    }

    QStringList movedSources;
    QSet<QString> invalidatedFolders{destinationInfo.absoluteFilePath()};
    for (const PendingItem &item : std::as_const(completed)) {
        const QFileInfo sourceInfo(item.source);
        invalidatedFolders.insert(sourceInfo.absolutePath());
        if (action == Qt::MoveAction) {
            movedSources.append(item.source);
        }
    }
    if (!movedSources.isEmpty()) {
        removeMovedPathsFromSelection(
            movedSources, QStringLiteral("Move by drag and drop"));
    }
    refreshFoldersAfterFileOperation(invalidatedFolders);
    return fileOperationResult(true, QString(), QString(), action,
                               completed.size(), destinationInfo.absoluteFilePath());
}

QVariantMap FileListModel::createFolder(
    const QString &parentPath, const QString &name) {
    const QString trimmedName = name.trimmed();
    const QFileInfo parentInfo(parentPath);
    if (!parentInfo.exists() || !parentInfo.isDir()) {
        return fileOperationResult(
            false, QStringLiteral("Folder is unavailable"),
            QStringLiteral("The current folder could not be found:\n%1").arg(parentPath));
    }
    if (trimmedName.isEmpty() || trimmedName == QStringLiteral(".") ||
        trimmedName == QStringLiteral("..") || trimmedName.contains('/') ||
        trimmedName.contains('\\')) {
        return fileOperationResult(
            false, QStringLiteral("Invalid folder name"),
            QStringLiteral("Enter a valid folder name without path separators."));
    }
#ifdef Q_OS_WIN
    static const QRegularExpression invalidWindowsName(
        QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    static const QRegularExpression reservedWindowsName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (invalidWindowsName.match(trimmedName).hasMatch() ||
        trimmedName.endsWith('.') || trimmedName.endsWith(' ') ||
        reservedWindowsName.match(trimmedName).hasMatch()) {
        return fileOperationResult(
            false, QStringLiteral("Invalid folder name"),
            QStringLiteral("That name is not allowed on Windows."));
    }
#endif
    const QString newFolder = QDir(parentPath).filePath(trimmedName);
    if (QFileInfo::exists(newFolder) || QFileInfo(newFolder).isSymLink()) {
        return fileOperationResult(
            false, QStringLiteral("Folder already exists"),
            QStringLiteral("An item named “%1” already exists in this folder.")
                .arg(trimmedName));
    }
    if (!QDir(parentPath).mkdir(trimmedName)) {
        return fileOperationResult(
            false, QStringLiteral("Could not create folder"),
            QStringLiteral("The folder could not be created:\n%1").arg(newFolder));
    }
    refreshFoldersAfterFileOperation({parentInfo.absoluteFilePath()});
    return fileOperationResult(true, QString(), QString(), Qt::IgnoreAction,
                               0, QFileInfo(newFolder).absoluteFilePath());
}

QVariantMap FileListModel::createFolderAndDropUrls(
    const QVariantList &urls, const QString &parentPath,
    const QString &name, int dropAction) {
    const QVariantMap created = createFolder(parentPath, name);
    if (!created.value(QStringLiteral("success")).toBool()) {
        return created;
    }
    const QString folder = created.value(QStringLiteral("destinationFolder")).toString();
    const QVariantMap dropped = dropUrlsIntoFolder(urls, folder, dropAction);
    if (!dropped.value(QStringLiteral("success")).toBool()) {
        QDir(parentPath).rmdir(QFileInfo(folder).fileName());
        refreshFoldersAfterFileOperation({QFileInfo(parentPath).absoluteFilePath()});
        return dropped;
    }
    return dropped;
}
