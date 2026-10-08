#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include "DecodeManager.h"
#include "FileListModel.h"
#include "ImageFile.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QScreen>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#endif

#include <cmath>

namespace {

QVariantMap imageEntry(const QString &id, int sourceIndex,
                       const QString &path) {
    return {
        {QStringLiteral("entryId"), id},
        {QStringLiteral("index"), sourceIndex},
        {QStringLiteral("name"), QFileInfo(path).fileName()},
        {QStringLiteral("localPath"), path},
        {QStringLiteral("isDir"), false},
        {QStringLiteral("isImage"), true},
        {QStringLiteral("selected"), false},
        {QStringLiteral("mtimeNs"), qint64(0)},
        {QStringLiteral("size"), QFileInfo(path).size()},
    };
}

bool writeImage(const QString &path, const QSize &size, const QColor &color) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image.save(path);
}

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
QVideoFrame solidVideoFrame(const QSize &size, const QColor &color) {
    QVideoFrame frame(QVideoFrameFormat(
        size, QVideoFrameFormat::Format_BGRA8888));
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    for (int y = 0; y < size.height(); ++y) {
        uchar *row = frame.bits(0) + y * frame.bytesPerLine(0);
        for (int x = 0; x < size.width(); ++x) {
            uchar *pixel = row + x * 4;
            pixel[0] = static_cast<uchar>(color.blue());
            pixel[1] = static_cast<uchar>(color.green());
            pixel[2] = static_cast<uchar>(color.red());
            pixel[3] = static_cast<uchar>(color.alpha());
        }
    }
    frame.unmap();
    return frame;
}

QVideoFrame neutralNv12VideoFrame(const QSize &size, uchar luma) {
    QVideoFrameFormat format(size, QVideoFrameFormat::Format_NV12);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
    format.setColorRange(QVideoFrameFormat::ColorRange_Full);
    format.setColorTransfer(QVideoFrameFormat::ColorTransfer_BT709);
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly) || frame.planeCount() != 2)
        return {};

    for (int y = 0; y < size.height(); ++y) {
        std::memset(frame.bits(0) + y * frame.bytesPerLine(0), luma,
                    size.width());
    }
    for (int y = 0; y < size.height() / 2; ++y) {
        std::memset(frame.bits(1) + y * frame.bytesPerLine(1), 128,
                    frame.bytesPerLine(1));
    }
    frame.unmap();
    return frame;
}

void selectScreenAtDpr(QQuickView &view, qreal requestedDpr) {
    for (QScreen *screen : QGuiApplication::screens()) {
        if (qAbs(screen->devicePixelRatio() - requestedDpr) < 0.001) {
            view.setScreen(screen);
            view.setPosition(screen->geometry().topLeft() + QPoint(40, 40));
            return;
        }
    }
}

bool supportsRhiVideoRendering(const QQuickView &view) {
    if (!view.rendererInterface())
        return false;
    const auto api = view.rendererInterface()->graphicsApi();
    return api == QSGRendererInterface::OpenGL
        || api == QSGRendererInterface::Direct3D11
        || api == QSGRendererInterface::Vulkan
        || api == QSGRendererInterface::Metal
        || api == QSGRendererInterface::Direct3D12;
}
#endif

QObject *createRoot(QQuickView &view, const QByteArray &qml,
                    const QString &name) {
    auto *component = new QQmlComponent(view.engine(), &view);
    component->setData(qml, QUrl(QStringLiteral("inline:") + name));
    if (component->isLoading()) {
        QSignalSpy statusSpy(component, &QQmlComponent::statusChanged);
        statusSpy.wait(5000);
    }
    if (!component->isReady()) {
        qWarning().noquote() << component->errorString();
        return nullptr;
    }
    QObject *root = component->create();
    if (!root) {
        qWarning().noquote() << component->errorString();
        return nullptr;
    }
    view.setContent(QUrl(QStringLiteral("inline:") + name), component, root);
    return root;
}

} // namespace

class GalleryQmlInteractionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
#ifdef Q_OS_LINUX
        if (qgetenv("QSG_RHI_BACKEND") == "opengl") {
            QSurfaceFormat format;
            format.setVersion(3, 2);
            format.setProfile(QSurfaceFormat::CoreProfile);
            QSurfaceFormat::setDefaultFormat(format);
            QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        }
