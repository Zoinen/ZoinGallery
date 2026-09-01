#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GalleryCatalogModel.h>
#include <ZoinGallery/GalleryPanelController.h>
#include <ZoinGallery/GallerySession.h>

#include "DummyQWK.h"
#include "FileListModel.h"
#include "GalleryViewModel.h"
#include "MainWindow.h"
#include "MasonryLayout.h"
#include "ViewerController.h"
#include "ViewerWheelArea.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QWindow>
#include <QtTest>

#include <cmath>
#include <functional>
#include <utility>

namespace {

bool writeImage(const QString &path, const QSize &size,
                const QColor &color) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image.save(path);
}

bool isDescendantOf(const QQuickItem *item, const QQuickItem *ancestor) {
    for (const QQuickItem *candidate = item; candidate;
         candidate = candidate->parentItem()) {
        if (candidate == ancestor) {
            return true;
        }
    }
    return false;
}

int indexForPath(const MasonryLayout *layout, const QString &path) {
    if (!layout) {
        return -1;
    }
    const QString wanted = QFileInfo(path).canonicalFilePath();
    for (int index = 0; index < layout->count(); ++index) {
        const QString candidate =
            QFileInfo(layout->indexFullPath(index)).canonicalFilePath();
        if (!wanted.isEmpty() && candidate == wanted) {
            return index;
        }
    }
    return -1;
}

bool observedRunning(const QSignalSpy &spy) {
    for (const QList<QVariant> &arguments : spy) {
        if (!arguments.isEmpty() && arguments.constFirst().toBool()) {
            return true;
        }
    }
    return false;
}

bool waitFor(const std::function<bool()> &predicate, int timeout = 3000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout) {
        QTest::qWait(5);
    }
    return predicate();
}

bool approximately(qreal actual, qreal expected, qreal tolerance = 0.025) {
    return std::abs(actual - expected) <= tolerance;
}

// QTest::keyClick synthesizes physical modifier-key presses around a modified
// key.  ViewerMode deliberately treats a physical Control press/release as
// part of its continuous held-key state machine, so that convenience API can
// stop a 150 ms Ctrl+digit zoom before it reaches its target.  Send the exact
// modified key events for shortcut assertions; continuous held-key motion is
// covered independently below.
void sendModifiedKeyClick(QWindow *window, Qt::Key key,
                          Qt::KeyboardModifiers modifiers) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(window, &release);
}

void sendTrackpadWheel(ViewerWheelArea *wheelArea, const QPoint &pixelDelta,
                       Qt::ScrollPhase phase) {
    const QPointF localPosition(wheelArea->width() * 0.5,
                                wheelArea->height() * 0.5);
    const QPointF scenePosition = wheelArea->mapToScene(localPosition);
    const QPointF globalPosition =
        wheelArea->window()->mapToGlobal(scenePosition.toPoint());
    QWheelEvent event(localPosition, globalPosition, pixelDelta, QPoint(),
                      Qt::NoButton, Qt::NoModifier, phase, false,
                      Qt::MouseEventNotSynthesized);
    QCoreApplication::sendEvent(wheelArea, &event);
}

} // namespace

class StandaloneShellSmokeTest final : public QObject {
    Q_OBJECT

public:
    explicit StandaloneShellSmokeTest(QString sandboxRoot)
        : _sandboxRoot(std::move(sandboxRoot)) {}

private slots:
    void loadsProductionShellAndKeepsLegacySurfacesAuthoritative() {
        const QString galleryPath =
            QDir(_sandboxRoot).filePath(QStringLiteral("gallery"));
        QVERIFY(QDir().mkpath(galleryPath));
        const QString firstImagePath =
            QDir(galleryPath).filePath(QStringLiteral("first.png"));
        const QString secondImagePath =
            QDir(galleryPath).filePath(QStringLiteral("second.png"));
        const QString thirdImagePath =
            QDir(galleryPath).filePath(QStringLiteral("third.png"));
        const QString fourthImagePath =
            QDir(galleryPath).filePath(QStringLiteral("fourth.png"));
        const QString albumPath =
            QDir(galleryPath).filePath(QStringLiteral("album"));
        const QString albumImagePath =
            QDir(albumPath).filePath(QStringLiteral("nested.png"));
        // Keep one image larger than the deterministic test window in both
        // dimensions.  That makes all four held-arrow pan directions
        // observable at 100%, instead of merely checking internal flags.
        QVERIFY(writeImage(firstImagePath, QSize(1600, 1200), Qt::magenta));
        QVERIFY(writeImage(secondImagePath, QSize(800, 1200), Qt::cyan));
        QVERIFY(writeImage(thirdImagePath, QSize(1200, 800), Qt::yellow));
        QVERIFY(writeImage(fourthImagePath, QSize(720, 720), Qt::green));

        // ViewerController and the QtCore Settings singleton use the normal
        // standalone QSettings API. The test keeps that API intact while its
        // process-wide INI root and application identity point at the sandbox.
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("currentPath"), galleryPath);
        settings.beginGroup(QStringLiteral("General"));
        settings.setValue(QStringLiteral("singleInstanceByDefault"), false);
        settings.setValue(QStringLiteral("animateResizing"), false);
        settings.endGroup();
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        QQuickStyle::setStyle(QStringLiteral("ZGStyle"));
        QImageReader::setAllocationLimit(0);
        ZoinGallery::GalleryRuntime::registerTypes();
        qmlRegisterType<MainWindow>("ZoinGallery.MainWindow", 1, 0,
                                    "MainWindow");
        qmlRegisterRevision<QWindow, 1>("ZoinGallery.MainWindow", 1, 0);
        qmlRegisterRevision<QQuickWindow, 1>("ZoinGallery.MainWindow", 1, 0);

        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        engine.addImportPath(QStringLiteral(":"));
        engine.addImportPath(QStringLiteral(":/ZoinGallery"));
        engine.rootContext()->setContextProperty(
            QStringLiteral("zoinVersion"), QStringLiteral("shell-smoke"));
        engine.rootContext()->setContextProperty(
            QStringLiteral("zoinBuildNumber"), QStringLiteral("test"));

