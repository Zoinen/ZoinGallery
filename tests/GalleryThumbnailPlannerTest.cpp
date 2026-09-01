#include "GalleryThumbnailPlanner.h"

#include <QTest>

class GalleryThumbnailPlannerTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlyValidRowsAndPrioritizesVisibleRows() {
        ZoinGallery::GalleryThumbnailPlanner planner;
        const auto plan = planner.planWindow({
            .candidates = {-1, 2, 3, 4, 99},
            .visible = {3},
            .logicalCount = 8,
            .mode = ZoinGallery::GalleryPresentationMode::Details,
        });
        QVERIFY(plan.valid);
        QCOMPARE(plan.visible, QList<int>{3});
        QCOMPARE(plan.background, (QList<int>{2, 4}));
        QVERIFY(!plan.catalogWideMetadata);
    }

    void disjointStandaloneJumpRebasesBoundedQueue() {
        ZoinGallery::GalleryThumbnailPlanner planner;
        static_cast<void>(planner.planWindow({
            .candidates = {0, 1, 2},
            .visible = {0, 1},
            .logicalCount = 100,
        }));
        const auto jump = planner.planWindow({
            .candidates = {70, 71, 72},
            .visible = {70, 71},
            .logicalCount = 100,
        });
        QVERIFY(jump.cancelExisting);
        QVERIFY(jump.forceRequests);
        QCOMPARE(planner.scheduledIndexCount(), 3);
    }

    void embeddedSourceNeverCancelsSharedDecodeWork() {
        ZoinGallery::GalleryThumbnailPlanner planner;
        static_cast<void>(planner.planWindow({
            .candidates = {0, 1},
            .visible = {0},
            .logicalCount = 100,
            .embedded = true,
        }));
        const auto jump = planner.planWindow({
            .candidates = {90, 91},
            .visible = {90},
            .logicalCount = 100,
            .embedded = true,
        });
        QVERIFY(!jump.cancelExisting);
    }

    void requestKeyTiersAreBoundedIndependently() {
        ZoinGallery::GalleryThumbnailPlanner planner;
        static_cast<void>(planner.accountRequestKeys(
            {QStringLiteral("a"), QStringLiteral("b")},
            2, true, false, false));
        const auto plan = planner.accountRequestKeys(
            {QStringLiteral("c"), QStringLiteral("d"),
             QStringLiteral("e"), QStringLiteral("f"),
             QStringLiteral("g")},
            2, true, false, false);
        QVERIFY(plan.cancelExisting);
        QVERIFY(plan.forceRequests);
        QCOMPARE(planner.scheduledRequestKeyCount(), 5);
    }
};

QTEST_MAIN(GalleryThumbnailPlannerTest)
#include "GalleryThumbnailPlannerTest.moc"