#endif
    }

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    void videoSourceInitializationWaitsForViewerExpandAnimation();

    void imageWheelZoomFrameTimingAt175Percent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString imagePath = qEnvironmentVariable("ZOIN_ZOOM_TEST_IMAGE");
        if (imagePath.isEmpty()) {
            imagePath = directory.filePath(QStringLiteral("zoom.png"));
            const int width = qMax(6000, qEnvironmentVariableIntValue("ZOIN_ZOOM_TEST_WIDTH"));
            QVERIFY(writeImage(imagePath, QSize(width, width * 2 / 3), QColor(82, 134, 182)));
        }
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("wheel-zoom-timing"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog({imageEntry(QStringLiteral("image"), 0, imagePath)}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("image"), 0, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty(QStringLiteral("zoomSession"), session);
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 1920; height: 1000
                GalleryViewer {
                    objectName: "wheelZoomViewer"
                    anchors.fill: parent
                    session: zoomSession
                    animationDuration: 250
                }
            }
        )QML", QStringLiteral("ImageWheelZoomTiming.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        selectScreenAtDpr(view, 1.75);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *viewer = root->findChild<QQuickItem *>(QStringLiteral("wheelZoomViewer"));
        auto *viewport = root->findChild<QQuickItem *>(QStringLiteral("galleryViewerViewport"));
        auto *native = root->findChild<QQuickItem *>(QStringLiteral("galleryViewerNativeImage"));
        QVERIFY(viewer);
        QVERIFY(viewport);
        QVERIFY(native);
        viewer->forceActiveFocus();
        QTRY_VERIFY_WITH_TIMEOUT(viewport->property("originalSize").toSizeF().width() > 1, 10000);
        QVERIFY(QMetaObject::invokeMethod(viewport, "zoomTo100", Q_ARG(QVariant, false)));
        QTRY_COMPARE_WITH_TIMEOUT(native->property("status").toInt(), 1, 10000);
        QTRY_VERIFY(!viewport->property("viewportAnimationRunning").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewport, "zoomTo100", Q_ARG(QVariant, false)));
        QTRY_VERIFY(!viewport->property("viewportAnimationRunning").toBool());
        QCOMPARE(viewport->property("zoomScale").toReal(), 1.0);
        const QUrl nativeSource = native->property("source").toUrl();
        QVERIFY(!nativeSource.isEmpty());
        for (bool nearest : {false, true}) {
            viewer->setProperty("nearestNeighbor", nearest);
            QTest::qWait(150);
            QElapsedTimer clock;
            clock.start();
            qint64 previous = 0;
            qint64 worst = 0;
            int count = 0;
            bool previousActive = false;
            qreal minimumZoom = 1;
            const auto connection = connect(&view, &QQuickWindow::frameSwapped, &view, [&] {
                const qint64 now = clock.elapsed();
                const bool active = viewport->property("viewportAnimationRunning").toBool();
                if (previousActive && active) {
                    worst = qMax(worst, now - previous);
                    if (now - previous > 50)
                        qInfo() << "wheel active gap" << now - previous
                                << "zoom" << viewport->property("zoomScale");
                }
                previousActive = active;
                minimumZoom = qMin(minimumZoom, viewport->property("zoomScale").toReal());
                previous = now;
                ++count;
            });
            QTest::mouseMove(&view, QPoint(420, 320));
            QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(420, 320));
            for (int direction : {-1, 1, -1, 1}) {
                for (int tick = 0; tick < 13; ++tick) {
                    QTest::wheelEvent(&view, QPointF(420, 320), QPoint(0, direction * 120));
                    QTest::qWait(35);
                }
                QTest::qWait(300);
            }
            QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(420, 320));
            disconnect(connection);
            qInfo() << "wheel zoom timing nearest" << nearest << "frames" << count
                    << "max active gap ms" << worst << "min zoom" << minimumZoom
                    << "final zoom" << viewport->property("zoomScale");
            QCOMPARE(native->property("source").toUrl(), nativeSource);
            QVERIFY2(worst < 100, "Animated zoom must not stall for a tenth of a second");
            QVERIFY(minimumZoom < 0.05);
            const QString capturePath = qEnvironmentVariable("ZOIN_ZOOM_TEST_CAPTURE");
            if (!capturePath.isEmpty()) {
                QVERIFY(view.grabWindow().save(capturePath + (nearest
                    ? QStringLiteral("-nearest.png") : QStringLiteral("-normal.png"))));
            }
        }
        runtime->shutdown();
    }

    void imageZoomFrameTimingAt175Percent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath = directory.filePath(QStringLiteral("zoom.png"));
        QVERIFY(writeImage(imagePath, QSize(6000, 4000), QColor(82, 134, 182)));
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("zoomImageUrl"), QUrl::fromLocalFile(imagePath));
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                id: scene
                width: 840; height: 640
                property real zoom: 1
                Image { id: pixels; source: zoomImageUrl; visible: false }
                ViewerResample {
                    id: effect
                    objectName: "zoomEffect"
                    width: 6000 / 1.75 * scene.zoom
                    height: 4000 / 1.75 * scene.zoom
                    imageSource: pixels
                    viewportSize: Qt.size(width * 1.75, height * 1.75)
                }
                NumberAnimation {
                    id: animation; target: scene; property: "zoom"
                    duration: 650; easing.type: Easing.InOutQuad
                }
                function zoomTo(value) {
                    animation.to = value
                    animation.restart()
                }
            }
        )QML", QStringLiteral("ImageZoomFrameTiming.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        selectScreenAtDpr(view, 1.75);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.devicePixelRatio(), 1.75);
        if (!supportsRhiVideoRendering(view))
            QSKIP("zoom timing requires an RHI backend");
        auto *effect = root->findChild<QObject *>(QStringLiteral("zoomEffect"));
        QVERIFY(effect);
        QTRY_COMPARE(effect->property("imagePixelSize").toSizeF(), QSizeF(6000, 4000));
        QTest::qWait(250);

        for (bool nearest : {false, true}) {
            effect->setProperty("nearestNeighbor", nearest);
            for (qreal target : {0.05, 1.0, 0.05, 1.0}) {
                QElapsedTimer clock;
                clock.start();
                qint64 previous = 0;
                qint64 worst = 0;
                int count = 0;
                const auto connection = connect(&view, &QQuickWindow::frameSwapped,
                    &view, [&] {
                        const qint64 now = clock.elapsed();
                        worst = qMax(worst, now - previous);
                        previous = now;
                        ++count;
                    });
                QVERIFY(QMetaObject::invokeMethod(root, "zoomTo", Q_ARG(QVariant, target)));
                QTest::qWait(800);
                disconnect(connection);
                qInfo() << "zoom timing nearest" << nearest << "target" << target
                        << "frames" << count << "max gap ms" << worst
                        << "retained levels" << effect->property("retainedLevels");
            }
        }
    }

    void videoResamplerMatchesImageFilterAt175Percent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("resample-video.png"));
        QImage sourceImage(QSize(24, 16), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < sourceImage.height(); ++y) {
            for (int x = 0; x < sourceImage.width(); ++x) {
                sourceImage.setPixelColor(x, y, QColor((x * 37 + y * 11) % 256,
                                                       (x * 13 + y * 53) % 256,
                                                       (x * 71 + y * 7) % 256));
            }
        }
        QVERIFY(sourceImage.save(path));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("videoResampleImage"), QUrl::fromLocalFile(path));
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 340; height: 180
                Image {
                    id: source
                    objectName: "videoResampleSource"
                    source: videoResampleImage
                    sourceSize: Qt.size(24, 16)
                    width: 24; height: 16
                    asynchronous: false
                }
                ViewerVideoFrameSource {
                    id: frameSource
                    objectName: "galleryVideoFrameSource"
                }
                ViewerResample {
                    objectName: "imageResampleReference"
                    x: 0; y: 8; width: 160; height: 160
                    imageSource: source
                    viewportSize: Qt.size(width * (Window.window ? Window.window.devicePixelRatio : 1),
                                          height * (Window.window ? Window.window.devicePixelRatio : 1))
                    sourceExtent: source.sourceSize
                }
                ViewerResample {
                    objectName: "videoResampleEffect"
                    x: 180; y: 8; width: 160; height: 160
                    imageSource: source
                    videoFrameSource: frameSource
                    viewportSize: Qt.size(width * (Window.window ? Window.window.devicePixelRatio : 1),
                                          height * (Window.window ? Window.window.devicePixelRatio : 1))
                    sourceExtent: source.sourceSize
                }
            }
        )QML", QStringLiteral("VideoResampler175.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        selectScreenAtDpr(view, 1.75);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        selectScreenAtDpr(view, 1.75);
        QCOMPARE(view.devicePixelRatio(), 1.75);
        if (!supportsRhiVideoRendering(view))
            QSKIP("video resampler comparison requires an RHI backend");

        auto *frameSource = root->findChild<QObject *>(
            QStringLiteral("galleryVideoFrameSource"));
        QVERIFY(frameSource);
        auto *sink = qobject_cast<QVideoSink *>(
            frameSource->property("sink").value<QObject *>());
        QVERIFY(sink);
        sink->setVideoFrame(QVideoFrame(sourceImage));
        QTRY_VERIFY(frameSource->property("hasFrame").toBool());

        auto *videoEffect = root->findChild<QQuickItem *>(
            QStringLiteral("videoResampleEffect"));
        QVERIFY(videoEffect);
        const QPointF origin = videoEffect->mapToItem(view.contentItem(), QPointF());
        const QPointF physicalOrigin = origin * view.devicePixelRatio();
        QVERIFY2(qAbs(physicalOrigin.x() - qRound(physicalOrigin.x())) < 0.001
                 && qAbs(physicalOrigin.y() - qRound(physicalOrigin.y())) < 0.001,
                 qPrintable(QStringLiteral("video resampler origin is (%1, %2) physical px")
                                .arg(physicalOrigin.x(), 0, 'f', 6)
                                .arg(physicalOrigin.y(), 0, 'f', 6)));
        QVERIFY(qAbs(videoEffect->width() * view.devicePixelRatio()
                     - qRound(videoEffect->width() * view.devicePixelRatio())) < 0.001);
        QVERIFY(qAbs(videoEffect->height() * view.devicePixelRatio()
                     - qRound(videoEffect->height() * view.devicePixelRatio())) < 0.001);
        const QPointF dx = videoEffect->mapToItem(view.contentItem(), QPointF(1, 0))
                           - origin;
        const QPointF dy = videoEffect->mapToItem(view.contentItem(), QPointF(0, 1))
                           - origin;
        QVERIFY(QLineF(dx, QPointF(1, 0)).length() < 0.0001);
        QVERIFY(QLineF(dy, QPointF(0, 1)).length() < 0.0001);

        QTest::qWait(100);
        const QImage capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        for (const QPointF point : {QPointF(40, 40), QPointF(80, 80),
                                    QPointF(120, 120)}) {
            const QPoint stillPixel(qRound(point.x() * view.devicePixelRatio()),
                                    qRound((point.y() + 8) * view.devicePixelRatio()));
            const QPoint videoPixel(qRound((point.x() + 180) * view.devicePixelRatio()),
                                    stillPixel.y());
            const QColor stillColor = capture.pixelColor(stillPixel);
            const QColor videoColor = capture.pixelColor(videoPixel);
            const QString diagnostic = QStringLiteral("still %1 versus video %2")
                .arg(stillColor.name(QColor::HexArgb),
                     videoColor.name(QColor::HexArgb));
            QVERIFY2(qAbs(stillColor.red() - videoColor.red()) <= 3
                     && qAbs(stillColor.green() - videoColor.green()) <= 3
                     && qAbs(stillColor.blue() - videoColor.blue()) <= 3,
                     qPrintable(diagnostic));
        }
        auto *imageEffect = root->findChild<QQuickItem *>(
            QStringLiteral("imageResampleReference"));
        QVERIFY(imageEffect);
        QVERIFY(imageEffect->setProperty("nearestNeighbor", true));
        QVERIFY(videoEffect->setProperty("nearestNeighbor", true));
        QTest::qWait(100);
        const QImage nearestCapture = view.grabWindow();
        QVERIFY(!nearestCapture.isNull());
        for (const QPoint outputPixel : {QPoint(33, 29), QPoint(97, 111), QPoint(201, 219)}) {
            const QPoint sourcePixel(outputPixel.x() * 24 / 280,
                                     outputPixel.y() * 16 / 280);
            const QColor expected = sourceImage.pixelColor(sourcePixel);
            for (const int left : {0, 315}) {
                const QColor actual = nearestCapture.pixelColor(
                    left + outputPixel.x(), 14 + outputPixel.y());
                QVERIFY(qAbs(actual.red() - expected.red()) <= 3);
                QVERIFY(qAbs(actual.green() - expected.green()) <= 3);
                QVERIFY(qAbs(actual.blue() - expected.blue()) <= 3);
            }
        }
    }

    void videoIdentityBranchConvertsYuvAt175Percent() {
        constexpr QSize frameSize(32, 18);
        constexpr uchar luma = 160;
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 80; height: 60
                ViewerVideoFrameSource {
                    id: frameSource
                    objectName: "identityVideoFrameSource"
                }
                ViewerResample {
                    objectName: "identityVideoResampleEffect"
                    width: 32 / (Window.window ? Window.window.devicePixelRatio : 1)
                    height: 18 / (Window.window ? Window.window.devicePixelRatio : 1)
                    viewportSize: Qt.size(32, 18)
                    videoFrameSource: frameSource
                    sourceExtent: Qt.size(32, 18)
                    pixelAligned: true
                }
            }
        )QML", QStringLiteral("VideoIdentityYuv175.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        selectScreenAtDpr(view, 1.75);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        selectScreenAtDpr(view, 1.75);
        QCOMPARE(view.devicePixelRatio(), 1.75);
        if (!supportsRhiVideoRendering(view))
            QSKIP("video identity rendering requires an RHI backend");

        auto *frameSource = root->findChild<QObject *>(
            QStringLiteral("identityVideoFrameSource"));
        auto *effect = root->findChild<QQuickItem *>(
            QStringLiteral("identityVideoResampleEffect"));
        QVERIFY(frameSource);
        QVERIFY(effect);
        auto *sink = qobject_cast<QVideoSink *>(
            frameSource->property("sink").value<QObject *>());
        QVERIFY(sink);

        QVideoFrame frame = neutralNv12VideoFrame(frameSize, luma);
        QVERIFY(frame.isValid());
        const QImage expectedImage = frame.toImage();
        QVERIFY(!expectedImage.isNull());
        const QColor expected = expectedImage.pixelColor(frameSize.width() / 2,
                                                          frameSize.height() / 2);
        QVERIFY(qAbs(expected.red() - expected.green()) <= 8);
        QVERIFY(qAbs(expected.green() - expected.blue()) <= 8);

        sink->setVideoFrame(frame);
        QTRY_VERIFY(frameSource->property("hasFrame").toBool());
        QTRY_VERIFY(effect->property("pixelAlignedIdentity").toBool());
        QCOMPARE(effect->property("requiredLevels").toInt(), 0);
        QCOMPARE(effect->property("selectedPixelSize").toSize(), frameSize);

        QTest::qWait(100);
        const QImage capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        const qreal dpr = view.devicePixelRatio();
        const QPointF origin = effect->mapToItem(view.contentItem(), QPointF());
        const QPoint sample(qRound(origin.x() * dpr) + frameSize.width() / 2,
                            qRound(origin.y() * dpr) + frameSize.height() / 2);
        const QColor actual = capture.pixelColor(sample);
        const QString diagnostic = QStringLiteral(
            "pixel-aligned video at 100% expected %1 from NV12, got %2")
            .arg(expected.name(QColor::HexArgb), actual.name(QColor::HexArgb));
        QVERIFY2(qAbs(actual.red() - expected.red()) <= 12
                 && qAbs(actual.green() - expected.green()) <= 12
                 && qAbs(actual.blue() - expected.blue()) <= 12,
                 qPrintable(diagnostic));
    }

    void videoFrameOwnsViewerOverLateContactSheetAt175Percent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString posterAPath = directory.filePath(QStringLiteral("poster-a.png"));
        const QString posterBPath = directory.filePath(QStringLiteral("poster-b.png"));
        const QString contactAPath = directory.filePath(QStringLiteral("contact-a.png"));
        const QString contactBPath = directory.filePath(QStringLiteral("contact-b.png"));
        const QString iconPath = directory.filePath(QStringLiteral("icon.png"));
        const QColor posterAColor(QStringLiteral("#174fc4"));
        const QColor posterBColor(QStringLiteral("#d59a18"));
        const QColor frameAColor(QStringLiteral("#2aaf58"));
        const QColor frameBColor(QStringLiteral("#6851cc"));
        const QColor backgroundColor(QStringLiteral("#18202a"));
        const QColor contactAColor(QStringLiteral("#c01830"));
        const QColor contactBColor(QStringLiteral("#d000d0"));

        QVERIFY(writeImage(posterAPath, QSize(1020, 425), posterAColor));
        QVERIFY(writeImage(posterBPath, QSize(1020, 425), posterBColor));
        QImage contactA(QSize(32, 24), QImage::Format_ARGB32_Premultiplied);
        QImage contactB(QSize(32, 24), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < contactA.height(); ++y) {
            for (int x = 0; x < contactA.width(); ++x) {
                contactA.setPixelColor(x, y,
                    x < contactA.width() / 2 && y < contactA.height() / 2
                        ? contactAColor : QColor(QStringLiteral("#a6c21b")));
                contactB.setPixelColor(x, y,
                    x < contactB.width() / 2 && y < contactB.height() / 2
                        ? contactBColor : QColor(QStringLiteral("#15a9b8")));
            }
        }
        QVERIFY(contactA.save(contactAPath));
        QVERIFY(contactB.save(contactBPath));
        QVERIFY(writeImage(iconPath, QSize(16, 16), Qt::white));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("initialContactSheet"), QUrl::fromLocalFile(contactAPath));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("videoControlIcon"), QUrl::fromLocalFile(iconPath));
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                id: root
                width: 320; height: 240
                property url contactSourceValue: initialContactSheet
                property url posterSourceValue: ""
                property string identityValue: "video-a"
                readonly property bool contactSheetReady:
                    viewport.viewerImageBase.status === Image.Ready
                Rectangle {
                    anchors.fill: parent
                    color: "#18202a"
                }
                FlickableZoomable {
                    id: viewport
                    objectName: "sharedGalleryViewerViewport"
                    anchors.fill: parent
                    active: true
                    devicePixelRatio: 1.75
                    checkerboardEnabled: false
                    videoMode: true
                    videoFrameSource: videoSurface.presentedFrameSource
                    videoPosterImage: videoSurface.posterImageSource
                    videoDisplayFailed: videoSurface.playbackFailed
                    onCloseRequested: root.closeRequests++
                    onMiddleClickRequested: root.fullscreenRequests++
                    Component.onCompleted:
                        setImage(root.contactSourceValue, Qt.size(0, 0), 0, 0)
                }
                QtObject {
                    id: videoController
                    objectName: "testVideoController"
                    property string state: "ready"
                    property bool playing: false
                    property bool muted: true
                    property real volume: 1
                    property int position: 0
                    property int duration: 10000
                    property string error: ""
                    property var outputSink: null
                    signal changed()
                    function setOutputSink(value) { outputSink = value }
                    function playPause() { playing = !playing; changed() }
                    function seekTo(value) { position = value; changed() }
                    function toggleMute() { muted = !muted; changed() }
                    function adjustVolume(value) {
                        volume = Math.max(0, Math.min(1, volume + value)); changed()
                    }
                    function publish() { changed() }
                }
                QtObject {
                    id: videoSession
                    property var videoPlaybackController: videoController
                }
                property int closeRequests: 0
                property int fullscreenRequests: 0
                GalleryVideoPlaybackSurface {
                    id: videoSurface
                    objectName: "videoSurfaceUnderTest"
                    anchors.fill: parent
                    controller: videoController
                    devicePixelRatio: 1.75
                    posterSource: root.posterSourceValue
                    videoIdentity: root.identityValue
                    previewVisible: viewport.imageTextureReady
                    foregroundColor: "#f3f4f6"
                    mutedColor: "#c7c9cc"
                    iconSources: ({
                        play: videoControlIcon,
                        pause: videoControlIcon,
                        muted: videoControlIcon,
                        sound: videoControlIcon
                    })
                }
                Connections {
                    target: videoSurface
                    function onDisplaySizeChanged() {
                        if (videoSurface.displaySize.width > 1
                                && videoSurface.displaySize.height > 1) {
                            viewport.sourceSizeFallbackPending = false
                            viewport.applyOriginalSize(Qt.size(
                                videoSurface.displaySize.width / 1.75,
                                videoSurface.displaySize.height / 1.75))
                        }
                    }
                    function onVideoIdentityChanged() {
                        Qt.callLater(() => {
                            if (videoSurface.displaySize.width > 1
                                    && videoSurface.displaySize.height > 1) {
                                viewport.sourceSizeFallbackPending = false
                                viewport.applyOriginalSize(Qt.size(
                                    videoSurface.displaySize.width / 1.75,
                                    videoSurface.displaySize.height / 1.75))
                            }
                        })
                    }
                }
                Item {
                    id: inputViewer
                    objectName: "inputViewer"
                    visible: false
                    property bool currentIsVideo: true
                    property var session: videoSession
                    property bool customContent: false
                    property bool zoomInPressed: false
                    property bool zoomOutPressed: false
                    property bool leftPressed: false
                    property bool rightPressed: false
                    property bool upPressed: false
                    property bool downPressed: false
                    property bool controlPressed: false
                    function ownsKey(event) { return true }
                    function updateHeldKeyMotion() {
                        const speed = controlPressed ? 0.06 : 1
                        viewport.startZoomScrollingAnimation(
                            leftPressed ? speed : rightPressed ? -speed : 0,
                            upPressed ? speed : downPressed ? -speed : 0,
                            zoomInPressed ? speed : zoomOutPressed ? -speed : 0)
                    }
                    function finishShiftSelection() {}
                }
                GalleryViewerInput {
                    id: videoInput
                    viewer: inputViewer
                    viewport: viewport
                }
                function dispatchVideoPress(key, modifiers) {
                    const event = { key: key, modifiers: modifiers,
                                    isAutoRepeat: false, accepted: false }
                    const alt = Boolean(modifiers & Qt.AltModifier)
                    const control = Boolean(modifiers & Qt.ControlModifier)
                    if (videoInput.handleVideoControlPressed(event, alt, control))
                        return "control"
                    if (videoInput.handleMotionPressed(event))
                        return "motion"
                    return "navigation"
                }
                function dispatchVideoRelease(key, modifiers) {
                    videoInput.handleReleased({ key: key, modifiers: modifiers,
                                                isAutoRepeat: false,
                                                accepted: false })
                }
            }
        )QML", QStringLiteral("VideoFrameOwnsViewerAt175.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        selectScreenAtDpr(view, 1.75);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        selectScreenAtDpr(view, 1.75);
        QCOMPARE(view.devicePixelRatio(), 1.75);
        const bool canRenderVideoFrame = supportsRhiVideoRendering(view);

        auto *surface = root->findChild<QQuickItem *>(
            QStringLiteral("videoSurfaceUnderTest"));
        auto *contactSheet = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerBaseImage"));
        auto *viewport = root->findChild<QQuickItem *>(
            QStringLiteral("sharedGalleryViewerViewport"));
        auto *videoEffect = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerImageShader"));
        auto *frameSource = root->findChild<QObject *>(
            QStringLiteral("galleryVideoFrameSource"));
        auto *controller = root->findChild<QObject *>(
            QStringLiteral("testVideoController"));
        QVERIFY(surface);
        QVERIFY(contactSheet);
        QVERIFY(viewport);
        QVERIFY(videoEffect);
        QVERIFY(frameSource);
        QVERIFY(controller);
        QTRY_VERIFY(root->property("contactSheetReady").toBool());
        auto *imageLayer = viewport->property("image").value<QObject *>();
        QVERIFY(imageLayer);
        const QSizeF originalSize = viewport->property("originalSize").toSizeF();
        const QSizeF contactSourceSize = contactSheet->property("sourceSize").toSizeF();
        const QString initialState = QStringLiteral(
            "textureReady=%1 base=%2x%3 sourceSize=%4x%5 pending=%6 source=%7 original=%8x%9 layer=%10x%11 opacity=%12 shader=%13x%14")
            .arg(viewport->property("imageTextureReady").toBool())
            .arg(contactSheet->property("implicitWidth").toReal())
            .arg(contactSheet->property("implicitHeight").toReal())
            .arg(contactSourceSize.width()).arg(contactSourceSize.height())
            .arg(viewport->property("sourceSizeFallbackPending").toBool())
            .arg(imageLayer->property("source").toUrl().toString())
            .arg(originalSize.width()).arg(originalSize.height())
            .arg(imageLayer->property("width").toReal())
            .arg(imageLayer->property("height").toReal())
            .arg(imageLayer->property("opacity").toReal())
            .arg(videoEffect->property("width").toReal())
            .arg(videoEffect->property("height").toReal());
        QVERIFY2(viewport->property("imageTextureReady").toBool(),
                 qPrintable(initialState));
        QVERIFY2(originalSize.width() > 1 && originalSize.height() > 1,
                 qPrintable(initialState));
        QVERIFY(!surface->property("displayReady").toBool());

        auto *statusText = root->findChild<QQuickItem *>(
            QStringLiteral("galleryVideoStatusText"));
        QVERIFY(statusText);
        QVERIFY(surface->property("previewVisible").toBool());
        QVERIFY(controller->setProperty("state", QStringLiteral("loading")));
        QVERIFY(QMetaObject::invokeMethod(controller, "publish"));
        QVERIFY(!statusText->property("visible").toBool());

        const QPointF videoPoint(80, 60);
        const QPointF letterboxPoint(160, 8);
        const qreal dpr = view.devicePixelRatio();
        const auto pixelAt = [&](const QImage &capture, const QPointF &logicalPoint) {
            const QPoint pixel(qRound(logicalPoint.x() * dpr),
                               qRound(logicalPoint.y() * dpr));
            return capture.pixelColor(pixel);
        };
        const auto colorMismatch = [](const QColor &actual, const QColor &expected,
                                      const QString &label) {
            if (qAbs(actual.red() - expected.red()) <= 12
                && qAbs(actual.green() - expected.green()) <= 12
                && qAbs(actual.blue() - expected.blue()) <= 12) {
                return QString();
            }
            return QStringLiteral("%1 expected %2 but got %3")
                .arg(label, expected.name(QColor::HexArgb),
                     actual.name(QColor::HexArgb));
        };

        QImage capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        QString mismatch = colorMismatch(pixelAt(capture, videoPoint), contactAColor,
                                         QStringLiteral("initial contact sheet"));
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch + QStringLiteral("; ")
                                                + initialState));

        QVERIFY(root->setProperty("posterSourceValue",
                                  QUrl::fromLocalFile(posterAPath)));
        QTRY_VERIFY(surface->property("displayReady").toBool());
        auto *posterImage = root->findChild<QQuickItem *>(
            QStringLiteral("galleryVideoPosterImage"));
        QVERIFY(posterImage);
        QTRY_VERIFY2(colorMismatch(pixelAt(view.grabWindow(), videoPoint),
                                   posterAColor,
                                   QStringLiteral("poster transition"))
                         .isEmpty(),
                     qPrintable(colorMismatch(pixelAt(view.grabWindow(), videoPoint),
                                              posterAColor,
                                              QStringLiteral("poster transition"))));
        const QSizeF posterFitSize(imageLayer->property("width").toReal(),
                                   imageLayer->property("height").toReal());

        auto *sink = qobject_cast<QVideoSink *>(
            frameSource->property("sink").value<QObject *>());
        QVERIFY(sink);
        const QVideoFrame decodedA = solidVideoFrame(QSize(1920, 800), frameAColor);
        QVERIFY(decodedA.isValid());
        sink->setVideoFrame(decodedA);
        QTRY_VERIFY(surface->property("hasDecodedFrame").toBool());
        const QSizeF frameFitSize(imageLayer->property("width").toReal(),
                                  imageLayer->property("height").toReal());
        QVERIFY2(qAbs(frameFitSize.width() - posterFitSize.width()) < 0.5
                     && qAbs(frameFitSize.height() - posterFitSize.height()) < 0.5,
                 qPrintable(QStringLiteral(
                     "poster fit %1x%2 changed to frame fit %3x%4")
                     .arg(posterFitSize.width()).arg(posterFitSize.height())
                     .arg(frameFitSize.width()).arg(frameFitSize.height())));
        if (canRenderVideoFrame) {
            QTRY_VERIFY2(colorMismatch(pixelAt(view.grabWindow(), videoPoint),
                                       frameAColor,
                                       QStringLiteral("first video frame"))
                             .isEmpty(),
                         qPrintable(colorMismatch(pixelAt(view.grabWindow(), videoPoint),
                                                  frameAColor,
                                                  QStringLiteral("first video frame"))));
        } else {
            QVERIFY(videoEffect->property("videoFrameSource").value<QObject *>()
                    == frameSource);
        }

        // A contact-sheet cache completion after the first decoded frame must
        // not reveal itself through the now-owned video viewport.
        const QUrl contactBUrl = QUrl::fromLocalFile(contactBPath);
        QVERIFY(contactSheet->setProperty("source", contactBUrl));
        QTRY_COMPARE(contactSheet->property("source").toUrl(), contactBUrl);
        QTRY_VERIFY(root->property("contactSheetReady").toBool());
        capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        if (canRenderVideoFrame) {
            mismatch = colorMismatch(pixelAt(capture, videoPoint), frameAColor,
                                     QStringLiteral("video after late contact sheet"));
            QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
        } else {
            QVERIFY(videoEffect->property("videoFrameSource").value<QObject *>()
                    == frameSource);
        }
        mismatch = colorMismatch(pixelAt(capture, letterboxPoint), backgroundColor,
                                 QStringLiteral("video letterbox background"));
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

        // Pausing and reaching the end retain the last valid decoded frame.
        QVERIFY(controller->setProperty("playing", false));
        QVERIFY(controller->setProperty("state", QStringLiteral("ended")));
        QVERIFY(QMetaObject::invokeMethod(controller, "publish"));
        QVERIFY(surface->property("hasDecodedFrame").toBool());
        capture = view.grabWindow();
        if (canRenderVideoFrame) {
            mismatch = colorMismatch(pixelAt(capture, videoPoint), frameAColor,
                                     QStringLiteral("retained frame after pause/end"));
            QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
        }

        // The previous frame remains in the test sink, but changing identity
        // makes it stale until a frame from the new source arrives.
        QVERIFY(root->setProperty("posterSourceValue",
                                  QUrl::fromLocalFile(posterBPath)));
        QVERIFY(root->setProperty("identityValue", QStringLiteral("video-b")));
        QVERIFY(!surface->property("hasDecodedFrame").toBool());
        QTRY_VERIFY(colorMismatch(pixelAt(view.grabWindow(), videoPoint),
                                  posterBColor, QStringLiteral("new-source poster"))
                        .isEmpty());

        const QVideoFrame decodedB = solidVideoFrame(QSize(1920, 800), frameBColor);
        QVERIFY(decodedB.isValid());
        sink->setVideoFrame(decodedB);
        QTRY_VERIFY(surface->property("hasDecodedFrame").toBool());
        if (canRenderVideoFrame) {
            QTRY_VERIFY(colorMismatch(pixelAt(view.grabWindow(), videoPoint),
                                      frameBColor, QStringLiteral("new-source frame"))
                            .isEmpty());
        } else {
            QVERIFY(videoEffect->property("videoFrameSource").value<QObject *>()
                    == frameSource);
        }

        // Video reuses the viewer's image layer and held-key transform. Plain
        // Plus zooms; Ctrl+Plus adjusts playback volume.
        QCOMPARE(viewport->property("viewerImageShader").value<QObject *>(),
                 static_cast<QObject *>(videoEffect));
        QVERIFY(videoEffect->property("videoFrameSource").value<QObject *>()
                == frameSource);
        QVariant dispatchResult;
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoPress", Q_RETURN_ARG(QVariant, dispatchResult),
            Q_ARG(QVariant, int(Qt::Key_Left)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        QCOMPARE(dispatchResult.toString(), QStringLiteral("navigation"));
        const qreal fitZoom = viewport->property("zoomScale").toReal();
        auto *keyViewer = root->findChild<QQuickItem *>(QStringLiteral("inputViewer"));
        auto *controlsTimeText = root->findChild<QQuickItem *>(
            QStringLiteral("galleryVideoTimeText"));
        QVERIFY(keyViewer);
        QVERIFY(controlsTimeText);
        const QPointF controlsOrigin = controlsTimeText->mapToItem(
            view.contentItem(), QPointF());
        const qreal controlsWidth = controlsTimeText->width();
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoPress", Q_RETURN_ARG(QVariant, dispatchResult),
            Q_ARG(QVariant, int(Qt::Key_Plus)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        QCOMPARE(dispatchResult.toString(), QStringLiteral("motion"));
        QVERIFY(keyViewer->property("zoomInPressed").toBool());
        QTest::qWait(150);
        QVERIFY(viewport->property("zoomScale").toReal() > fitZoom);
        QCOMPARE(controlsTimeText->width(), controlsWidth);
        QVERIFY(QLineF(controlsTimeText->mapToItem(view.contentItem(), QPointF()),
                       controlsOrigin).length() < 0.0001);
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoRelease", Q_ARG(QVariant, int(Qt::Key_Plus)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        QVERIFY(!keyViewer->property("zoomInPressed").toBool());
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoPress", Q_RETURN_ARG(QVariant, dispatchResult),
            Q_ARG(QVariant, int(Qt::Key_Left)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        QCOMPARE(dispatchResult.toString(), QStringLiteral("motion"));
        QVERIFY(keyViewer->property("leftPressed").toBool());
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoRelease", Q_ARG(QVariant, int(Qt::Key_Left)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        QVERIFY(!keyViewer->property("leftPressed").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewport, "zoomToFit",
                                          Q_ARG(QVariant, true)));

        QVERIFY(controller->setProperty("volume", 0.5));
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoPress", Q_RETURN_ARG(QVariant, dispatchResult),
            Q_ARG(QVariant, int(Qt::Key_Plus)),
            Q_ARG(QVariant, int(Qt::ControlModifier))));
        QCOMPARE(dispatchResult.toString(), QStringLiteral("control"));
        QVERIFY(!keyViewer->property("zoomInPressed").toBool());
        QVERIFY(qAbs(controller->property("volume").toReal() - 0.55) < 0.001);
        QVERIFY(QMetaObject::invokeMethod(
            root, "dispatchVideoRelease", Q_ARG(QVariant, int(Qt::Key_Plus)),
            Q_ARG(QVariant, int(Qt::ControlModifier))));

        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(80, 60));
        QCOMPARE(root->property("closeRequests").toInt(), 0);
        QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier,
                           QPoint(80, 60));
        QTRY_COMPARE(root->property("closeRequests").toInt(), 1);
        QTest::mouseClick(&view, Qt::MiddleButton, Qt::NoModifier,
                          QPoint(80, 60));
        QTRY_COMPARE(root->property("fullscreenRequests").toInt(), 1);
        QTRY_VERIFY(!viewport->property("viewportAnimationRunning").toBool());
        QTRY_VERIFY(!viewport->property("zoomScrollingAnimationRunning").toBool());

        // A failed new source owns the surface even without a poster/frame;
        // a contact-sheet update must not hide the error behind its fallback.
        QVERIFY(root->setProperty("posterSourceValue", QUrl()));
        QVERIFY(root->setProperty("identityValue", QStringLiteral("video-c")));
        QVERIFY(controller->setProperty("state", QStringLiteral("failed")));
        QVERIFY(controller->setProperty("error", QStringLiteral("decoder failure")));
        QVERIFY(QMetaObject::invokeMethod(controller, "publish"));
        QTRY_VERIFY(surface->property("displayReady").toBool());
        QTRY_VERIFY(statusText->property("visible").toBool());
        QCOMPARE(statusText->property("text").toString(),
                 QStringLiteral("decoder failure"));
        QVERIFY(!surface->property("hasDecodedFrame").toBool());
        QVERIFY(!viewport->property("imageTextureReady").toBool());
        capture = view.grabWindow();
        mismatch = colorMismatch(pixelAt(capture, letterboxPoint), backgroundColor,
                                 QStringLiteral("failed video with late contact sheet"));
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

        // Check the composed viewer leaves, including small text and raster
        // controls, in scene-space physical pixels with an identity transform.
        const QStringList visualLeaves{
            QStringLiteral("galleryViewerImageShader"),
            QStringLiteral("galleryVideoTimeText"),
            QStringLiteral("galleryVideoPlayButtonIcon"),
            QStringLiteral("galleryVideoMuteButtonIcon"),
            QStringLiteral("galleryVideoSeekTrack"),
            QStringLiteral("galleryVideoSeekProgress"),
            QStringLiteral("galleryVideoSeekHandle"),
            QStringLiteral("galleryVideoVolumeTrack"),
            QStringLiteral("galleryVideoVolumeProgress"),
            QStringLiteral("galleryVideoVolumeHandle"),
            QStringLiteral("galleryVideoStatusText"),
        };
        for (const QString &name : visualLeaves) {
            auto *item = root->findChild<QQuickItem *>(name);
            QVERIFY2(item, qPrintable(QStringLiteral("missing visual leaf %1").arg(name)));
            const QPointF origin = item->mapToItem(view.contentItem(), QPointF());
            const QPointF physicalOrigin = origin * dpr;
            const QString location = QStringLiteral(
                "%1 scene origin (%2, %3) physical px")
                .arg(name).arg(physicalOrigin.x(), 0, 'f', 6)
                .arg(physicalOrigin.y(), 0, 'f', 6);
            QVERIFY2(qAbs(physicalOrigin.x() - qRound(physicalOrigin.x())) < 0.001
                     && qAbs(physicalOrigin.y() - qRound(physicalOrigin.y())) < 0.001,
                     qPrintable(location));
            const QPointF dx = item->mapToItem(view.contentItem(), QPointF(1, 0))
                               - origin;
            const QPointF dy = item->mapToItem(view.contentItem(), QPointF(0, 1))
                               - origin;
            QVERIFY2(QLineF(dx, QPointF(1, 0)).length() < 0.0001
                     && QLineF(dy, QPointF(0, 1)).length() < 0.0001,
                     qPrintable(QStringLiteral("%1 has a non-identity scene transform")
                                    .arg(name)));
            QVERIFY2(qAbs(item->width() * dpr - qRound(item->width() * dpr)) < 0.001
                     && qAbs(item->height() * dpr - qRound(item->height() * dpr)) < 0.001,
                     qPrintable(QStringLiteral("%1 extent is off the physical pixel grid")
                                    .arg(name)));
        }
        QVERIFY(!capture.isNull());

        auto *playIcon = root->findChild<QQuickItem *>(
            QStringLiteral("galleryVideoPlayButtonIcon"));
        auto *muteIcon = root->findChild<QQuickItem *>(
            QStringLiteral("galleryVideoMuteButtonIcon"));
        auto *timeText = root->findChild<QQuickItem *>(
            QStringLiteral("galleryVideoTimeText"));
        QVERIFY(playIcon);
        QVERIFY(muteIcon);
        QVERIFY(timeText);
        QTRY_COMPARE(playIcon->property("status").toInt(), 1);
        QTRY_COMPARE(muteIcon->property("status").toInt(), 1);
        QCOMPARE(timeText->property("text").toString(),
                 QStringLiteral("00:00 / 00:10"));

        const auto itemCenterPixel = [&](QQuickItem *item) {
            const QPointF sceneCenter = item->mapToItem(
                view.contentItem(), QPointF(item->width() / 2, item->height() / 2));
            return QPoint(qRound(sceneCenter.x() * dpr),
                          qRound(sceneCenter.y() * dpr));
        };
        capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        const QColor playPixel = capture.pixelColor(itemCenterPixel(playIcon));
        const QColor mutePixel = capture.pixelColor(itemCenterPixel(muteIcon));
        QVERIFY2(playPixel.red() > 235 && playPixel.green() > 235
                     && playPixel.blue() > 235,
                 qPrintable(QStringLiteral("play icon rendered as %1")
                                .arg(playPixel.name(QColor::HexArgb))));
        QVERIFY2(mutePixel.red() > 235 && mutePixel.green() > 235
                     && mutePixel.blue() > 235,
                 qPrintable(QStringLiteral("mute icon rendered as %1")
                                .arg(mutePixel.name(QColor::HexArgb))));

        const QPointF timeOrigin = timeText->mapToItem(view.contentItem(), QPointF());
        const QRect timePixels(qRound(timeOrigin.x() * dpr),
                               qRound(timeOrigin.y() * dpr),
                               qRound(timeText->width() * dpr),
                               qRound(timeText->height() * dpr));
        int renderedTimePixels = 0;
        for (int y = timePixels.top(); y <= timePixels.bottom(); ++y) {
            for (int x = timePixels.left(); x <= timePixels.right(); ++x) {
                const QColor pixel = capture.pixelColor(x, y);
                if (pixel.red() > 160 && pixel.green() > 160
                    && pixel.blue() > 160) {
                    ++renderedTimePixels;
                }
            }
        }
        QVERIFY2(renderedTimePixels > 2,
                 "video time label did not render in the composed capture");

        const QPointF errorOrigin = statusText->mapToItem(view.contentItem(), QPointF());
        const QRect errorPixels(qRound(errorOrigin.x() * dpr),
                               qRound(errorOrigin.y() * dpr),
                               qRound(statusText->width() * dpr),
                               qRound(statusText->height() * dpr));
        int renderedErrorPixels = 0;
        for (int y = errorPixels.top(); y <= errorPixels.bottom(); ++y) {
            for (int x = errorPixels.left(); x <= errorPixels.right(); ++x) {
                const QColor pixel = capture.pixelColor(x, y);
                if (pixel.red() > 220 && pixel.green() > 220 && pixel.blue() > 220)
                    ++renderedErrorPixels;
            }
        }
        QVERIFY2(renderedErrorPixels > 2,
                 "video decoder error text did not render in the composed capture");

    }
