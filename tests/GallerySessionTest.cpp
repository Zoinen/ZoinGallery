#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include "DecodeManager.h"
#include "Decoders/HeicDecoder.h"
#include "FileListModel.h"
#include "GalleryViewModel.h"
#include "PersistentImageCache.h"
#include "QmlAsyncImageProvider.h"
#include "src/embed/ExternalCatalogModel.h"
#include "tests/HeicTestFixture.h"

#include <QAbstractItemModel>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QtTest>

#include <algorithm>
#include <atomic>

namespace {

QVariantMap entry(const QString &id, int index, const QString &name,
                  bool image = false, bool selected = false,
                  qint64 version = 0) {
    return {
        {QStringLiteral("entryId"), id},
        {QStringLiteral("index"), index},
        {QStringLiteral("name"), name},
        {QStringLiteral("localPath"),
         QDir(QStringLiteral("/virtual/gallery-session-test")).filePath(name)},
        {QStringLiteral("isDir"), false},
        {QStringLiteral("isImage"), image},
        {QStringLiteral("selected"), selected},
        {QStringLiteral("mtimeNs"), version},
        {QStringLiteral("size"), 42},
    };
}

class TestSourceLease final : public ZoinGallery::ImageSourceLease {
public:
    explicit TestSourceLease(QString path) : _path(std::move(path)) {}
    QString localPath() const override { return _path; }

private:
    QString _path;
};

class MaterializingTestProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    explicit MaterializingTestProvider(QString path)
        : _path(std::move(path)) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64 offset,
        qint64 length,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        ++rangeReads;
        if (cancellation && cancellation->isCanceled()) {
            return {.errorString = QStringLiteral("canceled")};
        }
        QFile file(_path);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(offset)) {
            return {.errorString = file.errorString()};
        }
        const QByteArray data = file.read(length);
        return {.data = data, .endOfFile = file.atEnd()};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &source,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        lastResourceId = source.resourceId;
        ++materializations;
        // Give a second session time to join the runtime single-flight.
        QThread::msleep(100);
        if (cancellation && cancellation->isCanceled()) {
            return {};
        }
        return QSharedPointer<TestSourceLease>::create(_path);
    }

    std::atomic_int materializations{0};
    std::atomic_int rangeReads{0};
    QString lastResourceId;

private:
    QString _path;
};

class ProgressiveTestProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    explicit ProgressiveTestProvider(QString path)
        : _path(std::move(path)) {
        QImage tiny(2, 2, QImage::Format_RGBA8888);
        tiny.fill(Qt::yellow);
        QBuffer buffer(&_tinyPreview);
        buffer.open(QIODevice::WriteOnly);
        if (!tiny.save(&buffer, "PNG")) {
            _tinyPreview.clear();
        }
    }

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++rangeReads;
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    ZoinGallery::ImageSourceProbeResult probeEmbedded(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        if (cancellation && cancellation->isCanceled()) {
            return {
                .status = ZoinGallery::ImageSourceProbeStatus::Failed,
                .errorString = QStringLiteral("canceled"),
            };
        }
        ++completedProbes;
        return {
            .status = ZoinGallery::ImageSourceProbeStatus::Found,
            .encodedData = _tinyPreview,
            .mimeType = QStringLiteral("image/png"),
            .pixelSize = QSize(2, 2),
        };
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        int unset = -1;
        firstMaterializationAfterProbes.compare_exchange_strong(
            unset, completedProbes.load());
        ++materializations;
        if (cancellation && cancellation->isCanceled()) {
            return {};
        }
        return QSharedPointer<TestSourceLease>::create(_path);
    }

    std::atomic_int completedProbes{0};
    std::atomic_int firstMaterializationAfterProbes{-1};
    std::atomic_int materializations{0};
    std::atomic_int rangeReads{0};

private:
    QString _path;
    QByteArray _tinyPreview;
};

class BlockingCatalogProbeProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    ZoinGallery::ImageSourceProbeResult probeEmbedded(
        const ZoinGallery::ImageSourceDescriptor &source,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        if (source.sourceKey != QStringLiteral("network/unchanged")) {
            ++otherProbeCalls;
            return {
                .status = ZoinGallery::ImageSourceProbeStatus::NotFound,
            };
        }

        ++unchangedProbeCalls;
        unchangedProbeEntered.store(true, std::memory_order_release);
        while (!releaseUnchangedProbe.load(std::memory_order_acquire)) {
            if (cancellation && cancellation->isCanceled()) {
                unchangedProbeCanceled.store(true,
                                             std::memory_order_release);
                return {
                    .status = ZoinGallery::ImageSourceProbeStatus::Failed,
                    .errorString = QStringLiteral("canceled"),
                };
            }
            QThread::msleep(2);
        }
        if (cancellation && cancellation->isCanceled()) {
            unchangedProbeCanceled.store(true, std::memory_order_release);
            return {
                .status = ZoinGallery::ImageSourceProbeStatus::Failed,
                .errorString = QStringLiteral("canceled"),
            };
        }
        return {
            .status = ZoinGallery::ImageSourceProbeStatus::NotFound,
        };
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {};
    }

    std::atomic_int unchangedProbeCalls{0};
    std::atomic_int otherProbeCalls{0};
    std::atomic_bool unchangedProbeEntered{false};
    std::atomic_bool unchangedProbeCanceled{false};
    std::atomic_bool releaseUnchangedProbe{false};
};

class RetryingMaterializeProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    RetryingMaterializeProvider(QString path, bool failFirst)
        : _path(std::move(path)), _failFirst(failFirst) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &source,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        lastDescriptorSize.store(source.size, std::memory_order_release);
        const int call = materializations.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if ((_failFirst && call == 1) ||
            (cancellation && cancellation->isCanceled())) {
            return {};
        }
        return QSharedPointer<TestSourceLease>::create(_path);
    }

    std::atomic_int materializations{0};
    std::atomic<qint64> lastDescriptorSize{-2};

private:
    QString _path;
    bool _failFirst = false;
};

class BlockingThumbnailMaterializeProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    explicit BlockingThumbnailMaterializeProvider(QString path)
        : _path(std::move(path)) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        ++materializations;
        entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
            if (cancellation && cancellation->isCanceled()) {
                return {};
            }
            QThread::msleep(2);
        }
        if (cancellation && cancellation->isCanceled()) {
            return {};
        }
        return QSharedPointer<TestSourceLease>::create(_path);
    }

    std::atomic_int materializations{0};
    std::atomic_bool entered{false};
    std::atomic_bool release{false};

private:
    QString _path;
};

class ViewerProbeBarrierProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    ViewerProbeBarrierProvider(QString path, QString blockedSourceKey)
        : _path(std::move(path)),
          _blockedSourceKey(std::move(blockedSourceKey)) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    ZoinGallery::ImageSourceProbeResult probeEmbedded(
        const ZoinGallery::ImageSourceDescriptor &source,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        if (source.sourceKey == _blockedSourceKey) {
            blockedProbeEntered.store(true, std::memory_order_release);
            while (!releaseBlockedProbe.load(std::memory_order_acquire)) {
                if (cancellation && cancellation->isCanceled()) {
                    return {
                        .status = ZoinGallery::ImageSourceProbeStatus::Failed,
                        .errorString = QStringLiteral("canceled"),
                    };
                }
                QThread::msleep(2);
            }
        }
        if (cancellation && cancellation->isCanceled()) {
            return {
                .status = ZoinGallery::ImageSourceProbeStatus::Failed,
                .errorString = QStringLiteral("canceled"),
            };
        }
        ++completedProbes;
        return {
            .status = ZoinGallery::ImageSourceProbeStatus::NotFound,
        };
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        ++materializations;
        if (cancellation && cancellation->isCanceled()) {
            return {};
        }
        return QSharedPointer<TestSourceLease>::create(_path);
    }

    std::atomic_int completedProbes{0};
    std::atomic_int materializations{0};
    std::atomic_bool blockedProbeEntered{false};
    std::atomic_bool releaseBlockedProbe{false};

private:
    QString _path;
    QString _blockedSourceKey;
};

class FailFirstPerSourceProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    explicit FailFirstPerSourceProvider(QString path)
        : _path(std::move(path)) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    ZoinGallery::ImageSourceProbeResult probeEmbedded(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++completedProbes;
        return {
            .status = ZoinGallery::ImageSourceProbeStatus::NotFound,
        };
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &source,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation>
            &cancellation) override {
        int call = 0;
        {
            QMutexLocker locker(&_mutex);
            call = ++_calls[source.accessKey()];
        }
        ++materializations;
        if (call == 1 ||
            (cancellation && cancellation->isCanceled())) {
            return {};
        }
        return QSharedPointer<TestSourceLease>::create(_path);
    }

    std::atomic_int completedProbes{0};
    std::atomic_int materializations{0};

private:
    QString _path;
    QMutex _mutex;
    QHash<QString, int> _calls;
};

} // namespace

