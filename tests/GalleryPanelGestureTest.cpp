#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

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

#include <utility>

namespace {

QVariantMap imageEntry(const QString &id, int index, const QString &path) {
    const QFileInfo file(path);
    return {
        {QStringLiteral("entryId"), id},
        {QStringLiteral("index"), index},
        {QStringLiteral("name"), file.fileName()},
        {QStringLiteral("localPath"), path},
        {QStringLiteral("isDir"), false},
        {QStringLiteral("isImage"), true},
        {QStringLiteral("selected"), false},
        {QStringLiteral("mtimeNs"),
         file.lastModified().toMSecsSinceEpoch() * 1'000'000 + index + 1},
        {QStringLiteral("size"), file.size()},
    };
}

QVariantMap folderEntry(const QString &id, int index, const QString &path) {
    return {
        {QStringLiteral("entryId"), id},
        {QStringLiteral("index"), index},
        {QStringLiteral("name"), QFileInfo(path).fileName()},
        {QStringLiteral("localPath"), path},
        {QStringLiteral("isDir"), true},
        {QStringLiteral("isImage"), false},
        {QStringLiteral("selected"), false},
        {QStringLiteral("mtimeNs"), qint64(0)},
        {QStringLiteral("size"), qint64(0)},
    };
}

bool indexIsVisible(QObject *layout, int index) {
    QRectF geometry;
    if (!QMetaObject::invokeMethod(layout, "indexGeometry",
                                  Q_RETURN_ARG(QRectF, geometry),
                                  Q_ARG(int, index)) ||
        geometry.width() <= 0 || geometry.height() <= 0) {
        return false;
    }
    const qreal contentY = layout->property("contentY").toReal();
    const qreal viewportBottom = contentY + layout->property("height").toReal();
    return geometry.top() >= contentY - 0.5 &&
           geometry.bottom() <= viewportBottom + 0.5;
}

QRectF indexGeometry(QObject *layout, int index) {
    QRectF geometry;
    const bool invoked = QMetaObject::invokeMethod(
        layout, "indexGeometry", Q_RETURN_ARG(QRectF, geometry),
        Q_ARG(int, index));
    return invoked ? geometry : QRectF{};
}

bool animationEverRan(const QSignalSpy &spy) {
    for (const QList<QVariant> &arguments : spy) {
        if (!arguments.isEmpty() && arguments.constFirst().toBool()) {
            return true;
        }
    }
    return false;
}

QObject *createPanel(QQuickView &view) {
    auto *component = new QQmlComponent(view.engine(), &view);
    component->setData(R"QML(
        import QtQuick
        import ZoinGallery 1.0
        GalleryPanel {
            objectName: "panel"
            width: 340
            height: 230
            thumbnailHeight: 90
            session: testSession
        }
    )QML", QUrl(QStringLiteral("inline:GalleryPanelGesture.qml")));
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
    view.setContent(QUrl(QStringLiteral("inline:GalleryPanelGesture.qml")),
                    component, root);
    return root;
}

bool invoke(QObject *target, const char *method,
            const QVariant &first = {}, const QVariant &second = {},
            const QVariant &third = {}) {
    if (!first.isValid()) {
        return QMetaObject::invokeMethod(target, method);
    }
    if (!second.isValid()) {
        return QMetaObject::invokeMethod(target, method,
                                         Q_ARG(QVariant, first));
    }
    if (!third.isValid()) {
        return QMetaObject::invokeMethod(target, method,
                                         Q_ARG(QVariant, first),
                                         Q_ARG(QVariant, second));
    }
    return QMetaObject::invokeMethod(target, method,
                                     Q_ARG(QVariant, first),
                                     Q_ARG(QVariant, second),
                                     Q_ARG(QVariant, third));
}

bool invokeWheel(QObject *target, qreal pixelY, qreal angleY,
                 int modifiers, qreal pixelX, qreal angleX) {
    QVariant handled;
    return QMetaObject::invokeMethod(
        target, "handlePanelWheel", Qt::DirectConnection,
        Q_RETURN_ARG(QVariant, handled),
        Q_ARG(QVariant, pixelY), Q_ARG(QVariant, angleY),
        Q_ARG(QVariant, modifiers), Q_ARG(QVariant, pixelX),
        Q_ARG(QVariant, angleX)) && handled.toBool();
}

} // namespace

