#include "ImageFile.h"
#include "ProviderImageStore.h"
#include "ViewerImageCache.h"

#include <QImage>
#include <QSharedPointer>
#include <QtTest>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace {
void configureImage(ImageFile &item, const QString &fileName,
                    const QSize &originalSize) {
    item.setFolderPath(QStringLiteral("/virtual/viewer-cache-tests"));
    item.setFileName(fileName);
    item.setIsImage(true);
    item.setFullSize(originalSize);

    ImageInfo info;
    info.path = item.fullPath();
    info.imageSize = originalSize;
    info.orientation = ExifOrientation::Horizontal;
    item.setInfo(info);
}

QString imageIdFromUrl(const QString &url) {
    return url.section(QLatin1Char('/'), -1);
}

ViewerImageCache::StoredImage storeFrame(
    ViewerImageCache &cache, const ImageDecodeRequest &request,
    const QColor &color = QColor(31, 127, 223)) {
    QImage decoded(request.targetSize, QImage::Format_RGBA8888);
    decoded.fill(color);
    DecodedImageInfo decodedInfo;
    decodedInfo.decoderUsed = QStringLiteral("test-source");
    return cache.storeDecodedImage(request, decoded, decodedInfo);
}
}

class ViewerImageCacheTest : public QObject {
    Q_OBJECT

private slots:
    void fitRequestReusesFullTierWhenImageFitsViewport() {
        const QSize originalSize(640, 480);
        const QSize viewportSize(2048, 1536);

        ImageFile item;
        configureImage(item, QStringLiteral("small.png"), originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("small-fit-"), store);

        const ViewerImageCache::RequestPlan initialPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(initialPlan.cachedImages.size(), 0);
        QCOMPARE(initialPlan.decodeRequests.size(), 1);
        QCOMPARE(initialPlan.decodeRequests.first().targetSize, originalSize);

        QImage decoded(originalSize, QImage::Format_RGBA8888);
        decoded.fill(QColor(31, 127, 223));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("test-source");

        const ViewerImageCache::StoredImage stored =
            cache.storeDecodedImage(initialPlan.decodeRequests.first(),
                                    decoded, sourceInfo);
        QVERIFY(stored.accepted);
        QCOMPARE(stored.level, 1);
        QVERIFY(stored.url.startsWith(
            QStringLiteral("image://thumbnails/")));
        QCOMPARE(store->snapshot(imageIdFromUrl(stored.url)).size(),
                 originalSize);

        const ViewerImageCache::RequestPlan repeatedFitPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(repeatedFitPlan.decodeRequests.size(), 0);
        QCOMPARE(repeatedFitPlan.cachedImages.size(), 1);
        QCOMPARE(repeatedFitPlan.cachedImages.first().first, stored.url);
        QCOMPARE(repeatedFitPlan.cachedImages.first().second, 1);

        const ViewerImageCache::RequestPlan nativePlan =
            cache.planRequest(items, 0, QSize(), 1);
        QCOMPARE(nativePlan.decodeRequests.size(), 0);
        QCOMPARE(nativePlan.cachedImages.size(), 1);
        QCOMPARE(nativePlan.cachedImages.first().second, 2);
        QVERIFY(nativePlan.cachedImages.first().first.startsWith(
            QStringLiteral("image://async/")));
        QCOMPARE(imageIdFromUrl(nativePlan.cachedImages.first().first),
                 imageIdFromUrl(stored.url));
    }

    void returnNavigationToSmallImagePrefersReadyNativeFrame() {
        const QSize originalSize(640, 480);
        const QSize viewportSize(2048, 1536);

        ImageFile firstItem;
        configureImage(firstItem, QStringLiteral("first-small.png"),
                       originalSize);
        firstItem.setImageId(QStringLiteral("lossy-first-thumbnail"));

        ImageFile secondItem;
        configureImage(secondItem, QStringLiteral("second-small.png"),
                       originalSize);
        secondItem.setImageId(QStringLiteral("lossy-second-thumbnail"));

        const QList<ImageFile *> items{&firstItem, &secondItem};
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("return-small-"), store);

        const ViewerImageCache::RequestPlan firstPlan =
            cache.planRequest(items, 0, viewportSize, 2);
        QCOMPARE(firstPlan.decodeRequests.size(), 2);
        QVERIFY(ViewerImageCache::isFullSizeRequest(
            firstPlan.decodeRequests.first()));

        QImage firstNative(originalSize, QImage::Format_RGBA8888);
        firstNative.fill(QColor(31, 127, 223));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage firstStored =
            cache.storeDecodedImage(firstPlan.decodeRequests.first(),
                                    firstNative, sourceInfo);
        QVERIFY(firstStored.accepted);
        QCOMPARE(firstStored.level, 1);

        const ViewerImageCache::RequestPlan secondPlan =
            cache.planRequest(items, 1, viewportSize, 2);
        QCOMPARE(secondPlan.decodeRequests.size(), 1);
        QVERIFY(ViewerImageCache::isFullSizeRequest(
            secondPlan.decodeRequests.first()));

        QImage secondNative(originalSize, QImage::Format_RGBA8888);
        secondNative.fill(QColor(47, 191, 95));
        const ViewerImageCache::StoredImage secondStored =
            cache.storeDecodedImage(secondPlan.decodeRequests.first(),
                                    secondNative, sourceInfo);
        QVERIFY(secondStored.accepted);

        const ViewerImageCache::RequestPlan returnPlan =
            cache.planRequest(items, 0, viewportSize, 2);
        QCOMPARE(returnPlan.decodeRequests.size(), 0);
        QCOMPARE(returnPlan.cachedImages.size(), 1);
        QCOMPARE(returnPlan.cachedImages.first().first, firstStored.url);
        QCOMPARE(returnPlan.cachedImages.first().second, 1);

