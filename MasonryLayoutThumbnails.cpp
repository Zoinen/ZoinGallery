#include "MasonryLayout.h"
#include "GalleryViewportMotion.h"
#include "VectorSource.h"

#include <ZoinGallery/GalleryCatalogSource.h>
#include <ZoinGallery/MediaTimingTrace.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace {

ZoinGallery::GalleryPresentationMode enginePresentationMode(
    MasonryLayout::PresentationMode mode) {
    using EngineMode = ZoinGallery::GalleryPresentationMode;
    switch (mode) {
    case MasonryLayout::Masonry: return EngineMode::Masonry;
    case MasonryLayout::Columns: return EngineMode::Columns;
    case MasonryLayout::Details: return EngineMode::Details;
    case MasonryLayout::Grid: return EngineMode::Grid;
    case MasonryLayout::Icons: return EngineMode::Icons;
    }
    return EngineMode::Masonry;
}

bool isScalableVectorThumbnail(const ImageInfo &info) {
    return isVectorSource(info);
}

} // namespace

ZoinGallery::GalleryViewportWindow
MasonryLayout::viewportMaterializationPlan() const {
    const qreal extent = _presentationMode == Columns ? width() : height();
    const qreal itemExtent = _presentationMode == Columns
        ? columnStride()
        : _presentationMode == Icons
            ? virtualGridRowHeight() : effectiveTargetExtent();
    auto plan = ZoinGallery::GalleryViewportMaterializer::plan(
        enginePresentationMode(_presentationMode), _contentY,
        extent, itemExtent);
    if (_presentationMode == Masonry && _viewportMotionState
        && _viewportMotionState->active.load(std::memory_order_acquire)) {
        auto leading = ZoinGallery::GalleryViewportMaterializer::withMotionTarget(
            plan, _viewportMotionState->targetY.load(std::memory_order_acquire));
        if (_viewportMotionState->page) {
            // Page motion prepares the leading rows in slices. A synchronous
            // commit only has to recover rows already inside the viewport.
            leading.delegateStart = plan.visibleStart;
            leading.delegateEnd = plan.visibleEnd;
        }
        return leading;
    }
    return plan;
}

bool MasonryLayout::preparingViewportMotion() const {
    return _presentationMode == Masonry && _viewportMotionState
        && _viewportMotionState->prepareAlongPath
        && _viewportMotionState->active.load(std::memory_order_acquire);
}

void MasonryLayout::updateViewportIndexSets() {
    QList<int> visible;
    QList<int> overscan;
    const ZoinGallery::GalleryViewportWindow materialization =
        viewportMaterializationPlan();
    if (_containedPreview) {
        // Contained cards have no scrolling window. Their final geometry
        // selects the bounded 1/4/9/16-cell demand; hidden bricks can retain
        // old row coordinates across a capacity change.
        for (int index = 0; index < _bricks.size(); ++index) {
            if (!_bricks[index].normalizedSize.isEmpty())
                visible.append(index);
        }
        overscan = visible;
    }
    else if (_presentationMode == Columns) {
        const qreal visibleLeft = materialization.visibleStart;
        const qreal visibleRight = materialization.visibleEnd;
        const qreal overscanLeft = materialization.metadataStart;
        const qreal overscanRight = materialization.metadataEnd;
        visible = indexesForHorizontalRange(visibleLeft, visibleRight);
        overscan = indexesForHorizontalRange(
            overscanLeft, overscanRight);
    }
    else {
        visible = indexesForVerticalRange(_contentY,
                                          materialization.visibleEnd);
        overscan = indexesForVerticalRange(
            materialization.metadataStart,
            materialization.metadataEnd);
    }
    const QSet<int> nextVisible(visible.cbegin(), visible.cend());
    const QSet<int> nextOverscan(overscan.cbegin(), overscan.cend());
    const bool visibleChanged = nextVisible != _visibleIndexSet;
    const bool overscanChanged = nextOverscan != _overscanIndexSet;
    _visibleIndexSet = nextVisible;
    _overscanIndexSet = nextOverscan;
    if (auto *source = dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model)) {
        const bool previewMode = _presentationMode == Masonry
            || _presentationMode == Grid || _presentationMode == Icons;
        if (previewMode && thumbnailsEnabled()
            && isVisible() && !visible.isEmpty()) {
            source->requestDirectoryPreviews(visible);
        }
        else if (!previewMode || !thumbnailsEnabled()) {
            // Leaving a preview presentation is an explicit cancellation.
            // An empty/hidden viewport while the same presentation is being
            // rewrapped is not: the host can hide the disposable geometry
            // pass and publish the final visible rows on the next update.
            source->requestDirectoryPreviews({});
        }
        else if (ZoinGallery::MediaTimingTrace::enabled()) {
            ZoinGallery::MediaTimingTrace::event(
                QStringLiteral("qt.gallery.directory_demand_retained"), {
                    {QStringLiteral("fix"),
                     QStringLiteral("[FIX:folder-preview-layout-demand]")},
                    {QStringLiteral("visible"), isVisible()},
                    {QStringLiteral("rows"), visible.size()},
                    {QStringLiteral("mode"), int(_presentationMode)},
                });
        }
    }

    if (_visibleIndexSet.isEmpty()) {
        _visibleStart = -1;
        _visibleEnd = -1;
    }
    else {
        const auto bounds = std::minmax_element(
            _visibleIndexSet.cbegin(), _visibleIndexSet.cend());
        _visibleStart = *bounds.first;
        _visibleEnd = *bounds.second;
    }
    if (visibleChanged) {
        emit visibleIndexesChanged();
    }
    if (overscanChanged) {
        emit overscanIndexesChanged();
    }
    if ((visibleChanged || overscanChanged)
        && !(_viewportMotionState && _viewportMotionState->edgeJump)) {
        planViewportThumbnails(_overscanIndexSet);
    }
}

