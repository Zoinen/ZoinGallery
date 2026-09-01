#include "SelectedImagesModel.h"

#include <QUrl>

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

void SelectedImagesModel::applySelectionChanges(
    const QList<int> &selectedIndexes,
    const QList<int> &deselectedIndexes) {
    bool changed = false;
    for (const int index : selectedIndexes) {
        if (index >= 0 && index < _items.size()
            && !_items[index]->isSelected()) {
            _items[index]->setIsSelected(true);
            changed = true;
        }
    }
    for (const int index : deselectedIndexes) {
        if (index >= 0 && index < _items.size()
            && _items[index]->isSelected()) {
            _items[index]->setIsSelected(false);
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

QVariantMap SelectedImagesModel::finalizeExternalDrag(
    const QVariantList &urls, int dropAction) {
    return _selectionSourceModel
        ? _selectionSourceModel->finalizeExternalDrag(urls, dropAction)
        : QVariantMap{};
}

void SelectedImagesModel::configureNativeDragCursors(QObject *dragSource) {
    if (_selectionSourceModel) {
        _selectionSourceModel->configureNativeDragCursors(dragSource);
    }
}
