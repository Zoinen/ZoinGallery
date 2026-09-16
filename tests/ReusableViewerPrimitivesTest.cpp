#include <ZoinGallery/GalleryRuntime.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QLineF>
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
        ZoinGallery::GalleryRuntime::registerTypes();
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

    void resizePreservesZoomedCenterAndFit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("resize-grid.png"));
        QImage grid(2800, 2100, QImage::Format_RGB32);
        grid.fill(Qt::white);
        for (int y = 0; y < grid.height(); ++y)
            for (int x = 0; x < grid.width(); ++x)
                if (x % 100 == 0 || y % 100 == 0)
                    grid.setPixelColor(x, y, Qt::black);
        QVERIFY(grid.save(path));
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            FlickableZoomable {
                width: 640; height: 420
                devicePixelRatio: 1.75
                animationDuration: 0
                property url testSource
                function prepare() {
                    setImage(testSource, Qt.size(2800, 2100), 0, 0)
                    setViewport(1, -400, -300)
                }
            }
        )QML", QStringLiteral("ResizeViewport.qml"));
        auto *viewport = qobject_cast<QQuickItem *>(root);
        QVERIFY(viewport);
        view.show();
        QTRY_VERIFY(view.isExposed());
        root->setProperty("testSource", QUrl::fromLocalFile(path));
        QVERIFY(QMetaObject::invokeMethod(root, "prepare"));
        auto *image = root->property("image").value<QQuickItem *>();
        QVERIFY(image);
        const QPointF center((viewport->width()/2-image->x()),
                             (viewport->height()/2-image->y()));
        viewport->setWidth(1000);
        viewport->setHeight(700);
        qInfo() << "[FIX:viewer-resize] center" << center << "image" << image->position();
        QCOMPARE(QPointF(viewport->width()/2-image->x(),
                         viewport->height()/2-image->y()), center);
        QCOMPARE(root->property("zoomScale").toReal(), 1.0);
        QTRY_VERIFY(root->property("imageTextureReady").toBool());
        auto *base = root->findChild<QQuickItem *>(QStringLiteral("galleryViewerBaseImage"));
        QVERIFY(base);
        const QPointF origin = base->mapToItem(view.contentItem(), QPointF());
        const qreal dpr = view.devicePixelRatio();
        QVERIFY(qAbs(origin.x()*dpr-qRound(origin.x()*dpr)) < 0.01);
        QVERIFY(qAbs(origin.y()*dpr-qRound(origin.y()*dpr)) < 0.01);
        QCOMPARE(base->mapToItem(view.contentItem(), QPointF(1,0))-origin, QPointF(1,0));
        QCOMPARE(base->mapToItem(view.contentItem(), QPointF(0,1))-origin, QPointF(0,1));
        view.requestUpdate();
        QTest::qWait(100);
        const QImage rendered = view.grabWindow();
        bool hasGrid = false;
        for (int y = 0; y < rendered.height() && !hasGrid; ++y)
            for (int x = 0; x < rendered.width() && !hasGrid; ++x)
                hasGrid = rendered.pixelColor(x, y).lightness() < 128;
        QVERIFY(hasGrid);
        QVERIFY(rendered.save(QStringLiteral("/tmp/f4-resize-grid-175.png")));
        QVERIFY(QMetaObject::invokeMethod(root, "zoomToFit", Q_ARG(QVariant, true)));
        for (const QSizeF size : {QSizeF(500, 900), QSizeF(1200, 400), QSizeF(640, 420)}) {
            viewport->setSize(size);
            const qreal scale = qMin(size.width()/1600, size.height()/1200);
            QVERIFY(qAbs(root->property("zoomScale").toReal()-scale) < 0.0001);
            QVERIFY(qAbs(image->x()-(size.width()-1600*scale)/2) < 0.01);
            QVERIFY(qAbs(image->y()-(size.height()-1200*scale)/2) < 0.01);
            QVERIFY(root->property("zoomFitView").toBool());
        }
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

    void fitUsesPreparedTierAfterNativeZoom() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString basePath = directory.filePath(QStringLiteral("fit.png"));
        const QString nativePath = directory.filePath(QStringLiteral("native.png"));
        QImage base(350, 210, QImage::Format_ARGB32_Premultiplied);
        QImage native(1400, 840, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::white);
        native.fill(Qt::white);
        QVERIFY(base.save(basePath));
        QVERIFY(native.save(nativePath));

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            FlickableZoomable {
                width: 200; height: 120
                devicePixelRatio: 1.75
                animationDuration: 0
                property url testBase
                property url testNative
                function installBase() {
                    setImage(testBase, Qt.size(1400, 840), 0, 1)
                    zoomToFit(true)
                }
                function installNative() {
                    // At this scale an entire axis is visible, so a file URL
                    // does not enter the provider-only native crop route.
                    setViewport(0.1, 0, 0)
                    setImage(testNative, Qt.size(1400, 840), 0, 2)
                }
            }
        )QML", QStringLiteral("FitTierAfterNative.qml"));
        QVERIFY(root);
        root->setProperty("testBase", QUrl::fromLocalFile(basePath));
        root->setProperty("testNative", QUrl::fromLocalFile(nativePath));
        QVERIFY(QMetaObject::invokeMethod(root, "installBase"));
        auto *baseItem = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerBaseImage"));
        auto *nativeItem = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerNativeImage"));
        QVERIFY(baseItem);
        QVERIFY(nativeItem);
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), baseItem);
        QVERIFY(QMetaObject::invokeMethod(root, "installNative"));
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), nativeItem);
        QVERIFY(QMetaObject::invokeMethod(root, "zoomToFit", Q_ARG(QVariant, true)));
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), baseItem);
        root->setProperty("sphericTextureMipmapsEnabled", true);
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), nativeItem);
        root->setProperty("sphericTextureMipmapsEnabled", false);
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), baseItem);

        // An undersized preview must not displace an already decoded original.
        auto *imageItem = root->property("image").value<QQuickItem *>();
        QVERIFY(imageItem);
        imageItem->setProperty("fromLevel", 0);
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), nativeItem);
        imageItem->setProperty("fromLevel", 1);
        root->setProperty("width", 400);
        root->setProperty("height", 240);
        QTRY_COMPARE(root->property("textureSource").value<QQuickItem *>(), nativeItem);
    }

    void imageLeavesSnapThroughFractionalAncestors() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("pixel-grid.png"));
        QImage image(1201, 799, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QVERIFY(image.save(path));
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                id: testRoot
                width: 700; height: 500
                property url testSource
                property real ancestorOffset: 0.13
                property real transformOffset: 0.17
                property alias cropCheckerboardEnabled: zoomable.checkerboardEnabled
                property alias sphereMipmapsEnabled: zoomable.sphericTextureMipmapsEnabled
                function prepare() {
                    zoomable.setImage(testSource, Qt.size(1201, 799), 0, 1)
                    zoomable.zoomToFit(true)
                    zoomable.viewerImageCrop.source = testSource
                    zoomable.viewerImageCrop.unscaledX = 13.7
                    zoomable.viewerImageCrop.unscaledY = 21.3
                    zoomable.viewerImageCrop.unscaledWidth = 201.1
                    zoomable.viewerImageCrop.unscaledHeight = 151.7
                }
                function prepareThinImage() {
                    zoomable.originalSize = Qt.size(8000 / 1.75, 1 / 1.75)
                    zoomable.zoomScale = 0.1
                }
                function enableCrop() { zoomable.zoomFitView = false }
                Item {
                    x: parent.ancestorOffset; y: 0.19
                    transform: Translate {
                        id: testTranslation
                        x: testRoot.transformOffset
                    }
                    FlickableZoomable {
                        id: zoomable
                        objectName: "zoomable"
                        x: 0.23; y: 0.29
                        width: 613; height: 401
                        devicePixelRatio: 1.75
                        pixelAlignmentRevision: testTranslation.x
                        animationDuration: 0
                    }
                }
            }
        )QML", QStringLiteral("ViewerPhysicalGrid.qml"));
        QVERIFY(root);
        root->setProperty("testSource", QUrl::fromLocalFile(path));
        QVERIFY(QMetaObject::invokeMethod(root, "prepare"));
        view.show();
        QTRY_VERIFY(view.isExposed());
        QCOMPARE(view.devicePixelRatio(), qreal(1.75));
        QCoreApplication::processEvents();

        for (qreal ancestorOffset : {0.13, 0.31}) {
            root->setProperty("ancestorOffset", ancestorOffset);
            root->setProperty("transformOffset", ancestorOffset * 2);
            QCoreApplication::processEvents();
            for (const QString &name : {
                     QStringLiteral("galleryViewerBaseImage"),
                     QStringLiteral("galleryViewerNativeImage"),
                     QStringLiteral("galleryViewerImageShader"),
                     QStringLiteral("galleryViewerCropImage"),
                     QStringLiteral("galleryViewerCropShader")}) {
                auto *leaf = root->findChild<QQuickItem *>(name);
                QVERIFY(leaf);
                const QPointF origin = leaf->mapToItem(view.contentItem(), QPointF());
                const qreal dpr = view.devicePixelRatio();
                const QString details = QStringLiteral(
                    "%1 physical origin=(%2,%3), size=(%4,%5)")
                    .arg(name).arg(origin.x() * dpr, 0, 'f', 6)
                    .arg(origin.y() * dpr, 0, 'f', 6)
                    .arg(leaf->width() * dpr, 0, 'f', 6)
                    .arg(leaf->height() * dpr, 0, 'f', 6);
                qInfo().noquote() << details;
                const auto aligned = [dpr](qreal value) {
                    return qAbs(value * dpr - qRound(value * dpr)) < 0.001;
                };
                QVERIFY2(aligned(origin.x()), qPrintable(details));
                QVERIFY2(aligned(origin.y()), qPrintable(details));
                QVERIFY2(aligned(leaf->width()), qPrintable(details));
                QVERIFY2(aligned(leaf->height()), qPrintable(details));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(1, 0))
                             - origin, QPointF(1, 0));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(0, 1))
                             - origin, QPointF(0, 1));
            }
        }
        auto *nativeLeaf = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerNativeImage"));
        QVERIFY(nativeLeaf);
        QCOMPARE(nativeLeaf->property("mipmap").toBool(), false);
        root->setProperty("sphereMipmapsEnabled", true);
        QCOMPARE(nativeLeaf->property("mipmap").toBool(), true);
        root->setProperty("sphereMipmapsEnabled", false);
        QCOMPARE(nativeLeaf->property("mipmap").toBool(), false);

        auto *cropLeaf = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerCropShader"));
        QVERIFY(cropLeaf);
        QVERIFY(QMetaObject::invokeMethod(root, "enableCrop"));
        QTRY_VERIFY(cropLeaf->isVisible());
        root->setProperty("cropCheckerboardEnabled", true);
        QTRY_VERIFY(cropLeaf->isVisible());
        root->setProperty("cropCheckerboardEnabled", false);
        QVERIFY(cropLeaf->isVisible());

        QVERIFY(QMetaObject::invokeMethod(root, "prepareThinImage"));
        auto *thinLeaf = root->findChild<QQuickItem *>(
            QStringLiteral("galleryViewerImageShader"));
        QVERIFY(thinLeaf);
        QCOMPARE(thinLeaf->height() * view.devicePixelRatio(), 1.0);
        QVERIFY(thinLeaf->width() > 0);
    }

    void navigationImageLeavesSnapThroughFractionalAncestors() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 700; height: 500
                property real ancestorOffset: 0.13
                Item {
                    id: viewerState
                    x: parent.ancestorOffset; y: 0.19
                    width: 613; height: 401
                    property Item customContent: null
                    property bool viewerContentVisible: true
                    property color backgroundColor: "black"
                    property color foregroundColor: "white"
                    property real surfaceProgress: 1
                    property bool transitionHasGeometry: false
                    property real transitionProgress: 1
                    property bool pinchCloseActive: false
                    property bool completingClose: false
                    property int animationDuration: 0
                    property real devicePixelRatio: 1.75
                    property GalleryThemePalette theme: GalleryThemePalette {}
                    property bool sphericViewerMode: false
                    property bool viewerNavigationActive: true
                    property bool viewerNavigationAnimationRunning: false
                    property bool viewerNavigationCommitAfterAnimation: false
                    property real viewerNavigationOffsetX: 0
                    property real viewerNavigationCurrentOpacity: 1
                    property real viewerNavigationCurrentOffsetX: 0.31
                    property int viewerNavigationDirection: 1
                    property real viewerNavigationTargetOpacity: 1
                    property int viewerNavigationTargetIndex: 1
                    property url viewerNavigationTargetSource: ""
                    property real viewerNavigationTargetImageX: 4.73
                    property real viewerNavigationTargetImageY: 11.27
                    property real viewerNavigationTargetDisplayWidth: 531.19
                    property real viewerNavigationTargetDisplayHeight: 353.77
                    property bool viewerNavigationTargetHasSize: true
                    property size viewerNavigationTargetDisplayOriginalSize:
                        Qt.size(1201 / 1.75, 799 / 1.75)
                    property real viewerNavigationTargetScale: 0.773
                    property var session: null
                    function scheduleDecodeRequest() {}
                    GalleryViewerSurface {
                        anchors.fill: parent
                        viewer: viewerState
                    }
                }
            }
        )QML", QStringLiteral("NavigationPhysicalGrid.qml"));
        QVERIFY(root);
        view.show();
        QTRY_VERIFY(view.isExposed());
        QCOMPARE(view.devicePixelRatio(), qreal(1.75));
        for (qreal offset : {0.13, 0.31}) {
            root->setProperty("ancestorOffset", offset);
            QCoreApplication::processEvents();
            for (const QString &name : {
                     QStringLiteral("galleryViewerNavigationNeighborImage"),
                     QStringLiteral("galleryViewerNavigationNeighborShader")}) {
                auto *leaf = root->findChild<QQuickItem *>(name);
                QVERIFY(leaf);
                const QPointF origin = leaf->mapToItem(view.contentItem(), QPointF());
                const qreal dpr = view.devicePixelRatio();
                const auto aligned = [dpr](qreal value) {
                    return qAbs(value * dpr - qRound(value * dpr)) < 0.001;
                };
                QVERIFY2(aligned(origin.x()), qPrintable(name));
                QVERIFY2(aligned(origin.y()), qPrintable(name));
                QVERIFY2(aligned(leaf->width()), qPrintable(name));
                QVERIFY2(aligned(leaf->height()), qPrintable(name));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(1, 0))
                             - origin, QPointF(1, 0));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(0, 1))
                             - origin, QPointF(0, 1));
            }
        }
    }

    void resamplerChoosesAndRetainsPyramidLevels();
    void nativeScaleShaderIsPixelExactThroughFractionalNavigation();
    void interactiveGeometryIsContinuousAndSettlesToPixelGrid();
    void centeredMinificationKeepsItsPhysicalCenter_data();
    void centeredMinificationKeepsItsPhysicalCenter();
    void wheelMinificationKeepsItsCenterDuringAnimation();

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