        ZoinGallery::RuntimeOptions options;
        options.providerPrefix = QStringLiteral("standalone-shell-smoke");
        options.storageNamespace = QStringLiteral("standalone-shell-smoke-%1")
            .arg(QCoreApplication::applicationPid());
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(&engine, options);
        QVERIFY(runtime);
        auto *session = runtime->createSession(
            QStringLiteral("standalone-shell-smoke"));
        QVERIFY(session);
        QVERIFY(session->localSource());

        auto *controller = new ViewerController(&engine, *session, *runtime);
        QVERIFY(controller);
        DummyQWK::registerTypes(&engine);

        const QUrl shellUrl(QStringLiteral(
            "qrc:/ZoinGallery/qml/main.qml"));
        QSignalSpy objectCreatedSpy(
            &engine, &QQmlApplicationEngine::objectCreated);
        engine.load(shellUrl);
        QVERIFY2(!engine.rootObjects().isEmpty(),
                 "The production qml/main.qml did not create a root object");
        QCOMPARE(objectCreatedSpy.size(), 1);

        auto *window = qobject_cast<MainWindow *>(
            engine.rootObjects().constFirst());
        QVERIFY(window);
        QCOMPARE(window->objectName(), QStringLiteral("standaloneMainWindow"));
        window->resize(960, 720);
        window->show();
        window->requestActivate();

        auto *shellContent = window->findChild<QQuickItem *>(
            QStringLiteral("standaloneShellContent"));
        auto *galleryPanel = window->findChild<QQuickItem *>(
            QStringLiteral("standaloneGalleryPanel"));
        auto *masonryMode = window->findChild<QQuickItem *>(
            QStringLiteral("standaloneGalleryFacade"));
        auto *galleryViewer = window->findChild<QQuickItem *>(
            QStringLiteral("standaloneGalleryViewer"));
        auto *viewerMode = window->findChild<QQuickItem *>(
            QStringLiteral("standaloneViewerMode"));
        QVERIFY(shellContent);
        QVERIFY(galleryPanel);
        QVERIFY(masonryMode);
        QVERIFY(galleryViewer);
        QVERIFY(viewerMode);

        // The standalone shell now uses the same typed panel facade as the
        // embedded host.  Keep the viewer on its legacy path until the later
        // viewer-pipeline milestone, but assert that thumbnails are no longer
        // routed through a hidden standalone renderer.
        QVERIFY(isDescendantOf(masonryMode, galleryPanel));
        QVERIFY(masonryMode->property("controller").value<QObject *>());
        QCOMPARE(galleryViewer->property("customContent").value<QObject *>(),
                 viewerMode);
        QCOMPARE(shellContent->property("state").toString(),
                 QStringLiteral("thumbnails"));

        auto *layout = masonryMode->findChild<MasonryLayout *>();
        auto *fileListModel =
            qobject_cast<FileListModel *>(session->fileListModel());
        auto *galleryViewModel =
            qobject_cast<GalleryViewModel *>(session->galleryViewModel());
        QVERIFY(layout);
        QVERIFY(fileListModel);
        QVERIFY(galleryViewModel);
        // The top-level standalone collection is adapted through the fixed
        // role catalog contract, while retaining GalleryViewModel as the
        // zero-copy source. It must not become a nested folder-card model.
        auto *catalogModel =
            qobject_cast<ZoinGallery::GalleryCatalogModel *>(layout->model());
        QVERIFY(catalogModel);
        QCOMPARE(catalogModel->sourceModel(),
                 static_cast<QAbstractItemModel *>(galleryViewModel));
        QCOMPARE(catalogModel->rootItem(), nullptr);
        QCOMPARE(galleryViewModel->rootItem(), nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(
            controller->property("currentPath").toString(), galleryPath,
            10000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->count() >= 4, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(window->activeFocusItem(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            isDescendantOf(window->activeFocusItem(), masonryMode), 5000);

        QTRY_VERIFY_WITH_TIMEOUT(
            indexForPath(layout, firstImagePath) >= 0, 10000);
        const int imageIndex = indexForPath(layout, firstImagePath);
        QVERIFY(imageIndex >= 0);
        QVERIFY(QMetaObject::invokeMethod(
            masonryMode, "setCurrentIndex",
            Q_ARG(QVariant, QVariant(imageIndex)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false))));
        QVERIFY(QMetaObject::invokeMethod(masonryMode, "focusView"));
        QTRY_COMPARE(layout->currentIndex(), imageIndex);
        QTRY_VERIFY_WITH_TIMEOUT(layout->currentItem(), 5000);
        auto *panelController = qobject_cast<
            ZoinGallery::GalleryPanelController *>(
                masonryMode->property("controller").value<QObject *>());
        QVERIFY(panelController);
        QVERIFY(panelController->dragEnabled());
        QQuickItem *entryActions = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (entryActions = masonryMode->findChild<QQuickItem *>(
                 QStringLiteral("galleryEntryActions-%1").arg(imageIndex)))
                != nullptr,
            5000);
        QVERIFY(panelController->prepareDrag(imageIndex, true, 5));
        QCOMPARE(panelController->dragUrls().size(), 1);
        QVERIFY(panelController->dragPreviewModel()->rowCount() <= 5);
        panelController->finishExternalDrag(Qt::IgnoreAction);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->indexOriginalSize(imageIndex).width() > 1 &&
                layout->indexOriginalSize(imageIndex).height() > 1,
            10000);

