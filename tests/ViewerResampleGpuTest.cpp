#include <QObject>

class ViewerResampleGpuTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void nativePixelsRemainIdentical();
    void alignedIdentityAtFractionalDprRemainsTexelExact();
    void fractionalIdentityAtFractionalDprUsesExactTexels();
    void fractionalPlacementInterpolatesDuringMotion();
    void magnificationPreservesFineLineContrast();
    void magnificationMatchesCubicReference_data();
    void magnificationMatchesCubicReference();
    void magnificationFiltersTransparentEdges();
    void settledViewerMatchesCpuThroughRoundedFramebuffer_data();
    void settledViewerMatchesCpuThroughRoundedFramebuffer();
    void viewerMotionInterpolatesAndRestoresExactPresentation_data();
    void viewerMotionInterpolatesAndRestoresExactPresentation();
    void blackWhiteHalfScaleUsesLinearLight();
    void pyramidMatchesCpuReference_data();
    void pyramidMatchesCpuReference();
    void transparencyFiltersPremultipliedLinearColor();
};

#include "ViewerResampler.h"

#include <QGuiApplication>
#include <QLineF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTest>
#include <QTransform>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#ifndef Q_MOC_RUN
namespace {

class FixtureImageProvider final : public QQuickImageProvider {
public:
    explicit FixtureImageProvider(QImage image)
        : QQuickImageProvider(Image), m_image(std::move(image)) {}

    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        *size = m_image.size();
        return m_image;
    }

private:
    QImage m_image;
};

// No native window is exposed and no images are saved. Real ShaderEffect/QSB
// rendering runs into an RHI texture, whose bytes are checked numerically.
class GpuFixture {
public:
    GpuFixture() : window(&control) { window.setColor(Qt::transparent); }

    ~GpuFixture() {
        root.reset();
        window.setRenderTarget({});
        target.reset();
        pass.reset();
        depth.reset();
        texture.reset();
        control.invalidate();
    }

    bool initialize(const QImage &image, QSize outputSize, qreal dpr = 1,
                    QSize effectPhysicalSize = {},
                    QPointF effectPhysicalOffset = {}) {
        return initializeScene(image, outputSize, dpr, effectPhysicalSize,
                               effectPhysicalOffset, false, 1, 0);
    }

    bool initializeViewer(const QImage &image, QSize outputSize, qreal dpr,
                          qreal scale, int rotation = 0) {
        return initializeScene(image, outputSize, dpr, {}, {}, true, scale,
                               rotation);
    }