class GalleryPanelGestureTest : public QObject {
    Q_OBJECT

private slots:
    void gridPinchKeepsTopBrickAndFractionalOffset() {
        QVariantList catalog;
        for (int index = 0; index < 120; ++index) {
            catalog.append(folderEntry(
                QStringLiteral("folder-%1").arg(index), index,
                QStringLiteral("/tmp/folder-%1").arg(index)));
        }

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("grid-pinch-anchor"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("folder-0"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *panel = createPanel(view);
        QVERIFY(panel);
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        auto *layout = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *scrollAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QVERIFY(layout);
        QVERIFY(scrollAnimation);
        view.show();
        QTRY_VERIFY(!panel->property("presentationSwitchPending").toBool());

        layout->setProperty("density", 96.0);
        QTRY_COMPARE(layout->property("density").toReal(), qreal(96));
        QTRY_VERIFY(indexGeometry(layout, 21).top() >
                    indexGeometry(layout, 18).top());
        QCOMPARE(indexGeometry(layout, 18).top(),
                 indexGeometry(layout, 20).top());

        constexpr int anchorIndex = 18;
        constexpr qreal clippedOffset = 23.75;
        const QRectF beforeGeometry = indexGeometry(layout, anchorIndex);
        QVERIFY(beforeGeometry.isValid());
        QVERIFY(invoke(panel, "setPanelContentY",
                       beforeGeometry.top() + clippedOffset, true));
        const qreal beforeViewportY = beforeGeometry.top() -
            layout->property("contentY").toReal();
        QVERIFY(qAbs(beforeViewportY + clippedOffset) < 0.001);

        QVERIFY(invoke(panel, "beginThumbnailPinch"));
        QVERIFY(invoke(panel, "updateThumbnailPinch", 1.5));
        QCOMPARE(layout->property("density").toReal(), qreal(144));
        // 340 logical pixels fit three 96px cells but only two 144px cells.
        // Index 18 starts a row in both lattices, making its identity and
        // fractional viewport position an unambiguous visual anchor.
        QCOMPARE(indexGeometry(layout, 18).top(),
                 indexGeometry(layout, 19).top());
        QVERIFY(indexGeometry(layout, 20).top() >
                indexGeometry(layout, 18).top());
        const auto assertAnchor = [&] {
            const QRectF geometry = indexGeometry(layout, anchorIndex);
            QVERIFY(geometry.isValid());
            QVERIFY(qAbs((geometry.top() -
                          layout->property("contentY").toReal()) -
                         beforeViewportY) < 0.01);
            QVERIFY(!scrollAnimation->property("running").toBool());
            QVERIFY(qAbs(session->panelScrollOffset() -
                         layout->property("contentY").toReal()) < 0.01);
        };
        assertAnchor();
        QCoreApplication::processEvents();
        QTest::qWait(20);
        assertAnchor();
        QVERIFY(invoke(panel, "finishThumbnailPinch"));
        QCoreApplication::processEvents();
        QTest::qWait(20);
        assertAnchor();

        // Reversing the gesture must restore the three-column lattice around
        // the very same semantic brick, without accumulating phase drift.
        QVERIFY(invoke(panel, "beginThumbnailPinch"));
        QVERIFY(invoke(panel, "updateThumbnailPinch", 2.0 / 3.0));
        QCOMPARE(layout->property("density").toReal(), qreal(96));
        assertAnchor();
        QVERIFY(invoke(panel, "finishThumbnailPinch"));
        QCoreApplication::processEvents();
        QTest::qWait(20);
        assertAnchor();

        // The upper endpoint is a real clamp: zooming at the first row must
        // remain exactly at zero rather than manufacturing a negative offset.
        layout->setProperty("density", 96.0);
        QVERIFY(invoke(panel, "setPanelContentY", 0.0, true));
        QVERIFY(invoke(panel, "beginThumbnailPinch"));
        QVERIFY(invoke(panel, "updateThumbnailPinch", 1.5));
        QCOMPARE(layout->property("contentY").toReal(), qreal(0));
        QCOMPARE(indexGeometry(layout, 0).top(), qreal(0));
        QVERIFY(invoke(panel, "finishThumbnailPinch"));
        QCoreApplication::processEvents();
        QCOMPARE(layout->property("contentY").toReal(), qreal(0));

        runtime->shutdown();
    }

    void columnsPreferHorizontalTrackpadGestureAndKeepVerticalWheelFallback() {
        QVariantList catalog;
        for (int index = 0; index < 120; ++index) {
            catalog.append(folderEntry(QStringLiteral("folder-%1").arg(index),
                                       index, QStringLiteral("/tmp/folder-%1").arg(index)));
        }
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("columns-wheel-axis"));
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        view.engine()->rootContext()->setContextProperty(QStringLiteral("testSession"), session);
        QObject *panel = createPanel(view);
        QVERIFY(panel);
        panel->setProperty("presentationMode", QStringLiteral("columns"));
        auto *layout = panel->findChild<QQuickItem *>(QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_VERIFY(layout->property("contentHeight").toReal() > layout->width());

        const qreal start = layout->property("contentY").toReal();
        QVERIFY(invokeWheel(panel, 7, 0, int(Qt::NoModifier), -53, 0));
        QCOMPARE(layout->property("contentY").toReal(), start + 53);
        QVERIFY(invokeWheel(panel, -31, 0, int(Qt::NoModifier), 0, 0));
        QCOMPARE(layout->property("contentY").toReal(), start + 84);
    }

    void positionsInitialAndRecreatedAuthoritativeCursorInstantly() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QVariantList catalog;
        for (int index = 0; index < 60; ++index) {
            catalog.append(folderEntry(
                QStringLiteral("folder-%1").arg(index), index,
                directory.filePath(QStringLiteral("folder-%1").arg(index))));
        }

        QQuickView view;
        view.resize(340, 230);
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-panel-instant-restore"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("folder-55"), 55, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QQmlComponent component(view.engine());
        component.setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            GalleryPanel {
                width: 340
                height: 230
                thumbnailHeight: 90
                session: testSession
            }
        )QML", QUrl(QStringLiteral("inline:GalleryPanelInstantRestore.qml")));
        if (component.isLoading()) {
            QSignalSpy statusSpy(&component, &QQmlComponent::statusChanged);
            statusSpy.wait(5000);
        }
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        auto createTrackedPanel = [&]() {
            QObject *created = component.beginCreate(
                view.engine()->rootContext());
            auto *panel = qobject_cast<QQuickItem *>(created);
            if (!panel) {
                component.completeCreate();
                return std::pair<QQuickItem *, QSignalSpy *>(nullptr, nullptr);
            }
            panel->setParentItem(view.contentItem());
            panel->setParent(&view);
            auto *animation = panel->findChild<QObject *>(
                QStringLiteral("galleryPanelScrollAnimation"));
            auto *spy = animation
                ? new QSignalSpy(animation, SIGNAL(runningChanged(bool)))
                : nullptr;
            component.completeCreate();
            return std::pair<QQuickItem *, QSignalSpy *>(panel, spy);
        };

        auto [initialPanel, initialAnimationSpy] = createTrackedPanel();
        QVERIFY(initialPanel);
        QVERIFY(initialAnimationSpy);
        QVERIFY(initialAnimationSpy->isValid());
        auto *initialLayout = initialPanel->findChild<QObject *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *initialAnimation = initialPanel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QVERIFY(initialLayout);
        QVERIFY(initialAnimation);
        QVERIFY(!initialAnimation->property("running").toBool());

        view.show();
        QTRY_VERIFY_WITH_TIMEOUT(indexIsVisible(initialLayout, 55), 5000);
        QTest::qWait(250);
        QVERIFY(indexIsVisible(initialLayout, 55));
        QVERIFY(!initialAnimation->property("running").toBool());
        QVERIFY(!animationEverRan(*initialAnimationSpy));
        QVERIFY(session->panelScrollOffset() > 0);
        QCOMPARE(session->panelViewportCursorEntryId(),
                 QStringLiteral("folder-55"));

        // A top offset is still an explicitly saved viewport. Recreating an
        // embedder's Loader must not interpret zero as "no saved position"
        // and jump back down to a cursor which the user intentionally left
        // outside the viewport.
        QVERIFY(invoke(initialPanel, "setPanelContentY", qreal(0), true));
        QTRY_COMPARE(initialLayout->property("contentY").toReal(), qreal(0));
        QCOMPARE(session->panelScrollOffset(), qreal(0));
        QCOMPARE(session->panelViewportCursorEntryId(),
                 QStringLiteral("folder-55"));

        delete initialAnimationSpy;
        delete initialPanel;

        auto [topPanel, topAnimationSpy] = createTrackedPanel();
        QVERIFY(topPanel);
        QVERIFY(topAnimationSpy);
        QVERIFY(topAnimationSpy->isValid());
        auto *topLayout = topPanel->findChild<QObject *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *topAnimation = topPanel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QVERIFY(topLayout);
        QVERIFY(topAnimation);
        QTRY_COMPARE(topLayout->property("count").toInt(), 60);
        QTRY_COMPARE(topLayout->property("contentY").toReal(), qreal(0));
        QTest::qWait(250);
        QCOMPARE(topLayout->property("contentY").toReal(), qreal(0));
        QVERIFY(!indexIsVisible(topLayout, 55));
        QVERIFY(!topAnimation->property("running").toBool());
        QVERIFY(!animationEverRan(*topAnimationSpy));

        delete topAnimationSpy;
        delete topPanel;
        QVERIFY(session->applyExternalState(
            QStringLiteral("folder-0"), 0, {}, 2));

        auto [recreatedPanel, recreatedAnimationSpy] = createTrackedPanel();
        QVERIFY(recreatedPanel);
        QVERIFY(recreatedAnimationSpy);
        QVERIFY(recreatedAnimationSpy->isValid());
        auto *recreatedLayout = recreatedPanel->findChild<QObject *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *recreatedAnimation = recreatedPanel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QVERIFY(recreatedLayout);
        QVERIFY(recreatedAnimation);
        QVERIFY(!recreatedAnimation->property("running").toBool());

        QTRY_VERIFY_WITH_TIMEOUT(indexIsVisible(recreatedLayout, 0), 5000);
        QTest::qWait(250);
        QVERIFY(indexIsVisible(recreatedLayout, 0));
        QVERIFY(!recreatedAnimation->property("running").toBool());
        QVERIFY(!animationEverRan(*recreatedAnimationSpy));
        QCOMPARE(session->panelScrollOffset(), qreal(0));
        QCOMPARE(session->panelViewportCursorEntryId(),
                 QStringLiteral("folder-0"));

        delete recreatedAnimationSpy;
        runtime->shutdown();
    }

