#include "GallerySceneItem.h"
#include "GalleryViewportMotion.h"
#include "GalleryItemMappingScope.h"
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#include <QScopeGuard>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#define private public
#include <QtQuick/private/qsgbatchrenderer_p.h>
#undef private
#include <QtTest>
#include <atomic>
#include <memory>
#include <vector>
class MappingMatrixFixture final : public QQuickTransform {
public:
    QMatrix4x4 value;
    void applyTo(QMatrix4x4 *matrix) const override { *matrix *= value; }
};
class RetainedMotionLeaf final : public QQuickItem {
public:
    explicit RetainedMotionLeaf(QQuickItem *parent):QQuickItem(parent){setFlag(ItemHasContents);}
protected:
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *node=static_cast<QSGSimpleRectNode *>(old);
        if(!node)node=new QSGSimpleRectNode;
        node->setRect(boundingRect());node->setColor(Qt::red);return node;
    }
};
class GallerySceneItemMotionTest final : public QObject {
    Q_OBJECT
private slots:
    void siblingMappingReusesAncestors() {
        QQuickWindow window;
        QQuickItem target(window.contentItem()), branch(&target);
        target.setPosition({8 / 1.75, 12 / 1.75});
        branch.setPosition({3 / 1.75, 7 / 1.75});
        std::vector<std::unique_ptr<QQuickItem>> items;
        for (int i = 0; i < 150; ++i) {
            auto item = std::make_unique<QQuickItem>(&branch);
            item->setPosition({i / 1.75, i * 3 / 1.75});
            item->setSize({37 / 1.75, 23 / 1.75});
            items.push_back(std::move(item));
        }
        GalleryItemMappingScope scope(&target);
        for (auto &item : items)
            QCOMPARE(scope.map(item.get(), item->boundingRect()),
                     item->mapRectToItem(&target, item->boundingRect()));
        QCOMPARE(scope.fullMaps(), quint64(1));
        QCOMPARE(scope.fastMaps(), quint64(150));
    }

    void transformedMappingUsesQtGeometry() {
        QQuickWindow window;
        QQuickItem target(window.contentItem()), parent(&target), leaf(&parent);
        parent.setPosition({-17 / 1.75, 21 / 1.75});
        parent.setRotation(23);
        parent.setScale(1.3);
        leaf.setPosition({5 / 1.75, 9 / 1.75});
        leaf.setSize({43 / 1.75, 37 / 1.75});
        const auto check = [&] {
            GalleryItemMappingScope scope(&target);
            const auto actual = scope.map(&leaf, leaf.boundingRect());
            const auto expected = leaf.mapRectToItem(&target, leaf.boundingRect());
            QVERIFY(qAbs(actual.x() - expected.x()) < 1e-8);
            QVERIFY(qAbs(actual.y() - expected.y()) < 1e-8);
            QVERIFY(qAbs(actual.width() - expected.width()) < 1e-8);
            QVERIFY(qAbs(actual.height() - expected.height()) < 1e-8);
        };
        check();
        leaf.setRotation(15); check();
        leaf.setRotation(0); leaf.setScale(.7); check();
        leaf.setScale(1);
        MappingMatrixFixture ancestor;
        ancestor.value.rotate(31, 1, 0, 0);
        ancestor.value.translate(0, 0, 9);
        auto list = parent.transform(); list.append(&list, &ancestor); check();
        MappingMatrixFixture child;
        child.value.rotate(17, 0, 1, 0);
        auto childList = leaf.transform(); childList.append(&childList, &child); check();
    }

    void mappingRefreshesBetweenPasses() {
        QQuickWindow window;
        QQuickItem target(window.contentItem()), a(&target), b(&target), leaf(&a);
        leaf.setSize({31, 17});
        leaf.setPosition({2 / 1.75, 4 / 1.75});
        for (int i = 0; i < 6; ++i) {
            a.setX(i * 7 / 1.75); b.setScale(1 + i * .1);
            leaf.setParentItem(i % 2 ? &a : &b);
            GalleryItemMappingScope scope(&target);
            QCOMPARE(scope.map(&leaf, leaf.boundingRect()),
                     leaf.mapRectToItem(&target, leaf.boundingRect()));
        }
        leaf.setParentItem(&target);
        GalleryItemMappingScope direct(&target);
        QCOMPARE(direct.map(&leaf, leaf.boundingRect()),
                 leaf.mapRectToItem(&target, leaf.boundingRect()));
        QCOMPARE(direct.fullMaps(), quint64(0));
    }