    bool initializeScene(const QImage &image, QSize outputSize, qreal dpr,
                         QSize effectPhysicalSize, QPointF effectPhysicalOffset,
                         bool viewer, qreal viewerScale, int rotation) {
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        engine.addImageProvider(QStringLiteral("gpu-fixture"),
                                new FixtureImageProvider(image));
        QQmlComponent component(&engine);
        component.setData(viewer ? R"QML(
            import QtQuick
            import ZoinGallery
            Item {
                id: testRoot
                property real testDpr: 1
                property size testImagePixelSize: Qt.size(0, 0)
                property size testEffectPhysicalSize: Qt.size(0, 0)
                property point testEffectPhysicalOffset: Qt.point(0, 0)
                property real testViewerScale: 1
                property int testRotation: 0
                property bool externalMoving: false
                readonly property var filtered: zoomable.image.shader
                property alias viewport: zoomable
                function prepare() {
                    zoomable.setImage("image://gpu-fixture/source",
                                      testImagePixelSize, 0, 0)
                    zoomable.rotationMode = testRotation
                    zoomable.setViewport(testViewerScale, 0, 0)
                    zoomable.setImage("image://gpu-fixture/source",
                                      testImagePixelSize, 0, 2)
                    // Isolate the image presentation. Fit can briefly reveal
                    // scrollbars before an oversized native source is applied.
                    zoomable.vbar.visible = false
                    zoomable.hbar.visible = false
                }
                FlickableZoomable {
                    id: zoomable
                    x: testRoot.testEffectPhysicalOffset.x / testRoot.testDpr
                    y: testRoot.testEffectPhysicalOffset.y / testRoot.testDpr
                    width: testRoot.width
                    height: testRoot.height
                    devicePixelRatio: testRoot.testDpr
                    pixelAlignmentRevision: testRoot.testEffectPhysicalOffset.x
                                            + testRoot.testEffectPhysicalOffset.y
                    externalTransformMoving: testRoot.externalMoving
                    animationDuration: 0
                    active: true
                }
            }
        )QML" : R"QML(
            import QtQuick
            import ZoinGallery
            Item {
                property real testDpr: 1
                property size testEffectPhysicalSize: Qt.size(0, 0)
                property point testEffectPhysicalOffset: Qt.point(0, 0)
                property alias filtered: filtered
                Image {
                    id: input
                    source: "image://gpu-fixture/source"
                    cache: false
                    asynchronous: false
                    mipmap: false
                    visible: false
                }
                ViewerResample {
                    id: filtered
                    x: parent.testEffectPhysicalOffset.x / parent.testDpr
                    y: parent.testEffectPhysicalOffset.y / parent.testDpr
                    width: parent.testEffectPhysicalSize.width / parent.testDpr
                    height: parent.testEffectPhysicalSize.height / parent.testDpr
                    imageSource: input
                    viewportSize: parent.testEffectPhysicalSize
                }
            }
        )QML", QUrl(QStringLiteral("qrc:/gpu-resampler-fixture.qml")));
        root.reset(qobject_cast<QQuickItem *>(component.create()));
        if (!root) {
            error = component.errorString();
            return false;
        }
        if (effectPhysicalSize.isEmpty())
            effectPhysicalSize = outputSize;
        root->setProperty("testDpr", dpr);
        root->setProperty("testEffectPhysicalSize", effectPhysicalSize);
        root->setProperty("testEffectPhysicalOffset", effectPhysicalOffset);
        const QSizeF exactLogicalSize = QSizeF(outputSize) / dpr;
        // A real QQuickWindow has integer logical dimensions. Its framebuffer
        // may cover half a physical pixel more than logicalSize * DPR.
        const QSizeF logicalSize = viewer
            ? QSizeF(exactLogicalSize.toSize()) : exactLogicalSize;
        root->setSize(logicalSize);
        root->setParentItem(window.contentItem());
        window.contentItem()->setSize(logicalSize);
        window.setGeometry(QRect(QPoint(), logicalSize.toSize()));
        if (viewer) {
            root->setProperty("testImagePixelSize", image.size());
            root->setProperty("testViewerScale", viewerScale);
            root->setProperty("testRotation", rotation);
            if (!QMetaObject::invokeMethod(root.get(), "prepare")) {
                error = QStringLiteral("Could not initialize the full image viewer");
                return false;
            }
        }
        if (!control.initialize()) {
            graphicsUnavailable = true;
            error = QStringLiteral("Could not initialize the RHI shader backend");
            return false;
        }
        rhi = control.rhi();
        if (!rhi || rhi->backend() == QRhi::Null
                || window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
            error = QStringLiteral("GPU regression requires an actual RHI shader backend");
            return false;
        }
        texture.reset(rhi->newTexture(QRhiTexture::RGBA8, outputSize, 1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, outputSize));
        if (!texture->create() || !depth->create()) {
            error = QStringLiteral("Could not allocate the offscreen render attachments");
            return false;
        }
        QRhiTextureRenderTargetDescription description(QRhiColorAttachment(texture.get()));
        description.setDepthStencilBuffer(depth.get());
        target.reset(rhi->newTextureRenderTarget(description));
        pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create()) {
            error = QStringLiteral("Could not create the offscreen render target");
            return false;
        }
        auto renderTarget = QQuickRenderTarget::fromRhiRenderTarget(target.get());
        renderTarget.setDevicePixelRatio(dpr);
        window.setRenderTarget(renderTarget);
        return true;
    }

    QImage render() {
        QImage result;
        // Allow texture providers and the retained pyramid to settle. Every
        // frame executes actual QSB shaders; a software scene graph cannot pass.
        for (int frame = 0; frame < 4; ++frame) {
            QCoreApplication::processEvents();
            control.polishItems();
            control.beginFrame();
            control.sync();
            control.render();
            QRhiReadbackResult readback;
            bool completed = false;
            readback.completed = [&] {
                const QImage bytes(
                    reinterpret_cast<const uchar *>(readback.data.constData()),
                    readback.pixelSize.width(), readback.pixelSize.height(),
                    QImage::Format_RGBA8888_Premultiplied);
                result = rhi->isYUpInFramebuffer() ? bytes.mirrored() : bytes.copy();
                completed = true;
            };
            auto *batch = rhi->nextResourceUpdateBatch();
            batch->readBackTexture(texture.get(), &readback);
            control.commandBuffer()->resourceUpdate(batch);
            control.endFrame();
            if (!completed) {
                error = QStringLiteral("Synchronous RHI readback did not complete");
                return {};
            }
        }
        return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    QObject *filtered() const { return root->property("filtered").value<QObject *>(); }
    bool setPixelAlignedIdentity(bool enabled) {
        return filtered()->setProperty("pixelAlignedIdentity", enabled);
    }
    QQuickItem *filteredItem() const {
        return qobject_cast<QQuickItem *>(filtered());
    }
    QPointF scenePoint(QPointF point = {}) const {
        return filteredItem()->mapToItem(window.contentItem(), point);
    }
    QRectF sceneBounds() const {
        return filteredItem()->mapRectToItem(window.contentItem(),
            QRectF(QPointF(), filteredItem()->size()));
    }
    bool setViewerMoving(bool moving) {
        return root->setProperty("externalMoving", moving);
    }
    bool setViewerPhysicalOffset(QPointF offset) {
        return root->setProperty("testEffectPhysicalOffset", offset);
    }
    bool setViewerPhysicalPan(QPointF pan, qreal dpr) {
        auto *viewport = root->property("viewport").value<QObject *>();
        return viewport && QMetaObject::invokeMethod(viewport, "setViewport",
            Q_ARG(QVariant, viewport->property("zoomScale")),
            Q_ARG(QVariant, QVariant(pan.x() / dpr)),
            Q_ARG(QVariant, QVariant(pan.y() / dpr)));
    }
    QRhi::Implementation backend() const { return rhi->backend(); }
    QByteArray deviceName() const { return rhi->driverInfo().deviceName; }

    QString error;
    bool graphicsUnavailable = false;

private:
    QQuickRenderControl control;
    QQuickWindow window;
    QQmlEngine engine;
    std::unique_ptr<QQuickItem> root;
    QRhi *rhi = nullptr;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
};

