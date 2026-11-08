#include "DecodeSizePolicy.h"

#include <QtMath>

#include <array>

namespace ZoinGallery {
namespace {

constexpr std::array<int, 7> ThumbnailTiers = {
    128, 192, 256, 384, 512, 768, MaximumPanelThumbnailEdge,
};
constexpr std::array<int, 9> ViewerFitTiers = {
    512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192,
};
constexpr qreal GrowthHysteresis = 1.10;

int longEdge(const QSize &size)
{
    return qMax(size.width(), size.height());
}

template <size_t N>
int coveringTier(int requested, const std::array<int, N> &tiers)
{
    for (const int tier : tiers) {
        if (tier >= requested) {
            return tier;
        }
    }
    return tiers.back();
}

QSize aspectSizeForLongEdge(const QSize &aspectSource, int edge)
{
    if (!aspectSource.isValid() || edge <= 0) {
        return {};
    }
    if (aspectSource.width() >= aspectSource.height()) {
        return QSize(edge, qMax(1, qCeil(
            qreal(aspectSource.height()) * edge / aspectSource.width())));
    }
    return QSize(qMax(1, qCeil(
                     qreal(aspectSource.width()) * edge
                     / aspectSource.height())),
                 edge);
}

} // namespace

QSize boundedPanelThumbnailSize(const QSize &size)
{
    if (longEdge(size) <= MaximumPanelThumbnailEdge) {
        return size;
    }
    const QSize fitted = size.scaled(MaximumPanelThumbnailEdge,
                                     MaximumPanelThumbnailEdge,
                                     Qt::KeepAspectRatio);
    return fitted.expandedTo(QSize(1, 1));
}

bool panelThumbnailCovers(const QSize &available, const QSize &bounds,
                          bool preserveAspect)
{
    if (available.isEmpty() || bounds.isEmpty()) {
        return false;
    }
    // A panel target is a bounding box for aspect-preserving frames.
    const QSize required = preserveAspect
        ? available.scaled(bounds, Qt::KeepAspectRatio).expandedTo(QSize(1, 1))
        : bounds;
    return available.width() >= required.width()
        && available.height() >= required.height();
}

QSize stableDecodeTarget(const QSize &requestedTarget,
                         const QSize &nativeSourceSize,
                         const QSize &previousPreparedTarget,
                         DecodeSizeFamily family,
                         bool expensiveSource)
{
    if (!requestedTarget.isValid() || requestedTarget.isEmpty()) {
        return {};
    }
    if (!expensiveSource) {
        return requestedTarget;
    }

    const int requestedEdge = longEdge(requestedTarget);
    const int nativeEdge = longEdge(nativeSourceSize);
    if (nativeSourceSize.isValid() && requestedEdge >= nativeEdge) {
        return nativeSourceSize;
    }

    const int previousEdge = longEdge(previousPreparedTarget);
    const bool previousCovers = previousPreparedTarget.width() >= requestedTarget.width()
        && previousPreparedTarget.height() >= requestedTarget.height();
    const bool viewerCanReuse = family == DecodeSizeFamily::ViewerFit
        && requestedEdge <= qFloor(previousEdge * GrowthHysteresis);
    if (previousPreparedTarget.isValid()
        && (previousCovers || viewerCanReuse)
        && (!nativeSourceSize.isValid() || previousEdge <= nativeEdge)) {
        // Preserve the viewer's hysteresis on slow sources. Panel thumbnails
        // reuse a prepared tier only when it covers the displayed pixels.
        return previousPreparedTarget;
    }

    int tier = family == DecodeSizeFamily::Thumbnail
        ? qMax(requestedEdge, coveringTier(requestedEdge, ThumbnailTiers))
        : coveringTier(requestedEdge, ViewerFitTiers);
    if (nativeSourceSize.isValid()) {
        tier = qMin(tier, nativeEdge);
    }
    const QSize aspectSource = nativeSourceSize.isValid()
        ? nativeSourceSize : requestedTarget;
    return aspectSizeForLongEdge(aspectSource, tier);
}

} // namespace ZoinGallery