    void mappingStorageIsBounded() {
        QQuickWindow window;
        QQuickItem target(window.contentItem());
        std::vector<std::unique_ptr<QQuickItem>> parents, leaves;
        for (int i = 0; i < 40; ++i) {
            parents.push_back(std::make_unique<QQuickItem>(&target));
            parents.back()->setX(i / 1.75);
            leaves.push_back(std::make_unique<QQuickItem>(parents.back().get()));
            leaves.back()->setSize({10, 17});
        }
        GalleryItemMappingScope scope(&target);
        for (auto &item : leaves)
            QCOMPARE(scope.map(item.get(), item->boundingRect()),
                     item->mapRectToItem(&target, item->boundingRect()));
        QCOMPARE(scope.fastMaps(), quint64(32));
        QCOMPARE(scope.fullMaps(), quint64(40));
    }

    void pendingPublicationOverridesCameraRetention() {
        QQuickWindow window;
        window.resize(112, 72);
        window.setColor(Qt::black);
        if (!QSGRendererInterface::isApiRhiBased(window.rendererInterface()->graphicsApi()))
            QSKIP("Requires a native RHI renderer");
        auto *root = new GallerySceneItem(window.contentItem());
        root->setPosition({8, 8});
        root->setSize({80, 40});
        root->setRetainSceneWhenHidden(true);
        root->setIncubationReady(false);
        auto *leaf = new RetainedMotionLeaf(root);
        leaf->setObjectName("pendingPublicationColorLeaf");
        leaf->setSize({80, 40});
        auto camera = std::make_shared<GalleryViewportMotionState>();
        camera->active = true;
        camera->renderViewport = {0, 0, 112, 72};
        std::atomic_bool blocked = false;
        const auto rendering = connect(&window, &QQuickWindow::beforeRendering, &window, [&] {
            auto *node = QQuickItemPrivate::get(root)->itemNodeInstance;
            if (!node) return;
            GallerySceneItem::setNodeViewportMotion(node, camera, {8, 8, 80, 40}, true);
            blocked = node->isSubtreeBlocked();
        }, Qt::DirectConnection);
        const auto cleanup = qScopeGuard([&] { disconnect(rendering); });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.devicePixelRatio(), 1.75);
        const auto hidden = window.grabWindow();
        QVERIFY(blocked.load());
        QCOMPARE(hidden.pixelColor(84, 49), QColor(Qt::black));
        root->setIncubationReady(true);
        const auto published = window.grabWindow();
        QVERIFY(!blocked.load());
        QCOMPARE(published.pixelColor(84, 49), QColor(Qt::red));
        root->setIncubationReady(false);
        const auto retired = window.grabWindow();
        QVERIFY(blocked.load());
        QCOMPARE(retired.pixelColor(84, 49), QColor(Qt::black));
        const auto origin = leaf->mapToItem(window.contentItem(), QPointF());
        QCOMPARE(origin*window.devicePixelRatio(), QPointF(14, 14));
        QCOMPARE(leaf->mapToItem(window.contentItem(), {1, 0})-origin, QPointF(1, 0));
        QCOMPARE(leaf->mapToItem(window.contentItem(), {0, 1})-origin, QPointF(0, 1));
        QVERIFY(published.save("scene-motion-publication-175.png"));
    }

    void cameraHonorsRetainedScene_data(){
        QTest::addColumn<bool>("retain");QTest::addColumn<bool>("prepared");
        QTest::newRow("ordinary")<<false<<false;
        QTest::newRow("retained")<<true<<false;
        QTest::newRow("prepared-before-gui-retention")<<false<<true;
        QTest::newRow("prepared-and-retained")<<true<<true;
    }
    void cameraHonorsRetainedScene(){
        QFETCH(bool,retain);QFETCH(bool,prepared);
        QQuickWindow window;window.resize(320,120);window.setColor(Qt::black);
        if(!QSGRendererInterface::isApiRhiBased(window.rendererInterface()->graphicsApi()))
            QSKIP("Requires a native RHI renderer");
        auto *viewport=new QQuickItem(window.contentItem());
        viewport->setObjectName("retainedMotionViewportClip");viewport->setSize({160,120});viewport->setClip(true);
        auto *root=new GallerySceneItem(viewport);
        root->setPosition({240,8});root->setSize({24,48});
        root->setSceneVisible(prepared);root->setRetainSceneWhenHidden(retain);
        auto *leaf=new RetainedMotionLeaf(root);leaf->setObjectName("retainedMotionVisualLeaf");
        leaf->setPosition({4,4});leaf->setSize({16,40});
        auto camera=std::make_shared<GalleryViewportMotionState>();camera->active=true;
        camera->renderViewport={0,0,160,120};camera->renderVisibleViewport=camera->renderViewport;
        std::atomic_bool blocked=true,resident=false;std::atomic_int mode=0;
        const auto before=connect(&window,&QQuickWindow::beforeRendering,&window,[&]{
            auto *node=QQuickItemPrivate::get(root)->itemNodeInstance;
            if(!node)return;
            if(mode.load()==1)GallerySceneItem::clearNodeViewportMotion(node,camera);
            else GallerySceneItem::setNodeViewportMotion(node,camera,
                mode.load()==2?QRectF(8,8,24,48):QRectF(240,8,24,48),prepared);
            blocked=node->isSubtreeBlocked();
        },Qt::DirectConnection);
        const auto after=connect(&window,&QQuickWindow::afterRendering,&window,[&]{
            auto *renderer=static_cast<QSGBatchRenderer::Renderer *>(QQuickWindowPrivate::get(&window)->renderer);
            auto *node=QQuickItemPrivate::get(leaf)->paintNode;
            auto *shadow=renderer && node?renderer->m_nodes.value(node):nullptr;
            const auto *element=shadow?shadow->element():nullptr;
            const auto *batch=element?element->batch:nullptr;
            resident=batch && batch->vbo.buf && (!batch->ibo.size || batch->ibo.buf);
        },Qt::DirectConnection);
        const auto cleanup=qScopeGuard([&]{disconnect(before);disconnect(after);});
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto hidden=window.grabWindow();QVERIFY(!hidden.isNull());QCOMPARE(window.devicePixelRatio(),1.75);
        QCOMPARE(blocked.load(),!(retain||prepared));
        QCOMPARE(resident.load(),retain||prepared);
        QCOMPARE(hidden.pixelColor(35,35),QColor(Qt::black));
        QCOMPARE(hidden.pixelColor(qRound(252*window.devicePixelRatio()),35),QColor(Qt::black));
        // Once GUI visibility catches up and the owning camera is cleared,
        // prepared-only retention must no longer keep the hidden scene alive.
        mode=1;root->setSceneVisible(false);window.grabWindow();
        QCOMPARE(blocked.load(),!retain);QCOMPARE(resident.load(),retain);
        mode=2;root->setPosition({8,8});root->setSceneVisible(true);
        const auto visible=window.grabWindow();QVERIFY(!blocked.load());QVERIFY(resident.load());
        const auto origin=leaf->mapToItem(window.contentItem(),QPointF());
        const auto dpr=window.devicePixelRatio();
        QVERIFY(qAbs(origin.x()*dpr-qRound(origin.x()*dpr))<.001);
        QVERIFY(qAbs(origin.y()*dpr-qRound(origin.y()*dpr))<.001);
        QCOMPARE(leaf->mapToItem(window.contentItem(),{1,0})-origin,QPointF(1,0));
        QCOMPARE(leaf->mapToItem(window.contentItem(),{0,1})-origin,QPointF(0,1));
        QCOMPARE(visible.pixelColor(qRound((origin.x()+8)*dpr),qRound((origin.y()+8)*dpr)),QColor(Qt::red));
        QVERIFY(visible.save(QStringLiteral("scene-motion-%1-%2-175.png").arg(retain).arg(prepared)));
    }
};
QTEST_MAIN(GallerySceneItemMotionTest)
#include "GallerySceneItemMotionTest.moc"