QImage periodicImage(QSize size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const int red = ((x / 2 + y / 3) % 2) ? 235 : 17;
            const int green = (x * 37 + y * 23) % 256;
            const int blue = ((x + 2 * y) % 5 < 2) ? 255 : 0;
            image.setPixel(x, y, qRgb(red, green, blue));
        }
    }
    return image;
}

QImage singlePixelPattern(QSize size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            image.setPixel(x, y, qRgb(x % 2 ? 255 : 0,
                                     y % 2 ? 255 : 0,
                                     (x + y) % 2 ? 255 : 0));
        }
    }
    return image;
}

double decodeChannel(double value) {
    return value <= 0.04045 ? value / 12.92
                           : std::pow((value + 0.055) / 1.055, 2.4);
}

double encodeChannel(double value) {
    return value <= 0.0031308 ? value * 12.92
                            : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

// Independent reference: interpolate each interval using cubic Hermite basis
// functions and centered finite-difference tangents, rather than shader taps.
double interpolateCubic(double p0, double p1, double p2, double p3, double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * p1
        + (t3 - 2 * t2 + t) * (p2 - p0) / 2
        + (-2 * t3 + 3 * t2) * p2
        + (t3 - t2) * (p3 - p1) / 2;
}

QImage cubicMagnificationReference(const QImage &source, QSize targetSize) {
    const QImage input = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    using Pixel = std::array<double, 4>;
    std::vector<Pixel> linear(input.width() * input.height());
    for (int y = 0; y < input.height(); ++y) {
        for (int x = 0; x < input.width(); ++x) {
            const QRgb pixel = input.pixel(x, y);
            const double alpha = qAlpha(pixel) / 255.0;
            linear[y * input.width() + x] = alpha > 0
                ? Pixel{decodeChannel(qRed(pixel) / (255 * alpha)) * alpha,
                        decodeChannel(qGreen(pixel) / (255 * alpha)) * alpha,
                        decodeChannel(qBlue(pixel) / (255 * alpha)) * alpha, alpha}
                : Pixel{};
        }
    }
    QImage result(targetSize, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < result.height(); ++y) {
        const double sy = (y + 0.5) * input.height() / result.height() - 0.5;
        const int iy = int(std::floor(sy));
        for (int x = 0; x < result.width(); ++x) {
            const double sx = (x + 0.5) * input.width() / result.width() - 0.5;
            const int ix = int(std::floor(sx));
            Pixel filtered{};
            for (int channel = 0; channel < 4; ++channel) {
                double rows[4];
                for (int row = 0; row < 4; ++row) {
                    const int py = std::clamp(iy + row - 1, 0, input.height() - 1);
                    double samples[4];
                    for (int col = 0; col < 4; ++col) {
                        const int px = std::clamp(ix + col - 1, 0, input.width() - 1);
                        samples[col] = linear[py * input.width() + px][channel];
                    }
                    rows[row] = interpolateCubic(samples[0], samples[1],
                                                  samples[2], samples[3], sx - ix);
                }
                filtered[channel] = interpolateCubic(rows[0], rows[1], rows[2], rows[3], sy - iy);
            }
            const double alpha = std::clamp(filtered[3], 0.0, 1.0);
            const auto encode = [alpha](double value) {
                return alpha > 0
                    ? qRound(encodeChannel(std::clamp(value / alpha, 0.0, 1.0)) * alpha * 255)
                    : 0;
            };
            result.setPixel(x, y, qRgba(encode(filtered[0]), encode(filtered[1]),
                                        encode(filtered[2]), qRound(alpha * 255)));
        }
    }
    return result;
}

void comparePixels(const QImage &actual, const QImage &expected, int tolerance) {
    QCOMPARE(actual.size(), expected.size());
    const QImage reference = expected.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            const QRgb a = actual.pixel(x, y);
            const QRgb b = reference.pixel(x, y);
            const int difference = std::max({std::abs(qRed(a) - qRed(b)),
                std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
                std::abs(qAlpha(a) - qAlpha(b))});
            QVERIFY2(difference <= tolerance,
                qPrintable(QStringLiteral("pixel (%1,%2): GPU=%3,%4,%5,%6 CPU=%7,%8,%9,%10 diff=%11")
                    .arg(x).arg(y).arg(qRed(a)).arg(qGreen(a)).arg(qBlue(a)).arg(qAlpha(a))
                    .arg(qRed(b)).arg(qGreen(b)).arg(qBlue(b)).arg(qAlpha(b)).arg(difference)));
        }
    }
}

} // namespace
#endif

