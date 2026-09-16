#include <QObject>

class ViewerResampleGpuTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void nativePixelsRemainIdentical();
    void alignedIdentityAtFractionalDprRemainsTexelExact();
    void fractionalIdentityAtFractionalDprUsesExactTexels();
    void fractionalPlacementInterpolatesDuringMotion();
    void blackWhiteHalfScaleUsesLinearLight();
    void pyramidMatchesCpuReference_data();
    void pyramidMatchesCpuReference();
    void transparencyFiltersPremultipliedLinearColor();
};

#include "ViewerResampler.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTest>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <memory>

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
        engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        engine.addImageProvider(QStringLiteral("gpu-fixture"),
                                new FixtureImageProvider(image));
        QQmlComponent component(&engine);
        component.setData(R"QML(
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
        const QString baselineShader = qEnvironmentVariable("F4_TEST_VIEWER_FRAGMENT_SHADER");
        if (!baselineShader.isEmpty())
            filtered()->setProperty("fragmentShader", QUrl::fromLocalFile(baselineShader));
        root->setProperty("testDpr", dpr);
        root->setProperty("testEffectPhysicalSize", effectPhysicalSize);
        root->setProperty("testEffectPhysicalOffset", effectPhysicalOffset);
        const QSizeF logicalSize = QSizeF(outputSize) / dpr;
        root->setSize(logicalSize);
        root->setParentItem(window.contentItem());
        window.contentItem()->setSize(logicalSize);
        window.setGeometry(QRect(QPoint(), logicalSize.toSize()));
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
        // The property does not exist in the regression baseline. Ignoring
        // setProperty's return keeps this control case able to demonstrate the
        // old linear sampling, while the identity tests require the new API.
        fixture.filtered()->setProperty("pixelAlignedIdentity", false);
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
