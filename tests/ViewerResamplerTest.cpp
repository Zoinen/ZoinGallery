#include "ViewerResampler.h"

#include <QColorSpace>
#include <QtTest>

#include <cmath>
#include <array>

namespace {

QImage stripes(QSize size) {
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            line[x] = (x % 2) ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
        }
    }
    return image;
}

// After reducing a source at Nyquist, the interior should approach its mean.
// Ignore edge extension, where the infinite periodic signal no longer applies.
double stripeAliasRms(const QImage &image, double mean = 127.5) {
    double energy = 0;
    int count = 0;
    for (int x = 16; x < image.width() - 16; ++x) {
        const double error = qRed(image.pixel(x, image.height() / 2)) - mean;
        energy += error * error;
        ++count;
    }
    return std::sqrt(energy / count);
}

}

class ViewerResamplerTest : public QObject {
    Q_OBJECT

private slots:
    void blackAndWhiteAreAveragedInLinearLight() {
        const QImage result = ZoinGallery::ViewerResampler::downsample(
            stripes({128, 16}), {64, 8});
        double sum = 0;
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 8; x < result.width() - 8; ++x) {
                const QRgb pixel = result.pixel(x, y);
                QVERIFY(qRed(pixel) >= 187 && qRed(pixel) <= 188);
                QCOMPARE(qRed(pixel), qGreen(pixel));
                QCOMPARE(qRed(pixel), qBlue(pixel));
                QCOMPARE(qAlpha(pixel), 255);
                sum += qRed(pixel);
            }
        }
        QVERIFY(std::abs(sum / (8 * 48) - 187.516) < 0.02);
    }

    void oddPyramidDimensionsKeepTheExactTwoToOnePhase() {
        QImage odd(131, 67, QImage::Format_RGB32);
        for (int y = 0; y < odd.height(); ++y) {
            for (int x = 0; x < odd.width(); ++x) {
                odd.setPixel(x, y, qRgb((x * 47 + y * 13) % 256,
                                       (x * 7 + y * 53) % 256,
                                       (x * 23 + y * 31) % 256));
            }
        }
        const QImage even = odd.copy(0, 0, 130, 66);
        const QImage actual = ZoinGallery::ViewerResampler::downsample(odd, {65, 33});
        const QImage expected = ZoinGallery::ViewerResampler::downsample(even, {65, 33});
        // The unpaired last row/column may affect the clamped boundary only;
        // they must not stretch the sample grid throughout the image.
        for (int y = 0; y < actual.height() - 3; ++y) {
            for (int x = 0; x < actual.width() - 3; ++x) {
                QCOMPARE(actual.pixel(x, y), expected.pixel(x, y));
            }
        }
    }

    void sixteenBitQuantizationDither() {
        constexpr std::array<int, 64> dither = {
             0, 48, 12, 60,  3, 51, 15, 63,
            32, 16, 44, 28, 35, 19, 47, 31,
             8, 56,  4, 52, 11, 59,  7, 55,
            40, 24, 36, 20, 43, 27, 39, 23,
             2, 50, 14, 62,  1, 49, 13, 61,
            34, 18, 46, 30, 33, 17, 45, 29,
            10, 58,  6, 54,  9, 57,  5, 53,
            42, 26, 38, 22, 41, 25, 37, 21
        };
        QImage image(32, 16, QImage::Format_RGBA64_Premultiplied);
        image.fill(QColor::fromRgba64(100 * 257 + 64, 200 * 257 + 192,
                                     50 * 257 + 128, 65535));
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {16, 8});
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 0; x < result.width(); ++x) {
                const double offset = (2 * dither[y * 8 + x % 8] - 63) / 128.0;
                const QRgb pixel = result.pixel(x, y);
                QCOMPARE(qRed(pixel), qRound(100 + 64 / 257.0 + offset));
                QCOMPARE(qGreen(pixel), qRound(200 + 192 / 257.0 + offset));
                QCOMPARE(qBlue(pixel), qRound(50 + 128 / 257.0 + offset));
                QCOMPARE(qAlpha(pixel), 255);
            }
        }
    }

    void alphaIsLinearAndRgbIsUnpremultipliedBeforeTransfer() {
        QImage image(128, 16, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixel(x, y, x % 2 ? qRgba(128, 128, 128, 128)
                                          : qRgba(0, 0, 0, 128));
            }
        }
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {64, 8});
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 8; x < result.width() - 8; ++x) {
                const QRgb pixel = result.pixel(x, y);
                QCOMPARE(qAlpha(pixel), 128);
                QVERIFY(qRed(pixel) >= 94 && qRed(pixel) <= 95);
                QCOMPARE(qRed(pixel), qGreen(pixel));
                QCOMPARE(qRed(pixel), qBlue(pixel));
            }
        }
    }

    void profilesArePreservedWithoutChangingSamplingMath() {
        QImage srgb = stripes({131, 67});
        srgb.setColorSpace(QColorSpace::SRgb);
        QImage p3 = srgb;
        p3.setColorSpace(QColorSpace::DisplayP3);
        const QImage srgbResult = ZoinGallery::ViewerResampler::downsample(srgb, {30, 22});
        const QImage p3Result = ZoinGallery::ViewerResampler::downsample(p3, {30, 22});
        QCOMPARE(srgbResult.colorSpace(), srgb.colorSpace());
        QCOMPARE(p3Result.colorSpace(), p3.colorSpace());
        for (int y = 0; y < srgbResult.height(); ++y) {
            for (int x = 0; x < srgbResult.width(); ++x) {
                QCOMPARE(srgbResult.pixel(x, y), p3Result.pixel(x, y));
            }
        }
    }

    void eachPyramidStageQuantizesBeforeTheNextStage() {
        QImage image(128, 64, QImage::Format_RGBA64_Premultiplied);
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                row[x] = QRgba64::fromRgba64((x * 7919 + y * 1237) % 65536,
                    (x * 997 + y * 3541) % 65536, 31 * 257 + 128, 65535);
            }
        }
        const QImage half = ZoinGallery::ViewerResampler::downsample(image, {64, 32});
        QCOMPARE(half.format(), QImage::Format_ARGB32_Premultiplied);
        const QImage staged = ZoinGallery::ViewerResampler::downsample(half, {32, 16});
        const QImage direct = ZoinGallery::ViewerResampler::downsample(image, {32, 16});
        QCOMPARE(direct, staged);
    }

    void quarterScalePeriodicPatterns_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("period");
        QTest::newRow("23-percent-period2") << 942 << 2;
        QTest::newRow("25-percent-period2") << 1024 << 2;
        QTest::newRow("23-percent-period4") << 942 << 4;
        QTest::newRow("25-percent-period4") << 1024 << 4;
    }

    void quarterScalePeriodicPatterns() {
        QFETCH(int, width);
        QFETCH(int, period);
        QImage image(4096, 128, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixel(x, y, x % period < period / 2
                    ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
            }
        }
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {width, 32});
        QVERIFY2(stripeAliasRms(result, 187.516) < 2.0,
                 "Out-of-band stripes should settle near their linear-light average");
    }

    void rejectsEmptyInput() {
        QVERIFY(ZoinGallery::ViewerResampler::downsample({}, {10, 10}).isNull());
        QImage image(10, 10, QImage::Format_RGB32);
        QVERIFY(ZoinGallery::ViewerResampler::downsample(image, {}).isNull());
        QVERIFY(ZoinGallery::ViewerResampler::downsample(image, {0, 10}).isNull());
    }

    void nativeSizeIsExactAndNeverUpscaled() {
        const QImage image = stripes({37, 21});
        const QImage native = ZoinGallery::ViewerResampler::downsample(image, image.size());
        QCOMPARE(native.cacheKey(), image.cacheKey());
        const QImage larger = ZoinGallery::ViewerResampler::downsample(image, {100, 100});
        QCOMPARE(larger.cacheKey(), image.cacheKey());
        QCOMPARE(ZoinGallery::ViewerResampler::downsample(image, {19, 100}).size(),
                 QSize(19, 21));
    }

    void nonIntegerDimensionsAndMetadata() {
        QImage image(103, 71, QImage::Format_ARGB32_Premultiplied);
        image.fill(qRgba(40, 80, 120, 160));
        image.setColorSpace(QColorSpace::DisplayP3);
        image.setDevicePixelRatio(1.75);
        image.setDotsPerMeterX(3500);
        image.setDotsPerMeterY(4200);
        image.setOffset({3, 7});
        image.setText(QStringLiteral("author"), QStringLiteral("viewer-test"));
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {33, 23});
        QCOMPARE(result.size(), QSize(33, 23));
        QCOMPARE(result.colorSpace(), image.colorSpace());
        QCOMPARE(result.devicePixelRatio(), image.devicePixelRatio());
        QCOMPARE(result.dotsPerMeterX(), image.dotsPerMeterX());
        QCOMPARE(result.dotsPerMeterY(), image.dotsPerMeterY());
        QCOMPARE(result.offset(), image.offset());
        QCOMPARE(result.text(QStringLiteral("author")), image.text(QStringLiteral("author")));
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 0; x < result.width(); ++x) {
                const QRgb color = result.pixel(x, y);
                QVERIFY(std::abs(qRed(color) - 40) <= 1);
                QVERIFY(std::abs(qGreen(color) - 80) <= 1);
                QVERIFY(std::abs(qBlue(color) - 120) <= 1);
                QVERIFY(std::abs(qAlpha(color) - 160) <= 1);
            }
        }
    }

    void transparentColorsDoNotLeak() {
        QImage image(127, 79, QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixel(x, y, x < 64 ? qRgba(255, 0, 0, 0)
                                          : qRgba(0, 0, 255, 180));
            }
        }
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {39, 25})
                                  .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 0; x < result.width(); ++x) {
                const QRgb color = result.pixel(x, y);
                QCOMPARE(qRed(color), 0);
                QCOMPARE(qGreen(color), 0);
                QVERIFY(qBlue(color) <= qAlpha(color));
                QVERIFY(std::abs(qBlue(color) - qAlpha(color)) <= 1);
            }
        }
    }

    void constantsAndThinImages_data() {
        QTest::addColumn<QSize>("sourceSize");
        QTest::addColumn<QSize>("targetSize");
        QTest::newRow("99-percent") << QSize(200, 100) << QSize(198, 99);
        QTest::newRow("90-percent") << QSize(200, 100) << QSize(180, 90);
        QTest::newRow("long-row") << QSize(8000, 1) << QSize(57, 1);
        QTest::newRow("long-column") << QSize(1, 8000) << QSize(1, 57);
        QTest::newRow("single-pixel") << QSize(127, 79) << QSize(1, 1);
    }

    void sixteenBitSourceIsFilteredBeforeQuantization() {
        QImage image(2, 1, QImage::Format_RGBA64_Premultiplied);
        auto *pixels = reinterpret_cast<QRgba64 *>(image.scanLine(0));
        pixels[0] = QRgba64::fromRgba64(100 * 257 - 64, 0, 0, 65535);
        pixels[1] = QRgba64::fromRgba64(101 * 257 - 64, 0, 0, 65535);
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {1, 1});
        // Their symmetric average is 100.251 in 8-bit units. Rounding the
        // two samples before convolution instead would round the mean to 101.
        QCOMPARE(qRed(result.pixel(0, 0)), 100);
        QCOMPARE(qAlpha(result.pixel(0, 0)), 255);
    }

    void constantsAndThinImages() {
        QFETCH(QSize, sourceSize);
        QFETCH(QSize, targetSize);
        QImage image(sourceSize, QImage::Format_RGB32);
        image.fill(qRgb(31, 129, 217));
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, targetSize);
        QCOMPARE(result.size(), targetSize);
        for (int y = 0; y < result.height(); ++y) {
            for (int x = 0; x < result.width(); ++x) {
                const QRgb color = result.pixel(x, y);
                QVERIFY(std::abs(qRed(color) - 31) <= 1);
                QVERIFY(std::abs(qGreen(color) - 129) <= 1);
                QVERIFY(std::abs(qBlue(color) - 217) <= 1);
                QCOMPARE(qAlpha(color), 255);
            }
        }
    }

    void periodicStripesLoseAliasedContrast_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("75-percent") << 1536;
        QTest::newRow("60-percent") << 1229;
        QTest::newRow("33-percent") << 682;
        QTest::newRow("11-percent") << 225;
    }

    void periodicStripesLoseAliasedContrast() {
        QFETCH(int, width);
        const QImage image = stripes({2048, 64});
        const QSize target(width, 32);
        const QImage oldArea = image.scaled(target, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, target);
        const double oldRms = stripeAliasRms(oldArea);
        const double newRms = stripeAliasRms(result, 187.516);
        QVERIFY2(newRms < oldRms * 0.8,
                 qPrintable(QStringLiteral("stripe alias RMS %1; old area %2")
                                .arg(newRms).arg(oldRms)));
    }

    void fortyMegapixelsToScreen() {
        // The working set is eight filtered float scanlines and one input
        // scanline, not a second full-resolution floating-point image.
        QImage image(8000, 5000, QImage::Format_RGB32);
        QVERIFY(!image.isNull());
        image.fill(qRgb(31, 129, 217));
        const QImage result = ZoinGallery::ViewerResampler::downsample(image, {1920, 1200});
        QCOMPARE(result.size(), QSize(1920, 1200));
        for (const QPoint point : {QPoint(0, 0), QPoint(959, 599), QPoint(1919, 1199)}) {
            const QRgb color = result.pixel(point);
            QVERIFY(std::abs(qRed(color) - 31) <= 1);
            QVERIFY(std::abs(qGreen(color) - 129) <= 1);
            QVERIFY(std::abs(qBlue(color) - 217) <= 1);
            QCOMPARE(qAlpha(color), 255);
        }
    }
};

QTEST_GUILESS_MAIN(ViewerResamplerTest)
#include "ViewerResamplerTest.moc"
