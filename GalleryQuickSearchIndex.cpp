#include "GalleryQuickSearchIndex.h"

#include <ZoinGallery/GalleryCatalogModel.h>

#include <algorithm>
#include <utility>

namespace ZoinGallery {

void GalleryQuickSearchIndex::setModel(GalleryCatalogModel *model) {
    if (_model == model) {
        return;
    }
    _model = model;
    invalidate();
}

void GalleryQuickSearchIndex::invalidate() {
    _names.clear();
    _foldedNames.clear();
    _matches.clear();
    _query.clear();
    _foldedQuery.clear();
    _indexed = false;
    _lastVisitedRows = 0;
}

void GalleryQuickSearchIndex::ensureIndex() {
    if (!_model || _indexed) {
        return;
    }
    const int count = _model->rowCount();
    _names.reserve(count);
    _foldedNames.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QString name = _model->data(
            _model->index(row, 0), GalleryCatalogModel::NameRole).toString();
        _names.append(name);
        _foldedNames.append(name.toCaseFolded());
    }
    _indexed = true;
}

bool GalleryQuickSearchIndex::setQuery(const QString &query) {
    if (_query == query) {
        _lastVisitedRows = 0;
        return !_matches.isEmpty() || query.isEmpty();
    }
    ensureIndex();
    const QString folded = query.toCaseFolded();
    const bool narrowsCurrent = !_foldedQuery.isEmpty()
        && folded.startsWith(_foldedQuery);
    const QVector<int> previous = narrowsCurrent ? _matches : QVector<int>{};
    if (folded.isEmpty()) {
        _query.clear();
        _foldedQuery.clear();
        _matches.clear();
        _lastVisitedRows = 0;
        return true;
    }

    _lastVisitedRows = narrowsCurrent ? previous.size()
                                      : _foldedNames.size();
    QVector<int> nextMatches;
    if (narrowsCurrent) {
        nextMatches.reserve(previous.size());
        for (const int row : previous) {
            if (_foldedNames.at(row).contains(folded)) {
                nextMatches.append(row);
            }
        }
    } else {
        nextMatches.reserve(_foldedNames.size());
        for (int row = 0; row < _foldedNames.size(); ++row) {
            if (_foldedNames.at(row).contains(folded)) {
                nextMatches.append(row);
            }
        }
    }
    if (nextMatches.isEmpty()) {
        return false;
    }
    _query = query;
    _foldedQuery = folded;
    _matches = std::move(nextMatches);
    return true;
}

const QString &GalleryQuickSearchIndex::query() const { return _query; }
int GalleryQuickSearchIndex::matchCount() const { return _matches.size(); }

GalleryQuickSearchMatch GalleryQuickSearchIndex::matchAt(int index) const {
    if (index < 0 || index >= _foldedNames.size()
        || _foldedQuery.isEmpty()
        || !std::binary_search(_matches.cbegin(), _matches.cend(), index)) {
        return {};
    }
    const int utf16Start = _foldedNames.at(index).indexOf(_foldedQuery);
    const int codePointStart = utf16Start < 0 ? -1
        : _names.at(index).left(utf16Start).toUcs4().size();
    const int codePointLength = _query.toUcs4().size();
    return {
        .index = index,
        // The public names predate the controller API. These values now use
        // Unicode code-point offsets, matching the Go fast-find payload and
        // QML's Array.from() based text slicing.
        .utf16Start = codePointStart,
        .utf16Length = codePointStart >= 0 ? codePointLength : 0,
    };
}

int GalleryQuickSearchIndex::nextMatch(
    int currentIndex, bool forward, bool forceMove, bool wrap) const {
    if (_matches.isEmpty()) {
        return currentIndex;
    }
    if (!forceMove
        && std::binary_search(_matches.cbegin(), _matches.cend(),
                              currentIndex)) {
        return currentIndex;
    }
    if (forward) {
        const auto next = std::upper_bound(
            _matches.cbegin(), _matches.cend(), currentIndex);
        return next != _matches.cend() ? *next
            : wrap ? _matches.constFirst() : currentIndex;
    }
    const auto next = std::lower_bound(
        _matches.cbegin(), _matches.cend(), currentIndex);
    if (next != _matches.cbegin()) {
        return *std::prev(next);
    }
    return wrap ? _matches.constLast() : currentIndex;
}

int GalleryQuickSearchIndex::indexedRowCount() const {
    return _names.size();
}

int GalleryQuickSearchIndex::lastVisitedRowCount() const {
    return _lastVisitedRows;
}

} // namespace ZoinGallery
