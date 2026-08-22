#include "DecodeSizePolicy.h"

#include <QtMath>

#include <array>

namespace ZoinGallery {
namespace {

constexpr std::array<int, 7> ThumbnailTiers = {
    128, 192, 256, 384, 512, 768, 1024,
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
    if (previousPreparedTarget.isValid()
        && requestedEdge <= qFloor(previousEdge * GrowthHysteresis)
        && (!nativeSourceSize.isValid() || previousEdge <= nativeEdge)) {
        // A small growth is rendered by a correspondingly tiny upscale. This
        // is deliberate for a slow source and prevents a +1 px layout change
        // from triggering another source read/decode.
        return previousPreparedTarget;
    }

    int tier = family == DecodeSizeFamily::Thumbnail
        ? coveringTier(requestedEdge, ThumbnailTiers)
        : coveringTier(requestedEdge, ViewerFitTiers);
    if (nativeSourceSize.isValid()) {
        tier = qMin(tier, nativeEdge);
    }
    const QSize aspectSource = nativeSourceSize.isValid()
        ? nativeSourceSize : requestedTarget;
    return aspectSizeForLongEdge(aspectSource, tier);
}

} // namespace ZoinGallery
