#include "DecodeManager.h"
#include "Decoders/ImageDecoderFactory.h"
#include "Decoders/ImageDecoderInterface.h"
#include "FileListModel.h"
#include "ProviderImageStore.h"
#include "SelectedImagesModel.h"

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>

namespace {

class TestRunner final : public Runner {
public:
    RunnerType type() override { return RunnerType::ImageDecode; }
};

ImageDecodeRequest decodeRequest(const QString &path,
                                 const QString &requestNamespace,
                                 bool viewerRequest) {
    ImageDecodeRequest request;
    request.info.path = path;
    request.info.lastModified = QFileInfo(path).lastModified();
    request.info.fileSize = QFileInfo(path).size();
    request.info.imageSize = QSize(48, 32);
    request.targetSize = QSize(24, 16);
    request.requestNamespace = requestNamespace;
    request.viewerRequest = viewerRequest;
    request.expandToCacheResolution = false;
    request.storeInPersistentCache = false;
    return request;
}

} // namespace

class DecodeLifecycleTest : public QObject {
    Q_OBJECT

private slots:
    void automaticWorkerLimitPreservesStandaloneParallelism() {
        const int ideal = qMax(1, QThread::idealThreadCount());
        const int reservedMinimum = 3;

        {
            DecodeManager automatic(nullptr, 0);
            QCOMPARE(automatic.workerCount(),
                     qMax(reservedMinimum, ideal));
            const QList<QThread *> workers =
                automatic.findChildren<QThread *>();
            QCOMPARE(workers.size(), automatic.workerCount());
            QVERIFY(std::none_of(
                workers.cbegin(), workers.cend(),
                [](const QThread *worker) { return worker->isRunning(); }));
        }
        {
            DecodeManager bounded(nullptr, 4);
            QCOMPARE(bounded.workerCount(),
                     qMax(reservedMinimum, qMin(ideal, 4)));
            const QList<QThread *> workers =
                bounded.findChildren<QThread *>();
            QCOMPARE(workers.size(), bounded.workerCount());
            QVERIFY(std::none_of(
                workers.cbegin(), workers.cend(),
                [](const QThread *worker) { return worker->isRunning(); }));
        }
    }

    void decoderFactoryReturnsScopedOwnership() {
        static_assert(std::is_same_v<
                      decltype(ImageDecoderFactory::createDecoder(0)),
                      std::unique_ptr<ImageDecoderInterface>>);

        QVERIFY(ImageDecoderFactory::decoderCount() > 0);
        for (int i = 0; i < ImageDecoderFactory::decoderCount(); ++i) {
            const auto decoder = ImageDecoderFactory::createDecoder(i);
            QVERIFY(decoder);
            QVERIFY(!decoder->decoderName().isEmpty());
        }
        QVERIFY(!ImageDecoderFactory::createDecoder(-1));
        QVERIFY(!ImageDecoderFactory::createDecoder(
            ImageDecoderFactory::decoderCount()));
    }

    void cancellationIsVisibleAcrossThreads() {
        TestRunner runner;
        std::atomic_bool observed{false};
        std::thread worker([&] {
            while (!runner.isCanceled()) {
                std::this_thread::yield();
            }
            observed.store(true, std::memory_order_release);
        });

        runner.cancel();
        worker.join();

        QVERIFY(runner.isCanceled());
        QVERIFY(observed.load(std::memory_order_acquire));
    }

    void namespaceCancellationSeparatesThumbnailAndViewerRequests() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath = directory.filePath(QStringLiteral("test.bmp"));
        QImage source(48, 32, QImage::Format_RGB32);
        source.fill(QColor(40, 90, 180));
        QVERIFY(source.save(imagePath, "BMP"));