        auto *viewerAnimation =
            viewerMode->property("animation").value<QObject *>();
        auto *viewerViewport =
            viewerMode->property("imageContainer").value<QObject *>();
        QVERIFY(viewerAnimation);
        QVERIFY(viewerViewport);
        auto *viewerImage = qobject_cast<QQuickItem *>(
            viewerViewport->property("image").value<QObject *>());
        QVERIFY(viewerImage);
        QSignalSpy viewerAnimationSpy(
            viewerAnimation, SIGNAL(runningChanged(bool)));
        QVERIFY(viewerAnimationSpy.isValid());

        // Enter is handled by the shared panel facade and starts the
        // thumbnail-to-viewer geometry animation.
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(shellContent->property("state").toString(),
                                  QStringLiteral("viewer"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(viewerMode->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            observedRunning(viewerAnimationSpy) ||
                viewerAnimation->property("running").toBool(),
            1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerAnimation->property("running").toBool(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            isDescendantOf(window->activeFocusItem(), viewerMode), 3000);

        const auto setViewIndex = [&](int index) {
            const bool invoked = QMetaObject::invokeMethod(
                masonryMode, "setCurrentIndex",
                Q_ARG(QVariant, QVariant(index)),
                Q_ARG(QVariant, QVariant(false)),
                Q_ARG(QVariant, QVariant(false)),
                Q_ARG(QVariant, QVariant(false)),
                Q_ARG(QVariant, QVariant(false)));
            return invoked && waitFor([&] {
                return layout->currentIndex() == index;
            });
        };
        const auto zoomToFitImmediately = [&] {
            const bool invoked = QMetaObject::invokeMethod(
                viewerViewport, "zoomToFit",
                Q_ARG(QVariant, QVariant(true)));
            return invoked && waitFor([&] {
                return viewerMode->property("zoomFitView").toBool() &&
                    !viewerViewport->property("viewportAnimationRunning")
                         .toBool();
            });
        };
        const auto waitForScale = [&](qreal expected) {
            return waitFor([&] {
                return approximately(
                           viewerViewport->property("zoomScale").toReal(),
                           expected) &&
                    !viewerViewport->property("viewportAnimationRunning")
                         .toBool();
            });
        };
        const auto viewerHasFocus = [&] {
            return isDescendantOf(window->activeFocusItem(), viewerMode);
        };

        // Resolve the image-list endpoints through MasonryLayout itself.  The
        // production view may use any sort mode and may contain non-images;
        // the key contract is expressed in image order, not raw row numbers.
        QVERIFY(setViewIndex(imageIndex));
        const int firstImageIndex = layout->nextImageIndex(false, true);
        const int lastImageIndex = layout->nextImageIndex(true, true);
        QVERIFY(firstImageIndex >= 0);
        QVERIFY(lastImageIndex >= 0);
        QVERIFY(firstImageIndex != lastImageIndex);
        QVERIFY(setViewIndex(firstImageIndex));
        const int middleImageIndex = layout->nextImageIndex(true, false);
        QVERIFY(middleImageIndex >= 0);
        QVERIFY(middleImageIndex != firstImageIndex);
        QVERIFY(setViewIndex(middleImageIndex));
        const int imageAfterMiddle = layout->nextImageIndex(true, false);
        QVERIFY(imageAfterMiddle >= 0);
        QVERIFY(imageAfterMiddle != middleImageIndex);

        // A two-finger swipe renders the neighboring image before it commits
        // the catalog cursor.  That reveal must select the Fit tier prepared
        // for this physical viewer, not whichever tier happened to be
        // requested most recently from the backend.  Prepare both Fit and
        // native tiers for a portrait neighbor, leave native as the last
        // backend request, and then enter the real phase-aware wheel path.
        const int swipeTargetIndex =
            indexForPath(layout, secondImagePath);
        QVERIFY(swipeTargetIndex >= 0);
        QVERIFY(setViewIndex(swipeTargetIndex));
        const int swipeStartIndex =
            layout->nextImageIndex(false, false);
        QVERIFY(swipeStartIndex >= 0);
        QVERIFY(swipeStartIndex != swipeTargetIndex);
        QVERIFY(setViewIndex(swipeStartIndex));
        QCOMPARE(layout->nextImageIndex(true, false), swipeTargetIndex);
        QVERIFY(zoomToFitImmediately());

        const int swipeTargetSourceIndex =
            galleryViewModel->mapToSourceRow(swipeTargetIndex);
        QVERIFY(swipeTargetSourceIndex >= 0);
        const qreal viewerDpr = window->property("dpr").toReal();
        QVERIFY(viewerDpr > 0);
        const QSize fitViewport(
            qMax(1, qRound(viewerMode->width() * viewerDpr)),
            qMax(1, qRound(viewerMode->height() * viewerDpr)));
        const QSize swipeTargetOriginalSize =
            layout->indexOriginalSize(swipeTargetIndex);
        QCOMPARE(swipeTargetOriginalSize, QSize(800, 1200));
        const QSize expectedFitSize = swipeTargetOriginalSize.scaled(
            fitViewport, Qt::KeepAspectRatio);
        QVERIFY(expectedFitSize != swipeTargetOriginalSize);

        fileListModel->requestViewerAt(
            swipeTargetSourceIndex, fitViewport.width(),
            fitViewport.height());
        QString preparedFitUrl;
        QVERIFY(waitFor([&] {
            preparedFitUrl =
                fileListModel->preparedViewerImageUrlForIndex(
                    swipeTargetSourceIndex, fitViewport.width(),
                    fitViewport.height());
            return !preparedFitUrl.isEmpty();
        }, 15000));
        const QString preparedFitImageId =
            preparedFitUrl.section(QLatin1Char('/'), -1);
        const QSize preparedFitFrameSize =
            fileListModel->viewerForImageId(preparedFitImageId).size();
        QVERIFY(preparedFitFrameSize.width() >= expectedFitSize.width());
        QVERIFY(preparedFitFrameSize.height() >= expectedFitSize.height());

        QTRY_VERIFY_WITH_TIMEOUT(
            !layout->indexImageIdUrl(swipeTargetIndex).isEmpty(), 10000);
        const QString levelZeroUrl =
            layout->indexImageIdUrl(swipeTargetIndex);
        QVERIFY(!levelZeroUrl.isEmpty());
        QVERIFY(preparedFitUrl != levelZeroUrl);

        // This second request deliberately changes the backend's last mode
        // without changing the viewer's visible Fit state.  Both cache tiers
        // must remain available, and the swipe must still choose the prepared
        // Fit frame rather than the native tier.
        fileListModel->requestViewerAt(swipeTargetSourceIndex, -1, -1);
        QString nativeUrl;
        QVERIFY(waitFor([&] {
            nativeUrl =
                fileListModel->preparedViewerImageUrlForIndex(
                    swipeTargetSourceIndex, -1, -1);
            if (nativeUrl.isEmpty()) {
                return false;
            }
            const QString imageId =
                nativeUrl.section(QLatin1Char('/'), -1);
            return fileListModel->fullSizeViewerForImageId(imageId).size()
                == swipeTargetOriginalSize;
        }, 15000));
        QVERIFY(nativeUrl != preparedFitUrl);
        // An already queued, equal-or-larger Fit decode may complete while
        // the native tier is prepared. Its provider URL is an implementation
        // detail; what the transition contract requires is a current level-1
        // frame covering this viewport alongside the native tier.
        preparedFitUrl =
            fileListModel->preparedViewerImageUrlForIndex(
                swipeTargetSourceIndex, fitViewport.width(),
                fitViewport.height());
        QVERIFY(!preparedFitUrl.isEmpty());
        const QSize currentPreparedFitFrameSize =
            fileListModel->viewerForImageId(
                preparedFitUrl.section(QLatin1Char('/'), -1)).size();
        QVERIFY(currentPreparedFitFrameSize.width() >= expectedFitSize.width());
        QVERIFY(currentPreparedFitFrameSize.height() >= expectedFitSize.height());
        QVERIFY(viewerMode->property("zoomFitView").toBool());
        QCOMPARE(layout->currentIndex(), swipeStartIndex);

        const QList<ViewerWheelArea *> viewerWheelAreas =
            viewerMode->findChildren<ViewerWheelArea *>();
        QCOMPARE(viewerWheelAreas.size(), 1);
        ViewerWheelArea *viewerWheelArea = viewerWheelAreas.constFirst();
        QVERIFY(viewerWheelArea->isEnabled());
        sendTrackpadWheel(viewerWheelArea, QPoint(), Qt::ScrollBegin);
        sendTrackpadWheel(viewerWheelArea, QPoint(-96, 0),
                          Qt::ScrollUpdate);
        QTRY_COMPARE(viewerMode->property("viewerNavigationTargetIndex")
                         .toInt(),
                     swipeTargetIndex);
        QTRY_COMPARE(viewerMode->property("viewerNavigationTargetSource")
                         .toString(),
                     preparedFitUrl);
        QVERIFY(viewerMode->property("viewerNavigationTargetSource")
                    .toString() != nativeUrl);
        QVERIFY(viewerMode->property("viewerNavigationTargetSource")
                    .toString() != levelZeroUrl);
        QCOMPARE(viewerMode->property("viewerNavigationTargetOriginalSize")
                     .toSize(),
                 swipeTargetOriginalSize);

        // Verify the transition surface itself, not just the cache entry and
        // URL selected for it. QQuickImage::sourceSize is the texture
        // factory's pixel size when sourceSize is not explicitly overridden,
        // so together with the coverage check above this catches a different
        // or lower-resolution image being installed in the scene graph.
        auto *swipeNeighborImage = viewerMode->findChild<QQuickItem *>(
            QStringLiteral("standaloneViewerNavigationNeighborImage"));
        QVERIFY(swipeNeighborImage);
        QTRY_COMPARE_WITH_TIMEOUT(
            swipeNeighborImage->property("source").toUrl(),
            QUrl(preparedFitUrl), 5000);
        // QQuickImageBase::Ready is 1. Waiting for it makes sourceSize an
        // assertion about the loaded transition texture, not its pending URL.
        QTRY_COMPARE_WITH_TIMEOUT(
            swipeNeighborImage->property("status").toInt(), 1, 5000);
        QCOMPARE(swipeNeighborImage->property("sourceSize").toSize(),
                 preparedFitFrameSize);
        QCOMPARE(swipeNeighborImage->property("mipmap").toBool(), false);
        QVERIFY(swipeNeighborImage->parentItem());
        QVERIFY(!swipeNeighborImage->isVisible());
        QVERIFY(swipeNeighborImage->parentItem()->isVisible());
        QVERIFY(swipeNeighborImage->parentItem()->opacity() > 0);

        // The decoded Image is intentionally hidden and serves only as the
        // texture provider. The visible swipe frame must go through the same
        // shader path as the committed viewer, with the shader operating in
        // physical pixels for the selected covering Fit texture.
        auto *swipeNeighborShader = viewerMode->findChild<QQuickItem *>(
            QStringLiteral("standaloneViewerNavigationNeighborShader"));
        QVERIFY(swipeNeighborShader);
        QCOMPARE(swipeNeighborShader->parentItem(),
                 swipeNeighborImage->parentItem());
        QCOMPARE(swipeNeighborShader->property("source").value<QObject *>(),
                 static_cast<QObject *>(swipeNeighborImage));
        QVERIFY(swipeNeighborShader->isVisible());
        QCOMPARE(swipeNeighborShader->x(), swipeNeighborImage->x());
        QCOMPARE(swipeNeighborShader->y(), swipeNeighborImage->y());
        QCOMPARE(swipeNeighborShader->width(), swipeNeighborImage->width());
        QCOMPARE(swipeNeighborShader->height(), swipeNeighborImage->height());
        QCOMPARE(swipeNeighborShader->property("sharpenAmount").toReal(),
                 1.5);
        const QSizeF expectedTransitionViewportSize(
            swipeNeighborShader->width() * viewerDpr,
            swipeNeighborShader->height() * viewerDpr);
        QCOMPARE(swipeNeighborShader->property("viewportSize").toSizeF(),
                 expectedTransitionViewportSize);
        QCOMPARE(swipeNeighborShader->property("fragmentShader").toUrl(),
                 QUrl(QStringLiteral(
                     "qrc:/ZoinGallery/resources/shader.frag.qsb")));
        QCOMPARE(layout->currentIndex(), swipeStartIndex);

        // Commit directly instead of feeding the deliberately high-velocity
        // synthetic gesture an End phase. The adopted setImage() call must
        // receive the target's native original size, not a bound QSize value
        // which becomes 0x0 when commit resets the navigation target.
        QVERIFY(QMetaObject::invokeMethod(viewerMode,
                                          "commitViewerNavigation"));
        QTRY_COMPARE(layout->currentIndex(), swipeTargetIndex);
        QCOMPARE(viewerMode->property(
                     "viewerNavigationLastAdoptedOriginalSize").toSize(),
                 swipeTargetOriginalSize);

        // Reset both gesture layers so the remaining standalone shortcut
        // checks start from clean state.
        QVERIFY(QMetaObject::invokeMethod(
            viewerMode, "resetViewerNavigation",
            Q_ARG(QVariant, QVariant(QStringLiteral("test cleanup")))));
        QVERIFY(QMetaObject::invokeMethod(
            viewerMode, "endViewerNavigationGesture",
            Q_ARG(QVariant, QVariant(true))));
        QTRY_VERIFY(!viewerMode->property("viewerNavigationActive").toBool());

        struct NavigationAlias {
            Qt::Key key;
            bool forward;
            const char *name;
        };
        const NavigationAlias navigationAliases[] = {
            {Qt::Key_Left, false, "Left"},
            {Qt::Key_PageUp, false, "PageUp"},
            {Qt::Key_Backspace, false, "Backspace"},
            {Qt::Key_Up, false, "Up"},
            {Qt::Key_Right, true, "Right"},
            {Qt::Key_PageDown, true, "PageDown"},
            {Qt::Key_Space, true, "Space"},
            {Qt::Key_Down, true, "Down"},
        };
        for (const NavigationAlias &alias : navigationAliases) {
            QVERIFY2(setViewIndex(middleImageIndex), alias.name);
            QVERIFY2(zoomToFitImmediately(), alias.name);
            const int expected =
                layout->nextImageIndex(alias.forward, false);
            QVERIFY2(expected != middleImageIndex, alias.name);
            QTest::keyClick(window, alias.key);
            const bool moved = waitFor([&] {
                return layout->currentIndex() == expected;
            });
            const QByteArray navigationFailure =
                QByteArray(alias.name) + " expected=" +
                QByteArray::number(expected) + " actual=" +
                QByteArray::number(layout->currentIndex());
            QVERIFY2(moved, navigationFailure.constData());
            QVERIFY2(shellContent->property("state").toString() ==
                         QStringLiteral("viewer"),
                     alias.name);
            QVERIFY2(viewerHasFocus(), alias.name);
        }

        QVERIFY(setViewIndex(middleImageIndex));
        QVERIFY(zoomToFitImmediately());
        QTest::keyClick(window, Qt::Key_Home);
        QVERIFY(waitFor([&] {
            return layout->currentIndex() == firstImageIndex;
        }));
        QVERIFY(setViewIndex(middleImageIndex));
        QVERIFY(zoomToFitImmediately());
        QTest::keyClick(window, Qt::Key_End);
        QVERIFY(waitFor([&] {
            return layout->currentIndex() == lastImageIndex;
        }));

        // The exact pre-split handler gives fitted PageUp navigation
        // precedence over its later Ctrl+PageUp close clause.  The latter is
        // therefore unreachable in git HEAD; preserving that observable
        // behavior here distinguishes parity testing from changing an old
        // shortcut policy during the component split.
        QVERIFY(setViewIndex(middleImageIndex));
        QVERIFY(zoomToFitImmediately());
        const int controlPageUpTarget =
            layout->nextImageIndex(false, false);
        sendModifiedKeyClick(window, Qt::Key_PageUp, Qt::ControlModifier);
        QVERIFY(waitFor([&] {
            return layout->currentIndex() == controlPageUpTarget;
        }));
        QCOMPARE(shellContent->property("state").toString(),
                 QStringLiteral("viewer"));

        // Exercise every fixed-scale alias and each Fit/100% toggle alias.
        struct ScaleAlias {
            Qt::Key key;
            Qt::KeyboardModifiers modifiers;
            qreal scale;
            const char *name;
        };
        const ScaleAlias scaleAliases[] = {
            {Qt::Key_Asterisk, Qt::NoModifier, 1.0, "Asterisk"},
            {Qt::Key_9, Qt::NoModifier, 1.0, "9"},
            {Qt::Key_1, Qt::ControlModifier, 1.0, "Ctrl+1"},
            {Qt::Key_2, Qt::ControlModifier, 0.5, "Ctrl+2"},
            {Qt::Key_3, Qt::ControlModifier, 0.25, "Ctrl+3"},
        };
        for (const ScaleAlias &alias : scaleAliases) {
            QVERIFY2(zoomToFitImmediately(), alias.name);
            if (alias.modifiers == Qt::NoModifier) {
                QTest::keyClick(window, alias.key);
            } else {
                sendModifiedKeyClick(window, alias.key, alias.modifiers);
            }
            const bool reachedScale = waitForScale(alias.scale);
            const QByteArray scaleFailure = QStringLiteral(
                "%1: scale=%2 target=%3 fit=%4 animation=%5 focus=%6")
                .arg(QString::fromLatin1(alias.name))
                .arg(viewerViewport->property("zoomScale").toReal())
                .arg(alias.scale)
                .arg(viewerMode->property("zoomFitView").toBool())
                .arg(viewerViewport->property("viewportAnimationRunning")
                         .toBool())
                .arg(viewerHasFocus())
                .toLocal8Bit();
            QVERIFY2(reachedScale, scaleFailure.constData());
            QVERIFY2(!viewerMode->property("zoomFitView").toBool(),
                     alias.name);
        }
        sendModifiedKeyClick(window, Qt::Key_0, Qt::ControlModifier);
        QVERIFY(waitFor([&] {
            return viewerMode->property("zoomFitView").toBool();
        }));

        const Qt::Key fitToggleAliases[] = {
            Qt::Key_Z, Qt::Key_Slash, Qt::Key_0,
        };
        for (Qt::Key key : fitToggleAliases) {
            QVERIFY(zoomToFitImmediately());
            QTest::keyClick(window, key);
            QVERIFY(waitForScale(1.0));
            QVERIFY(!viewerMode->property("zoomFitView").toBool());
            QTest::keyClick(window, key);
            QVERIFY(waitFor([&] {
                return viewerMode->property("zoomFitView").toBool();
            }));
        }

        // Tab and +/- have conflicting meanings in the thumbnail surface.
        // While ViewerMode owns focus they must mutate viewer-only state and
        // must not reach the hidden MasonryMode.
        const bool panelsBefore = viewerMode->property("panelsVisible").toBool();
        const bool namesBefore =
            masonryMode->property("alwaysShowFileNames").toBool();
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(viewerMode->property("panelsVisible").toBool(),
                     !panelsBefore);
        QCOMPARE(masonryMode->property("alwaysShowFileNames").toBool(),
                 namesBefore);

        QTRY_VERIFY_WITH_TIMEOUT(
            viewerViewport->property("originalSize").toSize().width() > 1 &&
                viewerViewport->property("originalSize").toSize().height() > 1,
            10000);
        const int thumbnailHeightBefore = layout->targetHeight();
        const qreal zoomBefore = viewerViewport->property("zoomScale").toReal();
        QTest::keyPress(window, Qt::Key_Plus);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewerViewport->property("zoomScale").toReal() > zoomBefore,
            3000);
        QTest::keyRelease(window, Qt::Key_Plus);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);
        QCOMPARE(layout->targetHeight(), thumbnailHeightBefore);
        QVERIFY(!viewerMode->property("zoomFitView").toBool());

        const qreal zoomAfterPlus =
            viewerViewport->property("zoomScale").toReal();
        QTest::keyPress(window, Qt::Key_Minus);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewerViewport->property("zoomScale").toReal() < zoomAfterPlus,
            3000);
        QCOMPARE(viewerMode->property("zoomOutPressed").toBool(), true);
        QTest::keyRelease(window, Qt::Key_Minus);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);
        QCOMPARE(viewerMode->property("zoomOutPressed").toBool(), false);

        const qreal zoomBeforeEqual =
            viewerViewport->property("zoomScale").toReal();
        QTest::keyPress(window, Qt::Key_Equal);
        QCOMPARE(viewerMode->property("zoomInPressed").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewerViewport->property("zoomScale").toReal() > zoomBeforeEqual,
            3000);
        QTest::keyRelease(window, Qt::Key_Equal);
        QCOMPARE(viewerMode->property("zoomInPressed").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);

        // At 100% the oversized image has room to move in both axes.  Verify
        // that held arrows drive the original FrameAnimation continuously and
        // that release clears its corresponding legacy state flag.
        QVERIFY(setViewIndex(imageIndex));
        QTRY_COMPARE_WITH_TIMEOUT(
            viewerViewport->property("originalSize").toSize(),
            QSize(1600, 1200), 10000);
        QVERIFY(zoomToFitImmediately());
        sendModifiedKeyClick(window, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(waitForScale(1.0));
        QTRY_VERIFY_WITH_TIMEOUT(
            viewerImage->width() > viewerViewport->property("width").toReal()
                && viewerImage->height() >
                    viewerViewport->property("height").toReal(),
            3000);

        const qreal xBeforeLeft = viewerImage->x();
        QTest::keyPress(window, Qt::Key_Left);
        QCOMPARE(viewerMode->property("leftPressed").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(viewerImage->x() > xBeforeLeft + 0.5, 3000);
        QTest::keyRelease(window, Qt::Key_Left);
        QCOMPARE(viewerMode->property("leftPressed").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);

        const qreal xBeforeRight = viewerImage->x();
        QTest::keyPress(window, Qt::Key_Right);
        QCOMPARE(viewerMode->property("rightPressed").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(viewerImage->x() < xBeforeRight - 0.5, 3000);
        QTest::keyRelease(window, Qt::Key_Right);
        QCOMPARE(viewerMode->property("rightPressed").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);

        const qreal yBeforeUp = viewerImage->y();
        QTest::keyPress(window, Qt::Key_Up);
        QCOMPARE(viewerMode->property("upPressed").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(viewerImage->y() > yBeforeUp + 0.5, 3000);
        QTest::keyRelease(window, Qt::Key_Up);
        QCOMPARE(viewerMode->property("upPressed").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);

        const qreal yBeforeDown = viewerImage->y();
        QTest::keyPress(window, Qt::Key_Down);
        QCOMPARE(viewerMode->property("downPressed").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(viewerImage->y() < yBeforeDown - 0.5, 3000);
        QTest::keyRelease(window, Qt::Key_Down);
        QCOMPARE(viewerMode->property("downPressed").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewerViewport->property("zoomScrollingAnimationRunning").toBool(),
            3000);

        // Selection commands remain ViewerMode commands in the production
        // shell, including its historic exclusive-target Shift range.
        fileListModel->setAllSelection(false);
        QTRY_COMPARE(fileListModel->selectedCount(), 0);
        QVERIFY(setViewIndex(middleImageIndex));
        const int middleSourceIndex =
            galleryViewModel->mapToSourceRow(middleImageIndex);
        QVERIFY(middleSourceIndex >= 0);
        QTest::keyClick(window, Qt::Key_Backslash);
        QTRY_VERIFY(fileListModel->isIndexSelected(middleSourceIndex));
        QTest::keyClick(window, Qt::Key_Backslash);
        QTRY_VERIFY(!fileListModel->isIndexSelected(middleSourceIndex));
        QTest::keyClick(window, Qt::Key_Insert);
        QTRY_VERIFY(fileListModel->isIndexSelected(middleSourceIndex));
        QTest::keyClick(window, Qt::Key_Insert);
        QCOMPARE(fileListModel->selectedCount(), 1);
        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_VERIFY(!fileListModel->isIndexSelected(middleSourceIndex));

        fileListModel->setAllSelection(false);
        QVERIFY(setViewIndex(firstImageIndex));
        const int secondRangeIndex = layout->nextImageIndex(true, false);
        QVERIFY(setViewIndex(secondRangeIndex));
        const int thirdRangeIndex = layout->nextImageIndex(true, false);
        QVERIFY(setViewIndex(firstImageIndex));
        QVERIFY(zoomToFitImmediately());
        QTest::keyPress(window, Qt::Key_Shift);
        QCOMPARE(viewerMode->property("shiftSelectionActive").toBool(), true);
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(waitFor([&] {
            return layout->currentIndex() == secondRangeIndex;
        }));
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(waitFor([&] {
            return layout->currentIndex() == thirdRangeIndex;
        }));
        QTest::keyRelease(window, Qt::Key_Shift);
        QTRY_VERIFY(!viewerMode->property("shiftSelectionActive").toBool());
        const QVariantList expectedShiftRange =
            galleryViewModel->sourceRowsForViewRange(
                firstImageIndex, thirdRangeIndex, false);
        QCOMPARE(fileListModel->selectedCount(), expectedShiftRange.size());
        for (const QVariant &sourceRow : expectedShiftRange) {
            QVERIFY(fileListModel->isIndexSelected(sourceRow.toInt()));
        }
        QVERIFY(!fileListModel->isIndexSelected(
            galleryViewModel->mapToSourceRow(thirdRangeIndex)));

        // The remaining legacy local-state commands are cheap to assert
        // directly and prove that the wrapper still delegates to ViewerMode.
        const int rotationBefore =
            viewerViewport->property("rotationMode").toInt();
        QTest::keyClick(window, Qt::Key_BracketRight);
        QTRY_COMPARE(viewerViewport->property("rotationMode").toInt(),
                     (rotationBefore + 1) % 4);
        QTest::keyClick(window, Qt::Key_BracketLeft);
        QTRY_COMPARE(viewerViewport->property("rotationMode").toInt(),
                     rotationBefore);

        // ViewerWheelArea now owns the accepted wheel event so it can avoid
        // backend-dependent double delivery.  In the standalone shell it
        // must still preserve the old panorama route: Ctrl+wheel changes the
        // sphere FOV and never zooms the hidden normal viewport.
        QVERIFY(zoomToFitImmediately());
        const qreal normalZoomBeforeSphereWheel =
            viewerViewport->property("zoomScale").toReal();
        QCOMPARE(viewerMode->property("sphericViewerMode").toBool(), false);
        QTest::keyClick(window, Qt::Key_S);
        QTRY_COMPARE(viewerMode->property("sphericViewerMode").toBool(), true);

        QQuickItem *sphericPointerArea = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (sphericPointerArea = viewerMode->findChild<QQuickItem *>(
                 QStringLiteral("sphericViewerPointerArea"))) != nullptr,
            3000);
        auto *sphericViewer = sphericPointerArea->parentItem();
        QVERIFY(sphericViewer);
        const qreal fovBeforeWheel =
            sphericViewer->property("fov").toReal();
        QTest::wheelEvent(
            window, QPoint(window->width() / 2, window->height() / 2),
            QPoint(0, 120), QPoint(), Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(
            sphericViewer->property("fov").toReal() < fovBeforeWheel,
            1000);
        QVERIFY(approximately(
            viewerViewport->property("zoomScale").toReal(),
            normalZoomBeforeSphereWheel, 0.000001));

        QTest::keyClick(window, Qt::Key_P);
        QTRY_COMPARE(viewerMode->property("sphericViewerMode").toBool(), false);

        // F5 is deliberately unknown to ViewerMode but reloads the thumbnail
        // model if it leaks into the hidden MasonryMode.  No model reset is a
        // stronger modal-focus assertion than using a key that is inert in
        // both surfaces.
        QSignalSpy hiddenModelResetSpy(
            fileListModel, &QAbstractItemModel::modelReset);
        QVERIFY(hiddenModelResetSpy.isValid());
        const int indexBeforeUnknownKey = layout->currentIndex();
        QTest::keyClick(window, Qt::Key_F5);
        QTest::qWait(250);
        QCOMPARE(hiddenModelResetSpy.size(), 0);
        QCOMPARE(layout->currentIndex(), indexBeforeUnknownKey);
        QVERIFY(viewerHasFocus());

        // Z and F are representative legacy ViewerMode hotkeys. Z restores
        // Fit; F toggles the actual standalone MainWindow fullscreen state.
        sendModifiedKeyClick(window, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(waitForScale(1.0));
        QVERIFY(!viewerMode->property("zoomFitView").toBool());
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewerMode->property("zoomFitView").toBool(), 3000);
        QTest::keyClick(window, Qt::Key_F);
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::FullScreen,
                                  3000);
        QTest::keyClick(window, Qt::Key_F);
        QTRY_VERIFY_WITH_TIMEOUT(window->visibility() != QWindow::FullScreen,
                                 3000);

        const struct {
            Qt::Key key;
            Qt::KeyboardModifiers modifiers;
            const char *name;
        } fullscreenAliases[] = {
            {Qt::Key_F11, Qt::NoModifier, "F11"},
            {Qt::Key_Return, Qt::AltModifier, "Alt+Enter"},
        };
        for (const auto &alias : fullscreenAliases) {
            QTest::keyClick(window, alias.key, alias.modifiers);
            QVERIFY2(waitFor([&] {
                return window->visibility() == QWindow::FullScreen;
            }), alias.name);
            QTest::keyClick(window, alias.key, alias.modifiers);
            QVERIFY2(waitFor([&] {
                return window->visibility() != QWindow::FullScreen;
            }), alias.name);
            QVERIFY2(viewerHasFocus(), alias.name);
        }

        // Exercise every reachable historic close binding, reopening through
        // the original MasonryMode between aliases and verifying focus/index
        // restoration each time.
        const struct {
            Qt::Key key;
            Qt::KeyboardModifiers modifiers;
            const char *name;
        } closeAliases[] = {
            {Qt::Key_Escape, Qt::NoModifier, "Escape"},
            {Qt::Key_Up, Qt::AltModifier, "Alt+Up"},
            {Qt::Key_Enter, Qt::NoModifier, "Enter"},
        };
        for (const auto &alias : closeAliases) {
            const int indexBeforeClose = layout->currentIndex();
            QTest::keyClick(window, alias.key, alias.modifiers);
            QVERIFY2(waitFor([&] {
                return shellContent->property("state").toString() ==
                           QStringLiteral("thumbnails") &&
                    !viewerMode->isVisible();
            }), alias.name);
            QVERIFY2(isDescendantOf(window->activeFocusItem(), masonryMode),
                     alias.name);
            QCOMPARE(layout->currentIndex(), indexBeforeClose);

            QTest::keyClick(window, Qt::Key_Return);
            QVERIFY2(waitFor([&] {
                return shellContent->property("state").toString() ==
                           QStringLiteral("viewer") &&
                    viewerMode->isVisible() && viewerHasFocus();
            }), alias.name);
        }

        // Return is distinct from keypad Enter in Qt's key enum and is the
        // final close, leaving the production shell in its startup surface.
        const int finalIndex = layout->currentIndex();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(shellContent->property("state").toString(),
                                  QStringLiteral("thumbnails"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!viewerMode->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            isDescendantOf(window->activeFocusItem(), masonryMode), 3000);
        QCOMPARE(layout->currentIndex(), finalIndex);

        // Recursive folder cards use the same bounded delegate pipeline. The
        // nested preview is loaded only for a visible folder whose asynchronous
        // preview model has become available; no legacy renderer is retained.
        QVERIFY(QDir().mkpath(albumPath));
        QVERIFY(writeImage(albumImagePath, QSize(640, 480), Qt::blue));
        fileListModel->enterRecursiveView();
        QTRY_VERIFY_WITH_TIMEOUT(layout->count() >= 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(indexForPath(layout, albumPath) >= 0, 10000);
        const int albumIndex = indexForPath(layout, albumPath);
        QVERIFY(albumIndex >= 0);
        QVERIFY(QMetaObject::invokeMethod(
            masonryMode, "setCurrentIndex",
            Q_ARG(QVariant, QVariant(albumIndex)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false))));
        QTRY_COMPARE(layout->currentIndex(), albumIndex);
        QQuickItem *folderPreview = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (folderPreview = masonryMode->findChild<QQuickItem *>(
                 QStringLiteral("galleryFolderPreview-%1")
                     .arg(albumIndex))) != nullptr,
            10000);

        window->hide();
        runtime->shutdown();
        QVERIFY(session->shutdownComplete());
    }

private:
    QString _sandboxRoot;
};

int main(int argc, char **argv) {
    QTemporaryDir sandbox(
        QDir::tempPath() + QStringLiteral("/zoingallery-shell-smoke-XXXXXX"));
    if (!sandbox.isValid()) {
        return 2;
    }

    const QByteArray root = QFile::encodeName(sandbox.path());
    qputenv("XDG_CONFIG_HOME", root + QByteArrayLiteral("/config"));
    qputenv("XDG_CACHE_HOME", root + QByteArrayLiteral("/cache"));
    qputenv("XDG_DATA_HOME", root + QByteArrayLiteral("/data"));
    qputenv("TMPDIR", root + QByteArrayLiteral("/tmp"));
#if defined(Q_OS_WIN)
    qputenv("TEMP", root + QByteArrayLiteral("/tmp"));
    qputenv("TMP", root + QByteArrayLiteral("/tmp"));
#endif
    QDir().mkpath(sandbox.filePath(QStringLiteral("tmp")));
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       sandbox.filePath(QStringLiteral("settings")));

    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    application.setOrganizationName(QStringLiteral("ZoinGalleryTests"));
    application.setOrganizationDomain(QStringLiteral("tests.zoingallery"));
    application.setApplicationName(
        QStringLiteral("StandaloneShellSmoke-%1")
            .arg(QCoreApplication::applicationPid()));

    StandaloneShellSmokeTest test(sandbox.path());
    return QTest::qExec(&test, argc, argv);
}

#include "StandaloneShellSmokeTest.moc"
