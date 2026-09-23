#include "GalleryGroupIndex.h"

#include <QTest>

using ZoinGallery::GalleryGroupDescriptor;
using ZoinGallery::GalleryGroupIndex;

class GalleryGroupIndexTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsSourceIndexesAndCollapsedPrefixes();
    void skipsCollapsedSourceRanges();
    void rejectsMalformedRanges();
    void materializesOnlyIntersectingHeaders();
};

void GalleryGroupIndexTest::mapsSourceIndexesAndCollapsedPrefixes()
{
    GalleryGroupIndex index;
    QVERIFY(index.setDescriptors(QVector<GalleryGroupDescriptor>{
        {QStringLiteral("a"), QStringLiteral("A"), 1, 2},
        {QStringLiteral("b"), QStringLiteral("B"), 3, 3},
    }, 6));

    QCOMPARE(index.groupForSourceIndex(0), -1);
    QCOMPARE(index.groupForSourceIndex(1), 0);
    QCOMPARE(index.localIndexForSourceIndex(2), 1);
    QCOMPARE(index.visibleOrdinalForSourceIndex(0), 0);
    QCOMPARE(index.visibleOrdinalForSourceIndex(3), 3);
    QCOMPARE(index.sourceIndexForVisibleOrdinal(4), 4);
    QCOMPARE(index.visibleFileCountBeforeGroup(1), 3);
    QCOMPARE(index.nearestVisibleSourceIndex(1, true), 2);
    QCOMPARE(index.nearestVisibleSourceIndex(3, false), 2);
    QCOMPARE(index.nearestVisibleSourceIndex(-1, true), 0);
    QCOMPARE(index.nearestVisibleSourceIndex(6, false), 5);

    QVERIFY(index.setCollapsed(QStringLiteral("a"), true));
    QCOMPARE(index.visibleFileCount(), 4);
    QCOMPARE(index.visibleOrdinalForSourceIndex(1), -1);
    QCOMPARE(index.visibleOrdinalForSourceIndex(3), 1);
    QCOMPARE(index.sourceIndexForVisibleOrdinal(1), 3);
    QCOMPARE(index.nearestVisibleSourceIndex(1, true), 3);
    QCOMPARE(index.nearestVisibleSourceIndex(3, false), 0);
    QCOMPARE(index.nearestVisibleSourceIndex(-1, true), 0);
    QVERIFY(!index.setCollapsed(QStringLiteral("a"), true));
    QVERIFY(index.toggleCollapsed(QStringLiteral("a")));
    QCOMPARE(index.visibleFileCount(), 6);
}

void GalleryGroupIndexTest::skipsCollapsedSourceRanges()
{
    GalleryGroupIndex index;
    QVERIFY(index.setDescriptors(QVector<GalleryGroupDescriptor>{
        {QStringLiteral("a"), QStringLiteral("A"), 1, 2},
        {QStringLiteral("b"), QStringLiteral("B"), 3, 2},
        {QStringLiteral("c"), QStringLiteral("C"), 5, 1},
    }, 6));
    QVERIFY(index.setCollapsed(QStringLiteral("b"), true));
    QCOMPARE(index.visibleSourceIndexesInRange(0, 5),
             QVector<int>({0, 1, 2, 5}));
    QCOMPARE(index.visibleSourceIndexesInRange(3, 4), QVector<int>());
}

void GalleryGroupIndexTest::rejectsMalformedRanges()
{
    GalleryGroupIndex index;
    QVERIFY(!index.setDescriptors(QVector<GalleryGroupDescriptor>{
        {QStringLiteral("duplicate"), QStringLiteral("A"), 1, 2},
        {QStringLiteral("duplicate"), QStringLiteral("B"), 3, 1},
    }, 5));
    QVERIFY(!index.active());
    QVERIFY(!index.setDescriptors(QVector<GalleryGroupDescriptor>{
        {QStringLiteral("overlap"), QStringLiteral("A"), 1, 3},
        {QStringLiteral("next"), QStringLiteral("B"), 2, 1},
    }, 5));
    QVERIFY(!index.active());
}

void GalleryGroupIndexTest::materializesOnlyIntersectingHeaders()
{
    GalleryGroupIndex index;
    QVERIFY(index.setDescriptors(QVector<GalleryGroupDescriptor>{
        {QStringLiteral("a"), QStringLiteral("A"), 0, 2},
        {QStringLiteral("b"), QStringLiteral("B"), 2, 2},
        {QStringLiteral("c"), QStringLiteral("C"), 4, 2},
    }, 6));
    index.setSectionGeometry({0, 100, 200}, {80, 80, 80}, 24);
    const QVariantList headers = index.headersForRange(90, 110, 0);
    QCOMPARE(headers.size(), 1);
    QCOMPARE(headers.first().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("b"));
}

QTEST_MAIN(GalleryGroupIndexTest)
#include "GalleryGroupIndexTest.moc"