void MasonryLayout::planThumbnailForIndex(
    int index, bool highPriority, QList<ImageDecodeRequest> &requests,
    bool force) {
    if (!thumbnailsEnabled() || !_model || index < 0
        || index >= logicalBrickCount()) {
        return;
    }
    MasonryBrick &brick = ensureBrickAt(index);
    ImageFile *image = brick.image;
    const auto traceSkipped = [&](const QString &reason) {
        if (!ZoinGallery::MediaTimingTrace::enabled()) {
            return;
        }
        ZoinGallery::MediaTimingTrace::event(
            QStringLiteral("qt.gallery.thumbnail.plan_skipped"), {
                {QStringLiteral("fix"), QStringLiteral("[FIX:incremental-facade-retention]")},
                {QStringLiteral("reason"), reason},
                {QStringLiteral("row"), index},
                {QStringLiteral("entryId"), brick.modelIdentity},
                {QStringLiteral("displayName"), brick.modelText},
                {QStringLiteral("modelIsImage"), brick.modelIsImage},
                {QStringLiteral("knownWidth"), brick.modelKnownSize.width()},
                {QStringLiteral("hasFacade"), image != nullptr},
                {QStringLiteral("facadeIsImage"), image && image->isImage()},
                {QStringLiteral("facadeWidth"), image ? image->fullSize().width() : -1},
                {QStringLiteral("refreshPending"), _delegateRefreshPending},
                {QStringLiteral("snapshotRefresh"), _visualSnapshotRefresh},
                {QStringLiteral("materializationPending"), _delegateMaterializationPending},
            });
    };
    if (!image && canUseLightweightRows()) {
        // The layout intentionally keeps non-delegate rows as POD-only
        // bricks. Once metadata makes an overscan image decodable, create its
        // ImageFile facade here rather than requiring a viewport-set change
        // (or materializing the whole catalog during model reset).
        if (_delegateRefreshPending || _visualSnapshotRefresh
            || _delegateMaterializationPending
            || !brick.modelIsImage) {
            traceSkipped(QStringLiteral("facade-deferred"));
            return;
        }
        if ((!brick.modelKnownSize.isValid() || brick.modelKnownSize.isEmpty())
            && (_visualSnapshotRole < 0
                || !_model->index(index, 0).data(_visualSnapshotRole).toMap()
                        .value(QStringLiteral("isVector")).toBool())) {
            traceSkipped(QStringLiteral("facade-deferred"));
            return;
        }
        image = materializeImageForIndex(index);
    }
    if (!image || !image->isImage()
        || (!image->fullSize().isValid() && !isScalableVectorThumbnail(image->info()))) {
        traceSkipped(QStringLiteral("facade-not-decodable"));
        return;
    }
    QSizeF previewSize = brick.previewGeometry.size();
    if ((!previewSize.isValid() || previewSize.isEmpty()) &&
        _presentationMode == Columns) {
        const qreal cellWidth = qMax<qreal>(
            1, (width() - _paddingLeft - _paddingRight) /
                   effectiveColumnCount());
        const qreal side = qMax<qreal>(
            1, qMin(effectiveTargetExtent() - _spacing,
                    cellWidth - _spacing));
        previewSize = QSizeF(side, side);
    }
    if (!previewSize.isValid() || previewSize.isEmpty()) {
        return;
    }
    const QSize targetSize = previewDecodeTargetSize(brick, previewSize);
    if (!targetSize.isValid() || targetSize.width() <= 0 ||
        targetSize.height() <= 0) {
        return;
    }
    const ImageInfo info = image->info();
    const QString transformKey = image->info().thumbnailKind
        == QStringLiteral("video")
        ? QStringLiteral("video-contact-sheet-2x2-display-v6")
        : previewTransformKey();
    const bool sameSource =
        brick.lastPlannedSourcePath == info.sourceIdentity() &&
        brick.lastPlannedModified == info.lastModified &&
        brick.lastPlannedFileSize == info.fileSize &&
        brick.lastPlannedVersionToken == info.sourceVersionToken;
    const bool plannedFrameCovers = sameSource &&
        brick.lastPlannedTransformKey == transformKey &&
        brick.lastPlannedTargetSize.isValid() &&
        brick.lastPlannedTargetSize.width() >= targetSize.width() &&
        brick.lastPlannedTargetSize.height() >= targetSize.height();
    const auto rememberPlan = [&brick, &info, &transformKey](
                                  const QSize &plannedSize) {
        brick.lastPlannedTargetSize = plannedSize;
        brick.lastPlannedTransformKey = transformKey;
        brick.lastPlannedSourcePath = info.sourceIdentity();
        brick.lastPlannedModified = info.lastModified;
        brick.lastPlannedFileSize = info.fileSize;
        brick.lastPlannedVersionToken = info.sourceVersionToken;
    };
    // A published image at least as large as the exact preview rect is shared
    // by every presentation mode. If eviction cleared its provider ID, do not
    // trust the retained QImage alone: resubmit so the model/cache can publish
    // a fresh URL synchronously or schedule a decode.
    if (!force && !image->imageIdUrl().isEmpty()) {
        // External sessions publish shared-cache provider IDs without keeping
        // a second QImage copy in every ImageFile. That URL is authoritative:
        // source/version changes and LRU eviction clear it, while geometry or
        // DPR changes explicitly invoke the forced re-read path below.
        if (image->image().isNull() && plannedFrameCovers) {
            return;
        }
        if (image->imageMatchesSource(image->info()) &&
            image->image().width() >= targetSize.width() &&
            image->image().height() >= targetSize.height()) {
            rememberPlan(image->image().size());
            return;
        }
    }
    requests.append(ImageDecodeRequest{
        .info = info,
        .targetSize = targetSize,
        .viewerRequest = false,
        // Video decoding is expensive enough that every request must first
        // consult the persistent cache, even when the fast metadata pass
        // bypassed its cache for a visible row.  The cache gate then keeps
        // QMediaPlayer/FFmpeg from starting on a proven path/stat hit.
        .checkCache = info.isCached
            || info.thumbnailKind == QStringLiteral("video"),
        // Opening a video invokes the platform multimedia backend and may
        // seek a very large source before the first frame arrives. Keep those
        // jobs in the background lane even for visible cells so a folder
        // transition can finish its metadata/layout work without competing
        // with FFmpeg for the urgent decode workers.
        .highPriority = highPriority
            && info.thumbnailKind != QStringLiteral("video"),
        .thumbnailTransformKey = transformKey,
    });
    rememberPlan(targetSize);
}

