#include "GalleryThumbnailPlanner.h"

#include <algorithm>
#include <utility>

namespace ZoinGallery {

GalleryThumbnailWindowPlan GalleryThumbnailPlanner::planWindow(
    const GalleryThumbnailWindowRequest &request) {
    GalleryThumbnailWindowPlan result;
    result.forceRequests = request.force;
    result.catalogWideMetadata =
        request.mode == GalleryPresentationMode::Masonry;

    for (const int index : request.candidates) {
        if (index >= 0 && index < request.logicalCount) {
            result.desired.insert(index);
        }
    }
    if (result.desired.isEmpty()) {
        return result;
    }
    result.valid = true;

    if (request.force) {
        _scheduledIndexes = result.desired;
        _scheduledRequestKeys.clear();
    } else if (request.tracksViewportWindow) {
        QSet<int> overlap = _lastViewportIndexes;
        overlap.intersect(result.desired);
        QSet<int> scheduledUnion = _scheduledIndexes;
        scheduledUnion.unite(result.desired);
        const int scheduledLimit = qMax(
            result.desired.size() + 1, result.desired.size() * 3);
        const bool disjointJump = !_lastViewportIndexes.isEmpty()
            && overlap.isEmpty();
        const bool queueWindowExceeded =
            scheduledUnion.size() > scheduledLimit;
        if (!request.embedded && (disjointJump || queueWindowExceeded)) {
            result.cancelExisting = true;
            result.forceRequests = true;
            _scheduledIndexes = result.desired;
            _scheduledRequestKeys.clear();
        } else {
            _scheduledIndexes = std::move(scheduledUnion);
        }
    }
    if (request.tracksViewportWindow) {
        _lastViewportIndexes = result.desired;
    }

    result.visible.reserve(result.desired.size());
    result.background.reserve(result.desired.size());
    for (const int index : std::as_const(result.desired)) {
        (request.visible.contains(index) ? result.visible : result.background)
            .append(index);
    }
    std::sort(result.visible.begin(), result.visible.end());
    std::sort(result.background.begin(), result.background.end());
    return result;
}

GalleryThumbnailRequestKeyPlan GalleryThumbnailPlanner::accountRequestKeys(
    const QSet<QString> &currentKeys, int desiredCount,
    bool tracksViewportWindow, bool embedded, bool force) {
    GalleryThumbnailRequestKeyPlan result;
    result.forceRequests = force;
    if (force) {
        _scheduledRequestKeys = currentKeys;
        return result;
    }
    if (!tracksViewportWindow) {
        return result;
    }

    QSet<QString> requestUnion = _scheduledRequestKeys;
    requestUnion.unite(currentKeys);
    const int requestLimit = qMax(desiredCount + 1, desiredCount * 3);
    if (!embedded && requestUnion.size() > requestLimit) {
        result.cancelExisting = true;
        result.forceRequests = true;
        _scheduledRequestKeys = currentKeys;
    } else {
        _scheduledRequestKeys = std::move(requestUnion);
    }
    return result;
}

void GalleryThumbnailPlanner::reset() {
    _scheduledIndexes.clear();
    _scheduledRequestKeys.clear();
    _lastViewportIndexes.clear();
}

int GalleryThumbnailPlanner::scheduledIndexCount() const {
    return _scheduledIndexes.size();
}

int GalleryThumbnailPlanner::scheduledRequestKeyCount() const {
    return _scheduledRequestKeys.size();
}

} // namespace ZoinGallery