void ReusableViewerPrimitivesTest::resamplerChoosesAndRetainsPyramidLevels() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 10; height: 10
                property alias outputSize: resampler.viewportSize
                property alias inputSize: sourceMetadata.sourceSize
                property alias sourceKey: sourceMetadata.source
                property alias resampler: resampler
                readonly property int rgba8Format: ShaderEffectSource.RGBA8
                function selectLevel(w, h, outW, outH) {
                    return resampler.levelForSize(Qt.size(w, h),
                                                  Qt.size(outW, outH))
                }
                QtObject {
                    id: sourceMetadata
                    property size sourceSize: Qt.size(8000, 5000)
                    property url source: "file:///first.png"
                }
                ViewerResample {
                    id: resampler
                    imageSource: sourceMetadata
                    viewportSize: Qt.size(1728, 1080)
                }
            }
        )QML", QStringLiteral("ResamplerPyramid.qml"));
        QVERIFY(root);
        QObject *resampler = root->property("resampler").value<QObject *>();
        QVERIFY(resampler);
        QCOMPARE(resampler->property("requiredLevels").toInt(), 2);
        QCOMPARE(resampler->property("retainedLevels").toInt(), 2);
        QObject *levelTwo = resampler->property("source").value<QObject *>();
        QVERIFY(levelTwo);
        QCOMPARE(levelTwo->property("textureSize").toSize(), QSize(2000, 1250));

        root->setProperty("outputSize", QSize(7200, 4500));
        QCOMPARE(resampler->property("requiredLevels").toInt(), 0);
        QCOMPARE(resampler->property("retainedLevels").toInt(), 2);
        QCOMPARE(resampler->property("source").value<QObject *>(),
                 resampler->property("imageSource").value<QObject *>());
        root->setProperty("outputSize", QSize(1728, 1080));
        QCOMPARE(resampler->property("source").value<QObject *>(), levelTwo);

        root->setProperty("inputSize", QSize(8001, 5001));
        QObject *oddLevel = resampler->property("source").value<QObject *>();
        QVERIFY(oddLevel);
        QCOMPARE(oddLevel->property("textureSize").toSize(), QSize(2000, 1250));
        QObject *oddReduction = oddLevel->property("sourceItem").value<QObject *>();
        QVERIFY(oddReduction);
        QCOMPARE(oddReduction->property("sourceExtent").toSize(), QSize(4000, 2500));
        QObject *firstLevel = oddReduction->property("source").value<QObject *>();
        QVERIFY(firstLevel);
        QCOMPARE(firstLevel->property("textureSize").toSize(), QSize(4000, 2500));
        QObject *firstReduction = firstLevel->property("sourceItem").value<QObject *>();
        QVERIFY(firstReduction);
        QCOMPARE(firstReduction->property("sourceExtent").toSize(), QSize(8000, 5000));
        QCOMPARE(firstLevel->property("format").toInt(), root->property("rgba8Format").toInt());
        QCOMPARE(oddLevel->property("format").toInt(), root->property("rgba8Format").toInt());
        QCOMPARE(resampler->property("sourceExtent").toSize(), QSize(0, 0));
        root->setProperty("inputSize", QSize(8000, 1));
        root->setProperty("outputSize", QSize(1000, 1));
        QCOMPARE(resampler->property("requiredLevels").toInt(), 3);
        QObject *scanlineLevel = resampler->property("source").value<QObject *>();
        QVERIFY(scanlineLevel);
        QCOMPARE(scanlineLevel->property("textureSize").toSize(), QSize(1000, 1));
        QObject *scanlineReduction = scanlineLevel->property("sourceItem").value<QObject *>();
        QVERIFY(scanlineReduction);
        QCOMPARE(scanlineReduction->property("sourceExtent").toSize(), QSize(2000, 1));

        root->setProperty("outputSize", QSize(7200, 1));
        QCOMPARE(resampler->property("retainedLevels").toInt(), 3);
        root->setProperty("sourceKey", QUrl(QStringLiteral("file:///second.png")));
        QCOMPARE(resampler->property("retainedLevels").toInt(), 0);
        const int cases[][5] = {
            {8000, 5000, 7920, 4950, 0},
            {8000, 5000, 4000, 2500, 1},
            {8000, 5000, 4001, 2501, 0},
            {8001, 5001, 2001, 1251, 1},
            {8001, 5001, 2000, 1250, 2},
            {8192, 6144, 2048, 1536, 2},
            {8192, 6144, 1884, 1414, 2},
            {8000, 1, 1000, 1, 3}
        };
        for (const auto &dimensions : cases) {
            QVariant result;
            QVERIFY(QMetaObject::invokeMethod(root, "selectLevel",
                Q_RETURN_ARG(QVariant, result),
                Q_ARG(QVariant, dimensions[0]), Q_ARG(QVariant, dimensions[1]),
                Q_ARG(QVariant, dimensions[2]), Q_ARG(QVariant, dimensions[3])));
            QCOMPARE(result.toInt(), dimensions[4]);
        }
    }