#endif

    void detailsZoom_data() {
        QTest::addColumn<bool>("geometryOnly");
        QTest::addColumn<bool>("separateExtensions");
        QTest::addColumn<int>("iconPadding");
        QTest::newRow("zoom-combined") << false << false << 3;
        QTest::newRow("zoom-separated") << false << true << 3;
        QTest::newRow("pixel-grid-combined") << true << false << 3;
        QTest::newRow("pixel-grid-separated") << true << true << 3;
        QTest::newRow("padded-pixel-grid-combined") << true << false << 6;
        QTest::newRow("padded-pixel-grid-separated") << true << true << 6;
    }

    void detailsZoom() {
        QFETCH(bool, geometryOnly);
        QFETCH(bool, separateExtensions);
        QFETCH(int, iconPadding);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral(
            "A photograph with a descriptive filename and additional words.png"));
        QVERIFY(writeImage(path, QSize(80, 160), QColor("#3da5d9")));
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("details-zoom"));
        auto photo = imageEntry(QStringLiteral("photo"), 0, path);
        auto file = imageEntry(QStringLiteral("file"), 1, directory.filePath("document.txt"));
        file[QStringLiteral("isImage")] = false;
        file[QStringLiteral("name")] = QStringLiteral("VeryLongUnbrokenFilename").repeated(100) + ".txt";
        auto shortFile = imageEntry(QStringLiteral("short"), 2, directory.filePath("short.txt"));
        shortFile[QStringLiteral("isImage")] = false;
        shortFile[QStringLiteral("highlightStyle")] = QVariantMap{
            {QStringLiteral("icon"), QUrl::fromLocalFile(path).toString()}};
        QVERIFY(session->applyExternalCatalog({photo, file, shortFile}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("photo"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(QStringLiteral("zoomSession"), session);
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 460; height: 260
                property alias panel: panel
                Item {
                    id: offset
                    objectName: "offset"
                    x: 0.3; y: 0.7
                    width: 440; height: 240
                    GalleryPanel {
                        id: panel
                        objectName: "detailsZoomPanel"
                        anchors.fill: parent
                        session: zoomSession
                        devicePixelRatio: Window.window ? Window.window.devicePixelRatio : 1
                        presentationMode: "details"
                        showDetailsHeader: false
                        animateLayoutChanges: false
                        autoFocus: false
                        density: 22
                    }
                }
            }
        )QML", QStringLiteral("DetailsZoom.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        auto *panel = root->findChild<QQuickItem *>(QStringLiteral("detailsZoomPanel"));
        QVERIFY(panel);
        panel->setProperty("separateFileExtensions", separateExtensions);
        auto *metrics = panel->property("metrics").value<QObject *>();
        QVERIFY(metrics);
        QVERIFY(metrics->setProperty("detailsIconVerticalPadding", iconPadding));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.devicePixelRatio(), 1.75);
        auto leaf = [root](const QString &name) {
            return root->findChild<QQuickItem *>(name);
        };
        QTRY_VERIFY(leaf(QStringLiteral("galleryBaseName-0")));
        QTRY_VERIFY_WITH_TIMEOUT(leaf(QStringLiteral("galleryThumbnailImage-0")), 5000);
        QTRY_COMPARE(leaf(QStringLiteral("galleryThumbnailImage-0"))->property("status").toInt(), 1);
        QTRY_VERIFY(leaf(QStringLiteral("gallerySourceColorIcon-2")));
        QTRY_COMPARE(leaf(QStringLiteral("gallerySourceColorIcon-2"))->property("status").toInt(), 1);
        auto *name = leaf(QStringLiteral("galleryBaseName-0"));
        auto *slot = leaf(QStringLiteral("galleryDetailsIconSlot-0"));
        auto *icon = leaf(QStringLiteral("galleryFallbackIcon-1"));
        QVERIFY(slot);
        QVERIFY(icon);
        const qreal smallSlot = slot->height();
        const qreal smallIcon = icon->height();
        qInfo() << "[FIX:details-zoom] compact" << name->property("text")
                << name->width() << slot->height() << icon->height();
        QCOMPARE(name->property("lineCount").toInt(), 1);
        QVERIFY(name->property("truncated").toBool());

        for (const int height : {22, 28, 40, 72, 22}) {
            panel->setProperty("density", height);
            QTRY_COMPARE(panel->property("density").toInt(), height);
            QTest::qWait(150);
            QCOMPARE(icon->height(),
                     qRound((height - 2 * iconPadding) * 1.75) / 1.75);
            QCOMPARE(slot->width(), slot->height());
            const qreal slotPhysicalSize = slot->width() * 1.75;
            QVERIFY2(qAbs(slotPhysicalSize - qRound(slotPhysicalSize)) < 0.001,
                     qPrintable(QStringLiteral("details icon slot has fractional physical size %1")
                                    .arg(slotPhysicalSize, 0, 'f', 6)));
            auto *shortName = leaf(QStringLiteral("galleryBaseName-2"));
            QVERIFY(shortName);
            QCOMPARE(shortName->property("lineCount").toInt(), 1);
            QVERIFY(!shortName->property("truncated").toBool());
            for (int row = 0; row < 3; ++row)
                QVERIFY(leaf(QStringLiteral("galleryBaseName-%1").arg(row))->height() <= height - 3);
            if (!geometryOnly && height > 28) {
                QVERIFY2(slot->height() > smallSlot, "Details preview must grow with row height");
                QVERIFY2(name->property("lineCount").toInt() > 1, "Filename must use the extra row height");
                QVERIFY(leaf(QStringLiteral("galleryBaseName-1"))->property("truncated").toBool());
                if (height == 72)
                    QVERIFY(!name->property("truncated").toBool());
            }
            if (!geometryOnly && height == 22) {
                QCOMPARE(slot->height(), smallSlot);
                QCOMPARE(icon->height(), smallIcon);
                QCOMPARE(name->property("lineCount").toInt(), 1);
                QVERIFY(name->property("truncated").toBool());
            }
            if (geometryOnly) {
                for (const qreal offset : {0.3, 0.65}) {
                    root->findChild<QQuickItem *>(QStringLiteral("offset"))->setX(offset);
                    QTest::qWait(50);
                    QStringList leaves{QStringLiteral("galleryThumbnailImage-0"),
                                       QStringLiteral("galleryFallbackIcon-1"),
                                       QStringLiteral("gallerySourceColorIcon-2")};
                    for (int row = 0; row < 3; ++row) {
                        leaves << QStringLiteral("galleryBaseName-%1").arg(row)
                               << QStringLiteral("gallerySize-%1").arg(row);
                        if (separateExtensions)
                            leaves << QStringLiteral("galleryExtension-%1").arg(row);
                    }
                    for (const auto &id : leaves) {
                        auto *item = leaf(id);
                        QVERIFY2(item && item->isVisible(), qPrintable(id));
                        const QPointF origin = item->mapToItem(view.contentItem(), QPointF());
                        const QPointF physical = origin * view.devicePixelRatio();
                        const QString diagnostic = QStringLiteral("%1 height %2 physical (%3, %4)")
                            .arg(id).arg(height).arg(physical.x(), 0, 'f', 6).arg(physical.y(), 0, 'f', 6);
                        QVERIFY2(qAbs(physical.x() - qRound(physical.x())) < 0.001
                                 && qAbs(physical.y() - qRound(physical.y())) < 0.001,
                                 qPrintable(diagnostic));
                        const QPointF dx = item->mapToItem(view.contentItem(), QPointF(1, 0)) - origin;
                        const QPointF dy = item->mapToItem(view.contentItem(), QPointF(0, 1)) - origin;
                        QVERIFY(QLineF(dx, QPointF(1, 0)).length() < 0.0001);
                        QVERIFY(QLineF(dy, QPointF(0, 1)).length() < 0.0001);
                        QVERIFY2(qAbs(item->width() * view.devicePixelRatio()
                                      - qRound(item->width() * view.devicePixelRatio())) < 0.001,
                                 qPrintable(id + " fractional physical width"));
                        QVERIFY2(qAbs(item->height() * view.devicePixelRatio()
                                      - qRound(item->height() * view.devicePixelRatio())) < 0.001,
                                 qPrintable(id + " fractional physical height"));
                    }
                }
            }
            const QImage frame = view.grabWindow();
            QVERIFY(!frame.isNull());
            const QString captureDir = qEnvironmentVariable("F4_DETAILS_CAPTURE_DIR");
            if (!captureDir.isEmpty()) {
                QVERIFY(QDir().mkpath(captureDir));
                QVERIFY(frame.save(captureDir + QStringLiteral("/details-%1-%2.png")
                    .arg(separateExtensions ? "separated" : "combined").arg(height)));
            }
        }
        // Columns shares the enlarged row range, in both two- and three-column layouts.
        root->setProperty("height", 800);
        root->setProperty("width", 850);
        root->findChild<QQuickItem *>(QStringLiteral("offset"))->setHeight(780);
        root->findChild<QQuickItem *>(QStringLiteral("offset"))->setWidth(830);
        for (const QString &mode : {QStringLiteral("details"), QStringLiteral("columns")}) {
            panel->setProperty("presentationMode", mode);
            for (const int columns : {2, 3}) {
                panel->setProperty("columnCount", columns);
                panel->setProperty("density", 216);
                QTRY_COMPARE(panel->property("density").toInt(), 216);
                QTest::qWait(100); // Let the old mode delegate finish its deferred destruction.
                QTRY_VERIFY(leaf(QStringLiteral("galleryFallbackIcon-1")));
                QTRY_VERIFY(leaf(QStringLiteral("galleryFallbackIcon-1"))->height() > 180);
                QTRY_VERIFY(leaf(QStringLiteral("galleryBaseName-0")));
                QTRY_VERIFY(leaf(QStringLiteral("galleryThumbnailImage-0")));
                QTRY_COMPARE(leaf(QStringLiteral("galleryThumbnailImage-0"))->property("status").toInt(), 1);
                qInfo() << "[FIX:columns-wrap]" << mode << columns
                        << leaf(QStringLiteral("galleryBaseName-0"))->width()
                        << leaf(QStringLiteral("galleryBaseName-0"))->property("lineCount")
                        << leaf(QStringLiteral("galleryBaseName-0"))->property("maximumLineCount")
                        << leaf(QStringLiteral("galleryBaseName-0"))->property("wrapMode");
                if (mode == QStringLiteral("columns"))
                    QTRY_VERIFY2(leaf(QStringLiteral("galleryBaseName-0"))->property("lineCount").toInt() > 1,
                                 "Column names should wrap when rows are tall");
                auto *photoName = leaf(QStringLiteral("galleryBaseName-0"));
                auto *photoImage = leaf(QStringLiteral("galleryThumbnailImage-0"));
                auto *photoSlot = leaf(QStringLiteral("galleryThumbnail-0"));
                QVERIFY(photoSlot);
                const qreal imageLeft = photoImage->mapToItem(view.contentItem(), QPointF()).x();
                const qreal slotLeft = photoSlot->mapToItem(view.contentItem(), QPointF()).x();
                const qreal imageCenter = imageLeft + photoImage->width() / 2;
                const qreal slotCenter = slotLeft + photoSlot->width() / 2;
                qInfo() << "[FIX:thumbnail-center]" << mode << columns
                        << imageCenter << slotCenter;
                QVERIFY2(qAbs(imageCenter - slotCenter) <= 1 / view.devicePixelRatio(),
                         "Portrait thumbnail must be centered in its slot");
                auto *fileName = leaf(QStringLiteral("galleryBaseName-1"));
                const qreal photoTextX = photoName->mapToItem(view.contentItem(), QPointF()).x();
                const qreal fileTextX = fileName->mapToItem(view.contentItem(), QPointF()).x();
                qInfo() << "[FIX:column-alignment]" << mode << columns
                        << photoTextX << fileTextX;
                QVERIFY2(qAbs(photoTextX - fileTextX) <= 1 / view.devicePixelRatio(),
                         "Text must align across rows with and without thumbnails");
                QVERIFY(leaf(QStringLiteral("galleryBaseName-1"))->property("truncated").toBool());
                const qreal nameLeft = photoName->mapToItem(view.contentItem(), QPointF()).x();
                const qreal slotRight = slotLeft + photoSlot->width();
                const qreal slotGap = nameLeft - slotRight;
                qInfo() << "[FIX:columns-spacing]" << mode << columns << slotGap;
                QVERIFY2(slotGap >= 3 && slotGap <= 16,
                         "Thumbnail slot-to-name gutter must remain fixed at high zoom");
                if (mode == QStringLiteral("columns")) {
                    panel->setProperty("density", 22);
                    QTRY_COMPARE(panel->property("density").toInt(), 22);
                    QTRY_COMPARE(leaf(QStringLiteral("galleryBaseName-0"))->property("maximumLineCount").toInt(), 1);
                    QCOMPARE(leaf(QStringLiteral("galleryBaseName-0"))->property("lineCount").toInt(), 1);
                    QVERIFY(leaf(QStringLiteral("galleryBaseName-1"))->property("truncated").toBool());
                }
            }
        }
        runtime->shutdown();
    }

    void terminalViewerFailureStopsBusyIndicatorAndFrames() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("unsupported.png"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not an image"), qint64(12));
        file.close();

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-terminal-viewer-failure"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("unsupported"), 0, path)}, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("unsupported"), 0, {}, 1));
        session->setViewerOpen(true);

        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("terminalFailureSession"), session);
