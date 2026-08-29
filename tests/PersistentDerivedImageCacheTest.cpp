#include "PersistentDerivedImageCache.h"
#include "PersistentImageCache.h"
#include "Runners/CacheImageRunners.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageReadRunner.h"
#include "StorageLocations.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <utility>

namespace {

class CountingSourceProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++rangeReads;
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++materializations;
        return {};
    }

    std::atomic_int rangeReads{0};
    std::atomic_int materializations{0};
};

class MetadataSourceLease final : public ZoinGallery::ImageSourceLease {
public:
    explicit MetadataSourceLease(QString path) : _path(std::move(path)) {}
    QString localPath() const override { return _path; }

private:
    QString _path;
};

class MetadataSourceProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    explicit MetadataSourceProvider(QString path)
        : _path(std::move(path)) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++materializations;
        return QSharedPointer<MetadataSourceLease>::create(_path);
    }

    std::atomic_int materializations{0};

private:
    QString _path;
};

ImageDecodeRequest requestFor(QString versionStrength,
                              QString contentVersion =
                                  QStringLiteral("revision-1"),
                              QString resourceId =
                                  QStringLiteral("resource-17")) {
    ImageDecodeRequest request;
    request.info.path = QStringLiteral("opaque-gallery-entry");
    request.info.source = {
        .resourceId = std::move(resourceId),
        .sourceKey = QStringLiteral("account-a/folder/image.jpg"),
        .contentVersion = contentVersion,
        .versionStrength = versionStrength,
        .storageClass = QStringLiteral("network"),
        .accessProfile = QStringLiteral("nativeRange"),
        .displayName = QStringLiteral("image.jpg"),
        .mimeType = QStringLiteral("image/jpeg"),
        .size = 123456,
    };
    request.info.sourceVersionToken = contentVersion;
    request.info.imageSize = QSize(1920, 1080);
    request.targetSize = QSize(384, 216);
    request.checkCache = true;
    request.storeInPersistentCache = true;
    request.thumbnailTransformKey = QStringLiteral("masonry-fit-v2");
    return request;
}

QImage testImage(const QSize &size, QRgb color = qRgb(25, 90, 180)) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

void persist(const ImageDecodeRequest &request, const QImage &image) {
    const QByteArray prepared =
        PersistentImageCache::createImageForCache(request, image);
    QVERIFY(!prepared.isEmpty());
    // This is the exact call shape of CachedImageStoreRunner: DecodeManager
    // intentionally hands the runner ImageInfo plus the prepared artifact.
    PersistentImageCache::storeImage(request.info, prepared);
}

} // namespace

class PersistentDerivedImageCacheTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(ZoinGallery::StorageLocations::configure(
            QStringLiteral("derived-cache-tests")));
        PersistentImageCache::clear();
    }

    void init() {
        PersistentImageCache::clear();
    }

    void cleanupTestCase() {
        PersistentImageCache::clear();
    }

    void strongHitSkipsSourceMaterialization() {
        const ImageDecodeRequest request =
            requestFor(QStringLiteral("strong"));
        persist(request, testImage(request.targetSize));

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        // Construction order mirrors DecodeManager::decodeImages: the cache
        // runner registers the lookup before the source runner joins it.
        CachedImageRetrieveRunner cacheRunner(request, true);
        ImageReadRunner sourceRunner(request, provider);
        QSignalSpy readySpy(
            &cacheRunner,
            &CachedImageRetrieveRunner::cachedThumbnailRetrieved);

        cacheRunner.run();
        sourceRunner.run();

        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);
    }

    void coldMetadataManifestReachesDerivedHitWithoutMaterialization() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("cold-cache.png"));
        QImage source(80, 60, QImage::Format_RGB32);
        source.fill(Qt::darkCyan);
        QVERIFY(source.save(backing, "PNG"));

        ImageDecodeRequest request = requestFor(
            QStringLiteral("strong"), QStringLiteral("png-revision-1"));
        request.info.source.sourceKey = QStringLiteral("remote/cold-cache.png");
        request.info.source.displayName = QStringLiteral("cold-cache.png");
        request.info.source.mimeType = QStringLiteral("image/png");
        request.info.source.size = QFileInfo(backing).size();
        request.info.imageSize = {};

        auto firstProvider =
            QSharedPointer<MetadataSourceProvider>::create(backing);
        ImageInfoReadRunner firstMetadata(
            request.info.source.runtimeIdentity(), true, false, false, 0,
            false, QStringLiteral("cold-first"),
            request.info.sourceVersionToken, request.info.source,
            firstProvider);
        QSignalSpy firstMetadataSpy(
            &firstMetadata, &ImageInfoReadRunner::imageInfoReady);
        firstMetadata.run();
        QCOMPARE(firstMetadataSpy.size(), 1);
        QCOMPARE(firstProvider->materializations.load(), 1);
        const ImageInfo discovered = firstMetadataSpy.constFirst().at(0)
            .value<ImageInfo>();
        QCOMPARE(discovered.imageSize, QSize(80, 60));
        QVERIFY(!discovered.isCached);

        request.info = discovered;
        request.targetSize = QSize(80, 60);
        request.viewerRequest = true;
        request.backgroundViewerRequest = true;
        request.fitToViewerRequest = true;
        persist(request, testImage(request.targetSize));

        auto coldProvider = QSharedPointer<CountingSourceProvider>::create();
        ImageInfoReadRunner coldMetadata(
            request.info.source.runtimeIdentity(), true, false, false, 0,
            false, QStringLiteral("cold-second"),
            request.info.sourceVersionToken, request.info.source,
            coldProvider);
        QSignalSpy coldMetadataSpy(
            &coldMetadata, &ImageInfoReadRunner::imageInfoReady);
        coldMetadata.run();
        QCOMPARE(coldMetadataSpy.size(), 1);
        const ImageInfo cachedInfo = coldMetadataSpy.constFirst().at(0)
            .value<ImageInfo>();
        QVERIFY(cachedInfo.isCached);
        QCOMPARE(cachedInfo.imageSize, QSize(80, 60));
        QCOMPARE(coldProvider->materializations.load(), 0);

        request.info = cachedInfo;
        CachedImageRetrieveRunner cacheRunner(request, true);
        ImageReadRunner sourceRunner(request, coldProvider);
        QSignalSpy readySpy(
            &cacheRunner,
            &CachedImageRetrieveRunner::cachedThumbnailRetrieved);
        cacheRunner.run();
        sourceRunner.run();
        QCOMPARE(readySpy.size(), 1);
        QCOMPARE(coldProvider->materializations.load(), 0);

        ImageInfo changed = cachedInfo;
        changed.source.contentVersion = QStringLiteral("png-revision-2");
        changed.sourceVersionToken = QStringLiteral("png-revision-2");
        ImageInfoReadRunner changedMetadata(
            changed.source.runtimeIdentity(), true, false, false, 0, false,
            QStringLiteral("cold-changed"), changed.sourceVersionToken,
            changed.source, coldProvider);
        QSignalSpy changedSpy(
            &changedMetadata, &ImageInfoReadRunner::imageInfoReady);
        changedMetadata.run();
        QCOMPARE(changedSpy.size(), 1);
        QVERIFY(changedSpy.constFirst().at(0)
                    .value<ImageInfo>().sourceAccessFailed);
        QCOMPARE(coldProvider->materializations.load(), 1);
    }

    void metadataManifestsAreRetrievedAsOneLargeBatch() {
        constexpr int hitCount = 128;
        QList<ImageInfo> candidates;
        candidates.reserve(hitCount + 1);
        for (int index = 0; index <= hitCount; ++index) {
            ImageDecodeRequest request = requestFor(
                QStringLiteral("strong"),
                QStringLiteral("batch-revision-%1").arg(index),
                QStringLiteral("batch-resource-%1").arg(index));
            request.info.source.sourceKey =
                QStringLiteral("remote/batch/image-%1.png").arg(index);
            request.info.source.displayName =
                QStringLiteral("image-%1.png").arg(index);
            request.info.source.mimeType = QStringLiteral("image/png");
            request.info.source.size = 10'000 + index;
            request.info.fileSize = request.info.source.size;
            request.info.imageSize = QSize(640 + index, 480 + index);
            request.info.requestNamespace = QStringLiteral("batch-test");
            request.info.isLast = index == hitCount;
            if (index < hitCount) {
                PersistentDerivedImageCache::storeMetadata(request.info);
            }
            request.info.imageSize = {};
            candidates.append(request.info);
        }

        QList<ImageInfo> hits;
        QList<ImageInfo> misses;
        PersistentDerivedImageCache::retrieveMetadataBatch(
            candidates, hits, misses);

        QCOMPARE(hits.size(), hitCount);
        QCOMPARE(misses.size(), 1);
        for (int index = 0; index < hits.size(); ++index) {
            QVERIFY(hits.at(index).isCached);
            QCOMPARE(hits.at(index).imageSize,
                     QSize(640 + index, 480 + index));
            QCOMPARE(hits.at(index).requestNamespace,
                     QStringLiteral("batch-test"));
            QVERIFY(!hits.at(index).isLast);
        }
        QCOMPARE(misses.first().source.contentVersion,
                 QStringLiteral("batch-revision-128"));
        QVERIFY(misses.first().isLast);
    }

    void decodeManagerPublishesCachedMetadataAsOneBatch() {
        constexpr int imageCount = 128;
        QList<DecodeManager::VersionedImageInfoRequest> requests;
        requests.reserve(imageCount);
        for (int index = 0; index < imageCount; ++index) {
            ImageDecodeRequest request = requestFor(
                QStringLiteral("strong"),
                QStringLiteral("manager-revision-%1").arg(index),
                QStringLiteral("manager-resource-%1").arg(index));
            request.info.source.sourceKey =
                QStringLiteral("remote/manager/image-%1.png").arg(index);
            request.info.source.displayName =
                QStringLiteral("image-%1.png").arg(index);
            request.info.source.mimeType = QStringLiteral("image/png");
            request.info.source.size = 20'000 + index;
            request.info.fileSize = request.info.source.size;
            request.info.imageSize = QSize(800 + index, 600 + index);
            PersistentDerivedImageCache::storeMetadata(request.info);
            DecodeManager::VersionedImageInfoRequest infoRequest{
                request.info.source.runtimeIdentity(),
                request.info.sourceVersionToken,
                request.info.source};
            infoRequest.highPriority = index < 16;
            requests.append(std::move(infoRequest));
        }

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        DecodeManager manager(nullptr, 4, provider);
        int batchSignals = 0;
        int singleSignals = 0;
        QList<ImageInfo> received;
        connect(&manager, &DecodeManager::imagesInfoReady, this,
                [&](const QList<ImageInfo> &infos) {
                    ++batchSignals;
                    received = infos;
                });
        connect(&manager, &DecodeManager::imageInfoReady, this,
                [&](const ImageInfo &) { ++singleSignals; });

        manager.readVersionedImagesInfo(
            requests, false, false, QStringLiteral("manager-batch"));

        QCOMPARE(batchSignals, 1);
        QCOMPARE(singleSignals, 0);
        QCOMPARE(received.size(), imageCount);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);
        QCOMPARE(std::count_if(received.cbegin(), received.cend(),
                               [](const ImageInfo &info) {
                                   return info.highPriority;
                               }),
                 16);
        QVERIFY(received.constLast().isLast);
        QCOMPARE(received.constLast().requestNamespace,
                 QStringLiteral("manager-batch"));
    }

    void decodeManagerFlushesMixedBatchAfterFinalMiss() {
        constexpr int imageCount = 128;
        constexpr int missingIndex = 37;
        QList<DecodeManager::VersionedImageInfoRequest> requests;
        requests.reserve(imageCount);
        for (int index = 0; index < imageCount; ++index) {
            ImageDecodeRequest request = requestFor(
                QStringLiteral("strong"),
                QStringLiteral("mixed-revision-%1").arg(index),
                QStringLiteral("mixed-resource-%1").arg(index));
            request.info.source.sourceKey =
                QStringLiteral("remote/mixed/image-%1.png").arg(index);
            request.info.source.displayName =
                QStringLiteral("image-%1.png").arg(index);
            request.info.source.mimeType = QStringLiteral("image/png");
            request.info.source.size = 30'000 + index;
            request.info.fileSize = request.info.source.size;
            request.info.imageSize = QSize(1000 + index, 700 + index);
            if (index != missingIndex) {
                PersistentDerivedImageCache::storeMetadata(request.info);
            }
            DecodeManager::VersionedImageInfoRequest infoRequest{
                request.info.source.runtimeIdentity(),
                request.info.sourceVersionToken,
                request.info.source};
            infoRequest.highPriority = index == missingIndex;
            requests.append(std::move(infoRequest));
        }

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        DecodeManager manager(nullptr, 4, provider);
        QList<ImageInfo> cached;
        QList<ImageInfo> sourceResults;
        connect(&manager, &DecodeManager::imagesInfoReady, this,
                [&](const QList<ImageInfo> &infos) { cached = infos; });
        connect(&manager, &DecodeManager::imageInfoReady, this,
                [&](const ImageInfo &info) { sourceResults.append(info); });

        manager.readVersionedImagesInfo(
            requests, false, false, QStringLiteral("mixed-batch"));

        QCOMPARE(cached.size(), imageCount - 1);
        QVERIFY(std::none_of(cached.cbegin(), cached.cend(),
                             [](const ImageInfo &info) {
                                 return info.isLast;
                             }));
        QTRY_COMPARE_WITH_TIMEOUT(sourceResults.size(), 1, 5000);
        QVERIFY(sourceResults.first().isLast);
        QVERIFY(sourceResults.first().highPriority);
        QVERIFY(sourceResults.first().sourceAccessFailed);
        QCOMPARE(provider->materializations.load(), 1);
    }

    void weakHitSkipsSourceMaterializationWithinAuthority() {
        const ImageDecodeRequest request =
            requestFor(QStringLiteral("weakRemote"));
        persist(request, testImage(request.targetSize));

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        CachedImageRetrieveRunner cacheRunner(request, true);
        ImageReadRunner sourceRunner(request, provider);
        QSignalSpy readySpy(
            &cacheRunner,
            &CachedImageRetrieveRunner::cachedThumbnailRetrieved);

        cacheRunner.run();
        sourceRunner.run();

        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);
    }

    void opaqueVersionInvalidatesEntry() {
        const ImageDecodeRequest first =
            requestFor(QStringLiteral("strong"),
                       QStringLiteral("revision-1"));
        persist(first, testImage(first.targetSize));
        ImageDecodeRequest firstCopy = first;
        QVERIFY(!PersistentImageCache::retrieveImage(firstCopy, true)
                     .isNull());

        ImageDecodeRequest changed =
            requestFor(QStringLiteral("strong"),
                       QStringLiteral("revision-2"));
        QVERIFY(PersistentImageCache::retrieveImage(changed, true).isNull());
    }

    void weakAndSessionRevisionsUseOnlySessionDiskCache() {
        const QStringList strengths = {
            QStringLiteral("weakRemote"), QStringLiteral("session")};
        for (const QString &strength : strengths) {
            PersistentDerivedImageCache::clearSession();
            ImageDecodeRequest request = requestFor(strength);
            persist(request, testImage(request.targetSize));

            QVERIFY(!PersistentImageCache::retrieveImage(request, true)
                         .isNull());
            QCOMPARE(PersistentDerivedImageCache::persistentCacheSize(), 0);
            QVERIFY(PersistentDerivedImageCache::sessionCacheSize() > 0);

            PersistentDerivedImageCache::clearSession();
            QVERIFY(PersistentImageCache::retrieveImage(request, true)
                        .isNull());
        }
    }

    void weakAuthorityResourceIdInvalidatesEntry() {
        const ImageDecodeRequest first =
            requestFor(QStringLiteral("weak"),
                       QStringLiteral("revision-1"),
                       QStringLiteral("authority-a/resource-17"));
        persist(first, testImage(first.targetSize));

        ImageDecodeRequest firstCopy = first;
        QVERIFY(!PersistentImageCache::retrieveImage(firstCopy, true)
                     .isNull());

        ImageDecodeRequest newAuthority =
            requestFor(QStringLiteral("weak"),
                       QStringLiteral("revision-1"),
                       QStringLiteral("authority-b/resource-17"));
        QVERIFY(PersistentImageCache::retrieveImage(newAuthority, true)
                    .isNull());

        // A delayed store runner from the old authority cannot populate the
        // new authority's key even if source/version/tier are identical.
        const QByteArray oldPrepared =
            PersistentImageCache::createImageForCache(
                first, testImage(first.targetSize));
        PersistentImageCache::storeImage(newAuthority.info, oldPrepared);
        QVERIFY(PersistentImageCache::retrieveImage(newAuthority, true)
                    .isNull());
    }

    void clearingSessionArtifactsPreservesStrongArtifacts() {
        ImageDecodeRequest strong = requestFor(QStringLiteral("strong"));
        ImageDecodeRequest weak = requestFor(
            QStringLiteral("weak"), QStringLiteral("revision-1"),
            QStringLiteral("authority-a/resource-17"));
        persist(strong, testImage(strong.targetSize, qRgb(20, 200, 20)));
        persist(weak, testImage(weak.targetSize, qRgb(20, 20, 200)));

        QVERIFY(PersistentDerivedImageCache::persistentCacheSize() > 0);
        QVERIFY(PersistentDerivedImageCache::sessionCacheSize() > 0);
        PersistentDerivedImageCache::clearSession();

        QVERIFY(!PersistentImageCache::retrieveImage(strong, true).isNull());
        QVERIFY(PersistentImageCache::retrieveImage(weak, true).isNull());
        QVERIFY(PersistentDerivedImageCache::persistentCacheSize() > 0);
        QCOMPARE(PersistentDerivedImageCache::sessionCacheSize(), 0);
    }

    void nativeArtifactIsNeverPersisted() {
        ImageDecodeRequest native =
            requestFor(QStringLiteral("localStat"));
        native.viewerRequest = true;
        native.fitToViewerRequest = false;
        native.targetSize = native.info.imageSize;

        QVERIFY(PersistentImageCache::createImageForCache(
                    native, testImage(native.targetSize)).isEmpty());
        QCOMPARE(PersistentDerivedImageCache::cacheSize(), 0);
        QVERIFY(PersistentImageCache::retrieveImage(native, true).isNull());
    }

    void viewerFitAndThumbnailUseSeparateArtifacts() {
        const ImageDecodeRequest thumbnail =
            requestFor(QStringLiteral("strong"));
        persist(thumbnail,
                testImage(thumbnail.targetSize, qRgb(200, 20, 20)));

        ImageDecodeRequest fit = thumbnail;
        fit.viewerRequest = true;
        fit.backgroundViewerRequest = true;
        fit.fitToViewerRequest = true;
        QVERIFY(PersistentImageCache::retrieveImage(fit, true).isNull());
        persist(fit, testImage(fit.targetSize, qRgb(20, 20, 200)));

        QVERIFY(!PersistentImageCache::retrieveImage(fit, true).isNull());
        ImageDecodeRequest thumbnailCopy = thumbnail;
        QVERIFY(!PersistentImageCache::retrieveImage(thumbnailCopy, true)
                     .isNull());
    }
};

QTEST_MAIN(PersistentDerivedImageCacheTest)
#include "PersistentDerivedImageCacheTest.moc"