void ReusableViewerPrimitivesTest::nativeScaleShaderIsPixelExactThroughFractionalNavigation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(
        QStringLiteral("native-pixel-grid.png"));
    QImage source(1201, 799, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    QVERIFY(source.save(sourcePath));

    QQuickView view;
    view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    QObject *root = createRoot(view, R"QML(
        import QtQuick
        import ZoinGallery 1.0

        Item {
            id: testRoot
            width: 700
            height: 500
            property url testSource
            property real navigationOffsetX: 0.17
            property real navigationOffsetY: 0.11
            property bool externalMoving: false
            property alias zoomable: zoomable
            function prepare() {
                zoomable.setImage(testSource, Qt.size(1201, 799), 0, 0)
                zoomable.setViewport(1, -71.137, -46.219)
                zoomable.setImage(testSource, Qt.size(1201, 799), 0, 2)
            }
            Item {
                x: 0.13
                y: 0.19
                transform: Translate {
                    x: testRoot.navigationOffsetX
                    y: testRoot.navigationOffsetY
                }
                FlickableZoomable {
                    id: zoomable
                    x: 0.23
                    y: 0.29
                    width: 480
                    height: 320
                    devicePixelRatio: 1.75
                    pixelAlignmentRevision: testRoot.navigationOffsetX
                                            + testRoot.navigationOffsetY
                    externalTransformMoving: testRoot.externalMoving
                    animationDuration: 80
                }
            }
        }
    )QML", QStringLiteral("NativeScalePhysicalGrid.qml"));
    QVERIFY(root);
    root->setProperty("testSource", QUrl::fromLocalFile(sourcePath));
    QVERIFY(QMetaObject::invokeMethod(root, "prepare"));
    view.show();
    QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);
    QCOMPARE(view.devicePixelRatio(), qreal(1.75));

    auto *shader = root->findChild<QQuickItem *>(
        QStringLiteral("galleryViewerImageShader"));
    auto *viewport = root->property("zoomable").value<QQuickItem *>();
    QVERIFY(shader);
    QVERIFY(viewport);
    QTRY_VERIFY_WITH_TIMEOUT(
        shader->property("pixelAlignedIdentity").toBool(), 5000);
    const qreal dpr = view.devicePixelRatio();
    const auto physicallyAligned = [dpr](qreal value) {
        return qAbs(value * dpr - qRound(value * dpr)) < 0.001;
    };

    for (const QPointF navigationOffset : {
             QPointF(0.17, 0.11), QPointF(0.31, 0.27)}) {
        root->setProperty("navigationOffsetX", navigationOffset.x());
        root->setProperty("navigationOffsetY", navigationOffset.y());
        QCoreApplication::processEvents();
        QCOMPARE(viewport->property("pixelAlignmentRevision").toReal(),
                 navigationOffset.x() + navigationOffset.y());
        QTRY_VERIFY(shader->property("pixelAlignedIdentity").toBool());

        const QPointF origin = shader->mapToItem(view.contentItem(), QPointF());
        const QPointF unitX = shader->mapToItem(
            view.contentItem(), QPointF(1, 0)) - origin;
        const QPointF unitY = shader->mapToItem(
            view.contentItem(), QPointF(0, 1)) - origin;
        const QString details = QStringLiteral(
            "navigation=(%1,%2) origin=(%3,%4) extent=(%5,%6) "
            "unitX=(%7,%8) unitY=(%9,%10)")
            .arg(navigationOffset.x()).arg(navigationOffset.y())
            .arg(origin.x() * dpr).arg(origin.y() * dpr)
            .arg(shader->width() * dpr).arg(shader->height() * dpr)
            .arg(unitX.x()).arg(unitX.y()).arg(unitY.x()).arg(unitY.y());
        QVERIFY2(physicallyAligned(origin.x()), qPrintable(details));
        QVERIFY2(physicallyAligned(origin.y()), qPrintable(details));
        QVERIFY2(physicallyAligned(shader->width()), qPrintable(details));
        QVERIFY2(physicallyAligned(shader->height()), qPrintable(details));
        QVERIFY2(qAbs(shader->width() * dpr - 1201) < 0.001,
                 qPrintable(details));
        QVERIFY2(qAbs(shader->height() * dpr - 799) < 0.001,
                 qPrintable(details));
        QVERIFY2(QLineF(unitX, QPointF(1, 0)).length() < 0.001,
                 qPrintable(details));
        QVERIFY2(QLineF(unitY, QPointF(0, 1)).length() < 0.001,
                 qPrintable(details));
    }

    // Host navigation is continuous while its Translate is moving. Exact
    // native fetch resumes only after the final transform has settled.
    root->setProperty("externalMoving", true);
    QCoreApplication::processEvents();
    QVERIFY(!shader->property("pixelAlignedIdentity").toBool());
    const QPointF movingOrigin = shader->mapToItem(
        view.contentItem(), QPointF());
    root->setProperty("navigationOffsetX", 0.447);
    root->setProperty("navigationOffsetY", 0.463);
    QCoreApplication::processEvents();
    const QPointF movedOrigin = shader->mapToItem(
        view.contentItem(), QPointF());
    QVERIFY(qAbs((movedOrigin.x() - movingOrigin.x()) * dpr
                 - 0.137 * dpr) < 0.001);
    QVERIFY(qAbs((movedOrigin.y() - movingOrigin.y()) * dpr
                 - 0.193 * dpr) < 0.001);
    root->setProperty("externalMoving", false);
    QTRY_VERIFY(shader->property("pixelAlignedIdentity").toBool());
    const QPointF settledOrigin = shader->mapToItem(
        view.contentItem(), QPointF());
    QVERIFY(physicallyAligned(settledOrigin.x()));
    QVERIFY(physicallyAligned(settledOrigin.y()));
}


