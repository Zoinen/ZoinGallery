#include "GalleryComponentHost.h"
#include "GallerySceneItem.h"
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QtQuick/private/qquickitem_p.h>
#include <QtTest>
#include <atomic>

#if defined(QT_STATIC)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QtQuick2Plugin)
Q_IMPORT_PLUGIN(QtQuick_WindowPlugin)
Q_IMPORT_PLUGIN(QtQmlPlugin)
Q_IMPORT_PLUGIN(QtQmlModelsPlugin)
Q_IMPORT_PLUGIN(QtQmlWorkerScriptPlugin)
static void initializeStagingQml() {
    Q_INIT_RESOURCE(qmake_QML);
    Q_INIT_RESOURCE(qmake_QtQml);
    Q_INIT_RESOURCE(qmake_QtQml_Models);
    Q_INIT_RESOURCE(qmake_QtQml_WorkerScript);
    Q_INIT_RESOURCE(qmake_QtQuick);
    Q_INIT_RESOURCE(qmake_QtQuick_Window);
}
Q_CONSTRUCTOR_FUNCTION(initializeStagingQml)
#endif

class StagingHost final : public GalleryComponentHost {
public:
    using GalleryComponentHost::componentComplete;
};

class GalleryComponentHostStagingTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qmlRegisterType<GallerySceneItem>("Staging.Native", 1, 0, "NativeScene");
    }

    void publicationKeepsReadySurface_data() {
        QTest::addColumn<bool>("nativeRoot");
        QTest::newRow("native") << true;
        QTest::newRow("generic") << false;
    }

    void publicationKeepsReadySurface() {
        QFETCH(bool, nativeRoot);
        QQmlIncubationController incubation;
        QQmlEngine engine;
        engine.setIncubationController(&incubation);
        QQuickWindow window;
        window.resize(112, 72);
        window.setColor(Qt::black);
        StagingHost host;
        host.setObjectName("staging-host");
        host.setPosition({8, 8});
        host.setSize({80, 40});
        host.setParentItem(window.contentItem());
        host.componentComplete();
        QQmlComponent first(&engine), second(&engine);
        const QByteArray type = nativeRoot ? "NativeScene" : "Item";
        first.setData("import QtQuick; import Staging.Native 1.0; " + type
            + " { Rectangle { anchors.fill: parent; color: 'red' } }",
            QUrl("qrc:/staging-fixture/First.qml"));
        second.setData("import QtQuick; import Staging.Native 1.0; " + type + R"QML( {
            property string hostName: parent ? parent.objectName : ""
            Rectangle { objectName: "staging-color-leaf"; anchors.fill: parent; color: "lime" }
            Repeater { model: 4096; Item {} }
        })QML", QUrl("qrc:/staging-fixture/Second.qml"));
        QTRY_VERIFY2(first.isReady(), qPrintable(first.errorString()));
        QTRY_VERIFY2(second.isReady(), qPrintable(second.errorString()));
        host.setAsynchronous(false);
        host.setSourceComponent(&first);
        QVERIFY(host.item());
        QPointer<QQuickItem> original = host.item();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto dpr = window.devicePixelRatio();
        QCOMPARE(dpr, 1.75);
        const auto initial = window.grabWindow();
        QCOMPARE(initial.pixelColor(qRound(48*dpr), qRound(28*dpr)), QColor(Qt::red));

        host.setAsynchronous(true);
        host.setSourceComponent(&second);
        QVERIFY(incubation.incubatingObjectCount() > 0);
        std::atomic_bool keep = true;
        const auto attached = connect(&host, &QQuickItem::childrenChanged, &engine, [&] {
            if (host.childItems().size() > 1) keep = false;
        });
        incubation.incubateWhile(&keep, 100);
        disconnect(attached);
        QVERIFY(!keep.load());
        QVERIFY(incubation.incubatingObjectCount() > 0);
        QCOMPARE(host.item(), original.data());
        QQuickItem *pending = nullptr;
        for (auto *item : host.childItems())
            if (item != original) pending = item;
        QVERIFY(pending);
        QCOMPARE(pending->parentItem(), &host);
        host.setWidth(88);
        QCOMPARE(pending->width(), 88.0);
        QCOMPARE(original->width(), 88.0);
        host.setWidth(80);

        std::atomic_bool pendingHasOpacity = false;
        std::atomic_bool hiddenReadyHasOpacity = false;
        const auto sync = connect(&window, &QQuickWindow::afterSynchronizing, &window, [&] {
            pendingHasOpacity = QQuickItemPrivate::get(pending)->opacityNode() != nullptr;
            if (original) {
                auto *opacity = QQuickItemPrivate::get(original)->opacityNode();
                if (opacity && opacity->opacity() == 0) hiddenReadyHasOpacity = true;
            }
        }, Qt::DirectConnection);
        const auto cleanup = qScopeGuard([&] { disconnect(sync); });
        const auto during = window.grabWindow();
        QCOMPARE(during.pixelColor(qRound(48*dpr), qRound(28*dpr)), QColor(Qt::red));
        if (nativeRoot && QSGRendererInterface::isApiRhiBased(window.rendererInterface()->graphicsApi()))
            QVERIFY2(!pendingHasOpacity.load(), "Incubation must not manufacture an opacity-zero node");

        for (int i = 0; i < 1000 && incubation.incubatingObjectCount(); ++i)
            incubation.incubateFor(1);
        QCOMPARE(incubation.incubatingObjectCount(), 0);
        QCOMPARE(host.item(), pending);
        QCOMPARE(pending->property("hostName").toString(), QString("staging-host"));
        const auto after = window.grabWindow();
        if (nativeRoot && QSGRendererInterface::isApiRhiBased(window.rendererInterface()->graphicsApi()))
            QVERIFY2(!hiddenReadyHasOpacity.load(), "Retiring a ready surface must not manufacture an opacity-zero node");
        QCOMPARE(after.pixelColor(qRound(48*dpr), qRound(28*dpr)), QColor(Qt::green));
        auto *leaf = pending->findChild<QQuickItem *>("staging-color-leaf");
        QVERIFY(leaf);
        const auto origin = leaf->mapToItem(window.contentItem(), QPointF());
        QCOMPARE(origin*dpr, QPointF(14, 14));
        QCOMPARE(leaf->mapToItem(window.contentItem(), {1, 0})-origin, QPointF(1, 0));
        QCOMPARE(leaf->mapToItem(window.contentItem(), {0, 1})-origin, QPointF(0, 1));
        const auto tag = QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(initial.save("staging-initial-" + tag + ".png"));
        QVERIFY(during.save("staging-pending-" + tag + ".png"));
        QVERIFY(after.save("staging-ready-" + tag + ".png"));
    }

    void cancellationAndReentrantClosing() {
        QQmlIncubationController incubation;
        QQmlEngine engine;
        engine.setIncubationController(&incubation);
        QQmlComponent first(&engine), slow(&engine), unsupported(&engine);
        first.setData("import QtQuick; import Staging.Native 1.0; NativeScene { opacity: 0.35; visible: false; enabled: false }",
            QUrl("qrc:/staging-fixture/Authored.qml"));
        slow.setData("import QtQuick; import Staging.Native 1.0; NativeScene { Repeater { model: 4096; Item {} } }",
            QUrl("qrc:/staging-fixture/Slow.qml"));
        unsupported.setData("import QtQml; QtObject { objectName: 'unsupported-staging-root' }",
            QUrl("qrc:/staging-fixture/Unsupported.qml"));
        QTRY_VERIFY(first.isReady() && slow.isReady() && unsupported.isReady());
        StagingHost host;
        host.componentComplete();
        host.setAsynchronous(false);
        host.setSourceComponent(&first);
        QVERIFY(host.item());
        QCOMPARE(host.item()->opacity(), .35);
        QVERIFY(!host.item()->isVisible());
        QVERIFY(!host.item()->isEnabled());
        host.setAsynchronous(true);
        host.setSourceComponent(&slow);
        QVERIFY(incubation.incubatingObjectCount() > 0);
        host.setActive(false);
        QVERIFY(!host.item());
        QCOMPARE(incubation.incubatingObjectCount(), 0);
        host.setActive(true);
        QVERIFY(incubation.incubatingObjectCount() > 0);
        host.setSourceComponent(&first);
        for (int i = 0; i < 1000 && incubation.incubatingObjectCount(); ++i)
            incubation.incubateFor(1);
        QVERIFY(host.item());
        QCOMPARE(host.item()->opacity(), .35);
        QVERIFY(!host.item()->isVisible());
        host.setAsynchronous(false);
        host.setSourceComponent(&unsupported);
        QVERIFY(!host.item());
        QCOMPARE(host.status(), int(QQmlIncubator::Error));
        QVERIFY(!host.findChild<QObject *>("unsupported-staging-root"));
        QPointer<StagingHost> closing = new StagingHost;
        closing->componentComplete();
        closing->setAsynchronous(false);
        connect(closing, &GalleryComponentHost::loaded, &engine, [&] { delete closing; });
        closing->setSourceComponent(&first);
        QVERIFY(!closing);
        QCOMPARE(incubation.incubatingObjectCount(), 0);
    }
};

QTEST_MAIN(GalleryComponentHostStagingTest)
#include "GalleryComponentHostStagingTest.moc"