QSize MasonryLayout::previewDecodeTargetSize(
    const MasonryBrick &brick, const QSizeF &previewBounds) {
    if (!brick.image || previewBounds.width() <= 0 || previewBounds.height() <= 0) {
        return {};
    }
    const QSizeF physicalBounds(
        dp(previewBounds.width()), dp(previewBounds.height()));
    if (physicalBounds.width() <= 0 || physicalBounds.height() <= 0) {
        return {};
    }
    const QSize source = brick.image->fullSize();
    if (!source.isValid() || source.isEmpty()) {
        // A vector request acquires scalable source geometry, whose authoring
        // viewport is supplied by the source itself. Bitmap metadata is not
        // a prerequisite and the target is only the existing queue's ticket.
        return isScalableVectorThumbnail(brick.image->info())
            ? QSize(qMax(1, qCeil(physicalBounds.width())),
                    qMax(1, qCeil(physicalBounds.height()))) : QSize{};
    }

    const qreal horizontalScale = physicalBounds.width() / source.width();
    const qreal verticalScale = physicalBounds.height() / source.height();
    // Masonry gives the preview its own pixel-aligned rectangle. Its rounded
    // edges can differ slightly from the source aspect, and the shader fills
    // both axes. The other modes fit the image inside a larger shared slot.
    qreal scale = _presentationMode == Masonry
        ? qMax(horizontalScale, verticalScale)
        : qMin(horizontalScale, verticalScale);
    // Raster thumbnails never need synthesized pixels: if the native image
    // is smaller than the preview, publish native dimensions and let the
    // scene graph perform the unavoidable display upscale. SVG metadata is
    // different: its width/height is only the authoring viewport (commonly
    // 16 or 24 px), not a native pixel limit. Clamping SVG here used to
    // publish a tiny raster which was then enlarged by QML in every panel
    // mode. Keep the requested physical preview size for scalable vectors.
    // Video metadata uses a 16x9 aspect-ratio placeholder because probing a
    // stream just to lay out a gallery would defeat the fast thumbnail path.
    // It is not a native pixel limit: treating it like one produces 16x9
    // thumbnails which the scene graph has to enlarge into the tile.
    if (!isScalableVectorThumbnail(brick.image->info())
        && !brick.image->isVideoThumbnail()) {
        scale = qMin<qreal>(scale, 1.0);
    }
    scale = qMax<qreal>(0, scale);
    if (scale <= 0) {
        return {};
    }

    const auto scaledDimension = [scale](int dimension) {
        return qMax(1, qCeil(dimension * scale));
    };
    return QSize(scaledDimension(source.width()),
                 scaledDimension(source.height()));
}