void ReusableViewerPrimitivesTest::interactiveGeometryIsContinuousAndSettlesToPixelGrid() {
    QQuickView view;
    view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    QObject *root = createRoot(view, R"QML(
        import QtQuick
        import ZoinGallery 1.0

        Item {
            id: testRoot
            width: 700
            height: 500
            property alias zoomable: zoomable
            function prepare() {
                zoomable.originalSize = Qt.size(1201 / 1.75, 799 / 1.75)
                zoomable.setViewport(1, -71.137, -46.219)
            }
            function beginPinch() { zoomable.beginPinchZoom(233.17, 151.29) }
            function updatePinch(scale) { zoomable.updatePinchZoom(scale) }
            function finishPinch() { zoomable.finishPinchZoom() }
            function wheelZoom() {
                zoomable.handleZoomWheel(-120, Qt.ControlModifier, Qt.NoButton)
            }
            function beginWheelPan() { zoomable.beginWheelPan() }
            function wheelPan(dx, dy) { zoomable.panBy(dx, dy, true) }
            function finishWheelPan() { zoomable.finishWheelPan() }
            Item {
                x: 0.13
                y: 0.19
                transform: Translate { x: 0.17; y: 0.11 }
                FlickableZoomable {
                    id: zoomable
                    x: 0.23
                    y: 0.29
                    width: 480
                    height: 320
                    active: true
                    devicePixelRatio: 1.75
                    pixelAlignmentRevision: 0.28
                    animationDuration: 80
                }
            }
        }
    )QML", QStringLiteral("InteractivePhysicalGrid.qml"));
    QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root, "prepare"));
    view.show();
    view.requestActivate();
    QTRY_VERIFY_WITH_TIMEOUT(view.isExposed(), 3000);
    QCOMPARE(view.devicePixelRatio(), qreal(1.75));

    auto *viewport = root->property("zoomable").value<QQuickItem *>();
    auto *image = viewport ? viewport->property("image").value<QQuickItem *>()
                           : nullptr;
    auto *shader = root->findChild<QQuickItem *>(
        QStringLiteral("galleryViewerImageShader"));
    auto *pointer = root->findChild<QQuickItem *>(
        QStringLiteral("galleryViewerPointerArea"));
    QVERIFY(viewport);
    QVERIFY(image);
    QVERIFY(shader);
    QVERIFY(pointer);
    const qreal dpr = view.devicePixelRatio();
    const auto sceneOrigin = [&view](QQuickItem *item) {
        return item->mapToItem(view.contentItem(), QPointF());
    };
    const auto verifyContinuous = [&]() {
        const QPointF imageOrigin = sceneOrigin(image);
        const QPointF shaderOrigin = sceneOrigin(shader);
        const qreal scale = viewport->property("zoomScale").toReal();
        const QString details = QStringLiteral(
            "imageOrigin=(%1,%2) shaderOrigin=(%3,%4) scale=%5 extent=(%6,%7)")
            .arg(imageOrigin.x() * dpr).arg(imageOrigin.y() * dpr)
            .arg(shaderOrigin.x() * dpr).arg(shaderOrigin.y() * dpr)
            .arg(scale).arg(shader->width() * dpr).arg(shader->height() * dpr);
        QVERIFY2(QLineF(imageOrigin, shaderOrigin).length() < 0.001,
                 qPrintable(details));
        QVERIFY2(qAbs(shader->width() * dpr - 1201 * scale) < 0.001,
                 qPrintable(details));
        QVERIFY2(qAbs(shader->height() * dpr - 799 * scale) < 0.001,
                 qPrintable(details));
    };
    const auto verifySettled = [&]() {
        QTRY_VERIFY_WITH_TIMEOUT(
            !viewport->property("viewportAnimationRunning").toBool(), 1500);
        QCoreApplication::processEvents();
        const QPointF origin = sceneOrigin(shader);
        const QPointF corner = shader->mapToItem(
            view.contentItem(), QPointF(shader->width(), shader->height()));
        const QString details = QStringLiteral(
            "settled origin=(%1,%2) corner=(%3,%4) extent=(%5,%6)")
            .arg(origin.x() * dpr).arg(origin.y() * dpr)
            .arg(corner.x() * dpr).arg(corner.y() * dpr)
            .arg(shader->width() * dpr).arg(shader->height() * dpr);
        for (const qreal edge : {origin.x() * dpr, origin.y() * dpr,
                                 corner.x() * dpr, corner.y() * dpr}) {
            QVERIFY2(qAbs(edge - qRound(edge)) < 0.001, qPrintable(details));
        }
    };

    // A press without motion must not switch geometry or sampling modes.
    const QPointF beforePressOrigin = sceneOrigin(shader);
    const QSizeF beforePressExtent(shader->width(), shader->height());
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(240, 160));
    QTRY_VERIFY(pointer->property("pressed").toBool());
    QCoreApplication::processEvents();
    QCOMPARE(sceneOrigin(shader), beforePressOrigin);
    QCOMPARE(QSizeF(shader->width(), shader->height()), beforePressExtent);

    // The real pointer path starts continuous presentation in the same event
    // that applies its first fractional delta.
    QTest::mouseMove(&view, QPoint(247, 155));
    QTRY_VERIFY(viewport->property("viewportAnimationRunning").toBool());
    verifyContinuous();
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(247, 155));
    verifySettled();

    // Pinch changes both scale and position synchronously while the fingers move.
    QVERIFY(QMetaObject::invokeMethod(root, "prepare"));
    QVERIFY(QMetaObject::invokeMethod(root, "beginPinch"));
    QVERIFY(QMetaObject::invokeMethod(root, "updatePinch",
        Q_ARG(QVariant, 1.0137)));
    QCoreApplication::processEvents();
    verifyContinuous();
    QVERIFY(QMetaObject::invokeMethod(root, "finishPinch"));
    verifySettled();

    // Ctrl-wheel zoom and its animation stay continuous, then return to the grid.
    QVERIFY(QMetaObject::invokeMethod(root, "wheelZoom"));
    QTRY_VERIFY(viewport->property("viewportAnimationRunning").toBool());
    verifyContinuous();
    QTest::qWait(16);
    verifyContinuous();
    verifySettled();

    // Wheel panning is direct manipulation; finishWheelPan starts inertia.
    QVERIFY(QMetaObject::invokeMethod(root, "beginWheelPan"));
    QVERIFY(QMetaObject::invokeMethod(root, "wheelPan",
        Q_ARG(QVariant, -0.137), Q_ARG(QVariant, 0.193)));
    QCoreApplication::processEvents();
    verifyContinuous();
    QVERIFY(QMetaObject::invokeMethod(root, "finishWheelPan"));
    QTRY_VERIFY(viewport->property("viewportAnimationRunning").toBool());
    verifyContinuous();
    QTest::qWait(16);
    verifyContinuous();
    verifySettled();
}


