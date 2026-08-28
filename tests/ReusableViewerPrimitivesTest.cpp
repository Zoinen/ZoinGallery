#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <cmath>

namespace {

class SquareImageProvider final : public QQuickImageProvider {
public:
    SquareImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override {
        const QUrl routeUrl = QUrl::fromEncoded(
            QByteArrayLiteral("f4icon://provider/") + id.toUtf8(),
            QUrl::StrictMode);
        const QUrlQuery query(routeUrl);
        const int logicalSize = std::max(
            1, query.queryItemValue(QStringLiteral("size")).toInt());
        const qreal dpr = std::max(
            0.5, query.queryItemValue(QStringLiteral("dpr")).toDouble());
        const QSize physicalSize(
            qRound(logicalSize * dpr), qRound(logicalSize * dpr));
        const QSize targetSize = requestedSize.isValid()
                                     ? requestedSize
                                     : physicalSize;
        QImage image(targetSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        for (int x = 0; x < targetSize.width(); ++x) {
            image.setPixelColor(x, 0, Qt::black);
            image.setPixelColor(x, targetSize.height() - 1, Qt::black);
        }
        for (int y = 0; y < targetSize.height(); ++y) {
            image.setPixelColor(0, y, Qt::black);
            image.setPixelColor(targetSize.width() - 1, y, Qt::black);
        }
        image.setDevicePixelRatio(dpr);
        if (size) {
            *size = QSize(logicalSize, logicalSize);
        }
        return image;
    }
};

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

QQuickItem *visualItem(QQuickItem *root, const QString &objectName) {
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *match = visualItem(child, objectName)) {
            return match;
        }
    }
    return nullptr;
}

} // namespace

class ReusableViewerPrimitivesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }

    void pathControlAcceptsNativeWindowsSeparators() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        view.engine()->addImageProvider(QStringLiteral("f4icons"),
                                        new SquareImageProvider);

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            Item {
                id: testRoot
                width: 260
                height: 80
                property string navigatedPath

                PathControl {
                    id: pathControl
                    objectName: "pathControl"
                    anchors.fill: parent
                    devicePixelRatio: 1.75
                    windowsPathSeparators: true
                    breadcrumbFontPixelSize: 13
                    text: "C:\\Users\\Alice\\Pictures"
                    navigationHandler: function(path) {
                        testRoot.navigatedPath = path
                    }
                }
            }
        )QML", QStringLiteral("WindowsPathControl.qml"));
        QVERIFY(root);
        auto *pathControl = root->findChild<QQuickItem *>(
            QStringLiteral("pathControl"));
        QVERIFY(pathControl);
        pathControl->setProperty("pathTextColor", QColor(Qt::black));
        pathControl->setProperty(
            "localDriveIconSource",
            QUrl(QStringLiteral(
                "image://f4icons/lucide/hard-drive?size=18&dpr=1.75")));
        pathControl->setProperty(
            "networkDriveIconSource",
            QUrl(QStringLiteral(
                "image://f4icons/lucide/network?size=18&dpr=1.75")));

        QCOMPARE(pathControl->property("normalizedText").toString(),
                 QStringLiteral("C:/Users/Alice/Pictures"));
        QCOMPARE(pathControl->property("breadcrumbs").toList(),
                 QVariantList({QStringLiteral("C:"), QStringLiteral("Users"),
                               QStringLiteral("Alice"),
                               QStringLiteral("Pictures")}));

        pathControl->setProperty("text",
                                 QStringLiteral("C:\\WINDOWS\\system32"));
        view.show();
        view.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);
        QCoreApplication::processEvents();
        QTest::qWait(50);

        auto *rootItem = qobject_cast<QQuickItem *>(root);
        QVERIFY(rootItem);
        auto *dynamicPart = visualItem(
            rootItem,
            QStringLiteral("pathDynamicPart"));
        auto *collapsiblePart = visualItem(
            rootItem,
            QStringLiteral("pathCollapsiblePart"));
        auto *windowsBreadcrumb = visualItem(
            rootItem,
            QStringLiteral("pathBreadcrumb-0"));
        auto *system32Breadcrumb = visualItem(
            rootItem,
            QStringLiteral("pathBreadcrumb-1"));
        auto *rootSeparator = visualItem(
            rootItem,
            QStringLiteral("pathBreadcrumbRoot-separator"));
        auto *driveIcon = visualItem(rootItem,
                                     QStringLiteral("pathDriveIcon"));
        auto *windowsSeparator = visualItem(
            rootItem,
            QStringLiteral("pathBreadcrumb-0-separator"));
        auto *windowsText = visualItem(
            rootItem,
            QStringLiteral("pathBreadcrumb-0-text"));
        auto *system32Text = visualItem(
            rootItem,
            QStringLiteral("pathBreadcrumb-1-text"));
        QVERIFY(dynamicPart);
        QVERIFY(collapsiblePart);
        QVERIFY(windowsBreadcrumb);
        QVERIFY(system32Breadcrumb);
        QVERIFY(rootSeparator);
        QVERIFY(driveIcon);
        QVERIFY(windowsSeparator);
        QVERIFY(windowsText);
        QVERIFY(system32Text);
        QVERIFY(dynamicPart->width() > 0.0);
        QVERIFY(collapsiblePart->width() > 0.0);
        QVERIFY(windowsBreadcrumb->width() > 0.0);
        QVERIFY(system32Breadcrumb->width() > 0.0);
        QVERIFY(system32Breadcrumb->x()
                >= windowsBreadcrumb->x() + windowsBreadcrumb->width());
        QVERIFY(rootSeparator->isVisible());
        QVERIFY(windowsSeparator->isVisible());

        const qreal layoutDpr = pathControl->property("dpr").toReal();
        QCOMPARE(layoutDpr, qreal(1.75));
        const auto isPhysicalPixelAligned = [layoutDpr](qreal value) {
            return qAbs(value * layoutDpr - qRound(value * layoutDpr)) < 0.001;
        };
        const QPointF rootSeparatorTopLeft =
            rootSeparator->mapToScene(QPointF(0, 0));
        const QPointF windowsSeparatorTopLeft =
            windowsSeparator->mapToScene(QPointF(0, 0));
        QVERIFY(isPhysicalPixelAligned(rootSeparatorTopLeft.y()));
        QVERIFY(isPhysicalPixelAligned(rootSeparatorTopLeft.y()
                                       + rootSeparator->height()));
        QVERIFY(isPhysicalPixelAligned(windowsSeparatorTopLeft.y()));
        QVERIFY(isPhysicalPixelAligned(windowsSeparatorTopLeft.y()
                                       + windowsSeparator->height()));
        QVERIFY(isPhysicalPixelAligned(rootSeparator->width()));
        QVERIFY(isPhysicalPixelAligned(rootSeparator->height()));
        QCOMPARE(rootSeparator->width(), pathControl->property(
                     "breadcrumbSeparatorSize").toReal());
        QCOMPARE(qRound(rootSeparator->width() * layoutDpr), 21);
        QVERIFY(isPhysicalPixelAligned(driveIcon->width()));
        QVERIFY(isPhysicalPixelAligned(driveIcon->height()));
        const QPointF driveIconTopLeft = driveIcon->mapToScene(QPointF(0, 0));
        QVERIFY(isPhysicalPixelAligned(driveIconTopLeft.x()));
        QVERIFY(isPhysicalPixelAligned(driveIconTopLeft.y()));
        QVERIFY(isPhysicalPixelAligned(driveIconTopLeft.x()
                                       + driveIcon->width()));
        QVERIFY(isPhysicalPixelAligned(driveIconTopLeft.y()
                                       + driveIcon->height()));
        QCOMPARE(qRound(driveIcon->width() * layoutDpr), 32);

        const qreal expectedSeparatorPadding = pathControl
            ->property("breadcrumbSeparatorHorizontalPadding").toReal();
        const qreal windowsTextLeft =
            windowsText->mapToScene(QPointF(0, 0)).x();
        const qreal windowsSeparatorLeft = windowsSeparatorTopLeft.x();
        const qreal system32TextLeft =
            system32Text->mapToScene(QPointF(0, 0)).x();
        const qreal leftSeparatorGap =
            windowsSeparatorLeft - (windowsTextLeft + windowsText->width());
        const qreal rightSeparatorGap = system32TextLeft
            - (windowsSeparatorLeft + windowsSeparator->width());
        const qreal layoutRoundingTolerance = 1.0 / layoutDpr + 0.01;
        const QByteArray gapDetails = QStringLiteral(
            "left=%1 right=%2 expected=%3 tolerance=%4")
            .arg(leftSeparatorGap)
            .arg(rightSeparatorGap)
            .arg(expectedSeparatorPadding)
            .arg(layoutRoundingTolerance)
            .toUtf8();
        QVERIFY2(qAbs(leftSeparatorGap - expectedSeparatorPadding)
                     < layoutRoundingTolerance,
                 gapDetails.constData());
        QVERIFY2(qAbs(rightSeparatorGap - expectedSeparatorPadding)
                     < layoutRoundingTolerance,
                 gapDetails.constData());
        QVERIFY(leftSeparatorGap < 7.0);
        QVERIFY(rightSeparatorGap < 7.0);
        QVERIFY(expectedSeparatorPadding < 7.0);

        const QImage frame = view.grabWindow();
        QVERIFY(!frame.isNull());
        const qreal frameDpr = frame.devicePixelRatio();
        const QPointF dynamicTopLeft = dynamicPart->mapToScene(QPointF(0, 0));
        const QRect dynamicPixels(
            qFloor(dynamicTopLeft.x() * frameDpr),
            qFloor(dynamicTopLeft.y() * frameDpr),
            qCeil(dynamicPart->width() * frameDpr),
            qCeil(dynamicPart->height() * frameDpr));
        const QRect visibleDynamicPixels = dynamicPixels.intersected(frame.rect());
        int darkPixels = 0;
        for (int y = visibleDynamicPixels.top();
             y <= visibleDynamicPixels.bottom(); ++y) {
            for (int x = visibleDynamicPixels.left();
                 x <= visibleDynamicPixels.right(); ++x) {
                const QColor pixel = frame.pixelColor(x, y);
                if (pixel.alpha() > 128 && pixel.red() < 160
                    && pixel.green() < 160 && pixel.blue() < 160) {
                    ++darkPixels;
                }
            }
        }
        QVERIFY2(darkPixels > 0,
                 "provider-backed dynamic breadcrumbs were not rendered");

        pathControl->setProperty("editMode", true);
        QCoreApplication::processEvents();
        QObject *pathField = root->findChild<QObject *>(
            QStringLiteral("pathField"));
        QVERIFY(pathField);
        QCOMPARE(pathField->property("font").value<QFont>(),
                 system32Text->property("font").value<QFont>());
        QVERIFY(pathField->property("visible").toBool());
        QVERIFY(!dynamicPart->property("visible").toBool());
        QVERIFY(!system32Text->isVisible());
        QCOMPARE(pathField->property("text").toString(),
                 QStringLiteral("C:\\WINDOWS\\system32"));

        pathField->setProperty("text", QStringLiteral("D:\\Media\\Photos"));
        QVERIFY(QMetaObject::invokeMethod(pathField, "accept"));
        QCOMPARE(root->property("navigatedPath").toString(),
                 QStringLiteral("D:/Media/Photos"));

        pathControl->setProperty(
            "text", QStringLiteral("\\\\server\\share\\folder"));
        QCoreApplication::processEvents();
        QCOMPARE(pathControl->property("normalizedText").toString(),
                 QStringLiteral("//server/share/folder"));
        QCOMPARE(pathControl->property("isNetworkDrive").toBool(), true);
        QCOMPARE(pathControl->property("breadcrumbs").toList(),
                 QVariantList({QStringLiteral("server"),
                               QStringLiteral("share"),
                               QStringLiteral("folder")}));

        QVERIFY(QMetaObject::invokeMethod(
            pathControl, "folderClicked",
            Q_ARG(QVariant, QStringLiteral("share/folder"))));
        QCOMPARE(root->property("navigatedPath").toString(),
                 QStringLiteral("//server/share/folder"));
    }

    void pathBreadcrumbTextDoesNotMoveWhenItGainsSeparator() {
        const QFont previousFont = QGuiApplication::font();
        const auto previousRenderType = QQuickWindow::textRenderType();
        const auto restoreTextPolicy = qScopeGuard([
                previousFont, previousRenderType]() {
            QGuiApplication::setFont(previousFont);
            QQuickWindow::setTextRenderType(previousRenderType);
        });
        QFont appFont(QStringLiteral("Consolas"));
        appFont.setPixelSize(18);
        QGuiApplication::setFont(appFont);
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            Item {
                width: 900
                height: 80

                PathControl {
                    id: pathControl
                    objectName: "pathControl"
                    anchors.fill: parent
                    devicePixelRatio: 1.75
                    windowsPathSeparators: true
                    breadcrumbFontPixelSize: 13
                    text: "C:\\WINDOWS\\system32"
                }
            }
        )QML", QStringLiteral("StablePathBreadcrumb.qml"));
        QVERIFY(root);
        auto *rootItem = qobject_cast<QQuickItem *>(root);
        auto *pathControl = root->findChild<QQuickItem *>(
            QStringLiteral("pathControl"));
        QVERIFY(rootItem);
        QVERIFY(pathControl);

        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);
        QCoreApplication::processEvents();

        QQuickItem *system32TextBefore = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (system32TextBefore = visualItem(
                 rootItem,
                 QStringLiteral("pathBreadcrumb-1-text"))) != nullptr,
            3000);
        const qreal before =
            system32TextBefore->mapToScene(QPointF(0, 0)).x();
        const QImage frameBefore = view.grabWindow();
        QVERIFY(!frameBefore.isNull());
        const auto physicalRect = [](QQuickItem *item, qreal frameDpr) {
            const QPointF topLeft = item->mapToScene(QPointF(0, 0));
            const int left = qFloor(topLeft.x() * frameDpr);
            const int top = qFloor(topLeft.y() * frameDpr);
            const int right = qCeil(
                (topLeft.x() + item->width()) * frameDpr);
            const int bottom = qCeil(
                (topLeft.y() + item->height()) * frameDpr);
            return QRect(left, top, right - left, bottom - top);
        };
        const qreal frameDpr = frameBefore.devicePixelRatio();
        const QRect beforeRect = physicalRect(system32TextBefore, frameDpr);
        const QImage beforeText = frameBefore.copy(beforeRect);

        pathControl->setProperty(
            "text", QStringLiteral("C:\\WINDOWS\\system32\\az"));
        QTRY_COMPARE_WITH_TIMEOUT(
            pathControl->property("breadcrumbs").toList().size(), 4, 3000);
        QTest::qWait(50);
        QCoreApplication::processEvents();
        QQuickItem *system32TextAfter = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (system32TextAfter = visualItem(
                 rootItem,
                 QStringLiteral("pathBreadcrumb-1-text"))) != nullptr,
            3000);
        const qreal after =
            system32TextAfter->mapToScene(QPointF(0, 0)).x();
        constexpr qreal dpr = 1.75;
        const qreal shiftPhysical = (after - before) * dpr;
        const QByteArray details = QStringLiteral(
            "system32 breadcrumb moved by %1 physical px (%2 -> %3 logical) "
            "when it gained a trailing separator")
            .arg(shiftPhysical, 0, 'f', 6)
            .arg(before, 0, 'f', 6)
            .arg(after, 0, 'f', 6)
            .toUtf8();
        QVERIFY2(qAbs(shiftPhysical) < 0.001, details.constData());

        const QImage frameAfter = view.grabWindow();
        QVERIFY(!frameAfter.isNull());
        QCOMPARE(frameAfter.devicePixelRatio(), frameDpr);
        const QRect afterRect = physicalRect(system32TextAfter, frameDpr);
        QCOMPARE(afterRect, beforeRect);
        const QImage afterText = frameAfter.copy(afterRect);
        int changedPixels = 0;
        for (int y = 0; y < beforeText.height(); ++y) {
            for (int x = 0; x < beforeText.width(); ++x) {
                if (beforeText.pixel(x, y) != afterText.pixel(x, y)) {
                    ++changedPixels;
                }
            }
        }
        QVERIFY2(changedPixels == 0,
                 qPrintable(QStringLiteral(
                     "system32 breadcrumb raster changed in %1 pixels")
                                .arg(changedPixels)));
    }

    void titleButtonSnapsItsIconAtFractionalDpr() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            Item {
                id: testRoot
                width: 100
                height: 50

                QtObject {
                    id: topLevelWindow
                    property bool active: true
                }
                Item {
                    id: titleBar
                    width: parent.width
                    height: 42
                }
                TitleButton {
                    id: testedButton
                    objectName: "testedTitleButton"
                    devicePixelRatio: 1.75
                    source: "qrc:/ZoinGallery/resources/WindowClose.svg"
                }
            }
        )QML", QStringLiteral("FractionalDprTitleButton.qml"));
        QVERIFY(root);
        auto *rootItem = qobject_cast<QQuickItem *>(root);
        QVERIFY(rootItem);
        auto *button = visualItem(rootItem,
                                  QStringLiteral("testedTitleButton"));
        QVERIFY(button);
        auto *icon = visualItem(button, QStringLiteral("titleBarButtonIcon"));
        QVERIFY(icon);

        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);
        QCoreApplication::processEvents();

        constexpr qreal dpr = 1.75;
        const auto isPhysicalPixelAligned = [](qreal value) {
            return qAbs(value * dpr - qRound(value * dpr)) < 0.001;
        };
        QVERIFY(isPhysicalPixelAligned(button->implicitWidth()));
        QVERIFY(isPhysicalPixelAligned(icon->width()));
        QVERIFY(isPhysicalPixelAligned(icon->height()));
        const QPointF iconTopLeft = icon->mapToScene(QPointF(0, 0));
        QVERIFY(isPhysicalPixelAligned(iconTopLeft.x()));
        QVERIFY(isPhysicalPixelAligned(iconTopLeft.y()));
        QVERIFY(isPhysicalPixelAligned(iconTopLeft.x() + icon->width()));
        QVERIFY(isPhysicalPixelAligned(iconTopLeft.y() + icon->height()));
        QCOMPARE(qRound(button->implicitWidth() * dpr), 81);
        QCOMPARE(qRound(icon->width() * dpr), 18);
        QCOMPARE(qRound(icon->height() * dpr), 18);
        const QSize sourceSize = icon->property("sourceSize").toSize();
        QCOMPARE(qRound(sourceSize.width() * dpr), 18);
        QCOMPARE(qRound(sourceSize.height() * dpr), 18);
        QCOMPARE(qRound(icon->implicitWidth() * dpr), 18);
        QCOMPARE(qRound(icon->implicitHeight() * dpr), 18);
        QVERIFY(!view.grabWindow().isNull());
    }

    void coldTierSizeChangeKeepsAnAtomicFittedViewport() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            Item {
                width: 640
                height: 420
                property rect imageRect: Qt.rect(zoomable.image.x,
                                                  zoomable.image.y,
                                                  zoomable.image.width,
                                                  zoomable.image.height)
                function installProvisionalSize() {
                    // A cold provider/source fallback can temporarily report
                    // a different physical size before catalog metadata and
                    // the authoritative viewer tier converge.
                    zoomable.setImage("", Qt.size(6048, 8064), 0, 0)
                    zoomable.zoomToFit(true)
                }
                function installAuthoritativeSize() {
                    zoomable.setImage("", Qt.size(3024, 4032), 0, 1)
                }
                function installLaterTierSize() {
                    zoomable.setImage("", Qt.size(6048, 8064), 0, 2)
                }
                FlickableZoomable {
                    id: zoomable
                    objectName: "zoomable"
                    anchors.fill: parent
                    active: true
                    devicePixelRatio: 2
                    animationDuration: 150
                }
            }
        )QML", QStringLiteral("ColdTierFit.qml"));
        QVERIFY(root);
        auto *zoomable = root->findChild<QQuickItem *>(
            QStringLiteral("zoomable"));
        QVERIFY(zoomable);
        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);

        QVERIFY(QMetaObject::invokeMethod(root, "installProvisionalSize"));
        QVERIFY(QMetaObject::invokeMethod(root, "installAuthoritativeSize"));

        // This is intentionally synchronous: there must be no renderable
        // half-size/up-left frame between the metadata handoff and next tick.
        const QRectF fitted = root->property("imageRect").toRectF();
        QVERIFY(qAbs(fitted.width() - 315.0) < 0.01);
        QVERIFY(qAbs(fitted.height() - 420.0) < 0.01);
        QVERIFY(qAbs(fitted.x() - 162.5) < 0.01);
        QVERIFY(qAbs(fitted.y()) < 0.01);
        QVERIFY(zoomable->property("zoomFitView").toBool());

        // An explicit user viewport must not be reset by a later tier.
        QVERIFY(QMetaObject::invokeMethod(
            zoomable, "zoomToScale", Q_ARG(QVariant, 1.0),
            Q_ARG(QVariant, false)));
        QTRY_VERIFY(!zoomable->property("zoomFitView").toBool());
        const qreal userZoom = zoomable->property("targetZoomScale").toReal();
        QVERIFY(QMetaObject::invokeMethod(root, "installLaterTierSize"));
        QCOMPARE(zoomable->property("targetZoomScale").toReal(), userZoom);
        QVERIFY(!zoomable->property("zoomFitView").toBool());
    }

    void sphericViewerIsWindowlessAndKeepsLegacyInputMath() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            SphericViewer {
                objectName: "sphericViewer"
                width: 480
                height: 320
                originalSize: Qt.size(2048, 1024)
                animationDuration: 0
            }
        )QML", QStringLiteral("ReusableSphericViewer.qml"));
        QVERIFY(root);

        auto *viewer = qobject_cast<QQuickItem *>(root);
        auto *pointer = root->findChild<QQuickItem *>(
            QStringLiteral("sphericViewerPointerArea"));
        QVERIFY(viewer);
        QVERIFY(pointer);
        QCOMPARE(viewer->property("fov").toReal(), 90.0);
        QCOMPARE(viewer->property("fovVisual").toReal(), 90.0);
        QCOMPARE(viewer->property("pan").toReal(), 0.0);
        QCOMPARE(viewer->property("tilt").toReal(), 0.0);

        QSignalSpy closeSpy(viewer, SIGNAL(closeRequested()));
        QSignalSpy cursorSpy(
            viewer,
            SIGNAL(sphereScrollingMouseCursorRequested(bool,bool,double)));
        QVERIFY(closeSpy.isValid());
        QVERIFY(cursorSpy.isValid());

        view.show();
        view.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);

        const QPoint center(240, 160);
        const qreal initialFov = viewer->property("fov").toReal();
        QTest::wheelEvent(&view, center, QPoint(0, 120), QPoint(),
                          Qt::NoModifier);
        QTest::qWait(50);
        QCOMPARE(viewer->property("fov").toReal(), initialFov);

        // Preserve the legacy exponential Ctrl-wheel FOV calculation.
        QTest::wheelEvent(&view, center, QPoint(0, 120), QPoint(),
                          Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(
            viewer->property("fov").toReal() < initialFov, 1000);
        const qreal normalizedFov = (initialFov - 1.0) / 179.0;
        const qreal expectedFov =
            initialFov * std::exp(-1.2 * 0.1 * (1.0 - normalizedFov));
        QVERIFY(qAbs(viewer->property("fov").toReal() - expectedFov) < 0.01);

        const QPoint dragStart(180, 130);
        const QPoint dragEnd(270, 185);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTRY_COMPARE_WITH_TIMEOUT(cursorSpy.size(), 1, 1000);
        QCOMPARE(cursorSpy.at(0).at(0).toBool(), true);
        QCOMPARE(cursorSpy.at(0).at(1).toBool(), true);
        QTest::mouseMove(&view, dragEnd, 20);
        QTRY_VERIFY_WITH_TIMEOUT(cursorSpy.size() >= 2, 1000);
        QCOMPARE(cursorSpy.at(1).at(0).toBool(), true);
        QCOMPARE(cursorSpy.at(1).at(1).toBool(), false);
        QTest::qWait(60);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, dragEnd);
        QTRY_VERIFY_WITH_TIMEOUT(cursorSpy.size() >= 3, 1000);
        const QList<QVariant> releaseRequest = cursorSpy.constLast();
        QCOMPARE(releaseRequest.at(0).toBool(), false);
        QCOMPARE(releaseRequest.at(1).toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(viewer->property("pan").toReal()) > 0.001, 1000);
        QVERIFY(viewer->property("tilt").toReal() >= -90.0);
        QVERIFY(viewer->property("tilt").toReal() <= 90.0);
        QVERIFY(viewer->property("inertiaRunning").toBool());

        QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE_WITH_TIMEOUT(closeSpy.size(), 1, 1000);
    }

    void middleClickHasAnIndependentReusableSignal() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            Item {
                width: 480
                height: 320
                FlickableZoomable {
                    id: zoomable
                    objectName: "zoomable"
                    anchors.fill: parent
                    active: true
                    animationDuration: 0
                }
                // Match ViewerMode's non-owning left-button overlay.  Middle
                // and right input must still reach the reusable viewport.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: mouse => { mouse.accepted = false }
                }
            }
        )QML", QStringLiteral("ReusableMiddleClick.qml"));
        QVERIFY(root);

        auto *zoomable = root->findChild<QQuickItem *>(
            QStringLiteral("zoomable"));
        auto *pointer = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerPointerArea"));
        QVERIFY(zoomable);
        QVERIFY(pointer);

        QSignalSpy middleSpy(zoomable, SIGNAL(middleClickRequested()));
        QSignalSpy clickSpy(zoomable, SIGNAL(clicked()));
        QSignalSpy closeSpy(zoomable, SIGNAL(closeRequested()));
        QVERIFY(middleSpy.isValid());
        QVERIFY(clickSpy.isValid());
        QVERIFY(closeSpy.isValid());

        view.show();
        view.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);
        const QPoint center(240, 160);

        QTest::mouseClick(&view, Qt::MiddleButton, Qt::NoModifier, center);
        QTRY_COMPARE_WITH_TIMEOUT(middleSpy.size(), 1, 1000);
        QCOMPARE(clickSpy.size(), 0);
        QCOMPARE(closeSpy.size(), 0);

        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE_WITH_TIMEOUT(clickSpy.size(), 1, 1000);
        QCOMPARE(middleSpy.size(), 1);
        QCOMPARE(closeSpy.size(), 0);

        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, center);
        QTest::qWait(50);
        QCOMPARE(middleSpy.size(), 1);
        QCOMPARE(clickSpy.size(), 1);
        QCOMPARE(closeSpy.size(), 0);
    }
};

QTEST_MAIN(ReusableViewerPrimitivesTest)
#include "ReusableViewerPrimitivesTest.moc"