void ViewerResampleGpuTest::initTestCase() {
    QImage image(4, 4, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    GpuFixture fixture;
    const bool initialized = fixture.initialize(image, image.size());
#ifndef Q_OS_WIN
    if (!initialized && fixture.graphicsUnavailable)
        QSKIP("No headless RHI context is available on this host");
#endif
    QVERIFY2(initialized, qPrintable(fixture.error));
#ifdef Q_OS_WIN
    QCOMPARE(fixture.backend(), QRhi::D3D11);
#endif
    qInfo().noquote() << "Numerical shader backend" << fixture.backend()
                      << "device" << fixture.deviceName();
}

void ViewerResampleGpuTest::nativePixelsRemainIdentical() {
        const QImage input = periodicImage({37, 29});
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, input.size()), qPrintable(fixture.error));
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        comparePixels(actual, input, 0);
    }

void ViewerResampleGpuTest::alignedIdentityAtFractionalDprRemainsTexelExact() {
        const QImage input = singlePixelPattern({35, 28});
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, {42, 35}, 1.75, input.size(), {3, 3}),
                 qPrintable(fixture.error));
        QVERIFY2(fixture.setPixelAlignedIdentity(true),
                 "ViewerResample must expose pixelAlignedIdentity to its shader");
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        comparePixels(actual.copy(QRect(QPoint(3, 3), input.size())), input, 0);
    }

void ViewerResampleGpuTest::fractionalIdentityAtFractionalDprUsesExactTexels() {
        const QImage input = singlePixelPattern({35, 28});
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, {42, 35}, 1.75, input.size(),
                                    {3.25, 3.25}),
                 qPrintable(fixture.error));
        QVERIFY2(fixture.setPixelAlignedIdentity(true),
                 "ViewerResample must expose pixelAlignedIdentity to its shader");
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        // Skip the partially covered leading edge. Every fully covered output
        // pixel must select one source texel rather than blend its neighbours.
        comparePixels(actual.copy(QRect(4, 4, 34, 27)),
                      input.copy(QRect(1, 1, 34, 27)), 0);
    }

void ViewerResampleGpuTest::fractionalPlacementInterpolatesDuringMotion() {
        const QImage input = singlePixelPattern({35, 28});
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, {42, 35}, 1.75, input.size(),
                                    {3.25, 3.25}),
                 qPrintable(fixture.error));
        QVERIFY(fixture.setPixelAlignedIdentity(false));
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));

        int interpolatedPixels = 0;
        const QImage interior = actual.copy(QRect(4, 4, 34, 27));
        for (int y = 0; y < interior.height(); ++y) {
            for (int x = 0; x < interior.width(); ++x) {
                const QRgb pixel = interior.pixel(x, y);
                const auto betweenTexels = [](int channel) {
                    return channel > 0 && channel < 255;
                };
                if (betweenTexels(qRed(pixel)) || betweenTexels(qGreen(pixel))
                        || betweenTexels(qBlue(pixel)))
                    ++interpolatedPixels;
            }
        }
        QVERIFY2(interpolatedPixels > interior.width() * interior.height() * 9 / 10,
                 qPrintable(QStringLiteral("expected fractional motion sampling, got only %1/%2 interpolated pixels")
                     .arg(interpolatedPixels).arg(interior.width() * interior.height())));
    }