void ReusableViewerPrimitivesTest::centeredMinificationKeepsItsPhysicalCenter_data() {
    QTest::addColumn<QSize>("nativeSize");
    QTest::addColumn<int>("rotation");
    QTest::newRow("odd") << QSize(1201, 799) << 0;
    QTest::newRow("even") << QSize(1200, 800) << 0;
    QTest::newRow("mixed-rotated") << QSize(1201, 800) << 1;
}

void ReusableViewerPrimitivesTest::centeredMinificationKeepsItsPhysicalCenter() {
    QFETCH(QSize, nativeSize);
    QFETCH(int, rotation);
    QQuickView view;
    view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    QObject *root = createRoot(view, R"QML(
        import QtQuick
        import ZoinGallery 1.0
        Item {
            width: 700; height: 500
            function prepare(w, h, rotation) {
                zoomable.originalSize = Qt.size(w / 1.75, h / 1.75)
                zoomable.rotationMode = rotation
            }
            function applyScale(value) { zoomable.setViewport(value, 0, 0) }
            function fitNativeViewport(w, h) {
                zoomable.width = w / 1.75
                zoomable.height = h / 1.75
                zoomable.setViewport(1, 0, 0)
            }
            Item {
                x: 0.13; y: 0.19
                FlickableZoomable {
                    id: zoomable
                    x: 0.23; y: 0.29
                    width: 613; height: 401
                    devicePixelRatio: 1.75
                    animationDuration: 0
                }
            }
        }
    )QML", QStringLiteral("CenteredMinification.qml"));
    QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root, "prepare",
        Q_ARG(QVariant, nativeSize.width()), Q_ARG(QVariant, nativeSize.height()),
        Q_ARG(QVariant, rotation)));
    view.show();
    QTRY_VERIFY(view.isExposed());
    const qreal dpr = view.devicePixelRatio();
    QCOMPARE(dpr, qreal(1.75));
    auto *leaf = root->findChild<QQuickItem *>(
        QStringLiteral("galleryViewerImageShader"));
    QVERIFY(leaf);
    QPointF fixedCenter;
    for (int step = 0; step != 30; ++step) {
        const qreal scale = 0.3 - step * 0.001;
        QVERIFY(QMetaObject::invokeMethod(root, "applyScale", Q_ARG(QVariant, scale)));
        QCoreApplication::processEvents();
        const QPointF origin = leaf->mapToItem(view.contentItem(), QPointF()) * dpr;
        const QPointF corner = leaf->mapToItem(view.contentItem(),
            QPointF(leaf->width(), leaf->height())) * dpr;
        const QPointF center = (origin + corner) / 2;
        if (step == 0) fixedCenter = center;
        const QString details = QStringLiteral(
            "step=%1 scale=%2 origin=(%3,%4) center=(%5,%6) expected=(%7,%8)")
            .arg(step).arg(scale).arg(origin.x()).arg(origin.y())
            .arg(center.x()).arg(center.y()).arg(fixedCenter.x()).arg(fixedCenter.y());
        QVERIFY2(qAbs(center.x() - fixedCenter.x()) < 0.001, qPrintable(details));
        QVERIFY2(qAbs(center.y() - fixedCenter.y()) < 0.001, qPrintable(details));
        for (qreal edge : {origin.x(), origin.y(), corner.x(), corner.y()})
            QVERIFY2(qAbs(edge - qRound(edge)) < 0.001, qPrintable(details));
        QVERIFY(qAbs(leaf->width() * dpr - nativeSize.width() * scale) <= 1.001);
        QVERIFY(qAbs(leaf->height() * dpr - nativeSize.height() * scale) <= 1.001);
    }
    QVERIFY(QMetaObject::invokeMethod(root, "applyScale", Q_ARG(QVariant, 1.0)));
    QCOMPARE(qRound(leaf->width() * dpr), nativeSize.width());
    QCOMPARE(qRound(leaf->height() * dpr), nativeSize.height());
    // A fractional ancestor can leave less than N physical pixels inside an
    // N-pixel viewport after alignment. Native 1:1 must still remain N pixels.
    QVERIFY(QMetaObject::invokeMethod(root, "fitNativeViewport",
        Q_ARG(QVariant, nativeSize.width()), Q_ARG(QVariant, nativeSize.height())));
    QCOMPARE(qRound(leaf->width() * dpr), nativeSize.width());
    QCOMPARE(qRound(leaf->height() * dpr), nativeSize.height());

    // The terminal one-pixel size cannot preserve even source parity. It
    // must stay visible and aligned instead of becoming a two-pixel scanline.
    QVERIFY(QMetaObject::invokeMethod(root, "prepare", Q_ARG(QVariant, 2),
        Q_ARG(QVariant, 2), Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(root, "applyScale", Q_ARG(QVariant, 0.5)));
    QCOMPARE(leaf->width() * dpr, 1.0);
    QCOMPARE(leaf->height() * dpr, 1.0);
    const QPointF terminalOrigin = leaf->mapToItem(view.contentItem(), QPointF()) * dpr;
    QVERIFY(qAbs(terminalOrigin.x() - qRound(terminalOrigin.x())) < 0.001);
    QVERIFY(qAbs(terminalOrigin.y() - qRound(terminalOrigin.y())) < 0.001);
}