    void detailsPathChangeCentersCursorBeforePaintingCatalog() {
        QVariantList firstCatalog;
        QVariantList secondCatalog;
        for (int index = 0; index < 80; ++index) {
            firstCatalog.append(folderEntry(
                QStringLiteral("first-%1").arg(index), index,
                QStringLiteral("/first/folder-%1").arg(index)));
            secondCatalog.append(folderEntry(
                QStringLiteral("second-%1").arg(index), index,
                QStringLiteral("/second/folder-%1").arg(index)));
        }

        QQuickView view;
        view.resize(340, 230);
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("details-path-centered-restore"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(firstCatalog, 1, {
            {QStringLiteral("currentPath"), QStringLiteral("/first")},
        }));
        QVERIFY(session->applyExternalState(
            QStringLiteral("first-0"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *panel = createPanel(view);
        QVERIFY(panel);
        panel->setProperty("presentationMode", QStringLiteral("details"));
        panel->setProperty("density", 30.0);
        auto *layout = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *scrollAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QVERIFY(layout);
        QVERIFY(scrollAnimation);
        view.show();
        QTRY_VERIFY(!panel->property("presentationSwitchPending").toBool());
        QTRY_COMPARE(layout->property("count").toInt(), 80);

        const qreal rememberedFirstContentY = 180;
        layout->setProperty("contentY", rememberedFirstContentY);
        session->setPanelScrollOffset(rememberedFirstContentY);
        session->setPanelViewportCursorEntryId(QStringLiteral("first-0"));

        // A cache miss publishes a small provisional catalog while the VFS
        // read is in flight. It must remain hidden instead of flashing an
        // almost-empty panel.
        QVERIFY(session->applyExternalCatalog(
            QVariantList{secondCatalog.constFirst()}, 2, {
            {QStringLiteral("currentPath"), QStringLiteral("/second")},
            {QStringLiteral("catalogProvisional"), true},
        }));
        QVERIFY(panel->property("pathViewportPlacementPending").toBool());
        QCOMPARE(layout->opacity(), qreal(0));

        QVERIFY(session->applyExternalCatalog(secondCatalog, 3, {
            {QStringLiteral("currentPath"), QStringLiteral("/second")},
            {QStringLiteral("deferCatalogReady"), true},
        }));
        QVERIFY(session->applyExternalState(
            QStringLiteral("second-50"), 50, {}, 3));
        session->setExternalCatalogReady(true);

        // The Details layout already has valid geometry, so the authoritative
        // index completes placement synchronously without an extra event-loop
        // or frame boundary.
        QVERIFY(!panel->property("pathViewportPlacementPending").toBool());
        auto *placementTimer = panel->findChild<QObject *>(
            QStringLiteral("galleryPathViewportPlacementTimer"));
        QVERIFY(placementTimer);
        QVERIFY(!placementTimer->property("running").toBool());
        QCOMPARE(layout->opacity(), qreal(1));
        QVERIFY(!scrollAnimation->property("running").toBool());
        const QRectF geometry = indexGeometry(layout, 50);
        QVERIFY(geometry.isValid());
        const qreal viewportCenter = layout->property("height").toReal() / 2;
        const qreal cursorCenter = geometry.center().y()
            - layout->property("contentY").toReal();
        QVERIFY(qAbs(cursorCenter - viewportCenter) < 0.51);
        QVERIFY(qAbs(session->panelScrollOffset()
                     - layout->property("contentY").toReal()) < 0.51);
        QCOMPARE(session->panelViewportCursorEntryId(),
                 QStringLiteral("second-50"));

        // Returning to a directory restores its exact session viewport. The
        // selected child may change, but the previously visible top remains
        // stable instead of being centered again.
        QVERIFY(session->applyExternalCatalog(firstCatalog, 4, {
            {QStringLiteral("currentPath"), QStringLiteral("/first")},
            {QStringLiteral("deferCatalogReady"), true},
        }));
        QVERIFY(session->applyExternalState(
            QStringLiteral("first-40"), 40, {}, 4));
        session->setExternalCatalogReady(true);
        QVERIFY(!panel->property("pathViewportPlacementPending").toBool());
        QCOMPARE(layout->opacity(), qreal(1));
        QVERIFY(qAbs(layout->property("contentY").toReal()
                     - rememberedFirstContentY) < 0.51);
        QCOMPARE(session->panelViewportCursorEntryId(),
                 QStringLiteral("first-0"));

        runtime->shutdown();
    }

    void detailsPathChangeWaitsThroughOverlappingIdRemap() {
        QVariantList firstCatalog;
        QVariantList secondCatalog;
        for (int index = 0; index < 80; ++index) {
            firstCatalog.append(folderEntry(
                QStringLiteral("shared-%1").arg(index), index,
                QStringLiteral("/first/folder-%1").arg(index)));
            const int reorderedId = index == 5 ? 50
                                  : index == 50 ? 5 : index;
            secondCatalog.append(folderEntry(
                QStringLiteral("shared-%1").arg(reorderedId), index,
                QStringLiteral("/second/folder-%1").arg(reorderedId)));
        }

        QQuickView view;
        view.resize(340, 230);
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("details-overlapping-id-remap"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(firstCatalog, 1, {
            {QStringLiteral("currentPath"), QStringLiteral("/first")},
        }));
        QVERIFY(session->applyExternalState(
            QStringLiteral("shared-50"), 50, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *panel = createPanel(view);
        QVERIFY(panel);
        panel->setProperty("presentationMode", QStringLiteral("details"));
        panel->setProperty("density", 30.0);
        auto *layout = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *placementTimer = panel->findChild<QObject *>(
            QStringLiteral("galleryPathViewportPlacementTimer"));
        QVERIFY(layout);
        QVERIFY(placementTimer);
        view.show();
        QTRY_VERIFY(!panel->property("presentationSwitchPending").toBool());
        QTRY_COMPARE(layout->property("count").toInt(), 80);

        QVERIFY(session->applyExternalCatalog(secondCatalog, 2, {
            {QStringLiteral("currentPath"), QStringLiteral("/second")},
            {QStringLiteral("deferCatalogReady"), true},
        }));
        QCOMPARE(session->currentIndex(), 5);
        QCOMPARE(session->cursorEntryId(), QStringLiteral("shared-50"));
        // The shared ID remaps before catalogRevisionChanged. It is not the
        // host's authoritative destination and must leave the replacement
        // hidden with its zero-delay fallback still armed.
        QVERIFY(panel->property("pathViewportPlacementPending").toBool());
        QVERIFY(!panel->property("pathViewportCatalogReady").toBool());
        QVERIFY(placementTimer->property("running").toBool());
        QCOMPARE(layout->opacity(), qreal(0));

        QVERIFY(session->applyExternalState(
            QStringLiteral("shared-70"), 70, {}, 2));
        session->setExternalCatalogReady(true);
        QVERIFY(!panel->property("pathViewportPlacementPending").toBool());
        QVERIFY(!placementTimer->property("running").toBool());
        QCOMPARE(layout->opacity(), qreal(1));
        const QRectF geometry = indexGeometry(layout, 70);
        QVERIFY(geometry.isValid());
        const qreal viewportCenter = layout->property("height").toReal() / 2;
        const qreal cursorCenter = geometry.center().y()
            - layout->property("contentY").toReal();
        QVERIFY(qAbs(cursorCenter - viewportCenter) < 0.51);
        QCOMPARE(session->panelViewportCursorEntryId(),
                 QStringLiteral("shared-70"));

        runtime->shutdown();
    }

    void restoresWheelZoomAnimatedScrollAndPinchResize() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        QVariantList catalog;
        for (int index = 0; index < 28; ++index) {
            const QString path = directory.filePath(
                QStringLiteral("image-%1.png").arg(index));
            QImage image(QSize(160 + index, 100 + index),
                         QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor::fromHsv((index * 29) % 360, 180, 220));
            QVERIFY(image.save(path));
            catalog.append(imageEntry(
                QStringLiteral("image-%1").arg(index), index, path));
        }

        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine());
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("qml-panel-gestures"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QVERIFY(session->applyExternalState(
            QStringLiteral("image-0"), 0, {}, 1));
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("testSession"), session);

        QObject *panel = createPanel(view);
        QVERIFY(panel);
        view.show();

        auto *layout = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *scrollAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        auto *pinchArea = panel->findChild<QObject *>(
            QStringLiteral("galleryPinchArea"));
        QVERIFY(layout);
        QVERIFY(scrollAnimation);
        QVERIFY(pinchArea);
        QSignalSpy layoutReset(layout, SIGNAL(layoutReset()));
        QVERIFY(layoutReset.isValid());

        QTRY_VERIFY_WITH_TIMEOUT(
            layout->property("contentHeight").toReal() > layout->height(),
            5000);

        // The old MasonryMode accumulates wheel input against the animation's
        // destination and animates to it instead of jumping contentY directly.
        const qreal beforeScroll = layout->property("contentY").toReal();
        QVERIFY(invokeWheel(panel, -120, -120, int(Qt::NoModifier), 0, 0));
        QCOMPARE(layout->property("contentY").toReal(), beforeScroll);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->property("contentY").toReal() > beforeScroll, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !scrollAnimation->property("running").toBool(), 1000);
        // Metadata/layout completion may land in the same event-loop turn as
        // the animation's final frame. The panel immediately reconciles that
        // final clamped offset, but the two notify signals are not ordered.
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(session->panelScrollOffset()
                 - layout->property("contentY").toReal()) < 0.5, 1000);

        // Ctrl-wheel keeps its original role: change masonry density rather
        // than scrolling the viewport, with MasonryLayout selecting the next
        // useful column count and scheduling new thumbnail decodes.
        const int beforeWheelZoom = layout->property("targetHeight").toInt();
        const int resetsBeforeWheelZoom = layoutReset.size();
        QVERIFY(invokeWheel(panel, 120, 120, int(Qt::ControlModifier), 0, 0));
        QVERIFY(layout->property("targetHeight").toInt() > beforeWheelZoom);
        QCOMPARE(panel->property("thumbnailHeight").toInt(),
                 layout->property("targetHeight").toInt());
        QVERIFY(layoutReset.size() > resetsBeforeWheelZoom);

        // Pinch follows the original continuous scale with exact 30..500
        // limits, and only asks for new thumbnail decodes when it finishes.
        QVERIFY(invoke(panel, "beginThumbnailPinch"));
        const int pinchStart = layout->property("targetHeight").toInt();
        QVERIFY(invoke(panel, "updateThumbnailPinch", 0.01));
        QCOMPARE(layout->property("targetHeight").toInt(), 30);
        QCOMPARE(panel->property("thumbnailHeight").toInt(), 30);
        const int resetsBeforePinchFinish = layoutReset.size();
        QVERIFY(invoke(panel, "finishThumbnailPinch"));
        QVERIFY(pinchStart != layout->property("targetHeight").toInt());
        QVERIFY(layoutReset.size() > resetsBeforePinchFinish);
        QCOMPARE(panel->property("thumbnailPinchStartHeight").toInt(), 0);

        QVERIFY(invoke(panel, "beginThumbnailPinch"));
        QVERIFY(invoke(panel, "updateThumbnailPinch", 100.0));
        QCOMPARE(layout->property("targetHeight").toInt(), 500);
        QVERIFY(invoke(panel, "finishThumbnailPinch"));

        runtime->shutdown();
    }
};

QTEST_MAIN(GalleryPanelGestureTest)
#include "GalleryPanelGestureTest.moc"