void ViewerResampleGpuTest::magnificationPreservesFineLineContrast() {
    const QImage input = singlePixelPattern({16, 12});
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(input, input.size() * 2), qPrintable(fixture.error));
    const QImage actual = fixture.render();
    QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
    // Alternating samples have zero Hermite tangents. At the quarter phase,
    // smoothstep gives 5/32 and 27/32, before transfer encoding.
    const int low = qRound(encodeChannel(5.0 / 32) * 255);
    const int high = qRound(encodeChannel(27.0 / 32) * 255);
    qInfo() << "[FIX:cubic-magnification] 2x line levels"
            << qRed(actual.pixel(4, 4)) << qRed(actual.pixel(6, 4))
            << "expected" << low << high;
    for (int y = 4; y < actual.height() - 4; ++y) {
        for (int x = 4; x < actual.width() - 4; ++x) {
            const int expected = x % 4 < 2 ? low : high;
            QVERIFY2(std::abs(qRed(actual.pixel(x, y)) - expected) <= 1,
                qPrintable(QStringLiteral("2x line contrast at (%1,%2): got %3, expected %4")
                    .arg(x).arg(y).arg(qRed(actual.pixel(x, y))).arg(expected)));
        }
    }
}

void ViewerResampleGpuTest::magnificationMatchesCubicReference_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<QSize>("targetSize");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("150-percent") << QSize(16, 12) << QSize(24, 18) << qreal(1);
    QTest::newRow("175-percent") << QSize(16, 12) << QSize(28, 21) << qreal(1);
    QTest::newRow("230-percent") << QSize(20, 10) << QSize(46, 23) << qreal(1);
    QTest::newRow("odd-source-dpr175") << QSize(29, 23) << QSize(56, 42) << qreal(1.75);
    QTest::newRow("one-to-one-y") << QSize(16, 12) << QSize(32, 12) << qreal(1);
    QTest::newRow("single-row") << QSize(17, 1) << QSize(39, 1) << qreal(1);
    QTest::newRow("single-column") << QSize(1, 17) << QSize(1, 39) << qreal(1);
}

void ViewerResampleGpuTest::magnificationMatchesCubicReference() {
    QFETCH(QSize, sourceSize);
    QFETCH(QSize, targetSize);
    QFETCH(qreal, dpr);
    const QImage input = periodicImage(sourceSize);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(input, targetSize, dpr), qPrintable(fixture.error));
    const QImage actual = fixture.render();
    QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
    comparePixels(actual, cubicMagnificationReference(input, targetSize), 2);
}

void ViewerResampleGpuTest::magnificationFiltersTransparentEdges() {
    QImage input(12, 8, QImage::Format_ARGB32);
    for (int y = 0; y < input.height(); ++y)
        for (int x = 0; x < input.width(); ++x)
            input.setPixel(x, y, x >= 4 && y >= 3
                ? qRgba(0, 0, 128, 180) : qRgba(255, 255, 0, 0));
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(input, {30, 20}), qPrintable(fixture.error));
    const QImage actual = fixture.render();
    QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
    comparePixels(actual, cubicMagnificationReference(input, actual.size()), 2);
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            const QRgb pixel = actual.pixel(x, y);
            QVERIFY(qRed(pixel) <= 1 && qGreen(pixel) <= 1);
            QVERIFY(qBlue(pixel) <= qAlpha(pixel));
            if (qAlpha(pixel) == 0)
                QCOMPARE(pixel, qRgba(0, 0, 0, 0));
        }
    }
}

void ViewerResampleGpuTest::settledViewerMatchesCpuThroughRoundedFramebuffer_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<int>("rotation");
    QTest::addColumn<QSize>("framebufferSize");
    QTest::addColumn<bool>("cpuReference");
    QTest::addColumn<QPointF>("physicalPan");
    const QSize framebuffer(3840, 2076);
    const auto row = [](const char *name, QSize source, qreal scale, int rotation,
                        QSize frame, bool cpu = true, QPointF pan = {}) {
        QTest::newRow(name) << source << scale << rotation << frame << cpu << pan;
    };
    row("native", {1396, 768}, 1, 0, framebuffer);
    row("half", {1396, 768}, 0.5, 0, framebuffer);
    row("quarter", {1392, 784}, 0.25, 0, framebuffer);
    row("three-eighths", {1392, 784}, 0.375, 0, framebuffer);
    row("odd-source", {1201, 799}, 0.5, 0, framebuffer, false);
    row("half-rotated-90", {1396, 768}, 0.5, 1, framebuffer);
    row("half-rotated-180", {1396, 768}, 0.5, 2, framebuffer);
    row("half-rotated-270", {1396, 768}, 0.5, 3, framebuffer);
    row("half-opposite-rounding", {1396, 768}, 0.5, 0, {3846, 2077});
    row("native-clipped", {4200, 2400}, 1, 0, framebuffer);
    row("double", {139, 77}, 2, 0, framebuffer);
    row("magnified-fractional", {139, 77}, 1.5, 0, framebuffer);
    row("magnified-rotated-clipped", {200, 128}, 2, 1, {423, 281});
    row("magnified-rotated-trailing", {200, 128}, 2, 1, {423, 281}, true, {0, -10000});
    row("magnified-negative-pan", {1500, 128}, 2, 1, {423, 281}, true, {0, -2500});
}

