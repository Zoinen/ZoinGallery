#include "ImageFile.h"
#include "DecodeManager.h"
#include "FileListModel.h"
#include "ProviderImageStore.h"
#include "ThumbnailLoader.h"
#include "src/embed/ExternalCatalogModel.h"
#include "src/embed/ThumbnailMemoryCache.h"
#include "tests/HeicTestFixture.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QImage frame(const QSize &size, QRgb color = qRgba(31, 127, 223, 255)) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}

ZoinGallery::ThumbnailMemoryCache::Handle storeFrame(
    ZoinGallery::ThumbnailMemoryCache &cache, const QString &owner,
    const QString &source, qint64 version, qint64 fileSize,
    const QSize &requested, const QSize &decoded = {}) {
    const QImage image = frame(decoded.isValid() ? decoded : requested);
    return cache.storeDecoded(
        owner, source, version, fileSize, requested,
        QString::fromLatin1(
            ZoinGallery::ThumbnailMemoryCache::DefaultTransformKey),
        image);
}

} // namespace

class ThumbnailMemoryCacheTest : public QObject {
    Q_OBJECT

private slots:
    void largerFrameIsDeduplicatedAndSharedAcrossSessions() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("image.png"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("source"), qint64(6));
        file.close();

        const auto store = QSharedPointer<ProviderImageStore>::create();
        ZoinGallery::ThumbnailMemoryCache cache(store, 1024 * 1024);
        QSignalSpy availableSpy(
            &cache,
            &ZoinGallery::ThumbnailMemoryCache::frameAvailable);

