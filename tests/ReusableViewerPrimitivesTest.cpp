#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest>

#include <cmath>

namespace {

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

class ReusableViewerPrimitivesTest : public QObject {
    Q_OBJECT

private slots:
    void pathControlAcceptsNativeWindowsSeparators() {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));

        QObject *root = createRoot(view, R"QML(
            import QtQuick
            import ZoinGallery 1.0

            Item {
                id: testRoot
                width: 640
                height: 80
                property string navigatedPath

                PathControl {
                    id: pathControl
                    objectName: "pathControl"
                    anchors.fill: parent
                    windowsPathSeparators: true
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

        QCOMPARE(pathControl->property("normalizedText").toString(),
                 QStringLiteral("C:/Users/Alice/Pictures"));
        QCOMPARE(pathControl->property("breadcrumbs").toList(),
                 QVariantList({QStringLiteral("C:"), QStringLiteral("Users"),
                               QStringLiteral("Alice"),
                               QStringLiteral("Pictures")}));

        pathControl->setProperty("editMode", true);
        QCoreApplication::processEvents();
        QObject *pathField = root->findChild<QObject *>(
            QStringLiteral("pathField"));
        QVERIFY(pathField);
        QCOMPARE(pathField->property("text").toString(),
                 QStringLiteral("C:\\Users\\Alice\\Pictures"));

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