        const QString blurryThumbnailUrl = firstItem.imageIdUrl();
        QVERIFY(!blurryThumbnailUrl.isEmpty());
        QVERIFY(blurryThumbnailUrl != firstStored.url);
        const QString nativeAsyncUrl = cache.bestImageUrl(&firstItem);
        QVERIFY(nativeAsyncUrl.startsWith(
            QStringLiteral("image://async/")));
        QCOMPARE(imageIdFromUrl(nativeAsyncUrl),
                 imageIdFromUrl(firstStored.url));
    }

    void fitRequestReusesViewerTierForLargeImage() {
        const QSize originalSize(4000, 3000);
        const QSize viewportSize(1000, 750);

        ImageFile item;
        configureImage(item, QStringLiteral("large.png"), originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("large-fit-"), store);

        const ViewerImageCache::RequestPlan initialPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(initialPlan.cachedImages.size(), 0);
        QCOMPARE(initialPlan.decodeRequests.size(), 1);
        QCOMPARE(initialPlan.decodeRequests.first().targetSize, viewportSize);

        QImage decoded(viewportSize, QImage::Format_RGBA8888);
        decoded.fill(QColor(47, 191, 95));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("test-source");

        const ViewerImageCache::StoredImage stored =
            cache.storeDecodedImage(initialPlan.decodeRequests.first(),
                                    decoded, sourceInfo);
        QVERIFY(stored.accepted);
        QCOMPARE(stored.level, 1);

        const ViewerImageCache::RequestPlan repeatedFitPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(repeatedFitPlan.decodeRequests.size(), 0);
        QCOMPARE(repeatedFitPlan.cachedImages.size(), 1);
        QCOMPARE(repeatedFitPlan.cachedImages.first().first, stored.url);
        QCOMPARE(repeatedFitPlan.cachedImages.first().second, 1);
    }

    void undersizedNativeDecodeStaysFallbackAndIsRetried() {
        const QSize originalSize(4000, 3000);
        const QSize viewportSize(1000, 750);

        ImageFile item;
        configureImage(item, QStringLiteral("undersized-native.heic"),
                       originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("undersized-native-"), store);

        const ViewerImageCache::RequestPlan fitPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(fitPlan.decodeRequests.size(), 1);
        const ViewerImageCache::StoredImage fitStored =
            storeFrame(cache, fitPlan.decodeRequests.first());
        QVERIFY(fitStored.presentable);
        QCOMPARE(fitStored.level, 1);

        const ViewerImageCache::RequestPlan nativePlan =
            cache.planRequest(items, 0, QSize(), 1);
        QCOMPARE(nativePlan.cachedImages.size(), 1);
        QCOMPARE(nativePlan.cachedImages.first().first, fitStored.url);
        QCOMPARE(nativePlan.decodeRequests.size(), 1);
        const ImageDecodeRequest nativeRequest =
            nativePlan.decodeRequests.first();
        QCOMPARE(nativeRequest.targetSize, originalSize);

        // A decoder may return a useful embedded preview for a native request.
        // Retain it provisionally, but never advertise it as the level-2/native
        // texture and never let it suppress the required source re-decode.
        QImage embeddedPreview(240, 180, QImage::Format_RGBA8888);
        embeddedPreview.fill(Qt::magenta);
        DecodedImageInfo previewInfo;
        previewInfo.decoderUsed = QStringLiteral("embedded-preview");
        previewInfo.previewUsed = QStringLiteral("HEIC thumbnail");
        const ViewerImageCache::StoredImage provisional =
            cache.storeDecodedImage(nativeRequest, embeddedPreview,
                                    previewInfo);
        QVERIFY(provisional.accepted);
        QVERIFY(!provisional.presentable);
        QVERIFY(provisional.url.isEmpty());

        const auto nativeSources = cache.imageSources(&item, QSize());
        QCOMPARE(nativeSources.size(), 1);
        QCOMPARE(nativeSources.first().first, fitStored.url);
        QCOMPARE(nativeSources.first().second, 1);

        const ViewerImageCache::RequestPlan retryPlan =
            cache.planRequest(items, 0, QSize(), 1);
        QCOMPARE(retryPlan.cachedImages.size(), 1);
        QCOMPARE(retryPlan.cachedImages.first().first, fitStored.url);
        QCOMPARE(retryPlan.decodeRequests.size(), 1);
        QCOMPARE(retryPlan.decodeRequests.first().targetSize, originalSize);
    }

    void largerFitRequestDoesNotPresentUndersizedCachedTier() {
        const QSize originalSize(4000, 3000);
        const QSize firstViewportSize(800, 600);
        const QSize enlargedViewportSize(1600, 1200);

        ImageFile item;
        configureImage(item, QStringLiteral("viewer-resize.png"),
                       originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("viewer-resize-"), store);

        const ViewerImageCache::RequestPlan firstPlan =
            cache.planRequest(items, 0, firstViewportSize, 1);
        QCOMPARE(firstPlan.decodeRequests.size(), 1);
        QCOMPARE(firstPlan.decodeRequests.first().targetSize,
                 firstViewportSize);
        const ViewerImageCache::StoredImage firstStored =
            storeFrame(cache, firstPlan.decodeRequests.first());
        QVERIFY(firstStored.accepted);
        QCOMPARE(store->snapshot(imageIdFromUrl(firstStored.url)).size(),
                 firstViewportSize);

        const ViewerImageCache::RequestPlan enlargedPlan =
            cache.planRequest(items, 0, enlargedViewportSize, 1);

        // The old Fit frame may remain retained while its replacement is
        // decoded, but it must not be advertised as satisfying the larger
        // viewer. Doing so produces the visible blur-to-sharp flash.
        QCOMPARE(enlargedPlan.cachedImages.size(), 0);
        QCOMPARE(cache.imageSources(&item, enlargedViewportSize).size(), 0);
        QCOMPARE(enlargedPlan.decodeRequests.size(), 1);
        QCOMPARE(enlargedPlan.decodeRequests.first().targetSize,
                 enlargedViewportSize);

        const ViewerImageCache::StoredImage enlargedStored =
            storeFrame(cache, enlargedPlan.decodeRequests.first(), Qt::green);
        QVERIFY(enlargedStored.accepted);
        QVERIFY(enlargedStored.url != firstStored.url);
        QCOMPARE(store->snapshot(imageIdFromUrl(enlargedStored.url)).size(),
                 enlargedViewportSize);

        const ViewerImageCache::RequestPlan repeatedPlan =
            cache.planRequest(items, 0, enlargedViewportSize, 1);
        QCOMPARE(repeatedPlan.decodeRequests.size(), 0);
        QCOMPARE(repeatedPlan.cachedImages.size(), 1);
        QCOMPARE(repeatedPlan.cachedImages.first().first, enlargedStored.url);
        QCOMPARE(repeatedPlan.cachedImages.first().second, 1);
    }

    void largerFitFrameSatisfiesSmallerTargetWithoutRedecode() {
        const QSize originalSize(4000, 3000);
        const QSize largeViewportSize(1600, 1200);
        const QSize smallViewportSize(800, 600);

        ImageFile item;
        configureImage(item, QStringLiteral("viewer-shrink.png"),
                       originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("viewer-shrink-"), store);

        const ViewerImageCache::RequestPlan largePlan =
            cache.planRequest(items, 0, largeViewportSize, 1);
        QCOMPARE(largePlan.decodeRequests.size(), 1);
        const ImageDecodeRequest largeRequest =
            largePlan.decodeRequests.first();
        const ViewerImageCache::StoredImage largeStored =
            storeFrame(cache, largeRequest, Qt::blue);
        QVERIFY(largeStored.accepted);
        QCOMPARE(cache.entryForPath(item.fullPath(), false).requestedSize,
                 largeViewportSize);

        const ViewerImageCache::RequestPlan smallPlan =
            cache.planRequest(items, 0, smallViewportSize, 1);
        QCOMPARE(smallPlan.cachedImages.size(), 1);
        QCOMPARE(smallPlan.cachedImages.first().first, largeStored.url);
        QCOMPARE(smallPlan.cachedImages.first().second, 1);
        QCOMPARE(smallPlan.decodeRequests.size(), 0);

        const auto smallSources =
            cache.imageSources(&item, smallViewportSize);
        QCOMPARE(smallSources.size(), 1);
        QCOMPARE(smallSources.first().first, largeStored.url);
        QCOMPARE(smallSources.first().second, 1);

        const ViewerImageCache::Entry retainedEntry =
            cache.entryForPath(item.fullPath(), false);
        QCOMPARE(retainedEntry.requestedSize, largeViewportSize);
        QCOMPARE(retainedEntry.image.size(), largeViewportSize);
        QVERIFY(!store->snapshot(imageIdFromUrl(largeStored.url)).isNull());

        // A native request still sees the retained Fit frame as its immediate
        // fallback. Native planning remains a separate tier and schedules the
        // full-resolution texture independently.
        const ViewerImageCache::RequestPlan nativePlan =
            cache.planRequest(items, 0, QSize(), 1);
        QCOMPARE(nativePlan.cachedImages.size(), 1);
        QCOMPARE(nativePlan.cachedImages.first().first, largeStored.url);
        QCOMPARE(nativePlan.cachedImages.first().second, 1);
        QCOMPARE(nativePlan.decodeRequests.size(), 1);
    }

    void staleFitCompletionUsesLatestTargetCoverage() {
        const QSize originalSize(4000, 3000);
        const QSize largeViewportSize(1600, 1200);
        const QSize smallViewportSize(800, 600);

        ImageFile item;
        configureImage(item, QStringLiteral("viewer-stale-resize.png"),
                       originalSize);
        const QList<ImageFile *> items{&item};

        // A smaller decode completing after the planned target grew is stale
        // and undersized. It must neither enter the cache nor be published.
        {
            const auto store =
                QSharedPointer<ProviderImageStore>::create();
            ViewerImageCache cache(QStringLiteral("viewer-grow-"), store);

            const ViewerImageCache::RequestPlan smallPlan =
                cache.planRequest(items, 0, smallViewportSize, 1);
            QCOMPARE(smallPlan.decodeRequests.size(), 1);
            const ImageDecodeRequest smallRequest =
                smallPlan.decodeRequests.first();

            const ViewerImageCache::RequestPlan largePlan =
                cache.planRequest(items, 0, largeViewportSize, 1);
            QCOMPARE(largePlan.decodeRequests.size(), 1);
            const ImageDecodeRequest largeRequest =
                largePlan.decodeRequests.first();

            const ViewerImageCache::StoredImage staleSmall =
                storeFrame(cache, smallRequest, Qt::red);
            QVERIFY(!staleSmall.accepted);
            QVERIFY(!staleSmall.presentable);
            QCOMPARE(cache.viewerImageCount(), 0);
            QCOMPARE(cache.imageSources(&item, largeViewportSize).size(), 0);

            const ViewerImageCache::StoredImage largeStored =
                storeFrame(cache, largeRequest, Qt::green);
            QVERIFY(largeStored.accepted);
            QVERIFY(largeStored.presentable);
            QCOMPARE(cache.imageSources(&item, largeViewportSize).size(), 1);
        }

        // A larger decode completing after the planned target shrank covers
        // that target, so it is immediately usable. A later completion of the
        // redundant smaller request must not replace the retained larger frame.
        {
            const auto store =
                QSharedPointer<ProviderImageStore>::create();
            ViewerImageCache cache(QStringLiteral("viewer-shrink-"), store);

            const ViewerImageCache::RequestPlan largePlan =
                cache.planRequest(items, 0, largeViewportSize, 1);
            QCOMPARE(largePlan.decodeRequests.size(), 1);
            const ImageDecodeRequest largeRequest =
                largePlan.decodeRequests.first();

            const ViewerImageCache::RequestPlan smallPlan =
                cache.planRequest(items, 0, smallViewportSize, 1);
            QCOMPARE(smallPlan.decodeRequests.size(), 1);
            const ImageDecodeRequest smallRequest =
                smallPlan.decodeRequests.first();

            const ViewerImageCache::StoredImage staleLarge =
                storeFrame(cache, largeRequest, Qt::blue);
            QVERIFY(staleLarge.accepted);
            QVERIFY(staleLarge.presentable);
            const QString retainedImageId = imageIdFromUrl(staleLarge.url);

            const auto smallSources =
                cache.imageSources(&item, smallViewportSize);
            QCOMPARE(smallSources.size(), 1);
            QCOMPARE(smallSources.first().first, staleLarge.url);
            QCOMPARE(smallSources.first().second, 1);

            const ViewerImageCache::StoredImage redundantSmall =
                storeFrame(cache, smallRequest, Qt::green);
            QVERIFY(!redundantSmall.accepted);
            const ViewerImageCache::Entry retainedEntry =
                cache.entryForPath(item.fullPath(), false);
            QCOMPARE(retainedEntry.imageId, retainedImageId);
            QCOMPARE(retainedEntry.requestedSize, largeViewportSize);
            QCOMPARE(retainedEntry.image.size(), largeViewportSize);
            QCOMPARE(retainedEntry.image.pixelColor(0, 0), QColor(Qt::blue));
            QVERIFY(!store->snapshot(retainedImageId).isNull());
        }
    }

    void adjacentPreparedFitIsImmediatelyPresentedAtRequestedResolution() {
        const QSize originalSize(4032, 3024);
        const QSize viewportSize(1600, 1000);
        const QSize expectedFitSize =
            originalSize.scaled(viewportSize, Qt::KeepAspectRatio);

        ImageFile current;
        configureImage(current, QStringLiteral("current.png"), originalSize);
        current.setImageId(QStringLiteral("current-grid-thumbnail"));

        ImageFile adjacent;
        configureImage(adjacent, QStringLiteral("adjacent.png"),
                       originalSize);
        adjacent.setImageId(QStringLiteral("adjacent-grid-thumbnail"));

        const QList<ImageFile *> items{&current, &adjacent};
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("adjacent-fit-"), store);

        const ViewerImageCache::RequestPlan predecodePlan =
            cache.planRequest(items, 0, viewportSize, 2);
        QCOMPARE(predecodePlan.decodeRequests.size(), 2);
        const auto adjacentRequestIt = std::find_if(
            predecodePlan.decodeRequests.cbegin(),
            predecodePlan.decodeRequests.cend(),
            [&adjacent](const ImageDecodeRequest &request) {
                return request.info.path == adjacent.fullPath();
            });
        QVERIFY(adjacentRequestIt != predecodePlan.decodeRequests.cend());
        QCOMPARE(adjacentRequestIt->targetSize, expectedFitSize);

        const ViewerImageCache::StoredImage adjacentStored =
            storeFrame(cache, *adjacentRequestIt, Qt::green);
        QVERIFY(adjacentStored.accepted);
        QCOMPARE(adjacentStored.level, 1);
        QCOMPARE(store->snapshot(imageIdFromUrl(adjacentStored.url)).size(),
                 expectedFitSize);

        const ViewerImageCache::RequestPlan navigationPlan =
            cache.planRequest(items, 1, viewportSize, 2);
        QCOMPARE(navigationPlan.cachedImages.size(), 1);
        QCOMPARE(navigationPlan.cachedImages.first().first,
                 adjacentStored.url);
        QCOMPARE(navigationPlan.cachedImages.first().second, 1);
        const bool adjacentWouldDecodeAgain = std::any_of(
            navigationPlan.decodeRequests.cbegin(),
            navigationPlan.decodeRequests.cend(),
            [&adjacent](const ImageDecodeRequest &request) {
                return request.info.path == adjacent.fullPath();
            });
        QVERIFY(!adjacentWouldDecodeAgain);

        const auto adjacentSources =
            cache.imageSources(&adjacent, viewportSize);
        QCOMPARE(adjacentSources.size(), 2);
        QCOMPARE(adjacentSources.first().first, adjacent.imageIdUrl());
        QCOMPARE(adjacentSources.first().second, 0);
        QCOMPARE(adjacentSources.constLast().first, adjacentStored.url);
        QCOMPARE(adjacentSources.constLast().second, 1);
    }

    void equalSizeSourceDecodeReplacesCachedLossyEntry() {
        const QSize originalSize(4000, 3000);
        const QSize viewportSize(1000, 750);

        ImageFile item;
        configureImage(item, QStringLiteral("same-size.png"), originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("same-size-"), store);
        const ViewerImageCache::RequestPlan plan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(plan.decodeRequests.size(), 1);
        const ImageDecodeRequest request = plan.decodeRequests.first();

        QImage cachedLossy(viewportSize, QImage::Format_RGBA8888);
        cachedLossy.fill(QColor(191, 31, 47));
        DecodedImageInfo cachedInfo;
        cachedInfo.decoderUsed = QStringLiteral("persistent-cache");
        cachedInfo.isFromCache = true;

        const ViewerImageCache::StoredImage cachedStored =
            cache.storeDecodedImage(request, cachedLossy, cachedInfo);
        QVERIFY(cachedStored.accepted);
        QCOMPARE(cachedStored.level, 1);

        const ViewerImageCache::RequestPlan provisionalPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(provisionalPlan.cachedImages.size(), 1);
        QCOMPARE(provisionalPlan.cachedImages.first().first,
                 cachedStored.url);
        QCOMPARE(provisionalPlan.decodeRequests.size(), 1);

        QImage sourceDecode(viewportSize, QImage::Format_RGBA8888);
        sourceDecode.fill(QColor(31, 191, 79));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("source-decoder");
        sourceInfo.isFromCache = false;

        const ViewerImageCache::StoredImage sourceStored =
            cache.storeDecodedImage(request, sourceDecode, sourceInfo);
        QVERIFY2(sourceStored.accepted,
                 "An equal-size source decode must replace a lossy cached image");
        QCOMPARE(sourceStored.level, 1);
        QVERIFY(sourceStored.url != cachedStored.url);

        const ViewerImageCache::Entry entry =
            cache.entryForPath(item.fullPath(), false);
        QVERIFY(!entry.decodedInfo.isFromCache);
        QCOMPARE(entry.decodedInfo.decoderUsed,
                 QStringLiteral("source-decoder"));
        QCOMPARE(entry.image.pixelColor(0, 0), QColor(31, 191, 79));
        QVERIFY(store->snapshot(imageIdFromUrl(cachedStored.url)).isNull());
        QCOMPARE(store->snapshot(imageIdFromUrl(sourceStored.url))
                     .pixelColor(0, 0),
                 QColor(31, 191, 79));

        const ViewerImageCache::StoredImage lateCached =
            cache.storeDecodedImage(request, cachedLossy, cachedInfo);
        QVERIFY2(!lateCached.accepted,
                 "A late cache result must not replace a source decode");
        QCOMPARE(cache.entryForPath(item.fullPath(), false)
                     .decodedInfo.decoderUsed,
                 QStringLiteral("source-decoder"));
        QCOMPARE(store->snapshot(imageIdFromUrl(sourceStored.url))
                     .pixelColor(0, 0),
                 QColor(31, 191, 79));
    }

    void equalSizeSourceDecodeReplacesCachedLossyFullSizeEntry() {
        const QSize originalSize(640, 480);
        const QSize viewportSize(2048, 1536);

        ImageFile item;
        configureImage(item, QStringLiteral("small-cached.png"),
                       originalSize);
        const QList<ImageFile *> items{&item};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("small-cached-"), store);
        const ViewerImageCache::RequestPlan plan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(plan.decodeRequests.size(), 1);
        const ImageDecodeRequest request = plan.decodeRequests.first();
        QVERIFY(ViewerImageCache::isFullSizeRequest(request));

        QImage cachedLossy(originalSize, QImage::Format_RGBA8888);
        cachedLossy.fill(QColor(223, 63, 31));
        DecodedImageInfo cachedInfo;
        cachedInfo.decoderUsed = QStringLiteral("persistent-cache");
        cachedInfo.isFromCache = true;
        const ViewerImageCache::StoredImage cachedStored =
            cache.storeDecodedImage(request, cachedLossy, cachedInfo);
        QVERIFY(cachedStored.accepted);
        QCOMPARE(cachedStored.level, 1);

        const ViewerImageCache::RequestPlan provisionalPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(provisionalPlan.cachedImages.size(), 1);
        QCOMPARE(provisionalPlan.decodeRequests.size(), 1);

        QImage sourceDecode(originalSize, QImage::Format_RGBA8888);
        sourceDecode.fill(QColor(31, 159, 223));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage sourceStored =
            cache.storeDecodedImage(request, sourceDecode, sourceInfo);
        QVERIFY(sourceStored.accepted);
        QCOMPARE(sourceStored.level, 1);
        QCOMPARE(cache.entryForPath(item.fullPath(), true)
                     .image.pixelColor(0, 0),
                 QColor(31, 159, 223));
        QVERIFY(store->snapshot(imageIdFromUrl(cachedStored.url)).isNull());
    }

    void smallerLateSourceDecodeDoesNotReplaceLargerCachedImage() {
        ImageInfo info;
        info.path =
            QStringLiteral("/virtual/viewer-cache-tests/race.png");
        info.imageSize = QSize(4000, 3000);

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("race-"), store);
        const ImageDecodeRequest largeRequest =
            ViewerImageCache::makeRequest(
                info, info.imageSize, QSize(1600, 1200));
        const ImageDecodeRequest smallRequest =
            ViewerImageCache::makeRequest(
                info, info.imageSize, QSize(800, 600));

        QImage cachedLarge(largeRequest.targetSize,
                           QImage::Format_RGBA8888);
        cachedLarge.fill(QColor(127, 31, 191));
        DecodedImageInfo cachedInfo;
        cachedInfo.decoderUsed = QStringLiteral("persistent-cache");
        cachedInfo.isFromCache = true;
        const ViewerImageCache::StoredImage cachedStored =
            cache.storeDecodedImage(largeRequest, cachedLarge, cachedInfo);
        QVERIFY(cachedStored.accepted);

        QImage sourceSmall(smallRequest.targetSize,
                           QImage::Format_RGBA8888);
        sourceSmall.fill(QColor(31, 223, 127));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage sourceStored =
            cache.storeDecodedImage(smallRequest, sourceSmall, sourceInfo);
        QVERIFY2(!sourceStored.accepted,
                 "A smaller late decode must not downgrade the displayed resolution");

        const ViewerImageCache::Entry entry =
            cache.entryForPath(info.path, false);
        QCOMPARE(entry.image.size(), largeRequest.targetSize);
        QVERIFY(entry.decodedInfo.isFromCache);
        QCOMPARE(entry.image.pixelColor(0, 0), QColor(127, 31, 191));
        QVERIFY(!store->snapshot(imageIdFromUrl(cachedStored.url)).isNull());
    }

    void equalSizeSourceRefreshReplacesPreviousSourceEntry() {
        ImageInfo info;
        info.path =
            QStringLiteral("/virtual/viewer-cache-tests/refreshed.png");
        info.imageSize = QSize(640, 480);
        info.lastModified =
            QDateTime::fromMSecsSinceEpoch(1000);
        const ImageDecodeRequest firstRequest =
            ViewerImageCache::makeRequest(info, info.imageSize);

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("refresh-"), store);
        QImage firstImage(info.imageSize, QImage::Format_RGBA8888);
        firstImage.fill(QColor(191, 31, 127));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage firstStored =
            cache.storeDecodedImage(firstRequest, firstImage, sourceInfo);
        QVERIFY(firstStored.accepted);

        info.lastModified =
            QDateTime::fromMSecsSinceEpoch(2000);
        const ImageDecodeRequest refreshedRequest =
            ViewerImageCache::makeRequest(info, info.imageSize);
        QImage refreshedImage(info.imageSize, QImage::Format_RGBA8888);
        refreshedImage.fill(QColor(31, 127, 223));
        const ViewerImageCache::StoredImage refreshedStored =
            cache.storeDecodedImage(refreshedRequest, refreshedImage,
                                    sourceInfo);
        QVERIFY2(refreshedStored.accepted,
                 "A refreshed source image at the same path and size must replace the old pixels");
        QCOMPARE(cache.entryForPath(info.path, true)
                     .image.pixelColor(0, 0),
                 QColor(31, 127, 223));
        QVERIFY(store->snapshot(imageIdFromUrl(firstStored.url)).isNull());
    }

    void changedSourceVersionKeepsOldFrameWhileQueuingRefresh() {
        const QSize originalSize(1280, 720);
        const QSize viewportSize(960, 540);
        ImageFile item;
        configureImage(item, QStringLiteral("watched.png"), originalSize);
        ImageInfo info = item.info();
        info.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
        info.fileSize = 1234;
        item.setInfo(info);

        const QList<ImageFile *> items{&item};
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("watched-"), store);
        const ViewerImageCache::RequestPlan firstPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(firstPlan.decodeRequests.size(), 1);

        QImage firstFrame(firstPlan.decodeRequests.first().targetSize,
                          QImage::Format_RGBA8888);
        firstFrame.fill(QColor(20, 40, 60));
        DecodedImageInfo decodedInfo;
        decodedInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage stored =
            cache.storeDecodedImage(firstPlan.decodeRequests.first(),
                                    firstFrame, decodedInfo);
        QVERIFY(stored.accepted);

        info.lastModified = QDateTime::fromMSecsSinceEpoch(2000);
        info.fileSize = 5678;
        item.setInfo(info);
        const ViewerImageCache::RequestPlan refreshedPlan =
            cache.planRequest(items, 0, viewportSize, 1);

        QCOMPARE(refreshedPlan.cachedImages.size(), 1);
        QCOMPARE(refreshedPlan.cachedImages.first().first, stored.url);
        QCOMPARE(refreshedPlan.decodeRequests.size(), 1);
        QCOMPARE(refreshedPlan.decodeRequests.first().info.lastModified,
                 info.lastModified);
        QCOMPARE(refreshedPlan.decodeRequests.first().info.fileSize,
                 info.fileSize);
    }

    void opaqueNanosecondVersionInvalidatesSameMillisecondAndSize() {
        constexpr qint64 firstVersion = 1'700'000'000'123'456'789LL;
        constexpr qint64 secondVersion = firstVersion + 1;
        static_assert(firstVersion / 1'000'000 ==
                      secondVersion / 1'000'000);

        const QSize originalSize(1280, 720);
        const QSize viewportSize(640, 360);
        ImageFile item;
        configureImage(item, QStringLiteral("opaque-version.png"),
                       originalSize);
        ImageInfo info = item.info();
        info.lastModified = QDateTime::fromMSecsSinceEpoch(
            firstVersion / 1'000'000);
        info.fileSize = 4096;
        info.sourceVersionToken = firstVersion;
        item.setInfo(info);

        const QList<ImageFile *> items{&item};
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("opaque-"), store);
        const ViewerImageCache::RequestPlan firstPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(firstPlan.decodeRequests.size(), 1);

        QImage firstFrame(firstPlan.decodeRequests.first().targetSize,
                          QImage::Format_RGBA8888);
        firstFrame.fill(QColor(24, 48, 72));
        DecodedImageInfo decodedInfo;
        decodedInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage firstStored =
            cache.storeDecodedImage(firstPlan.decodeRequests.first(),
                                    firstFrame, decodedInfo);
        QVERIFY(firstStored.accepted);
        QVERIFY(firstStored.url.contains(
            QStringLiteral("v%1-s4096-").arg(firstVersion)));

        info.sourceVersionToken = secondVersion;
        // QDateTime and file size intentionally remain identical.
        item.setInfo(info);
        const ViewerImageCache::RequestPlan refreshedPlan =
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(refreshedPlan.cachedImages.size(), 1);
        QCOMPARE(refreshedPlan.cachedImages.first().first,
                 firstStored.url);
        QCOMPARE(refreshedPlan.decodeRequests.size(), 1);
        QCOMPARE(refreshedPlan.decodeRequests.first()
                     .info.sourceVersionToken,
                 secondVersion);

        QImage refreshedFrame(
            refreshedPlan.decodeRequests.first().targetSize,
            QImage::Format_RGBA8888);
        refreshedFrame.fill(QColor(72, 48, 24));
        const ViewerImageCache::StoredImage refreshedStored =
            cache.storeDecodedImage(
                refreshedPlan.decodeRequests.first(), refreshedFrame,
                decodedInfo);
        QVERIFY(refreshedStored.accepted);
        QVERIFY(refreshedStored.url != firstStored.url);
        QVERIFY(refreshedStored.url.contains(
            QStringLiteral("v%1-s4096-").arg(secondVersion)));
        QVERIFY(store->snapshot(imageIdFromUrl(firstStored.url)).isNull());
        QCOMPARE(cache.entryForPath(item.fullPath(), false)
                     .sourceVersionToken,
                 secondVersion);
    }

    void knownSourceVersionRefreshesEntryStoredWithoutVersion() {
        const QSize originalSize(800, 600);
        ImageFile item;
        configureImage(item, QStringLiteral("unknown-version.png"),
                       originalSize);

        const QList<ImageFile *> items{&item};
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("unknown-version-"), store);
        const ViewerImageCache::RequestPlan initialPlan =
            cache.planRequest(items, 0, QSize(400, 300), 1);
        QCOMPARE(initialPlan.decodeRequests.size(), 1);

        QImage firstFrame(initialPlan.decodeRequests.first().targetSize,
                          QImage::Format_RGBA8888);
        firstFrame.fill(QColor(80, 100, 120));
        DecodedImageInfo decodedInfo;
        decodedInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage stored =
            cache.storeDecodedImage(initialPlan.decodeRequests.first(),
                                    firstFrame, decodedInfo);
        QVERIFY(stored.accepted);

        ImageInfo knownInfo = item.info();
        knownInfo.lastModified = QDateTime::fromMSecsSinceEpoch(5000);
        knownInfo.fileSize = 4321;
        item.setInfo(knownInfo);
        const ViewerImageCache::RequestPlan refreshPlan =
            cache.planRequest(items, 0, QSize(400, 300), 1);

        QCOMPARE(refreshPlan.cachedImages.size(), 1);
        QCOMPARE(refreshPlan.cachedImages.first().first, stored.url);
        QCOMPARE(refreshPlan.decodeRequests.size(), 1);
        QCOMPARE(refreshPlan.decodeRequests.first().info.lastModified,
                 knownInfo.lastModified);
        QCOMPARE(refreshPlan.decodeRequests.first().info.fileSize,
                 knownInfo.fileSize);
    }

    void changedSourceVersionCanReplaceLargerOldFrame() {
        ImageInfo oldInfo;
        oldInfo.path =
            QStringLiteral("/virtual/viewer-cache-tests/aspect-change.png");
        oldInfo.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
        oldInfo.fileSize = 100;
        oldInfo.imageSize = QSize(1800, 1200);
        const ImageDecodeRequest oldRequest =
            ViewerImageCache::makeRequest(oldInfo, oldInfo.imageSize,
                                          QSize(1200, 1200));
        QCOMPARE(oldRequest.targetSize, QSize(1200, 800));

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("aspect-change-"), store);
        QImage oldFrame(oldRequest.targetSize, QImage::Format_RGBA8888);
        oldFrame.fill(QColor(160, 40, 40));
        DecodedImageInfo decodedInfo;
        decodedInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage oldStored =
            cache.storeDecodedImage(oldRequest, oldFrame, decodedInfo);
        QVERIFY(oldStored.accepted);

        ImageInfo newInfo = oldInfo;
        newInfo.lastModified = QDateTime::fromMSecsSinceEpoch(2000);
        newInfo.fileSize = 200;
        newInfo.imageSize = QSize(1800, 900);
        const ImageDecodeRequest newRequest =
            ViewerImageCache::makeRequest(newInfo, newInfo.imageSize,
                                          QSize(1200, 1200));
        QCOMPARE(newRequest.targetSize, QSize(1200, 600));

        QImage newFrame(newRequest.targetSize, QImage::Format_RGBA8888);
        newFrame.fill(QColor(40, 160, 40));
        const ViewerImageCache::StoredImage newStored =
            cache.storeDecodedImage(newRequest, newFrame, decodedInfo);
        QVERIFY(newStored.accepted);
        QCOMPARE(cache.entryForPath(newInfo.path, false).image.size(),
                 newRequest.targetSize);
        QVERIFY(store->snapshot(imageIdFromUrl(oldStored.url)).isNull());
    }

    void rotatedSmallImageUsesFullSizeTierWithoutUpscaling() {
        ImageFile item;
        item.setFolderPath(
            QStringLiteral("/virtual/viewer-cache-tests"));
        item.setFileName(QStringLiteral("rotated.png"));
        item.setIsImage(true);
        item.setFullSize(QSize(640, 480));

        ImageInfo info;
        info.path = item.fullPath();
        info.imageSize = QSize(480, 640);
        info.orientation = ExifOrientation::Rotate90CW;
        item.setInfo(info);

        const QList<ImageFile *> items{&item};
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("rotated-"), store);
        const ViewerImageCache::RequestPlan initialPlan =
            cache.planRequest(items, 0, QSize(2048, 1536), 1);
        QCOMPARE(initialPlan.decodeRequests.size(), 1);
        QCOMPARE(initialPlan.decodeRequests.first().targetSize,
                 QSize(640, 480));
        QVERIFY(ViewerImageCache::isFullSizeRequest(
            initialPlan.decodeRequests.first()));

        QImage decoded(QSize(640, 480), QImage::Format_RGBA8888);
        decoded.fill(QColor(239, 191, 31));
        DecodedImageInfo sourceInfo;
        sourceInfo.decoderUsed = QStringLiteral("source-decoder");
        const ViewerImageCache::StoredImage stored =
            cache.storeDecodedImage(initialPlan.decodeRequests.first(),
                                    decoded, sourceInfo);
        QVERIFY(stored.accepted);
        QCOMPARE(stored.level, 1);

        const ViewerImageCache::RequestPlan repeatedPlan =
            cache.planRequest(items, 0, QSize(2048, 1536), 1);
        QCOMPARE(repeatedPlan.decodeRequests.size(), 0);
        QCOMPARE(repeatedPlan.cachedImages.size(), 1);
        QCOMPARE(repeatedPlan.cachedImages.first().second, 1);
    }

    void defaultAndConfiguredByteBudgetsAreReported() {
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache defaultCache(QStringLiteral("default-budget-"),
                                      store);
        QCOMPARE(defaultCache.byteBudget(),
                 ViewerImageCache::DefaultByteBudget);
        QCOMPARE(defaultCache.fitByteBudget(),
                 ViewerImageCache::DefaultFitByteBudget);
        QCOMPARE(defaultCache.nativeByteBudget(),
                 ViewerImageCache::DefaultNativeByteBudget);

        constexpr qint64 customFitBudget = 123456;
        constexpr qint64 customNativeBudget = 654321;
        ViewerImageCache configuredCache(
            QStringLiteral("configured-budget-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            customFitBudget, customNativeBudget);
        QCOMPARE(configuredCache.fitByteBudget(), customFitBudget);
        QCOMPARE(configuredCache.nativeByteBudget(), customNativeBudget);
        configuredCache.setByteBudgets(customFitBudget / 2,
                                       customNativeBudget / 3);
        QCOMPARE(configuredCache.fitByteBudget(), customFitBudget / 2);
        QCOMPARE(configuredCache.nativeByteBudget(),
                 customNativeBudget / 3);
    }

    void navigationRetainsDecodedFramesUntilBudgetPressure() {
        constexpr int side = 50;
        const QSize originalSize(100, 100);
        const QSize viewportSize(side, side);

        ImageFile zero;
        ImageFile one;
        ImageFile two;
        ImageFile three;
        ImageFile four;
        configureImage(zero, QStringLiteral("zero.png"), originalSize);
        configureImage(one, QStringLiteral("one.png"), originalSize);
        configureImage(two, QStringLiteral("two.png"), originalSize);
        configureImage(three, QStringLiteral("three.png"), originalSize);
        configureImage(four, QStringLiteral("four.png"), originalSize);
        const QList<ImageFile *> items{
            &zero, &one, &two, &three, &four};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        constexpr qint64 frameBytes = side * side * 4;
        ViewerImageCache cache(
            QStringLiteral("plan-retain-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            frameBytes * 5, ViewerImageCache::DefaultNativeByteBudget);
        const ViewerImageCache::RequestPlan firstPlan =
            cache.planRequest(items, 1, viewportSize, 3);
        QCOMPARE(firstPlan.decodeRequests.size(), 3);

        QStringList oldImageIds;
        for (const ImageDecodeRequest &request :
             firstPlan.decodeRequests) {
            const ViewerImageCache::StoredImage stored =
                storeFrame(cache, request);
            QVERIFY(stored.accepted);
            oldImageIds.append(imageIdFromUrl(stored.url));
        }
        QCOMPARE(cache.viewerImageCount(), 3);

        const ViewerImageCache::RequestPlan secondPlan =
            cache.planRequest(items, 4, viewportSize, 2);
        QCOMPARE(secondPlan.decodeRequests.size(), 2);
        // Changing the active predecode plan must not throw away frames that
        // have already completed. They remain instant to revisit until real
        // memory pressure requires eviction.
        QCOMPARE(cache.viewerImageCount(), 3);
        QCOMPARE(cache.fitRetainedBytes(), frameBytes * 3);
        for (const QString &imageId : std::as_const(oldImageIds)) {
            QVERIFY(!store->snapshot(imageId).isNull());
        }

        const ViewerImageCache::StoredImage staleResult =
            storeFrame(cache, firstPlan.decodeRequests.first());
        QVERIFY2(!staleResult.accepted,
                 "A decode from the previous plan must not refill the cache");
        QCOMPARE(cache.fitRetainedBytes(), frameBytes * 3);

        for (const ImageDecodeRequest &request :
             secondPlan.decodeRequests) {
            QVERIFY(storeFrame(cache, request).accepted);
        }
        QCOMPARE(cache.viewerImageCount(), 5);
        QCOMPARE(cache.fitRetainedBytes(), frameBytes * 5);

        cache.setByteBudgets(frameBytes * 2,
                             ViewerImageCache::DefaultNativeByteBudget);
        QCOMPARE(cache.viewerImageCount(), 2);
        QCOMPARE(cache.fitRetainedBytes(), frameBytes * 2);
        QVERIFY(!cache.entryForPath(four.fullPath(), false).image.isNull());
        QVERIFY(!cache.entryForPath(three.fullPath(), false).image.isNull());
        QVERIFY(cache.entryForPath(zero.fullPath(), false).image.isNull());
        QVERIFY(cache.entryForPath(one.fullPath(), false).image.isNull());
        QVERIFY(cache.entryForPath(two.fullPath(), false).image.isNull());
    }

    void supplementalTransitionFrameDoesNotReplacePrimaryPlan() {
        const QSize originalSize(100, 100);
        const QSize viewportSize(50, 50);
        ImageFile zero;
        ImageFile one;
        ImageFile two;
        ImageFile three;
        configureImage(zero, QStringLiteral("supplement-zero.png"),
                       originalSize);
        configureImage(one, QStringLiteral("supplement-one.png"),
                       originalSize);
        configureImage(two, QStringLiteral("supplement-two.png"),
                       originalSize);
        configureImage(three, QStringLiteral("supplement-three.png"),
                       originalSize);
        const QList<ImageFile *> items{&zero, &one, &two, &three};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(QStringLiteral("supplement-"), store);
        const auto primary = cache.planRequest(items, 0, viewportSize, 2);
        QCOMPARE(primary.decodeRequests.size(), 2);
        for (const ImageDecodeRequest &request : primary.decodeRequests) {
            QVERIFY(storeFrame(cache, request).accepted);
        }

        const auto supplemental =
            cache.planRequest(items, 3, viewportSize, 1);
        QCOMPARE(supplemental.decodeRequests.size(), 1);

        // An ordinary metadata/viewport refresh of the active plan can race
        // with this supplemental decode. It must not turn the in-flight
        // transition result into stale work.
        cache.planRequest(items, 0, viewportSize, 2);
        QVERIFY(storeFrame(cache, supplemental.decodeRequests.first())
                    .accepted);
        QCOMPARE(cache.viewerImageCount(), 3);
        QVERIFY(!cache.entryForPath(zero.fullPath(), false).image.isNull());
        QVERIFY(!cache.entryForPath(one.fullPath(), false).image.isNull());
        QVERIFY(!cache.entryForPath(three.fullPath(), false).image.isNull());
    }

    void byteBudgetEvictsFarthestPrefetchFramesFirst() {
        constexpr int side = 50;
        constexpr qint64 frameBytes = side * side * 4;
        constexpr qint64 budget = frameBytes * 3;
        const QSize originalSize(100, 100);
        const QSize viewportSize(side, side);

        ImageFile zero;
        ImageFile one;
        ImageFile two;
        ImageFile three;
        ImageFile four;
        configureImage(zero, QStringLiteral("budget-zero.png"),
                       originalSize);
        configureImage(one, QStringLiteral("budget-one.png"),
                       originalSize);
        configureImage(two, QStringLiteral("budget-two.png"),
                       originalSize);
        configureImage(three, QStringLiteral("budget-three.png"),
                       originalSize);
        configureImage(four, QStringLiteral("budget-four.png"),
                       originalSize);
        const QList<ImageFile *> items{
            &zero, &one, &two, &three, &four};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(
            QStringLiteral("budget-evict-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            budget);
        const ViewerImageCache::RequestPlan plan =
            cache.planRequest(items, 2, viewportSize, 5);
        QCOMPARE(plan.decodeRequests.size(), 5);

        QList<ViewerImageCache::StoredImage> stored;
        for (const ImageDecodeRequest &request : plan.decodeRequests) {
            stored.append(storeFrame(cache, request));
        }

        QCOMPARE(cache.retainedBytes(), budget);
        QCOMPARE(cache.viewerImageCount(), 3);
        QVERIFY(!cache.entryForPath(two.fullPath(), false).image.isNull());
        QVERIFY(!cache.entryForPath(one.fullPath(), false).image.isNull());
        QVERIFY(!cache.entryForPath(three.fullPath(), false).image.isNull());
        QVERIFY(cache.entryForPath(zero.fullPath(), false).image.isNull());
        QVERIFY(cache.entryForPath(four.fullPath(), false).image.isNull());
        QVERIFY(stored.at(0).accepted);
        QVERIFY(stored.at(1).accepted);
        QVERIFY(stored.at(2).accepted);
        QVERIFY(!stored.at(3).accepted);
        QVERIFY(!stored.at(4).accepted);
    }

    void oversizedCurrentFrameIsNeverEvicted() {
        constexpr qint64 budget = 10'000;
        const QSize nativeSize(100, 100);

        ImageFile current;
        ImageFile neighbor;
        configureImage(current, QStringLiteral("oversized-current.png"),
                       nativeSize);
        configureImage(neighbor, QStringLiteral("oversized-neighbor.png"),
                       nativeSize);
        const QList<ImageFile *> items{&current, &neighbor};

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(
            QStringLiteral("oversized-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            budget);
        const ViewerImageCache::RequestPlan plan =
            cache.planRequest(items, 0, nativeSize, 2);
        QCOMPARE(plan.decodeRequests.size(), 2);

        const ViewerImageCache::StoredImage currentStored =
            storeFrame(cache, plan.decodeRequests.at(0), Qt::red);
        QVERIFY(currentStored.accepted);
        QVERIFY(cache.retainedBytes() > cache.byteBudget());
        QVERIFY(!store->snapshot(
            imageIdFromUrl(currentStored.url)).isNull());

        const ViewerImageCache::StoredImage neighborStored =
            storeFrame(cache, plan.decodeRequests.at(1), Qt::green);
        QVERIFY(!neighborStored.accepted);
        QCOMPARE(cache.fullSizeImageCount(), 1);
        QVERIFY(!cache.entryForPath(current.fullPath(), true).image.isNull());
        QVERIFY(cache.entryForPath(neighbor.fullPath(), true).image.isNull());
        QCOMPARE(cache.retainedBytes(),
                 static_cast<qint64>(nativeSize.width()) *
                     nativeSize.height() * 4);
    }

    void oversizedNativeCurrentPreservesSixteenFitFrames() {
        constexpr int itemCount = 17;
        constexpr int currentIndex = 8;
        const QSize nativeSize(100, 100);
        const QSize fitSize(10, 10);
        const qint64 fitFrameBytes =
            static_cast<qint64>(fitSize.width()) * fitSize.height() * 4;
        const qint64 nativeFrameBytes =
            static_cast<qint64>(nativeSize.width()) * nativeSize.height() * 4;
        const qint64 fitBudget = fitFrameBytes * 16;
        const qint64 nativeBudget = nativeFrameBytes / 2;

        std::vector<std::unique_ptr<ImageFile>> storage;
        QList<ImageFile *> items;
        storage.reserve(itemCount);
        items.reserve(itemCount);
        for (int index = 0; index < itemCount; ++index) {
            auto item = std::make_unique<ImageFile>();
            configureImage(*item,
                           QStringLiteral("huge-native-%1.png").arg(index),
                           nativeSize);
            items.append(item.get());
            storage.push_back(std::move(item));
        }

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(
            QStringLiteral("separate-tiers-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            fitBudget, nativeBudget);
        const auto fitPlan =
            cache.planRequest(items, currentIndex, fitSize, 16);
        QCOMPARE(fitPlan.decodeRequests.size(), 16);
        QStringList fitPaths;
        for (const ImageDecodeRequest &request : fitPlan.decodeRequests) {
            fitPaths.append(request.info.path);
            QVERIFY(storeFrame(cache, request).accepted);
        }
        QCOMPARE(cache.viewerImageCount(), 16);
        QCOMPARE(cache.fitRetainedBytes(), fitBudget);
        QCOMPARE(cache.nativeRetainedBytes(), 0);

        const auto nativePlan =
            cache.planRequest(items, currentIndex, QSize(), 1);
        QCOMPARE(nativePlan.decodeRequests.size(), 1);
        QVERIFY(ViewerImageCache::isFullSizeRequest(
            nativePlan.decodeRequests.first()));
        const auto nativeStored =
            storeFrame(cache, nativePlan.decodeRequests.first(), Qt::red);
        QVERIFY(nativeStored.accepted);
        QCOMPARE(cache.fullSizeImageCount(), 1);
        QCOMPARE(cache.nativeRetainedBytes(), nativeFrameBytes);
        QVERIFY(cache.nativeRetainedBytes() > cache.nativeByteBudget());
        QCOMPARE(cache.retainedBytes(), fitBudget + nativeFrameBytes);

        // The oversized current native frame is pinned in its own tier. It
        // cannot consume a byte of the Fit budget or evict any neighbor from
        // the prepared navigation sequence.
        QCOMPARE(cache.viewerImageCount(), 16);
        QCOMPARE(cache.fitRetainedBytes(), fitBudget);
        for (const QString &path : std::as_const(fitPaths)) {
            QVERIFY(!cache.entryForPath(path, false).image.isNull());
        }

        // Moving current transfers the single oversized pin. The previous
        // oversized native must be released before the next one is accepted;
        // repeated navigation cannot accumulate over-budget native frames.
        const int nextCurrentIndex = currentIndex - 1;
        const QString oldCurrentPath = items.at(currentIndex)->fullPath();
        const auto nextFitPlan =
            cache.planRequest(items, nextCurrentIndex, fitSize, 16);
        for (const ImageDecodeRequest &request : nextFitPlan.decodeRequests) {
            QVERIFY(storeFrame(cache, request).accepted);
        }
        QVERIFY(cache.entryForPath(oldCurrentPath, true).image.isNull());
        QCOMPARE(cache.fullSizeImageCount(), 0);
        QCOMPARE(cache.viewerImageCount(), 16);

        const auto nextNativePlan =
            cache.planRequest(items, nextCurrentIndex, QSize(), 1);
        QCOMPARE(nextNativePlan.decodeRequests.size(), 1);
        QVERIFY(storeFrame(cache, nextNativePlan.decodeRequests.first(),
                           Qt::green).accepted);
        QCOMPARE(cache.fullSizeImageCount(), 1);
        QCOMPARE(cache.nativeRetainedBytes(), nativeFrameBytes);
        QCOMPARE(cache.fitRetainedBytes(), fitBudget);
        QCOMPARE(cache.retainedBytes(), fitBudget + nativeFrameBytes);
    }

    void fitAndNativeBudgetsPruneIndependentlyByRetentionRank() {
        constexpr int itemCount = 5;
        constexpr int currentIndex = 2;
        const QSize nativeSize(50, 50);
        const QSize fitSize(10, 10);
        const qint64 fitFrameBytes =
            static_cast<qint64>(fitSize.width()) * fitSize.height() * 4;
        const qint64 nativeFrameBytes =
            static_cast<qint64>(nativeSize.width()) * nativeSize.height() * 4;
        const qint64 fitBudget = fitFrameBytes * itemCount;
        const qint64 nativeBudget = nativeFrameBytes * 2;

        std::vector<std::unique_ptr<ImageFile>> storage;
        QList<ImageFile *> items;
        storage.reserve(itemCount);
        items.reserve(itemCount);
        for (int index = 0; index < itemCount; ++index) {
            auto item = std::make_unique<ImageFile>();
            configureImage(*item,
                           QStringLiteral("tier-order-%1.png").arg(index),
                           nativeSize);
            items.append(item.get());
            storage.push_back(std::move(item));
        }

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(
            QStringLiteral("tier-order-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            fitBudget, nativeBudget);
        const auto fitPlan =
            cache.planRequest(items, currentIndex, fitSize, itemCount);
        for (const ImageDecodeRequest &request : fitPlan.decodeRequests) {
            QVERIFY(storeFrame(cache, request).accepted);
        }
        QCOMPARE(cache.viewerImageCount(), itemCount);
        QCOMPARE(cache.fitRetainedBytes(), fitBudget);

        const auto nativePlan =
            cache.planRequest(items, currentIndex, QSize(), itemCount);
        QCOMPARE(nativePlan.decodeRequests.size(), itemCount);
        const auto nativeRequestForPath = [&](const QString &path) {
            const auto it = std::find_if(
                nativePlan.decodeRequests.cbegin(),
                nativePlan.decodeRequests.cend(),
                [&path](const ImageDecodeRequest &request) {
                    return request.info.path == path;
                });
            return it == nativePlan.decodeRequests.cend()
                ? ImageDecodeRequest() : *it;
        };

        const QString currentPath = items.at(currentIndex)->fullPath();
        const QString nearestPath = items.at(currentIndex - 1)->fullPath();
        const QString farthestPath = items.at(itemCount - 1)->fullPath();
        QVERIFY(storeFrame(cache, nativeRequestForPath(currentPath),
                           Qt::red).accepted);
        const auto farthestStored = storeFrame(
            cache, nativeRequestForPath(farthestPath), Qt::blue);
        QVERIFY(farthestStored.accepted);
        QVERIFY(storeFrame(cache, nativeRequestForPath(nearestPath),
                           Qt::green).accepted);

        QCOMPARE(cache.fullSizeImageCount(), 2);
        QVERIFY(!cache.entryForPath(currentPath, true).image.isNull());
        QVERIFY(!cache.entryForPath(nearestPath, true).image.isNull());
        QVERIFY(cache.entryForPath(farthestPath, true).image.isNull());
        QVERIFY(store->snapshot(imageIdFromUrl(farthestStored.url)).isNull());
        QCOMPARE(cache.nativeRetainedBytes(), nativeBudget);
        QCOMPARE(cache.viewerImageCount(), itemCount);
        QCOMPARE(cache.fitRetainedBytes(), fitBudget);
        QCOMPARE(cache.retainedBytes(), fitBudget + nativeBudget);

        // Tightening one tier cannot prune the other tier. Each tier still
        // uses the same nearest-first retention ranks.
        cache.setByteBudgets(fitFrameBytes * 3, nativeFrameBytes);
        QCOMPARE(cache.viewerImageCount(), 3);
        QCOMPARE(cache.fitRetainedBytes(), fitFrameBytes * 3);
        QCOMPARE(cache.fullSizeImageCount(), 1);
        QCOMPARE(cache.nativeRetainedBytes(), nativeFrameBytes);
        QCOMPARE(cache.retainedBytes(),
                 fitFrameBytes * 3 + nativeFrameBytes);
        QVERIFY(!cache.entryForPath(currentPath, false).image.isNull());
        QVERIFY(!cache.entryForPath(items.at(currentIndex - 1)->fullPath(),
                                    false).image.isNull());
        QVERIFY(!cache.entryForPath(items.at(currentIndex + 1)->fullPath(),
                                    false).image.isNull());
        QVERIFY(!cache.entryForPath(currentPath, true).image.isNull());
    }

    void thumbnailSourceVersionRequiresTimestampAndFileSizeMatch() {
        ImageFile item;
        configureImage(item, QStringLiteral("versioned.png"),
                       QSize(640, 480));

        ImageInfo firstVersion = item.info();
        firstVersion.lastModified =
            QDateTime::fromMSecsSinceEpoch(1'700'000'000'000LL);
        firstVersion.fileSize = 1234;
        QImage decoded(QSize(320, 240), QImage::Format_RGBA8888);
        decoded.fill(Qt::red);
        item.setImage(decoded, firstVersion);

        QVERIFY(item.imageMatchesSource(firstVersion));

        ImageInfo changedTimestamp = firstVersion;
        changedTimestamp.lastModified =
            changedTimestamp.lastModified.addMSecs(1);
        QVERIFY(!item.imageMatchesSource(changedTimestamp));

        ImageInfo changedSize = firstVersion;
        changedSize.fileSize++;
        QVERIFY(!item.imageMatchesSource(changedSize));

        ImageInfo unknownVersion = firstVersion;
        unknownVersion.lastModified = {};
        unknownVersion.fileSize = -1;
        QVERIFY(!item.imageMatchesSource(unknownVersion));
    }

    void unboundedStandalonePolicyRetainsFramesAcrossNavigationPlans() {
        constexpr int itemCount = 48;
        const QSize originalSize(1600, 1200);
        const QSize viewportSize(800, 600);
        std::vector<std::unique_ptr<ImageFile>> ownedItems;
        QList<ImageFile *> items;
        ownedItems.reserve(itemCount);
        items.reserve(itemCount);
        for (int index = 0; index < itemCount; ++index) {
            auto item = std::make_unique<ImageFile>();
            configureImage(*item,
                           QStringLiteral("history-%1.png").arg(index),
                           originalSize);
            items.append(item.get());
            ownedItems.push_back(std::move(item));
        }

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ViewerImageCache cache(
            QStringLiteral("standalone-unbounded-"), store,
            QStringLiteral("thumbnails"), QStringLiteral("async"),
            ViewerImageCache::UnboundedByteBudget,
            ViewerImageCache::UnboundedByteBudget);
        QCOMPARE(cache.fitByteBudget(),
                 std::numeric_limits<qint64>::max());
        QCOMPARE(cache.nativeByteBudget(),
                 std::numeric_limits<qint64>::max());

        constexpr int firstCenter = 8;
        const auto firstPlan = cache.planRequest(
            items, firstCenter, viewportSize, 16);
        QCOMPARE(firstPlan.decodeRequests.size(), 16);
        for (const ImageDecodeRequest &request : firstPlan.decodeRequests) {
            QVERIFY(storeFrame(cache, request).accepted);
        }
        const QString firstPath = items.at(firstCenter)->fullPath();
        const QString firstId =
            cache.entryForPath(firstPath, false).imageId;
        QVERIFY(!firstId.isEmpty());

        constexpr int farCenter = 39;
        const auto farPlan = cache.planRequest(
            items, farCenter, viewportSize, 16);
        QCOMPARE(cache.entryForPath(firstPath, false).imageId, firstId);
        for (const ImageDecodeRequest &request : farPlan.decodeRequests) {
            QVERIFY(storeFrame(cache, request).accepted);
        }
        QVERIFY(cache.viewerImageCount() > 16);
        QCOMPARE(cache.entryForPath(firstPath, false).imageId, firstId);

        const auto returnPlan = cache.planRequest(
            items, firstCenter, viewportSize, 16);
        const bool centerWouldDecode = std::any_of(
            returnPlan.decodeRequests.cbegin(),
            returnPlan.decodeRequests.cend(),
            [&](const ImageDecodeRequest &request) {
                return request.info.path == firstPath;
            });
        QVERIFY(!centerWouldDecode);
        QCOMPARE(cache.entryForPath(firstPath, false).imageId, firstId);
    }
};

QTEST_MAIN(ViewerImageCacheTest)

#include "ViewerImageCacheTest.moc"
