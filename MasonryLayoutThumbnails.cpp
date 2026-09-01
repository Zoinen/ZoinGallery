#include "MasonryLayout.h"

#include <ZoinGallery/GalleryCatalogSource.h>

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
    const QString suffix = QFileInfo(info.path).suffix();
    return suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("svgz"), Qt::CaseInsensitive) == 0;
}

} // namespace

ZoinGallery::GalleryViewportWindow
MasonryLayout::viewportMaterializationPlan() const {
    const qreal extent = _presentationMode == Columns ? width() : height();
    const qreal itemExtent = _presentationMode == Columns
        ? columnStride()
        : (_presentationMode == Icons || sparseVirtualLayout())
            ? virtualGridRowHeight() : effectiveTargetExtent();
    return ZoinGallery::GalleryViewportMaterializer::plan(
        enginePresentationMode(_presentationMode), _contentY,
        extent, itemExtent);
}

void MasonryLayout::updateViewportIndexSets() {
    QList<int> visible;
    QList<int> overscan;
    const ZoinGallery::GalleryViewportWindow materialization =
        viewportMaterializationPlan();
    if (_presentationMode == Columns) {
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
    if (visibleChanged || overscanChanged) {
        planViewportThumbnails(_overscanIndexSet);
    }
}

void MasonryLayout::planThumbnailForIndex(
    int index, bool highPriority, QList<ImageDecodeRequest> &requests,
    bool force) {
    if (!_model || index < 0 || index >= logicalBrickCount()) {
        return;
    }
    MasonryBrick &brick = ensureBrickAt(index);
    ImageFile *image = brick.image;
    if (!image && canUseLightweightRows()) {
        // The layout intentionally keeps non-delegate rows as POD-only
        // bricks. Once metadata makes an overscan image decodable, create its
        // ImageFile facade here rather than requiring a viewport-set change
        // (or materializing the whole catalog during model reset).
        if (_delegateRefreshPending || _visualSnapshotRefresh
            || _delegateMaterializationPending
            || !brick.modelIsImage || !brick.modelKnownSize.isValid()
            || brick.modelKnownSize.isEmpty()) {
            return;
        }
        image = materializeImageForIndex(index);
    }
    if (!image || !image->isImage() || !image->fullSize().isValid()) {
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
    const QString transformKey = previewTransformKey();
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
        .checkCache = info.isCached,
        .highPriority = highPriority,
        .thumbnailTransformKey = transformKey,
    });
    rememberPlan(targetSize);
}

QSize MasonryLayout::previewDecodeTargetSize(
    const MasonryBrick &brick, const QSizeF &previewBounds) {
    if (!brick.image || !brick.image->fullSize().isValid() ||
        brick.image->fullSize().isEmpty() ||
        previewBounds.width() <= 0 || previewBounds.height() <= 0) {
        return {};
    }

    const QSize source = brick.image->fullSize();
    const QSizeF physicalBounds(
        dp(previewBounds.width()), dp(previewBounds.height()));
    if (physicalBounds.width() <= 0 || physicalBounds.height() <= 0) {
        return {};
    }

    const qreal horizontalScale = physicalBounds.width() / source.width();
    const qreal verticalScale = physicalBounds.height() / source.height();
    // Only Masonry gives the preview surface the source aspect ratio itself.
    // Every fixed presentation displays the complete image with
    // PreserveAspectFit, so decode the smallest aspect-preserving frame that
    // covers that fitted surface instead of the old Grid crop tier.
    qreal scale = qMin(horizontalScale, verticalScale);
    // Raster thumbnails never need synthesized pixels: if the native image
    // is smaller than the preview, publish native dimensions and let the
    // scene graph perform the unavoidable display upscale. SVG metadata is
    // different: its width/height is only the authoring viewport (commonly
    // 16 or 24 px), not a native pixel limit. Clamping SVG here used to
    // publish a tiny raster which was then enlarged by QML in every panel
    // mode. Keep the requested physical preview size for scalable vectors.
    if (!isScalableVectorThumbnail(brick.image->info())) {
        scale = qMin<qreal>(scale, 1.0);
    }
    scale = qMax<qreal>(0, scale);
    if (scale <= 0) {
        return {};
    }

    const auto scaledDimension = [scale, this](int dimension) {
        const qreal scaled = dimension * scale;
        return qMax(1, qFloor(scaled));
    };
    return QSize(scaledDimension(source.width()),
                 scaledDimension(source.height()));
}

QString MasonryLayout::previewTransformKey() const {
    // Pixel processing is identical for every presentation: decode one
    // oriented, source-aspect frame and let the QML surface fit it.
    return QStringLiteral("thumbnail-aspect-v1");
}

void MasonryLayout::planViewportThumbnails(
    const QSet<int> &candidateIndexes, bool force) {
    auto *requestModel =
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model);
    if (!requestModel || candidateIndexes.isEmpty()
        || _cancelingThumbnailPlan) {
        return;
    }

    const bool tracksViewportWindow =
        force || candidateIndexes == _overscanIndexSet;
    const ZoinGallery::GalleryThumbnailWindowPlan windowPlan =
        _thumbnailPlanner.planWindow({
            .candidates = candidateIndexes,
            .visible = _visibleIndexSet,
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
    requestModel->requestImageMetadata(windowPlan.visible, true, false);
    requestModel->requestImageMetadata(windowPlan.background, false, false);
    if (windowPlan.catalogWideMetadata) {
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