void ViewerResampleGpuTest::settledViewerMatchesCpuThroughRoundedFramebuffer() {
    QFETCH(QSize, sourceSize);
    QFETCH(qreal, scale);
    QFETCH(int, rotation);
    QFETCH(QSize, framebufferSize);
    QFETCH(bool, cpuReference);
    QFETCH(QPointF, physicalPan);
    const qreal dpr = 1.75;
    const QImage input = periodicImage(sourceSize);
    GpuFixture fixture;
    QVERIFY2(fixture.initializeViewer(input, framebufferSize, dpr, scale, rotation),
             qPrintable(fixture.error));
    if (!physicalPan.isNull())
        QVERIFY(fixture.setViewerPhysicalPan(physicalPan, dpr));
    QQuickItem *shader = fixture.filteredItem();
    QVERIFY(shader);
    QCoreApplication::processEvents();

    const QPointF origin = fixture.scenePoint();
    const QPointF unitX = fixture.scenePoint({1, 0}) - origin;
    const QPointF unitY = fixture.scenePoint({0, 1}) - origin;
    const QSize presentationSize(qRound(shader->width() * dpr),
                                 qRound(shader->height() * dpr));
    const QRectF sceneBounds = fixture.sceneBounds();
    const QRect presentationRect(
        QPoint(qRound(sceneBounds.x() * dpr), qRound(sceneBounds.y() * dpr)),
        QSize(qRound(sceneBounds.width() * dpr),
              qRound(sceneBounds.height() * dpr)));
    const QSize roundedLogicalSize = (QSizeF(framebufferSize) / dpr).toSize();
    const QPointF uncorrectedRasterOrigin(
        origin.x() * framebufferSize.width() / roundedLogicalSize.width(),
        origin.y() * framebufferSize.height() / roundedLogicalSize.height());
    const QString details = QStringLiteral(
        "[FIX:framebuffer-resampling] source=%1x%2 scale=%3 rotation=%4 "
        "physicalSceneOrigin=(%5,%6) extent=%7x%8 framebuffer=%9x%10 "
        "unitX=(%11,%12) unitY=(%13,%14) uncorrectedRasterOrigin=(%15,%16)")
        .arg(sourceSize.width()).arg(sourceSize.height()).arg(scale).arg(rotation)
        .arg(origin.x() * dpr, 0, 'f', 6).arg(origin.y() * dpr, 0, 'f', 6)
        .arg(presentationSize.width()).arg(presentationSize.height())
        .arg(framebufferSize.width()).arg(framebufferSize.height())
        .arg(unitX.x()).arg(unitX.y()).arg(unitY.x()).arg(unitY.y())
        .arg(uncorrectedRasterOrigin.x(), 0, 'f', 6)
        .arg(uncorrectedRasterOrigin.y(), 0, 'f', 6);
    qInfo().noquote() << details;
    for (qreal edge : {origin.x() * dpr, origin.y() * dpr,
                       sceneBounds.left() * dpr, sceneBounds.top() * dpr,
                       sceneBounds.right() * dpr, sceneBounds.bottom() * dpr}) {
        QVERIFY2(qAbs(edge - qRound(edge)) < 0.001, qPrintable(details));
    }
    QTransform rotationTransform;
    rotationTransform.rotate(rotation * 90);
    QVERIFY2(QLineF(unitX, rotationTransform.map(QPointF(1, 0))).length() < 0.001,
             qPrintable(details));
    QVERIFY2(QLineF(unitY, rotationTransform.map(QPointF(0, 1))).length() < 0.001,
             qPrintable(details));

    QImage expected;
    if (cpuReference) {
        expected = scale > 1 ? cubicMagnificationReference(input, presentationSize)
            : ZoinGallery::ViewerResampler::downsample(input, presentationSize);
    } else {
        // Existing CPU and GPU pyramid policies differ at this odd threshold:
        // 1201x799 -> 601x399 halves Y alone on the CPU (798 source texels),
        // while the shared GPU level cannot halve X and samples the full 799.
        // Compare the same GPU filter on an exact framebuffer to isolate this
        // regression's final physical mapping from that separate filter policy.
        GpuFixture reference;
        QVERIFY2(reference.initialize(input, presentationSize),
                 qPrintable(reference.error));
        expected = reference.render();
        QVERIFY2(!expected.isNull(), qPrintable(reference.error));
        const QImage cpu = ZoinGallery::ViewerResampler::downsample(input,
                                                                  presentationSize);
        const QRgb gpuSample = expected.pixel(3, 2);
        const QRgb cpuSample = cpu.pixel(3, 2);
        qInfo().noquote() << QStringLiteral(
            "[FIX:framebuffer-resampling] odd threshold at (3,2): "
            "exact-framebuffer GPU=(%1,%2,%3), independent-axis CPU=(%4,%5,%6)")
            .arg(qRed(gpuSample)).arg(qGreen(gpuSample)).arg(qBlue(gpuSample))
            .arg(qRed(cpuSample)).arg(qGreen(cpuSample)).arg(qBlue(cpuSample));
    }
    expected = expected.transformed(rotationTransform);
    const QImage frame = fixture.render();
    QVERIFY2(!frame.isNull(), qPrintable(fixture.error));
    QCOMPARE(frame.size(), framebufferSize);
    QCOMPARE(presentationRect.size(), expected.size());
    // Compare every image pixel, including the final row and column. An exact
    // fetch with an uncorrected framebuffer stretch can otherwise hide a
    // duplicated texel behind a sharp-looking interior.
    const QRect visibleRect = presentationRect.intersected(frame.rect());
    comparePixels(frame.copy(visibleRect),
                  expected.copy(QRect(visibleRect.topLeft() - presentationRect.topLeft(),
                                      visibleRect.size())), 2);
    const QRect perimeter = presentationRect.adjusted(-1, -1, 1, 1)
                                .intersected(frame.rect());
    for (int y = perimeter.top(); y <= perimeter.bottom(); ++y) {
        for (int x = perimeter.left(); x <= perimeter.right(); ++x) {
            if (presentationRect.contains(x, y))
                continue;
            QVERIFY2(qAlpha(frame.pixel(x, y)) == 0,
                qPrintable(QStringLiteral("Unexpected image coverage outside %1,%2 %3x%4 at (%5,%6)")
                    .arg(presentationRect.x()).arg(presentationRect.y())
                    .arg(presentationRect.width()).arg(presentationRect.height())
                    .arg(x).arg(y)));
        }
    }
}

