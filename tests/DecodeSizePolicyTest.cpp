#include "src/embed/DecodeSizePolicy.h"

#include <QtTest>

class DecodeSizePolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void localSourcesKeepExactTarget()
    {
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(513, 342), QSize(6000, 4000), {},
                     ZoinGallery::DecodeSizeFamily::ViewerFit, false),
                 QSize(513, 342));
    }

    void expensiveSourcesRoundUpByFamily()
    {
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(513, 342), QSize(6000, 4000), {},
                     ZoinGallery::DecodeSizeFamily::ViewerFit, true),
                 QSize(768, 512));
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(257, 171), QSize(6000, 4000), {},
                     ZoinGallery::DecodeSizeFamily::Thumbnail, true),
                 QSize(384, 256));
    }

    void hysteresisKeepsPreparedTierAcrossSmallGrowth()
    {
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(769, 513), QSize(6000, 4000), QSize(768, 512),
                     ZoinGallery::DecodeSizeFamily::ViewerFit, true),
                 QSize(768, 512));
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(850, 567), QSize(6000, 4000), QSize(768, 512),
                     ZoinGallery::DecodeSizeFamily::ViewerFit, true),
                 QSize(1024, 683));
    }

    void neverPreparesBeyondNativePixels()
    {
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(2000, 1200), QSize(640, 480), {},
                     ZoinGallery::DecodeSizeFamily::ViewerFit, true),
                 QSize(640, 480));
        QCOMPARE(ZoinGallery::stableDecodeTarget(
                     QSize(600, 450), QSize(640, 480), {},
                     ZoinGallery::DecodeSizeFamily::ViewerFit, true),
                 QSize(640, 480));
    }
};

QTEST_MAIN(DecodeSizePolicyTest)
#include "DecodeSizePolicyTest.moc"
