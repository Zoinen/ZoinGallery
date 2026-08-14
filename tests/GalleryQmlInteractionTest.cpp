#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1500);
        QTRY_VERIFY(!viewer->property("transitioning").toBool());

        QVERIFY(QMetaObject::invokeMethod(viewer, "beginOpen"));
        QTRY_VERIFY_WITH_TIMEOUT(animation->property("running").toBool(), 1000);
        viewer->setProperty("transitionProgress", 0.25);
        QCOMPARE(viewer->property("transitionProgress").toReal(), 0.25);
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        QVERIFY(viewer->property("transitioning").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_VERIFY(!viewer->property("transitioning").toBool());

        QVERIFY(QMetaObject::invokeMethod(viewer, "requestClose"));
        QTRY_VERIFY_WITH_TIMEOUT(animation->property("running").toBool(), 1000);
        viewer->setProperty("transitionProgress", 0.75);
        QCOMPARE(viewer->property("transitionProgress").toReal(), 0.75);
        QVERIFY(QMetaObject::invokeMethod(animation, "stop"));
        QVERIFY(viewer->property("transitioning").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(rootObject->property("closeCount").toInt(),
                                  1, 1000);
        QCOMPARE(viewer->property("transitionProgress").toReal(), 0.0);
        QVERIFY(!viewer->property("viewerContentVisible").toBool());
        QTRY_VERIFY(!viewer->property("transitioning").toBool());
        QTest::qWait(500);
        QCOMPARE(rootObject->property("closeCount").toInt(), 1);
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

        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewport->property("imageTextureReady").toBool(), 5000);
        viewer->forceActiveFocus();

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

        // Ordinary close animates this same viewport container to the tile;
        // there is no second thumbnail overlay that can cross-fade or blink.
        QVERIFY(QMetaObject::invokeMethod(rootObject, "closeOrdinary"));
        QVERIFY(viewer->property("transitionHasGeometry").toBool());
        QCOMPARE(viewer->property("transitionSourceGeometry").toRectF().width(),
                 120.0);
        QTest::qWait(25);
        QCOMPARE(rootObject->property("closeCount").toInt(), 0);
        QTRY_VERIFY(viewport->width() < 640);
        QVERIFY(viewport->width() > 120);
        QTRY_COMPARE_WITH_TIMEOUT(rootObject->property("closeCount").toInt(),
                                  1, 1000);
        QVERIFY(!viewer->property("viewerContentVisible").toBool());
        QVERIFY(QMetaObject::invokeMethod(rootObject, "reopenViewer"));
        QTRY_COMPARE_WITH_TIMEOUT(
            viewer->property("transitionProgress").toReal(), 1.0, 1000);
        QTRY_VERIFY(viewer->property("viewerContentVisible").toBool());
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
            QStringLiteral("galleryMasonryLayout"));
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
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->property("count").toInt(), 30, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->property("contentHeight").toReal() > layout->height(), 5000);

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
                function commitNextSwipe() {
                    viewer.beginViewerNavigation(1)
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

        const qreal initialPan = sphericViewer->property("pan").toReal();
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(260, 180));
        QTest::mouseMove(&view, QPoint(350, 235), 20);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                            QPoint(350, 235));
        QTRY_VERIFY(qAbs(sphericViewer->property("pan").toReal()
                         - initialPan) > 0.001);
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
            Q_ARG(QVariant, QVariant(0.0)),
            Q_ARG(QVariant, QVariant(0.0))));
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

        // Every renderer shares one source/effect pair. Masonry retains its
        // justified Crop behavior; every fixed presentation keeps the whole
        // source visible with PreserveAspectFit.
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
            QTRY_VERIFY_WITH_TIMEOUT(thumbnailShader->isVisible(), 3000);
            QVERIFY(!thumbnailImage->isVisible());
            QCOMPARE(thumbnailImage->property("asynchronous").toBool(),
                     false);
            QCOMPARE(thumbnailImage->property("cache").toBool(), false);
            QCOMPARE(thumbnailShader->property("source").value<QObject *>(),
                     static_cast<QObject *>(thumbnailImage));
            // Icons now uses its whole preview area directly, without the
            // dark card/checkerboard treatment reserved for Grid/Masonry.
            const bool largePreviewMode = mode == QStringLiteral("masonry")
                || mode == QStringLiteral("grid");
            QCOMPARE(thumbnailShader->property("showCheckerboard").toBool(),
                     largePreviewMode);
            QCOMPARE(thumbnailBackdrop->property(
                         "enabledForPresentation").toBool(),
                     largePreviewMode);
            QVERIFY(qAbs(thumbnail->width() * 2 -
                         qRound(thumbnail->width() * 2)) <= 0.001);
            QVERIFY(qAbs(thumbnail->height() * 2 -
                         qRound(thumbnail->height() * 2)) <= 0.001);
            QTRY_VERIFY_WITH_TIMEOUT(
                thumbnailImage->property("fillMode").toInt() ==
                    (thumbnailImage->property("diffIsSmall").toBool()
                         ? padMode : scaledFillMode),
                3000);
            if (mode != QStringLiteral("masonry")) {
                QTRY_VERIFY_WITH_TIMEOUT(thumbnailShader->height() > 0,
                                         3000);
                const qreal renderedAspect = thumbnailShader->width()
                    / thumbnailShader->height();
                const qreal decodedAspect = thumbnailImage->implicitWidth()
                    / thumbnailImage->implicitHeight();
                QVERIFY2(qAbs(renderedAspect - decodedAspect) < 0.001
                             && qAbs(decodedAspect - sourceAspect) < 0.02,
                         qPrintable(QStringLiteral(
                             "%1 shader %2x%3 aspect %4; source image "
                             "implicit %5x%6")
                             .arg(mode)
                             .arg(thumbnailShader->width())
                             .arg(thumbnailShader->height())
                             .arg(renderedAspect)
                             .arg(thumbnailImage->implicitWidth())
                             .arg(thumbnailImage->implicitHeight())));
            }
            QVERIFY(!thumbnail->property("source").toUrl().isEmpty());
        }
        panel->setProperty("presentationMode", QStringLiteral("details"));
        QTRY_VERIFY_WITH_TIMEOUT(thumbnail->isVisible(), 3000);
        QVERIFY(thumbnailShader->isVisible());
        QVERIFY(thumbnail->parentItem());
        QVERIFY(thumbnail->parentItem()->parentItem());
        QCOMPARE(thumbnail->parentItem()->parentItem()->objectName(),
                 QStringLiteral("galleryDetailsIconSlot-0"));
        auto *detailsIcon = rootObject->findChild<QQuickItem *>(
            QStringLiteral("galleryFallbackIcon-0"));
        QVERIFY(detailsIcon);
        QTRY_VERIFY_WITH_TIMEOUT(!detailsIcon->isVisible(), 3000);
        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        QTRY_VERIFY_WITH_TIMEOUT(thumbnailShader->isVisible(), 3000);

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

QTEST_MAIN(GalleryQmlInteractionTest)
#include "GalleryQmlInteractionTest.moc"