class GallerySessionTest : public QObject {
    Q_OBJECT

private slots:
    void relayoutKeepsExpensiveThumbnailOwnerAdmission() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("blocked-thumbnail.png"));
        QImage source(640, 480, QImage::Format_RGBA8888);
        source.fill(Qt::darkMagenta);
        QVERIFY(source.save(backing, "PNG"));

        auto provider =
            QSharedPointer<BlockingThumbnailMaterializeProvider>::create(
                backing);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("thumbnail-relayout-single-flight"));
        QVERIFY(session);

        const QFileInfo backingInfo(backing);
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("remote")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("remote.png")},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("resourceId"), QStringLiteral("resource:remote")},
            {QStringLiteral("sourceKey"), QStringLiteral("network/remote")},
            {QStringLiteral("contentVersion"), QStringLiteral("strong-v1")},
            {QStringLiteral("versionStrength"), QStringLiteral("strong")},
            {QStringLiteral("storageClass"), QStringLiteral("network")},
            {QStringLiteral("accessProfile"),
             QStringLiteral("materializeOnce")},
            {QStringLiteral("mimeType"), QStringLiteral("image/png")},
            {QStringLiteral("size"), backingInfo.size()},
        }}, 1));

        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);
        ImageDecodeRequest request{
            .info = imageFile->info(),
            .targetSize = QSize(257, 193),
            .highPriority = true,
        };
        model->decodeImages({request});
        QTRY_VERIFY_WITH_TIMEOUT(
            provider->entered.load(std::memory_order_acquire), 5000);

        // Masonry revokes its geometry lease before immediately asking for
        // the same stable remote tier again. The live decode remains the
        // shared owner and the second request must coalesce behind it.
        model->cancelAllDecodeRunners();
        model->decodeImages({request});
        provider->release.store(true, std::memory_order_release);

        QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 10000);
        QTest::qWait(250);
        QCOMPARE(readySpy.size(), 1);
        QCOMPARE(provider->materializations.load(), 1);

        runtime->shutdown();
    }

    void viewerPrefetchWaitsForCatalogProbeBarrier() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("viewer-barrier.png"));
        QImage source(320, 240, QImage::Format_RGBA8888);
        source.fill(Qt::darkBlue);
        QVERIFY(source.save(backing, "PNG"));

        constexpr int CatalogSize = 6;
        const QString blockedSourceKey =
            QStringLiteral("network/barrier/%1").arg(CatalogSize - 1);
        auto provider = QSharedPointer<ViewerProbeBarrierProvider>::create(
            backing, blockedSourceKey);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("viewer-probe-barrier"));
        QVERIFY(session);

        const QFileInfo backingInfo(backing);
        QVariantList catalog;
        for (int row = 0; row < CatalogSize; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("remote-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("remote-%1.png").arg(row)},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("resourceId"),
                 QStringLiteral("resource:%1").arg(row)},
                {QStringLiteral("sourceKey"),
                 QStringLiteral("network/barrier/%1").arg(row)},
                {QStringLiteral("contentVersion"),
                 QStringLiteral("strong-v1-%1").arg(row)},
                {QStringLiteral("versionStrength"),
                 QStringLiteral("strong")},
                {QStringLiteral("storageClass"),
                 QStringLiteral("network")},
                {QStringLiteral("accessProfile"),
                 QStringLiteral("materializeOnce")},
                {QStringLiteral("mimeType"),
                 QStringLiteral("image/png")},
                {QStringLiteral("size"), backingInfo.size()},
            });
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("remote-0"), 0, {}, 1));

        session->ensurePreviews();
        QTRY_VERIFY_WITH_TIMEOUT(
            provider->blockedProbeEntered.load(std::memory_order_acquire),
            5000);
        QCOMPARE(provider->completedProbes.load(), CatalogSize - 1);

        session->setViewerOpen(true);
        session->requestViewer(320, 240);
        QTRY_COMPARE_WITH_TIMEOUT(provider->materializations.load(), 1, 5000);
        QTest::qWait(300);
        // Explicit current may bypass pass 1 after its own probe. Neighbor
        // prefetch must not consume full-source bandwidth while one bounded
        // catalog probe is still outstanding.
        QCOMPARE(provider->materializations.load(), 1);

        provider->releaseBlockedProbe.store(true,
                                             std::memory_order_release);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider->completedProbes.load(), CatalogSize, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider->materializations.load(), CatalogSize, 15000);

        runtime->shutdown();
    }

    void catalogFitPrimesCanonicalThumbnailWithoutSecondSourceRead() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("catalog-thumbnail.png"));
        QImage source(640, 480, QImage::Format_RGBA8888);
        source.fill(Qt::darkCyan);
        QVERIFY(source.save(backing, "PNG"));

        constexpr int CatalogSize = 3;
        auto provider =
            QSharedPointer<MaterializingTestProvider>::create(backing);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("catalog-thumbnail-prime"));
        QVERIFY(session);

        const QFileInfo backingInfo(backing);
        QVariantList catalog;
        for (int row = 0; row < CatalogSize; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("remote-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("remote-%1.png").arg(row)},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("resourceId"),
                 QStringLiteral("resource:%1").arg(row)},
                {QStringLiteral("sourceKey"),
                 QStringLiteral("network/catalog-thumbnail/%1").arg(row)},
                {QStringLiteral("contentVersion"),
                 QStringLiteral("strong-v1-%1").arg(row)},
                {QStringLiteral("versionStrength"),
                 QStringLiteral("strong")},
                {QStringLiteral("storageClass"),
                 QStringLiteral("network")},
                {QStringLiteral("accessProfile"),
                 QStringLiteral("materializeOnce")},
                {QStringLiteral("mimeType"),
                 QStringLiteral("image/png")},
                {QStringLiteral("size"), backingInfo.size()},
            });
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        session->ensurePreviews();
        QTRY_COMPARE_WITH_TIMEOUT(
            provider->materializations.load(), CatalogSize, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(
            runtime->thumbnailCacheFrameCount(),
            qsizetype(CatalogSize), 10000);

        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        QVERIFY(imageFileRole >= 0);
        QVERIFY(imageUrlRole >= 0);
        const QModelIndex targetIndex = session->model()->index(2, 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(targetIndex, imageFileRole)
                .value<QObject *>());
        QVERIFY(imageFile);

        ImageDecodeRequest request{
            .info = imageFile->info(),
            .targetSize = QSize(128, 96),
            .highPriority = true,
            .thumbnailTransformKey = QStringLiteral(
                "thumbnail-aspect-v1"),
        };
        model->decodeImages({request});
        QTRY_VERIFY_WITH_TIMEOUT(
            !session->model()->data(targetIndex, imageUrlRole)
                 .toString().isEmpty(),
            2000);
        QCOMPARE(provider->materializations.load(), CatalogSize);

        runtime->shutdown();
    }

    void catalogDiffDoesNotResumeFitBeforeNewProbeBarrier() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("catalog-diff-barrier.png"));
        QImage source(160, 120, QImage::Format_RGBA8888);
        source.fill(Qt::darkYellow);
        QVERIFY(source.save(backing, "PNG"));

        const QString blockedSourceKey = QStringLiteral("network/diff/3");
        auto provider = QSharedPointer<ViewerProbeBarrierProvider>::create(
            backing, blockedSourceKey);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("catalog-diff-probe-barrier"));
        QVERIFY(session);

        const QFileInfo backingInfo(backing);
        const auto remoteEntry = [&backingInfo](int row) {
            return QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("remote-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("remote-%1.png").arg(row)},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("resourceId"),
                 QStringLiteral("resource:%1").arg(row)},
                {QStringLiteral("sourceKey"),
                 QStringLiteral("network/diff/%1").arg(row)},
                {QStringLiteral("contentVersion"),
                 QStringLiteral("strong-v1-%1").arg(row)},
                {QStringLiteral("versionStrength"),
                 QStringLiteral("strong")},
                {QStringLiteral("storageClass"),
                 QStringLiteral("network")},
                {QStringLiteral("accessProfile"),
                 QStringLiteral("materializeOnce")},
                {QStringLiteral("mimeType"),
                 QStringLiteral("image/png")},
                {QStringLiteral("size"), backingInfo.size()},
            };
        };

        QVariantList catalog{remoteEntry(0), remoteEntry(1)};
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        session->ensurePreviews();
        QTRY_COMPARE_WITH_TIMEOUT(provider->materializations.load(), 2, 10000);

        catalog.append(remoteEntry(2));
        catalog.append(remoteEntry(3));
        QVERIFY(session->applyExternalCatalog(catalog, 2));
        QTRY_VERIFY_WITH_TIMEOUT(
            provider->blockedProbeEntered.load(std::memory_order_acquire),
            5000);
        QTest::qWait(300);
        QCOMPARE(provider->materializations.load(), 2);

        provider->releaseBlockedProbe.store(true,
                                             std::memory_order_release);
        QTRY_COMPARE_WITH_TIMEOUT(provider->materializations.load(), 4, 10000);

        runtime->shutdown();
    }

    void backgroundRetriesDoNotStarveNinthSource() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("many-retries.png"));
        QImage source(96, 64, QImage::Format_RGBA8888);
        source.fill(Qt::darkGreen);
        QVERIFY(source.save(backing, "PNG"));

        constexpr int CatalogSize = 10;
        auto provider = QSharedPointer<FailFirstPerSourceProvider>::create(
            backing);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("background-retry-fairness"));
        QVERIFY(session);

        const QFileInfo backingInfo(backing);
        QVariantList catalog;
        for (int row = 0; row < CatalogSize; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("retry-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("retry-%1.png").arg(row)},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("resourceId"),
                 QStringLiteral("retry-resource:%1").arg(row)},
                {QStringLiteral("sourceKey"),
                 QStringLiteral("network/retry-fairness/%1").arg(row)},
                {QStringLiteral("contentVersion"),
                 QStringLiteral("strong-v1-%1").arg(row)},
                {QStringLiteral("versionStrength"),
                 QStringLiteral("strong")},
                {QStringLiteral("storageClass"),
                 QStringLiteral("network")},
                {QStringLiteral("accessProfile"),
                 QStringLiteral("materializeOnce")},
                {QStringLiteral("mimeType"),
                 QStringLiteral("image/png")},
                {QStringLiteral("size"), backingInfo.size()},
            });
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        session->ensurePreviews();

        const auto resolvedCount = [session] {
            int count = 0;
            for (int row = 0; row < CatalogSize; ++row) {
                if (session->imageOriginalSizeAt(row).isValid()) {
                    ++count;
                }
            }
            return count;
        };
        QTRY_COMPARE_WITH_TIMEOUT(resolvedCount(), CatalogSize, 15000);
        QCOMPARE(provider->materializations.load(), CatalogSize * 2);

        runtime->shutdown();
    }

    void versionStrengthChangeInvalidatesPublishedAuthority() {
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("authority-strength-change"));
        QVERIFY(session);

        QVariantMap imageEntry{
            {QStringLiteral("entryId"), QStringLiteral("remote")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("remote.png")},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("resourceId"),
             QStringLiteral("authority-a/resource")},
            {QStringLiteral("sourceKey"),
             QStringLiteral("network/authority-image")},
            {QStringLiteral("contentVersion"), QStringLiteral("revision-1")},
            {QStringLiteral("versionStrength"), QStringLiteral("strong")},
            {QStringLiteral("storageClass"), QStringLiteral("network")},
            {QStringLiteral("accessProfile"),
             QStringLiteral("materializeOnce")},
            {QStringLiteral("mimeType"), QStringLiteral("image/png")},
            {QStringLiteral("size"), 42},
        };
        QVERIFY(session->applyExternalCatalog({imageEntry}, 1));

        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        QVERIFY(imageFileRole >= 0);
        QVERIFY(imageUrlRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);

        ImageDecodeRequest result{
            .info = imageFile->info(),
            .targetSize = QSize(32, 24),
            .requestNamespace = session->sessionId(),
        };
        QImage decoded(32, 24, QImage::Format_ARGB32_Premultiplied);
        decoded.fill(Qt::red);
        decodeManager->imageReady(result, decoded, {});
        const QModelIndex modelIndex = session->model()->index(0, 0);
        QVERIFY(!session->model()->data(modelIndex, imageUrlRole)
                     .toString().isEmpty());

        imageEntry.insert(QStringLiteral("versionStrength"),
                          QStringLiteral("session"));
        QVERIFY(session->applyExternalCatalog({imageEntry}, 2));
        QVERIFY(session->model()->data(modelIndex, imageUrlRole)
                    .toString().isEmpty());

        // A queued completion carrying the former strong authority contract
        // must not republish into the newly resource-scoped session identity.
        decodeManager->imageReady(result, decoded, {});
        QVERIFY(session->model()->data(modelIndex, imageUrlRole)
                    .toString().isEmpty());

        runtime->shutdown();
    }

    void progressiveRemotePassesProbeThenFitThenFiveNative() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("remote-source.png"));
        QImage source(2400, 1800, QImage::Format_RGBA8888);
        source.fill(Qt::darkCyan);
        QVERIFY(source.save(backing, "PNG"));

        auto provider =
            QSharedPointer<ProgressiveTestProvider>::create(backing);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("progressive-vfs"));
        QVERIFY(session);

        const QFileInfo backingInfo(backing);
        QVariantList catalog;
        constexpr int CatalogSize = 7;
        for (int row = 0; row < CatalogSize; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("remote-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("remote-%1.png").arg(row)},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("resourceId"),
                 QStringLiteral("resource:%1").arg(row)},
                {QStringLiteral("sourceKey"),
                 QStringLiteral("network/source/%1").arg(row)},
                {QStringLiteral("contentVersion"),
                 QStringLiteral("strong-version-%1").arg(row)},
                {QStringLiteral("versionStrength"),
                 QStringLiteral("strong")},
                {QStringLiteral("storageClass"),
                 QStringLiteral("network")},
                {QStringLiteral("accessProfile"),
                 QStringLiteral("nativeRange")},
                {QStringLiteral("mimeType"),
                 QStringLiteral("image/png")},
                {QStringLiteral("size"), backingInfo.size()},
            });
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);

        session->ensurePreviews();
        QTRY_COMPARE_WITH_TIMEOUT(
            provider->completedProbes.load(), CatalogSize, 5000);
        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        QVERIFY(imageUrlRole >= 0);
        for (int row = 0; row < CatalogSize; ++row) {
            QTRY_VERIFY_WITH_TIMEOUT(
                !session->model()->data(
                    session->model()->index(row, 0), imageUrlRole)
                     .toString().isEmpty(),
                5000);
        }

        QTRY_COMPARE_WITH_TIMEOUT(
            provider->firstMaterializationAfterProbes.load(),
            CatalogSize, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            provider->materializations.load(), CatalogSize, 10000);
        QCOMPARE(provider->rangeReads.load(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            model->viewerFitFrameCount() >= CatalogSize, 20000);

        QVERIFY(session->applyExternalState(
            QStringLiteral("remote-3"), 3, {}, 1));
        session->setViewerOpen(true);
        // The catalog pass prepared the 1536px Fit tier. A tiny viewport grow
        // across that raw boundary must keep the same prepared URL instead of
        // decoding the same tier again and rejecting it as undersized.
        session->requestViewer(1536, 1152);
        QTRY_COMPARE_WITH_TIMEOUT(session->viewerSourceLevel(), 1, 5000);
        const QUrl stableFitSource = session->viewerSource();
        QVERIFY(!stableFitSource.isEmpty());
        session->requestViewer(1540, 1155);
        QCOMPARE(session->viewerSource(), stableFitSource);
        QTest::qWait(150);
        QCOMPARE(session->viewerSource(), stableFitSource);
        QCOMPARE(provider->materializations.load(), CatalogSize);

        session->requestViewer(0, 0);
        QTest::qWait(250);
        // Native work has a separate dwell after the current +/-2 Fit window
        // is ready. A brief native-mode request must remain Fit-only.
        QCOMPARE(model->viewerNativeFrameCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            model->viewerNativeFrameCount() == 5, 20000);
        // Fit and Native reuse the same bounded strong leases: no second
        // provider materialization and therefore no second network download.
        QCOMPARE(provider->materializations.load(), CatalogSize);

        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *center = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(3, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(center);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);
        ImageDecodeRequest tile{
            .info = center->info(),
            .targetSize = QSize(513, 342),
            .highPriority = true,
        };
        model->decodeImages({tile});
        QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 10000);
        tile.targetSize = QSize(514, 343);
        model->decodeImages({tile});
        QTest::qWait(150);
        // Both presentation sizes map to one 768px remote tier; layout jitter
        // neither decodes again nor re-enters the provider.
        QCOMPARE(readySpy.size(), 1);
        QCOMPARE(provider->materializations.load(), CatalogSize);

        runtime->shutdown();
    }

    void catalogDiffKeepsUnchangedInFlightProbeAlive() {
        auto provider =
            QSharedPointer<BlockingCatalogProbeProvider>::create();
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("targeted-catalog-diff"));
        QVERIFY(session);

        const auto remoteEntry = [](const QString &entryId,
                                    const QString &sourceKey,
                                    const QString &version) {
            return QVariantMap{
                {QStringLiteral("entryId"), entryId},
                {QStringLiteral("index"),
                 entryId == QStringLiteral("unchanged") ? 0 : 1},
                {QStringLiteral("name"), entryId + QStringLiteral(".jpg")},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("resourceId"),
                 QStringLiteral("resource:") + sourceKey},
                {QStringLiteral("sourceKey"), sourceKey},
                {QStringLiteral("contentVersion"), version},
                {QStringLiteral("versionStrength"),
                 QStringLiteral("strong")},
                {QStringLiteral("storageClass"),
                 QStringLiteral("network")},
                {QStringLiteral("accessProfile"),
                 QStringLiteral("nativeRange")},
                {QStringLiteral("mimeType"), QStringLiteral("image/jpeg")},
                {QStringLiteral("size"), 42},
            };
        };

        QVariantList catalog{
            remoteEntry(QStringLiteral("unchanged"),
                        QStringLiteral("network/unchanged"),
                        QStringLiteral("v1")),
            remoteEntry(QStringLiteral("changed"),
                        QStringLiteral("network/old"),
                        QStringLiteral("v1")),
        };
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        std::atomic_int unchangedResults{0};
        connect(decodeManager, &DecodeManager::imageProbeReady, this,
                [&unchangedResults](
                    const ZoinGallery::ImageProbeResult &result) {
                    if (result.request.source.sourceKey ==
                        QStringLiteral("network/unchanged")) {
                        ++unchangedResults;
                    }
                });

        session->ensurePreviews();
        QTRY_VERIFY_WITH_TIMEOUT(
            provider->unchangedProbeEntered.load(std::memory_order_acquire),
            5000);

        catalog[1] = remoteEntry(
            QStringLiteral("changed"), QStringLiteral("network/replacement"),
            QStringLiteral("v2"));
        const bool applied = session->applyExternalCatalog(catalog, 2);
        QTest::qWait(100);
        const bool canceledBeforeRelease =
            provider->unchangedProbeCanceled.load(std::memory_order_acquire);
        const int callsBeforeRelease =
            provider->unchangedProbeCalls.load(std::memory_order_acquire);
        provider->releaseUnchangedProbe.store(true,
                                              std::memory_order_release);

        QVERIFY(applied);
        QVERIFY(!canceledBeforeRelease);
        QCOMPARE(callsBeforeRelease, 1);
        QTRY_COMPARE_WITH_TIMEOUT(unchangedResults.load(), 1, 5000);
        QTest::qWait(100);
        QCOMPARE(provider->unchangedProbeCalls.load(), 1);
        QVERIFY(provider->otherProbeCalls.load() >= 1);

        runtime->shutdown();
    }

    void unknownSourceSizeRetriesTransportFailureWithoutPoisoningAdmission() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("retry-source.png"));
        QImage source(80, 60, QImage::Format_RGBA8888);
        source.fill(Qt::darkGreen);
        QVERIFY(source.save(backing, "PNG"));
        const QFileInfo backingInfo(backing);

        auto provider = QSharedPointer<RetryingMaterializeProvider>::create(
            backing, true);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 2;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("unknown-size-transport-retry"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("retry")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("retry-source.png")},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("resourceId"), QStringLiteral("resource:retry")},
            {QStringLiteral("sourceKey"), QStringLiteral("network/retry")},
            {QStringLiteral("contentVersion"), QStringLiteral("strong-v1")},
            {QStringLiteral("versionStrength"), QStringLiteral("strong")},
            {QStringLiteral("storageClass"), QStringLiteral("network")},
            {QStringLiteral("accessProfile"),
             QStringLiteral("materializeOnce")},
            {QStringLiteral("mimeType"), QStringLiteral("image/png")},
            // A stale numeric placeholder must be ignored when the host says
            // the source size is not authoritative.
            {QStringLiteral("size"), backingInfo.size()},
            {QStringLiteral("sizeKnown"), false},
        }}, 1));

        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);
        QCOMPARE(imageFile->info().source.size, qint64(-1));
        QCOMPARE(imageFile->info().fileSize, qint64(-1));

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy failedSpy(decodeManager,
                             &DecodeManager::imageReadFailed);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);
        QSignalSpy changedSpy(session->model(),
                              &QAbstractItemModel::dataChanged);

        ImageDecodeRequest request;
        request.info = imageFile->info();
        // Simulate a materialized QFile discovering its real size. Admission
        // must still be released with the host-authoritative -1 key.
        request.info.fileSize = backingInfo.size();
        request.targetSize = QSize(40, 30);
        request.highPriority = true;
        model->decodeImages({request});

        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 5000);
        QVERIFY(failedSpy.constFirst().at(0)
                    .value<ImageDecodeRequest>().sourceAccessFailed);
        QCOMPARE(provider->materializations.load(), 1);
        QCOMPARE(provider->lastDescriptorSize.load(), qint64(-1));

        // Transport retry is delayed, so a disconnected backend cannot spin
        // through materializations on synchronous layout notifications.
        QTest::qWait(350);
        QCOMPARE(changedSpy.size(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(changedSpy.size() >= 1, 1000);

        model->decodeImages({request});
        QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
        QVERIFY(!readySpy.constFirst().at(1).value<QImage>().isNull());
        QCOMPARE(provider->materializations.load(), 2);
        QCOMPARE(provider->lastDescriptorSize.load(), qint64(-1));

        runtime->shutdown();
    }

    void corruptMaterializedSourceStaysTerminalUntilExplicitRetry() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString corruptPath = directory.filePath(
            QStringLiteral("corrupt-source.heic"));
        QFile corrupt(corruptPath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        QCOMPARE(corrupt.write("not-a-heif-image"), qint64(16));
        corrupt.close();

        auto provider = QSharedPointer<RetryingMaterializeProvider>::create(
            corruptPath, false);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 2;
        options.persistentCache = false;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("corrupt-terminal"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("corrupt")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("corrupt-source.heic")},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("resourceId"),
             QStringLiteral("resource:corrupt")},
            {QStringLiteral("sourceKey"), QStringLiteral("network/corrupt")},
            {QStringLiteral("contentVersion"), QStringLiteral("strong-v1")},
            {QStringLiteral("versionStrength"), QStringLiteral("strong")},
            {QStringLiteral("storageClass"), QStringLiteral("network")},
            {QStringLiteral("accessProfile"),
             QStringLiteral("materializeOnce")},
            {QStringLiteral("mimeType"), QStringLiteral("image/heic")},
            {QStringLiteral("size"), QFileInfo(corruptPath).size()},
        }}, 1));

        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy failedSpy(decodeManager,
                             &DecodeManager::imageReadFailed);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);
        ImageDecodeRequest request;
        request.info = imageFile->info();
        request.targetSize = QSize(32, 32);
        request.highPriority = true;
        model->decodeImages({request});

        QTRY_VERIFY_WITH_TIMEOUT(
            readySpy.size() + failedSpy.size() == 1, 5000);
        if (!readySpy.isEmpty()) {
            QVERIFY(readySpy.constFirst().at(1).value<QImage>().isNull());
        }
        if (!failedSpy.isEmpty()) {
            QVERIFY(!failedSpy.constFirst().at(0)
                         .value<ImageDecodeRequest>().sourceAccessFailed);
        }
        const int firstReadyCount = readySpy.size();
        const int firstFailedCount = failedSpy.size();

        // Decoder corruption is terminal for this automatic request. Only a
        // later explicit layout/user request is allowed to try it again.
        QTest::qWait(750);
        QCOMPARE(readySpy.size(), firstReadyCount);
        QCOMPARE(failedSpy.size(), firstFailedCount);

        model->decodeImages({request});
        QTRY_VERIFY_WITH_TIMEOUT(
            readySpy.size() + failedSpy.size() == 2, 5000);

        runtime->shutdown();
    }

    void externalCatalogUsesOpaqueSourceIdentityAndSharedMaterialization() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing =
            directory.filePath(QStringLiteral("materialized-blob"));
        QImage source(48, 32, QImage::Format_RGBA8888);
        source.fill(Qt::cyan);
        QVERIFY(source.save(backing, "PNG"));

        auto provider =
            QSharedPointer<MaterializingTestProvider>::create(backing);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.imageSourceProvider = provider;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *left =
            runtime->createExternalSession(QStringLiteral("vfs-left"));
        ZoinGallery::GallerySession *right =
            runtime->createExternalSession(QStringLiteral("vfs-right"));
        QVERIFY(left);
        QVERIFY(right);

        const QFileInfo backingInfo(backing);
        const QString opaqueVersion =
            QStringLiteral("etag:\"abc\"/revision#42");
        const QVariantMap remoteEntry{
            {QStringLiteral("entryId"), QStringLiteral("remote-image")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("remote.png")},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("resourceId"), QStringLiteral("resource:7")},
            {QStringLiteral("sourceKey"),
             QStringLiteral("cloudfox/google/item-7")},
            {QStringLiteral("contentVersion"), opaqueVersion},
            {QStringLiteral("versionStrength"), QStringLiteral("strong")},
            {QStringLiteral("storageClass"), QStringLiteral("remote")},
            {QStringLiteral("accessProfile"), QStringLiteral("hybrid-range")},
            {QStringLiteral("mimeType"), QStringLiteral("image/png")},
            {QStringLiteral("size"), backingInfo.size()},
        };
        QVERIFY(left->applyExternalCatalog({remoteEntry}, 1));
        QVERIFY(right->applyExternalCatalog({remoteEntry}, 1));
        QVERIFY(left->localPathAt(0).isEmpty());
        QCOMPARE(left->model()->data(
                     left->model()->index(0, 0),
                     left->model()->roleNames().key("versionToken"))
                     .toString(),
                 opaqueVersion);

        auto *leftModel = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            left->model());
        auto *rightModel = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            right->model());
        QVERIFY(leftModel);
        QVERIFY(rightModel);
        leftModel->requestImageMetadata({0}, true);
        rightModel->requestImageMetadata({0}, true);
        QTRY_COMPARE_WITH_TIMEOUT(leftModel->imageOriginalSizeAt(0),
                                  QSize(48, 32), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(rightModel->imageOriginalSizeAt(0),
                                  QSize(48, 32), 5000);
        QCOMPARE(provider->materializations.load(), 1);
        QCOMPARE(provider->lastResourceId, QStringLiteral("resource:7"));

        runtime->shutdown();
    }

    void scansAndWatchesLocalFilesystemSession() {
        QTemporaryDir leftDirectory;
        QTemporaryDir rightDirectory;
        QVERIFY(leftDirectory.isValid());
        QVERIFY(rightDirectory.isValid());

        QVERIFY(QDir(leftDirectory.path()).mkdir(QStringLiteral("folder")));
        QFile textFile(leftDirectory.filePath(QStringLiteral("note.txt")));
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        QVERIFY(textFile.write("hello") == 5);
        textFile.close();
        QImage image(24, 18, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::magenta);
        const QString imagePath =
            leftDirectory.filePath(QStringLiteral("pixel.png"));
        QVERIFY(image.save(imagePath));

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *left =
            runtime->createSession(QStringLiteral("local/left"));
        ZoinGallery::GallerySession *right =
            runtime->createSession(QStringLiteral("local-right"));
        QVERIFY(left);
        QVERIFY(right);
        QCOMPARE(left->sourceKind(),
                 ZoinGallery::GallerySession::LocalFilesystemSource);
        QVERIFY(left->localSource());
        QVERIFY(left->fileListModel());
        QVERIFY(left->galleryViewModel());
        QVERIFY(left->selectedImagesModel());
        QVERIFY(left->imageModel());
        QCOMPARE(left->sessionId(), QStringLiteral("local_left"));
        QCOMPARE(runtime->createSession(QStringLiteral("local_left")), left);

        QVERIFY(left->cd(leftDirectory.path()) >= 0);
        QCOMPARE(right->cd(rightDirectory.path()), -1);
        QCOMPARE(left->currentPath(), leftDirectory.path());
        QTRY_COMPARE_WITH_TIMEOUT(left->model()->rowCount(), 3, 5000);
        auto *leftGallery = qobject_cast<GalleryViewModel *>(
            left->galleryViewModel());
        QVERIFY(leftGallery);
        leftGallery->setSortMode(GalleryViewModel::NameAscending);
        QCOMPARE(left->entryNameAt(0), QStringLiteral("folder"));
        QCOMPARE(left->entryNameAt(1), QStringLiteral("note.txt"));
        QCOMPARE(left->entryNameAt(2), QStringLiteral("pixel.png"));
        QCOMPARE(left->entryIdAt(2), imagePath);
        QVERIFY(left->isDirectoryAt(0));
        QVERIFY(left->isImageAt(2));

        const qulonglong selectionRevision = left->selectionRevision();
        left->requestToggleSelection(2);
        QTRY_VERIFY_WITH_TIMEOUT(
            left->selectionRevision() > selectionRevision, 2000);
        QVERIFY(left->isSelectedAt(2));

        QFile watchedFile(
            leftDirectory.filePath(QStringLiteral("watched.txt")));
        QVERIFY(watchedFile.open(QIODevice::WriteOnly));
        QVERIFY(watchedFile.write("watch") == 5);
        watchedFile.close();
        QTRY_COMPARE_WITH_TIMEOUT(left->model()->rowCount(), 4, 7000);
        QVERIFY(left->catalogRevision() > 0);

        left->shutdown();
        QVERIFY(left->shutdownComplete());
        QVERIFY(!right->shutdownComplete());
        runtime->shutdown();
        QVERIFY(right->shutdownComplete());
    }

    void localViewerPrefetchFollowsSortedProxyOrder() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        constexpr int imageCount = 24;
        for (int sourceRow = 0; sourceRow < imageCount; ++sourceRow) {
            const QString path = directory.filePath(
                QStringLiteral("image-%1.png")
                    .arg(sourceRow, 2, 10, QLatin1Char('0')));
            QImage image(640, 480, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromHsv((sourceRow * 29) % 360, 190, 220));
            QVERIFY2(image.save(path), qPrintable(path));

            // Deliberately make modified-time order unrelated to the raw
            // name/source order. Seven is coprime with 24, so every rank is
            // unique and the proxy has a deterministic permutation.
            QFile stampedFile(path);
            QVERIFY(stampedFile.open(QIODevice::ReadWrite));
            const qint64 modifiedRank = (sourceRow * 7) % imageCount;
            QVERIFY(stampedFile.setFileTime(
                QDateTime::fromMSecsSinceEpoch(
                    1'700'000'000'000LL + modifiedRank * 1000,
                    QTimeZone::UTC),
                QFileDevice::FileModificationTime));
        }

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createSession(QStringLiteral("sorted-prefetch"));
        QVERIFY(session);
        QVERIFY(session->cd(directory.path()) >= 0);
        QTRY_COMPARE_WITH_TIMEOUT(session->model()->rowCount(), imageCount,
                                  5000);

        auto *fileList = qobject_cast<FileListModel *>(
            session->fileListModel());
        auto *gallery = qobject_cast<GalleryViewModel *>(
            session->galleryViewModel());
        QVERIFY(fileList);
        QVERIFY(gallery);
        gallery->setSortMode(GalleryViewModel::ModifiedDescending);

        constexpr int centerViewRow = 12;
        QTRY_VERIFY_WITH_TIMEOUT(
            gallery->mapToSourceRow(centerViewRow) != centerViewRow, 2000);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (int viewRow = 0; viewRow < imageCount; ++viewRow) {
                if (session->imageOriginalSizeAt(viewRow) != QSize(640, 480)) {
                    return false;
                }
            }
            return true;
        }(), 10000);

        const QVariantList orderedRows =
            gallery->viewerPrefetchSourceRows(centerViewRow, 16);
        QCOMPARE(orderedRows.size(), 16);
        QSet<int> expectedSourceRows;
        for (const QVariant &row : orderedRows) {
            expectedSourceRows.insert(row.toInt());
        }
        QCOMPARE(expectedSourceRows.size(), 16);

        session->setCurrentIndex(centerViewRow);
        session->setViewerOpen(true);
        session->requestViewer(320, 240);

        const auto preparedFitSourceRows = [&] {
            QSet<int> result;
            for (int sourceRow = 0; sourceRow < imageCount; ++sourceRow) {
                const auto sources = fileList->viewerImageSourcesForIndex(
                    sourceRow, QSize(320, 240));
                const bool hasPreparedFit = std::any_of(
                    sources.cbegin(), sources.cend(),
                    [](const auto &source) { return source.second == 1; });
                if (hasPreparedFit) {
                    result.insert(sourceRow);
                }
            }
            return result;
        };
        QTRY_COMPARE_WITH_TIMEOUT(preparedFitSourceRows(), expectedSourceRows,
                                  15000);

        for (const int sourceRow : std::as_const(expectedSourceRows)) {
            const auto sources = fileList->viewerImageSourcesForIndex(
                sourceRow, QSize(320, 240));
            const auto fit = std::find_if(
                sources.crbegin(), sources.crend(),
                [](const auto &source) { return source.second == 1; });
            QVERIFY(fit != sources.crend());
            const QString imageId = fit->first.section(QLatin1Char('/'), -1);
            const QSize preparedSize =
                fileList->viewerForImageId(imageId).size();
            QVERIFY(preparedSize.width() >= 320);
            QVERIFY(preparedSize.height() >= 240);
        }

        const int nextViewRow =
            session->adjacentImageIndex(centerViewRow, 1);
        const int nextSourceRow = gallery->mapToSourceRow(nextViewRow);
        const auto nextSources = fileList->viewerImageSourcesForIndex(
            nextSourceRow, QSize(320, 240));
        QVERIFY(!nextSources.isEmpty());
        const QUrl prefetchedNextSource(nextSources.constLast().first);
        session->setCurrentIndex(nextViewRow);
        session->requestViewer(320, 240);
        QCOMPARE(session->viewerSource(), prefetchedNextSource);

        // Do not leak this test's persistent standalone sort preference into
        // subsequent local-session tests or developer runs.
        gallery->setSortMode(GalleryViewModel::NameAscending);
        runtime->shutdown();
    }

    void localPreparedViewerTierUsesExplicitSizeNotLastRequestMode() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString currentPath =
            directory.filePath(QStringLiteral("a-current.png"));
        const QString neighborPath =
            directory.filePath(QStringLiteral("b-neighbor.png"));
        QImage currentImage(1800, 1200,
                            QImage::Format_ARGB32_Premultiplied);
        currentImage.fill(Qt::red);
        QVERIFY(currentImage.save(currentPath));
        QImage neighborImage(1200, 1800,
                             QImage::Format_ARGB32_Premultiplied);
        neighborImage.fill(Qt::blue);
        QVERIFY(neighborImage.save(neighborPath));

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 3;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createSession(QStringLiteral("explicit-viewer-tier"));
        QVERIFY(session);
        QVERIFY(session->cd(directory.path()) >= 0);
        QTRY_COMPARE_WITH_TIMEOUT(session->model()->rowCount(), 2, 5000);

        auto *fileList = qobject_cast<FileListModel *>(
            session->fileListModel());
        auto *gallery = qobject_cast<GalleryViewModel *>(
            session->galleryViewModel());
        QVERIFY(fileList);
        QVERIFY(gallery);
        gallery->setSortMode(GalleryViewModel::NameAscending);
        QTRY_COMPARE_WITH_TIMEOUT(
            session->imageOriginalSizeAt(0), QSize(1800, 1200), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(
            session->imageOriginalSizeAt(1), QSize(1200, 1800), 10000);

        constexpr int currentViewRow = 0;
        constexpr int neighborViewRow = 1;
        const int neighborSourceRow =
            gallery->mapToSourceRow(neighborViewRow);
        QVERIFY(neighborSourceRow >= 0);
        session->setCurrentIndex(currentViewRow);
        session->setViewerOpen(true);

        const QSize fitViewport(640, 420);
        const QSize expectedFitSize = QSize(1200, 1800).scaled(
            fitViewport, Qt::KeepAspectRatio);
        fileList->requestViewerAt(neighborSourceRow, fitViewport.width(),
                                  fitViewport.height());
        QString fitUrl;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            fitUrl = fileList->preparedViewerImageUrlForIndex(
                neighborSourceRow, fitViewport.width(),
                fitViewport.height());
            if (fitUrl.isEmpty()) {
                return false;
            }
            const QString imageId =
                fitUrl.section(QLatin1Char('/'), -1);
            const QSize preparedSize =
                fileList->viewerForImageId(imageId).size();
            return preparedSize.width() >= expectedFitSize.width()
                && preparedSize.height() >= expectedFitSize.height();
        }(), 15000);

        // Populate the native tier after Fit so an implicit "best image"
        // lookup would be governed by the wrong last request mode.  The
        // explicitly-sized presentation query must remain stable.
        fileList->requestViewerAt(neighborSourceRow, -1, -1);
        QString nativeUrl;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            nativeUrl = fileList->preparedViewerImageUrlForIndex(
                neighborSourceRow, -1, -1);
            if (nativeUrl.isEmpty()) {
                return false;
            }
            const QString imageId =
                nativeUrl.section(QLatin1Char('/'), -1);
            return fileList->fullSizeViewerForImageId(imageId).size()
                == QSize(1200, 1800);
        }(), 15000);
        QVERIFY(nativeUrl != fitUrl);
        QCOMPARE(fileList->preparedViewerImageUrlForIndex(
                     neighborSourceRow, fitViewport.width(),
                     fitViewport.height()),
                 fitUrl);
        QCOMPARE(session->currentIndex(), currentViewRow);

        // Keep developer and later-test standalone preferences deterministic.
        gallery->setSortMode(GalleryViewModel::NameAscending);
        runtime->shutdown();
    }

    void destroyedLocalSessionDisconnectsSharedManagerCallbacks() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("survivor.bmp"));
        QImage source(96, 64, QImage::Format_RGB32);
        source.fill(QColor(QStringLiteral("#4c6ef5")));
        QVERIFY(source.save(imagePath, "BMP"));

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 3;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);

        auto *shortLivedOwner = new QObject;
        QPointer<ZoinGallery::GallerySession> shortLived =
            runtime->createSession(QStringLiteral("short-lived"),
                                   shortLivedOwner);
        ZoinGallery::GallerySession *survivor =
            runtime->createSession(QStringLiteral("survivor"));
        QVERIFY(shortLived);
        QVERIFY(survivor);
        QPointer<QObject> destroyedFileList =
            shortLived->fileListModel();
        auto *survivingFileList = qobject_cast<FileListModel *>(
            survivor->fileListModel());
        QVERIFY(destroyedFileList);
        QVERIFY(survivingFileList);

        QVERIFY(survivor->cd(directory.path()) >= 0);
        QTRY_COMPARE_WITH_TIMEOUT(survivor->model()->rowCount(), 1, 5000);
        QCOMPARE(survivor->localPathAt(0), imagePath);

        // The external owner destroys GallerySession and its FileListModel,
        // while GalleryRuntime and its shared DecodeManager deliberately stay
        // alive for the other panel.
        delete shortLivedOwner;
        QVERIFY(shortLived.isNull());
        QVERIFY(destroyedFileList.isNull());
        QVERIFY(!survivor->shutdownComplete());

        QSignalSpy survivingTasksSpy(
            survivingFileList, &FileListModel::runningTasksChanged);
        // This synchronous emission deterministically enters every remaining
        // connection. Before the QObject context fix it also entered the
        // lambda capturing the deleted FileListModel and caused a UAF.
        decodeManager->runningTasksChanged(
            QStringLiteral("lifetime-probe"),
            {QStringLiteral("manual shared-manager emission")});
        QCOMPARE(survivingTasksSpy.size(), 1);

        QSignalSpy decodedSpy(decodeManager, &DecodeManager::imageReady);
        ImageDecodeRequest request;
        request.info.path = imagePath;
        request.info.lastModified = QFileInfo(imagePath).lastModified();
        request.info.fileSize = QFileInfo(imagePath).size();
        request.info.imageSize = source.size();
        request.targetSize = QSize(48, 32);
        request.requestNamespace =
            QStringLiteral("zoingallery.local.survivor.catalog");
        request.viewerRequest = false;
        request.checkCache = false;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = false;
        decodeManager->decodeImages({request});

        QTRY_VERIFY_WITH_TIMEOUT(decodedSpy.size() >= 1, 5000);
        bool matchingDecodeCompleted = false;
        for (const QList<QVariant> &arguments : decodedSpy) {
            const ImageDecodeRequest completed =
                qvariant_cast<ImageDecodeRequest>(arguments.at(0));
            const QImage decoded = qvariant_cast<QImage>(arguments.at(1));
            if (completed.requestNamespace == request.requestNamespace &&
                completed.info.path == imagePath && !decoded.isNull()) {
                matchingDecodeCompleted = true;
                QCOMPARE(decoded.size(), request.targetSize);
                break;
            }
        }
        QVERIFY(matchingDecodeCompleted);
        QTRY_VERIFY_WITH_TIMEOUT(survivingTasksSpy.size() > 1, 5000);
        QCOMPARE(survivor->model()->rowCount(), 1);
        QCOMPARE(survivor->entryNameAt(0), QStringLiteral("survivor.bmp"));

        runtime->shutdown();
        QVERIFY(survivor->shutdownComplete());
    }

    void destroyLocalSessionDuringActiveDecodeKeepsSharedRuntimeUsable() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString largeImagePath =
            directory.filePath(QStringLiteral("active-session.png"));
        QImage largeImage(3072, 3072,
                          QImage::Format_ARGB32_Premultiplied);
        largeImage.fill(QColor(QStringLiteral("#6c5ce7")));
        QVERIFY(largeImage.save(largeImagePath, "PNG"));

        const QString survivorImagePath =
            directory.filePath(QStringLiteral("survivor.bmp"));
        QImage survivorImage(96, 64, QImage::Format_RGB32);
        survivorImage.fill(QColor(QStringLiteral("#00b894")));
        QVERIFY(survivorImage.save(survivorImagePath, "BMP"));

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 3;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);

        auto *shortLivedOwner = new QObject;
        QPointer<ZoinGallery::GallerySession> shortLived =
            runtime->createSession(QStringLiteral("active-short-lived"),
                                   shortLivedOwner);
        ZoinGallery::GallerySession *survivor =
            runtime->createSession(QStringLiteral("active-survivor"));
        QVERIFY(shortLived);
        QVERIFY(survivor);
        auto *shortLivedModel = qobject_cast<FileListModel *>(
            shortLived->fileListModel());
        auto *survivorModel = qobject_cast<FileListModel *>(
            survivor->fileListModel());
        QVERIFY(shortLivedModel);
        QVERIFY(survivorModel);
        QPointer<QObject> destroyedFileList = shortLivedModel;

        ImageDecodeRequest largeRequest;
        largeRequest.info.path = largeImagePath;
        largeRequest.info.lastModified =
            QFileInfo(largeImagePath).lastModified();
        largeRequest.info.fileSize = QFileInfo(largeImagePath).size();
        largeRequest.info.imageSize = largeImage.size();
        largeRequest.targetSize = largeImage.size();
        largeRequest.viewerRequest = true;
        largeRequest.checkCache = false;
        largeRequest.expandToCacheResolution = false;
        largeRequest.storeInPersistentCache = false;

        // Keep both decode workers occupied long enough for the main thread
        // to observe an active task before destroying its owning session.
        QList<ImageDecodeRequest> largeRequests;
        largeRequests.fill(largeRequest, 12);
        QSignalSpy tasksSpy(
            decodeManager, &DecodeManager::runningTasksChanged);
        shortLivedModel->decodeImages(largeRequests);

        const auto observedActiveWorker = [&tasksSpy] {
            for (const QList<QVariant> &arguments : tasksSpy) {
                bool ok = false;
                const int active = arguments.at(0)
                                       .toString()
                                       .section(QLatin1Char('/'), 0, 0)
                                       .toInt(&ok);
                if (ok && active > 0) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(observedActiveWorker(), 5000);

        delete shortLivedOwner;
        QVERIFY(shortLived.isNull());
        QVERIFY(destroyedFileList.isNull());
        QVERIFY(!survivor->shutdownComplete());

        QSignalSpy decodedSpy(decodeManager, &DecodeManager::imageReady);
        ImageDecodeRequest survivorRequest;
        survivorRequest.info.path = survivorImagePath;
        survivorRequest.info.lastModified =
            QFileInfo(survivorImagePath).lastModified();
        survivorRequest.info.fileSize =
            QFileInfo(survivorImagePath).size();
        survivorRequest.info.imageSize = survivorImage.size();
        survivorRequest.targetSize = QSize(48, 32);
        survivorRequest.viewerRequest = false;
        survivorRequest.checkCache = false;
        survivorRequest.expandToCacheResolution = false;
        survivorRequest.storeInPersistentCache = false;
        survivorModel->decodeImages({survivorRequest});

        const QString survivorNamespace =
            QStringLiteral("zoingallery.local.active-survivor.catalog");
        const auto survivorDecodeCompleted = [&decodedSpy,
                                               &survivorImagePath,
                                               &survivorNamespace] {
            for (const QList<QVariant> &arguments : decodedSpy) {
                const ImageDecodeRequest completed =
                    qvariant_cast<ImageDecodeRequest>(arguments.at(0));
                const QImage decoded =
                    qvariant_cast<QImage>(arguments.at(1));
                if (completed.requestNamespace == survivorNamespace &&
                    completed.info.path == survivorImagePath &&
                    decoded.size() == QSize(48, 32)) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(survivorDecodeCompleted(), 10000);

        runtime->shutdown();
        QVERIFY(survivor->shutdownComplete());
    }

    void preservesExternalOrderAndStableState() {
        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("left"));
        QVERIFY(session);

        const QVariantList first{
            entry(QStringLiteral("b"), 8, QStringLiteral("b.txt"), false,
                  false, 1234567891),
            entry(QStringLiteral("a"), 3, QStringLiteral("a.txt"), false,
                  true, 1234567892),
            entry(QStringLiteral("c"), 12, QStringLiteral("c.txt"), false,
                  false, 1234567893),
        };
        QVERIFY(session->applyExternalCatalog(
            first, 10, {{QStringLiteral("currentPath"),
                         QStringLiteral("/virtual/gallery-session-test")}}));
        QCOMPARE(session->currentPath(),
                 QStringLiteral("/virtual/gallery-session-test"));
        QCOMPARE(session->entryIdAt(0), QStringLiteral("b"));
        QCOMPARE(session->entryIdAt(1), QStringLiteral("a"));
        QCOMPARE(session->sourceIndexAt(0), 8);
        QCOMPARE(session->sourceIndexAt(1), 3);

        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        ImageFile *firstImageFile = qvariant_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole));
        QVERIFY(firstImageFile);
        QSignalSpy appearanceReset(session->model(),
                                   &QAbstractItemModel::modelReset);
        const QVariantMap appearanceStyle{
            {QStringLiteral("icon"), QStringLiteral("qrc:/custom/archive.svg")},
            {QStringLiteral("normal"), QVariantMap{
                 {QStringLiteral("foreground"), QStringLiteral("#112233")}}},
        };
        QVERIFY(session->applyExternalAppearance(
            {QVariantMap{{QStringLiteral("entryId"), QStringLiteral("b")},
                         {QStringLiteral("highlightStyle"), appearanceStyle}}},
            7));
        QCOMPARE(appearanceReset.size(), 0);
        QCOMPARE(qvariant_cast<ImageFile *>(session->model()->data(
                     session->model()->index(0, 0), imageFileRole)),
                 firstImageFile);
        QCOMPARE(firstImageFile->highlightStyle(), appearanceStyle);
        QCOMPARE(session->highlightStyleAt(0), appearanceStyle);
        QVERIFY(session->highlightStyleAt(-1).isEmpty());
        QVERIFY(session->highlightStyleAt(session->model()->rowCount()).isEmpty());
        QCOMPARE(firstImageFile->iconPath(),
                 QStringLiteral("qrc:/custom/archive.svg"));

        QSignalSpy scrollChanges(
            session, &ZoinGallery::GallerySession::panelScrollOffsetChanged);
        session->setPanelScrollOffset(91.5);
        QCOMPARE(session->panelScrollOffset(), qreal(91.5));
        QCOMPARE(scrollChanges.size(), 1);

        const QHash<int, QByteArray> roles = session->model()->roleNames();
        const int versionRole = roles.key(QByteArrayLiteral("versionToken"), -1);
        QVERIFY(versionRole >= 0);
        QCOMPARE(session->model()->data(session->model()->index(0, 0),
                                        versionRole).toLongLong(),
                 qint64(1234567891));

        QVERIFY(session->applyExternalState(
            QStringLiteral("c"), 0, {QStringLiteral("a")}, 20));
        QCOMPARE(session->currentIndex(), 2);
        QCOMPARE(session->cursorEntryId(), QStringLiteral("c"));
        QCOMPARE(session->selectionRevision(), qulonglong(20));
        QVERIFY(!session->isSelectedAt(0));
        QVERIFY(session->isSelectedAt(1));
        QVERIFY(!session->isSelectedAt(2));

        // Cursor-only acknowledgements carry the unchanged selection
        // revision. They must not rescan or rewrite the entire catalog.
        const int selectedRole = roles.key(
            QByteArrayLiteral("selectedRole"), -1);
        QVERIFY(selectedRole >= 0);
        QSignalSpy cursorOnlyDataChanges(
            session->model(), &QAbstractItemModel::dataChanged);
        QVERIFY(session->applyExternalState(
            QStringLiteral("b"), 0, {}, 20));
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(session->model()->data(session->model()->index(1, 0),
                                        selectedRole).toBool(),
                 true);
        QCOMPARE(cursorOnlyDataChanges.size(), 0);
        QVERIFY(session->applyExternalState(
            QStringLiteral("c"), 2, {QStringLiteral("a")}, 20));
        QCOMPARE(cursorOnlyDataChanges.size(), 0);

        QSignalSpy sparseSelectionChanges(
            session->model(), &QAbstractItemModel::dataChanged);
        const QVariantList delta{
            QVariantMap{{QStringLiteral("index"), 8},
                        {QStringLiteral("entryId"), QStringLiteral("b")},
                        {QStringLiteral("selected"), true}},
            QVariantMap{{QStringLiteral("index"), 3},
                        {QStringLiteral("entryId"), QStringLiteral("a")},
                        {QStringLiteral("selected"), false}},
        };
        QVERIFY(session->applyExternalStateDelta(
            QStringLiteral("c"), 2, delta, 20, 21));
        QCOMPARE(session->selectionRevision(), qulonglong(21));
        QCOMPARE(session->currentIndex(), 2);
        QVERIFY(session->isSelectedAt(0));
        QVERIFY(!session->isSelectedAt(1));
        QCOMPARE(sparseSelectionChanges.size(), 1);

        // Revision mismatch and one invalid identity both reject the complete
        // operation; the valid first row must not be changed partially.
        QVERIFY(!session->applyExternalStateDelta(
            QStringLiteral("c"), 2, delta, 20, 22));
        const QVariantList invalidDelta{
            QVariantMap{{QStringLiteral("index"), 8},
                        {QStringLiteral("entryId"), QStringLiteral("b")},
                        {QStringLiteral("selected"), false}},
            QVariantMap{{QStringLiteral("index"), 999},
                        {QStringLiteral("entryId"), QStringLiteral("c")},
                        {QStringLiteral("selected"), true}},
        };
        QVERIFY(!session->applyExternalStateDelta(
            QStringLiteral("c"), 2, invalidDelta, 21, 22));
        QCOMPARE(session->selectionRevision(), qulonglong(21));
        QVERIFY(session->isSelectedAt(0));
        QVERIFY(!session->isSelectedAt(1));

        QSignalSpy viewportCursorChanges(
            session,
            &ZoinGallery::GallerySession::panelViewportCursorEntryIdChanged);
        session->setPanelViewportCursorEntryId(session->cursorEntryId());
        QCOMPARE(session->panelViewportCursorEntryId(), QStringLiteral("c"));
        QCOMPARE(viewportCursorChanges.size(), 1);

        const QVariantList reordered{
            first.at(2), first.at(0), first.at(1),
        };
        QVERIFY(session->applyExternalCatalog(reordered, 11));
        // A catalog refresh/reorder in the same directory must not jump a
        // panel whose Loader was recreated or temporarily switched to list.
        QCOMPARE(session->panelScrollOffset(), qreal(91.5));
        QCOMPARE(session->panelViewportCursorEntryId(), QStringLiteral("c"));
        QCOMPARE(session->entryIdAt(0), QStringLiteral("c"));
        QCOMPARE(session->entryIdAt(1), QStringLiteral("b"));
        QCOMPARE(session->cursorEntryId(), QStringLiteral("c"));
        QVERIFY(!session->applyExternalCatalog(first, 9));
        QCOMPARE(session->entryIdAt(0), QStringLiteral("c"));

        QVERIFY(session->applyExternalCatalog(
            reordered, 12,
            {{QStringLiteral("currentPath"),
              QStringLiteral("/virtual/gallery-session-test/other")},
             {QStringLiteral("catalogProvisional"), true}}));
        QVERIFY(!session->catalogReady());
        QCOMPARE(session->panelScrollOffset(), qreal(0));
        QVERIFY(session->panelViewportCursorEntryId().isEmpty());
        QVERIFY(!session->panelViewportStateAvailable());

        session->setPanelScrollOffset(73);
        session->setPanelViewportCursorEntryId(QStringLiteral("c"));
        QVERIFY(session->panelViewportStateAvailable());
        QVERIFY(session->applyExternalCatalog(
            reordered, 13,
            {{QStringLiteral("currentPath"),
              QStringLiteral("/virtual/gallery-session-test/other")}}));
        QVERIFY(session->catalogReady());

        QVERIFY(session->applyExternalCatalog(
            reordered, 14,
            {{QStringLiteral("currentPath"),
              QStringLiteral("/virtual/gallery-session-test")}}));
        QCOMPARE(session->panelScrollOffset(), qreal(91.5));
        QCOMPARE(session->panelViewportCursorEntryId(), QStringLiteral("c"));
        QVERIFY(session->panelViewportStateAvailable());

        QVERIFY(session->applyExternalCatalog(
            reordered, 15,
            {{QStringLiteral("currentPath"),
              QStringLiteral("/virtual/gallery-session-test/other")}}));
        QCOMPARE(session->panelScrollOffset(), qreal(73));
        QCOMPARE(session->panelViewportCursorEntryId(), QStringLiteral("c"));
        QVERIFY(session->panelViewportStateAvailable());

        session->resetExternalSource();
        QCOMPARE(session->panelScrollOffset(), qreal(0));
        QVERIFY(session->panelViewportCursorEntryId().isEmpty());
        QCOMPARE(session->model()->rowCount(), 0);
        QCOMPARE(session->catalogRevision(), qulonglong(0));
        QCOMPARE(session->selectionRevision(), qulonglong(0));
        QVERIFY(session->applyExternalCatalog(first, 1));
        QCOMPARE(session->entryIdAt(0), QStringLiteral("b"));

        runtime->shutdown();
        QVERIFY(session->shutdownComplete());
        QVERIFY(!QCoreApplication::closingDown());
    }

    void batchesExternalAppearanceChanges() {
        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("appearance-batch"));
        QVERIFY(session);

        constexpr int entryCount = 128;
        const QVariantMap initialStyle{
            {QStringLiteral("icon"),
             QStringLiteral("qrc:/custom/initial.svg")},
        };
        QVariantList catalog;
        catalog.reserve(entryCount);
        for (int row = 0; row < entryCount; ++row) {
            QVariantMap value = entry(
                QStringLiteral("appearance-%1").arg(row), row,
                QStringLiteral("appearance-%1.txt").arg(row));
            value.insert(QStringLiteral("highlightStyle"), initialStyle);
            catalog.append(value);
        }

        QSignalSpy resetSpy(session->model(),
                            &QAbstractItemModel::modelReset);
        QSignalSpy changedSpy(session->model(),
                              &QAbstractItemModel::dataChanged);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QCOMPARE(resetSpy.size(), 1);
        QCOMPARE(changedSpy.size(), 0);
        QCOMPARE(session->highlightStyleAt(64), initialStyle);

        const QVariantMap updatedStyle{
            {QStringLiteral("icon"),
             QStringLiteral("qrc:/custom/updated.svg")},
            {QStringLiteral("normal"), QVariantMap{
                 {QStringLiteral("foreground"),
                  QStringLiteral("#abcdef")},
             }},
        };
        QVariantList appearance;
        appearance.reserve(entryCount);
        for (int row = 0; row < entryCount; ++row) {
            appearance.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("appearance-%1").arg(row)},
                {QStringLiteral("highlightStyle"), updatedStyle},
            });
        }

        QVERIFY(session->applyExternalAppearance(appearance, 2));
        QCOMPARE(changedSpy.size(), 1);
        const QList<QVariant> arguments = changedSpy.constFirst();
        QCOMPARE(arguments.at(0).value<QModelIndex>().row(), 0);
        QCOMPARE(arguments.at(1).value<QModelIndex>().row(),
                 entryCount - 1);
        const QList<int> roles = arguments.at(2).value<QList<int>>();
        QVERIFY(roles.contains(FileListModel::ImageFileRole));

        // Replaying an equivalent semantic snapshot is a no-op for both the
        // item properties and the model observers.
        QVERIFY(session->applyExternalAppearance(appearance, 2));
        QCOMPARE(changedSpy.size(), 1);

        runtime->shutdown();
    }

    void appliesRevisionedMetadataChunksWithoutResettingCatalog() {
        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("metadata-chunks"));
        QVERIFY(session);

        QVariantMap first{
            {QStringLiteral("entryId"), QStringLiteral("first")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("first.png")},
            {QStringLiteral("path"), QStringLiteral("/virtual/first.png")},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("isHidden"), true},
            {QStringLiteral("displayBaseName"), QStringLiteral("first")},
            {QStringLiteral("displayExtension"), QStringLiteral("png")},
        };
        QVariantMap second{
            {QStringLiteral("entryId"), QStringLiteral("second")},
            {QStringLiteral("index"), 1},
            {QStringLiteral("name"), QStringLiteral("second.txt")},
            {QStringLiteral("path"), QStringLiteral("/virtual/second.txt")},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), false},
            {QStringLiteral("displayBaseName"), QStringLiteral("second")},
            {QStringLiteral("displayExtension"), QStringLiteral("txt")},
        };
        const QVariantMap options{
            {QStringLiteral("currentPath"), QStringLiteral("/virtual")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("metadataRevision"), qulonglong(77)},
        };
        QVERIFY(session->applyExternalCatalog({first, second}, 12, options));

        const auto roles = session->model()->roleNames();
        const int itemRole = roles.key(QByteArrayLiteral("imageFileRole"), -1);
        const int sizeRole = roles.key(QByteArrayLiteral("fileSizeRole"), -1);
        QVERIFY(itemRole >= 0);
        QVERIFY(sizeRole >= 0);
        ImageFile *firstItem = qvariant_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0), itemRole));
        QVERIFY(firstItem);
        QVERIFY(firstItem->displayFields().value(
                    QStringLiteral("isHidden")).toBool());
        QCOMPARE(session->model()->data(session->model()->index(0, 0), sizeRole)
                     .toLongLong(),
                 qint64(-1));
        QVERIFY(!firstItem->lastModified().isValid());

        QSignalSpy resetSpy(session->model(),
                            &QAbstractItemModel::modelReset);
        QSignalSpy changedSpy(session->model(),
                              &QAbstractItemModel::dataChanged);
        const QVariantMap style{
            {QStringLiteral("icon"), QStringLiteral("qrc:/custom/png.svg")},
        };
        const QVariantMap firstMetadata{
            {QStringLiteral("entryId"), QStringLiteral("first")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("size"), qint64(8192)},
            {QStringLiteral("mtimeNanos"), qint64(1'700'000'000'123'000'000LL)},
            {QStringLiteral("sizeText"), QStringLiteral("8 KB")},
            {QStringLiteral("mtimeText"), QStringLiteral("now")},
            {QStringLiteral("modeText"), QStringLiteral("rw-r--r--")},
            {QStringLiteral("highlightStyle"), style},
        };

        // Wrong catalog/revision and a malformed chunk are atomic no-ops.
        QVERIFY(!session->applyExternalMetadata(
            {firstMetadata}, 11, 77));
        QVariantMap missing = firstMetadata;
        missing.insert(QStringLiteral("entryId"), QStringLiteral("missing"));
        QVERIFY(!session->applyExternalMetadata(
            {firstMetadata, missing}, 12, 77));
        QCOMPARE(changedSpy.size(), 0);

        QVERIFY(session->applyExternalMetadata(
            {firstMetadata}, 12, 77, false));
        QCOMPARE(resetSpy.size(), 0);
        QCOMPARE(qvariant_cast<ImageFile *>(session->model()->data(
                     session->model()->index(0, 0), itemRole)),
                 firstItem);
        QCOMPARE(session->model()->data(session->model()->index(0, 0), sizeRole)
                     .toLongLong(),
                 qint64(8192));
        QCOMPARE(firstItem->displayFields().value(
                     QStringLiteral("sizeText")).toString(),
                 QStringLiteral("8 KB"));
        QVERIFY(firstItem->displayFields().value(
                    QStringLiteral("isHidden")).toBool());
        QCOMPARE(firstItem->highlightStyle(), style);
        QCOMPARE(firstItem->iconPath(), QStringLiteral("qrc:/custom/png.svg"));
        QVERIFY(firstItem->lastModified().isValid());
        QCOMPARE(changedSpy.size(), 1);
        const QList<int> metadataRoles =
            changedSpy.constFirst().at(2).value<QList<int>>();
        QVERIFY(metadataRoles.contains(FileListModel::FileSizeRole));
        QVERIFY(metadataRoles.contains(
            ZoinGallery::ExternalCatalogModel::VisualSnapshotRole));

        const QVariantMap secondMetadata{
            {QStringLiteral("entryId"), QStringLiteral("second")},
            {QStringLiteral("index"), 1},
            {QStringLiteral("size"), qint64(17)},
            {QStringLiteral("mtimeNanos"), qint64(42'000'000)},
            {QStringLiteral("sizeText"), QStringLiteral("17 B")},
        };
        QVERIFY(session->applyExternalMetadata(
            {secondMetadata}, 12, 77, true));
        QCOMPARE(resetSpy.size(), 0);
        QVERIFY(!session->applyExternalMetadata(
            {firstMetadata}, 12, 77, false));
        QVERIFY(session->applyExternalCatalog({first, second}, 12, options));
        QVERIFY(!session->applyExternalMetadata(
            {firstMetadata}, 12, 77, false));

        // Replacing the base catalog invalidates every outstanding chunk.
        QVERIFY(session->applyExternalCatalog(
            {first, second}, 13,
            {{QStringLiteral("currentPath"), QStringLiteral("/virtual")},
             {QStringLiteral("metadataDeferred"), true},
             {QStringLiteral("metadataRevision"), qulonglong(78)}}));
        QVERIFY(!session->applyExternalMetadata(
            {firstMetadata}, 12, 77));

        runtime->shutdown();
    }

    void deferredMetadataRequeuesVisiblePreviewForPublishedVersion() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("deferred-preview.png"));
        QImage source(41, 29, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::cyan);
        QVERIFY(source.save(path));
        const QFileInfo file(path);

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions runtimeOptions;
        runtimeOptions.maxDecodeThreads = 1;
        runtimeOptions.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, runtimeOptions);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("deferred-preview-version"));
        QVERIFY(session);
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);

        const QVariantMap base{
            {QStringLiteral("entryId"), QStringLiteral("preview")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("path"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
        };
        const QVariantMap options{
            {QStringLiteral("currentPath"), directory.path()},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("metadataRevision"), qulonglong(91)},
        };
        QVERIFY(session->applyExternalCatalog({base}, 23, options));

        // Seed the exact race exercised by the phased protocol: the first
        // painted catalog asks for image geometry while its version is still
        // provisional, then the authoritative metadata arrives.
        model->requestImageMetadata({0}, true);
        for (int attempt = 0;
             attempt < 20 && model->metadataSubmittedBatchCount() == 0;
             ++attempt) {
            QCoreApplication::processEvents();
            QTest::qWait(1);
        }
        QVERIFY(model->metadataSubmittedBatchCount() > 0);

        QSignalSpy changedSpy(model, &QAbstractItemModel::dataChanged);
        const qint64 version =
            file.lastModified().toMSecsSinceEpoch() * 1'000'000;
        const QVariantMap metadata{
            {QStringLiteral("entryId"), QStringLiteral("preview")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("size"), file.size()},
            {QStringLiteral("mtimeNanos"), version},
            {QStringLiteral("isImage"), true},
        };
        QVERIFY(session->applyExternalMetadata(
            {metadata}, 23, 91, true));

        bool invalidationRolesSeen = false;
        for (const QList<QVariant> &arguments : changedSpy) {
            const QList<int> roles = arguments.at(2).value<QList<int>>();
            if (roles.contains(FileListModel::ImageIdUrlRole)
                && roles.contains(FileListModel::ImageFullSizeRole)) {
                invalidationRolesSeen = true;
                break;
            }
        }
        QVERIFY(invalidationRolesSeen);
        QTRY_COMPARE_WITH_TIMEOUT(
            model->imageOriginalSizeAt(0), source.size(), 5000);

        runtime->shutdown();
    }

    void preservesPreviousIdentityAcrossCatalogDiffsAndViewerReloads() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("previous-state"));
        QVERIFY(session);

        const QVariantMap a = entry(QStringLiteral("a"), 11,
                                    QStringLiteral("a.png"), true);
        const QVariantMap b = entry(QStringLiteral("b"), 22,
                                    QStringLiteral("b.png"), true);
        const QVariantMap c = entry(QStringLiteral("c"), 33,
                                    QStringLiteral("c.png"), true);
        QVERIFY(session->applyExternalCatalog(
            {a, b, c}, 1,
            {{QStringLiteral("currentPath"), QStringLiteral("/one")}}));
        QVERIFY(session->applyExternalState(QStringLiteral("b"), 22, {}, 1));
        QCOMPARE(session->indexForEntryId(QStringLiteral("a")), 0);
        QCOMPARE(session->indexForEntryId(QStringLiteral("b")), 1);
        QCOMPARE(session->indexForEntryId(QStringLiteral("missing")), -1);

        QSignalSpy stateChanges(
            session,
            &ZoinGallery::GallerySession::viewerPreviousStateChanged);
        const QVariantMap pendingViewport{
            {QStringLiteral("targetEntryId"), QStringLiteral("a")},
            {QStringLiteral("centerRatioX"), 0.35},
            {QStringLiteral("centerRatioY"), 0.65},
            {QStringLiteral("zoomToFitRatio"), 2.0},
        };
        session->setViewerPreviousState(QStringLiteral("a"), QString(),
                                        false, pendingViewport);
        QCOMPARE(stateChanges.size(), 1);
        session->setViewerPreviousState(QStringLiteral("a"), QString(),
                                        false, pendingViewport);
        QCOMPARE(stateChanges.size(), 1);

        QVERIFY(session->applyExternalCatalog({c, b, a}, 2));
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("a"));
        QCOMPARE(session->indexForEntryId(QStringLiteral("a")), 2);
        QCOMPARE(session->viewerPreviousViewport(), pendingViewport);

        // An ordinary previous identity is forgotten when its file vanishes.
        QVERIFY(session->applyExternalCatalog({c, b}, 3));
        QVERIFY(session->viewerPreviousEntryId().isEmpty());
        QVERIFY(session->viewerPreviousViewport().isEmpty());

        // A lock is semantic rather than positional: keep the identity while
        // absent, discard only an impossible in-flight viewport, and revive
        // the same identity if a later snapshot brings it back.
        session->setViewerPreviousState(QStringLiteral("b"),
                                        QStringLiteral("c"), true,
                                        {{QStringLiteral("targetEntryId"),
                                          QStringLiteral("b")}});
        QVERIFY(session->applyExternalCatalog({c}, 4));
        QVERIFY(session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("b"));
        QCOMPARE(session->viewerPreviousReturnEntryId(),
                 QStringLiteral("c"));
        QVERIFY(session->viewerPreviousViewport().isEmpty());
        QCOMPARE(session->indexForEntryId(QStringLiteral("b")), -1);

        session->setViewerOpen(true);
        session->setViewerOpen(false);
        QVERIFY(session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("b"));
        QVERIFY(session->applyExternalCatalog({b, c}, 5));
        QCOMPARE(session->indexForEntryId(QStringLiteral("b")), 0);

        // f4's GallerySession outlives its transient Loader. Destroying and
        // recreating the actual windowless viewer must not discard a lock.
        engine.rootContext()->setContextProperty(
            QStringLiteral("previousStateSession"), session);
        QQmlComponent viewerComponent(&engine);
        viewerComponent.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryViewer {
                width: 320
                height: 240
                session: previousStateSession
                animationDuration: 1
            }
        )QML", QUrl(QStringLiteral("inline:PreviousStateViewer.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(
            viewerComponent.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(viewerComponent.isReady(),
                 qPrintable(viewerComponent.errorString()));
        {
            QScopedPointer<QObject> firstViewer(viewerComponent.create());
            QVERIFY2(firstViewer, qPrintable(viewerComponent.errorString()));
        }
        QVERIFY(session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("b"));
        {
            QScopedPointer<QObject> recreatedViewer(viewerComponent.create());
            QVERIFY2(recreatedViewer,
                     qPrintable(viewerComponent.errorString()));
            QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("b"));
        }

        session->setViewerPreviousState(QStringLiteral("c"), QString(),
                                        false, QVariantMap());
        session->setViewerOpen(true);
        session->setViewerOpen(false);
        QVERIFY(session->viewerPreviousEntryId().isEmpty());
        QVERIFY(!session->viewerPreviousLocked());

        session->setViewerPreviousState(QStringLiteral("b"),
                                        QStringLiteral("c"), true,
                                        QVariantMap());
        QVERIFY(session->applyExternalCatalog(
            {b, c}, 6,
            {{QStringLiteral("currentPath"), QStringLiteral("/two")}}));
        QVERIFY(session->viewerPreviousEntryId().isEmpty());
        QVERIFY(!session->viewerPreviousLocked());

        runtime->shutdown();
    }

    void equivalentCatalogRevisionAdvancesWithoutModelReset() {
        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("equivalent-catalog"));
        QVERIFY(session);

        const QVariantList catalog{
            entry(QStringLiteral("first"), 4, QStringLiteral("first.png"),
                  true, false, 1234567891),
            entry(QStringLiteral("second"), 9, QStringLiteral("second.txt"),
                  false, false, 1234567892),
        };
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        QAbstractItemModel *model = session->model();
        QVERIFY(model);
        const int imageFileRole = model->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        QObject *firstItem = model->data(model->index(0, 0), imageFileRole)
                                 .value<QObject *>();
        QObject *secondItem = model->data(model->index(1, 0), imageFileRole)
                                  .value<QObject *>();
        QVERIFY(firstItem);
        QVERIFY(secondItem);

        QSignalSpy resetSpy(model, &QAbstractItemModel::modelReset);
        QSignalSpy revisionSpy(
            session, &ZoinGallery::GallerySession::catalogRevisionChanged);

        QVariantList equivalent = catalog;
        QVariantMap first = equivalent.at(0).toMap();
        // f4 can advance its authoritative revision for semantic fields that
        // are intentionally absent from Gallery's effective catalog.
        first.insert(QStringLiteral("isCached"), true);
        equivalent[0] = first;
        QVERIFY(session->applyExternalCatalog(equivalent, 2));

        QCOMPARE(session->catalogRevision(), qulonglong(2));
        QCOMPARE(revisionSpy.size(), 1);
        QCOMPARE(resetSpy.size(), 0);
        QCOMPARE(model->data(model->index(0, 0), imageFileRole)
                     .value<QObject *>(),
                 firstItem);
        QCOMPARE(model->data(model->index(1, 0), imageFileRole)
                     .value<QObject *>(),
                 secondItem);

        runtime->shutdown();
    }

    void batchesMetadataOnceAcrossEquivalentRevision() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QVariantList catalog;
        constexpr int imageCount = 5;
        for (int row = 0; row < imageCount; ++row) {
            const QString name = QStringLiteral("metadata-%1.png").arg(row);
            const QString path = directory.filePath(name);
            QImage image(32 + row, 24 + row,
                         QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromHsv(row * 47, 180, 230));
            QVERIFY2(image.save(path), qPrintable(path));
            const QFileInfo file(path);
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("metadata-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"), name},
                {QStringLiteral("localPath"), path},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("mtimeNs"),
                 file.lastModified().toMSecsSinceEpoch() * 1'000'000 + row},
                {QStringLiteral("size"), file.size()},
            });
        }

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("metadata-batch"));
        QVERIFY(session);
        auto *externalModel =
            qobject_cast<ZoinGallery::ExternalCatalogModel *>(session->model());
        QVERIFY(externalModel);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);

        int metadataReadyCount = 0;
        connect(decodeManager, &DecodeManager::imageInfoReady, this,
                [&](const ImageInfo &info) {
                    if (info.requestNamespace == session->sessionId()) {
                        ++metadataReadyCount;
                    }
                });

        int fullSizeChangeCount = 0;
        int flushChangeCount = 0;
        connect(session->model(), &QAbstractItemModel::dataChanged, this,
                [&](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                    const QList<int> &roles) {
                    if (roles.contains(FileListModel::ImageFullSizeRole)) {
                        fullSizeChangeCount +=
                            bottomRight.row() - topLeft.row() + 1;
                    }
                    if (roles.contains(FileListModel::TimeToFlushRole)) {
                        ++flushChangeCount;
                    }
                });
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        session->ensurePreviews();
        QCoreApplication::processEvents();
        QCOMPARE(metadataReadyCount, 0);
        externalModel->requestImageMetadata({}, false, true);
        QSignalSpy resetSpy(session->model(), &QAbstractItemModel::modelReset);

        QVariantList equivalent = catalog;
        QVariantMap last = equivalent.constLast().toMap();
        last.insert(QStringLiteral("isCached"), true);
        equivalent.last() = last;
        QVERIFY(session->applyExternalCatalog(equivalent, 2));
        QCOMPARE(resetSpy.size(), 0);

        QTRY_COMPARE_WITH_TIMEOUT(metadataReadyCount, imageCount, 10000);
        QCOMPARE(fullSizeChangeCount, imageCount);
        QCOMPARE(flushChangeCount, 1);
        for (int row = 0; row < imageCount; ++row) {
            QVERIFY(externalModel->imageOriginalSizeAt(row).isValid());
        }

        runtime->shutdown();
    }

    void coalescesVisibleAndCatalogMetadataIntoOneSubmission() {
        constexpr int imageCount = 128;
        constexpr int visibleCount = 16;
        QVariantList catalog;
        catalog.reserve(imageCount);
        for (int row = 0; row < imageCount; ++row) {
            catalog.append(entry(
                QStringLiteral("coalesced-metadata-%1").arg(row), row,
                QStringLiteral("coalesced-%1.png").arg(row), true, false,
                1'700'000'000'000'000'000LL + row));
        }

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 4;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("coalesced-metadata-submission"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);

        QList<int> visibleRows;
        visibleRows.reserve(visibleCount);
        for (int row = 0; row < visibleCount; ++row) {
            visibleRows.append(row);
        }
        model->requestImageMetadata(visibleRows, true, false);
        model->requestImageMetadata({}, false, true);

        QTRY_COMPARE_WITH_TIMEOUT(
            model->metadataSubmittedBatchCount(), quint64(1), 1000);
        QCOMPARE(model->metadataPeakPendingRequestCount(),
                 qsizetype(imageCount));
        QVERIFY(model->metadataPeakPendingRequestCount() <=
                model->metadataRequestLimit());

        runtime->shutdown();
    }

    void appliesCachedMetadataBatchWithOneModelRange() {
        constexpr int imageCount = 128;
        QVariantList catalog;
        catalog.reserve(imageCount);
        for (int row = 0; row < imageCount; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("cached-metadata-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("cached-%1.png").arg(row)},
                {QStringLiteral("localPath"),
                 QStringLiteral("/virtual/cache/cached-%1.png").arg(row)},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("mtimeNs"), qint64(1'700'000'000'000'000'000LL + row)},
                {QStringLiteral("size"), qint64(4096 + row)},
            });
        }

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(
                QStringLiteral("cached-metadata-range"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);

        QList<ImageInfo> infos;
        infos.reserve(imageCount);
        for (int row = 0; row < imageCount; ++row) {
            ImageFile *image = session->model()
                ->data(session->model()->index(row, 0),
                       FileListModel::ImageFileRole)
                .value<ImageFile *>();
            QVERIFY(image);
            ImageInfo info = image->info();
            info.imageSize = QSize(900 + row, 700 + row);
            info.isCached = true;
            info.isLast = row == imageCount - 1;
            info.requestNamespace = session->sessionId();
            infos.append(std::move(info));
        }

        int fullSizeSignals = 0;
        int firstChanged = -1;
        int lastChanged = -1;
        bool flushed = false;
        bool cachedBatch = false;
        connect(session->model(), &QAbstractItemModel::dataChanged, this,
                [&](const QModelIndex &topLeft,
                    const QModelIndex &bottomRight,
                    const QList<int> &roles) {
                    if (!roles.contains(FileListModel::ImageFullSizeRole)) {
                        return;
                    }
                    ++fullSizeSignals;
                    firstChanged = topLeft.row();
                    lastChanged = bottomRight.row();
                    flushed = roles.contains(FileListModel::TimeToFlushRole);
                    cachedBatch = roles.contains(
                        FileListModel::CachedMetadataBatchRole);
                });

        decodeManager->imagesInfoReady(infos);

        QCOMPARE(fullSizeSignals, 1);
        QCOMPARE(firstChanged, 0);
        QCOMPARE(lastChanged, imageCount - 1);
        QVERIFY(flushed);
        QVERIFY(cachedBatch);
        for (int row = 0; row < imageCount; ++row) {
            QCOMPARE(session->imageOriginalSizeAt(row),
                     QSize(900 + row, 700 + row));
        }

        runtime->shutdown();
    }

    void externalThumbnailUsesExactTargetAndDeduplicates() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("large.png"));
        QImage source(1600, 900, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(32, 96, 160));
        QVERIFY(source.save(path));
        const QFileInfo file(path);

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("exact-thumbnail"));
        QVERIFY(session);
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("large")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("large.png")},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"),
             file.lastModified().toMSecsSinceEpoch() * 1'000'000},
            {QStringLiteral("size"), file.size()},
        }}, 1));

        session->ensurePreviews();
        model->requestImageMetadata({0}, true);
        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);
        QTRY_VERIFY_WITH_TIMEOUT(imageFile->fullSize().isValid(), 5000);

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        int readyCount = 0;
        ImageDecodeRequest deliveredRequest;
        QImage deliveredImage;
        connect(decodeManager, &DecodeManager::imageReady, this,
                [&](const ImageDecodeRequest &request, const QImage &image,
                    const DecodedImageInfo &) {
                    if (request.requestNamespace == session->sessionId() &&
                        !request.viewerRequest) {
                        ++readyCount;
                        deliveredRequest = request;
                        deliveredImage = image;
                    }
                });

        const QSize targetSize(123, 77);
        const ImageDecodeRequest request{
            .info = imageFile->info(),
            .targetSize = targetSize,
        };
        model->decodeImages({request, request});
        QTRY_COMPARE_WITH_TIMEOUT(readyCount, 1, 5000);
        QCOMPARE(deliveredRequest.targetSize, targetSize);
        QVERIFY(deliveredRequest.checkCache);
        QVERIFY(!deliveredRequest.expandToCacheResolution);
        QVERIFY(deliveredRequest.storeInPersistentCache);
        QCOMPARE(deliveredImage.size(), targetSize);

        // The published frame already covers the same physical tile, so an
        // identical later layout pass must not enqueue another decode.
        model->decodeImages({request});
        QTest::qWait(100);
        QCOMPARE(readyCount, 1);

        runtime->shutdown();
    }

    void externalHeicPublishesAspectPreservingDecodeForExactRequest() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("portrait.heic"));
        QFile fixture(path);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        const QByteArray fixtureBytes =
            ZoinGalleryTest::portraitHeicFixture();
        QCOMPARE(fixtureBytes.size(), 589);
        QCOMPARE(fixture.write(fixtureBytes), fixtureBytes.size());
        fixture.close();

        const QFileInfo file(path);
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("portable-heic"));
        QVERIFY(session);
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);

        // Positive Go int64 fields arrive through MessagePack as qulonglong.
        const qulonglong version = static_cast<qulonglong>(
            file.lastModified().toMSecsSinceEpoch()) * 1'000'000ULL;
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("portable-heic")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("selected"), false},
            {QStringLiteral("mtimeNs"), QVariant::fromValue(version)},
            {QStringLiteral("size"), QVariant::fromValue<qulonglong>(
                 static_cast<qulonglong>(file.size()))},
        }}, 1));
        QVERIFY(session->isImageAt(0));

        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy failedSpy(decodeManager, &DecodeManager::imageReadFailed);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);

        model->requestImageMetadata({0}, true);
        QTRY_COMPARE_WITH_TIMEOUT(imageFile->fullSize(), QSize(48, 64), 5000);

        // Deliberately use a bounding box rather than the source aspect.
        // HeicDecoder correctly returns 24x32. The exact request owner still
        // has to publish that valid result even though the shared cache must
        // not advertise it as covering a full 32x32 tier for other callers.
        const ImageDecodeRequest request{
            .info = imageFile->info(),
            .targetSize = QSize(32, 32),
            .highPriority = true,
        };
        model->decodeImages({request});
        QTRY_VERIFY_WITH_TIMEOUT(readySpy.size() + failedSpy.size() > 0,
                                 5000);
        QCOMPARE(failedSpy.size(), 0);
        QCOMPARE(readySpy.size(), 1);
        const QImage decoded = readySpy.constFirst().at(1).value<QImage>();
        QCOMPARE(decoded.size(), QSize(24, 32));
        QTRY_VERIFY_WITH_TIMEOUT(!imageFile->imageIdUrl().isEmpty(), 5000);

        runtime->shutdown();
    }

    void heicEmbeddedThumbnailDoesNotRequireJpegPlugin() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("embedded-thumbnail.heic"));
        QFile fixture(path);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        const QByteArray fixtureBytes =
            ZoinGalleryTest::embeddedThumbnailHeicFixture();
        QCOMPARE(fixtureBytes.size(), 768);
        QCOMPARE(fixture.write(fixtureBytes), fixtureBytes.size());
        fixture.close();

        HeicDecoder decoder;
        ImageInfo info{
            .path = path,
            .lastModified = QFileInfo(path).lastModified(),
            .fileSize = QFileInfo(path).size(),
        };
        QVERIFY(decoder.readMetadata(info));
        QCOMPARE(info.imageSize, QSize(128, 96));

        const ImageDecodeRequest request{
            .info = info,
            .targetSize = QSize(24, 18),
            .expandToCacheResolution = false,
            .storeInPersistentCache = false,
        };
        ImageData imageData(request);
        QVERIFY(decoder.readPreviewAndMime(imageData));

        // The extracted package does not deploy Qt's optional qjpeg plugin.
        // The embedded HEIC thumbnail must therefore use a built-in format and
        // must not silently fall back to the full tiled source image.
        QVERIFY(imageData.data.isEmpty());
        QVERIFY(imageData.previewData);
        QVERIFY(imageData.previewDataSize > 0);
        QCOMPARE(imageData.previewMimeType, QStringLiteral("image/png"));
        QCOMPARE(imageData.previewUsed, QStringLiteral("HEIC thumbnail"));
        const QByteArray previewBytes = QByteArray::fromRawData(
            imageData.previewData.get(), imageData.previewDataSize);
        const QImage preview = QImage::fromData(previewBytes, "PNG");
        QCOMPARE(preview.size(), QSize(32, 24));
    }

    void heicViewerFitAndNativeRequestsBypassUndersizedPreview() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("viewer-resolution.heic"));
        QFile fixture(path);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        const QByteArray fixtureBytes =
            ZoinGalleryTest::embeddedThumbnailHeicFixture();
        QCOMPARE(fixture.write(fixtureBytes), fixtureBytes.size());
        fixture.close();

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("heic-viewer"));
        QVERIFY(session);
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);

        const QFileInfo file(path);
        const qulonglong version = static_cast<qulonglong>(
            file.lastModified().toMSecsSinceEpoch()) * 1'000'000ULL;
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("heic")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("selected"), false},
            {QStringLiteral("mtimeNs"), QVariant::fromValue(version)},
            {QStringLiteral("size"), QVariant::fromValue<qulonglong>(
                 static_cast<qulonglong>(file.size()))},
        }}, 1));

        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);
        model->requestImageMetadata({0}, true);
        QTRY_COMPARE_WITH_TIMEOUT(imageFile->fullSize(), QSize(128, 96), 5000);

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);
        QSignalSpy failedSpy(decodeManager, &DecodeManager::imageReadFailed);

        const auto findViewerResult = [&](bool fitRequest,
                                          const QSize &targetSize) {
            for (int resultIndex = 0; resultIndex < readySpy.size();
                 ++resultIndex) {
                const ImageDecodeRequest request =
                    readySpy.at(resultIndex).at(0)
                        .value<ImageDecodeRequest>();
                if (request.viewerRequest &&
                    request.fitToViewerRequest == fitRequest &&
                    request.targetSize == targetSize) {
                    return resultIndex;
                }
            }
            return -1;
        };
        const auto hasViewerSourceLevel = [&](int level) {
            const QVariantList sources = session->viewerSourcesAt(0);
            return std::any_of(
                sources.cbegin(), sources.cend(), [level](const QVariant &value) {
                    return value.toMap().value(QStringLiteral("level")).toInt()
                        == level;
                });
        };

        // The fixture's embedded preview is only 32x24. A larger Fit request
        // must decode the HEIC source instead of upscaling that preview and
        // labelling the resulting 80x60 buffer as a prepared viewer tier.
        session->activateIndex(0);
        session->setViewerOpen(true);
        const QSize fitSize(80, 60);
        session->requestViewer(fitSize.width(), fitSize.height());
        QTRY_VERIFY_WITH_TIMEOUT(findViewerResult(true, fitSize) >= 0, 10000);
        QCOMPARE(failedSpy.size(), 0);
        const int fitResultIndex = findViewerResult(true, fitSize);
        QCOMPARE(readySpy.at(fitResultIndex).at(1).value<QImage>().size(),
                 fitSize);
        QVERIFY(readySpy.at(fitResultIndex).at(2)
                    .value<DecodedImageInfo>().previewUsed.isEmpty());

        QVERIFY(hasViewerSourceLevel(1));

        // 0x0 is the public native/100% sentinel. The resulting request must
        // produce real 128x96 source pixels and publish a level-2 texture.
        readySpy.clear();
        session->requestViewer(0, 0);
        const QSize nativeSize(128, 96);
        QTRY_VERIFY_WITH_TIMEOUT(findViewerResult(false, nativeSize) >= 0,
                                 10000);
        QCOMPARE(failedSpy.size(), 0);
        const int nativeResultIndex = findViewerResult(false, nativeSize);
        QCOMPARE(readySpy.at(nativeResultIndex).at(1).value<QImage>().size(),
                 nativeSize);
        QVERIFY(readySpy.at(nativeResultIndex).at(2)
                    .value<DecodedImageInfo>().previewUsed.isEmpty());

        QTRY_VERIFY_WITH_TIMEOUT(hasViewerSourceLevel(2), 5000);

        runtime->shutdown();
    }

    void localExternalHeicMasonryRegression() {
        const QString path = qEnvironmentVariable(
            "ZOIN_HEIC_REGRESSION_PATH");
        if (path.isEmpty()) {
            QSKIP("Set ZOIN_HEIC_REGRESSION_PATH for the native HEIC proof");
        }
        const QFileInfo file(path);
        if (!file.isFile()) {
            QSKIP("ZOIN_HEIC_REGRESSION_PATH is not a file");
        }

        HeicDecoder heicDecoder;
        ImageInfo nativeInfo{
            .path = path,
            .lastModified = file.lastModified(),
            .fileSize = file.size(),
        };
        QVERIFY(heicDecoder.readMetadata(nativeInfo));
        QVERIFY(nativeInfo.imageSize.isValid());

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("external-heic"));
        QVERIFY(session);
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);

        // The MessagePack bridge represents positive Go int64 values as a
        // QVariant qulonglong. Exercise that exact transport type here.
        const qulonglong version = static_cast<qulonglong>(
            file.lastModified().toMSecsSinceEpoch()) * 1'000'000ULL;
        QVERIFY(session->applyExternalCatalog({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("heic")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("selected"), false},
            {QStringLiteral("mtimeNs"), QVariant::fromValue(version)},
            {QStringLiteral("size"), QVariant::fromValue<qulonglong>(
                 static_cast<qulonglong>(file.size()))},
        }}, 1));
        QVERIFY(session->isImageAt(0));

        const int imageFileRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFileRole"), -1);
        QVERIFY(imageFileRole >= 0);
        auto *imageFile = qobject_cast<ImageFile *>(
            session->model()->data(session->model()->index(0, 0),
                                   imageFileRole).value<QObject *>());
        QVERIFY(imageFile);

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QSignalSpy failedSpy(decodeManager, &DecodeManager::imageReadFailed);
        QSignalSpy readySpy(decodeManager, &DecodeManager::imageReady);
        model->requestImageMetadata({0}, true);
        QTRY_COMPARE_WITH_TIMEOUT(imageFile->fullSize(), nativeInfo.imageSize,
                                  5000);

        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        engine.rootContext()->setContextProperty(
            QStringLiteral("externalHeicSession"), session);
        QQmlComponent panel(&engine);
        panel.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 640
                height: 480
                session: externalHeicSession
                presentationMode: "masonry"
                autoFocus: false
                devicePixelRatio: 2
            }
        )QML", QUrl(QStringLiteral("inline:ExternalHeicThumbnailTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(
            panel.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(panel.isReady(), qPrintable(panel.errorString()));
        QScopedPointer<QObject> panelObject(panel.create());
        QVERIFY2(panelObject, qPrintable(panel.errorString()));

        QTRY_VERIFY_WITH_TIMEOUT(readySpy.size() + failedSpy.size() > 0,
                                 10000);
        QCOMPARE(failedSpy.size(), 0);
        QVERIFY(!readySpy.isEmpty());
        const ImageDecodeRequest deliveredRequest =
            readySpy.constLast().at(0).value<ImageDecodeRequest>();
        const QImage decoded = readySpy.constLast().at(1).value<QImage>();
        QVERIFY(!decoded.isNull());
        QCOMPARE(decoded.size(), deliveredRequest.targetSize);
        const DecodedImageInfo decodedInfo =
            readySpy.constLast().at(2).value<DecodedImageInfo>();
        QVERIFY2(decodedInfo.previewUsed.contains(
                     QStringLiteral("HEIC thumbnail")),
                 qPrintable(decodedInfo.previewUsed));
        QTRY_VERIFY_WITH_TIMEOUT(!imageFile->imageIdUrl().isEmpty(), 10000);

        runtime->shutdown();
    }

    void explicitlyRegistersBuiltInDecoders() {
        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("formats"));
        QVERIFY(session);

        QVariantMap inferredPng = entry(
            QStringLiteral("png"), 0, QStringLiteral("thumbnail.png"));
        inferredPng.remove(QStringLiteral("isImage"));
        QVERIFY(session->applyExternalCatalog({inferredPng}, 1));
        QVERIFY(session->isImageAt(0));

        QVariantMap inferredText = entry(
            QStringLiteral("text"), 1, QStringLiteral("notes.txt"));
        inferredText.remove(QStringLiteral("isImage"));
        QVERIFY(session->applyExternalCatalog({inferredText}, 2));
        QVERIFY(!session->isImageAt(0));

        runtime->shutdown();
    }

    void decodesThumbnailWithSessionAndVersionNamespacing() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("pixel.png"));
        QImage source(32, 24, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(QStringLiteral("#4c8bf5")));
        QVERIFY(source.save(path));

        const QFileInfo file(path);
        const qint64 version =
            file.lastModified().toMSecsSinceEpoch() * 1000000;
        const qint64 size = file.size();
        QVariantMap imageEntry{
            {QStringLiteral("entryId"), QStringLiteral("pixel")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("mtimeNs"), version},
            {QStringLiteral("size"), size},
        };

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("versioned"));
        QVERIFY(session->applyExternalCatalog({imageEntry}, 1));
        engine.rootContext()->setContextProperty(
            QStringLiteral("versionedGallerySession"), session);

        QQmlComponent panel(&engine);
        panel.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 640
                height: 480
                session: versionedGallerySession
            }
        )QML", QUrl(QStringLiteral("inline:VersionedThumbnailTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(
            panel.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(panel.isReady(), qPrintable(panel.errorString()));
        QScopedPointer<QObject> panelObject(panel.create());
        QVERIFY2(panelObject, qPrintable(panel.errorString()));

        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        QVERIFY(imageUrlRole >= 0);
        const QModelIndex modelIndex = session->model()->index(0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            !session->model()->data(modelIndex, imageUrlRole)
                 .toString().isEmpty(),
            5000);
        const QString imageUrl =
            session->model()->data(modelIndex, imageUrlRole).toString();
        QVERIFY(imageUrl.contains(QStringLiteral("versioned-")));
        QVERIFY(imageUrl.contains(QStringLiteral("-v%1-s%2-")
                                      .arg(version)
                                      .arg(size)));

        session->activateIndex(0);
        session->setViewerOpen(true);
        session->requestViewer(640, 480);
        QTRY_COMPARE_WITH_TIMEOUT(session->viewerSourceLevel(), 1, 5000);
        const QVariantList fitSources = session->viewerSourcesAt(0);
        QVERIFY(fitSources.size() >= 2);
        QCOMPARE(fitSources.constFirst().toMap()
                     .value(QStringLiteral("level")).toInt(), 0);
        QCOMPARE(fitSources.constLast().toMap()
                     .value(QStringLiteral("level")).toInt(), 1);
        QVERIFY(session->viewerSource().toString().startsWith(
            QStringLiteral("image://zoingallery-thumbnails/")));
        const QString viewerUrl = session->viewerSource().toString();
        QVERIFY(viewerUrl.contains(QStringLiteral("versioned-")));
        QVERIFY(viewerUrl.contains(QStringLiteral("-view-v%1-s%2-")
                                       .arg(version)
                                       .arg(size)));

        // A native request republishes the same prepared frame as level 2,
        // while retaining the thumbnail as the level-0 base. Returning to a
        // fit request presents that cached native frame synchronously as
        // level 1 again; neither switch requires another decode.
        session->requestViewer(0, 0);
        QTRY_COMPARE_WITH_TIMEOUT(session->viewerSourceLevel(), 2, 5000);
        const QVariantList nativeSources = session->viewerSourcesAt(0);
        QVERIFY(nativeSources.size() >= 2);
        QCOMPARE(nativeSources.constFirst().toMap()
                     .value(QStringLiteral("level")).toInt(), 0);
        QCOMPARE(nativeSources.constLast().toMap()
                     .value(QStringLiteral("level")).toInt(), 2);
        QVERIFY(session->viewerSource().toString().startsWith(
            QStringLiteral("image://zoingallery-async/")));
        session->requestViewer(640, 480);
        QTRY_COMPARE_WITH_TIMEOUT(session->viewerSourceLevel(), 1, 5000);
        QCOMPARE(session->viewerSource(), QUrl(viewerUrl));

        session->setViewerOpen(false);
        QCOMPARE(session->viewerSource().toString(), imageUrl);
        QVERIFY(!session->viewerSource().toString().startsWith(
            QStringLiteral("image://zoingallery-async/")));

        imageEntry.insert(QStringLiteral("mtimeNs"), version + 1);
        QVERIFY(session->applyExternalCatalog({imageEntry}, 2));
        QVERIFY(session->model()->data(session->model()->index(0, 0),
                                       imageUrlRole).toString().isEmpty());
        QVERIFY(session->viewerSource().isEmpty());

        runtime->shutdown();
    }

    void decodesThumbnailWhenCatalogArrivesAfterPanel() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("late.png"));
        QImage source(96, 72, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(QStringLiteral("#2f9e44")));
        QVERIFY(source.save(path));

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("late-catalog"));
        engine.rootContext()->setContextProperty(
            QStringLiteral("lateGallerySession"), session);

        QQmlComponent panel(&engine);
        panel.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 640
                height: 480
                session: lateGallerySession
            }
        )QML", QUrl(QStringLiteral("inline:LateCatalogPanelTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(
            panel.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(panel.isReady(), qPrintable(panel.errorString()));
        QScopedPointer<QObject> panelObject(panel.create());
        QVERIFY2(panelObject, qPrintable(panel.errorString()));
        QCoreApplication::processEvents();

        const QFileInfo file(path);
        const QVariantMap imageEntry{
            {QStringLiteral("entryId"), QStringLiteral("late")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"),
             file.lastModified().toMSecsSinceEpoch() * 1000000},
            {QStringLiteral("size"), file.size()},
        };
        QVERIFY(session->applyExternalCatalog({imageEntry}, 1));

        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        QVERIFY(imageUrlRole >= 0);
        const QModelIndex modelIndex = session->model()->index(0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            !session->model()->data(modelIndex, imageUrlRole)
                 .toString().isEmpty(),
            2000);
        const QUrl publishedSource = session->model()
            ->data(modelIndex, imageUrlRole).toUrl();
        const auto panelUsesPublishedSource = [&] {
            for (QObject *object : panelObject->findChildren<QObject *>()) {
                const QVariant sourceValue = object->property("source");
                if (sourceValue.isValid() &&
                    sourceValue.toUrl() == publishedSource) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(panelUsesPublishedSource(), 2000);

        runtime->shutdown();
    }

    void preservesOpenViewerAcrossCatalogReorderAndRefreshesVersion() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("viewer.png"));
        QImage source(2048, 1536, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(QStringLiteral("#2f9e44")));
        QVERIFY(source.save(path));

        const QFileInfo file(path);
        const qint64 version =
            file.lastModified().toMSecsSinceEpoch() * 1000000;
        const qint64 size = file.size();
        const QVariantMap textEntry{
            {QStringLiteral("entryId"), QStringLiteral("text")},
            {QStringLiteral("index"), 7},
            {QStringLiteral("name"), QStringLiteral("notes.txt")},
            {QStringLiteral("localPath"),
             directory.filePath(QStringLiteral("notes.txt"))},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), false},
            {QStringLiteral("mtimeNs"), qint64(1)},
            {QStringLiteral("size"), qint64(0)},
        };
        QVariantMap imageEntry{
            {QStringLiteral("entryId"), QStringLiteral("image")},
            {QStringLiteral("index"), 11},
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"), version},
            {QStringLiteral("size"), size},
        };

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("viewer-refresh"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog({textEntry, imageEntry}, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("image"), 1, {}, 1));
        QCOMPARE(session->currentIndex(), 1);

        session->setViewerOpen(true);
        session->requestViewer(320, 240);
        QTRY_VERIFY_WITH_TIMEOUT(
            session->viewerSource().toString().startsWith(
                QStringLiteral("image://zoingallery-thumbnails/")),
            5000);
        const QString firstViewerUrl = session->viewerSource().toString();
        QSignalSpy sourceChanges(
            session, &ZoinGallery::GallerySession::viewerSourceChanged);

        // An identical request must reuse the published viewer frame instead
        // of scheduling another decode with a new provider ID.
        session->requestViewer(320, 240);
        QTest::qWait(300);
        QCOMPARE(session->viewerSource().toString(), firstViewerUrl);
        QCOMPARE(sourceChanges.size(), 0);

        // Moving the same stable entry to a different row must not blank or
        // replace the open viewer frame.
        QVERIFY(session->applyExternalCatalog({imageEntry, textEntry}, 2));
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(session->cursorEntryId(), QStringLiteral("image"));
        QVERIFY(session->viewerOpen());
        QCOMPARE(session->viewerSource().toString(), firstViewerUrl);

        // The QML viewer keeps its already presented texture while the larger
        // decode is pending, but the model must not advertise that undersized
        // frame as if it satisfied the new viewport. Refreshing the catalog
        // must immediately resume the retained target without another QML
        // request.
        session->requestViewer(1280, 960);
        QVERIFY(session->viewerSource().toString() != firstViewerUrl);
        QVERIFY(session->applyExternalCatalog({imageEntry, textEntry}, 3));
        QVERIFY(session->viewerSource().toString() != firstViewerUrl);
        QTRY_VERIFY_WITH_TIMEOUT(
            session->viewerSource().toString().startsWith(
                QStringLiteral("image://zoingallery-thumbnails/")) &&
                session->viewerSource().toString() != firstViewerUrl,
            5000);
        const QString enlargedViewerUrl =
            session->viewerSource().toString();

        // Shrinking the viewport must reuse the already decoded larger Fit
        // frame. It is the same source version and covers the new target, so
        // replacing it with a smaller frame would only add decode churn and
        // make a later grow decode the image again.
        sourceChanges.clear();
        session->requestViewer(640, 480);
        QCOMPARE(session->viewerSource().toString(), enlargedViewerUrl);
        QTest::qWait(300);
        QCOMPARE(session->viewerSource().toString(), enlargedViewerUrl);
        QCOMPARE(sourceChanges.size(), 0);

        // A source-version change is different: invalidate the old frame and
        // republish a version-namespaced replacement for the active viewer.
        imageEntry.insert(QStringLiteral("mtimeNs"), version + 1);
        QVERIFY(session->applyExternalCatalog({imageEntry, textEntry}, 4));
        QVERIFY(session->viewerOpen());
        QTRY_VERIFY_WITH_TIMEOUT(
            session->viewerSource().toString().startsWith(
                QStringLiteral("image://zoingallery-thumbnails/")) &&
                session->viewerSource().toString() != enlargedViewerUrl,
            5000);
        QVERIFY(session->viewerSource().toString().contains(
            QStringLiteral("-view-v%1-s%2-")
                .arg(version + 1)
                .arg(size)));

        runtime->shutdown();
    }

    void predecodesBoundedSequenceAndReusesNextBackFrames() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QVariantList catalog;
        catalog.reserve(20);
        QHash<int, qint64> versions;
        for (int row = 0; row < 20; ++row) {
            if (row == 9 || row == 12) {
                QVariantMap textEntry = entry(
                    QStringLiteral("text-%1").arg(row), 500 - row,
                    QStringLiteral("note-%1.txt").arg(row));
                textEntry.insert(
                    QStringLiteral("localPath"),
                    directory.filePath(
                        QStringLiteral("note-%1.txt").arg(row)));
                catalog.append(textEntry);
                continue;
            }

            const QString name =
                QStringLiteral("image-%1.png").arg(row, 2, 10,
                                                     QLatin1Char('0'));
            const QString path = directory.filePath(name);
            QImage image(640, 480, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromHsv((row * 31) % 360, 190, 220));
            QVERIFY2(image.save(path), qPrintable(path));
            const QFileInfo file(path);
            const qint64 version =
                file.lastModified().toMSecsSinceEpoch() * 1'000'000 +
                row + 1;
            versions.insert(row, version);
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("image-%1").arg(row)},
                {QStringLiteral("index"), 500 - row},
                {QStringLiteral("name"), name},
                {QStringLiteral("localPath"), path},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("mtimeNs"), version},
                {QStringLiteral("size"), file.size()},
            });
        }

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("sequence"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        constexpr int centerRow = 8;
        QVERIFY(session->applyExternalState(
            QStringLiteral("image-8"), centerRow, {}, 1));
        QCOMPARE(session->currentIndex(), centerRow);
        // Adjacency follows the host's exact catalog order and skips files
        // that the image viewer cannot present.
        QCOMPARE(session->adjacentImageIndex(centerRow, -1), 7);
        QCOMPARE(session->adjacentImageIndex(centerRow, 1), 10);
        QCOMPARE(session->adjacentImageIndex(0, -1), 0);
        QCOMPARE(session->adjacentImageIndex(centerRow, 0), -1);

        session->setViewerOpen(true);
        session->requestViewer(320, 240);

        const auto cachedViewerRows = [&] {
            QSet<int> rows;
            for (int row = 0; row < catalog.size(); ++row) {
                if (session->viewerSourceAt(row).toString().contains(
                        QStringLiteral("/zg-sequence-view-v"))) {
                    rows.insert(row);
                }
            }
            return rows;
        };
        QTRY_COMPARE_WITH_TIMEOUT(cachedViewerRows().size(), 16, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(
            session->imageOriginalSizeAt(centerRow), QSize(640, 480),
            5000);

        QSet<int> expectedRows;
        bool hitStart = false;
        bool hitEnd = false;
        for (int counter = 0;
             expectedRows.size() < 16 && !(hitStart && hitEnd);
             ++counter) {
            const int row = counter % 2 == 0
                ? centerRow + counter / 2
                : centerRow - (counter + 1) / 2;
            if (row < 0) {
                hitStart = true;
            }
            if (row >= catalog.size()) {
                hitEnd = true;
            }
            if (row >= 0 && row < catalog.size() &&
                session->isImageAt(row)) {
                expectedRows.insert(row);
            }
        }
        QCOMPARE(cachedViewerRows(), expectedRows);

        QList<int> outsideRows;
        for (int row = 0; row < catalog.size(); ++row) {
            if (session->isImageAt(row) && !expectedRows.contains(row)) {
                outsideRows.append(row);
            }
        }
        QCOMPARE(outsideRows.size(), 2);
        session->requestViewerAt(outsideRows.first(), 320, 240);
        QCOMPARE(session->currentIndex(), centerRow);
        QTRY_VERIFY_WITH_TIMEOUT(
            !session->viewerSourceAt(outsideRows.first()).isEmpty(),
            5000);
        QCOMPARE(cachedViewerRows().size(), 17);
        QVERIFY(session->viewerSourceAt(outsideRows.last()).isEmpty());

        const int nextRow = session->adjacentImageIndex(centerRow, 1);
        const QUrl centerSource = session->viewerSourceAt(centerRow);
        const QUrl nextSource = session->viewerSourceAt(nextRow);
        QVERIFY(!centerSource.isEmpty());
        QVERIFY(!nextSource.isEmpty());

        // Explicit neighbor preparation must not mutate the host cursor.
        session->requestViewerAt(nextRow, 320, 240);
        QCOMPARE(session->currentIndex(), centerRow);

        // Switching to either predecoded neighbor publishes its existing
        // provider URL synchronously; no decode flash or replacement occurs.
        session->activateIndex(nextRow);
        QCOMPARE(session->viewerSource(), nextSource);
        session->requestViewer(320, 240);
        QCOMPARE(session->viewerSource(), nextSource);

        session->activateIndex(centerRow);
        QCOMPARE(session->viewerSource(), centerSource);
        session->requestViewer(320, 240);
        QCOMPARE(session->viewerSource(), centerSource);

        // Closing releases only presentation state. Reopening the session
        // reuses the prepared frame immediately.
        session->setViewerOpen(false);
        QCOMPARE(session->viewerSourceAt(centerRow), centerSource);
        session->setViewerOpen(true);
        session->requestViewer(320, 240);
        QCOMPARE(session->viewerSource(), centerSource);

        // A nanosecond-only version change at the same path and size removes
        // that file's frame without evicting unaffected neighbors.
        QVariantMap changed = catalog.at(nextRow).toMap();
        const qint64 changedVersion = versions.value(nextRow) + 1;
        changed.insert(QStringLiteral("mtimeNs"), changedVersion);
        catalog[nextRow] = changed;
        QVERIFY(session->applyExternalCatalog(catalog, 2));
        QVERIFY(session->viewerSourceAt(nextRow).isEmpty());
        QCOMPARE(session->viewerSourceAt(centerRow), centerSource);
        QTRY_VERIFY_WITH_TIMEOUT(
            !session->viewerSourceAt(nextRow).isEmpty() &&
                session->viewerSourceAt(nextRow) != nextSource &&
                session->viewerSourceAt(nextRow).toString().contains(
                    QStringLiteral("v%1-s").arg(changedVersion)),
            10000);

        runtime->shutdown();
    }

    void zeroSizeRequestsNativeTierForPlanAndExplicitRow() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QVariantList catalog;
        catalog.reserve(17);
        for (int row = 0; row < 17; ++row) {
            const QString name =
                QStringLiteral("native-%1.png").arg(row, 2, 10,
                                                      QLatin1Char('0'));
            const QString path = directory.filePath(name);
            QImage image(80, 60, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromHsv((row * 37) % 360, 180, 230));
            QVERIFY2(image.save(path), qPrintable(path));
            const QFileInfo file(path);
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("native-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"), name},
                {QStringLiteral("localPath"), path},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("mtimeNs"),
                 file.lastModified().toMSecsSinceEpoch() * 1'000'000 +
                     row + 1},
                {QStringLiteral("size"), file.size()},
            });
        }

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("native-plan"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        constexpr int centerRow = 8;
        QVERIFY(session->applyExternalState(
            QStringLiteral("native-8"), centerRow, {}, 1));

        session->setViewerOpen(true);
        session->requestViewer(0, 0);

        const auto nativeRows = [&] {
            QSet<int> rows;
            for (int row = 0; row < catalog.size(); ++row) {
                if (session->viewerSourceAt(row).toString().startsWith(
                        QStringLiteral("image://zoingallery-async/"))) {
                    rows.insert(row);
                }
            }
            return rows;
        };
        // Native pixels are deliberately limited to current +/-2; the wider
        // 16-item navigation window remains prepared only at the Fit tier.
        QTRY_COMPARE_WITH_TIMEOUT(nativeRows().size(), 5, 10000);
        QCOMPARE(session->viewerSource().toString().startsWith(
                     QStringLiteral("image://zoingallery-async/")),
                 true);

        int outsideRow = -1;
        for (int row = 0; row < catalog.size(); ++row) {
            if (!nativeRows().contains(row)) {
                outsideRow = row;
                break;
            }
        }
        QVERIFY(outsideRow >= 0);
        QVERIFY(session->viewerSourceAt(outsideRow).isEmpty());

        session->requestViewerAt(outsideRow, 0, 0);
        QCOMPARE(session->currentIndex(), centerRow);
        QTRY_VERIFY_WITH_TIMEOUT(
            session->viewerSourceAt(outsideRow).toString().startsWith(
                QStringLiteral("image://zoingallery-async/")),
            5000);

        runtime->shutdown();
    }

    void rejectsStaleSameMillisecondDecodeByOpaqueVersion() {
        constexpr qint64 firstVersion = 1700000000123456789LL;
        constexpr qint64 secondVersion = firstVersion + 1;
        static_assert(firstVersion / 1000000 == secondVersion / 1000000);

        QQmlEngine engine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("stale-token"));
        QVERIFY(session);

        QVariantMap imageEntry = entry(
            QStringLiteral("pixel"), 0, QStringLiteral("pixel.png"), true,
            false, firstVersion);
        QVERIFY(session->applyExternalCatalog({imageEntry}, 1));

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        QVERIFY(imageUrlRole >= 0);
        const QModelIndex modelIndex = session->model()->index(0, 0);

        ImageDecodeRequest result;
        result.info.path =
            imageEntry.value(QStringLiteral("localPath")).toString();
        result.info.lastModified = QDateTime::fromMSecsSinceEpoch(
            firstVersion / 1000000, QTimeZone::UTC);
        result.info.fileSize = 42;
        result.info.sourceVersionToken = QString::number(firstVersion);
        result.targetSize = QSize(32, 24);
        result.requestNamespace = session->sessionId();
        QImage decoded(32, 24, QImage::Format_ARGB32_Premultiplied);
        decoded.fill(Qt::red);

        imageEntry.insert(QStringLiteral("mtimeNs"), secondVersion);
        QVERIFY(session->applyExternalCatalog({imageEntry}, 2));

        // Path, size, and QDateTime millisecond all still match. The opaque
        // nanosecond token is what makes this queued result stale.
        decodeManager->imageReady(result, decoded, {});
        QVERIFY(session->model()->data(modelIndex, imageUrlRole)
                    .toString().isEmpty());

        result.info.sourceVersionToken = QString::number(secondVersion);
        decodeManager->imageReady(result, decoded, {});
        const QString published =
            session->model()->data(modelIndex, imageUrlRole).toString();
        QVERIFY(!published.isEmpty());
        QVERIFY(published.contains(QStringLiteral("-v%1-s42-")
                                       .arg(secondVersion)));

        runtime->shutdown();
    }

    void externalMetadataBypassesMillisecondPersistentCache() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("shape.bmp"));

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = true;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        decodeManager->setImageCacheMode(CacheUsageMode::On);

        PersistentImageCache::clear();
        QImage staleImage(40, 20, QImage::Format_RGB32);
        staleImage.fill(Qt::red);
        QVERIFY(staleImage.save(path, "BMP"));
        const QFileInfo initialFile(path);
        const QDateTime sharedModificationTime = initialFile.lastModified();
        const qint64 sharedFileSize = initialFile.size();

        ImageInfo staleInfo;
        staleInfo.path = path;
        staleInfo.lastModified = sharedModificationTime;
        staleInfo.fileSize = sharedFileSize;
        staleInfo.imageSize = staleImage.size();
        const QByteArray cachedImage =
            PersistentImageCache::createImageForCache(staleImage);
        QVERIFY(!cachedImage.isEmpty());
        PersistentImageCache::storeImage(staleInfo, cachedImage);

        QList<ImageInfo> cachedInfos;
        QStringList cacheMisses;
        PersistentImageCache::retrieveImagesInfo(
            {path}, cachedInfos, cacheMisses, true);
        QCOMPARE(cachedInfos.size(), 1);
        QCOMPARE(cachedInfos.first().imageSize, staleImage.size());
        QVERIFY(cacheMisses.isEmpty());

        // BMP dimensions with the same pixel count produce the same file
        // size. Restore the first modification millisecond after replacing
        // the file so the legacy persistent-cache key still appears valid.
        QImage currentImage(20, 40, QImage::Format_RGB32);
        currentImage.fill(Qt::blue);
        QVERIFY(currentImage.save(path, "BMP"));
        QFile timestampFile(path);
        QVERIFY(timestampFile.open(QIODevice::ReadWrite));
        QVERIFY(timestampFile.setFileTime(
            sharedModificationTime, QFileDevice::FileModificationTime));
        timestampFile.close();
        const QFileInfo currentFile(path);
        QCOMPARE(currentFile.size(), sharedFileSize);
        QCOMPARE(currentFile.lastModified().toMSecsSinceEpoch(),
                 sharedModificationTime.toMSecsSinceEpoch());

        const qint64 currentVersion =
            sharedModificationTime.toMSecsSinceEpoch() * 1000000 + 1;
        QVariantMap imageEntry{
            {QStringLiteral("entryId"), QStringLiteral("shape")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), currentFile.fileName()},
            {QStringLiteral("localPath"), path},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("mtimeNs"), currentVersion},
            {QStringLiteral("size"), sharedFileSize},
        };

        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("metadata-source"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog({imageEntry}, 1));

        auto *externalModel = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(externalModel);
        externalModel->ensurePreviews();
        externalModel->requestImageMetadata({0}, true);
        const int fullSizeRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageFullSizeRole"), -1);
        QVERIFY(fullSizeRole >= 0);
        QTRY_COMPARE_WITH_TIMEOUT(
            session->model()
                ->data(session->model()->index(0, 0), fullSizeRole)
                .toSize(),
            currentImage.size(), 5000);

        runtime->shutdown();
        PersistentImageCache::clear();
    }

    void engineOwnsProviderWithoutDanglingRuntimePointer() {
        constexpr qint64 version = 1700000000123456789LL;
        auto *engine = new QQmlEngine;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(engine);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("engine-teardown"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {entry(QStringLiteral("large"), 0, QStringLiteral("large.png"),
                   true, false, version)},
            1));

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        ImageDecodeRequest result;
        result.info.path = session->localPathAt(0);
        result.info.lastModified = QDateTime::fromMSecsSinceEpoch(
            version / 1000000, QTimeZone::UTC);
        result.info.fileSize = 42;
        result.info.sourceVersionToken = QString::number(version);
        result.targetSize = QSize(2048, 2048);
        result.requestNamespace = session->sessionId();
        QImage decoded(2048, 2048, QImage::Format_ARGB32_Premultiplied);
        decoded.fill(Qt::blue);
        decodeManager->imageReady(result, decoded, {});

        const int imageUrlRole = session->model()->roleNames().key(
            QByteArrayLiteral("imageIdUrlRole"), -1);
        const QString imageUrl = session->model()
                                     ->data(session->model()->index(0, 0),
                                            imageUrlRole)
                                     .toString();
        QVERIFY(!imageUrl.isEmpty());
        QString imageId = QUrl(imageUrl).path();
        if (imageId.startsWith(QLatin1Char('/'))) {
            imageId.remove(0, 1);
        }

        auto *provider = dynamic_cast<QmlAsyncImageProvider *>(
            engine->imageProvider(runtime->asyncProviderName()));
        QVERIFY(provider);
        QList<QQuickImageResponse *> responses;
        for (int index = 0; index < 6; ++index) {
            responses.append(provider->requestImageResponse(
                imageId + QStringLiteral("/0,0,2048,2048"), {}));
        }

        // The provider is engine-owned and can be deleted before QObject
        // deletes the child runtime. That ordering must remain safe even with
        // provider work in flight and no explicit runtime shutdown.
        delete engine;
        qDeleteAll(responses);
    }

    void isolatesTwoSessionsAndPreparesViewerBeforeSignal() {
        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.providerPrefix = QStringLiteral("test-gallery");
        options.maxDecodeThreads = 4;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        QCOMPARE(runtime->thumbnailProviderName(),
                 QStringLiteral("test-gallery-thumbnails"));
        QVERIFY(runtime->decodeWorkerCount() >= 3);
        QVERIFY(runtime->decodeWorkerCount() <= options.maxDecodeThreads);

        ZoinGallery::GallerySession *left =
            runtime->createExternalSession(QStringLiteral("left"));
        ZoinGallery::GallerySession *right =
            runtime->createExternalSession(QStringLiteral("right"));
        QVERIFY(left);
        QVERIFY(right);
        QVERIFY(left != right);
        QCOMPARE(runtime->createExternalSession(QStringLiteral("left")), left);

        QVERIFY(left->applyExternalCatalog(
            {entry(QStringLiteral("left-image"), 4,
                   QStringLiteral("left.png"), true)}, 1));
        QVERIFY(right->applyExternalCatalog(
            {entry(QStringLiteral("right-file"), 9,
                   QStringLiteral("right.txt"))}, 1));

        bool viewerWasPrepared = false;
        connect(left, &ZoinGallery::GallerySession::viewerRequested,
                this, [&](int index, const QUrl &source) {
                    viewerWasPrepared = left->viewerOpen() &&
                        left->currentIndex() == index &&
                        left->viewerSource() == source;
                });
        left->requestOpen(0);
        QVERIFY(viewerWasPrepared);
        QVERIFY(left->viewerOpen());
        QVERIFY(!right->viewerOpen());

        QSignalSpy actionSpy(
            right, &ZoinGallery::GallerySession::actionRequested);
        right->requestOpen(0);
        QCOMPARE(actionSpy.size(), 1);
        QCOMPARE(actionSpy.at(0).at(0).toString(),
                 QStringLiteral("panel.open"));
        QCOMPARE(actionSpy.at(0).at(1).toMap().value(
                     QStringLiteral("index")).toInt(), 9);

        left->shutdown();
        QVERIFY(left->shutdownComplete());
        QVERIFY(!right->shutdownComplete());
        runtime->shutdown();
        QVERIFY(right->shutdownComplete());
    }

    void runtimeBudgetsKeepHugeNativeSeparateFromFitSequence() {
        constexpr int itemCount = 17;
        constexpr int currentRow = 8;
        const QSize nativeSize(100, 100);
        const QSize fitSize(10, 10);
        const qint64 fitFrameBytes =
            static_cast<qint64>(fitSize.width()) * fitSize.height() * 4;
        const qint64 nativeFrameBytes =
            static_cast<qint64>(nativeSize.width()) * nativeSize.height() * 4;

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage sourceImage(nativeSize,
                           QImage::Format_ARGB32_Premultiplied);
        sourceImage.fill(QColor(QStringLiteral("#4285f4")));
        QStringList paths;
        QList<qint64> versions;
        QList<qint64> fileSizes;
        QVariantList catalog;
        for (int row = 0; row < itemCount; ++row) {
            const QString path = directory.filePath(
                QStringLiteral("tier-%1.png").arg(row));
            QVERIFY(sourceImage.save(path));
            const QFileInfo file(path);
            const qint64 version =
                file.lastModified().toMSecsSinceEpoch() * 1'000'000 + row;
            paths.append(path);
            versions.append(version);
            fileSizes.append(file.size());
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("tier-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"), file.fileName()},
                {QStringLiteral("localPath"), path},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("selected"), false},
                {QStringLiteral("mtimeNs"), version},
                {QStringLiteral("size"), file.size()},
            });
        }

        QQmlEngine engine;
        ZoinGallery::RuntimeOptions options;
        options.maxDecodeThreads = 2;
        options.viewerFitCacheByteBudget = fitFrameBytes * 16;
        options.viewerNativeCacheByteBudget = nativeFrameBytes / 2;
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("tier-budgets"));
        QVERIFY(session);

        QVERIFY(session->applyExternalCatalog(catalog, 1));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        QCOMPARE(model->viewerFitByteBudget(),
                 options.viewerFitCacheByteBudget);
        QCOMPARE(model->viewerNativeByteBudget(),
                 options.viewerNativeCacheByteBudget);

        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        const auto imageInfoForRow = [&](int row) {
            ImageInfo info;
            info.path = paths.at(row);
            info.imageSize = nativeSize;
            info.orientation = ExifOrientation::Horizontal;
            info.lastModified = QDateTime::fromMSecsSinceEpoch(
                versions.at(row) / 1'000'000, QTimeZone::UTC);
            info.fileSize = fileSizes.at(row);
            info.sourceVersionToken = QString::number(versions.at(row));
            info.requestNamespace = session->sessionId();
            return info;
        };
        for (int row = 0; row < itemCount; ++row) {
            decodeManager->imageInfoReady(imageInfoForRow(row));
        }

        session->activateIndex(currentRow);
        session->setViewerOpen(true);
        session->requestViewer(fitSize.width(), fitSize.height());

        const auto publishFitFrames = [&] {
            for (int row = 0; row < itemCount; ++row) {
                ImageDecodeRequest request;
                request.info = imageInfoForRow(row);
                request.targetSize = fitSize;
                request.viewerRequest = true;
                request.fitToViewerRequest = true;
                request.requestNamespace = session->sessionId();
                QImage decoded(fitSize,
                               QImage::Format_ARGB32_Premultiplied);
                decoded.fill(QColor::fromHsv((row * 31) % 360, 180, 220));
                decodeManager->imageReady(request, decoded, {});
            }
        };
        publishFitFrames();
        QCOMPARE(model->viewerFitFrameCount(), 16);
        QCOMPARE(model->viewerFitRetainedBytes(),
                 options.viewerFitCacheByteBudget);
        QCOMPARE(model->viewerNativeFrameCount(), 0);

        const auto hasFitSource = [&](int row) {
            const QVariantList sources = session->viewerSourcesAt(row);
            return std::any_of(
                sources.cbegin(), sources.cend(), [](const QVariant &value) {
                    return value.toMap().value(QStringLiteral("level"))
                               .toInt() == 1;
                });
        };
        int preparedFitFrames = 0;
        for (int row = 0; row < itemCount; ++row) {
            preparedFitFrames += hasFitSource(row) ? 1 : 0;
        }
        QCOMPARE(preparedFitFrames, 16);

        const auto publishNativeFrame = [&](int row, const QColor &color) {
            ImageDecodeRequest request;
            request.info = imageInfoForRow(row);
            request.targetSize = nativeSize;
            request.viewerRequest = true;
            request.fitToViewerRequest = false;
            request.requestNamespace = session->sessionId();
            QImage decoded(nativeSize,
                           QImage::Format_ARGB32_Premultiplied);
            decoded.fill(color);
            decodeManager->imageReady(request, decoded, {});
        };
        publishNativeFrame(currentRow, Qt::red);
        QCOMPARE(model->viewerNativeFrameCount(), 1);
        QCOMPARE(model->viewerNativeRetainedBytes(), nativeFrameBytes);
        QVERIFY(model->viewerNativeRetainedBytes() >
                model->viewerNativeByteBudget());
        QCOMPARE(model->viewerFitFrameCount(), 16);
        QCOMPARE(model->viewerFitRetainedBytes(),
                 options.viewerFitCacheByteBudget);

        // Changing current releases the previous oversized native before
        // pinning the next one. Session navigation therefore cannot grow an
        // unbounded chain of over-budget panoramas.
        const int nextCurrentRow = currentRow - 1;
        session->activateIndex(nextCurrentRow);
        QCOMPARE(model->viewerNativeFrameCount(), 0);
        QCOMPARE(model->viewerNativeRetainedBytes(), 0);
        publishFitFrames();
        QCOMPARE(model->viewerFitFrameCount(), 16);
        publishNativeFrame(nextCurrentRow, Qt::green);
        QCOMPARE(model->viewerNativeFrameCount(), 1);
        QCOMPARE(model->viewerNativeRetainedBytes(), nativeFrameBytes);
        QCOMPARE(model->viewerFitFrameCount(), 16);
        QCOMPARE(model->viewerFitRetainedBytes(),
                 options.viewerFitCacheByteBudget);

        runtime->shutdown();
    }

    void loadsWindowlessQmlComponents() {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        ZoinGallery::GalleryRuntime *runtime =
            ZoinGallery::GalleryRuntime::install(&engine);
        ZoinGallery::GallerySession *session =
            runtime->createExternalSession(QStringLiteral("qml"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {entry(QStringLiteral("plain"), 1,
                   QStringLiteral("plain.txt"))}, 1));
        engine.rootContext()->setContextProperty(
            QStringLiteral("testGallerySession"), session);

        QQmlComponent panel(&engine);
        panel.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 640
                height: 480
                session: testGallerySession
            }
        )QML", QUrl(QStringLiteral("inline:GalleryPanelTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(
            panel.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(panel.isReady(), qPrintable(panel.errorString()));
        QScopedPointer<QObject> panelObject(panel.create());
        QVERIFY2(panelObject, qPrintable(panel.errorString()));

        QQmlComponent viewer(&engine);
        viewer.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryViewer {
                width: 640
                height: 480
                session: testGallerySession
            }
        )QML", QUrl(QStringLiteral("inline:GalleryViewerTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(
            viewer.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(viewer.isReady(), qPrintable(viewer.errorString()));
        QScopedPointer<QObject> viewerObject(viewer.create());
        QVERIFY2(viewerObject, qPrintable(viewer.errorString()));

        runtime->shutdown();
    }
};

QTEST_MAIN(GallerySessionTest)
#include "GallerySessionTest.moc"
