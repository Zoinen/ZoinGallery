#ifndef GALLERYTHUMBNAILPLANNER_H
#define GALLERYTHUMBNAILPLANNER_H

#include "GalleryLayoutEngine.h"

#include <QList>
#include <QSet>
#include <QString>

namespace ZoinGallery {

struct GalleryThumbnailWindowRequest {
    QSet<int> candidates;
    QSet<int> visible;
    int logicalCount = 0;
    GalleryPresentationMode mode = GalleryPresentationMode::Masonry;
    bool force = false;
    bool tracksViewportWindow = true;
    bool embedded = false;
};

struct GalleryThumbnailWindowPlan {
    QSet<int> desired;
    QList<int> visible;
    QList<int> background;
    bool valid = false;
    bool cancelExisting = false;
    bool forceRequests = false;
    bool catalogWideMetadata = false;
};

struct GalleryThumbnailRequestKeyPlan {
    bool cancelExisting = false;
    bool forceRequests = false;
};

// Owns bounded decode-window bookkeeping only. It never stores catalog rows,
// image objects, rendered delegates, or per-presentation state.
class GalleryThumbnailPlanner final {
public:
    [[nodiscard]] GalleryThumbnailWindowPlan planWindow(
        const GalleryThumbnailWindowRequest &request);
    [[nodiscard]] GalleryThumbnailRequestKeyPlan accountRequestKeys(
        const QSet<QString> &currentKeys, int desiredCount,
        bool tracksViewportWindow, bool embedded, bool force);
    void reset();

    [[nodiscard]] int scheduledIndexCount() const;
    [[nodiscard]] int scheduledRequestKeyCount() const;

private:
    QSet<int> _scheduledIndexes;
    QSet<QString> _scheduledRequestKeys;
    QSet<int> _lastViewportIndexes;
};

} // namespace ZoinGallery

#endif // GALLERYTHUMBNAILPLANNER_H