#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryViewer {
                objectName: "terminalFailureViewer"
                width: 640
                height: 420
                session: terminalFailureSession
                animationDuration: 1
            }
        )QML", QStringLiteral("GalleryTerminalViewerFailure.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();

        auto *viewer = rootObject;
        auto *busy = rootObject->findChild<QObject *>(
            QStringLiteral("galleryViewerBusyIndicator"));
        auto *failure = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerLoadFailure"));
        QVERIFY(viewer);
        QVERIFY(busy);
        QVERIFY(failure);

        QSignalSpy requestStateSpy(
            session, &ZoinGallery::GallerySession::viewerRequestStateAtChanged);
        QTRY_COMPARE_WITH_TIMEOUT(session->viewerRequestStateAt(0),
                                  QStringLiteral("failed"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentViewerRequestState").toString(),
            QStringLiteral("failed"), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!busy->property("running").toBool(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(failure->isVisible(), 1000);
        QVERIFY(!requestStateSpy.isEmpty());

        // A repeated request for the same immutable failed revision must not
        // fall back to a pending state just because there is still no decode
        // target. The resolved metadata outcome is authoritative.
        session->requestViewer(640, 420);
        QCOMPARE(session->viewerRequestStateAt(0),
                 QStringLiteral("failed"));

        // The Basic style fades a stopped BusyIndicator out for 250 ms.
        // Measure after that bounded presentation transition has settled.
        QTest::qWait(350);
        QSignalSpy settledFrames(&view, &QQuickWindow::frameSwapped);
        QVERIFY(settledFrames.isValid());
        QTest::qWait(160);
        QCOMPARE(settledFrames.size(), 0);

        runtime->shutdown();
    }

    void quickSearchCursorBlinkSettlesAndHiddenPanelStopsIt() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-bounded-quick-search-cursor"));
        QVERIFY(session);
        const QVariantMap folderEntry{
            {QStringLiteral("entryId"), QStringLiteral("folder")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("alpha-folder")},
            {QStringLiteral("localPath"), QDir::tempPath()},
            {QStringLiteral("isDir"), true},
            {QStringLiteral("isImage"), false},
            {QStringLiteral("selected"), false},
            {QStringLiteral("mtimeNs"), qint64(0)},
            {QStringLiteral("size"), qint64(0)},
        };
        QVERIFY(session->applyExternalCatalog({folderEntry}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("quickSearchSession"), session);

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                objectName: "quickSearchPanel"
                width: 500
                height: 320
                session: quickSearchSession
                localQuickSearchEnabled: true
                animateLayoutChanges: false
                function setSearch(value) {
                    return controller.setQuickSearchQuery(value)
                }
            }
        )QML", QStringLiteral("GalleryBoundedQuickSearchCursor.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(view.isActive(), 3000);
        QVERIFY(QMetaObject::invokeMethod(
            rootObject, "setSearch", Q_ARG(QVariant, QStringLiteral("alpha"))));

        auto *overlay = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryQuickSearchOverlay"));
        auto *cursor = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryQuickSearchCursor"));
        QVERIFY(overlay);
        QVERIFY(cursor);
        QTRY_VERIFY_WITH_TIMEOUT(overlay->isVisible(), 3000);
        QVERIFY(overlay->setProperty("blinkInterval", 20));
        QVERIFY(QMetaObject::invokeMethod(overlay, "restartBlink"));
        QTRY_VERIFY_WITH_TIMEOUT(
            overlay->property("blinkTimerRunning").toBool(), 500);
        QTRY_VERIFY_WITH_TIMEOUT(
            !overlay->property("blinkTimerRunning").toBool(), 500);
        QVERIFY(cursor->property("blinkOn").toBool());

        QTest::qWait(50);
        QSignalSpy settledFrames(&view, &QQuickWindow::frameSwapped);
        QVERIFY(settledFrames.isValid());
        QTest::qWait(120);
        QCOMPARE(settledFrames.size(), 0);

        QVERIFY(QMetaObject::invokeMethod(
            rootObject, "setSearch", Q_ARG(QVariant, QStringLiteral("folder"))));
        QTRY_VERIFY_WITH_TIMEOUT(
            overlay->property("blinkTimerRunning").toBool(), 500);
        rootObject->setProperty("visible", false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !overlay->property("blinkTimerRunning").toBool(), 500);
        QVERIFY(cursor->property("blinkOn").toBool());

        runtime->shutdown();
    }

    void autoScrollArmsWithoutAnimatingAndTracksTheDeadZone() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        QVERIFY(ZoinGallery::GalleryRuntime::install(view.engine()));

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 480
                height: 320
                readonly property var controller: controllerLoader.item

                QtObject {
                    id: fakePointer
                    objectName: "autoScrollFakePointer"
                    property real mouseX: 0
                    property real mouseY: 100
                }
                QtObject {
                    id: fakeLayout
                    objectName: "autoScrollFakeLayout"
                    property real contentY: 100
                    property bool scrollingMode: false
                    property int scrollingDirection: 99
                    property int clearCount: 0
                    function setScrollingMode(active, direction) {
                        scrollingMode = active
                        scrollingDirection = direction === undefined ? 0 : direction
                        if (!active)
                            clearCount++
                    }
                }
                Loader {
                    id: controllerLoader
                    active: true
                    sourceComponent: Component {
                        AutoScrollController {
                            objectName: "testedAutoScrollController"
                        }
                    }
                    onLoaded: {
                        item.layout = fakeLayout
                        item.pointerSource = fakePointer
                        item.scrollExtent = 320
                    }
                }
                function unloadController() { controllerLoader.active = false }
            }
        )QML", QStringLiteral("AutoScrollIdleLifecycle.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();

        QObject *controller = rootObject->findChild<QObject *>(
            QStringLiteral("testedAutoScrollController"));
        QObject *pointer = rootObject->findChild<QObject *>(
            QStringLiteral("autoScrollFakePointer"));
        QObject *layout = rootObject->findChild<QObject *>(
            QStringLiteral("autoScrollFakeLayout"));
        QVERIFY(controller);
        QVERIFY(pointer);
        QVERIFY(layout);

        QVERIFY(QMetaObject::invokeMethod(controller, "start"));
        QVERIFY(controller->property("scrollingMode").toBool());
        QVERIFY(!controller->property("animationRunning").toBool());
        QVERIFY(layout->property("scrollingMode").toBool());
        QCOMPARE(layout->property("scrollingDirection").toInt(), 0);
        QTest::qWait(50);
        QSignalSpy stationaryFrames(&view, &QQuickWindow::frameSwapped);
        QVERIFY(stationaryFrames.isValid());
        QTest::qWait(120);
        QCOMPARE(stationaryFrames.size(), 0);

        pointer->setProperty("mouseY", 180);
        QVERIFY(QMetaObject::invokeMethod(controller, "updatePointerMotion"));
        QTRY_VERIFY_WITH_TIMEOUT(
            controller->property("animationRunning").toBool(), 1000);
        QCOMPARE(layout->property("scrollingDirection").toInt(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(layout->property("contentY").toReal() > 100,
                                 1000);

        pointer->setProperty("mouseY", 110);
        QVERIFY(QMetaObject::invokeMethod(controller, "updatePointerMotion"));
        QTRY_VERIFY_WITH_TIMEOUT(
            !controller->property("animationRunning").toBool(), 1000);
        QVERIFY(controller->property("scrollingMode").toBool());
        QCOMPARE(layout->property("scrollingDirection").toInt(), 0);

        const int clearCount = layout->property("clearCount").toInt();
        QVERIFY(QMetaObject::invokeMethod(rootObject, "unloadController"));
        QTRY_VERIFY_WITH_TIMEOUT(
            rootObject->property("controller").value<QObject *>() == nullptr,
            1000);
        QVERIFY(layout->property("clearCount").toInt() > clearCount);
        QVERIFY(!layout->property("scrollingMode").toBool());
    }

    void panelLifecycleAlwaysClearsArmedAutoScroll() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("auto-scroll-panel-lifecycle"));
        QVERIFY(session);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("autoScrollSession"), session);

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 640
                height: 420
                GalleryPanel {
                    id: panel
                    objectName: "autoScrollLifecyclePanel"
                    anchors.fill: parent
                    session: autoScrollSession
                    showCursor: true
                    focus: true
                }
                FocusScope {
                    id: alternateFocus
                    objectName: "autoScrollAlternateFocus"
                }
                function clearPanelSession() { panel.session = null }
            }
        )QML", QStringLiteral("AutoScrollPanelLifecycle.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();

        auto *panel = rootObject->findChild<QQuickItem *>(
            QStringLiteral("autoScrollLifecyclePanel"));
        auto *alternateFocus = rootObject->findChild<QQuickItem *>(
            QStringLiteral("autoScrollAlternateFocus"));
        QObject *controller = rootObject->findChild<QObject *>(
            QStringLiteral("galleryMouseAutoScrollController"));
        QObject *middleButtonArea = rootObject->findChild<QObject *>(
            QStringLiteral("galleryMiddleButtonArea"));
        QVERIFY(panel);
        QVERIFY(alternateFocus);
        QVERIFY(controller);
        QVERIFY(middleButtonArea);

        const auto armMoving = [&]() {
            panel->forceActiveFocus();
            QTRY_VERIFY_WITH_TIMEOUT(panel->hasActiveFocus(), 1000);
            QVERIFY(QMetaObject::invokeMethod(controller, "start"));
            controller->setProperty("startCoordinate", -1000);
            QVERIFY(QMetaObject::invokeMethod(controller,
                                              "updatePointerMotion"));
            QTRY_VERIFY_WITH_TIMEOUT(
                controller->property("animationRunning").toBool(), 1000);
        };
        const auto verifyStopped = [&](const char *context) {
            QTRY_VERIFY2_WITH_TIMEOUT(
                !controller->property("scrollingMode").toBool(), context, 1000);
            QVERIFY(!controller->property("animationRunning").toBool());
        };

        // All panel presentations share this controller. Verify each one
        // can retain the armed neutral cursor without retaining a frame-loop.
        const QStringList modes{
            QStringLiteral("masonry"), QStringLiteral("grid"),
            QStringLiteral("icons"), QStringLiteral("details"),
            QStringLiteral("columns")};
        for (const QString &mode : modes) {
            panel->setProperty("presentationMode", mode);
            QVERIFY(QMetaObject::invokeMethod(controller, "start"));
            QVERIFY(controller->property("scrollingMode").toBool());
            QVERIFY(!controller->property("animationRunning").toBool());
            QVERIFY(QMetaObject::invokeMethod(controller, "end"));
            verifyStopped("explicit end did not clear auto-scroll");

            // Let bounded mode-change layout and scrollbar transitions finish,
            // then require a genuinely sleeping Qt Quick scene.
            QTest::qWait(250);
            QSignalSpy settledFrames(&view, &QQuickWindow::frameSwapped);
            QVERIFY(settledFrames.isValid());
            QTest::qWait(120);
            QCOMPARE(settledFrames.size(), 0);
        }

        armMoving();
        QVERIFY(QMetaObject::invokeMethod(middleButtonArea, "canceled"));
        verifyStopped("pointer cancel did not clear auto-scroll");

        // F4 retains this panel below documents, dialogs, and the image
        // viewer. showCursor=false is the host's explicit inactive/covered
        // lifecycle edge and must stop hidden frame work immediately.
        armMoving();
        panel->setProperty("showCursor", false);
        verifyStopped("inactive retained panel did not clear auto-scroll");

        panel->setProperty("showCursor", true);
        armMoving();
        alternateFocus->forceActiveFocus();
        verifyStopped("focus loss did not clear auto-scroll");

        armMoving();
        panel->setVisible(false);
        verifyStopped("visibility loss did not clear auto-scroll");

        panel->setVisible(true);
        armMoving();
        QVERIFY(QMetaObject::invokeMethod(rootObject, "clearPanelSession"));
        verifyStopped("session removal did not clear auto-scroll");
    }

    void managedPresentationKeepsSessionAndPreviewIndependent() {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.png");
        const auto second = directory.filePath("second.png");
        QVERIFY(writeImage(first, QSize(900, 600), Qt::cyan));
        QVERIFY(writeImage(second, QSize(600, 900), Qt::green));
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        auto *session = runtime->createExternalSession("managed-presentation");
        QVERIFY(session->applyExternalCatalog({imageEntry("first", 0, first), imageEntry("second", 1, second)}, 1));
        QVERIFY(session->applyExternalState("first", 0, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty("managedSession", session);
#ifndef Q_MOC_RUN
        QObject *viewer = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryViewer {
                width: 320; height: 400
                managedPresentation: true
                autoFocus: false
                session: managedSession
                previewEntryId: "second"
            }
        )QML", "ManagedPresentation.qml");
#else
        QObject *viewer = nullptr;
#endif
        QVERIFY(viewer);
        view.show();
        QTRY_COMPARE(viewer->property("transitionProgress").toReal(), 1.0);
        QTRY_COMPARE(viewer->property("presentedEntryId").toString(), QString("second"));
        QCOMPARE(session->cursorEntryId(), QString("first"));
        QVERIFY(!viewer->property("transitioning").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewer, "setPresentedIndex", Q_ARG(QVariant, 1), Q_ARG(QVariant, true)));
        QVERIFY(session->applyExternalState("second", 1, {}, 1));
        QTRY_COMPARE(viewer->property("pendingAuthorityEntryId").toString(), QString());
        QVERIFY(session->applyExternalState("first", 0, {}, 1));
        QCOMPARE(viewer->property("presentedEntryId").toString(), QString("second"));
        QSignalSpy closeIntent(viewer, SIGNAL(presentationCloseRequested()));
        QSignalSpy closed(viewer, SIGNAL(closeCompleted()));
        QVERIFY(QMetaObject::invokeMethod(viewer, "requestImmediateClose"));
        QCOMPARE(closeIntent.size(), 1);
        QCOMPARE(closed.size(), 0);
        QVERIFY(session->viewerOpen());
        QCOMPARE(viewer->property("transitionProgress").toReal(), 1.0);
        viewer->setProperty("previewEntryId", "");
        QTRY_COMPARE(viewer->property("presentedEntryId").toString(), QString("first"));
        runtime->shutdown();
    }

    void viewerFlightKeepsInnerViewportStable_data() {
        QTest::addColumn<QSize>("sourceSize");
        QTest::addColumn<QRectF>("tile");
        QTest::addColumn<int>("rotation");
        QTest::newRow("landscape") << QSize(1200, 800)
            << QRectF(36, 54, 140, 90) << 0;
        QTest::newRow("portrait-wide-tile") << QSize(800, 1200)
            << QRectF(23, 61, 170, 50) << 0;
        QTest::newRow("quarter-turn") << QSize(1200, 800)
            << QRectF(36, 54, 90, 140) << 1;
        QTest::newRow("odd-source") << QSize(1201, 799)
            << QRectF(23, 61, 170, 50) << 0;
    }

    void viewerFlightKeepsInnerViewportStable() {
        QFETCH(QSize, sourceSize);
        QFETCH(QRectF, tile);
        QFETCH(int, rotation);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath = directory.filePath("flight.png");
        QImage source(sourceSize, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::cyan);
        {
            QPainter paint(&source);
            paint.fillRect(0, 0, sourceSize.width() / 3, sourceSize.height(), Qt::magenta);
            paint.fillRect(sourceSize.width() / 2, 0, sourceSize.width() / 2,
                           sourceSize.height() / 4, Qt::yellow);
        }
        QVERIFY(source.save(imagePath));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession("qml-stable-flight");
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry("image", 7, imagePath)}, 1));
        QVERIFY(session->applyExternalState("image", 7, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty("flightSession", session);
        view.engine()->rootContext()->setContextProperty("flightTile", tile);
        view.engine()->rootContext()->setContextProperty("flightImage", QUrl::fromLocalFile(imagePath));
        view.engine()->rootContext()->setContextProperty("flightDpr", view.devicePixelRatio());

#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Rectangle {
                width: 640; height: 420; color: "#334455"
                Item {
                    id: panel
                    property bool viewerTransitionActive: false
                    property string viewerTransitionEntryId: ""
                    function currentItemImageGeometry(target) { return flightTile }
                    function currentItemImageSource() { return flightImage }
                }
                GalleryViewer {
                    objectName: "stableFlightViewer"
                    anchors.fill: parent
                    session: flightSession
                    sourcePanel: panel
                    animationDuration: 1
                    devicePixelRatio: flightDpr
                    theme: GalleryThemePalette { viewerBackground: "transparent" }
                }
            }
        )QML", QStringLiteral("StableViewerFlight.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        auto *viewer = root->findChild<QQuickItem *>("stableFlightViewer");
        auto *viewport = root->findChild<QQuickItem *>("galleryViewerViewport");
        auto *animation = root->findChild<QObject *>("galleryViewerTransitionAnimation");
        auto *shader = root->findChild<QQuickItem *>("galleryViewerImageShader");
        QVERIFY(viewer);
        QVERIFY(viewport);
        QVERIFY(animation);
        QVERIFY(shader);
        view.show();
        QTRY_VERIFY(view.isExposed());
        QTRY_VERIFY(viewport->property("imageTextureReady").toBool());
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        viewport->setProperty("rotationMode", rotation);
        QVERIFY(QMetaObject::invokeMethod(viewport, "zoomToFit", Q_ARG(QVariant, true)));
        auto *image = viewport->property("image").value<QQuickItem *>();
        QVERIFY(image);
        QTest::qWait(100);
        const QSizeF innerSize(viewport->width(), viewport->height());
        const qreal fittedZoom = viewport->property("zoomScale").toReal();
        const QImage before = view.grabWindow();
        QVERIFY(!before.isNull());
        // The software CTest run checks geometry; the hardware run must also
        // prove that the image shader actually drew, not compare blank grabs.
        if (view.rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
            QVERIFY(before.pixelColor(before.width() / 2, before.height() / 2)
                    != QColor("#334455"));
        }
        QSignalSpy widths(viewport, &QQuickItem::widthChanged);
        QSignalSpy heights(viewport, &QQuickItem::heightChanged);
        QSignalSpy zooms(viewport, SIGNAL(zoomScaleChanged()));
        QVERIFY(zooms.isValid());
        viewer->setProperty("animationDuration", 10000);
        QVERIFY(QMetaObject::invokeMethod(viewer, "beginOpen"));
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        const QSizeF effective = viewport->property("effectiveOriginalSize").toSizeF();
        QVERIFY(effective.width() > 1 && effective.height() > 1);
        for (const qreal progress : {0.0, 0.25, 0.65, 0.85, 0.65, 0.25, 0.0, 1.0}) {
            viewer->setProperty("transitionProgress", progress);
            QCOMPARE(QSizeF(viewport->width(), viewport->height()), innerSize);
            QCOMPARE(viewport->property("zoomScale").toReal(), fittedZoom);
            const QRectF virtualViewport(tile.x() * (1 - progress),
                tile.y() * (1 - progress),
                tile.width() * (1 - progress) + innerSize.width() * progress,
                tile.height() * (1 - progress) + innerSize.height() * progress);
            const qreal fit = qMin(virtualViewport.width() / effective.width(),
                                   virtualViewport.height() / effective.height());
            const QSizeF extent = effective * fit;
            const QRectF expected(virtualViewport.center() - QPointF(extent.width() / 2,
                extent.height() / 2), extent);
            const QRectF actual = image->mapRectToItem(viewer, image->boundingRect());
            QVERIFY2(QLineF(actual.topLeft(), expected.topLeft()).length() < 0.001
                && QLineF(actual.bottomRight(), expected.bottomRight()).length() < 0.001,
                qPrintable(QString("progress=%1 actual=%2,%3 %4x%5 expected=%6,%7 %8x%9")
                    .arg(progress).arg(actual.x()).arg(actual.y()).arg(actual.width()).arg(actual.height())
                    .arg(expected.x()).arg(expected.y()).arg(expected.width()).arg(expected.height())));
            if (progress < 1) {
                const QRectF rendered = shader->mapRectToItem(viewer, shader->boundingRect());
                const QString detail = QString("progress=%1 shader=%2,%3 %4x%5 expected=%6,%7 %8x%9")
                    .arg(progress).arg(rendered.x()).arg(rendered.y()).arg(rendered.width()).arg(rendered.height())
                    .arg(expected.x()).arg(expected.y()).arg(expected.width()).arg(expected.height());
                QVERIFY2(QLineF(rendered.topLeft(), expected.topLeft()).length() < 0.001,
                         qPrintable(detail));
                QVERIFY2(QLineF(rendered.bottomRight(), expected.bottomRight()).length() < 0.001,
                         qPrintable(detail));
            }
        }
        QCOMPARE(widths.size(), 0);
        QCOMPARE(heights.size(), 0);
        QCOMPARE(zooms.size(), 0);
        viewer->setProperty("transitionProgress", 0.65);
        const QRectF openingRect = image->mapRectToItem(viewer, image->boundingRect());
        QVERIFY(QMetaObject::invokeMethod(viewer, "requestClose"));
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        QCOMPARE(image->mapRectToItem(viewer, image->boundingRect()), openingRect);
        QCOMPARE(QSizeF(viewport->width(), viewport->height()), innerSize);
        QVERIFY(viewer->property("completingClose").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewer, "completeTransition"));
        QVERIFY(!viewer->property("viewerContentVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewer, "beginOpen"));
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        QVERIFY(QMetaObject::invokeMethod(viewer, "finishOpen"));
        QTest::qWait(100);
        const qreal dpr = view.devicePixelRatio();
        for (const QString &name : {QStringLiteral("galleryViewerImageShader"),
                                    QStringLiteral("galleryViewerCropShader")}) {
            auto *leaf = root->findChild<QQuickItem *>(name);
            QVERIFY(leaf);
            const QPointF origin = leaf->mapToScene(QPointF());
            const QString coordinates = QString("%1 physical origin=(%2,%3), DPR=%4")
                .arg(name).arg(origin.x() * dpr, 0, 'f', 6)
                .arg(origin.y() * dpr, 0, 'f', 6).arg(dpr);
            QVERIFY2(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.001,
                     qPrintable(coordinates));
            QVERIFY2(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.001,
                     qPrintable(coordinates));
            // Media rotation is deliberate; no extra flight scale/shear remains.
            const qreal angle = rotation * M_PI / 2;
            const QPointF basisX = leaf->mapToScene(QPointF(1, 0)) - origin;
            const QPointF basisY = leaf->mapToScene(QPointF(0, 1)) - origin;
            QVERIFY(QLineF(basisX, QPointF(std::cos(angle), std::sin(angle))).length() < 0.001);
            QVERIFY(QLineF(basisY, QPointF(-std::sin(angle), std::cos(angle))).length() < 0.001);
        }
        const QImage after = view.grabWindow();
        QCOMPARE(after, before);
        const QString captureDir = qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR");
        if (!captureDir.isEmpty())
            QVERIFY(after.save(QDir(captureDir).filePath(
                QString("flight-rest-%1.png").arg(QTest::currentDataTag()))));
        runtime->shutdown();
    }

    void interruptedViewerTransitionFinalizesExactlyOnce() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("transition.png"));
        QVERIFY(writeImage(imagePath, QSize(1200, 800), Qt::cyan));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-interrupted-transition"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("image"), 7, imagePath)}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("image"), 7, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("transitionSession"), session);

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 640
                height: 420
                property int closeCount: 0
                Item {
                    id: sourcePanel
                    width: 140
                    height: 90
                    property bool viewerTransitionActive: false
                    property string viewerTransitionEntryId: ""
                    function currentItemImageGeometry(targetItem) {
                        const point = targetItem.mapFromItem(sourcePanel, 0, 0)
                        return Qt.rect(point.x, point.y, width, height)
                    }
                    function currentItemImageSource() {
                        return "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
                    }
                }
                GalleryViewer {
                    id: viewer
                    objectName: "interruptedTransitionViewer"
                    anchors.fill: parent
                    session: transitionSession
                    sourcePanel: sourcePanel
                    theme: GalleryThemePalette {
                        viewerBackground: "transparent"
                    }
                    animationDuration: 300
                    onCloseCompleted: parent.closeCount++
                }
            }
        )QML", QStringLiteral("GalleryViewerInterruptedTransition.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();

        QObject *viewer = rootObject->findChild<QObject *>(
            QStringLiteral("interruptedTransitionViewer"));
        QObject *animation = rootObject->findChild<QObject *>(
            QStringLiteral("galleryViewerTransitionAnimation"));
        QVERIFY(viewer);
        QVERIFY(animation);
        const auto verifySampling = [&](bool enabled) {
            auto *viewport = viewer->property("flickableArea").value<QQuickItem *>();
            QVERIFY(viewport);
            QCOMPARE(viewport->property("hardwareSampling").toBool(), enabled);
            for (const QString &name : {QStringLiteral("galleryViewerImageShader"),
                                        QStringLiteral("galleryViewerCropShader")}) {
                auto *shader = rootObject->findChild<QQuickItem *>(name);
                QVERIFY(shader);
                QCOMPARE(shader->property("hardwareSampling").toBool(), enabled);
            }
        };
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1500);
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        verifySampling(false);

        QVERIFY(QMetaObject::invokeMethod(viewer, "beginOpen"));
        QTRY_VERIFY_WITH_TIMEOUT(animation->property("running").toBool(), 1000);
        verifySampling(true);
        viewer->setProperty("transitionProgress", 0.25);
        QCOMPARE(viewer->property("transitionProgress").toReal(), 0.25);
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        QVERIFY(viewer->property("transitioning").toBool());
        verifySampling(true);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        verifySampling(false);

        QVERIFY(QMetaObject::invokeMethod(viewer, "requestClose"));
        QTRY_VERIFY_WITH_TIMEOUT(animation->property("running").toBool(), 1000);
        verifySampling(true);
        viewer->setProperty("transitionProgress", 0.75);
        QCOMPARE(viewer->property("transitionProgress").toReal(), 0.75);
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        QVERIFY(viewer->property("transitioning").toBool());
        verifySampling(true);
        QTRY_COMPARE_WITH_TIMEOUT(rootObject->property("closeCount").toInt(),
                                  1, 1000);
        QCOMPARE(viewer->property("transitionProgress").toReal(), 0.0);
        QVERIFY(!viewer->property("viewerContentVisible").toBool());
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        verifySampling(false);
        QTest::qWait(500);
        QCOMPARE(rootObject->property("closeCount").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(viewer, "beginOpen"));
        QTRY_VERIFY(animation->property("running").toBool());
        verifySampling(true);
        QVERIFY(QMetaObject::invokeMethod(viewer, "requestImmediateClose"));
        QTRY_COMPARE(rootObject->property("closeCount").toInt(), 2);
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        verifySampling(false);
        QVERIFY(QMetaObject::invokeMethod(viewer, "beginOpen"));
        QTRY_VERIFY(animation->property("running").toBool());
        verifySampling(true);
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        verifySampling(false);
    }

    void viewerUsesOriginalHeldKeysPinchGeometryAndScrollBars() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("viewer-input.png"));
        QVERIFY(writeImage(imagePath, QSize(1600, 1000), Qt::magenta));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-original-viewer-input"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("image"), 7, imagePath),
             imageEntry(QStringLiteral("image-2"), 8, imagePath),
             imageEntry(QStringLiteral("image-3"), 9, imagePath)}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("image"), 7, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Rectangle {
                width: 640
                height: 420
                color: "#334455"
                property int closeCount: 0
                property rect viewerImageRect: viewer.currentViewerImageGeometry()
                function beginPinch(progress) {
                    viewer.updatePinchClose(progress)
                }
                function finishPinch(commit) {
                    viewer.finishPinchClose(commit)
                }
                function resetViewer() { viewer.resetView() }
                function closeOrdinary() { viewer.requestClose() }
                function reopenViewer() {
                    closeCount = 0
                    viewer.beginOpen()
                }
                function restoreFirst() {
                    viewer.setPresentedIndex(0, false)
                    viewer.resetView()
                }
                Rectangle {
                    id: sourcePanel
                    objectName: "sourcePanel"
                    anchors.fill: parent
                    color: "#334455"
                    opacity: 1
                    property bool viewerTransitionActive: false
                    property string viewerTransitionEntryId: ""
                    function currentItemImageGeometry(targetItem) {
                        const point = targetItem.mapFromItem(sourcePanel, 36, 54)
                        return Qt.rect(point.x, point.y, 120, 84)
                    }
                    function currentItemImageSource() {
                        return "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
                    }
                }
                GalleryViewer {
                    id: viewer
                    objectName: "viewer"
                    anchors.fill: parent
                    session: testSession
                    sourcePanel: sourcePanel
                    theme: GalleryThemePalette {
                        viewerBackground: "transparent"
                    }
                    animationDuration: 100
                    onCloseCompleted: parent.closeCount++
                }
            }
        )QML", QStringLiteral("GalleryViewerOriginalInput.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();

        auto *viewer = rootObject->findChild<QQuickItem *>(
            QStringLiteral("viewer"));
        auto *viewport = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerViewport"));
        auto *transitionFrame = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerTransitionFrame"));
        auto *viewerBackground = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerBackground"));
        auto *sourcePanel = rootObject->findChild<QQuickItem *>(
            QStringLiteral("sourcePanel"));
        auto *verticalScrollBar = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerVerticalScrollBar"));
        auto *horizontalScrollBar = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerHorizontalScrollBar"));
        QVERIFY(viewer);
        QVERIFY(viewport);
        QVERIFY(transitionFrame);
        QVERIFY(viewerBackground);
        QVERIFY(sourcePanel);
        QVERIFY(verticalScrollBar);
        QVERIFY(horizontalScrollBar);
        viewer->forceActiveFocus();
        QVERIFY(!viewer->property("nearestNeighbor").toBool());
        QTest::keyClick(&view, Qt::Key_F9);
        QTRY_VERIFY(viewer->property("nearestNeighbor").toBool());
        QTRY_VERIFY(viewport->property("nearestNeighbor").toBool());
        QTest::keyClick(&view, Qt::Key_F9);
        QTRY_VERIFY(!viewport->property("nearestNeighbor").toBool());
        QCOMPARE(viewerBackground->property("color").value<QColor>().alphaF(),
                 0.0);

        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        auto *baseImage = viewport->findChild<QObject *>(
            QStringLiteral("galleryViewerBaseImage"));
        auto *nativeImage = viewport->findChild<QObject *>(
            QStringLiteral("galleryViewerNativeImage"));
        QVERIFY(baseImage);
        QVERIFY(nativeImage);
        const auto textureDiagnostic = [&]() {
            return QStringLiteral(
                       "base(source=%1,status=%2,implicit=%3x%4,sourceSize=%5x%6) "
                       "native(source=%7,status=%8,implicit=%9x%10) "
                       "viewer(source=%11,level=%12,tiers=%13,original=%14x%15)")
                .arg(baseImage->property("source").toUrl().toString())
                .arg(baseImage->property("status").toInt())
                .arg(baseImage->property("implicitWidth").toReal())
                .arg(baseImage->property("implicitHeight").toReal())
                .arg(baseImage->property("sourceSize").toSize().width())
                .arg(baseImage->property("sourceSize").toSize().height())
                .arg(nativeImage->property("source").toUrl().toString())
                .arg(nativeImage->property("status").toInt())
                .arg(nativeImage->property("implicitWidth").toReal())
                .arg(nativeImage->property("implicitHeight").toReal())
                .arg(viewer->property("currentSourceValue").toUrl().toString())
                .arg(viewer->property("currentSourceLevelValue").toInt())
                .arg(session->viewerSourcesAt(0).size())
                .arg(session->imageOriginalSizeAt(0).width())
                .arg(session->imageOriginalSizeAt(0).height());
        };
        QTRY_VERIFY2_WITH_TIMEOUT(
            viewport->property("imageTextureReady").toBool(),
            qPrintable(textureDiagnostic()), 5000);
        viewer->forceActiveFocus();

        QTest::keyClick(&view, Qt::Key_F9);
        QTRY_VERIFY(viewer->property("nearestNeighbor").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(nativeImage->property("status").toInt(), 1, 5000);
        auto *imageShader = viewport->findChild<QObject *>(
            QStringLiteral("galleryViewerImageShader"));
        QVERIFY(imageShader);
        QTRY_COMPARE(imageShader->property("imageSource").value<QObject *>(), nativeImage);
        QCOMPARE(imageShader->property("requiredLevels").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_F9);
        QTRY_VERIFY(!viewer->property("nearestNeighbor").toBool());
        // A successfully decoded, fitted image is a steady presentation.
        // Decode completion and the opening transition may request bounded
        // frames, but the visible viewer must not retain an animation loop
        // once those operations settle.
        QTest::qWait(350);
        QSignalSpy idleViewerFrames(&view, &QQuickWindow::frameSwapped);
        QVERIFY(idleViewerFrames.isValid());
        QTest::qWait(160);
        QCOMPARE(idleViewerFrames.size(), 0);

        // The original continuous-motion branch does not consume arrow auto
        // repeats while Fit is active: each repeat moves another image.
        QSignalSpy navigationSpy(viewer,
                                 SIGNAL(navigationRequested(QString,int)));
        QKeyEvent firstRepeat(QEvent::KeyPress, Qt::Key_Right,
                              Qt::NoModifier, QString(), true, 1);
        QCoreApplication::sendEvent(&view, &firstRepeat);
        QKeyEvent secondRepeat(QEvent::KeyPress, Qt::Key_Right,
                               Qt::NoModifier, QString(), true, 1);
        QCoreApplication::sendEvent(&view, &secondRepeat);
        QTRY_COMPARE(navigationSpy.size(), 2);
        QCOMPARE(navigationSpy.at(0).at(0).toString(),
                 QStringLiteral("image-2"));
        QCOMPARE(navigationSpy.at(1).at(0).toString(),
                 QStringLiteral("image-3"));
        QVERIFY(QMetaObject::invokeMethod(rootObject, "restoreFirst"));
        QTRY_VERIFY(viewport->property("zoomFitView").toBool());

        // Ctrl-wheel is handled by FlickableZoomable's original MouseArea,
        // not the wrapper's fit-relative setZoom().  It can zoom below Fit.
        const qreal fitScale = viewport->property("zoomScale").toReal();
        const qreal targetScaleBeforeWheel =
            viewport->property("targetZoomScale").toReal();
        QTest::wheelEvent(&view, QPointF(320, 210), QPoint(0, -120), QPoint(),
                          Qt::ControlModifier);
        // ViewerWheelArea now dispatches explicitly and accepts the native
        // event. Check the exact original 120-step factor so a backend cannot
        // also deliver the ignored event to the lower MouseArea and zoom twice.
        const qreal expectedWheelTarget = targetScaleBeforeWheel / 1.32;
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(viewport->property("targetZoomScale").toReal()
                 - expectedWheelTarget) < 0.001,
            1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("zoomScale").toReal() < fitScale, 1000);
        QVERIFY(!viewport->property("zoomFitView").toBool());
        QVERIFY(QMetaObject::invokeMethod(rootObject, "resetViewer"));
        QTRY_VERIFY(viewport->property("zoomFitView").toBool());

        // A complete fitted arrow press/release remains catalog navigation.
        // The original ViewerMode gates its release-time motion update with
        // !zoomFitView; starting the zero-vector FrameAnimation here would
        // silently clear Fit and turn the next arrow into panning.
        QTest::keyClick(&view, Qt::Key_Right);
        QTRY_COMPARE(navigationSpy.size(), 3);
        QTRY_VERIFY(viewport->property("zoomFitView").toBool());
        QVERIFY(QMetaObject::invokeMethod(rootObject, "restoreFirst"));
        QTRY_VERIFY(viewport->property("zoomFitView").toBool());

        // ViewerMode starts one FrameAnimation on key-down and ignores native
        // key repeat.  Zoom therefore continues for the entire hold.
        QTest::keyPress(&view, Qt::Key_Plus, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("zoomScrollingAnimationRunning").toBool(),
            1000);
        const qreal firstZoom = viewport->property("zoomScale").toReal();
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("zoomScale").toReal() > firstZoom * 1.5,
            1000);
        const qreal secondZoom = viewport->property("zoomScale").toReal();
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("zoomScale").toReal() > secondZoom * 1.03,
            1000);
        QTest::keyRelease(&view, Qt::Key_Plus, Qt::ShiftModifier);
        QTRY_VERIFY(!viewport->property("zoomScrollingAnimationRunning").toBool());

        const qreal beforeMinus = viewport->property("zoomScale").toReal();
        QTest::keyPress(&view, Qt::Key_Minus);
        QTRY_VERIFY(viewport->property("zoomScrollingAnimationRunning").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("zoomScale").toReal() < beforeMinus * 0.85,
            1000);
        QTest::keyRelease(&view, Qt::Key_Minus);
        QTRY_VERIFY(!viewport->property("zoomScrollingAnimationRunning").toBool());

        QTRY_VERIFY(horizontalScrollBar->isVisible());
        QTRY_VERIFY(verticalScrollBar->isVisible());
        const qreal horizontalSize =
            horizontalScrollBar->property("size").toReal();
        const qreal verticalSize = verticalScrollBar->property("size").toReal();
        QVERIFY(horizontalSize > 0 && horizontalSize < 1);
        QVERIFY(verticalSize > 0 && verticalSize < 1);
        auto *viewerImage =
            viewport->property("image").value<QObject *>();
        QVERIFY(viewerImage);
        QVERIFY(qAbs(horizontalSize
                     - viewport->width()
                           / viewerImage->property("width").toReal()) < 0.01);

        // The original bars were siblings above the image and its pointer
        // area. A z value on a bar cannot escape an extracted parent layer.
        const qreal thumbPosition = verticalScrollBar->property("position").toReal();
        const QPoint thumbPoint = verticalScrollBar->mapToScene(QPointF(
            verticalScrollBar->width() / 2,
            verticalScrollBar->height() * (thumbPosition + verticalSize / 2))).toPoint();
        QTest::mouseMove(&view, thumbPoint);
        QTRY_VERIFY(verticalScrollBar->property("hovered").toBool());
        QTRY_COMPARE(verticalScrollBar->opacity(), 1.0);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, thumbPoint);
        const bool scrollbarReceivedPress = verticalScrollBar->property("pressed").toBool();
        const qreal beforeThumbDrag = viewerImage->property("y").toReal();
        const QPoint thumbDragPoint = thumbPoint + QPoint(0, thumbPosition < 0.1 ? 20 : -20);
        QTest::mouseMove(&view, thumbDragPoint, 30);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, thumbDragPoint);
        QVERIFY2(scrollbarReceivedPress, "Image pointer layer intercepted the scrollbar thumb");
        QVERIFY(qAbs(viewerImage->property("y").toReal() - beforeThumbDrag) > 1);
        if (!qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR").isEmpty()) {
            const QImage frame = view.grabWindow();
            QVERIFY(!frame.isNull());
            QVERIFY(frame.save(QDir(qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR"))
                                   .filePath(QStringLiteral("viewer-scrollbars.png"))));
        }
        QTest::mouseMove(&view, QPoint(320, 200));
        QTest::qWait(250);
        QCOMPARE(verticalScrollBar->opacity(), 1.0);
        QTRY_VERIFY_WITH_TIMEOUT(verticalScrollBar->opacity() < 1.0, 750);
        QTRY_COMPARE_WITH_TIMEOUT(verticalScrollBar->opacity(), 0.0, 1000);
        const QPoint horizontalThumbPoint = horizontalScrollBar->mapToScene(QPointF(
            horizontalScrollBar->width()
                * (horizontalScrollBar->property("position").toReal() + horizontalSize / 2),
            horizontalScrollBar->height() / 2)).toPoint();
        QTest::mouseMove(&view, horizontalThumbPoint);
        QTRY_COMPARE(horizontalScrollBar->opacity(), 1.0);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, horizontalThumbPoint);
        const bool horizontalReceivedPress = horizontalScrollBar->property("pressed").toBool();
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, horizontalThumbPoint);
        QVERIFY(horizontalReceivedPress);
        QTest::mouseMove(&view, QPoint(320, 200));

        viewer->setProperty("viewerNavigationCommitAfterAnimation", true);
        QVERIFY(!verticalScrollBar->isVisible());
        viewer->setProperty("viewerNavigationCommitAfterAnimation", false);
        viewer->setProperty("viewerNavigationOffsetX", 0.2);
        QVERIFY(!verticalScrollBar->isVisible());
        viewer->setProperty("viewerNavigationOffsetX", 0.0);
        QTRY_VERIFY(verticalScrollBar->isVisible());

        const qreal beforeX = viewerImage->property("x").toReal();
        const qreal minimumX = viewport->width()
                               - viewerImage->property("width").toReal();
        const Qt::Key motionKey = beforeX <= minimumX + 1
                                     ? Qt::Key_Left : Qt::Key_Right;
        QTest::keyPress(&view, motionKey);
        QTRY_VERIFY(viewport->property("zoomScrollingAnimationRunning").toBool());
        QTest::qWait(120);
        const qreal duringX = viewerImage->property("x").toReal();
        QVERIFY(qAbs(duringX - beforeX) > 0.5);
        QTest::keyRelease(&view, motionKey);
        QTRY_VERIFY(!viewport->property("zoomScrollingAnimationRunning").toBool());

        QVERIFY(QMetaObject::invokeMethod(rootObject, "resetViewer"));
        QTRY_VERIFY(viewport->property("zoomFitView").toBool());
        QTest::qWait(120);
        const QRectF initialImage =
            rootObject->property("viewerImageRect").toRectF();
        QVERIFY(initialImage.width() > 1);

        // Exercise real presses AND releases: releasing a discrete zoom key
        // must not cancel its pending target via the held-key motion handler.
        QTest::keyClick(&view, Qt::Key_Asterisk);
        QTRY_COMPARE(viewport->property("zoomScale").toReal(), 1.0);
        for (qreal expected : {0.75, 0.5, 0.375}) {
            QTest::keyClick(&view, Qt::Key_Minus, Qt::AltModifier);
            QCOMPARE(viewport->property("targetZoomScale").toReal(), expected);
            QTRY_COMPARE(viewport->property("zoomScale").toReal(), expected);
        }
        QTest::keyPress(&view, Qt::Key_Control);
        QTest::keyPress(&view, Qt::Key_Alt, Qt::ControlModifier);
        QTest::wheelEvent(&view, QPoint(320, 210), QPoint(0, -120), QPoint(),
                          Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(viewport->property("targetZoomScale").toReal(), 0.25);
        // Qt on Windows remaps Alt+vertical-wheel to angleDelta.x().
        QTest::wheelEvent(&view, QPoint(320, 210), QPoint(-120, 0), QPoint(),
                          Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(viewport->property("targetZoomScale").toReal(), 0.1875);
        QTest::keyRelease(&view, Qt::Key_Alt, Qt::ControlModifier);
        QTest::keyRelease(&view, Qt::Key_Control);
        QTRY_COMPARE(viewport->property("zoomScale").toReal(), 0.1875);
        QVERIFY(QMetaObject::invokeMethod(rootObject, "resetViewer"));
        QTest::qWait(120);

        // Ordinary close transforms this same image to the tile while its
        // internal viewport stays fixed; no thumbnail overlay cross-fades.
        QVERIFY(QMetaObject::invokeMethod(rootObject, "closeOrdinary"));
        QVERIFY(viewer->property("transitionHasGeometry").toBool());
        QCOMPARE(viewer->property("transitionSourceGeometry").toRectF().width(),
                 120.0);
        QTest::qWait(25);
        QCOMPARE(rootObject->property("closeCount").toInt(), 0);
        QCOMPARE(viewport->width(), 640.0);
        QCOMPARE(viewport->height(), 420.0);
        QTRY_VERIFY(viewport->mapRectToItem(viewer, viewport->boundingRect()).width() < 640);
        QVERIFY(viewport->mapRectToItem(viewer, viewport->boundingRect()).width() > 120);
        QTRY_COMPARE_WITH_TIMEOUT(rootObject->property("closeCount").toInt(),
                                  1, 1000);
        QVERIFY(!viewer->property("viewerContentVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(rootObject, "reopenViewer"));
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_VERIFY(viewer->property("viewerContentVisible").toBool());
        QTest::qWait(120);

        // A small image at 100% must not jump up to Fit before closing.
        viewer->setProperty("devicePixelRatio", 4.0);
        viewport->setProperty("originalSize", QSizeF(400, 250));
        QVERIFY(QMetaObject::invokeMethod(viewport, "zoomToFit", Q_ARG(QVariant, true)));
        QVERIFY(viewport->property("zoomScale").toReal() > 1.0);
        QTest::keyClick(&view, Qt::Key_Asterisk);
        QTRY_COMPARE(viewport->property("zoomScale").toReal(), 1.0);
        QTest::qWait(120);
        const QRectF nativeImageRect = rootObject->property("viewerImageRect").toRectF();
        QTest::keyClick(&view, Qt::Key_Return);
        const QRectF closeStart = rootObject->property("viewerImageRect").toRectF();
        QVERIFY2(qAbs(closeStart.width() - nativeImageRect.width()) < 1,
                 qPrintable(QString("close jumped from %1 to %2").arg(nativeImageRect.width()).arg(closeStart.width())));
        QTest::qWait(25);
        QVERIFY(rootObject->property("viewerImageRect").toRectF().width() <= nativeImageRect.width());
        QTRY_COMPARE_WITH_TIMEOUT(rootObject->property("closeCount").toInt(), 1, 1000);
        viewer->setProperty("devicePixelRatio", 1.0);
        viewport->setProperty("originalSize", QSizeF(1600, 1000));
        QVERIFY(QMetaObject::invokeMethod(rootObject, "reopenViewer"));
        QTRY_COMPARE(viewer->property("transitionProgress").toReal(), 1.0);
        QTest::qWait(120);

        QVERIFY(QMetaObject::invokeMethod(rootObject, "beginPinch",
                                          Q_ARG(QVariant, 0.5)));
        QVERIFY(viewer->property("pinchCloseActive").toBool());
        QCOMPARE(transitionFrame->x(), 0.0);
        QCOMPARE(transitionFrame->y(), 0.0);
        QCOMPARE(transitionFrame->width(), 640.0);
        QCOMPARE(transitionFrame->height(), 420.0);
        QCOMPARE(viewport->x(), 0.0);
        QCOMPARE(viewport->y(), 0.0);
        QCOMPARE(viewport->width(), 640.0);
        QCOMPARE(viewport->height(), 420.0);

        const QRectF start =
            viewer->property("pinchCloseStartGeometry").toRectF();
        const QRectF target =
            viewer->property("pinchCloseTargetGeometry").toRectF();
        const QRectF halfway =
            rootObject->property("viewerImageRect").toRectF();
        const qreal eased = std::sin(std::acos(-1.0) / 4.0);
        QVERIFY(qAbs(halfway.x()
                     - (start.x() + (target.x() - start.x()) * eased)) < 1.0);
        QVERIFY(qAbs(halfway.y()
                     - (start.y() + (target.y() - start.y()) * eased)) < 1.0);
        QVERIFY(qAbs(halfway.width()
                     - (start.width()
                        + (target.width() - start.width()) * eased)) < 1.0);
        QCOMPARE(viewerBackground->property("opacity").toReal(), 0.5);
        const qreal viewerOpacity =
            viewerBackground->property("opacity").toReal();
        const qreal panelOpacity = sourcePanel->property("opacity").toReal();
        QCOMPARE(1 - (1 - viewerOpacity) * (1 - panelOpacity), 1.0);

        QVERIFY(QMetaObject::invokeMethod(rootObject, "finishPinch",
                                          Q_ARG(QVariant, false)));
        QVERIFY(!viewer->property("pinchCloseActive").toBool());
        QTRY_VERIFY(viewport->property("zoomFitView").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(rootObject->property("viewerImageRect").toRectF().x()
                 - initialImage.x()) < 1.0,
            1000);
        QCOMPARE(rootObject->property("closeCount").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(rootObject, "beginPinch",
                                          Q_ARG(QVariant, 0.5)));
        QVERIFY(QMetaObject::invokeMethod(rootObject, "finishPinch",
                                          Q_ARG(QVariant, true)));
        QCOMPARE(rootObject->property("closeCount").toInt(), 0);
        QVERIFY(viewer->property("viewerContentVisible").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(rootObject->property("closeCount").toInt(),
                                  1, 1000);
        QVERIFY(!viewer->property("viewerContentVisible").toBool());

        runtime->shutdown();
    }

    void panelRestoresOriginalMasonryScrollBarContract() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("scroll-tile.png"));
        QVERIFY(writeImage(imagePath, QSize(320, 240), Qt::cyan));

        QVariantList entries;
        for (int index = 0; index < 30; ++index) {
            entries.push_back(imageEntry(QStringLiteral("entry-%1").arg(index),
                                         index, imagePath));
        }

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-panel-scrollbar"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(entries, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("entry-0"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                objectName: "panel"
                width: 320
                height: 220
                thumbnailHeight: 90
                session: testSession
            }
        )QML", QStringLiteral("GalleryPanelScrollbar.qml"));
        QVERIFY(rootObject);
        view.show();

        auto *layout = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewportItem"));
        auto *scrollBar = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryPanelScrollBar"));
        QVERIFY(layout);
        QVERIFY(scrollBar);
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 5000);
        const qreal contentHeight = layout->property("contentHeight").toReal();
        QVERIFY(contentHeight > layout->height());
        QTRY_VERIFY(qAbs(scrollBar->property("size").toReal()
                         - layout->height() / contentHeight) < 0.01);

        layout->setProperty("contentY", contentHeight * 0.35);
        QTRY_VERIFY(qAbs(scrollBar->property("position").toReal()
                         - layout->property("contentY").toReal()
                               / contentHeight) < 0.01);

        layout->setProperty("contentY", 0);
        QTRY_VERIFY(scrollBar->property("position").toReal() < 0.01);
        const qreal thumbCenterY = scrollBar->height()
                                   * scrollBar->property("size").toReal() / 2;
        const QPoint pressPoint =
            scrollBar->mapToScene(QPointF(scrollBar->width() / 2,
                                          thumbCenterY)).toPoint();
        const QPoint dragPoint =
            scrollBar->mapToScene(QPointF(scrollBar->width() / 2,
                                          scrollBar->height() * 0.7)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, pressPoint);
        QTest::mouseMove(&view, dragPoint, 50);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, dragPoint);
        QTRY_VERIFY(layout->property("contentY").toReal() > 1);
        QTRY_VERIFY(session->panelScrollOffset() > 1);

        runtime->shutdown();
    }

    void panelKeepsVerticalInsetsInsideScrollableTileContent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("content-padding-tile.png"));
        QVERIFY(writeImage(imagePath, QSize(320, 240), Qt::cyan));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-panel-content-padding"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("entry-0"), 0, imagePath)}, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("entry-0"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                objectName: "panel"
                width: 320
                height: 220
                thumbnailHeight: 90
                session: testSession
            }
        )QML", QStringLiteral("GalleryPanelContentPadding.qml"));
        QVERIFY(rootObject);
        view.show();

        auto *panel = qobject_cast<QQuickItem *>(rootObject);
        auto *layout = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(panel);
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->property("count").toInt(), 1, 5000);

        constexpr qreal inset = 6.0;
        const QStringList modes{
            QStringLiteral("masonry"),
            QStringLiteral("grid"),
            QStringLiteral("icons"),
        };
        for (const QString &mode : modes) {
            rootObject->setProperty("presentationMode", mode);
            layout->setProperty("contentY", 0.0);

            QTRY_VERIFY_WITH_TIMEOUT(qAbs(layout->y()) < 0.01, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(
                qAbs(layout->height() - panel->height()) < 0.01, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(
                layout->property("paddingTop").toReal(), inset, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(
                layout->property("paddingBottom").toReal(), inset, 3000);

            const auto activeTile = [&]() -> QQuickItem * {
                const auto pointers = rootObject->findChildren<QQuickItem *>(
                    QStringLiteral("gallerySelectionSurface-0"),
                    Qt::FindChildrenRecursively);
                for (QQuickItem *pointer : pointers) {
                    QQuickItem *const candidate = pointer
                        ? pointer->parentItem() : nullptr;
                    if (candidate && candidate->isVisible()
                        && candidate->property("mode").toString() == mode) {
                        return candidate;
                    }
                }
                return nullptr;
            };
            QQuickItem *tile = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (tile = activeTile()) != nullptr,
                5000);
            QTRY_VERIFY_WITH_TIMEOUT(qAbs(tile->y() - inset) < 0.01, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(tile->height() > 0.0, 3000);

            const qreal tileBottom = tile->y() + tile->height();
            const qreal contentHeight =
                layout->property("contentHeight").toReal();
            QVERIFY2(qAbs(contentHeight - tileBottom - inset) < 1.01,
                     qPrintable(QStringLiteral(
                         "%1 content bottom spacing is %2, expected %3")
                         .arg(mode)
                         .arg(contentHeight - tileBottom)
                         .arg(inset)));
        }

        // Columns retains its existing viewport-margin contract.
        rootObject->setProperty("presentationMode", QStringLiteral("columns"));
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(layout->y() - inset) < 0.01, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->height() - (panel->height() - inset * 2)) < 0.01,
            3000);
        QTRY_COMPARE_WITH_TIMEOUT(
            layout->property("paddingTop").toReal(), 0.0, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(
            layout->property("paddingBottom").toReal(), 0.0, 3000);

        runtime->shutdown();
    }

    void rightDragSelectionStartsInEmptyPanelSpace_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<bool>("initiallySelected");
        for (const auto &mode : {QString("details"), QString("columns"), QString("icons"), QString("grid"), QString("masonry")}) {
            QTest::newRow(qPrintable(mode + "-add")) << mode << false;
            QTest::newRow(qPrintable(mode + "-remove")) << mode << true;
        }
    }

    void rightDragSelectionStartsInEmptyPanelSpace() {
        QFETCH(QString, mode);
        QFETCH(bool, initiallySelected);
        QVariantList entries;
        for (int index = 0; index < 4; ++index)
            entries.append(QVariantMap{{"entryId", QString("entry-%1").arg(index)},
                {"index", index}, {"name", QString("folder-%1").arg(index)},
                {"isDir", true}, {"isImage", false}});
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession("empty-space-drag");
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(entries, 1));
        QStringList selected{"entry-3"};
        if (initiallySelected)
            selected.append({"entry-1", "entry-2"});
        QVERIFY(session->applyExternalState("entry-0", 0, selected, 1));
        view.engine()->rootContext()->setContextProperty("testSession", session);
        auto *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 360; height: 720
                thumbnailHeight: 55
                liveSelectionUpdates: true
                showDetailsHeader: false
                session: testSession
            }
        )QML", "EmptySpaceRightDrag.qml");
        QVERIFY(root);
        QVERIFY(root->setProperty("presentationMode", mode));
        view.show();
        auto *layout = root->findChild<QQuickItem *>("galleryViewportItem");
        QVERIFY(layout);
        QTRY_COMPARE(layout->property("count").toInt(), 4);
        QTest::qWait(80);
        auto indexAt = [layout](QPointF point) {
            int index = -2;
            QMetaObject::invokeMethod(layout, "indexAtViewport", Q_RETURN_ARG(int, index),
                Q_ARG(qreal, point.x()), Q_ARG(qreal, point.y()));
            return index;
        };
        const QPointF emptyLocal(layout->width()/2, layout->height()-20);
        QCOMPARE(indexAt(emptyLocal), -1);
        const auto empty = layout->mapToScene(emptyLocal).toPoint();
        QSignalSpy requests(root, SIGNAL(selectionTransactionRequested(QVariant,QString,int)));
        QVERIFY(requests.isValid());
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
        QCOMPARE(requests.size(), 0);
        QVERIFY(!root->property("dragCursorActive").toBool());
        QCOMPARE(session->currentIndex(), 0);
        QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, empty);
        QVERIFY(root->property("dragCursorActive").toBool());
        QCOMPARE(requests.size(), 0);
        auto effectiveSelected = [root, session](int index) {
            QVariant result;
            QMetaObject::invokeMethod(root, "effectiveEntrySelected", Q_RETURN_ARG(QVariant, result),
                Q_ARG(QVariant, QString("entry-%1").arg(index)),
                Q_ARG(QVariant, session->isSelectedAt(index)));
            return result.toBool();
        };
        QPoint target;
        for (int index : {1, 2}) {
            QRectF geometry;
            QVERIFY(QMetaObject::invokeMethod(layout, "indexGeometry", Q_RETURN_ARG(QRectF, geometry), Q_ARG(int, index)));
            const auto local = geometry.center() - QPointF(layout->property("contentX").toReal(), layout->property("contentY").toReal());
            QCOMPARE(indexAt(local), index);
            target = layout->mapToScene(local).toPoint();
            QTest::mouseMove(&view, target);
            QTRY_COMPARE(effectiveSelected(index), !initiallySelected);
        }
        QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, target);
        QVERIFY(!root->property("dragCursorActive").toBool());
        QVERIFY(!root->property("keyboardShiftSelectionActive").toBool());
        QVERIFY(!requests.isEmpty());
        QCOMPARE(effectiveSelected(0), false);
        QCOMPARE(effectiveSelected(1), !initiallySelected);
        QCOMPARE(effectiveSelected(2), !initiallySelected);
        QCOMPARE(effectiveSelected(3), true);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, empty);
        QVERIFY(!root->property("dragCursorActive").toBool());
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, empty);
        runtime->shutdown();
    }

    void panelPointerSelectionRevealsPartiallyVisibleTile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("pointer-reveal.png"));
        QVERIFY(writeImage(imagePath, QSize(320, 240), Qt::yellow));

        QVariantList entries;
        for (int index = 0; index < 30; ++index) {
            entries.push_back(imageEntry(QStringLiteral("entry-%1").arg(index),
                                         index, imagePath));
        }

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-panel-pointer-reveal"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(entries, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("entry-0"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 320
                height: 220
                thumbnailHeight: 90
                session: testSession
            }
        )QML", QStringLiteral("GalleryPanelPointerReveal.qml"));
        QVERIFY(rootObject);
        view.show();

        auto *layout = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->property("count").toInt(), 30, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->property("contentHeight").toReal() > layout->height(), 5000);
        // Every logical entry deliberately references the same physical test
        // image. Wait for the shared metadata result to fan out before
        // capturing geometry; otherwise the legitimate masonry rewrap can
        // race the pointer-reveal assertion below.
        QTRY_COMPARE_WITH_TIMEOUT(
            session->imageOriginalSizeAt(0), QSize(320, 240), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            session->imageOriginalSizeAt(29), QSize(320, 240), 5000);

        constexpr int targetIndex = 15;
        QRectF geometry;
        QVERIFY(QMetaObject::invokeMethod(
            layout, "indexGeometry", Q_RETURN_ARG(QRectF, geometry),
            Q_ARG(int, targetIndex)));
        QVERIFY(geometry.isValid());
        QVERIFY(geometry.y() > layout->height());

        // Leave only the top edge of the tile visible at the bottom, then
        // exercise the same QML method called by the delegate's MouseArea.
        const qreal partlyBelow = geometry.y() - layout->height() + 4;
        layout->setProperty("contentY", partlyBelow);
        QVERIFY(QMetaObject::invokeMethod(
            rootObject, "handlePointerPress",
            Q_ARG(QVariant, targetIndex),
            Q_ARG(QVariant, int(Qt::LeftButton)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        const qreal expectedBelow = geometry.y() + geometry.height()
                                    - layout->height();
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->property("contentY").toReal() - expectedBelow) < 1.0,
            1000);

        // And the inverse: leave only its bottom edge visible at the top.
        layout->setProperty("contentY", geometry.y() + geometry.height() - 4);
        QVERIFY(QMetaObject::invokeMethod(
            rootObject, "handlePointerPress",
            Q_ARG(QVariant, targetIndex),
            Q_ARG(QVariant, int(Qt::LeftButton)),
            Q_ARG(QVariant, int(Qt::NoModifier))));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->property("contentY").toReal() - geometry.y()) < 1.0,
            1000);

        runtime->shutdown();
    }

    void viewerOwnsKeysNavigatesByStableIdentityAndClosesInTwoPhases() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString firstPath = directory.filePath(QStringLiteral("first.png"));
        const QString secondPath = directory.filePath(QStringLiteral("second.png"));
        QVERIFY(writeImage(firstPath, QSize(800, 500), Qt::red));
        QVERIFY(writeImage(secondPath, QSize(500, 800), Qt::blue));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("qml-viewer"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("first"), 11, firstPath),
             imageEntry(QStringLiteral("second"), 22, secondPath)}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("first"), 11, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 640
                height: 420
                property int closeCompletedCount: 0
                property int closeAliasCount: 0
                property int bubbledKeyCount: 0
                property bool ownsEnter: viewer.ownsKey({ key: Qt.Key_Enter,
                                                          modifiers: Qt.NoModifier })
                property bool ownsEscape: viewer.ownsKey({ key: Qt.Key_Escape,
                                                           modifiers: Qt.NoModifier })
                property bool ownsCtrlPlus: viewer.ownsKey({ key: Qt.Key_Plus,
                                                             modifiers: Qt.ControlModifier })
                property bool ownsCtrlF: viewer.ownsKey({ key: Qt.Key_F,
                                                          modifiers: Qt.ControlModifier })
                property bool ownsModifiedEnter: viewer.ownsKey({ key: Qt.Key_Enter,
                                                                  modifiers: Qt.AltModifier })
                Keys.onPressed: event => { bubbledKeyCount++ }
                function zoomForSwipe() { viewer.setZoom(2) }
                property point committedPosition
                function commitNextSwipe() {
                    viewer.beginViewerNavigation(1)
                    committedPosition = Qt.point(viewer.viewerNavigationTargetFinalImageX,
                                                 viewer.viewerNavigationTargetFinalImageY)
                    viewer.commitViewerNavigation()
                }
                function closeNow() { viewer.requestClose() }
                Item {
                    id: sourcePanel
                    objectName: "sourcePanel"
                    x: 12
                    y: 18
                    width: 260
                    height: 330
                    property bool viewerTransitionActive: false
                    property string viewerTransitionEntryId: ""
                    function currentItemImageGeometry(targetItem) {
                        const point = targetItem.mapFromItem(sourcePanel, 24, 36)
                        return Qt.rect(point.x, point.y, 120, 84)
                    }
                    function currentItemImageSource() {
                        return "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
                    }
                }
                GalleryViewer {
                    id: viewer
                    objectName: "viewer"
                    anchors.fill: parent
                    session: testSession
                    sourcePanel: sourcePanel
                    animationDuration: 80
                    onCloseCompleted: parent.closeCompletedCount++
                    onCloseRequested: parent.closeAliasCount++
                }
            }
        )QML", QStringLiteral("GalleryViewerContract.qml"));
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();

        auto *viewer = rootObject->findChild<QObject *>(QStringLiteral("viewer"));
        auto *sourcePanel = rootObject->findChild<QObject *>(QStringLiteral("sourcePanel"));
        auto *viewport = rootObject->findChild<QObject *>(
            QStringLiteral("galleryViewerViewport"));
        auto *baseImage = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerBaseImage"));
        auto *nativeImage = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerNativeImage"));
        auto *sphericLoader = rootObject->findChild<QObject *>(
            QStringLiteral("gallerySphericViewerLoader"));
        QVERIFY(viewer);
        QVERIFY(sourcePanel);
        QVERIFY(viewport);
        QVERIFY(baseImage);
        QVERIFY(nativeImage);
        QVERIFY(sphericLoader);
        QVERIFY(rootObject->property("ownsEnter").toBool());
        QVERIFY(rootObject->property("ownsEscape").toBool());
        QVERIFY(rootObject->property("ownsCtrlPlus").toBool());
        QVERIFY(rootObject->property("ownsCtrlF").toBool());
        QVERIFY(rootObject->property("ownsModifiedEnter").toBool());

        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QCOMPARE(viewer->property("transitioning").toBool(), false);
        QCOMPARE(sourcePanel->property("viewerTransitionActive").toBool(), false);

        QSignalSpy navigationSpy(viewer,
                                 SIGNAL(navigationRequested(QString,int)));
        QTRY_VERIFY_WITH_TIMEOUT(
            session->imageOriginalSizeAt(0).width() > 1
                && session->imageOriginalSizeAt(1).width() > 1,
            5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentSourceLevelValue").toInt(), 1, 5000);
        auto *viewerImage = viewport->property("image").value<QObject *>();
        QVERIFY(viewerImage);
        QCOMPARE(viewerImage->property("fromIndex").toInt(), 0);
        QCOMPARE(viewerImage->property("fromLevel").toInt(), 1);
        const QUrl fitBaseSource = baseImage->property("source").toUrl();
        QVERIFY(fitBaseSource.toString().startsWith(
            QStringLiteral("image://zoingallery-thumbnails/")));
        QVERIFY(viewport->property("imageTextureReady").toBool());

        auto *viewerItem = qobject_cast<QQuickItem *>(viewer);
        QVERIFY(viewerItem);
        viewerItem->forceActiveFocus();
        QTRY_VERIFY(viewerItem->hasActiveFocus());
        QSignalSpy fullscreenRequests(
            viewer, SIGNAL(fullscreenToggleRequested()));
        QSignalSpy selectionRequests(
            viewer, SIGNAL(selectionRequested(QString,QVariant)));
        QVERIFY(fullscreenRequests.isValid());
        QVERIFY(selectionRequests.isValid());

        // The reusable viewer owns the complete keyboard surface and restores
        // ViewerMode's local commands instead of merely swallowing them.
        QVERIFY(viewport->property("zoomFitView").toBool());
        QTest::keyClick(&view, Qt::Key_Z);
        QVERIFY(!viewport->property("zoomFitView").toBool());
        QTest::keyClick(&view, Qt::Key_Z);
        QVERIFY(viewport->property("zoomFitView").toBool());
        QTest::keyClick(&view, Qt::Key_F);
        QTest::keyClick(&view, Qt::Key_Return, Qt::AltModifier);
        QCOMPARE(fullscreenRequests.size(), 2);

        // S/P is the original ViewerMode panorama toggle.  The embedded mode
        // must reuse the viewport's already-decoded texture and keep both the
        // toggle and Ctrl-wheel FOV update entirely local.
        const int navigationBeforeSphere = navigationSpy.size();
        const int selectionBeforeSphere = selectionRequests.size();
        QTest::keyClick(&view, Qt::Key_S);
        QTRY_VERIFY(viewer->property("sphericViewerMode").toBool());
        QTRY_VERIFY(sphericLoader->property("item").value<QObject *>());
        auto *sphericViewer =
            sphericLoader->property("item").value<QObject *>();
        QVERIFY(sphericViewer);
        // Panorama rendering needs the full/native texture.  Switching S on
        // must promote the current fit tier through the existing shared decode
        // sequence, not start a private decoder or leave a scaled thumbnail in
        // the shader.
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentSourceLevelValue").toInt(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewport->property("textureSource").value<QObject *>(),
            static_cast<QObject *>(nativeImage), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            sphericViewer->property("source").value<QObject *>(),
            viewport->property("textureSource").value<QObject *>(), 5000);
        QCOMPARE(sphericViewer->property("originalSize").toSize(),
                 viewport->property("originalSize").toSize());
        const qreal initialFov = sphericViewer->property("fov").toReal();
        QTest::wheelEvent(&view, QPoint(320, 210), QPoint(0, 120), QPoint(),
                          Qt::ControlModifier);
        QTRY_VERIFY(sphericViewer->property("fov").toReal() < initialFov);

        auto *sphericPointerArea = sphericViewer->findChild<QQuickItem *>(
            QStringLiteral("sphericViewerPointerArea"));
        QVERIFY(sphericPointerArea);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(260, 180));
        QVERIFY(!sphericViewer->property("inertiaRunning").toBool());
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                            QPoint(260, 180));
        QVERIFY(!sphericViewer->property("inertiaRunning").toBool());

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(260, 180));
        QTest::mouseMove(&view, QPoint(350, 235), 20);
        QTRY_VERIFY(sphericViewer->property("inertiaRunning").toBool());
        QVERIFY(QMetaObject::invokeMethod(sphericPointerArea, "canceled"));
        QTRY_VERIFY(!sphericViewer->property("inertiaRunning").toBool());
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                            QPoint(350, 235));

        const qreal initialPan = sphericViewer->property("pan").toReal();
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(260, 180));
        QTest::mouseMove(&view, QPoint(350, 235), 20);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                            QPoint(350, 235));
        QTRY_VERIFY(qAbs(sphericViewer->property("pan").toReal()
                         - initialPan) > 0.001);
        QTRY_VERIFY(sphericViewer->property("inertiaRunning").toBool());
        sphericViewer->setProperty("visible", false);
        QTRY_VERIFY(!sphericViewer->property("inertiaRunning").toBool());
        sphericViewer->setProperty("visible", true);
        QCOMPARE(navigationSpy.size(), navigationBeforeSphere);

        // Middle click retains the old fullscreen command even while the
        // panorama MouseArea is topmost; it must not become an f4 action.
        QTest::mouseClick(&view, Qt::MiddleButton, Qt::NoModifier,
                          QPoint(320, 210));
        QTRY_COMPARE(fullscreenRequests.size(), 3);
        QCOMPARE(navigationSpy.size(), navigationBeforeSphere);
        QCOMPARE(selectionRequests.size(), selectionBeforeSphere);
        QCOMPARE(rootObject->property("bubbledKeyCount").toInt(), 0);
        QVERIFY(viewerItem->hasActiveFocus());

        // Neighbor navigation keeps the one sphere instance bound to the
        // Flickable's shared native texture.  Only the stable cursor intents
        // are emitted; the panorama never owns a decoder or file operation.
        QTest::keyClick(&view, Qt::Key_Right);
        QTRY_COMPARE(navigationSpy.size(), navigationBeforeSphere + 1);
        QTRY_COMPARE(viewer->property("presentedIndex").toInt(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentSourceLevelValue").toInt(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            sphericViewer->property("source").value<QObject *>(),
            viewport->property("textureSource").value<QObject *>(), 5000);
        QTest::keyClick(&view, Qt::Key_Left);
        QTRY_COMPARE(navigationSpy.size(), navigationBeforeSphere + 2);
        QTRY_COMPARE(viewer->property("presentedIndex").toInt(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentSourceLevelValue").toInt(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            sphericViewer->property("source").value<QObject *>(),
            viewport->property("textureSource").value<QObject *>(), 5000);
        navigationSpy.clear();

        QTest::keyClick(&view, Qt::Key_P);
        QTRY_VERIFY(!viewer->property("sphericViewerMode").toBool());
        QTRY_VERIFY(!sphericLoader->property("item").value<QObject *>());
        QCOMPARE(navigationSpy.size(), 0);
        QCOMPARE(selectionRequests.size(), selectionBeforeSphere);
        QCOMPARE(viewport->property("rotationMode").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_BracketRight);
        QCOMPARE(viewport->property("rotationMode").toInt(), 1);
        QTest::keyClick(&view, Qt::Key_BracketLeft);
        QCOMPARE(viewport->property("rotationMode").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_Insert);
        QCOMPARE(selectionRequests.size(), 1);
        QCOMPARE(selectionRequests.constFirst().at(0).toString(),
                 QStringLiteral("add"));
        QCOMPARE(selectionRequests.constFirst().at(1).toList(),
                 QVariantList{QStringLiteral("first")});
        QTest::keyClick(&view, Qt::Key_X);
        QTest::keyClick(&view, Qt::Key_F3);
        QCOMPARE(rootObject->property("bubbledKeyCount").toInt(), 0);
        // Let the restored Fit and rotation animations settle before the
        // existing viewport-preservation scenario records its baseline.
        QTest::qWait(250);
        QVERIFY(viewport->property("zoomFitView").toBool());

        const qreal targetScale = viewer->property("fittedScale").toReal() * 2;
        QVERIFY(QMetaObject::invokeMethod(
            viewport, "setViewport",
            Q_ARG(QVariant, QVariant(targetScale)),
            Q_ARG(QVariant, QVariant(-123.0)),
            Q_ARG(QVariant, QVariant(-85.0))));
        QTRY_VERIFY(viewer->property("zoomFactor").toReal() > 1.5);
        const qreal preservedScale = viewport->property("zoomScale").toReal();
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentSourceLevelValue").toInt(), 2, 5000);
        QCOMPARE(baseImage->property("source").toUrl(), fitBaseSource);
        QCOMPARE(viewerImage->property("fromLevel").toInt(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(
            nativeImage->property("source").toUrl().toString().startsWith(
                QStringLiteral("image://zoingallery-async/")),
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("imageTextureReady").toBool(), 5000);

        QSignalSpy textureReadySpy(viewport,
                                   SIGNAL(imageTextureReadyChanged()));
        QVERIFY(textureReadySpy.isValid());
        QVERIFY(QMetaObject::invokeMethod(rootObject, "commitNextSwipe"));
        const auto committedPosition = rootObject->property("committedPosition").toPointF();
        qInfo() << "[FIX:swipe-commit] expected" << committedPosition
                << "actual" << viewerImage->property("x") << viewerImage->property("y");
        QCOMPARE(QPointF(viewerImage->property("x").toReal(),
                         viewerImage->property("y").toReal()), committedPosition);
        QTRY_COMPARE(navigationSpy.size(), 1);
        QCOMPARE(navigationSpy.at(0).at(0).toString(), QStringLiteral("second"));
        QCOMPARE(navigationSpy.at(0).at(1).toInt(), 22);
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(viewer->property("presentedIndex").toInt(), 1);
        QTRY_VERIFY(qAbs(viewport->property("zoomScale").toReal()
                         - preservedScale) < 0.01);
        QVERIFY(!viewport->property("zoomFitView").toBool());
        QTRY_COMPARE(viewerImage->property("fromIndex").toInt(), 1);
        QTRY_VERIFY(viewport->property("imageTextureReady").toBool());
        // The readiness property starts true. Any notify emission here would
        // necessarily mean it toggled through false during the source handoff
        // (QML property notify signals carry no value argument).
        QCOMPARE(textureReadySpy.size(), 0);
        const qreal imageX = viewerImage->property("x").toReal();
        const qreal imageY = viewerImage->property("y").toReal();
        const qreal minimumX = qMin<qreal>(
            0, 640 - viewerImage->property("width").toReal());
        const qreal minimumY = qMin<qreal>(
            0, 420 - viewerImage->property("height").toReal());
        QVERIFY(imageX >= minimumX - 0.5 && imageX <= 0.5);
        QVERIFY(imageY >= minimumY - 0.5 && imageY <= 0.5);

        viewer->setProperty("sphericViewerMode", true);
        QTRY_VERIFY(sphericLoader->property("item").value<QObject *>());
        QSignalSpy closeCompleted(viewer, SIGNAL(closeCompleted()));
        QSignalSpy closeAlias(viewer, SIGNAL(closeRequested()));
        QVERIFY(QMetaObject::invokeMethod(rootObject, "closeNow"));
        QCOMPARE(closeCompleted.size(), 0);
        QCOMPARE(closeAlias.size(), 0);
        // The sphere is part of the same reverse image-to-tile transition; it
        // must not unload on close press before the animation completes.
        QVERIFY(sphericLoader->property("item").value<QObject *>());
        QVERIFY(sourcePanel->property("viewerTransitionActive").toBool());
        QVERIFY(session->viewerOpen());
        QTRY_COMPARE_WITH_TIMEOUT(closeCompleted.size(), 1, 5000);
        QCOMPARE(closeAlias.size(), 1);
        QCOMPARE(rootObject->property("closeCompletedCount").toInt(), 1);
        QCOMPARE(rootObject->property("closeAliasCount").toInt(), 1);
        QCOMPARE(sourcePanel->property("viewerTransitionActive").toBool(), false);
        QVERIFY(session->viewerOpen());

        runtime->shutdown();
    }

    void fitSwipeCommitSnapshotsOriginalSizeBeforeNavigationReset() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString firstPath =
            directory.filePath(QStringLiteral("snapshot-first.png"));
        const QString secondPath =
            directory.filePath(QStringLiteral("snapshot-second.png"));
        QVERIFY(writeImage(firstPath, QSize(800, 500), Qt::red));
        QVERIFY(writeImage(secondPath, QSize(500, 800), Qt::blue));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-fit-commit-snapshot"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("first"), 11, firstPath),
             imageEntry(QStringLiteral("second"), 22, secondPath)}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("first"), 11,
                                            {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("snapshotSession"), session);

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 640
                height: 420
                function beginNextSwipe() {
                    viewer.beginViewerNavigation(1)
                }
                function commitSwipe() {
                    viewer.commitViewerNavigation()
                }
                GalleryViewer {
                    id: viewer
                    objectName: "snapshotViewer"
                    anchors.fill: parent
                    session: snapshotSession
                    animationDuration: 1
                }
            }
        )QML", QStringLiteral("GalleryViewerFitCommitSnapshot.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();

        auto *viewer = rootObject->findChild<QQuickItem *>(
            QStringLiteral("snapshotViewer"));
        auto *viewport = rootObject->findChild<QObject *>(
            QStringLiteral("galleryViewerViewport"));
        auto *neighborImage = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerNavigationNeighborImage"));
        QVERIFY(viewer);
        QVERIFY(viewport);
        QVERIFY(neighborImage);

        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(session->imageOriginalSizeAt(1),
                                  QSize(500, 800), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentSourceLevelValue").toInt(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("imageTextureReady").toBool(), 5000);
        QVERIFY(viewport->property("zoomFitView").toBool());
        QVERIFY(!viewport->property("sourceSizeFallbackPending").toBool());

        QVERIFY(QMetaObject::invokeMethod(rootObject, "beginNextSwipe"));
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("viewerNavigationTargetSourceLevel").toInt(),
            1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewer->property("viewerNavigationTargetSource")
                 .toUrl().isEmpty(),
            5000);
        // QQuickImage::Ready is 1. Adoption is deliberately unavailable until
        // this hidden transition texture is ready.
        QTRY_COMPARE_WITH_TIMEOUT(neighborImage->property("status").toInt(),
                                  1, 5000);
        QCOMPARE(viewer->property("viewerNavigationTargetOriginalSize")
                     .toSize(),
                 QSize(500, 800));

        QSignalSpy fallbackSpy(
            viewport, SIGNAL(sourceSizeFallbackPendingChanged()));
        QSignalSpy navigationSpy(
            viewer, SIGNAL(navigationRequested(QString,int)));
        QVERIFY(fallbackSpy.isValid());
        QVERIFY(navigationSpy.isValid());

        QVERIFY(QMetaObject::invokeMethod(rootObject, "commitSwipe"));
        QTRY_COMPARE(navigationSpy.size(), 1);
        QCOMPARE(viewer->property("presentedIndex").toInt(), 1);
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(viewport->property("originalSize").toSize(),
                 QSize(500, 800));
        // A 0x0 adoption would toggle this true and then false again when the
        // normal tier refresh repairs the original size.
        QCOMPARE(fallbackSpy.size(), 0);
        QVERIFY(!viewport->property("sourceSizeFallbackPending").toBool());

        runtime->shutdown();
    }

    void viewerTildeRestoresPreviousIdentityViewportAndLock() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString firstPath =
            directory.filePath(QStringLiteral("tilde-first.png"));
        const QString secondPath =
            directory.filePath(QStringLiteral("tilde-second.png"));
        const QString thirdPath =
            directory.filePath(QStringLiteral("tilde-third.png"));
        QVERIFY(writeImage(firstPath, QSize(1600, 1200), Qt::red));
        QVERIFY(writeImage(secondPath, QSize(800, 600), Qt::blue));
        QVERIFY(writeImage(thirdPath, QSize(1200, 900), Qt::green));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-viewer-tilde"));
        QVERIFY(session);
        const QVariantMap first =
            imageEntry(QStringLiteral("a"), 11, firstPath);
        const QVariantMap second =
            imageEntry(QStringLiteral("b"), 22, secondPath);
        const QVariantMap third =
            imageEntry(QStringLiteral("c"), 33, thirdPath);
        QVERIFY(session->applyExternalCatalog({first, second, third}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("a"), 11, {}, 1));
        session->setViewerOpen(true);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("tildeSession"), session);