void ViewerResampleGpuTest::viewerMotionInterpolatesAndRestoresExactPresentation_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<qreal>("scale");
    QTest::newRow("half") << QSize(1396, 768) << qreal(0.5);
    QTest::newRow("double") << QSize(140, 98) << qreal(2);
}

void ViewerResampleGpuTest::viewerMotionInterpolatesAndRestoresExactPresentation() {
    QFETCH(QSize, sourceSize);
    QFETCH(qreal, scale);
    const qreal dpr = 1.75;
    const QImage input = periodicImage(sourceSize);
    const QSize targetSize = (QSizeF(sourceSize) * scale).toSize();
    const QImage expected = scale > 1 ? cubicMagnificationReference(input, targetSize)
        : ZoinGallery::ViewerResampler::downsample(input, targetSize);
    GpuFixture fixture;
    QVERIFY2(fixture.initializeViewer(input, {3840, 2076}, dpr, scale),
             qPrintable(fixture.error));
    const auto imageRect = [&fixture, dpr] {
        const QRectF bounds = fixture.sceneBounds();
        return QRect(QPoint(qRound(bounds.x() * dpr), qRound(bounds.y() * dpr)),
                     QSize(qRound(bounds.width() * dpr),
                           qRound(bounds.height() * dpr)));
    };
    const QImage restingFrame = fixture.render();
    QVERIFY2(!restingFrame.isNull(), qPrintable(fixture.error));
    comparePixels(restingFrame.copy(imageRect()), expected, 2);

    QVERIFY(fixture.setViewerMoving(true));
    QVERIFY(fixture.setViewerPhysicalOffset({0.21, 0.27}));
    const QImage movingFrame = fixture.render();
    QVERIFY2(!movingFrame.isNull(), qPrintable(fixture.error));
    QVERIFY(!fixture.filtered()->property("pixelAlignedIdentity").toBool());
    const QImage movingImage = movingFrame.copy(imageRect());
    int interpolatedPixels = 0;
    for (int y = 2; y < expected.height() - 2; ++y) {
        for (int x = 2; x < expected.width() - 2; ++x) {
            const QRgb actual = movingImage.pixel(x, y);
            const QRgb reference = expected.pixel(x, y);
            if (std::max({std::abs(qRed(actual) - qRed(reference)),
                          std::abs(qGreen(actual) - qGreen(reference)),
                          std::abs(qBlue(actual) - qBlue(reference))}) > 3)
                ++interpolatedPixels;
        }
    }
    QVERIFY2(interpolatedPixels > expected.width() * expected.height() / 2,
             qPrintable(QStringLiteral("Expected continuous fractional sampling during motion, got %1 changed pixels")
                 .arg(interpolatedPixels)));

    QVERIFY(fixture.setViewerMoving(false));
    const QImage settledFrame = fixture.render();
    QVERIFY2(!settledFrame.isNull(), qPrintable(fixture.error));
    comparePixels(settledFrame.copy(imageRect()), expected, 2);
}

