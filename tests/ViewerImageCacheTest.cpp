#include "ImageFile.h"
#include "ProviderImageStore.h"
#include "ViewerImageCache.h"

#include <QImage>
#include <QSharedPointer>
#include <QtTest>

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
            cache.planRequest(items, 0, viewportSize, 1);
        QCOMPARE(firstPlan.decodeRequests.size(), 1);
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
            cache.planRequest(items, 1, viewportSize, 1);
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
            cache.planRequest(items, 0, viewportSize, 1);
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
};

QTEST_MAIN(ViewerImageCacheTest)

#include "ViewerImageCacheTest.moc"