void ReusableViewerPrimitivesTest::wheelMinificationKeepsItsCenterDuringAnimation() {
    QQuickView view;
    view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    QObject *root = createRoot(view, R"QML(
        import QtQuick
        import ZoinGallery 1.0
        Item {
            width: 700; height: 500
            property alias zoomable: zoomable
            function prepare() {
                zoomable.originalSize = Qt.size(1201 / 1.75, 799 / 1.75)
                zoomable.setViewport(0.3, 0, 0)
            }
            function zoomOut() {
                zoomable.handleZoomWheel(-120, Qt.ControlModifier, Qt.NoButton)
            }
            Item {
                x: 0.13; y: 0.19
                FlickableZoomable {
                    id: zoomable
                    x: 0.23; y: 0.29
                    width: 613; height: 401
                    devicePixelRatio: 1.75
                    animationDuration: 100
                }
            }
        }
    )QML", QStringLiteral("CenteredWheelMinification.qml"));
    QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root, "prepare"));
    view.show();
    QTRY_VERIFY(view.isExposed());
    const qreal dpr = view.devicePixelRatio();
    QCOMPARE(dpr, qreal(1.75));
    auto *viewport = root->property("zoomable").value<QQuickItem *>();
    auto *leaf = root->findChild<QQuickItem *>(
        QStringLiteral("galleryViewerImageShader"));
    QVERIFY(viewport);
    QVERIFY(leaf);
    const QPointF fixedCenter = leaf->mapToItem(view.contentItem(),
        QPointF(leaf->width() / 2, leaf->height() / 2)) * dpr;
    for (int gesture = 0; gesture != 3; ++gesture) {
        QVERIFY(QMetaObject::invokeMethod(root, "zoomOut"));
        QTRY_VERIFY(viewport->property("viewportAnimationRunning").toBool());
        bool sampledAnimation = false;
        for (int sample = 0; sample != 200
             && viewport->property("viewportAnimationRunning").toBool(); ++sample) {
            const QPointF center = leaf->mapToItem(view.contentItem(),
                QPointF(leaf->width() / 2, leaf->height() / 2)) * dpr;
            const qreal scale = viewport->property("zoomScale").toReal();
            const QString details = QStringLiteral(
                "gesture=%1 scale=%2 center=(%3,%4) expected=(%5,%6) extent=(%7,%8)")
                .arg(gesture).arg(scale).arg(center.x()).arg(center.y())
                .arg(fixedCenter.x()).arg(fixedCenter.y())
                .arg(leaf->width() * dpr).arg(leaf->height() * dpr);
            QVERIFY2(qAbs(center.x() - fixedCenter.x()) < 0.001, qPrintable(details));
            QVERIFY2(qAbs(center.y() - fixedCenter.y()) < 0.001, qPrintable(details));
            QVERIFY2(qAbs(leaf->width() * dpr - 1201 * scale) < 0.001,
                     qPrintable(details));
            QVERIFY2(qAbs(leaf->height() * dpr - 799 * scale) < 0.001,
                     qPrintable(details));
            sampledAnimation = true;
            QTest::qWait(8);
        }
        QVERIFY(sampledAnimation);
        QVERIFY(!viewport->property("viewportAnimationRunning").toBool());
        const QPointF origin = leaf->mapToItem(view.contentItem(), QPointF()) * dpr;
        const QPointF center = leaf->mapToItem(view.contentItem(),
            QPointF(leaf->width() / 2, leaf->height() / 2)) * dpr;
        QVERIFY(qAbs(center.x() - fixedCenter.x()) < 0.001);
        QVERIFY(qAbs(center.y() - fixedCenter.y()) < 0.001);
        QVERIFY(qAbs(origin.x() - qRound(origin.x())) < 0.001);
        QVERIFY(qAbs(origin.y() - qRound(origin.y())) < 0.001);
    }
}

QTEST_MAIN(ReusableViewerPrimitivesTest)
#include "ReusableViewerPrimitivesTest.moc"