#ifndef Q_MOC_RUN
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 640
                height: 420
                GalleryViewer {
                    id: viewer
                    objectName: "tildeViewer"
                    anchors.fill: parent
                    session: tildeSession
                    animationDuration: 1
                }
            }
        )QML", QStringLiteral("GalleryViewerTilde.qml"));
#else
        QObject *rootObject = nullptr;
#endif
        QVERIFY(rootObject);
        view.show();
        view.requestActivate();

        auto *viewer = rootObject->findChild<QQuickItem *>(
            QStringLiteral("tildeViewer"));
        auto *viewport = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerViewport"));
        QVERIFY(viewer);
        QVERIFY(viewport);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            session->imageOriginalSizeAt(0).width() > 1
                && session->imageOriginalSizeAt(1).width() > 1,
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("imageTextureReady").toBool(), 5000);
        viewer->forceActiveFocus();
        QTRY_VERIFY(viewer->hasActiveFocus());

        auto *viewerImage = viewport->property("image").value<QObject *>();
        QVERIFY(viewerImage);

        QSignalSpy navigationSpy(
            viewer, SIGNAL(navigationRequested(QString,int)));
        QVERIFY(navigationSpy.isValid());

        // With no history, the baseline's first QuoteLeft selects the next
        // image. The reusable viewer stays optimistic until f4 acknowledges
        // the stable identity; exactly one host intent is emitted.
        QTest::keyClick(&view, Qt::Key_QuoteLeft);
        QTRY_COMPARE(navigationSpy.size(), 1);
        QCOMPARE(navigationSpy.constLast().at(0).toString(),
                 QStringLiteral("b"));
        QCOMPARE(navigationSpy.constLast().at(1).toInt(), 22);
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(viewer->property("presentedEntryId").toString(),
                 QStringLiteral("b"));
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("a"));
        QTest::qWait(100);
        QCOMPARE(navigationSpy.size(), 1);

        QVERIFY(session->applyExternalState(QStringLiteral("b"), 22, {}, 1));
        QTRY_COMPARE(session->cursorEntryId(), QStringLiteral("b"));
        const qreal fitScale = viewer->property("fittedScale").toReal();
        const qreal zoomScale = fitScale * 2.0;
        QVERIFY(QMetaObject::invokeMethod(
            viewport, "setViewport",
            Q_ARG(QVariant, QVariant(zoomScale)),
            Q_ARG(QVariant, QVariant(-230.0)),
            Q_ARG(QVariant, QVariant(-145.0))));
        QTRY_VERIFY(viewer->property("zoomFactor").toReal() > 1.9);
        const QSizeF firstEffectiveSize =
            viewport->property("effectiveOriginalSize").toSizeF();
        const qreal firstCenterRatioX =
            ((320.0 - viewerImage->property("x").toReal())
             / viewport->property("zoomScale").toReal())
            / firstEffectiveSize.width();
        const qreal firstCenterRatioY =
            ((210.0 - viewerImage->property("y").toReal())
             / viewport->property("zoomScale").toReal())
            / firstEffectiveSize.height();
        const auto currentCenterRatioX = [&] {
            const QSizeF size =
                viewport->property("effectiveOriginalSize").toSizeF();
            return ((320.0 - viewerImage->property("x").toReal())
                    / viewport->property("zoomScale").toReal())
                / size.width();
        };
        const auto currentCenterRatioY = [&] {
            const QSizeF size =
                viewport->property("effectiveOriginalSize").toSizeF();
            return ((210.0 - viewerImage->property("y").toReal())
                    / viewport->property("zoomScale").toReal())
                / size.height();
        };

        QTest::keyClick(&view, Qt::Key_AsciiTilde);
        QTRY_COMPARE(navigationSpy.size(), 2);
        QCOMPARE(navigationSpy.constLast().at(0).toString(),
                 QStringLiteral("a"));
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("b"));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(viewer->property("zoomFactor").toReal() - 2.0) < 0.03,
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(currentCenterRatioX() - firstCenterRatioX) < 0.02,
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(currentCenterRatioY() - firstCenterRatioY) < 0.02,
            5000);
        QTest::qWait(100);
        QTRY_COMPARE(navigationSpy.size(), 2);
        QVERIFY(session->applyExternalState(QStringLiteral("a"), 11, {}, 1));

        // Shift+tilde locks A and remembers B as the return identity. Normal
        // navigation updates only the return side of that pair.
        QTest::keyClick(&view, Qt::Key_AsciiTilde, Qt::ShiftModifier);
        QVERIFY(session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("a"));
        QCOMPARE(session->viewerPreviousReturnEntryId(),
                 QStringLiteral("b"));
        QCOMPARE(navigationSpy.size(), 2);

        QTest::keyClick(&view, Qt::Key_PageDown);
        QTRY_COMPARE(navigationSpy.size(), 3);
        QCOMPARE(navigationSpy.constLast().at(0).toString(),
                 QStringLiteral("b"));
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("a"));
        QCOMPARE(session->viewerPreviousReturnEntryId(),
                 QStringLiteral("b"));
        QVERIFY(session->applyExternalState(QStringLiteral("b"), 22, {}, 1));

        QTest::keyClick(&view, Qt::Key_QuoteLeft);
        QTRY_COMPARE(navigationSpy.size(), 4);
        QCOMPARE(navigationSpy.constLast().at(0).toString(),
                 QStringLiteral("a"));
        QVERIFY(session->applyExternalState(QStringLiteral("a"), 11, {}, 1));
        QTest::keyClick(&view, Qt::Key_AsciiTilde, Qt::ShiftModifier);
        QVERIFY(!session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("b"));
        QCOMPARE(navigationSpy.size(), 4);

        // Re-lock A, then prove an in-place reorder remaps both the current
        // and locked previous identities without manufacturing navigation.
        QTest::keyClick(&view, Qt::Key_QuoteLeft, Qt::ShiftModifier);
        QVERIFY(session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("a"));
        QVERIFY(session->applyExternalCatalog({second, third, first}, 2));
        QTRY_COMPARE(viewer->property("presentedEntryId").toString(),
                     QStringLiteral("a"));
        QCOMPARE(viewer->property("presentedIndex").toInt(), 2);
        QCOMPARE(session->indexForEntryId(QStringLiteral("a")), 2);
        QCOMPARE(navigationSpy.size(), 4);

        QTest::keyClick(&view, Qt::Key_AsciiTilde);
        QTRY_COMPARE(navigationSpy.size(), 5);
        QCOMPARE(navigationSpy.constLast().at(0).toString(),
                 QStringLiteral("b"));
        QVERIFY(session->applyExternalState(QStringLiteral("b"), 22, {}, 1));

        // Removing the locked image leaves no stale row. As in ViewerMode,
        // tilde then uses the first-use next/previous fallback safely.
        QVERIFY(session->applyExternalCatalog({second, third}, 3));
        QVERIFY(session->viewerPreviousLocked());
        QCOMPARE(session->viewerPreviousEntryId(), QStringLiteral("a"));
        QCOMPARE(session->indexForEntryId(QStringLiteral("a")), -1);
        QTest::keyClick(&view, Qt::Key_QuoteLeft);
        QTRY_COMPARE(navigationSpy.size(), 6);
        QCOMPARE(navigationSpy.constLast().at(0).toString(),
                 QStringLiteral("c"));
        QTest::qWait(100);
        QCOMPARE(navigationSpy.size(), 6);

        runtime->shutdown();
    }

    void panelRestoresNonzeroTransitionTargetAfterCatalogReset() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath = directory.filePath(QStringLiteral("tile.png"));
        QVERIFY(writeImage(imagePath, QSize(320, 120), Qt::green));
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        auto *session = runtime->createExternalSession(QStringLiteral("transition-reset"));
        const QVariantList entries{
            imageEntry(QStringLiteral("first"), 0, imagePath),
            imageEntry(QStringLiteral("tile"), 1, imagePath)};
        QVERIFY(session->applyExternalCatalog(entries, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("tile"), 1, {}, 1));
        view.engine()->rootContext()->setContextProperty(QStringLiteral("testSession"), session);
        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                id: panel
                width: 480; height: 360
                session: null
                devicePixelRatio: 1.75
                property int transitionCursor: -1
                property rect capturedGeometry
                property string capturedSource
                property bool openViewer: false
                function captureTarget() {
                    const item = currentTransitionItem()
                    transitionCursor = item ? item.viewIndex : -1
                    capturedGeometry = currentItemImageGeometry(panel)
                    capturedSource = currentItemImageSource()
                }
                function resetLayoutCursor() { galleryLayout.currentIndex = 0 }
                Loader {
                    anchors.fill: parent
                    active: panel.openViewer
                    sourceComponent: GalleryViewer {
                        objectName: "resetViewer"
                        session: testSession
                        sourcePanel: panel
                        animationDuration: 300
                    }
                }
            }
        )QML", QStringLiteral("GalleryTransitionReset.qml"));
        QVERIFY(rootObject);
        view.show();
        rootObject->setProperty("session", QVariant::fromValue(session));
        QTest::qWait(200);
        QMetaObject::invokeMethod(rootObject, "captureTarget");
        QCOMPARE(rootObject->property("transitionCursor").toInt(), 1);
        QVERIFY(session->applyExternalCatalog(entries, 2, {{QStringLiteral("sourceIdentityChanged"), true}}));
        QVERIFY(session->applyExternalState(QStringLiteral("tile"), 1, {}, 2));
        QTest::qWait(200);
        // A native layout reset can retain row zero while the authoritative
        // session cursor is unchanged (observed in f4: layout 0, session 75).
        QMetaObject::invokeMethod(rootObject, "resetLayoutCursor");
        QMetaObject::invokeMethod(rootObject, "captureTarget");
        QCOMPARE(rootObject->property("transitionCursor").toInt(), 1);
        const QRectF thumbnailGeometry = rootObject->property("capturedGeometry").toRectF();
        QVERIFY(thumbnailGeometry.width() > 1);
        QVERIFY(thumbnailGeometry.height() > 1);
        QVERIFY(!rootObject->property("capturedSource").toString().isEmpty());
        session->setViewerOpen(true);
        rootObject->setProperty("openViewer", true);
        auto *viewer = rootObject->findChild<QQuickItem *>(QStringLiteral("resetViewer"));
        QVERIFY(viewer);
        QTRY_VERIFY(viewer->property("transitionHasGeometry").toBool());
        QCOMPARE(viewer->property("transitionSourceGeometry").toRectF(), thumbnailGeometry);
        QTRY_VERIFY(viewer->property("transitionProgress").toReal() > 0);
        QVERIFY(viewer->property("transitionProgress").toReal() < 1);
        const QImage transitionFrame = view.grabWindow();
        QVERIFY(!transitionFrame.isNull());
        QTRY_COMPARE(viewer->property("transitionProgress").toReal(), 1.0);
        const QImage settledFrame = view.grabWindow();
        QVERIFY(!settledFrame.isNull());
        QVERIFY(transitionFrame != settledFrame);
        if (!qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR").isEmpty()) {
            const QDir captureDir(qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR"));
            QVERIFY(transitionFrame.save(captureDir.filePath(QStringLiteral("viewer-opening.png"))));
            QVERIFY(settledFrame.save(captureDir.filePath(QStringLiteral("viewer-open.png"))));
        }
        runtime->shutdown();
    }

    void videoThumbnailShowsCenteredPlayBadgeAt175Percent_data() {
        QTest::addColumn<QSize>("imageSize");
        QTest::newRow("landscape") << QSize(240, 135);
        QTest::newRow("portrait") << QSize(90, 160);
        QTest::newRow("square") << QSize(160, 160);
    }

    void videoThumbnailShowsCenteredPlayBadgeAt175Percent() {
        QFETCH(QSize, imageSize);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath =
            directory.filePath(QStringLiteral("video-contact-sheet.png"));
        QVERIFY(writeImage(imagePath, imageSize, QColor(46, 72, 96)));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("video-thumbnail-play-badge"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("video-tile"), 0, imagePath)}, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("video-tile"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 520
                height: 360
                GalleryPanel {
                    id: panel
                    objectName: "videoBadgePanel"
                    anchors.fill: parent
                    session: testSession
                    devicePixelRatio: 1.75
                }
            }
        )QML", QStringLiteral("GalleryVideoThumbnailBadge.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.devicePixelRatio(), qreal(1.75));

        auto leaf = [root](const QString &name) {
            return root->findChild<QQuickItem *>(name);
        };
        QQuickItem *thumbnail = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (thumbnail = leaf(QStringLiteral("galleryThumbnail-0")))
                && thumbnail->isVisible(), 5000);
        QQuickItem *thumbnailImage =
            leaf(QStringLiteral("galleryThumbnailImage-0"));
        QVERIFY(thumbnailImage);
        QTRY_COMPARE_WITH_TIMEOUT(
            thumbnailImage->property("status").toInt(), 1, 5000);
        QQuickItem *preview = thumbnail->parentItem();
        QVERIFY(preview);
        QObject *entry = preview->property("entry").value<QObject *>();
        QVERIFY(entry);
        auto *visualModel = entry->property("visualModel").value<QObject *>();
        QVERIFY(visualModel);

        QQuickItem *badge = leaf(QStringLiteral("galleryVideoPlayBadge-0"));
        QVERIFY(badge);
        QVERIFY(!badge->isVisible());
        QVariantMap row = entry->property("visualRow").toMap();
        QVERIFY(!row.isEmpty());

        row.insert(QStringLiteral("thumbnailKind"), QStringLiteral("video"));
        QVERIFY(entry->setProperty("visualRow", row));
        QTRY_VERIFY_WITH_TIMEOUT(visualModel->property("isVideo").toBool(),
                                 1000);
        QTRY_VERIFY_WITH_TIMEOUT(badge->isVisible(), 3000);

        QQuickItem *badgeCircle =
            leaf(QStringLiteral("galleryVideoPlayBadgeCircle-0"));
        QQuickItem *playIcon = leaf(QStringLiteral("galleryVideoPlayIcon-0"));
        QVERIFY(badgeCircle);
        QVERIFY(playIcon);
        QTRY_COMPARE_WITH_TIMEOUT(playIcon->property("status").toInt(), 1,
                                  3000);

        QVERIFY(playIcon->property("smooth").toBool());
        const qreal dpr = view.devicePixelRatio();
        QCOMPARE(playIcon->property("sourceSize").toSize(),
                 QSize(qRound(playIcon->width() * dpr),
                       qRound(playIcon->height() * dpr)));
        QVERIFY(thumbnailImage->height() > 0);
        const qreal sourceAspect = qreal(imageSize.width()) / imageSize.height();
        const qreal physicalHeight = thumbnailImage->height() * dpr;
        QVERIFY2(qAbs(thumbnailImage->width() * dpr
                      - physicalHeight * sourceAspect) <= 1 + sourceAspect,
                 "thumbnail image geometry must keep its source aspect ratio");
        QList<QQuickItem *> visualLeaves{thumbnailImage, badge, badgeCircle, playIcon};
        if (QQuickItem *shader = leaf(QStringLiteral("galleryThumbnailShader-0"));
            shader && shader->isVisible())
            visualLeaves.append(shader);
        for (const QString &name : {QStringLiteral("galleryMasonryLabel-0"),
                                    QStringLiteral("galleryGridLabel-0"),
                                    QStringLiteral("galleryIconsLabel-0"),
                                    QStringLiteral("galleryBaseName-0"),
                                    QStringLiteral("galleryExtension-0"),
                                    QStringLiteral("gallerySize-0")}) {
            if (QQuickItem *caption = leaf(name); caption && caption->isVisible())
                visualLeaves.append(caption);
        }
        for (QQuickItem *item : visualLeaves) {
            const QPointF origin =
                item->mapToItem(view.contentItem(), QPointF());
            const QPointF physicalOrigin = origin * dpr;
            const QString name = item->objectName();
            QVERIFY2(qAbs(physicalOrigin.x() - qRound(physicalOrigin.x()))
                         < 0.001
                     && qAbs(physicalOrigin.y() - qRound(physicalOrigin.y()))
                         < 0.001,
                     qPrintable(QStringLiteral(
                         "%1 scene origin is off the 1.75x pixel grid: %2, %3")
                         .arg(name)
                         .arg(physicalOrigin.x(), 0, 'f', 6)
                         .arg(physicalOrigin.y(), 0, 'f', 6)));
            const QPointF unitX = item->mapToItem(
                view.contentItem(), QPointF(1, 0)) - origin;
            const QPointF unitY = item->mapToItem(
                view.contentItem(), QPointF(0, 1)) - origin;
            QVERIFY(QLineF(unitX, QPointF(1, 0)).length() < 0.0001);
            QVERIFY(QLineF(unitY, QPointF(0, 1)).length() < 0.0001);
            QVERIFY(qAbs(item->width() * dpr
                         - qRound(item->width() * dpr)) < 0.001);
            QVERIFY(qAbs(item->height() * dpr
                         - qRound(item->height() * dpr)) < 0.001);
        }

        const QPointF badgeCenter = badge->mapToItem(
            view.contentItem(), QPointF(badge->width() / 2,
                                        badge->height() / 2));
        const QPointF thumbnailCenter = thumbnail->mapToItem(
            view.contentItem(), QPointF(thumbnail->width() / 2,
                                        thumbnail->height() / 2));
        const QPointF centerDelta = (badgeCenter - thumbnailCenter) * dpr;
        QVERIFY2(qAbs(centerDelta.x()) <= 0.501
                     && qAbs(centerDelta.y()) <= 0.501,
                 qPrintable(QStringLiteral(
                     "video play badge is not centered over its thumbnail: "
                     "%1, %2 physical pixels")
                     .arg(centerDelta.x(), 0, 'f', 3)
                     .arg(centerDelta.y(), 0, 'f', 3)));
        const QPointF iconCenter = playIcon->mapToItem(
            view.contentItem(), QPointF(playIcon->width() / 2,
                                        playIcon->height() / 2));
        const QPointF iconCenterDelta = (iconCenter - badgeCenter) * dpr;
        QVERIFY2(qAbs(iconCenterDelta.x()) <= 0.501
                     && qAbs(iconCenterDelta.y()) <= 0.501,
                 qPrintable(QStringLiteral(
                     "play glyph is not centered in its badge: %1, %2 "
                     "physical pixels")
                     .arg(iconCenterDelta.x(), 0, 'f', 3)
                     .arg(iconCenterDelta.y(), 0, 'f', 3)));

        const QImage capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        const QColor glyphCenterPixel = capture.pixelColor(
            qRound(iconCenter.x() * dpr), qRound(iconCenter.y() * dpr));
        QVERIFY2(glyphCenterPixel.red() > 235
                     && glyphCenterPixel.green() > 235
                     && glyphCenterPixel.blue() > 235,
                 "the thumbnail play glyph must have a filled white center");
        if (!qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR").isEmpty()) {
            const QDir captureDir(
                qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR"));
            QVERIFY(capture.save(captureDir.filePath(
                QStringLiteral("video-thumbnail-play-badge-%1.png")
                    .arg(QString::fromLatin1(QTest::currentDataTag())))));
        }

        row.remove(QStringLiteral("thumbnailKind"));
        QVERIFY(entry->setProperty("visualRow", row));
        QTRY_VERIFY_WITH_TIMEOUT(!visualModel->property("isVideo").toBool(),
                                 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!badge->isVisible(), 1000);

        runtime->shutdown();
    }

    void panelExposesTransitionTileAndSuppressesOnlyItsImage() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath = directory.filePath(QStringLiteral("tile.png"));
        QVERIFY(writeImage(imagePath, QSize(320, 120), Qt::green));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("qml-panel"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {imageEntry(QStringLiteral("tile"), 4, imagePath)}, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("tile"), 4, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 480
                height: 360
                property string capturedTileSource: ""
                property int imagePadMode: Image.Pad
                property int imageCropMode: Image.PreserveAspectCrop
                property int imageFitMode: Image.PreserveAspectFit
                function tileGeometry() {
                    return panel.currentItemImageGeometry(this)
                }
                function captureTileSource() {
                    capturedTileSource = panel.currentItemImageSource()
                }
                function suppressTile() {
                    panel.viewerTransitionEntryId = "tile"
                    panel.viewerTransitionActive = true
                }
                function revealTile() {
                    panel.viewerTransitionActive = false
                    panel.viewerTransitionEntryId = ""
                }
                GalleryPanel {
                    id: panel
                    objectName: "panel"
                    anchors.fill: parent
                    session: testSession
                    devicePixelRatio: 2
                }
            }
        )QML", QStringLiteral("GalleryPanelTransition.qml"));
        QVERIFY(rootObject);
        view.show();

        auto *panel = rootObject->findChild<QObject *>(QStringLiteral("panel"));
        QVERIFY(panel);
        QQuickItem *thumbnail = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (thumbnail = rootObject->findChild<QQuickItem *>(
                 QStringLiteral("galleryThumbnail-0"))) != nullptr,
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnail->isVisible(), 5000);
        auto *thumbnailImage = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailImage-0"));
        auto *thumbnailShader = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailShader-0"));
        auto *thumbnailBackdrop = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailBackdrop-0"));
        QVERIFY(thumbnailImage);
        QVERIFY(thumbnailShader);
        QVERIFY(thumbnailBackdrop);

        // Embedded Masonry must use the same render chain as standalone:
        // the ordinary Image is only the texture source, while the visible
        // ShaderEffect performs unsharp-mask, alpha checkerboard and rounded
        // corners using physical-pixel uniforms.
        QTRY_VERIFY_WITH_TIMEOUT(thumbnailShader->isVisible(), 5000);
        QVERIFY(!thumbnailImage->isVisible());
        QCOMPARE(thumbnailShader->property("source").value<QObject *>(),
                 static_cast<QObject *>(thumbnailImage));
        QCOMPARE(thumbnailImage->property("asynchronous").toBool(), false);
        QCOMPARE(thumbnailImage->property("cache").toBool(), false);
        QCOMPARE(thumbnailShader->property("sharpenAmount").toReal(), 1.5);
        QCOMPARE(thumbnailShader->property("showCheckerboard").toBool(),
                 true);
        QCOMPARE(thumbnailShader->property("checkerboardSize").toInt(), 8);
        QCOMPARE(thumbnailShader->property("borderRadius").toReal(), 8.2);
        QCOMPARE(thumbnailShader->property("fragmentShader").toUrl(),
                 QUrl(QStringLiteral(
                     "qrc:/ZoinGallery/resources/shader.frag.qsb")));
        const QSizeF shaderViewport =
            thumbnailShader->property("viewportSize").toSizeF();
        QCOMPARE(shaderViewport.width(), thumbnailShader->width() * 2);
        QCOMPARE(shaderViewport.height(), thumbnailShader->height() * 2);

        QTRY_COMPARE_WITH_TIMEOUT(
            thumbnailImage->property("status").toInt(), 1, 5000);

        // Every presentation owns only its active visual subtree. Masonry
        // and Grid use the shader path; Icons and the compact modes use one
        // direct Image and must not retain a hidden shader subtree.
        const int padMode = rootObject->property("imagePadMode").toInt();
        const int cropMode = rootObject->property("imageCropMode").toInt();
        const int fitMode = rootObject->property("imageFitMode").toInt();
        constexpr qreal sourceAspect = 320.0 / 120.0;
        const QList<QPair<QString, int>> modes{
            {QStringLiteral("masonry"), cropMode},
            {QStringLiteral("grid"), fitMode},
            {QStringLiteral("icons"), fitMode},
            {QStringLiteral("columns"), fitMode},
            {QStringLiteral("details"), fitMode},
        };
        for (const auto &[mode, scaledFillMode] : modes) {
            panel->setProperty("presentationMode", mode);
            QTRY_COMPARE_WITH_TIMEOUT(
                panel->property("presentationMode").toString(), mode, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(
                !panel->property("presentationSwitchPending").toBool(),
                3000);

            const auto activeVisual = [rootObject, &view](
                                          const QString &objectName)
                -> QQuickItem * {
                const auto candidates = rootObject->findChildren<QQuickItem *>(
                    objectName, Qt::FindChildrenRecursively);
                for (QQuickItem *candidate : candidates) {
                    // Loader replacement uses deleteLater(). The outgoing
                    // subtree can still be discoverable as a QObject during
                    // this event turn, but only the committed visual remains
                    // attached to the active scene graph window.
                    if (candidate->window() == &view) {
                        return candidate;
                    }
                }
                return nullptr;
            };
            QQuickItem *modeThumbnail = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (modeThumbnail = activeVisual(
                     QStringLiteral("galleryThumbnail-0")))
                    && modeThumbnail->isVisible(),
                3000);
            auto *modeImage = activeVisual(
                QStringLiteral("galleryThumbnailImage-0"));
            QVERIFY(modeImage);
            QTRY_COMPARE_WITH_TIMEOUT(
                modeImage->property("status").toInt(), 1, 5000);
            QCOMPARE(modeImage->property("asynchronous").toBool(),
                     false);
            QCOMPARE(modeImage->property("cache").toBool(), false);
            QVERIFY(qAbs(modeThumbnail->width() * 2 -
                         qRound(modeThumbnail->width() * 2)) <= 0.001);
            QVERIFY(qAbs(modeThumbnail->height() * 2 -
                         qRound(modeThumbnail->height() * 2)) <= 0.001);
            QVERIFY(!modeThumbnail->property("source").toUrl().isEmpty());

            const bool shaderMode = mode == QStringLiteral("masonry")
                || mode == QStringLiteral("grid");
            auto *modeShader = activeVisual(
                QStringLiteral("galleryThumbnailShader-0"));
            auto *modeBackdrop = activeVisual(
                QStringLiteral("galleryThumbnailBackdrop-0"));
            if (shaderMode) {
                QVERIFY(modeShader);
                QVERIFY(modeBackdrop);
                QTRY_VERIFY_WITH_TIMEOUT(modeShader->isVisible(), 3000);
                QVERIFY(!modeImage->isVisible());
                QCOMPARE(modeShader->property("source").value<QObject *>(),
                         static_cast<QObject *>(modeImage));
                QCOMPARE(modeShader->property(
                             "showCheckerboard").toBool(), true);
                QCOMPARE(modeBackdrop->property(
                             "enabledForPresentation").toBool(), true);
                QTRY_VERIFY_WITH_TIMEOUT(
                    modeImage->property("fillMode").toInt() ==
                        (modeImage->property("diffIsSmall").toBool()
                             ? padMode : scaledFillMode),
                    3000);
            } else {
                QVERIFY(!modeShader);
                QTRY_VERIFY_WITH_TIMEOUT(modeImage->isVisible(), 3000);
                QCOMPARE(modeImage->property("fillMode").toInt(), fitMode);
                // The one shared preview primitive survives presentation
                // changes. Compact modes retain its lightweight backdrop
                // object but never enable or paint it.
                QVERIFY(modeBackdrop);
                QVERIFY(!modeBackdrop->property(
                             "enabledForPresentation").toBool());
                QVERIFY(!modeBackdrop->isVisible());
            }
            if (mode == QStringLiteral("grid")) {
                QVERIFY(modeShader->height() > 0);
                const qreal renderedAspect = modeShader->width()
                    / modeShader->height();
                const qreal decodedAspect = modeImage->implicitWidth()
                    / modeImage->implicitHeight();
                QVERIFY2(qAbs(renderedAspect - decodedAspect) < 0.001
                             && qAbs(decodedAspect - sourceAspect) < 0.02,
                         qPrintable(QStringLiteral(
                             "%1 shader %2x%3 aspect %4; source image "
                             "implicit %5x%6")
                             .arg(mode)
                             .arg(modeShader->width())
                             .arg(modeShader->height())
                             .arg(renderedAspect)
                             .arg(modeImage->implicitWidth())
                             .arg(modeImage->implicitHeight())));
            }
        }
        panel->setProperty("presentationMode", QStringLiteral("details"));
        QTRY_VERIFY_WITH_TIMEOUT(
            (thumbnail = rootObject->findChild<QQuickItem *>(
                 QStringLiteral("galleryThumbnail-0")))
                && thumbnail->isVisible(),
            3000);
        QVERIFY(thumbnail->parentItem());
        auto *detailsSlot = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryDetailsIconSlot-0"));
        QVERIFY(detailsSlot);
        const QPointF thumbnailPosition = thumbnail->mapToItem(
            detailsSlot->parentItem(), QPointF());
        const QPointF slotPosition = detailsSlot->mapToItem(
            detailsSlot->parentItem(), QPointF());
        QVERIFY(QLineF(thumbnailPosition, slotPosition).length() < 0.01);
        QCOMPARE(thumbnail->width(), detailsSlot->width());
        QCOMPARE(thumbnail->height(), detailsSlot->height());
        auto *detailsIcon = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryFallbackIcon-0"));
        QVERIFY(detailsIcon);
        QTRY_VERIFY_WITH_TIMEOUT(!detailsIcon->isVisible(), 3000);
        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        QTRY_VERIFY_WITH_TIMEOUT(
            (thumbnail = rootObject->findChild<QQuickItem *>(
                 QStringLiteral("galleryThumbnail-0")))
                && thumbnail->isVisible(),
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (thumbnailShader = rootObject->findChild<QQuickItem *>(
                 QStringLiteral("galleryThumbnailShader-0")))
                && thumbnailShader->isVisible(),
            3000);

        QVariant geometry;
        QVERIFY(QMetaObject::invokeMethod(rootObject, "tileGeometry",
                                          Q_RETURN_ARG(QVariant, geometry)));
        const QRectF rect = geometry.toRectF();
        QVERIFY(rect.width() > 1);
        QVERIFY(rect.height() > 1);

        auto refreshTileSource = [&]() {
            if (!QMetaObject::invokeMethod(rootObject, "captureTileSource"))
                return false;
            return !rootObject->property("capturedTileSource")
                        .toString().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(refreshTileSource(), 5000);

        QVERIFY(QMetaObject::invokeMethod(rootObject, "suppressTile"));
        QTRY_VERIFY(!thumbnail->isVisible());
        QVERIFY(panel->property("viewerTransitionActive").toBool());
        QCOMPARE(panel->property("viewerTransitionEntryId").toString(),
                 QStringLiteral("tile"));
        QVERIFY(QMetaObject::invokeMethod(rootObject, "revealTile"));
        QTRY_VERIFY(thumbnail->isVisible());

        runtime->shutdown();
    }

    void customContentKeepsStandaloneInteractionSurfaceAuthoritative() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);

        QObject *rootObject = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryViewer {
                id: viewer
                width: 500
                height: 300
                property int wrapperCloseCount: 0
                property bool ownsEscapeThroughWrapper:
                    ownsKey({ key: Qt.Key_Escape, modifiers: Qt.NoModifier })
                function askWrapperToClose() { requestClose() }
                onCloseCompleted: wrapperCloseCount++
                customContent: legacySurface
                Item {
                    id: legacySurface
                    objectName: "legacySurface"
                    anchors.fill: parent
                    property string authorityMarker: "standalone"
                }
            }
        )QML", QStringLiteral("GalleryViewerCustomContent.qml"));
        QVERIFY(rootObject);
        view.show();

        auto *legacy = rootObject->findChild<QQuickItem *>(
            QStringLiteral("legacySurface"));
        QVERIFY(legacy);
        QVERIFY(legacy->isVisible());
        QCOMPARE(legacy->property("authorityMarker").toString(),
                 QStringLiteral("standalone"));
        QVERIFY(!rootObject->property("ownsEscapeThroughWrapper").toBool());
        QVERIFY(QMetaObject::invokeMethod(rootObject, "askWrapperToClose"));
        QTest::qWait(200);
        QCOMPARE(rootObject->property("wrapperCloseCount").toInt(), 0);
        QCOMPARE(rootObject->property("transitionProgress").toReal(), 0.0);

        runtime->shutdown();
    }
};

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
void GalleryQmlInteractionTest::videoSourceInitializationWaitsForViewerExpandAnimation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString contactSheetPath = directory.filePath(
            QStringLiteral("contact-sheet.png"));
        const QString posterPath = directory.filePath(
            QStringLiteral("poster-frame.png"));
        QVERIFY(writeImage(contactSheetPath, QSize(12, 8), QColor("blue")));
        QVERIFY(writeImage(posterPath, QSize(12, 8), QColor("green")));
        const QUrl contactSheetUrl = QUrl::fromLocalFile(contactSheetPath);
        const QUrl posterUrl = QUrl::fromLocalFile(posterPath);

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("deferredVideoContactSheetSource"), contactSheetUrl);
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("deferredVideoPosterSource"), posterUrl);
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
#ifndef Q_MOC_RUN
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 640; height: 420
                QtObject {
                    id: playback
                    objectName: "deferredVideoPlaybackController"
                    property string state: "idle"
                    property string error: ""
                    property int position: 0
                    property int duration: 10000
                    property bool playing: false
                    property bool muted: true
                    property real volume: 1
                    property int openSourceCount: 0
                    property string lastOpenedIdentity: ""
                    property var outputSink: null
                    signal changed()
                    function setPresentationVisible(value) {}
                    function setOutputSink(value) { outputSink = value }
                    function openSource(source, mode) {
                        ++openSourceCount
                        lastOpenedIdentity = source.identity
                        state = "loading"
                        changed()
                    }
                    function stop() { state = "idle"; changed() }
                    function playPause() {}
                    function seekTo(value) {}
                    function toggleMute() {}
                    function adjustVolume(value) {}
                }
                QtObject {
                    id: videoSession
                    objectName: "deferredVideoSession"
                    property int currentIndex: 0
                    property string cursorEntryId: "video-a"
                    property int catalogRevision: 1
                    property bool videoPlaybackAvailable: true
                    property url posterSourceValue: ""
                    property var videoPlaybackController: playback
                    signal viewerSourceAtChanged(int index)
                    function entryIdAt(index) { return index === 0 ? "video-a" : "" }
                    function indexForEntryId(entryId) { return entryId === "video-a" ? 0 : -1 }
                    function isVideoAt(index) { return index === 0 }
                    function isImageAt(index) { return false }
                    function videoSourceAt(index) {
                        return index === 0 ? {
                            identity: "video-a",
                            thumbnailSource: deferredVideoContactSheetSource,
                            posterSource: videoSession.posterSourceValue
                        } : ({})
                    }
                    function viewerRequestStateAt(index) { return "idle" }
                }
                Item {
                    id: sourcePanel
                    objectName: "deferredVideoSourcePanel"
                    width: 300; height: 200
                    property bool viewerTransitionActive: false
                    property string viewerTransitionEntryId: ""
                    function currentItemImageGeometry(targetItem) {
                        const point = targetItem.mapFromItem(sourcePanel, 30, 40)
                        return Qt.rect(point.x, point.y, 140, 90)
                    }
                    function currentItemImageSource() {
                        return deferredVideoContactSheetSource
                    }
                }
                GalleryViewer {
                    id: viewer
                    objectName: "deferredVideoViewer"
                    anchors.fill: parent
                    session: videoSession
                    sourcePanel: sourcePanel
                    animationDuration: 320
                    autoFocus: false
                    videoPlaybackMode: "manual"
                    videoIconSources: ({
                        play: "", pause: "", muted: "", sound: ""
                    })
                }
            }
        )QML", QStringLiteral("GalleryVideoPlaybackDeferred.qml"));
