#include <QtTest>

#include "GalleryLayoutEngine.h"
#include "GalleryGeometryIndex.h"
#include "GalleryDelegatePool.h"
#include "GalleryViewportMaterializer.h"

using namespace ZoinGallery;

class GalleryLayoutEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void columnMajorUsesOnePhysicalPixelStride_data();
    void columnMajorUsesOnePhysicalPixelStride();
    void detailsPlanIncludesBottomAlignmentSlack();
    void analyticalRangeIsBoundedByViewport();
    void iconsGrowOnlyRowsThatNeedWrappedLabels();
    void masonryJustifiesCompletedRows();
    void densityPolicyIsPresentationSpecific();
    void geometryIndexBuildsOneSortedActiveBandSet();
    void delegatePoolRetainsOnlyClaimedViewportSlots();
    void viewportMaterializationUsesBoundedOverscan();
};

void GalleryLayoutEngineTest::columnMajorUsesOnePhysicalPixelStride_data() {
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::newRow("dpr-100") << qreal(1.0);
    QTest::newRow("dpr-125") << qreal(1.25);
    QTest::newRow("dpr-150") << qreal(1.5);
    QTest::newRow("dpr-200") << qreal(2.0);
}

void GalleryLayoutEngineTest::columnMajorUsesOnePhysicalPixelStride() {
    QFETCH(qreal, devicePixelRatio);
    GalleryLayoutRequest request;
    request.mode = GalleryPresentationMode::Columns;
    request.viewportSize = QSizeF(1001, 500);
    request.density = 25;
    request.columnCount = 3;
    request.devicePixelRatio = devicePixelRatio;

    const GalleryFixedLayoutPlan plan =
        GalleryLayoutEngine::fixedPlan(request, 100);
    QCOMPARE(plan.rowsPerColumn, 20);
    QCOMPARE(plan.columns, 3);
    const qreal expectedPhysicalStride = std::floor(
        std::floor(1001 * request.devicePixelRatio) / 3);
    QVERIFY(qAbs(plan.cellWidth * request.devicePixelRatio
                 - expectedPhysicalStride) < 0.001);
    QVERIFY(qAbs(plan.geometryFor(20).x() - plan.cellWidth) < 0.001);
    QVERIFY(qAbs(plan.geometryFor(40).x() - plan.cellWidth * 2) < 0.001);
    QVERIFY(qAbs(plan.geometryFor(40).width()
                 - plan.geometryFor(0).width()) < 0.001);
}

void GalleryLayoutEngineTest::detailsPlanIncludesBottomAlignmentSlack() {
    GalleryLayoutRequest request;
    request.mode = GalleryPresentationMode::Details;
    request.viewportSize = QSizeF(640, 479);
    request.insets = {.top = 7, .bottom = 13};
    request.density = 23.5;

    const GalleryFixedLayoutPlan plan =
        GalleryLayoutEngine::fixedPlan(request, 101);
    QCOMPARE(plan.geometryFor(100).top(), 7 + 100 * 23.5);
    const qreal usableHeight = 479 - 7 - 13;
    const qreal remainder = usableHeight
        - std::floor(usableHeight / 23.5) * 23.5;
    QCOMPARE(plan.contentExtent, 7 + 101 * 23.5 + remainder + 13);
}

void GalleryLayoutEngineTest::analyticalRangeIsBoundedByViewport() {
    GalleryLayoutRequest request;
    request.mode = GalleryPresentationMode::Details;
    request.viewportSize = QSizeF(800, 900);
    request.density = 25;

    const GalleryFixedLayoutPlan plan =
        GalleryLayoutEngine::fixedPlan(request, 30000);
    const QVector<int> indexes = plan.indexesIntersecting(10000, 10900);
    QVERIFY(indexes.size() <= 38);
    QCOMPARE(indexes.first(), 400);
    QCOMPARE(indexes.last(), 436);
}

void GalleryLayoutEngineTest::iconsGrowOnlyRowsThatNeedWrappedLabels() {
    GalleryLayoutRequest request;
    request.mode = GalleryPresentationMode::Icons;
    request.viewportSize = QSizeF(400, 400);
    request.density = 100;

    QVector<GalleryLayoutEntry> entries(8);
    for (GalleryLayoutEntry &entry : entries) {
        entry.originalSize = QSizeF(100, 100);
        entry.labelHeight = 16;
    }
    entries[1].labelHeight = 64;

    const GalleryLayoutResult result = GalleryLayoutEngine::layout(
        request, entries, 16);
    QCOMPARE(result.cells.size(), 8);
    QVERIFY(result.cells.at(0).geometry.height()
            > result.cells.at(4).geometry.height());
    QCOMPARE(result.cells.at(0).geometry.height(),
             result.cells.at(1).geometry.height());
    QCOMPARE(result.cells.at(4).geometry.top(),
             result.cells.at(0).geometry.bottom());
}