        const auto left = cache.acquire(
            QStringLiteral("left"), source, 101, 6, QSize(96, 64));
        QCOMPARE(left.state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto rightWhilePending = cache.acquire(
            QStringLiteral("right"),
            directory.filePath(QStringLiteral("./image.png")),
            101, 6, QSize(48, 32));
        QCOMPARE(rightWhilePending.state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Pending);
        QCOMPARE(cache.pendingRequestCount(), qsizetype(1));
        QCOMPARE(cache.coalescedRequestCount(), quint64(1));

        const auto published = storeFrame(
            cache, QStringLiteral("left"), source, 101, 6,
            QSize(96, 64));
        QVERIFY(published.isValid());
        QCOMPARE(cache.pendingRequestCount(), qsizetype(0));
        QCOMPARE(cache.frameCount(), qsizetype(1));
        QCOMPARE(availableSpy.size(), 1);
        QCOMPARE(availableSpy.constFirst().at(3).toSize(), QSize(96, 64));
        QCOMPARE(availableSpy.constFirst().at(4).toString(),
                 QStringLiteral("thumbnail-aspect-v1"));
        QCOMPARE(availableSpy.constFirst().at(5).toString(),
                 published.providerId);
        QVERIFY(store->contains(published.providerId));

        const auto right = cache.acquire(
            QStringLiteral("right"), source, 101, 6, QSize(48, 32));
        QCOMPARE(right.state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Hit);
        QCOMPARE(right.handle.providerId, published.providerId);
        QCOMPARE(right.handle.decodedSize, QSize(96, 64));
        QCOMPARE(cache.hitCount(), quint64(1));

        // Version, source size and pixel transform policy are all part of
        // identity. Gallery crop/fit/masonry share one aspect-preserved pixel
        // policy because QML alone chooses crop versus fit at presentation.
        QCOMPARE(cache.acquire(QStringLiteral("right"), source, 102, 6,
                               QSize(48, 32)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        QCOMPARE(cache.acquire(QStringLiteral("right"), source, 101, 7,
                               QSize(48, 32)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto aspectReuse = cache.acquire(
            QStringLiteral("right"), source, 101, 6, QSize(48, 32),
            QStringLiteral("thumbnail-aspect-v1"));
        QCOMPARE(aspectReuse.state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Hit);
        QCOMPARE(aspectReuse.handle.providerId, published.providerId);
        QCOMPARE(cache.acquire(QStringLiteral("right"), source, 101, 6,
                               QSize(48, 32),
                               QStringLiteral("thumbnail-filtered-v2")).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
    }

    void largerGridTierIsReusedByIconsAndMasonry() {
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ZoinGallery::ThumbnailMemoryCache cache(store, 4 * 1024 * 1024);
        const QString source = QStringLiteral(
            "/virtual/presentations/wide-image.jpg");
        const QString aspectKey = QStringLiteral("thumbnail-aspect-v1");

        const QSize gridCropTier(320, 180);
        QCOMPARE(cache.acquire(QStringLiteral("gallery"), source, 7, 100,
                               gridCropTier, aspectKey).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto gridFrame = cache.storeDecoded(
            QStringLiteral("gallery"), source, 7, 100, gridCropTier,
            aspectKey, frame(gridCropTier));
        QVERIFY(gridFrame.isValid());

        const auto icons = cache.acquire(
            QStringLiteral("gallery"), source, 7, 100,
            QSize(128, 72), aspectKey);
        QCOMPARE(icons.state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Hit);
        QCOMPARE(icons.handle.providerId, gridFrame.providerId);

        const auto masonry = cache.acquire(
            QStringLiteral("gallery"), source, 7, 100,
            QSize(160, 90), aspectKey);
        QCOMPARE(masonry.state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Hit);
        QCOMPARE(masonry.handle.providerId, gridFrame.providerId);
        QCOMPARE(cache.frameCount(), qsizetype(1));
        QCOMPARE(cache.storeCount(), quint64(1));
    }

    void lruEvictsTheLeastRecentlyUsedFrameAndProviderImage() {
        const auto store = QSharedPointer<ProviderImageStore>::create();
        constexpr qint64 frameBytes = 10 * 10 * 4;
        ZoinGallery::ThumbnailMemoryCache cache(store, frameBytes * 2);
        QSignalSpy evictionSpy(
            &cache, &ZoinGallery::ThumbnailMemoryCache::frameEvicted);

        const auto add = [&](const QString &name) {
            const QString source = QStringLiteral("/virtual/cache/%1.png")
                                       .arg(name);
            return storeFrame(cache, name, source, 1, frameBytes,
                              QSize(10, 10));
        };

        QCOMPARE(cache.acquire(QStringLiteral("first"),
                               QStringLiteral("/virtual/cache/first.png"),
                               1, frameBytes, QSize(10, 10)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto first = add(QStringLiteral("first"));
        QCOMPARE(cache.acquire(QStringLiteral("second"),
                               QStringLiteral("/virtual/cache/second.png"),
                               1, frameBytes, QSize(10, 10)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto second = add(QStringLiteral("second"));
        QVERIFY(first.isValid());
        QVERIFY(second.isValid());
        QCOMPARE(cache.retainedBytes(), frameBytes * 2);
        QCOMPARE(store->retainedBytes(), frameBytes * 2);

        // A hit updates recency, so the untouched second frame is the victim.
        QCOMPARE(cache.acquire(QStringLiteral("reader"),
                               QStringLiteral("/virtual/cache/first.png"),
                               1, frameBytes, QSize(10, 10)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Hit);
        QCOMPARE(cache.acquire(QStringLiteral("third"),
                               QStringLiteral("/virtual/cache/third.png"),
                               1, frameBytes, QSize(10, 10)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto third = add(QStringLiteral("third"));
        QVERIFY(third.isValid());

        QCOMPARE(cache.frameCount(), qsizetype(2));
        QCOMPARE(cache.retainedBytes(), frameBytes * 2);
        QCOMPARE(store->imageCount(), qsizetype(2));
        QCOMPARE(store->retainedBytes(), frameBytes * 2);
        QVERIFY(store->contains(first.providerId));
        QVERIFY(!store->contains(second.providerId));
        QVERIFY(store->contains(third.providerId));
        QCOMPARE(cache.evictionCount(), quint64(1));
        QCOMPARE(evictionSpy.size(), 1);
        QCOMPARE(evictionSpy.constFirst().constFirst().toString(),
                 second.providerId);
    }

    void aSingleOversizedFrameIsBoundedToOnePinnedAllocation() {
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ZoinGallery::ThumbnailMemoryCache cache(store, 64);

        const QString firstSource = QStringLiteral("/virtual/huge/one.png");
        QCOMPARE(cache.acquire(QStringLiteral("one"), firstSource, 1, 400,
                               QSize(10, 10)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto first = storeFrame(cache, QStringLiteral("one"),
                                      firstSource, 1, 400, QSize(10, 10));
        QCOMPARE(cache.frameCount(), qsizetype(1));
        QCOMPARE(cache.retainedBytes(), qint64(400));

        const QString secondSource = QStringLiteral("/virtual/huge/two.png");
        QCOMPARE(cache.acquire(QStringLiteral("two"), secondSource, 1, 400,
                               QSize(10, 10)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        const auto second = storeFrame(cache, QStringLiteral("two"),
                                       secondSource, 1, 400, QSize(10, 10));
        QCOMPARE(cache.frameCount(), qsizetype(1));
        QCOMPARE(cache.retainedBytes(), qint64(400));
        QVERIFY(!store->contains(first.providerId));
        QVERIFY(store->contains(second.providerId));
    }

    void cancellingAnOwnerReleasesAWaitingSession() {
        const auto store = QSharedPointer<ProviderImageStore>::create();
        ZoinGallery::ThumbnailMemoryCache cache(store, 1024);
        QSignalSpy releasedSpy(
            &cache, &ZoinGallery::ThumbnailMemoryCache::requestReleased);
        const QString source = QStringLiteral("/virtual/cancel/image.png");

        QCOMPARE(cache.acquire(QStringLiteral("left"), source, 5, 90,
                               QSize(12, 12)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
        QCOMPARE(cache.acquire(QStringLiteral("right"), source, 5, 90,
                               QSize(8, 8)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Pending);
        cache.cancelRequests(QStringLiteral("left"));
        QCOMPARE(cache.pendingRequestCount(), qsizetype(0));
        QCOMPARE(releasedSpy.size(), 1);
        QCOMPARE(releasedSpy.constFirst().at(3).toSize(), QSize(12, 12));
        QCOMPARE(releasedSpy.constFirst().at(4).toString(),
                 QStringLiteral("thumbnail-aspect-v1"));
        QVERIFY(releasedSpy.constFirst().at(5).toBool());

        QCOMPARE(cache.acquire(QStringLiteral("right"), source, 5, 90,
                               QSize(8, 8)).state,
                 ZoinGallery::ThumbnailMemoryCache::AcquireState::Owner);
    }

    void providerStoreAccountsReplacementAndRemovalBytes() {
        ProviderImageStore store;
        store.publish(QStringLiteral("same"), frame(QSize(10, 10)));
        QCOMPARE(store.imageCount(), qsizetype(1));
        QCOMPARE(store.retainedBytes(), qint64(400));

        store.publish(QStringLiteral("same"), frame(QSize(20, 10)));
        QCOMPARE(store.imageCount(), qsizetype(1));
        QCOMPARE(store.retainedBytes(), qint64(800));

        store.remove(QStringLiteral("same"));
        QCOMPARE(store.imageCount(), qsizetype(0));
        QCOMPARE(store.retainedBytes(), qint64(0));
    }

    void imageFileDisplayFieldsKeepHostOverridesAndRefreshDefaults() {
        ImageFile file;
        file.setFileName(QStringLiteral("archive.tar.gz"));
        file.setIsFolder(false);
        ImageInfo info;
        info.fileSize = 2048;
        file.setInfo(info);

        QCOMPARE(file.displayFields().value(
                     QStringLiteral("displayBaseName")).toString(),
                 QStringLiteral("archive.tar"));
        QCOMPARE(file.displayFields().value(
                     QStringLiteral("displayExtension")).toString(),
                 QStringLiteral("gz"));
        QVERIFY(!file.displayFields().value(
                     QStringLiteral("sizeText")).toString().isEmpty());

        file.setDisplayFields({
            {QStringLiteral("displayBaseName"), QStringLiteral("Host name")},
            {QStringLiteral("modeText"), QStringLiteral("rwxr-xr-x")},
        });
        info.fileSize = 4096;
        file.setInfo(info);
        QCOMPARE(file.displayFields().value(
                     QStringLiteral("displayBaseName")).toString(),
                 QStringLiteral("Host name"));
        QCOMPARE(file.displayFields().value(
                     QStringLiteral("modeText")).toString(),
                 QStringLiteral("rwxr-xr-x"));
        QVERIFY(!file.displayFields().value(
                     QStringLiteral("sizeText")).toString().isEmpty());
    }

    void externalModelsShareOneDecodeAndDoNotRetainImageCopies() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath =
            directory.filePath(QStringLiteral("shared.png"));
        QImage source(320, 240, QImage::Format_RGBA8888);
        source.fill(QColor(QStringLiteral("#4c8bf5")));
        QVERIFY(source.save(sourcePath));
        const QFileInfo sourceInfo(sourcePath);
        const qint64 version =
            sourceInfo.lastModified().toMSecsSinceEpoch() * 1'000'000;

        ThumbnailLoader::init();
        const auto store = QSharedPointer<ProviderImageStore>::create();
        const auto cache =
            QSharedPointer<ZoinGallery::ThumbnailMemoryCache>::create(
                store, 1024 * 1024);
        DecodeManager decodeManager(nullptr, 2);
        ZoinGallery::ExternalCatalogModel left(
            QStringLiteral("left"), QStringLiteral("shared-thumbnails"),
            QStringLiteral("shared-async"), store, cache, &decodeManager,
            1024 * 1024, 1024 * 1024);
        ZoinGallery::ExternalCatalogModel right(
            QStringLiteral("right"), QStringLiteral("shared-thumbnails"),
            QStringLiteral("shared-async"), store, cache, &decodeManager,
            1024 * 1024, 1024 * 1024);
        const QVariantMap catalogEntry{
            {QStringLiteral("entryId"), QStringLiteral("shared")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), sourceInfo.fileName()},
            {QStringLiteral("localPath"), sourcePath},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"), version},
            {QStringLiteral("size"), sourceInfo.size()},
        };
        QVERIFY(left.applyCatalog({catalogEntry}));
        QVERIFY(right.applyCatalog({catalogEntry}));

        auto imageFileAt = [](ZoinGallery::ExternalCatalogModel &model) {
            return model.data(model.index(0, 0),
                              FileListModel::ImageFileRole)
                .value<ImageFile *>();
        };
        ImageFile *leftFile = imageFileAt(left);
        ImageFile *rightFile = imageFileAt(right);
        QVERIFY(leftFile);
        QVERIFY(rightFile);

        int thumbnailResults = 0;
        connect(&decodeManager, &DecodeManager::imageReady, this,
                [&](const ImageDecodeRequest &request, const QImage &,
                    const DecodedImageInfo &) {
                    if (!request.viewerRequest &&
                        (request.requestNamespace == QStringLiteral("left") ||
                         request.requestNamespace == QStringLiteral("right"))) {
                        ++thumbnailResults;
                    }
                });

        ImageDecodeRequest request;
        request.info = leftFile->info();
        // The renderer resolves every presentation to a source-aspect decoded
        // size before it reaches ThumbnailLoader. A larger Grid tier and a
        // smaller Icons/Masonry tier therefore share the same pixel family.
        request.targetSize = QSize(80, 60);
        request.thumbnailTransformKey = QStringLiteral("thumbnail-aspect-v1");
        left.decodeImages({request});
        request.info = rightFile->info();
        request.targetSize = QSize(40, 30);
        right.decodeImages({request});

        QTRY_VERIFY_WITH_TIMEOUT(!leftFile->imageIdUrl().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!rightFile->imageIdUrl().isEmpty(), 5000);
        QCOMPARE(thumbnailResults, 1);
        QCOMPARE(leftFile->imageIdUrl(), rightFile->imageIdUrl());
        QCOMPARE(cache->frameCount(), qsizetype(1));
        QCOMPARE(store->imageCount(), qsizetype(1));
        QVERIFY(leftFile->image().isNull());
        QVERIFY(rightFile->image().isNull());
        QVERIFY(cache->coalescedRequestCount() >= 1);

        left.shutdown();
        right.shutdown();
        decodeManager.prepareToClose();
    }

    void equalTargetHeicCompletionPublishesToBothCoalescedSessions() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath =
            directory.filePath(QStringLiteral("shared-portrait.heic"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        const QByteArray bytes = ZoinGalleryTest::portraitHeicFixture();
        QCOMPARE(bytes.size(), 589);
        QCOMPARE(source.write(bytes), bytes.size());
        source.close();

        const QFileInfo sourceInfo(sourcePath);
        const qint64 version =
            sourceInfo.lastModified().toMSecsSinceEpoch() * 1'000'000;
        ThumbnailLoader::init();
        const auto store = QSharedPointer<ProviderImageStore>::create();
        const auto cache =
            QSharedPointer<ZoinGallery::ThumbnailMemoryCache>::create(
                store, 1024 * 1024);
        DecodeManager decodeManager(nullptr, 1);
        ZoinGallery::ExternalCatalogModel left(
            QStringLiteral("heic-left"), QStringLiteral("shared-thumbnails"),
            QStringLiteral("shared-async"), store, cache, &decodeManager,
            1024 * 1024, 1024 * 1024);
        ZoinGallery::ExternalCatalogModel right(
            QStringLiteral("heic-right"), QStringLiteral("shared-thumbnails"),
            QStringLiteral("shared-async"), store, cache, &decodeManager,
            1024 * 1024, 1024 * 1024);
        const QVariantMap catalogEntry{
            {QStringLiteral("entryId"), QStringLiteral("shared-heic")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), sourceInfo.fileName()},
            {QStringLiteral("localPath"), sourcePath},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"), version},
            {QStringLiteral("size"), sourceInfo.size()},
        };
        QVERIFY(left.applyCatalog({catalogEntry}));
        QVERIFY(right.applyCatalog({catalogEntry}));

        const auto imageFileAt = [](ZoinGallery::ExternalCatalogModel &model) {
            return model.data(model.index(0, 0),
                              FileListModel::ImageFileRole)
                .value<ImageFile *>();
        };
        ImageFile *leftFile = imageFileAt(left);
        ImageFile *rightFile = imageFileAt(right);
        QVERIFY(leftFile);
        QVERIFY(rightFile);
        left.requestImageMetadata({0}, true);
        right.requestImageMetadata({0}, true);
        QTRY_COMPARE_WITH_TIMEOUT(leftFile->fullSize(), QSize(48, 64),
                                  5000);
        QTRY_COMPARE_WITH_TIMEOUT(rightFile->fullSize(), QSize(48, 64),
                                  5000);

        QSignalSpy readySpy(&decodeManager, &DecodeManager::imageReady);
        QSignalSpy failedSpy(&decodeManager,
                             &DecodeManager::imageReadFailed);
        ImageDecodeRequest request;
        request.info = leftFile->info();
        request.targetSize = QSize(32, 32);
        request.thumbnailTransformKey = QStringLiteral(
            "thumbnail-aspect-v1");
        left.decodeImages({request});
        request.info = rightFile->info();
        right.decodeImages({request});

        QTRY_VERIFY_WITH_TIMEOUT(!leftFile->imageIdUrl().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!rightFile->imageIdUrl().isEmpty(), 5000);
        QCOMPARE(failedSpy.size(), 0);
        QCOMPARE(readySpy.size(), 1);
        QCOMPARE(readySpy.constFirst().at(1).value<QImage>().size(),
                 QSize(24, 32));
        QCOMPARE(leftFile->imageIdUrl(), rightFile->imageIdUrl());
        QCOMPARE(cache->frameCount(), qsizetype(1));
        QCOMPARE(cache->storeCount(), quint64(1));
        QCOMPARE(cache->coalescedRequestCount(), quint64(1));
        QCOMPARE(cache->pendingRequestCount(), qsizetype(0));

        left.shutdown();
        right.shutdown();
        decodeManager.prepareToClose();
    }

    void nullCompletionReleasesEveryWaiterWithoutRetryPingPong() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath =
            directory.filePath(QStringLiteral("corrupt.heic"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("not-a-heif-image"), qint64(16));
        source.close();

        const QFileInfo sourceInfo(sourcePath);
        const qint64 version =
            sourceInfo.lastModified().toMSecsSinceEpoch() * 1'000'000;
        ThumbnailLoader::init();
        const auto store = QSharedPointer<ProviderImageStore>::create();
        const auto cache =
            QSharedPointer<ZoinGallery::ThumbnailMemoryCache>::create(
                store, 1024 * 1024);
        DecodeManager decodeManager(nullptr, 1);
        ZoinGallery::ExternalCatalogModel left(
            QStringLiteral("bad-left"), QStringLiteral("shared-thumbnails"),
            QStringLiteral("shared-async"), store, cache, &decodeManager,
            1024 * 1024, 1024 * 1024);
        ZoinGallery::ExternalCatalogModel right(
            QStringLiteral("bad-right"), QStringLiteral("shared-thumbnails"),
            QStringLiteral("shared-async"), store, cache, &decodeManager,
            1024 * 1024, 1024 * 1024);
        const QVariantMap catalogEntry{
            {QStringLiteral("entryId"), QStringLiteral("corrupt-heic")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), sourceInfo.fileName()},
            {QStringLiteral("localPath"), sourcePath},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"), version},
            {QStringLiteral("size"), sourceInfo.size()},
        };
        QVERIFY(left.applyCatalog({catalogEntry}));
        QVERIFY(right.applyCatalog({catalogEntry}));
        auto imageFileAt = [](ZoinGallery::ExternalCatalogModel &model) {
            return model.data(model.index(0, 0),
                              FileListModel::ImageFileRole)
                .value<ImageFile *>();
        };
        ImageFile *leftFile = imageFileAt(left);
        ImageFile *rightFile = imageFileAt(right);
        QVERIFY(leftFile);
        QVERIFY(rightFile);

        QSignalSpy readySpy(&decodeManager, &DecodeManager::imageReady);
        QSignalSpy failedSpy(&decodeManager,
                             &DecodeManager::imageReadFailed);
        QSignalSpy leftChanged(&left, &QAbstractItemModel::dataChanged);
        QSignalSpy rightChanged(&right, &QAbstractItemModel::dataChanged);
        ImageDecodeRequest request;
        request.info = leftFile->info();
        request.targetSize = QSize(32, 32);
        request.thumbnailTransformKey = QStringLiteral(
            "thumbnail-aspect-v1");
        left.decodeImages({request});
        request.info = rightFile->info();
        right.decodeImages({request});

        QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
        QCOMPARE(failedSpy.size(), 0);
        QVERIFY(readySpy.constFirst().at(1).value<QImage>().isNull());
        QCOMPARE(cache->pendingRequestCount(), qsizetype(0));
        QCOMPARE(cache->missCount(), quint64(1));
        QCOMPARE(cache->coalescedRequestCount(), quint64(1));
        QVERIFY(leftFile->imageIdUrl().isEmpty());
        QVERIFY(rightFile->imageIdUrl().isEmpty());

        // Failure release is shared and non-replanning. Neither session wakes
        // the other into an endless alternating retry loop.
        QTest::qWait(150);
        QCOMPARE(readySpy.size(), 1);
        QCOMPARE(failedSpy.size(), 0);
        QCOMPARE(leftChanged.size(), 0);
        QCOMPARE(rightChanged.size(), 0);

        // Both local admission records were cleared, so a later explicit
        // retry can elect a fresh owner instead of remaining poisoned.
        request.info = rightFile->info();
        right.decodeImages({request});
        QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
        QVERIFY(readySpy.constLast().at(1).value<QImage>().isNull());
        QCOMPARE(cache->missCount(), quint64(2));
        QCOMPARE(cache->pendingRequestCount(), qsizetype(0));

        left.shutdown();
        right.shutdown();
        decodeManager.prepareToClose();
    }
};

QTEST_GUILESS_MAIN(ThumbnailMemoryCacheTest)

#include "ThumbnailMemoryCacheTest.moc"