#else
        QObject *root = nullptr;
#endif
        QVERIFY(root);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto *viewer = root->findChild<QObject *>(
            QStringLiteral("deferredVideoViewer"));
        auto *playback = root->findChild<QObject *>(
            QStringLiteral("deferredVideoPlaybackController"));
        auto *videoSession = root->findChild<QObject *>(
            QStringLiteral("deferredVideoSession"));
        auto *animation = root->findChild<QObject *>(
            QStringLiteral("galleryViewerTransitionAnimation"));
        auto *videoLoader = root->findChild<QObject *>(
            QStringLiteral("galleryVideoPlaybackLoader"));
        auto *baseImage = root->findChild<QObject *>(
            QStringLiteral("galleryViewerBaseImage"));
        QVERIFY(viewer);
        QVERIFY(videoSession);
        auto *sourcePanel = root->findChild<QObject *>(
            QStringLiteral("deferredVideoSourcePanel"));
        QVERIFY(playback);
        QVERIFY(animation);
        QVERIFY(videoLoader);
        QVERIFY(baseImage);
        QVERIFY(!viewer->property(
            "videoPlaybackInitializationAllowed").toBool());
        QVERIFY(sourcePanel);
        QVariant panelThumbnail;
        QVERIFY(QMetaObject::invokeMethod(
            sourcePanel, "currentItemImageSource",
            Q_RETURN_ARG(QVariant, panelThumbnail)));
        QCOMPARE(panelThumbnail.toUrl(), contactSheetUrl);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionThumbnailSource").toUrl(),
            contactSheetUrl, 1000);
        QVERIFY(videoSession->setProperty("posterSourceValue", posterUrl));
        QCOMPARE(videoSession->property("posterSourceValue").toUrl(),
                 posterUrl);
        QVariant videoSourceValue;
        QVERIFY(QMetaObject::invokeMethod(
            videoSession, "videoSourceAt", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, videoSourceValue),
            Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(videoSourceValue.toMap()
                     .value(QStringLiteral("posterSource")).toUrl(),
                 posterUrl);
        QVERIFY(QMetaObject::invokeMethod(
            videoSession, "viewerSourceAtChanged", Qt::DirectConnection,
            Q_ARG(int, 0)));
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("currentVideoPosterSourceValue").toUrl(),
            posterUrl, 1000);
        QVERIFY(viewer->property("transitionHasGeometry").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionThumbnailSource").toUrl(),
            posterUrl, 1000);
        QCOMPARE(viewer->property("currentSourceValue").toUrl(),
                 posterUrl);
        QCOMPARE(baseImage->property("source").toUrl(), posterUrl);
        QCOMPARE(sourcePanel->property("viewerTransitionActive").toBool(), true);
        QVERIFY(!videoLoader->property("active").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(animation->property("running").toBool(), 1000);
        QTest::qWait(100);
        QVERIFY(animation->property("running").toBool());
        QCOMPARE(playback->property("openSourceCount").toInt(), 0);
        QCOMPARE(playback->property("state").toString(), QStringLiteral("idle"));

        QTRY_VERIFY_WITH_TIMEOUT(!animation->property("running").toBool(), 1500);
        QVERIFY(viewer->property(
            "videoPlaybackInitializationAllowed").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(videoLoader->property("active").toBool(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(
            playback->property("openSourceCount").toInt(), 1, 1000);
        QCOMPARE(playback->property("lastOpenedIdentity").toString(),
                 QStringLiteral("video-a"));
        QCOMPARE(playback->property("state").toString(),
                 QStringLiteral("loading"));
        runtime->shutdown();
    }
#endif

QTEST_MAIN(GalleryQmlInteractionTest)
#include "GalleryQmlInteractionTest.moc"