        const auto runScenario = [&](bool cancelViewer) {
            DecodeManager manager(nullptr, 3);
            manager.setImageCacheMode(CacheUsageMode::Off);

            QStringList completed;
            connect(&manager, &DecodeManager::imageReady, &manager,
                    [&completed](const ImageDecodeRequest &request,
                                 const QImage &image,
                                 const DecodedImageInfo &) {
                        QVERIFY(!image.isNull());
                        completed.append(QStringLiteral("%1:%2").arg(
                            request.requestNamespace,
                            request.viewerRequest ? QStringLiteral("viewer")
                                                  : QStringLiteral("thumbnail")));
                    });

            ImageDecodeRequest thumbnailA = decodeRequest(
                imagePath, QStringLiteral("A"), false);
            ImageDecodeRequest viewerA = decodeRequest(
                imagePath, QStringLiteral("A"), true);
            ImageDecodeRequest thumbnailB = decodeRequest(
                imagePath, QStringLiteral("B"), false);
            ImageDecodeRequest viewerB = decodeRequest(
                imagePath, QStringLiteral("B"), true);

            // Exercise normalization from ImageInfo as well as the outer
            // ImageDecodeRequest namespace used by the decode stages.
            if (cancelViewer) {
                viewerA.info.requestNamespace = viewerA.requestNamespace;
                viewerA.requestNamespace.clear();
            }
            else {
                thumbnailA.info.requestNamespace =
                    thumbnailA.requestNamespace;
                thumbnailA.requestNamespace.clear();
            }

            manager.decodeImages(
                {thumbnailA, viewerA, thumbnailB, viewerB});
            if (cancelViewer) {
                manager.cancelViewerRequests(QStringLiteral("A"));
            }
            else {
                manager.cancelThumbnailRequests(QStringLiteral("A"));
            }

            QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 3, 5000);
            const QString canceledKind = cancelViewer
                ? QStringLiteral("A:viewer")
                : QStringLiteral("A:thumbnail");
            const QString retainedKind = cancelViewer
                ? QStringLiteral("A:thumbnail")
                : QStringLiteral("A:viewer");
            QVERIFY(!completed.contains(canceledKind));
            QVERIFY(completed.contains(retainedKind));
            QVERIFY(completed.contains(QStringLiteral("B:thumbnail")));
            QVERIFY(completed.contains(QStringLiteral("B:viewer")));
        };

        runScenario(false);
        runScenario(true);
    }

    void closingNamespacedViewerPreservesThumbnailQueue() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QStringList paths;
        for (int index = 0; index < 3; ++index) {
            const QString path = directory.filePath(
                QStringLiteral("thumbnail-%1.bmp").arg(index));
            QImage source(256, 192, QImage::Format_RGB32);
            source.fill(QColor(30 + index * 40, 80, 170));
            QVERIFY(source.save(path, "BMP"));
            paths.append(path);
        }

        DecodeManager manager(nullptr, 3);
        manager.setImageCacheMode(CacheUsageMode::Off);
        manager.setFileListCacheMode(CacheUsageMode::Off);
        const auto store = QSharedPointer<ProviderImageStore>::create();
        FileListModel model(
            store, &manager, QStringLiteral("standalone-regression"),
            QStringLiteral("standalone-regression-"),
            QStringLiteral("standalone-regression-thumbnails"),
            QStringLiteral("standalone-regression-async"));

        QList<ImageDecodeRequest> completed;
        connect(&manager, &DecodeManager::imageReady, &manager,
                [&completed](const ImageDecodeRequest &request,
                             const QImage &image,
                             const DecodedImageInfo &) {
                    QVERIFY(!image.isNull());
                    completed.append(request);
                });
        QSignalSpy viewerCanceledSpy(
            &manager, &DecodeManager::viewerRunnerCanceled);

        QList<ImageDecodeRequest> requests;
        for (const QString &path : paths) {
            requests.append(decodeRequest(
                path, QStringLiteral("ignored-by-model"), false));
        }
        ImageDecodeRequest viewer = decodeRequest(
            paths.first(), QStringLiteral("ignored-by-model"), true);
        requests.prepend(viewer);

        model.decodeImages(requests);
        model.cancelAllDecodeViewerRunnersForViewerClose();

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), paths.size(), 5000);
        for (const ImageDecodeRequest &request : std::as_const(completed)) {
            QVERIFY(!request.viewerRequest);
            QCOMPARE(request.requestNamespace,
                     QStringLiteral("standalone-regression"));
        }
        QVERIFY(viewerCanceledSpy.size() >= 1);
        QTest::qWait(100);
        QCOMPARE(completed.size(), paths.size());

        model.shutdown();
    }

    void closingSelectedImagesViewerPreservesThumbnailQueue() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("selected-thumbnail.bmp"));
        QImage source(256, 192, QImage::Format_RGB32);
        source.fill(QColor(65, 120, 195));
        QVERIFY(source.save(path, "BMP"));

        DecodeManager manager(nullptr, 3);
        manager.setImageCacheMode(CacheUsageMode::Off);
        manager.setFileListCacheMode(CacheUsageMode::Off);
        const auto store = QSharedPointer<ProviderImageStore>::create();
        FileListModel sourceModel(
            store, &manager, QStringLiteral("selected-source-regression"),
            QStringLiteral("selected-source-regression-"),
            QStringLiteral("selected-regression-thumbnails"),
            QStringLiteral("selected-regression-async"));
        SelectedImagesModel selectedModel(
            &sourceModel, store, &manager,
            QStringLiteral("selected-view-regression"),
            QStringLiteral("selected-view-regression-"),
            QStringLiteral("selected-regression-thumbnails"),
            QStringLiteral("selected-regression-async"));

        QList<ImageDecodeRequest> completed;
        connect(&manager, &DecodeManager::imageReady, &manager,
                [&completed](const ImageDecodeRequest &request,
                             const QImage &image,
                             const DecodedImageInfo &) {
                    QVERIFY(!image.isNull());
                    completed.append(request);
                });

        ImageDecodeRequest thumbnail = decodeRequest(
            path, QStringLiteral("ignored-by-model"), false);
        ImageDecodeRequest viewer = decodeRequest(
            path, QStringLiteral("ignored-by-model"), true);
        selectedModel.decodeImages({viewer, thumbnail});
        selectedModel.cancelAllDecodeViewerRunnersForViewerClose();

        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QVERIFY(!completed.first().viewerRequest);
        QCOMPARE(completed.first().requestNamespace,
                 QStringLiteral("selected-view-regression"));
        QTest::qWait(100);
        QCOMPARE(completed.size(), 1);

        selectedModel.prepareToClose();
        sourceModel.shutdown();
    }

    void inaccessibleFolderPreviewStopsRetryingAndCanBeRefreshed() {
#ifndef Q_OS_UNIX
        QSKIP("This permission-based regression requires POSIX permissions");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString blockedPath =
            directory.filePath(QStringLiteral("blocked"));
        QVERIFY(QDir().mkpath(blockedPath));
        const QString sentinelPath =
            QDir(blockedPath).filePath(QStringLiteral("sentinel.txt"));
        QFile sentinel(sentinelPath);
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        QCOMPARE(sentinel.write("blocked"), qint64(7));
        sentinel.close();

        const QFileDevice::Permissions originalPermissions =
            QFileInfo(blockedPath).permissions();
        struct PermissionRestorer {
            QString path;
            QFileDevice::Permissions permissions;
            ~PermissionRestorer() {
                QFile::setPermissions(path, permissions);
            }
        } permissionRestorer{blockedPath, originalPermissions};
        QVERIFY(QFile::setPermissions(blockedPath, {}));

        QFile permissionProbe(sentinelPath);
        if (permissionProbe.open(QIODevice::ReadOnly)) {
            QSKIP("The test process can still traverse a mode-000 directory");
        }

        DecodeManager manager(nullptr, 3);
        manager.setImageCacheMode(CacheUsageMode::Off);
        manager.setFileListCacheMode(CacheUsageMode::Off);
        const auto store = QSharedPointer<ProviderImageStore>::create();
        FileListModel model(
            store, &manager, QStringLiteral("folder-retry-regression"),
            QStringLiteral("folder-retry-regression-"),
            QStringLiteral("folder-retry-regression-thumbnails"),
            QStringLiteral("folder-retry-regression-async"));
        QSignalSpy failedSpy(&manager, &DecodeManager::folderListFailed);
        QSignalSpy readySpy(&manager, &DecodeManager::folderListReady);

        model.cd(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 5000);
        const quint64 generation =
            failedSpy.constFirst().at(2).toULongLong();

        // Advance through the same-generation backoff synchronously. The last
        // failure reaches the cap and must invalidate every queued singleShot.
        for (int retry = 0; retry < 4; ++retry) {
            manager.folderListFailed(blockedPath,
                                     QStringLiteral("forced failure"),
                                     generation);
        }
        QCOMPARE(failedSpy.size(), 5);
        QTest::qWait(750);
        QCOMPARE(failedSpy.size(), 5);

        QVERIFY(QFile::setPermissions(blockedPath, originalPermissions));
        const QString imagePath =
            QDir(blockedPath).filePath(QStringLiteral("recovered.bmp"));
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(QColor(80, 140, 210));
        QVERIFY(image.save(imagePath, "BMP"));

        model.cd(directory.path());
        const auto recoveredPreviewReady = [&readySpy, &blockedPath] {
            for (const QList<QVariant> &arguments : readySpy) {
                if (arguments.at(0).toString() == blockedPath) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(recoveredPreviewReady(), 5000);

        model.shutdown();
        manager.prepareToClose();
#endif
    }
};

QTEST_GUILESS_MAIN(DecodeLifecycleTest)

#include "DecodeLifecycleTest.moc"