void GalleryLayoutEngineTest::masonryJustifiesCompletedRows() {
    GalleryLayoutRequest request;
    request.mode = GalleryPresentationMode::Masonry;
    request.viewportSize = QSizeF(600, 400);
    request.density = 100;
    request.spacing = 4;

    QVector<GalleryLayoutEntry> entries(10);
    for (GalleryLayoutEntry &entry : entries) {
        entry.originalSize = QSizeF(160, 100);
    }

    const GalleryLayoutResult result = GalleryLayoutEngine::layout(
        request, entries);
    QVERIFY(result.bands.size() >= 2);
    const GalleryLayoutBand &first = result.bands.first();
    const QRectF &last = result.cells.at(first.indexes.last()).geometry;
    QString geometryText;
    for (const int index : first.indexes) {
        const QRectF geometry = result.cells.at(index).geometry;
        geometryText += QStringLiteral("%1:[%2,%3] ")
            .arg(index).arg(geometry.x(), 0, 'f', 6)
            .arg(geometry.width(), 0, 'f', 6);
    }
    QVERIFY2(qAbs(last.right() - 600) < 0.001,
             qPrintable(QStringLiteral("row right=%1 %2")
                            .arg(last.right(), 0, 'f', 6)
                            .arg(geometryText)));
}

void GalleryLayoutEngineTest::densityPolicyIsPresentationSpecific() {
    QCOMPARE(GalleryDensityPolicy::normalized(
                 GalleryPresentationMode::Masonry, 1), 30.0);
    QCOMPARE(GalleryDensityPolicy::normalized(
                 GalleryPresentationMode::Details, 200), 72.0);
    QCOMPARE(GalleryDensityPolicy::normalized(
                 GalleryPresentationMode::Grid, 1), 96.0);
    QCOMPARE(GalleryDensityPolicy::normalized(
                 GalleryPresentationMode::Icons, 500), 256.0);
}

void GalleryLayoutEngineTest::geometryIndexBuildsOneSortedActiveBandSet() {
    GalleryGeometryIndex index;
    index.rebuild({
        {.index = 2, .row = 1, .geometry = QRectF(0, 40, 30, 20)},
        {.index = 0, .row = 0, .geometry = QRectF(0, 10, 40, 20)},
        {.index = 1, .row = 0, .geometry = QRectF(40, 10, 40, 20)},
        {.index = 3, .row = 2, .geometry = QRectF(0, 70, 80, 20)},
    });

    QCOMPARE(index.size(), 3);
    QCOMPARE(index.bands().at(0).indexes, QVector<int>({0, 1}));
    QCOMPARE(index.firstBandIntersecting(31), 1);
    QCOMPARE(index.indexesIntersecting(29, 71),
             QVector<int>({0, 1, 2, 3}));
    index.clear();
    QVERIFY(index.empty());
}

void GalleryLayoutEngineTest::delegatePoolRetainsOnlyClaimedViewportSlots() {
    GalleryDelegatePool pool;
    int allocations = 0;
    const auto factory = [&allocations]() {
        ++allocations;
        return new GalleryDelegateItem;
    };
    GalleryDelegateItem *first = pool.acquire(1, factory);
    GalleryDelegateItem *second = pool.acquire(1, factory);
    QCOMPARE(allocations, 2);
    QCOMPARE(pool.liveCount(), 2);

    pool.release(first);
    QCOMPARE(pool.transientFreeCount(), 1);
    QCOMPARE(pool.acquire(1, factory), first);
    QCOMPARE(allocations, 2);

    QPointer<GalleryDelegateItem> retired(second);
    pool.release(second);
    pool.trimSurplus();
    QVERIFY(retired.isNull());
    QCOMPARE(pool.liveCount(), 1);

    pool.release(first);
    pool.trimSurplus();
    QCOMPARE(pool.liveCount(), 0);
}

void GalleryLayoutEngineTest::viewportMaterializationUsesBoundedOverscan() {
    const GalleryViewportWindow details = GalleryViewportMaterializer::plan(
        GalleryPresentationMode::Details, 1000, 900, 22);
    QCOMPARE(details.visibleStart, 1000.0);
    QCOMPARE(details.visibleEnd, 1900.0);
    QCOMPARE(details.delegateStart, 1000.0);
    QCOMPARE(details.delegateEnd, 1900.0);
    QCOMPARE(details.metadataStart, 912.0);
    QCOMPARE(details.metadataEnd, 1988.0);

    const GalleryViewportWindow grid = GalleryViewportMaterializer::plan(
        GalleryPresentationMode::Grid, 1000, 900, 140);
    QCOMPARE(grid.delegateStart, 1000.0);
    QCOMPARE(grid.delegateEnd, 1900.0);
    QCOMPARE(grid.metadataStart, 775.0);
    QCOMPARE(grid.metadataEnd, 2125.0);

    const GalleryViewportWindow columns = GalleryViewportMaterializer::plan(
        GalleryPresentationMode::Columns, 500, 1200, 400);
    QCOMPARE(columns.delegateStart, 500.0);
    QCOMPARE(columns.delegateEnd, 1700.0);
    QCOMPARE(columns.metadataStart, 100.0);
    QCOMPARE(columns.metadataEnd, 2100.0);

    const GalleryViewportWindow masonry = GalleryViewportMaterializer::plan(
        GalleryPresentationMode::Masonry, 1000, 900, 140);
    QCOMPARE(masonry.delegateStart, 952.0);
    QCOMPARE(masonry.delegateEnd, 1948.0);
}

QTEST_MAIN(GalleryLayoutEngineTest)
#include "GalleryLayoutEngineTest.moc"