void ViewerResampleGpuTest::blackWhiteHalfScaleUsesLinearLight() {
        QImage input(64, 64, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < input.height(); ++y)
            for (int x = 0; x < input.width(); ++x)
                input.setPixel(x, y, (x + y) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, {32, 32}), qPrintable(fixture.error));
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        for (int y = 5; y < actual.height() - 5; ++y) {
            for (int x = 5; x < actual.width() - 5; ++x) {
                const QRgb pixel = actual.pixel(x, y);
                QVERIFY2(qRed(pixel) >= 187 && qRed(pixel) <= 188,
                    qPrintable(QStringLiteral("linear 50%% black/white should encode 187..188, got %1 at (%2,%3)")
                        .arg(qRed(pixel)).arg(x).arg(y)));
                QCOMPARE(qRed(pixel), qGreen(pixel));
                QCOMPARE(qRed(pixel), qBlue(pixel));
                QCOMPARE(qAlpha(pixel), 255);
            }
        }
    }

void ViewerResampleGpuTest::pyramidMatchesCpuReference_data() {
        QTest::addColumn<QSize>("sourceSize");
        QTest::addColumn<QSize>("targetSize");
        QTest::addColumn<qreal>("dpr");
        QTest::newRow("25-percent") << QSize(128, 96) << QSize(32, 24) << qreal(1);
        QTest::newRow("23-percent") << QSize(128, 96) << QSize(29, 22) << qreal(1);
        QTest::newRow("odd-floor-phase") << QSize(129, 97) << QSize(32, 24) << qreal(1);
        QTest::newRow("25-percent-dpr175") << QSize(224, 168) << QSize(56, 42) << qreal(1.75);
        QTest::newRow("23-percent-dpr175") << QSize(244, 184) << QSize(56, 42) << qreal(1.75);
    }

void ViewerResampleGpuTest::pyramidMatchesCpuReference() {
        QFETCH(QSize, sourceSize);
        QFETCH(QSize, targetSize);
        QFETCH(qreal, dpr);
        const QImage input = periodicImage(sourceSize);
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, targetSize, dpr), qPrintable(fixture.error));
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        QObject *selected = fixture.filtered()->property("source").value<QObject *>();
        QVERIFY(selected);
        QCOMPARE(selected->property("textureSize").toSize(),
                 QSize(sourceSize.width() / 4, sourceSize.height() / 4));
        comparePixels(actual, ZoinGallery::ViewerResampler::downsample(input, targetSize), 2);
    }

void ViewerResampleGpuTest::transparencyFiltersPremultipliedLinearColor() {
        QImage input(64, 64, QImage::Format_ARGB32);
        for (int y = 0; y < input.height(); ++y)
            for (int x = 0; x < input.width(); ++x)
                input.setPixel(x, y, x % 2 ? qRgba(128, 0, 0, 128) : qRgba(0, 255, 255, 0));
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(input, {32, 32}), qPrintable(fixture.error));
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        for (int y = 5; y < 27; ++y) {
            for (int x = 5; x < 27; ++x) {
                const QRgb pixel = actual.pixel(x, y);
                QVERIFY(std::abs(qAlpha(pixel) - 64) <= 1);
                QVERIFY(std::abs(qRed(pixel) - 32) <= 1);
                QCOMPARE(qGreen(pixel), 0);
                QCOMPARE(qBlue(pixel), 0);
            }
        }
        comparePixels(actual, ZoinGallery::ViewerResampler::downsample(input, {32, 32}), 2);
    }
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_SCALE_FACTOR", "1");
    // offscreen QPA otherwise selects the software scene graph before the
    // render-control RHI can initialize. Explicitly require real shaders.
    qputenv("QT_QUICK_BACKEND", "rhi");
#ifdef Q_OS_WIN
    qputenv("QSG_RHI_BACKEND", "d3d11");
    qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#elif defined(Q_OS_MACOS)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    QGuiApplication application(argc, argv);
    ViewerResampleGpuTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ViewerResampleGpuTest.moc"