QString MasonryLayout::previewTransformKey() const {
    // Pixel processing is identical for every presentation: decode one
    // oriented, source-aspect frame and let the QML surface fit it.
    return QStringLiteral("thumbnail-aspect-v1");
}

QList<int> MasonryLayout::edgeThumbnailIndexes() const {
    if (!_viewportMotionState || !_viewportMotionState->edgeJump)
        return {};
    const qreal target = _viewportMotionState->targetY.load(std::memory_order_acquire);
    return indexesForVerticalRange(target, target + height());
}

QVariantList MasonryLayout::metadataDemandIndexes() const {
    const auto edge = edgeThumbnailIndexes();
    if (edge.isEmpty()) return visibleIndexes();
    QVariantList result;
    result.reserve(edge.size());
    for (int index : edge) result.append(index);
    return result;
}

void MasonryLayout::planViewportThumbnails(
    const QSet<int> &candidateIndexes, bool force) {
    if (!thumbnailsEnabled()) {
        return;
    }
    auto *requestModel =
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model);
    if (!requestModel || _cancelingThumbnailPlan) {
        return;
    }

    const auto edge = edgeThumbnailIndexes();
    const QSet<int> edgeIndexes(edge.cbegin(), edge.cend());
    const auto &candidates = edge.isEmpty() ? candidateIndexes : edgeIndexes;
    const auto &visible = edge.isEmpty() ? _visibleIndexSet : edgeIndexes;
    const auto &overscan = edge.isEmpty() ? _overscanIndexSet : edgeIndexes;
    if (candidates.isEmpty()) return;

    const bool tracksViewportWindow =
        force || !edge.isEmpty() || candidateIndexes == _overscanIndexSet;
    const ZoinGallery::GalleryThumbnailWindowPlan windowPlan =
        _thumbnailPlanner.planWindow({
            .candidates = candidates,
            .visible = visible,
            .logicalCount = logicalBrickCount(),
            .mode = enginePresentationMode(_presentationMode),
            .force = force,
            .tracksViewportWindow = tracksViewportWindow,
            .embedded = isEmbedded(),
        });
    if (!windowPlan.valid) {
        return;
    }

    const auto cancelDecodeWindow = [&] {
        _cancelingThumbnailPlan = true;
        requestModel->cancelAllDecodeRunners();
        _cancelingThumbnailPlan = false;
    };
    if (windowPlan.cancelExisting) {
        cancelDecodeWindow();
    }

    // Fixed layouts request metadata only for visible cells and bounded
    // overscan. Justified masonry additionally maintains one background
    // aspect-ratio lease because every preceding row affects later geometry.
    // A model delta plans pixels only for its dirty rows. Metadata demand is
    // the complete viewport lease; replacing it with those rows would demote
    // and cancel other images which are still visible.
    QList<int> visibleDemand(visible.cbegin(), visible.cend());
    QList<int> backgroundDemand;
    backgroundDemand.reserve(overscan.size());
    for (const int index : overscan) {
        if (!visible.contains(index)) backgroundDemand.append(index);
    }
    std::sort(visibleDemand.begin(), visibleDemand.end());
    std::sort(backgroundDemand.begin(), backgroundDemand.end());
    requestModel->requestImageMetadata(visibleDemand, true, false);
    requestModel->requestImageMetadata(backgroundDemand, false, false);
    if (windowPlan.catalogWideMetadata && !_containedPreview) {
        requestModel->requestImageMetadata({}, false, true);
    }

    const auto buildRequests = [&](bool forceRequests) {
        QList<ImageDecodeRequest> result;
        result.reserve(windowPlan.visible.size()
                       + windowPlan.background.size());
        for (const int index : windowPlan.visible) {
            planThumbnailForIndex(index, true, result, forceRequests);
        }
        for (const int index : windowPlan.background) {
            planThumbnailForIndex(index, false, result, forceRequests);
        }
        return result;
    };
    const auto requestKeys = [](const QList<ImageDecodeRequest> &requests) {
        QSet<QString> keys;
        keys.reserve(requests.size());
        for (const ImageDecodeRequest &request : requests) {
            keys.insert(request.info.sourceIdentity() + QChar(0x1f)
                        + request.info.sourceVersionToken + QChar(0x1f)
                        + QString::number(request.info.fileSize)
                        + QChar(0x1f)
                        + QString::number(request.targetSize.width())
                        + QLatin1Char('x')
                        + QString::number(request.targetSize.height())
                        + QChar(0x1f)
                        + request.thumbnailTransformKey);
        }
        return keys;
    };

    bool forceRequests = windowPlan.forceRequests;
    QList<ImageDecodeRequest> requests = buildRequests(forceRequests);
    QSet<QString> currentRequestKeys = requestKeys(requests);
    const ZoinGallery::GalleryThumbnailRequestKeyPlan keyPlan =
        _thumbnailPlanner.accountRequestKeys(
            currentRequestKeys, windowPlan.desired.size(),
            tracksViewportWindow, isEmbedded(), forceRequests);
    if (keyPlan.cancelExisting) {
        // Continuous resize can retain the same rows while creating several
        // successively larger target tiers. Rebase those tiers under the same
        // bounded policy as spatial scrolling.
        cancelDecodeWindow();
        forceRequests = true;
        requests = buildRequests(true);
        currentRequestKeys = requestKeys(requests);
        static_cast<void>(_thumbnailPlanner.accountRequestKeys(
            currentRequestKeys, windowPlan.desired.size(),
            tracksViewportWindow, isEmbedded(), true));
    }
    if (!requests.isEmpty()) {
        requestModel->decodeImages(requests);
    }
}
