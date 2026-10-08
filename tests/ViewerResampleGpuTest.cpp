#include <QObject>

class ViewerResampleGpuTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void shaderStageInterfacesMatch();
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
    void fractionalWaylandBackingScalePreservesSettledGeometry();
    void viewerMotionInterpolatesAndRestoresExactPresentation_data();
    void viewerMotionInterpolatesAndRestoresExactPresentation();
    void flightSamplingUsesHardwareAndRestoresQuality_data();
    void flightSamplingUsesHardwareAndRestoresQuality();
    void blackWhiteHalfScaleUsesLinearLight();
    void pyramidMatchesCpuReference_data();
    void pyramidMatchesCpuReference();
    void transparencyFiltersPremultipliedLinearColor();
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    void videoFramesRenderAndUpdate_data();
    void videoFramesRenderAndUpdate();
    void videoResampleMatchesReference_data();
    void videoResampleMatchesReference();
    void videoConversionSurvivesPanning_data();
    void videoConversionSurvivesPanning();
    void videoConversionExclusions_data();
    void videoConversionExclusions();
    void videoConversionInputs_data();
    void videoConversionInputs();
    void videoConversionSurvivesPyramidResize_data();
    void videoConversionSurvivesPyramidResize();
    void conversionShaderFailureNeverPublishesTexture();
    void pausedVideoReusesPresentation_data();
    void pausedVideoReusesPresentation();
    void viewerVideoCacheEligibility();
    void videoFlightSamplingRestoresQuality_data();
    void videoFlightSamplingRestoresQuality();
    void decodedVideoFileRenders();
    void decodedVideoReusesPresentation();
    void decodedVideoResampleBenchmark_data();
    void decodedVideoResampleBenchmark();
    void audioDeviceDiagnostics();
#endif
};

#include "ViewerResampler.h"
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
#include "ViewerVideoFrameSource.h"
#include "ViewerLinearVideoTexture.h"
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgdefaultrendercontext_p.h>
#include <QVideoFrameFormat>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QAudioDevice>
#endif

#include <QGuiApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QLineF>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QSet>
#include <QSurfaceFormat>
#include <QSignalSpy>
#include <QTest>
#include <QTransform>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#ifndef Q_MOC_RUN
namespace {

int finalResampleDraws = 0;
int intermediateResampleDraws = 0;
int linearVideoSampleDraws = 0;
int linearVideoConversions = 0;
quint64 lastLinearVideoRevision = 0;
quint64 lastConversionRevision = 0;
QtMessageHandler previousDrawHandler = nullptr;
void countLinearVideoSamples(const QString &message) {
    if (message.startsWith(QStringLiteral("F4_VIEWER_LINEAR_CONVERSION "))) {
        ++linearVideoConversions;
        lastConversionRevision = message.section(QLatin1Char(' '), -1).toULongLong();
        return;
    }
    if (!message.startsWith(QStringLiteral("F4_VIEWER_LINEAR_SAMPLE true ")))
        return;
    ++linearVideoSampleDraws;
    lastLinearVideoRevision = message.section(QLatin1Char(' '), -1).toULongLong();
}
void countResampleDraws(QtMsgType type, const QMessageLogContext &context,
                        const QString &message) {
    countLinearVideoSamples(message);
    if (message.startsWith(QStringLiteral("F4_VIEWER_RESAMPLE_DRAW false false ")))
        ++finalResampleDraws;
    if (message.startsWith(QStringLiteral("F4_VIEWER_RESAMPLE_DRAW true ")))
        ++intermediateResampleDraws;
    if (message.startsWith(QStringLiteral("F4_VIEWER_RESAMPLE_DRAW "))
        || message.startsWith(QStringLiteral("F4_VIEWER_LINEAR_SAMPLE "))
        || message.startsWith(QStringLiteral("F4_VIEWER_LINEAR_CONVERSION ")))
        return;
    if (previousDrawHandler)
        previousDrawHandler(type, context, message);
}

class DrawCounter {
public:
    DrawCounter() { previousDrawHandler = qInstallMessageHandler(countResampleDraws); }
    ~DrawCounter() { qInstallMessageHandler(previousDrawHandler); }
    void reset() {
        finalResampleDraws = 0;
        intermediateResampleDraws = 0;
        linearVideoSampleDraws = 0;
        linearVideoConversions = 0;
        lastLinearVideoRevision = 0;
        lastConversionRevision = 0;
    }
    int count() const { return finalResampleDraws; }
    int intermediateCount() const { return intermediateResampleDraws; }
    int linearCount() const { return linearVideoSampleDraws; }
    quint64 linearRevision() const { return lastLinearVideoRevision; }
    int conversionCount() const { return linearVideoConversions; }
    quint64 conversionRevision() const { return lastConversionRevision; }
};

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name), m_previous(qgetenv(name)),
          m_wasSet(qEnvironmentVariableIsSet(name)) {
        if (value.isNull())
            qunsetenv(name);
        else
            qputenv(name, value);
    }
    ~ScopedEnvironmentVariable() {
        if (m_wasSet)
            qputenv(m_name, m_previous);
        else
            qunsetenv(m_name);
    }
    ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
    ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

private:
    const char *m_name;
    QByteArray m_previous;
    bool m_wasSet;
};

// Keep the benchmark's logging bounded even though the draw-trace flag is
// captured on the first shader submission in this test process.
QtMessageHandler previousBenchmarkHandler = nullptr;
void suppressBenchmarkDrawTrace(QtMsgType type, const QMessageLogContext &context,
                                const QString &message) {
    countLinearVideoSamples(message);
    if (message.startsWith(QStringLiteral("F4_VIEWER_RESAMPLE_DRAW false false ")))
        ++finalResampleDraws;
    if (message.startsWith(QStringLiteral("F4_VIEWER_RESAMPLE_DRAW "))
        || message.startsWith(QStringLiteral("F4_VIEWER_LINEAR_SAMPLE "))
        || message.startsWith(QStringLiteral("F4_VIEWER_LINEAR_CONVERSION ")))
        return;
    if (previousBenchmarkHandler)
        previousBenchmarkHandler(type, context, message);
}

class BenchmarkDrawTraceSilencer {
public:
    BenchmarkDrawTraceSilencer() {
        previousBenchmarkHandler = qInstallMessageHandler(suppressBenchmarkDrawTrace);
    }
    ~BenchmarkDrawTraceSilencer() { qInstallMessageHandler(previousBenchmarkHandler); }
    void reset() {
        finalResampleDraws = 0;
        linearVideoSampleDraws = 0;
        linearVideoConversions = 0;
        lastLinearVideoRevision = 0;
        lastConversionRevision = 0;
    }
    int count() const { return finalResampleDraws; }
    int linearCount() const { return linearVideoSampleDraws; }
    quint64 linearRevision() const { return lastLinearVideoRevision; }
    int conversionCount() const { return linearVideoConversions; }
    quint64 conversionRevision() const { return lastConversionRevision; }
};

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

// No native window is exposed. Real ShaderEffect/QSB
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
                          qreal scale, int rotation = 0,
                          qreal renderTargetDpr = 0) {
        return initializeScene(image, outputSize, dpr, {}, {}, true, scale,
                               rotation, renderTargetDpr > 0 ? renderTargetDpr
                                                            : dpr);
    }

    bool initializeScene(const QImage &image, QSize outputSize, qreal dpr,
                         QSize effectPhysicalSize, QPointF effectPhysicalOffset,
                         bool viewer, qreal viewerScale, int rotation,
                         qreal renderTargetDpr = 0) {
        if (renderTargetDpr <= 0)
            renderTargetDpr = dpr;
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
                property alias unrelated: unrelated
                Rectangle { id: unrelated; width: 4; height: 4; z: -1; color: "black"; visible: false }
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
        const QSizeF exactLogicalSize = QSizeF(outputSize) / renderTargetDpr;
        // A real QQuickWindow has integer logical dimensions. Its framebuffer
        // may cover half a physical pixel more than logicalSize * DPR.
        const QSizeF logicalSize = viewer
            ? QSizeF(exactLogicalSize.toSize()) : exactLogicalSize;
        root->setSize(logicalSize);
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
        renderTarget.setDevicePixelRatio(renderTargetDpr);
        window.setRenderTarget(renderTarget);
        // Attach only after the offscreen window has its effective DPR. Real
        // windows already have a backing DPR when their QML scene is attached.
        root->setParentItem(window.contentItem());
        return true;
    }

    QImage render(int frames = 4) {
        QImage result;
        // Allow texture providers and the retained pyramid to settle. Every
        // frame executes actual QSB shaders; a software scene graph cannot pass.
        for (int frame = 0; frame < frames; ++frame) {
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
    bool cacheCompositeIsAligned(qreal dpr) const {
        QList<QQuickItem *> pending{window.contentItem()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (item->objectName() == QStringLiteral("galleryViewerVideoCacheComposite")) {
                const QPointF origin = item->mapToItem(window.contentItem(), QPointF());
                const QPointF end = item->mapToItem(window.contentItem(), {item->width(), item->height()});
                for (const QPointF point : {origin, end}) {
                    if (qAbs(point.x() * dpr - qRound(point.x() * dpr)) >= 0.001
                        || qAbs(point.y() * dpr - qRound(point.y() * dpr)) >= 0.001)
                        return false;
                }
                return item->mapToItem(window.contentItem(), {1, 0}) - origin == QPointF(1, 0)
                    && item->mapToItem(window.contentItem(), {0, 1}) - origin == QPointF(0, 1);
            }
            pending.append(item->childItems());
        }
        return false;
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
    QObject *viewport() const { return root->property("viewport").value<QObject *>(); }
    void setSceneOpacity(qreal opacity) { root->setOpacity(opacity); }
    void setSceneTransform(qreal scale, qreal rotation) {
        root->setTransformOrigin(QQuickItem::TopLeft);
        root->setScale(scale);
        root->setRotation(rotation);
    }
    bool changeUnrelatedSibling() {
        auto *sibling = root->property("unrelated").value<QObject *>();
        return sibling && sibling->setProperty("visible", true)
            && sibling->setProperty("color", QColor(Qt::red));
    }
    bool requestsFrameWhileIdle() {
        QCoreApplication::processEvents();
        QSignalSpy renders(&control, &QQuickRenderControl::renderRequested);
        QSignalSpy changes(&control, &QQuickRenderControl::sceneChanged);
        QTest::qWait(30);
        return !renders.isEmpty() || !changes.isEmpty();
    }
    bool setViewerPhysicalOffset(QPointF offset) {
        return root->setProperty("testEffectPhysicalOffset", offset);
    }
    bool setEffectPhysicalSize(QSize size) {
        return root->setProperty("testEffectPhysicalSize", size);
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
    bool rhiSupportsLinearVideoCache() const {
        return rhi->isTextureFormatSupported(QRhiTexture::RGBA32F, QRhiTexture::RenderTarget)
            && rhi->resourceLimit(QRhi::TextureSizeMax) >= 4096;
    }
    QObject *linearVideoConsumer() const {
        return filtered()->property("linearVideoCacheConsumer").value<QObject *>();
    }
    int maximumTextureSize() const { return rhi->resourceLimit(QRhi::TextureSizeMax); }
    void assertConversionShaderFailure(const QVideoFrame &frame) {
        auto *context = qobject_cast<QSGDefaultRenderContext *>(QQuickWindowPrivate::get(&window)->context);
        QVERIFY(context);
        // An allocated float target is not proof that a conversion was drawn.
        // Inject missing compiled stages and verify no ready texture escapes.
        ViewerLinearVideoTexture invalid(context, rhi, QShader{}, QShader{});
        const QByteArray uniforms(308, '\0');
        invalid.setFrame(frame, 1, 1, uniforms);
        control.polishItems();
        control.beginFrame();
        control.sync();
        const bool failedClosed = !invalid.updateTexture() && invalid.failed()
            && !invalid.readyFor(1, 1, rhi) && !invalid.rhiTexture();
        invalid.setFrame(frame, 1, 2, uniforms);
        const bool stayedDisabled = !invalid.updateTexture() && invalid.failed()
            && !invalid.readyFor(1, 2, rhi) && !invalid.rhiTexture();
        control.render();
        control.endFrame();
        invalid.onFrameEnd();
        QVERIFY(failedClosed);
        QVERIFY(stayedDisabled);
    }
    bool rhiSupportsP010() const {
        return rhi->isTextureFormatSupported(QRhiTexture::R16)
            && rhi->isTextureFormatSupported(QRhiTexture::RG16);
    }
    bool hasLinearVideoTexture(QSize frameSize) const {
        auto *item = qobject_cast<QQuickItem *>(filtered()->property("linearVideoSource").value<QObject *>());
        if (!item || !item->isTextureProvider())
            return false;
        auto *provider = item->textureProvider();
        auto *texture = provider ? provider->texture() : nullptr;
        auto *native = texture ? texture->rhiTexture() : nullptr;
        return native && native->format() == QRhiTexture::RGBA32F
            && native->pixelSize() == frameSize;
    }

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

QImage encodedBilinearReference(const QImage &input, QSize size) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const double px = (x + 0.5) * input.width() / size.width() - 0.5;
            const double py = (y + 0.5) * input.height() / size.height() - 0.5;
            const int ix = int(std::floor(px)), iy = int(std::floor(py));
            const double wx = px - ix, wy = py - iy;
            std::array<double, 4> channels{};
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    const QRgb pixel = input.pixel(std::clamp(ix + col, 0, input.width() - 1),
                                                   std::clamp(iy + row, 0, input.height() - 1));
                    const double weight = (col ? wx : 1 - wx) * (row ? wy : 1 - wy);
                    channels[0] += qRed(pixel) * weight;
                    channels[1] += qGreen(pixel) * weight;
                    channels[2] += qBlue(pixel) * weight;
                    channels[3] += qAlpha(pixel) * weight;
                }
            }
            result.setPixel(x, y, qRgba(qRound(channels[0]), qRound(channels[1]),
                                        qRound(channels[2]), qRound(channels[3])));
        }
    }
    return result;
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
        if (actual.format() == reference.format()
            && std::memcmp(actual.constScanLine(y), reference.constScanLine(y),
                           size_t(actual.width()) * sizeof(QRgb)) == 0)
            continue;
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

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
QVideoFrame syntheticVideoFrame(QSize size, bool yuv, int revision,
                                QtVideo::Rotation rotation = QtVideo::Rotation::None,
                                bool mirrored = false) {
    QVideoFrameFormat format(size, yuv ? QVideoFrameFormat::Format_NV12
                                     : QVideoFrameFormat::Format_BGRA8888);
    if (yuv) {
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
        format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    }
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    const QImage pattern = yuv ? QImage() : periodicImage(size);
    for (int y = 0; y < size.height(); ++y) {
        auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
        for (int x = 0; x < size.width(); ++x) {
            if (yuv) {
                row[x] = 16 + (x * 37 + y * 23 + revision * 91) % 220;
            } else {
                const QRgb pixel = pattern.pixel(x, y);
                row[4*x] = revision ? 255 - qBlue(pixel) : qBlue(pixel);
                row[4*x+1] = revision ? 255 - qGreen(pixel) : qGreen(pixel);
                row[4*x+2] = revision ? 255 - qRed(pixel) : qRed(pixel);
                row[4*x+3] = 255;
            }
        }
    }
    if (yuv) {
        for (int y = 0; y < (size.height() + 1) / 2; ++y) {
            auto *row = frame.bits(1) + y * frame.bytesPerLine(1);
            for (int x = 0; x < (size.width() + 1) / 2; ++x) {
                row[2*x] = 48 + (x * 17 + y * 3 + revision * 47) % 160;
                row[2*x+1] = 48 + (x * 3 + y * 17 + revision * 71) % 160;
            }
        }
    }
    frame.unmap();
    frame.setRotation(rotation);
    frame.setMirrored(mirrored);
    return frame;
}

// A QVideoFrame copy shares its buffer and metadata with the sink's current
// frame. Allocate and copy planes so every submission is independently owned
// and immutable, while retaining the decoded format and color metadata.
QVideoFrame independentVideoFrame(QVideoFrame original, qint64 startTime) {
    QVideoFrame copy(original.surfaceFormat());
    if (!original.map(QVideoFrame::ReadOnly))
        return {};
    if (!copy.map(QVideoFrame::WriteOnly)) {
        original.unmap();
        return {};
    }
    bool valid = copy.planeCount() == original.planeCount();
    for (int plane = 0; valid && plane < original.planeCount(); ++plane) {
        const int rows = plane == 0
            || original.pixelFormat() == QVideoFrameFormat::Format_YUV422P
            ? original.height() : (original.height() + 1) / 2;
        const int rowBytes = std::min(original.bytesPerLine(plane), copy.bytesPerLine(plane));
        valid = rowBytes > 0
            && qint64(rows - 1) * original.bytesPerLine(plane) + rowBytes
                <= original.mappedBytes(plane)
            && qint64(rows - 1) * copy.bytesPerLine(plane) + rowBytes
                <= copy.mappedBytes(plane);
        if (!valid)
            break;
        for (int row = 0; row < rows; ++row)
            std::memcpy(copy.bits(plane) + row * copy.bytesPerLine(plane),
                        original.bits(plane) + row * original.bytesPerLine(plane), rowBytes);
    }
    copy.unmap();
    original.unmap();
    if (!valid)
        return {};
    copy.setRotation(original.rotation());
    copy.setMirrored(original.mirrored());
    copy.setStartTime(startTime);
    copy.setEndTime(startTime + 33333);
    return copy;
}

QVideoFrame conversionInputFrame(QVideoFrameFormat::PixelFormat pixelFormat,
                                 bool cropped, bool alpha, int revision,
                                 QtVideo::Rotation rotation, bool mirrored) {
    const QSize size = cropped ? QSize(96, 80) : QSize(84, 63);
    if (pixelFormat == QVideoFrameFormat::Format_NV12
        || pixelFormat == QVideoFrameFormat::Format_YUV420P) {
        QVideoFrame original = syntheticVideoFrame(size, true, revision);
        QVideoFrameFormat format(size, pixelFormat);
        format.setColorSpace(original.surfaceFormat().colorSpace());
        format.setColorRange(original.surfaceFormat().colorRange());
        format.setColorTransfer(original.surfaceFormat().colorTransfer());
        if (cropped)
            format.setViewport({7, 9, 84, 63});
        // Allocate with the viewport metadata before publishing any frame.
        QVideoFrame frame(format);
        if (!original.map(QVideoFrame::ReadOnly))
            return {};
        if (!frame.map(QVideoFrame::WriteOnly)) {
            original.unmap();
            return {};
        }
        for (int plane = 0; plane < frame.planeCount(); ++plane) {
            const int rows = plane == 0 ? size.height() : (size.height() + 1) / 2;
            const int rowBytes = plane == 0 ? size.width() : ((size.width() + 1) / 2) * 2;
            for (int row = 0; row < rows; ++row) {
                if (pixelFormat == QVideoFrameFormat::Format_YUV420P && plane > 0) {
                    for (int col = 0; col < (size.width() + 1) / 2; ++col)
                        frame.bits(plane)[row * frame.bytesPerLine(plane) + col]
                            = original.bits(1)[row * original.bytesPerLine(1) + 2 * col + plane - 1];
                } else {
                    std::memcpy(frame.bits(plane) + row * frame.bytesPerLine(plane),
                                original.bits(plane) + row * original.bytesPerLine(plane), rowBytes);
                }
            }
        }
        frame.unmap();
        original.unmap();
        frame.setRotation(rotation);
        frame.setMirrored(mirrored);
        return frame;
    }
    QVideoFrameFormat format(size, pixelFormat);
    if (cropped)
        format.setViewport({7, 9, 84, 63});
    const bool tenBit = pixelFormat == QVideoFrameFormat::Format_P010;
    if (tenBit) {
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
        format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    }
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    const QImage pattern = tenBit ? QImage() : periodicImage(size);
    const std::array<int, 5> opacity{0, 32, 96, 160, 255};
    for (int y = 0; y < size.height(); ++y) {
        auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
        for (int x = 0; x < size.width(); ++x) {
            if (tenBit) {
                reinterpret_cast<quint16 *>(row)[x] = quint16((64 + (x * 37 + y * 23 + revision * 365) % 877) << 6);
            } else {
                const QRgb patternPixel = pattern.pixel(x, y);
                QRgb pixel = qRgba(revision ? 255 - qRed(patternPixel) : qRed(patternPixel),
                                  revision ? 255 - qGreen(patternPixel) : qGreen(patternPixel),
                                  revision ? 255 - qBlue(patternPixel) : qBlue(patternPixel),
                                  alpha ? opacity[(x + 3 * y + revision) % opacity.size()] : 255);
                if (pixelFormat == QVideoFrameFormat::Format_BGRA8888_Premultiplied)
                    pixel = qPremultiply(pixel);
                row[4*x] = qBlue(pixel);
                row[4*x+1] = qGreen(pixel);
                row[4*x+2] = qRed(pixel);
                row[4*x+3] = qAlpha(pixel);
            }
        }
    }
    if (tenBit) {
        for (int y = 0; y < (size.height() + 1) / 2; ++y) {
            auto *row = reinterpret_cast<quint16 *>(frame.bits(1) + y * frame.bytesPerLine(1));
            for (int x = 0; x < (size.width() + 1) / 2; ++x) {
                row[2*x] = quint16((192 + (x * 17 + y * 3 + revision * 191) % 641) << 6);
                row[2*x+1] = quint16((192 + (x * 3 + y * 17 + revision * 283) % 641) << 6);
            }
        }
    }
    frame.unmap();
    frame.setRotation(rotation);
    frame.setMirrored(mirrored);
    return frame;
}

struct RenderTimingSummary {
    double medianMs;
    double p95Ms;
    double minimumMs;
};

RenderTimingSummary summarizeRenderTimings(std::vector<qint64> samples) {
    std::sort(samples.begin(), samples.end());
    const size_t middle = samples.size() / 2;
    const double median = samples.size() % 2 ? double(samples[middle])
        : (double(samples[middle - 1]) + double(samples[middle])) / 2;
    const size_t p95 = size_t(std::ceil(samples.size() * 0.95)) - 1;
    return {median / 1000000.0, samples[p95] / 1000000.0,
            samples.front() / 1000000.0};
}
#endif

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

void ViewerResampleGpuTest::fractionalWaylandBackingScalePreservesSettledGeometry() {
    // QWaylandScreen exposes the integer wl_output scale while QQuickWindow
    // renders at the compositor's fractional DPR. The regular scene graph
    // matrix contains the window DPR. A settled-only vertex correction must
    // not replace it with the screen DPR supplied by the viewer host.
    constexpr qreal screenDpr = 2.0;
    constexpr qreal windowDpr = 1.5;
    const QImage input = periodicImage({300, 160});
    GpuFixture fixture;
    QVERIFY2(fixture.initializeViewer(input, {225, 150}, screenDpr, 1, 0,
                                      windowDpr),
             qPrintable(fixture.error));
    QVERIFY(fixture.setViewerMoving(true));
    QCoreApplication::processEvents();

    const QRectF sceneBounds = fixture.sceneBounds();
    const QRect presentationRect(
        QPoint(qRound(sceneBounds.x() * windowDpr),
               qRound(sceneBounds.y() * windowDpr)),
        QSize(qRound(sceneBounds.width() * windowDpr),
              qRound(sceneBounds.height() * windowDpr)));
    QCOMPARE(presentationRect, QRect(0, 15, 225, 120));
    const QImage expected = ZoinGallery::ViewerResampler::downsample(
        input, presentationRect.size());
    const QImage movingFrame = fixture.render();
    QVERIFY2(!movingFrame.isNull(), qPrintable(fixture.error));
    comparePixels(movingFrame.copy(presentationRect), expected, 2);

    QVERIFY(fixture.setViewerMoving(false));
    const QImage settledFrame = fixture.render();
    QVERIFY2(!settledFrame.isNull(), qPrintable(fixture.error));
    qInfo().noquote() << QStringLiteral(
        "[FIX:wayland-backing-scale] screenDpr=%1 windowDpr=%2 "
        "scene=(%3,%4 %5x%6) framebufferRect=(%7,%8 %9x%10)")
        .arg(screenDpr).arg(windowDpr)
        .arg(sceneBounds.x()).arg(sceneBounds.y())
        .arg(sceneBounds.width()).arg(sceneBounds.height())
        .arg(presentationRect.x()).arg(presentationRect.y())
        .arg(presentationRect.width()).arg(presentationRect.height());
    comparePixels(settledFrame.copy(presentationRect), expected, 2);
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

void ViewerResampleGpuTest::flightSamplingUsesHardwareAndRestoresQuality_data() {
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr1") << qreal(1);
    QTest::newRow("dpr175") << qreal(1.75);
}

void ViewerResampleGpuTest::flightSamplingUsesHardwareAndRestoresQuality() {
    QFETCH(qreal, dpr);
    QImage input(112, 84, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < input.height(); ++y)
        for (int x = 0; x < input.width(); ++x)
            input.setPixel(x, y, (x + y) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    GpuFixture fixture;
    QVERIFY2(fixture.initializeViewer(input, {140, 105}, dpr, 0.5), qPrintable(fixture.error));
    const QImage resting = fixture.render();
    QVERIFY(!resting.isNull());
    QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), 1);
    QVERIFY(fixture.viewport()->setProperty("hardwareSampling", true));
    QVERIFY(fixture.setViewerMoving(true));
    DrawCounter draws;
    draws.reset();
    const QImage flying = fixture.render();
    QVERIFY(!flying.isNull());
    QVERIFY(fixture.filtered()->property("hardwareSampling").toBool());
    QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), 0);
    QCOMPARE(draws.intermediateCount(), 0);
    QVERIFY(!fixture.filtered()->property("pixelAligned").toBool());
    const QRectF bounds = fixture.sceneBounds();
    const QPoint center(qRound(bounds.center().x() * dpr), qRound(bounds.center().y() * dpr));
    // Encoded hardware bilinear is intentionally different from the resting
    // linear-light filter, but must interpolate rather than alias to nearest.
    QVERIFY(qRed(resting.pixel(center)) >= 187 && qRed(resting.pixel(center)) <= 188);
    QVERIFY(qRed(flying.pixel(center)) >= 127 && qRed(flying.pixel(center)) <= 128);
    QVERIFY(fixture.viewport()->setProperty("hardwareSampling", false));
    QVERIFY(fixture.setViewerMoving(false));
    comparePixels(fixture.render(), resting, 0);
    QVERIFY(!fixture.filtered()->property("hardwareSampling").toBool());
    const QPointF origin = fixture.scenePoint();
    const QPointF end = fixture.scenePoint({fixture.filteredItem()->width(), fixture.filteredItem()->height()});
    for (const QPointF point : {origin, end}) {
        QVERIFY(qAbs(point.x() * dpr - qRound(point.x() * dpr)) < 0.001);
        QVERIFY(qAbs(point.y() * dpr - qRound(point.y() * dpr)) < 0.001);
    }
    QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
    QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
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
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
void ViewerResampleGpuTest::videoResampleMatchesReference_data() {
    // Preserve the paused-cache matrix and exercise the same filtering paths
    // with fresh revisions instead of reusing a paused presentation.
    pausedVideoReusesPresentation_data();
    for (bool yuv : {false, true}) {
        for (qreal dpr : {qreal(1), qreal(1.75)}) {
            const QByteArray suffix = QByteArray(yuv ? "-nv12" : "-bgra")
                + (dpr == 1 ? "" : "-dpr175");
            QTest::newRow(("below-minification-threshold" + suffix).constData())
                << QSize(82, 62) << QSize(80, 60) << dpr << yuv;
            QTest::newRow(("minification-threshold" + suffix).constData())
                << QSize(84, 63) << QSize(80, 60) << dpr << yuv;
        }
    }
}

void ViewerResampleGpuTest::videoResampleMatchesReference() {
    QFETCH(QSize, sourceSize);
    QFETCH(QSize, targetSize);
    QFETCH(qreal, dpr);
    QFETCH(bool, yuv);
    ScopedEnvironmentVariable enableConversion("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE", QByteArray());
    std::vector<QVideoFrame> frames;
    for (QtVideo::Rotation rotation : {QtVideo::Rotation::None,
                                       QtVideo::Rotation::Clockwise90,
                                       QtVideo::Rotation::Clockwise180,
                                       QtVideo::Rotation::Clockwise270}) {
        for (bool mirrored : {false, true}) {
            for (int revision = 0; revision < 2; ++revision) {
                frames.push_back(syntheticVideoFrame(sourceSize, yuv, revision,
                                                      rotation, mirrored));
                QVERIFY(frames.back().isValid());
                QCOMPARE(frames.back().pixelFormat(), yuv ? QVideoFrameFormat::Format_NV12
                                                         : QVideoFrameFormat::Format_BGRA8888);
            }
        }
    }
    const QVideoFrame warmup = syntheticVideoFrame(sourceSize, yuv, 0);
    QVERIFY(warmup.isValid());
    QImage fallback(sourceSize, QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    std::vector<QImage> reference;
    reference.reserve(frames.size());
    for (const QByteArray &mode : {QByteArray("1"), QByteArray("0"), QByteArray()}) {
        // Shader/material selection happens at construction. Destroy this
        // fixture and its QRhi before changing the mode for the next pass.
        ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", mode);
        ZoinGallery::ViewerVideoFrameSource source;
        GpuFixture fixture;
        DrawCounter draws;
        QVERIFY2(fixture.initialize(fallback, targetSize, dpr), qPrintable(fixture.error));
        QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
        QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", true));
        QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        sink->setVideoFrame(warmup);
        QVERIFY2(!fixture.render().isNull(), qPrintable(fixture.error));
        QVERIFY(fixture.filtered()->property("linearVideoCacheSupported").isValid());
        QVERIFY(fixture.filtered()->property("linearVideoCacheEligible").isValid());
        QVERIFY(fixture.filtered()->property("linearVideoCacheRequested").isValid());
        QCOMPARE(fixture.filtered()->property("referenceSampling").toBool(), mode == "1");
        const bool deviceSupported = fixture.rhiSupportsLinearVideoCache();
        const int expectedLevels = sourceSize.width() >= targetSize.width() * 4
            && sourceSize.height() >= targetSize.height() * 4 ? 2
            : (sourceSize.width() >= targetSize.width() * 2
               && sourceSize.height() >= targetSize.height() * 2 ? 1 : 0);
        QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), expectedLevels);
        const QPointF origin = fixture.scenePoint();
        QVERIFY(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.001);
        QVERIFY(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.001);
        QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
        QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
        QImage previous;
        bool checkedConversionToggle = false;
        for (size_t index = 0; index < frames.size(); ++index) {
            const quint64 before = source.revision();
            sink->setVideoFrame(frames[index]);
            QCOMPARE(source.revision(), before + 1);
            QSize displaySize = sourceSize;
            if (frames[index].rotation() == QtVideo::Rotation::Clockwise90
                || frames[index].rotation() == QtVideo::Rotation::Clockwise270)
                displaySize.transpose();
            QCOMPARE(source.frameSize(), displaySize);
            draws.reset();
            const QImage actual = fixture.render();
            QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
            QCOMPARE(actual.size(), targetSize);
            QCOMPARE(source.revision(), before + 1);
            QVERIFY2(draws.count() > 0, "Fresh video revision must execute the resampling shader");
            const bool presentationEligible = qAbs(qreal(targetSize.width()) / qRound(targetSize.width() / dpr) - dpr) <= 0.01
                && qAbs(qreal(targetSize.height()) / qRound(targetSize.height() / dpr) - dpr) <= 0.01;
            QCOMPARE(fixture.filtered()->property("presentationCacheEligible").toBool(), presentationEligible);
            const bool requested = mode != "1"
                && displaySize.width() <= 4096 && displaySize.height() <= 4096
                && qint64(displaySize.width()) * displaySize.height() <= 9 * 1024 * 1024
                && displaySize.width() >= targetSize.width() * 1.05
                && displaySize.height() >= targetSize.height() * 1.05;
            const bool conversionEligible = requested && deviceSupported;
            QCOMPARE(fixture.filtered()->property("linearVideoCacheRequested").toBool(), requested);
            QVERIFY(fixture.linearVideoConsumer());
            QCOMPARE(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool(), conversionEligible);
            const bool directConsumer = fixture.filtered()->property("requiredLevels").toInt() == 0;
            QCOMPARE(fixture.filtered()->property("requestLinearVideoCache").toBool(), requested && directConsumer);
            QCOMPARE(fixture.linearVideoConsumer()->property("requestLinearVideoCache").toBool(), requested);
            QCOMPARE(fixture.filtered()->property("linearVideoCacheEligible").toBool(), conversionEligible);
            if (conversionEligible) {
                QVERIFY2(fixture.hasLinearVideoTexture(displaySize), "Converter must produce a source-sized RGBA32F texture");
                QVERIFY2(draws.linearCount() > 0, "Eligible optimized render must select the converter, not raw YUV");
                QCOMPARE(draws.linearRevision(), source.revision());
                QCOMPARE(draws.conversionCount(), 1);
                QCOMPARE(draws.conversionRevision(), source.revision());
            } else {
                QCOMPARE(draws.linearCount(), 0);
                QVERIFY(!fixture.filtered()->property("linearVideoSource").value<QObject *>());
            }
            if (index % 2)
                QVERIFY2(actual != previous, "A changed immutable frame must change the rendered pixels");
            if (mode == "1")
                reference.push_back(actual);
            else {
                comparePixels(actual, reference[index], 0);
                if (QTest::currentTestFailed()) {
                    qInfo() << "Video reference mismatch: mode"
                            << (mode.isNull() ? QByteArray("unset") : mode)
                            << "frame" << index << "rotation" << frames[index].rotation()
                            << "mirrored" << frames[index].mirrored();
                    return;
                }
            }
            if (conversionEligible && !checkedConversionToggle) {
                const quint64 revision = source.revision();
                QVERIFY(fixture.filtered()->setProperty("cacheVideoConversion", false));
                draws.reset();
                const QImage raw = fixture.render();
                QVERIFY2(!raw.isNull(), qPrintable(fixture.error));
                QVERIFY(!fixture.filtered()->property("linearVideoCacheEligible").toBool());
                QVERIFY(!fixture.filtered()->property("linearVideoCacheRequested").toBool());
                QVERIFY(!fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool());
                QVERIFY(!fixture.filtered()->property("linearVideoSource").value<QObject *>());
                QVERIFY(draws.count() > 0);
                QCOMPARE(draws.linearCount(), 0);
                QCOMPARE(source.revision(), revision);
                comparePixels(raw, actual, 0);
                QVERIFY(fixture.filtered()->setProperty("cacheVideoConversion", true));
                draws.reset();
                const QImage restored = fixture.render();
                QVERIFY2(!restored.isNull(), qPrintable(fixture.error));
                QVERIFY(fixture.filtered()->property("linearVideoCacheEligible").toBool());
                QVERIFY(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool());
                QVERIFY(fixture.hasLinearVideoTexture(displaySize));
                QVERIFY(draws.linearCount() > 0);
                QCOMPARE(draws.linearRevision(), revision);
                QCOMPARE(source.revision(), revision);
                comparePixels(restored, actual, 0);
                if (QTest::currentTestFailed())
                    return;
                checkedConversionToggle = true;
            }
            previous = actual;
        }
    }
}

void ViewerResampleGpuTest::videoConversionSurvivesPanning_data() {
    QTest::addColumn<bool>("yuv");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<bool>("fullViewer");
    for (bool fullViewer : {false, true}) {
        for (bool yuv : {false, true}) {
            for (qreal dpr : {qreal(1), qreal(1.75)}) {
                const QByteArray row = QByteArray(fullViewer ? "viewer" : "resampler")
                    + (yuv ? "-nv12" : "-bgra") + (dpr == 1 ? "" : "-dpr175");
                QTest::newRow(row.constData()) << yuv << dpr << fullViewer;
            }
        }
    }
}

void ViewerResampleGpuTest::videoConversionSurvivesPanning() {
    QFETCH(bool, yuv);
    QFETCH(qreal, dpr);
    QFETCH(bool, fullViewer);
    ScopedEnvironmentVariable enableConversion("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE", QByteArray());
    const QSize sourceSize = fullViewer ? QSize(96, 70) : QSize(84, 63);
    const QSize targetSize(56, 42);
    const std::array<QVideoFrame, 2> frames{
        syntheticVideoFrame(sourceSize, yuv, 0),
        syntheticVideoFrame(sourceSize, yuv, 1)};
    for (const QVideoFrame &frame : frames)
        QVERIFY(frame.isValid());
    const QImage fallback = periodicImage(sourceSize);
    struct Step {
        const char *name;
        QPointF pan;
        bool moving;
        bool freshFrame;
        qreal opacity;
        qreal scale;
        qreal rotation;
    };
    const QPointF fractionalPan = fullViewer ? QPointF(-0.35, -0.7) : QPointF(0.35, 0.7);
    const QPointF clippedPan = fullViewer ? QPointF(-7.35, -4.7) : QPointF(7.35, -5.7);
    const std::array<Step, 9> steps{{
        {"rest", {}, false, false, 1, 1, 0},
        {"fractional-pan", fractionalPan, true, false, 1, 1, 0},
        {"clipped-pan", clippedPan, true, false, 1, 1, 0},
        {"unchanged-clipped-pan", clippedPan, true, false, 1, 1, 0},
        {"fresh-frame", clippedPan, true, true, 1, 1, 0},
        {"ancestor-opacity", clippedPan, true, false, 0.5, 1, 0},
        {"ancestor-transform", clippedPan, true, false, 1, 0.93, 7},
        {"release-to-rest", {}, false, false, 1, 1, 0},
        {"unchanged-rest", {}, false, false, 1, 1, 0},
    }};
    std::array<QImage, steps.size()> reference;
    for (const QByteArray &mode : {QByteArray("1"), QByteArray("0")}) {
        ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", mode);
        ZoinGallery::ViewerVideoFrameSource source;
        GpuFixture fixture;
        DrawCounter draws;
        const bool initialized = fullViewer
            ? fixture.initializeViewer(fallback, targetSize, dpr, 0.75)
            : fixture.initialize(fallback, targetSize, dpr);
        QVERIFY2(initialized, qPrintable(fixture.error));
        QObject *videoOwner = fullViewer ? fixture.viewport() : fixture.filtered();
        QVERIFY(videoOwner);
        if (fullViewer)
            QVERIFY(videoOwner->setProperty("videoMode", true));
        else
            QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
        QVERIFY(videoOwner->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        QVERIFY2(fixture.rhiSupportsLinearVideoCache(), "Regression requires hardware RGBA32F support");
        QPointer<QObject> converter;
        QPointF restingOrigin;
        QImage previous;
        for (size_t index = 0; index < steps.size(); ++index) {
            const Step &step = steps[index];
            qInfo() << "Panning regression" << mode << step.name;
            draws.reset();
            fixture.setSceneOpacity(step.opacity);
            fixture.setSceneTransform(step.scale, step.rotation);
            if (fullViewer) {
                QVERIFY(fixture.setViewerMoving(step.moving));
                QVERIFY(fixture.setViewerPhysicalPan(step.pan, dpr));
            } else {
                QVERIFY(fixture.filtered()->setProperty("pixelAligned", !step.moving));
                QVERIFY(fixture.setViewerPhysicalOffset(step.pan));
            }
            const quint64 before = source.revision();
            if (index == 0 || step.freshFrame)
                sink->setVideoFrame(frames[step.freshFrame ? 1 : 0]);
            QCOMPARE(source.revision(), before + (index == 0 || step.freshFrame ? 1 : 0));
            const QImage actual = fixture.render();
            QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
            QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), 0);
            QCOMPARE(fixture.filtered()->property("presentationCacheEligible").toBool(), !step.moving);
            if (index == 0)
                restingOrigin = fixture.scenePoint();
            if (index == 1) {
                QVERIFY(fixture.scenePoint() != restingOrigin);
                const QPointF origin = fixture.scenePoint() * dpr;
                QVERIFY(qAbs(origin.x() - qRound(origin.x())) > 0.01);
                QVERIFY(qAbs(origin.y() - qRound(origin.y())) > 0.01);
                QVERIFY(actual != previous);
            }
            if (index == 2 || step.freshFrame)
                QVERIFY(actual != previous);
            if (index == 3 || index == 8)
                comparePixels(actual, previous, 0);
            if (step.rotation != 0)
                QVERIFY(fixture.scenePoint({1, 0}) - fixture.scenePoint() != QPointF(1, 0));
            if (!step.moving) {
                const QPointF origin = fixture.scenePoint();
                const QRectF bounds = fixture.sceneBounds();
                for (qreal coordinate : {origin.x(), origin.y(), bounds.right(), bounds.bottom()})
                    QVERIFY(qAbs(coordinate * dpr - qRound(coordinate * dpr)) < 0.001);
                QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
                QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
                QVERIFY(fixture.cacheCompositeIsAligned(dpr));
            }
            if (mode == "1") {
                reference[index] = actual;
                QCOMPARE(draws.conversionCount(), 0);
                QCOMPARE(draws.linearCount(), 0);
            } else {
                QVERIFY2(fixture.filtered()->property("linearVideoCacheEligible").toBool(),
                    "Panning, opacity and scene transforms must retain source-space conversion");
                if (index == 0)
                    converter = fixture.filtered()->property("linearVideoSource").value<QObject *>();
                QVERIFY(converter);
                QCOMPARE(fixture.filtered()->property("linearVideoSource").value<QObject *>(), converter.data());
                QVERIFY(fixture.hasLinearVideoTexture(sourceSize));
                QCOMPARE(draws.conversionCount(), index == 0 || step.freshFrame ? 1 : 0);
                if (draws.conversionCount())
                    QCOMPARE(draws.conversionRevision(), source.revision());
                if (index != 8) {
                    QVERIFY(draws.linearCount() > 0);
                    QCOMPARE(draws.linearRevision(), source.revision());
                } else {
                    QCOMPARE(draws.count(), 0);
                    QVERIFY(!fixture.requestsFrameWhileIdle());
                }
                comparePixels(actual, reference[index], 0);
            }
            if (QTest::currentTestFailed())
                return;
            previous = actual;
        }
    }
}

void ViewerResampleGpuTest::videoConversionExclusions_data() {
    QTest::addColumn<QString>("exclusion");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<bool>("yuv");
    QTest::addColumn<int>("pyramidLevel");
    for (const char *name : {"nearest", "motion", "fallback", "oversize",
                             "unsupported", "missing-provider", "wrong-format"}) {
        for (bool yuv : {false, true}) {
            for (qreal dpr : {qreal(1), qreal(1.75)}) {
                const QByteArray row = QByteArray(name) + (yuv ? "-nv12" : "-bgra")
                    + (dpr == 1 ? "" : "-dpr175");
                QTest::newRow(row.constData()) << QString::fromLatin1(name) << dpr << yuv << 0;
                if (QByteArray(name) == "unsupported" || QByteArray(name) == "fallback"
                    || QByteArray(name) == "nearest" || QByteArray(name) == "motion") {
                    for (int level = 1; level <= 2; ++level) {
                        const QByteArray pyramid = row + "-pyramid" + QByteArray::number(level);
                        QTest::newRow(pyramid.constData()) << QString::fromLatin1(name) << dpr << yuv << level;
                    }
                }
            }
        }
    }
    QTest::newRow("pixel-budget-nv12-dpr175-pyramid2")
        << QStringLiteral("pixel-budget") << qreal(1.75) << true << 2;
}

void ViewerResampleGpuTest::videoConversionExclusions() {
    QFETCH(QString, exclusion);
    QFETCH(qreal, dpr);
    QFETCH(bool, yuv);
    QFETCH(int, pyramidLevel);
    const bool oversize = exclusion == QStringLiteral("oversize");
    const bool pixelBudget = exclusion == QStringLiteral("pixel-budget");
    const bool invalidProvider = exclusion == QStringLiteral("missing-provider")
        || exclusion == QStringLiteral("wrong-format");
    const QSize sourceSize = oversize ? QSize(4097, 84)
        : (pixelBudget ? QSize(3073, 3072)
           : (pyramidLevel == 1 ? QSize(112, 84)
              : (pyramidLevel == 2 ? QSize(224, 168) : QSize(84, 63))));
    const QSize targetSize = oversize ? QSize(3003, 56)
        : (pixelBudget ? QSize(768, 768) : QSize(56, 42));
    ScopedEnvironmentVariable deviceOverride("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE",
        exclusion == QStringLiteral("unsupported") ? QByteArray("1") : QByteArray());
    std::array<QVideoFrame, 3> frames;
    for (size_t index = 0; index < frames.size(); ++index) {
        frames[index] = syntheticVideoFrame(sourceSize, yuv, int(index % 2));
        QVERIFY(frames[index].isValid());
    }
    QImage fallback(sourceSize, QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    std::array<QImage, 3> reference;
    for (const QByteArray &mode : {QByteArray("1"), QByteArray("0")}) {
        ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", mode);
        ZoinGallery::ViewerVideoFrameSource source;
        GpuFixture fixture;
        DrawCounter draws;
        QVERIFY2(fixture.initialize(fallback, targetSize, dpr), qPrintable(fixture.error));
        if (sourceSize.width() > fixture.maximumTextureSize())
            QSKIP("This RHI cannot upload the oversize source used by this exclusion regression");
        QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
        // Upload the fallback Image too, so the wrong-format case supplies a
        // real RGBA8 provider rather than only an uninitialized texture.
        QVERIFY2(!fixture.render().isNull(), qPrintable(fixture.error));
        QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        const bool deviceSupported = fixture.rhiSupportsLinearVideoCache()
            && exclusion != QStringLiteral("unsupported");
        QPointer<QQuickItem> originalProvider;
        for (size_t phase = 0; phase < frames.size(); ++phase) {
            const bool excluded = phase == 1;
            if (exclusion == QStringLiteral("nearest"))
                QVERIFY(fixture.filtered()->setProperty("nearestNeighbor", excluded));
            if (exclusion == QStringLiteral("motion"))
                QVERIFY(fixture.filtered()->setProperty("pixelAligned", !excluded));
            if (invalidProvider && phase > 0) {
                auto *provider = phase == 2 ? originalProvider.data()
                    : (exclusion == QStringLiteral("wrong-format")
                       ? qobject_cast<QQuickItem *>(fixture.filtered()->property("imageSource").value<QObject *>())
                       : nullptr);
                if (excluded && exclusion == QStringLiteral("wrong-format"))
                    QVERIFY(provider);
                if (phase == 2 && mode != "1" && deviceSupported) {
                    if (exclusion == QStringLiteral("wrong-format"))
                        QVERIFY(!originalProvider); // Latched failure unloads the rejected producer.
                    else
                        QVERIFY(originalProvider);
                }
                QVERIFY(fixture.filtered()->setProperty("linearVideoSource", QVariant::fromValue(provider)));
            }
            const quint64 before = source.revision();
            sink->setVideoFrame(excluded && exclusion == QStringLiteral("fallback")
                ? QVideoFrame() : frames[phase]);
            QCOMPARE(source.revision(), before + 1);
            draws.reset();
            const QImage actual = fixture.render();
            QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
            QCOMPARE(source.revision(), before + 1);
            const bool requested = mode != "1" && !oversize && !pixelBudget
                && (!excluded || invalidProvider || exclusion == QStringLiteral("unsupported")
                    || exclusion == QStringLiteral("motion"));
            // A rejected allocation/provider remains disabled for this size
            // and RHI, even when another frame revision arrives. Missing input
            // alone is transient and can restore the already admitted producer.
            const bool eligible = requested && deviceSupported
                && !(exclusion == QStringLiteral("wrong-format") && phase > 0);
            QCOMPARE(fixture.filtered()->property("linearVideoCacheRequested").toBool(), requested);
            QVERIFY(fixture.linearVideoConsumer());
            QCOMPARE(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool(), eligible);
            QCOMPARE(fixture.filtered()->property("linearVideoCacheEligible").toBool(), eligible);
            const bool selected = eligible && !(excluded && invalidProvider);
            if (selected) {
                QVERIFY(fixture.hasLinearVideoTexture(source.frameSize()));
                QVERIFY2(draws.linearCount() > 0, "Eligible minification must use the converter, including motion and pyramids");
                QCOMPARE(draws.linearRevision(), source.revision());
            } else {
                QCOMPARE(draws.linearCount(), 0);
                if (excluded && invalidProvider)
                    QVERIFY(!fixture.hasLinearVideoTexture(source.frameSize()));
            }
            if (phase == 0)
                originalProvider = qobject_cast<QQuickItem *>(fixture.filtered()->property("linearVideoSource").value<QObject *>());
            if (excluded && exclusion == QStringLiteral("fallback")) {
                QVERIFY(!source.hasFrame());
                QVERIFY(!fixture.filtered()->property("videoSource").value<QObject *>());
                QImage expected(targetSize, QImage::Format_ARGB32_Premultiplied);
                expected.fill(Qt::magenta);
                comparePixels(actual, expected, 0);
            }
            if (mode == "1")
                reference[phase] = actual;
            else
                comparePixels(actual, reference[phase], 0);
            if (QTest::currentTestFailed())
                return;
        }
    }
}

void ViewerResampleGpuTest::videoConversionInputs_data() {
    QTest::addColumn<int>("pixelFormat");
    QTest::addColumn<bool>("cropped");
    QTest::addColumn<bool>("alpha");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<int>("rotation");
    QTest::addColumn<bool>("mirrored");
    QTest::addColumn<QSize>("targetSize");
    struct Input { const char *name; QVideoFrameFormat::PixelFormat format; bool cropped; bool alpha; };
    const QList<Input> inputs{
        {"bgra-alpha", QVideoFrameFormat::Format_BGRA8888, false, true},
        {"bgra-premultiplied-alpha", QVideoFrameFormat::Format_BGRA8888_Premultiplied, false, true},
        {"bgra-crop", QVideoFrameFormat::Format_BGRA8888, true, false},
        {"nv12-crop", QVideoFrameFormat::Format_NV12, true, false},
        {"yuv420p-crop", QVideoFrameFormat::Format_YUV420P, true, false},
        {"p010", QVideoFrameFormat::Format_P010, false, false},
        {"p010-crop", QVideoFrameFormat::Format_P010, true, false},
    };
    for (const Input &input : inputs) {
        for (qreal dpr : {qreal(1), qreal(1.75)}) {
            for (int rotation : {0, 90, 180, 270}) {
                for (bool mirrored : {false, true}) {
                    const QByteArray row = QByteArray(input.name) + "-r" + QByteArray::number(rotation)
                        + (mirrored ? "-mirror" : "") + (dpr == 1 ? "" : "-dpr175");
                    for (int level = 0; level <= 2; ++level) {
                        const QByteArray name = row + (level ? "-pyramid" + QByteArray::number(level) : "");
                        const QSize target = level == 0 ? QSize(56, 42)
                            : (level == 1 ? QSize(28, 21) : QSize(14, 10));
                        QTest::newRow(name.constData()) << int(input.format) << input.cropped << input.alpha
                            << dpr << rotation << mirrored << target;
                    }
                }
            }
        }
    }
}

void ViewerResampleGpuTest::videoConversionInputs() {
    QFETCH(int, pixelFormat);
    QFETCH(bool, cropped);
    QFETCH(bool, alpha);
    QFETCH(qreal, dpr);
    QFETCH(int, rotation);
    QFETCH(bool, mirrored);
    QFETCH(QSize, targetSize);
    ScopedEnvironmentVariable enableConversion("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE", QByteArray());
    std::array<QVideoFrame, 3> frames;
    for (size_t index = 0; index < frames.size(); ++index) {
        frames[index] = conversionInputFrame(static_cast<QVideoFrameFormat::PixelFormat>(pixelFormat),
            cropped, alpha, int(index % 2), static_cast<QtVideo::Rotation>(rotation), mirrored);
        QVERIFY(frames[index].isValid());
        QCOMPARE(int(frames[index].pixelFormat()), pixelFormat);
        if (cropped)
            QCOMPARE(frames[index].surfaceFormat().viewport(), QRect(7, 9, 84, 63));
    }
    QSize displaySize(84, 63);
    if (rotation == 90 || rotation == 270)
        displaySize.transpose();
    QImage fallback(frames.front().size(), QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    std::array<QImage, 2> reference;
    for (const QByteArray &mode : {QByteArray("1"), QByteArray("0"), QByteArray()}) {
        ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", mode);
        ZoinGallery::ViewerVideoFrameSource source;
        GpuFixture fixture;
        DrawCounter draws;
        QVERIFY2(fixture.initialize(fallback, targetSize, dpr), qPrintable(fixture.error));
        if (pixelFormat == int(QVideoFrameFormat::Format_P010) && !fixture.rhiSupportsP010())
            QSKIP("This RHI lacks the normalized 16-bit plane formats used by P010");
        QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
        QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", true));
        QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        sink->setVideoFrame(frames.front());
        QVERIFY2(!fixture.render().isNull(), qPrintable(fixture.error));
        const bool eligible = mode != "1" && fixture.rhiSupportsLinearVideoCache();
        QCOMPARE(fixture.filtered()->property("linearVideoCacheRequested").toBool(), mode != "1");
        QVERIFY(fixture.linearVideoConsumer());
        QCOMPARE(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool(), eligible);
        const QPointF origin = fixture.scenePoint();
        QVERIFY(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.001);
        QVERIFY(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.001);
        QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
        QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
        QImage previous;
        for (size_t index = 1; index < frames.size(); ++index) {
            const quint64 before = source.revision();
            sink->setVideoFrame(frames[index]);
            QCOMPARE(source.revision(), before + 1);
            draws.reset();
            const QImage actual = fixture.render();
            QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
            QCOMPARE(source.frameSize(), displaySize);
            QCOMPARE(source.revision(), before + 1);
            QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(),
                targetSize == QSize(56, 42) ? 0 : (targetSize == QSize(28, 21) ? 1 : 2));
            QCOMPARE(fixture.filtered()->property("linearVideoCacheEligible").toBool(), eligible);
            QVERIFY(draws.count() > 0);
            if (eligible) {
                QVERIFY(fixture.hasLinearVideoTexture(displaySize));
                QVERIFY2(draws.linearCount() > 0, "Input comparison must exercise converted sampling");
                QCOMPARE(draws.linearRevision(), source.revision());
                QCOMPARE(draws.conversionCount(), 1);
                QCOMPARE(draws.conversionRevision(), source.revision());
            } else {
                QCOMPARE(draws.linearCount(), 0);
            }
            if (!previous.isNull())
                QVERIFY(actual != previous);
            if (mode == "1")
                reference[index - 1] = actual;
            else
                comparePixels(actual, reference[index - 1], 0);
            if (eligible && index == 1) {
                QVERIFY(fixture.filtered()->setProperty("cacheVideoConversion", false));
                draws.reset();
                const QImage raw = fixture.render();
                QVERIFY2(!raw.isNull(), qPrintable(fixture.error));
                QVERIFY(!fixture.filtered()->property("linearVideoCacheEligible").toBool());
                QVERIFY(!fixture.filtered()->property("linearVideoCacheRequested").toBool());
                QVERIFY(!fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool());
                QCOMPARE(draws.linearCount(), 0);
                comparePixels(raw, actual, 0);
                QVERIFY(fixture.filtered()->setProperty("cacheVideoConversion", true));
                draws.reset();
                const QImage restored = fixture.render();
                QVERIFY2(!restored.isNull(), qPrintable(fixture.error));
                QVERIFY(fixture.filtered()->property("linearVideoCacheEligible").toBool());
                QVERIFY(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool());
                QVERIFY(draws.linearCount() > 0);
                QCOMPARE(draws.linearRevision(), source.revision());
                comparePixels(restored, actual, 0);
                QCOMPARE(source.revision(), before + 1);
            }
            if (QTest::currentTestFailed())
                return;
            previous = actual;
        }
    }
}

void ViewerResampleGpuTest::conversionShaderFailureNeverPublishesTexture() {
    QImage fallback(QSize(84, 63), QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(fallback, QSize(56, 42), 1.75), qPrintable(fixture.error));
    if (!fixture.rhiSupportsLinearVideoCache())
        QSKIP("This RHI cannot allocate the full-precision conversion target");
    fixture.assertConversionShaderFailure(syntheticVideoFrame(QSize(84, 63), true, 0));
    QVERIFY(!QTest::currentTestFailed());
    const QImage actual = fixture.render();
    QImage expected(QSize(56, 42), QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::magenta);
    comparePixels(actual, expected, 0);
}

void ViewerResampleGpuTest::videoConversionSurvivesPyramidResize_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<QSize>("framebufferSize");
    QTest::addColumn<QList<QSize>>("sizes");
    QTest::newRow("odd-nv12-dpr175") << QSize(229, 175) << QSize(140, 105)
        << QList<QSize>{{115, 88}, {114, 87}, {57, 43}, {58, 44}, {115, 88}};
    // A single 4K RGBA32F conversion nearly fills the shared RHI budget.
    // Both consumers must hand off admission, not reserve it concurrently.
    QTest::newRow("4k-nv12-budget-handoff-dpr175") << QSize(3840, 2160)
        << QSize(2560, 1440)
        << QList<QSize>{{2560, 1440}, {1538, 866}, {960, 540},
                        {1538, 866}, {2560, 1440}};
}

void ViewerResampleGpuTest::videoConversionSurvivesPyramidResize() {
    QFETCH(QSize, sourceSize);
    QFETCH(QSize, framebufferSize);
    QFETCH(QList<QSize>, sizes);
    ScopedEnvironmentVariable enableConversion("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE", QByteArray());
    const qreal dpr = 1.75;
    const QList<int> levels{0, 1, 2, 1, 0};
    QCOMPARE(sizes.size(), levels.size());
    const std::array<QVideoFrame, 2> frames{
        syntheticVideoFrame(sourceSize, true, 0),
        syntheticVideoFrame(sourceSize, true, 1)};
    QVERIFY(frames.front().isValid());
    QVERIFY(frames.back().isValid());
    QImage fallback(sourceSize, QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    QList<QImage> reference;
    for (const QByteArray &mode : {QByteArray("1"), QByteArray("0")}) {
        ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", mode);
        ZoinGallery::ViewerVideoFrameSource source;
        GpuFixture fixture;
        DrawCounter draws;
        QVERIFY2(fixture.initialize(fallback, framebufferSize, dpr, sizes.front()),
                 qPrintable(fixture.error));
        QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
        QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        sink->setVideoFrame(frames.front());
        const bool eligible = mode != "1" && fixture.rhiSupportsLinearVideoCache();
        QPointer<QObject> converter;
        for (qsizetype index = 0; index <= sizes.size(); ++index) {
            const bool fresh = index == sizes.size();
            const qsizetype geometryIndex = fresh ? sizes.size() - 1 : index;
            QVERIFY(fixture.setEffectPhysicalSize(sizes[geometryIndex]));
            if (fresh)
                sink->setVideoFrame(frames.back());
            const quint64 revision = source.revision();
            draws.reset();
            const QImage actual = fixture.render(6);
            QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
            qInfo() << "Pyramid resize" << index << sizes[geometryIndex]
                    << "levels" << fixture.filtered()->property("requiredLevels")
                    << "conversions" << draws.conversionCount();
            QCOMPARE(source.revision(), revision);
            QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), levels[geometryIndex]);
            QCOMPARE(fixture.filtered()->property("linearVideoCacheEligible").toBool(), eligible);
            const QPointF origin = fixture.scenePoint();
            const QRectF bounds = fixture.sceneBounds();
            for (qreal coordinate : {origin.x(), origin.y(), bounds.right(), bounds.bottom()})
                QVERIFY(qAbs(coordinate * dpr - qRound(coordinate * dpr)) < 0.001);
            QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
            QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
            if (mode == "1") {
                reference.append(actual);
                QCOMPARE(draws.conversionCount(), 0);
                QCOMPARE(draws.linearCount(), 0);
            } else {
                comparePixels(actual, reference[index], 0);
                if (eligible) {
                    QVERIFY(fixture.linearVideoConsumer());
                    QVERIFY(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool());
                    QVERIFY(fixture.hasLinearVideoTexture(sourceSize));
                    auto *current = fixture.filtered()->property("linearVideoSource").value<QObject *>();
                    QVERIFY(current);
                    const bool handoff = index > 0 && !fresh
                        && (levels[index] == 0 || levels[index - 1] == 0);
                    // Increasing the Repeater's retained integer model rebuilds
                    // its delegates, including the first admitting consumer.
                    const bool grows = index > 0 && !fresh
                        && levels[index] > *std::max_element(levels.cbegin(), levels.cbegin() + index);
                    if (index == 0 || handoff || grows) {
                        converter = current;
                    } else {
                        QVERIFY(converter);
                        QCOMPARE(current, converter.data());
                    }
                    // Changing the admitting consumer may rebuild its producer
                    // once; an unchanged pass and paused rendering never do.
                    QCOMPARE(draws.conversionCount(), index == 0 || fresh || handoff || grows ? 1 : 0);
                    if (draws.conversionCount())
                        QCOMPARE(draws.conversionRevision(), revision);
                }
            }
            if (QTest::currentTestFailed())
                return;
            draws.reset();
            comparePixels(fixture.render(), actual, 0);
            QCOMPARE(draws.conversionCount(), 0);
            QCOMPARE(draws.intermediateCount(), 0);
            QVERIFY(!fixture.requestsFrameWhileIdle());
        }
    }
}

void ViewerResampleGpuTest::pausedVideoReusesPresentation_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<QSize>("targetSize");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<bool>("yuv");
    for (bool yuv : {false, true}) {
        for (qreal dpr : {qreal(1), qreal(1.75)}) {
            const QByteArray suffix = QByteArray(yuv ? "-nv12" : "-bgra")
                + (dpr == 1 ? "" : "-dpr175");
            QTest::newRow(("direct" + suffix).constData())
                << QSize(84, 63) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("pyramid" + suffix).constData())
                << QSize(112, 84) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("quarter" + suffix).constData())
                << QSize(224, 168) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("odd-half" + suffix).constData())
                << QSize(115, 87) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("odd-quarter" + suffix).constData())
                << QSize(229, 175) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("odd-half-output" + suffix).constData())
                << QSize(127, 99) << QSize(63, 49) << dpr << yuv;
            QTest::newRow(("odd-quarter-output" + suffix).constData())
                << QSize(255, 199) << QSize(63, 49) << dpr << yuv;
            QTest::newRow(("native" + suffix).constData())
                << QSize(56, 42) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("magnified" + suffix).constData())
                << QSize(28, 21) << QSize(56, 42) << dpr << yuv;
            QTest::newRow(("odd" + suffix).constData())
                << QSize(79, 61) << QSize(57, 43) << dpr << yuv;
        }
    }
}

void ViewerResampleGpuTest::pausedVideoReusesPresentation() {
    QFETCH(QSize, sourceSize);
    QFETCH(QSize, targetSize);
    QFETCH(qreal, dpr);
    QFETCH(bool, yuv);
    ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", QByteArray("0"));
    ScopedEnvironmentVariable enableConversion("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE", QByteArray());
    ZoinGallery::ViewerVideoFrameSource source;
    QImage fallback(sourceSize, QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(fallback, targetSize, dpr), qPrintable(fixture.error));
    QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
    QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
        QVariant::fromValue(static_cast<QObject *>(&source))));
    auto *sink = qobject_cast<QVideoSink *>(source.sink());
    QVERIFY(sink);
    QVideoFrame frame;
    if (yuv) {
        QVideoFrameFormat format(sourceSize, QVideoFrameFormat::Format_NV12);
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
        format.setColorRange(QVideoFrameFormat::ColorRange_Video);
        frame = QVideoFrame(format);
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < sourceSize.height(); ++y) {
            auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
            for (int x = 0; x < sourceSize.width(); ++x)
                row[x] = 16 + (x * 37 + y * 23) % 220;
        }
        for (int y = 0; y < (sourceSize.height() + 1) / 2; ++y) {
            auto *row = frame.bits(1) + y * frame.bytesPerLine(1);
            for (int x = 0; x < (sourceSize.width() + 1) / 2; ++x) {
                row[2*x] = 48 + (x * 17 + y * 3) % 160;
                row[2*x+1] = 48 + (x * 3 + y * 17) % 160;
            }
        }
        frame.unmap();
    } else {
        frame = QVideoFrame(periodicImage(sourceSize).convertToFormat(QImage::Format_ARGB32));
    }
    sink->setVideoFrame(frame);
    QVERIFY(source.hasFrame());
    DrawCounter draws;
    draws.reset();
    const QImage first = fixture.render();
    QVERIFY(!first.isNull());
    qInfo() << "Presentation cache" << fixture.filtered()->property("presentationDpr")
            << fixture.filtered()->property("presentationCacheEligible")
            << fixture.filteredItem()->size() << fixture.scenePoint();
    QVERIFY(draws.count() > 0);
    if (fixture.filtered()->property("requiredLevels").toInt() > 0
        && fixture.rhiSupportsLinearVideoCache()) {
        QCOMPARE(draws.conversionCount(), 1);
        QCOMPARE(draws.conversionRevision(), source.revision());
        QVERIFY2(draws.linearCount() > 0,
                 "The first video pyramid pass must sample the full-precision converter");
        QCOMPARE(draws.linearRevision(), source.revision());
        QVERIFY(fixture.hasLinearVideoTexture(source.frameSize()));
    }
    const quint64 revision = source.revision();
    draws.reset();
    QVERIFY(fixture.changeUnrelatedSibling());
    const QImage repeated = fixture.render();
    QCOMPARE(source.revision(), revision);
    comparePixels(repeated, first, 0);
    const bool projectionMatches = qAbs(qreal(targetSize.width()) / qRound(targetSize.width() / dpr) - dpr) <= 0.01
        && qAbs(qreal(targetSize.height()) / qRound(targetSize.height() / dpr) - dpr) <= 0.01;
    QCOMPARE(fixture.filtered()->property("presentationCacheEligible").toBool(), projectionMatches);
    if (projectionMatches)
        QVERIFY(fixture.cacheCompositeIsAligned(dpr));
    QCOMPARE(draws.count(), projectionMatches ? 0 : 4);
    QCOMPARE(draws.intermediateCount(), 0);
    QCOMPARE(draws.conversionCount(), 0);
    QVERIFY(!fixture.requestsFrameWhileIdle());
    const QPointF origin = fixture.scenePoint();
    for (const QPointF point : {origin, fixture.scenePoint({fixture.filteredItem()->width(),
                                                           fixture.filteredItem()->height()})}) {
        QVERIFY(qAbs(point.x() * dpr - qRound(point.x() * dpr)) < 0.001);
        QVERIFY(qAbs(point.y() * dpr - qRound(point.y() * dpr)) < 0.001);
    }
    QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
    QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
    QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", false));
    const QImage direct = fixture.render();
    comparePixels(first, direct, 0);
    QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", true));
    comparePixels(fixture.render(), direct, 0);
    for (QtVideo::Rotation rotation : {QtVideo::Rotation::Clockwise90,
                                       QtVideo::Rotation::Clockwise180,
                                       QtVideo::Rotation::Clockwise270}) {
        // A decoded frame is immutable after delivery. Mutating a shallow copy
        // also mutates the sink's current frame and suppresses its notification.
        QVideoFrame orientedFrame(frame.surfaceFormat());
        QVERIFY(frame.map(QVideoFrame::ReadOnly));
        QVERIFY(orientedFrame.map(QVideoFrame::WriteOnly));
        QCOMPARE(orientedFrame.planeCount(), frame.planeCount());
        for (int plane = 0; plane < frame.planeCount(); ++plane) {
            const int rows = plane == 0 ? sourceSize.height() : (sourceSize.height() + 1) / 2;
            const int rowBytes = plane == 0 ? sourceSize.width() * (yuv ? 1 : 4)
                                           : ((sourceSize.width() + 1) / 2) * 2;
            QVERIFY(frame.bytesPerLine(plane) >= rowBytes);
            QVERIFY(orientedFrame.bytesPerLine(plane) >= rowBytes);
            for (int row = 0; row < rows; ++row)
                std::memcpy(orientedFrame.bits(plane) + row * orientedFrame.bytesPerLine(plane),
                            frame.bits(plane) + row * frame.bytesPerLine(plane), rowBytes);
        }
        orientedFrame.unmap();
        frame.unmap();
        orientedFrame.setRotation(rotation);
        orientedFrame.setMirrored(true);
        const quint64 before = source.revision();
        sink->setVideoFrame(orientedFrame);
        QVERIFY(source.revision() > before);
        const QImage oriented = fixture.render();
        fixture.filtered()->setProperty("cacheVideoPresentation", false);
        comparePixels(fixture.render(), oriented, 0);
        fixture.filtered()->setProperty("cacheVideoPresentation", true);
        comparePixels(fixture.render(), oriented, 0);
    }
    // A new same-size frame must invalidate once; retained pyramid textures
    // must propagate the change without an unconditional redraw loop.
    QImage replacement(sourceSize, QImage::Format_ARGB32);
    replacement.fill(Qt::cyan);
    sink->setVideoFrame(QVideoFrame(replacement));
    draws.reset();
    const QImage changed = fixture.render();
    QVERIFY(draws.count() > 0);
    QVERIFY(changed != first);
    draws.reset();
    comparePixels(fixture.render(), changed, 0);
    QCOMPARE(draws.count(), projectionMatches ? 0 : 4);
    QCOMPARE(draws.intermediateCount(), 0);
    QCOMPARE(draws.conversionCount(), 0);
    QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", false));
    comparePixels(fixture.render(), changed, 0);
    QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", true));
    fixture.render();
    for (qreal opacity : {qreal(0.5), qreal(0)}) {
        fixture.setSceneOpacity(opacity);
        fixture.render();
        QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
    }
    fixture.setSceneOpacity(1);
    fixture.filtered()->setProperty("pixelAligned", false);
    fixture.render();
    QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
    fixture.filtered()->setProperty("pixelAligned", true);
    const QImage restored = fixture.render();
    comparePixels(restored, changed, 0);
    fixture.filtered()->setProperty("nearestNeighbor", true);
    const QImage nearest = fixture.render();
    fixture.filtered()->setProperty("cacheVideoPresentation", false);
    comparePixels(fixture.render(), nearest, 0);
    fixture.filtered()->setProperty("cacheVideoPresentation", true);
    comparePixels(fixture.render(), nearest, 0);
    fixture.filteredItem()->setWidth(5000 / dpr);
    fixture.filtered()->setProperty("viewportSize", QSizeF(5000, targetSize.height()));
    QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
    fixture.filteredItem()->setWidth(targetSize.width() / dpr);
    fixture.filtered()->setProperty("viewportSize", QSizeF(targetSize));
    comparePixels(fixture.render(), nearest, 0);
    sink->setVideoFrame(QVideoFrame());
    fixture.render();
    QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
    if (dpr == 1.75 && yuv && sourceSize == QSize(84, 63)) {
        const QString capture = qEnvironmentVariable("ZOIN_VIDEO_GPU_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(first.save(capture + "-cache175.png"));
    }
}

void ViewerResampleGpuTest::viewerVideoCacheEligibility() {
    const QSize sourceSize(84, 63);
    QImage input = periodicImage(sourceSize);
    ZoinGallery::ViewerVideoFrameSource source;
    auto *sink = qobject_cast<QVideoSink *>(source.sink());
    sink->setVideoFrame(QVideoFrame(input));
    for (qreal backingDpr : {qreal(1.75), qreal(2)}) {
        GpuFixture fixture;
        QVERIFY2(fixture.initializeViewer(input, {140, 105}, 1.75, 1, 0, backingDpr),
                 qPrintable(fixture.error));
        auto *viewport = fixture.viewport();
        QVERIFY(viewport);
        QVERIFY(viewport->setProperty("videoMode", true));
        QVERIFY(viewport->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        const QImage cached = fixture.render();
        const bool eligible = fixture.filtered()->property("presentationCacheEligible").toBool();
        QCOMPARE(eligible, backingDpr == 1.75);
        if (eligible)
            QVERIFY(fixture.cacheCompositeIsAligned(backingDpr));
        QVERIFY(fixture.filtered()->setProperty("cacheVideoPresentation", false));
        comparePixels(fixture.render(), cached, 0);
        fixture.filtered()->setProperty("cacheVideoPresentation", true);
        fixture.render();
        if (!eligible)
            continue;
        fixture.filteredItem()->setVisible(false);
        QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
        fixture.render();
        fixture.filteredItem()->setVisible(true);
        comparePixels(fixture.render(), cached, 0);
        const QPointF origin = fixture.scenePoint();
        QVERIFY(qAbs(origin.x() * backingDpr - qRound(origin.x() * backingDpr)) < 0.001);
        QVERIFY(qAbs(origin.y() * backingDpr - qRound(origin.y() * backingDpr)) < 0.001);
        QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
        QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
        DrawCounter draws;
        draws.reset();
        comparePixels(fixture.render(), cached, 0);
        QCOMPARE(draws.count(), 0);
        fixture.setViewerMoving(true);
        fixture.render();
        QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
        fixture.setViewerMoving(false);
        comparePixels(fixture.render(), cached, 0);
        fixture.setViewerPhysicalOffset({0.35, 0.7});
        const QImage translated = fixture.render();
        fixture.filtered()->setProperty("cacheVideoPresentation", false);
        comparePixels(fixture.render(), translated, 0);
        const QString capture = qEnvironmentVariable("ZOIN_VIDEO_GPU_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(cached.save(capture + "-viewer-cache175.png"));
    }
}

void ViewerResampleGpuTest::videoFlightSamplingRestoresQuality_data() {
    QTest::addColumn<int>("format");
    QTest::addColumn<int>("rotation");
    QTest::addColumn<bool>("mirrored");
    QTest::addColumn<qreal>("dpr");
    for (auto format : {QVideoFrameFormat::Format_BGRA8888_Premultiplied,
                        QVideoFrameFormat::Format_NV12, QVideoFrameFormat::Format_YUV420P,
                        QVideoFrameFormat::Format_P010}) {
        for (int rotation : {0, 90, 180, 270}) {
            for (bool mirrored : {false, true}) {
                for (qreal dpr : {qreal(1), qreal(1.75)}) {
                    const QByteArray row = QByteArray::number(format) + "-r" + QByteArray::number(rotation)
                        + (mirrored ? "-mirror" : "") + (dpr == 1 ? "" : "-dpr175");
                    QTest::newRow(row.constData()) << int(format) << rotation << mirrored << dpr;
                }
            }
        }
    }
}

void ViewerResampleGpuTest::videoFlightSamplingRestoresQuality() {
    QFETCH(int, format);
    QFETCH(int, rotation);
    QFETCH(bool, mirrored);
    QFETCH(qreal, dpr);
    const auto pixelFormat = static_cast<QVideoFrameFormat::PixelFormat>(format);
    const QVideoFrame frame = conversionInputFrame(pixelFormat, true,
        pixelFormat == QVideoFrameFormat::Format_BGRA8888_Premultiplied,
        0, static_cast<QtVideo::Rotation>(rotation), mirrored);
    QVERIFY(frame.isValid());
    ZoinGallery::ViewerVideoFrameSource source;
    auto *sink = qobject_cast<QVideoSink *>(source.sink());
    QVERIFY(sink);
    sink->setVideoFrame(frame);
    const QSize nativeSize = source.frameSize();
    const QSize smallSize = (QSizeF(nativeSize) * 0.37).toSize();
    QImage fallback(nativeSize, QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(fallback, nativeSize, dpr), qPrintable(fixture.error));
    if (pixelFormat == QVideoFrameFormat::Format_P010 && !fixture.rhiSupportsP010())
        QSKIP("RHI lacks native P010 plane textures");
    QVERIFY(fixture.filtered()->setProperty("videoFrameSource", QVariant::fromValue(static_cast<QObject *>(&source))));
    QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
    const QImage native = fixture.render();
    QVERIFY(fixture.filtered()->setProperty("hardwareSampling", true));
    // Continuous video coordinates must retain crop, orientation and alpha at
    // pixel centers before testing the cheaper interpolated reduction.
    comparePixels(fixture.render(), native, 0);
    QVERIFY(fixture.setEffectPhysicalSize(smallSize));
    QVERIFY(fixture.filtered()->setProperty("pixelAligned", false));
    DrawCounter draws;
    draws.reset();
    const QImage flying = fixture.render();
    QVERIFY(!flying.isNull());
    if (pixelFormat == QVideoFrameFormat::Format_BGRA8888_Premultiplied)
        comparePixels(flying.copy(QRect(QPoint(), smallSize)), encodedBilinearReference(native, smallSize), 1);
    QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), 0);
    QVERIFY(!fixture.filtered()->property("linearVideoCacheRequested").toBool());
    QVERIFY(!fixture.filtered()->property("presentationCacheEligible").toBool());
    QCOMPARE(draws.conversionCount(), 0);
    QCOMPARE(draws.linearCount(), 0);
    QVERIFY(draws.count() > 0);
    QVERIFY(flying.pixelColor(smallSize.width() / 2, smallSize.height() / 2) != QColor(Qt::magenta));
    QVERIFY(fixture.filtered()->setProperty("hardwareSampling", false));
    QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
    const QImage quality = fixture.render();
    QVERIFY(fixture.filtered()->property("requiredLevels").toInt() > 0);
    QVERIFY(fixture.filtered()->setProperty("hardwareSampling", true));
    sink->setVideoFrame(frame); // A new revision must not refresh retained pyramid levels.
    draws.reset();
    QVERIFY(!fixture.render().isNull());
    QCOMPARE(draws.intermediateCount(), 0);
    QCOMPARE(draws.conversionCount(), 0);
    QVERIFY(fixture.filtered()->setProperty("hardwareSampling", false));
    comparePixels(fixture.render(), quality, 0);
    QVERIFY(fixture.setEffectPhysicalSize(nativeSize));
    comparePixels(fixture.render(), native, 0);
}

void ViewerResampleGpuTest::videoFramesRenderAndUpdate_data() {
    QTest::addColumn<QSize>("sourceSize");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("direct") << QSize(56, 42) << qreal(1);
    QTest::newRow("direct-dpr175") << QSize(56, 42) << qreal(1.75);
    QTest::newRow("pyramid") << QSize(112, 84) << qreal(1);
    QTest::newRow("pyramid-dpr175") << QSize(112, 84) << qreal(1.75);
}

void ViewerResampleGpuTest::videoFramesRenderAndUpdate() {
    QFETCH(QSize, sourceSize);
    QFETCH(qreal, dpr);
    const QSize targetSize(56, 42);
    ZoinGallery::ViewerVideoFrameSource source;
    QImage fallback(sourceSize, QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(fallback, targetSize, dpr), qPrintable(fixture.error));
    QImage expectedFallback(targetSize, QImage::Format_ARGB32);
    expectedFallback.fill(Qt::magenta);
    comparePixels(fixture.render(), expectedFallback, 0);
    const QPointF origin = fixture.scenePoint();
    QVERIFY(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.001);
    QVERIFY(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.001);
    QCOMPARE(fixture.scenePoint({1, 0}) - origin, QPointF(1, 0));
    QCOMPARE(fixture.scenePoint({0, 1}) - origin, QPointF(0, 1));
    QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
                                           QVariant::fromValue(static_cast<QObject *>(&source))));
    qInfo() << "Video regression RHI" << fixture.backend() << fixture.deviceName();
    for (int revision = 1; revision <= 2; ++revision) {
        QImage reference = periodicImage(sourceSize).convertToFormat(QImage::Format_ARGB32);
        if (revision == 2)
            reference.invertPixels();
        QVideoFrame frame(QVideoFrameFormat(sourceSize, QVideoFrameFormat::Format_BGRA8888));
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < sourceSize.height(); ++y) {
            auto *row = frame.bits(0) + y * frame.bytesPerLine(0);
            for (int x = 0; x < sourceSize.width(); ++x) {
                const QRgb pixel = reference.pixel(x, y);
                row[4*x] = qBlue(pixel);
                row[4*x+1] = qGreen(pixel);
                row[4*x+2] = qRed(pixel);
                row[4*x+3] = 255;
            }
        }
        frame.unmap();
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        sink->setVideoFrame(frame);
        QVERIFY(source.hasFrame());
        QCOMPARE(source.frameSize(), sourceSize);
        QCOMPARE(source.revision(), quint64(revision));
        const QImage actual = fixture.render();
        QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
        const int levels = sourceSize == targetSize ? 0 : 1;
        QCOMPARE(fixture.filtered()->property("requiredLevels").toInt(), levels);
        if (!levels)
            QCOMPARE(fixture.filtered()->property("videoSource").value<QObject *>(), &source);
        else {
            auto *selected = fixture.filtered()->property("source").value<QObject *>();
            QVERIFY(selected);
            QCOMPARE(selected->property("textureSize").toSize(), targetSize);
        }
        comparePixels(actual, levels ? ZoinGallery::ViewerResampler::downsample(reference, targetSize)
                                     : reference, 2);
        if (dpr == 1.75 && revision == 2) {
            const QString capture = qEnvironmentVariable("ZOIN_VIDEO_GPU_CAPTURE");
            if (!capture.isEmpty())
                QVERIFY(actual.save(capture + (levels ? "-pyramid.png" : "-direct.png")));
        }
    }
    qobject_cast<QVideoSink *>(source.sink())->setVideoFrame(QVideoFrame());
    QVERIFY(!source.hasFrame());
    comparePixels(fixture.render(), expectedFallback, 0);
    QVERIFY(!fixture.filtered()->property("videoSource").value<QObject *>());
}
#endif

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
void ViewerResampleGpuTest::audioDeviceDiagnostics() {
    const auto outputs = QMediaDevices::audioOutputs();
    qInfo() << "Qt audio outputs:" << outputs.size()
            << "default:" << QMediaDevices::defaultAudioOutput().description();
    for (const auto &output : outputs)
        qInfo() << "Audio output:" << output.description() << "default:" << output.isDefault();
    // Enumeration is diagnostic: a build container need not have a live audio
    // server. Compiled backend availability is a separate build contract.
}

void ViewerResampleGpuTest::decodedVideoFileRenders() {
    const QString path = qEnvironmentVariable("ZOIN_VIDEO_GPU_FILE");
    if (path.isEmpty())
        QSKIP("Set ZOIN_VIDEO_GPU_FILE to exercise desktop FFmpeg decoding and presentation");
    QVERIFY(QFile::exists(path));
    ZoinGallery::ViewerVideoFrameSource source;
    QMediaPlayer player;
    player.setVideoSink(qobject_cast<QVideoSink *>(source.sink()));
    player.setSource(QUrl::fromLocalFile(path));
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(source.hasFrame(), 20000);
    player.pause();
    QCoreApplication::processEvents();
    const auto snapshot = source.snapshot();
    const QImage reference = snapshot.frame.toImage();
    QVERIFY(!reference.isNull());
    const QSize targetSize(672, 378);
    QImage fallback(reference.size(), QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(fallback, targetSize, 1.75), qPrintable(fixture.error));
    QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
                                           QVariant::fromValue(static_cast<QObject *>(&source))));
    const QImage actual = fixture.render();
    QVERIFY(!actual.isNull());
    qInfo() << "Decoded frame" << reference.size() << "pixel format"
            << snapshot.frame.pixelFormat() << "rendered on" << fixture.deviceName();
    const QString capture = qEnvironmentVariable("ZOIN_VIDEO_GPU_CAPTURE");
    if (!capture.isEmpty())
        QVERIFY(actual.save(capture + "-decoded.png"));
    // Qt's CPU YUV conversion and this shader use different chroma sampling
    // at sharp edges. Keep the exact per-pixel RGB regression above; for an
    // arbitrary decoded YUV file, verify opaque coverage and mean RGB error.
    const QImage expected = ZoinGallery::ViewerResampler::downsample(reference, targetSize);
    quint64 totalError = 0;
    int maximumError = 0;
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            const QRgb a = actual.pixel(x, y);
            const QRgb e = expected.pixel(x, y);
            QCOMPARE(qAlpha(a), 255);
            for (const int difference : {std::abs(qRed(a) - qRed(e)),
                                         std::abs(qGreen(a) - qGreen(e)),
                                         std::abs(qBlue(a) - qBlue(e))}) {
                totalError += difference;
                maximumError = std::max(maximumError, difference);
            }
        }
    }
    const double meanError = double(totalError) / (targetSize.width() * targetSize.height() * 3);
    qInfo() << "Decoded YUV/CPU reference mean RGB error:" << meanError
            << "maximum:" << maximumError;
    QVERIFY2(meanError <= 2.0, "Decoded video does not match the source frame");
    player.stop();
}

void ViewerResampleGpuTest::decodedVideoReusesPresentation() {
    const QString path = qEnvironmentVariable("ZOIN_VIDEO_GPU_FILE");
    if (path.isEmpty())
        QSKIP("Set ZOIN_VIDEO_GPU_FILE to test reuse of a real decoded 4K frame");
    ZoinGallery::ViewerVideoFrameSource source;
    ZoinGallery::ViewerVideoFrameSource decodedSource;
    auto *sink = qobject_cast<QVideoSink *>(source.sink());
    QMediaPlayer player;
    player.setVideoSink(qobject_cast<QVideoSink *>(decodedSource.sink()));
    player.setSource(QUrl::fromLocalFile(path));
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(decodedSource.hasFrame(), 20000);
    player.pause();
    const QVideoFrame frame = decodedSource.snapshot().frame;
    player.stop();
    // Freeze exactly one decoded frame, independent of queued player events.
    player.setVideoSink(nullptr);
    sink->setVideoFrame(frame);
    QImage fallback(source.frameSize(), QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    GpuFixture fixture;
    QVERIFY2(fixture.initialize(fallback, {2560, 1440}, 1.75), qPrintable(fixture.error));
    fixture.filtered()->setProperty("pixelAligned", true);
    fixture.filtered()->setProperty("videoFrameSource",
        QVariant::fromValue(static_cast<QObject *>(&source)));
    const QImage cached = fixture.render();
    QVERIFY(fixture.filtered()->property("presentationCacheEligible").toBool());
    DrawCounter draws;
    draws.reset();
    for (int repeat = 0; repeat < 3; ++repeat) {
        QElapsedTimer timer;
        timer.start();
        const QImage repeated = fixture.render(1);
        qInfo() << "Paused 4K cached render+readback ms" << timer.nsecsElapsed() / 1000000.0;
        comparePixels(repeated, cached, 0);
    }
    QCOMPARE(draws.count(), 0);
    fixture.filtered()->setProperty("cacheVideoPresentation", false);
    const QImage direct = fixture.render();
    comparePixels(cached, direct, 0);
    for (int repeat = 0; repeat < 3; ++repeat) {
        QElapsedTimer timer;
        timer.start();
        const QImage repeated = fixture.render(1);
        qInfo() << "Paused 4K direct render+readback ms" << timer.nsecsElapsed() / 1000000.0;
        comparePixels(repeated, direct, 0);
    }
    const QString capture = qEnvironmentVariable("ZOIN_VIDEO_GPU_CAPTURE");
    if (!capture.isEmpty())
        QVERIFY(cached.save(capture + "-decoded-cache175.png"));
}

void ViewerResampleGpuTest::decodedVideoResampleBenchmark_data() {
    QTest::addColumn<QSize>("targetSize");
    QTest::newRow("direct") << QSize(2560, 1440);
    QTest::newRow("windowed-pyramid") << QSize(1538, 866);
    QTest::newRow("quarter-pyramid") << QSize(960, 540);
}

void ViewerResampleGpuTest::decodedVideoResampleBenchmark() {
    QFETCH(QSize, targetSize);
    const QString path = qEnvironmentVariable("ZOIN_VIDEO_GPU_FILE");
    if (path.isEmpty())
        QSKIP("Set ZOIN_VIDEO_GPU_FILE to a 4K clip for fresh-frame resampling timings");
    QVERIFY(QFile::exists(path));
    ZoinGallery::ViewerVideoFrameSource decodedSource;
    QMediaPlayer player;
    player.setVideoSink(qobject_cast<QVideoSink *>(decodedSource.sink()));
    player.setSource(QUrl::fromLocalFile(path));
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(decodedSource.hasFrame(), 20000);
    player.pause();
    const QVideoFrame decoded = decodedSource.snapshot().frame;
    player.stop();
    player.setVideoSink(nullptr);
    QVERIFY(decoded.isValid());
    QVERIFY2(decoded.width() >= 3840 && decoded.height() >= 2160,
             "Fresh-frame benchmark requires a 4K source clip");
    const qreal dpr = 1.75;
    const int warmupCount = 3;
    const int sampleCount = 20;
    std::vector<QVideoFrame> frames;
    frames.reserve(warmupCount + sampleCount);
    // Decode, allocate, copy, and assign metadata before either timing pass.
    // Different buffers keep identical pixels but always advance the sink's
    // revision; changing a shallow copy's timestamp would mutate a live frame.
    for (int index = 0; index < warmupCount + sampleCount; ++index) {
        frames.push_back(independentVideoFrame(decoded, qint64(index) * 33333));
        QVERIFY2(frames.back().isValid(), "Could not copy the decoded video planes independently");
        QCOMPARE(frames.back().pixelFormat(), decoded.pixelFormat());
        QCOMPARE(frames.back().surfaceFormat(), decoded.surfaceFormat());
    }
    QImage fallback(decoded.size(), QImage::Format_ARGB32);
    fallback.fill(Qt::magenta);
    QImage reference;
    RenderTimingSummary referenceTiming{};
    BenchmarkDrawTraceSilencer silenceDraws;
    qInfo() << "Fresh 4K video benchmark" << decoded.size() << "format" << decoded.pixelFormat()
            << "target" << targetSize << "DPR" << dpr
            << "warmup" << warmupCount << "samples" << sampleCount;
    for (const QByteArray &mode : {QByteArray("1"), QByteArray("0"), QByteArray()}) {
        ScopedEnvironmentVariable selection("F4_VIEWER_RESAMPLE_REFERENCE", mode);
        ZoinGallery::ViewerVideoFrameSource source;
        GpuFixture fixture;
        QVERIFY2(fixture.initialize(fallback, targetSize, dpr), qPrintable(fixture.error));
        QVERIFY(fixture.filtered()->setProperty("pixelAligned", true));
        QVERIFY(fixture.filtered()->setProperty("videoFrameSource",
            QVariant::fromValue(static_cast<QObject *>(&source))));
        auto *sink = qobject_cast<QVideoSink *>(source.sink());
        QVERIFY(sink);
        // Settle scene construction and pipeline creation without consuming
        // any of the measured revisions. The paused cache remains enabled.
        sink->setVideoFrame(frames.front());
        QVERIFY2(!fixture.render().isNull(), qPrintable(fixture.error));
        for (int index = 1; index < warmupCount; ++index) {
            const quint64 before = source.revision();
            sink->setVideoFrame(frames[index]);
            QCOMPARE(source.revision(), before + 1);
            QVERIFY2(!fixture.render(1).isNull(), qPrintable(fixture.error));
        }
        const bool deviceSupported = qEnvironmentVariableIntValue("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE") != 1
            && fixture.rhiSupportsLinearVideoCache();
        QCOMPARE(fixture.filtered()->property("referenceSampling").toBool(), mode == "1");
        const QSize displaySize = source.frameSize();
        const bool requested = mode != "1"
            && displaySize.width() <= 4096 && displaySize.height() <= 4096
            && qint64(displaySize.width()) * displaySize.height() <= 9 * 1024 * 1024
            && displaySize.width() >= targetSize.width() * 1.05
            && displaySize.height() >= targetSize.height() * 1.05;
        const bool conversionExpected = requested && deviceSupported;
        QCOMPARE(fixture.filtered()->property("linearVideoCacheRequested").toBool(), requested);
        QVERIFY(fixture.linearVideoConsumer());
        QCOMPARE(fixture.linearVideoConsumer()->property("linearVideoCacheSupported").toBool(), conversionExpected);
        QCOMPARE(fixture.filtered()->property("linearVideoCacheEligible").toBool(), conversionExpected);
        if (conversionExpected)
            QVERIFY2(fixture.hasLinearVideoTexture(displaySize), "Benchmark must have a source-sized RGBA32F converter texture");
        std::vector<qint64> timings;
        timings.reserve(sampleCount);
        for (int index = 0; index < sampleCount; ++index) {
            const quint64 before = source.revision();
            sink->setVideoFrame(frames[warmupCount + index]);
            QCOMPARE(source.revision(), before + 1);
            silenceDraws.reset();
            // Each timed region contains exactly one sync/render/readback.
            // Frame construction, delivery, comparison, and statistics stay out.
            QElapsedTimer timer;
            timer.start();
            const QImage actual = fixture.render(1);
            const qint64 elapsed = timer.nsecsElapsed();
            QVERIFY2(!actual.isNull(), qPrintable(fixture.error));
            QCOMPARE(actual.size(), targetSize);
            QCOMPARE(source.revision(), before + 1);
            QVERIFY2(silenceDraws.count() > 0,
                     "Measured fresh revision must execute filtering, not hit the paused cache");
            QCOMPARE(silenceDraws.linearCount(), conversionExpected ? 1 : 0);
            QCOMPARE(silenceDraws.conversionCount(), conversionExpected ? 1 : 0);
            if (conversionExpected)
                QCOMPARE(silenceDraws.linearRevision(), before + 1);
            timings.push_back(elapsed);
            if (mode == "1" && reference.isNull())
                reference = actual;
            else
                comparePixels(actual, reference, 0);
            if (QTest::currentTestFailed())
                return;
        }
        const RenderTimingSummary summary = summarizeRenderTimings(timings);
        const QByteArray label = mode == "1" ? QByteArray("reference=1")
            : (mode.isNull() ? QByteArray("optimized=unset") : QByteArray("optimized=0"));
        qInfo().noquote() << "Fresh 4K sync render+readback" << label
                         << "device" << fixture.deviceName()
                         << "linear samples/revision" << (conversionExpected ? 1 : 0)
                         << "median ms" << summary.medianMs << "p95 ms" << summary.p95Ms
                         << "min ms" << summary.minimumMs;
        if (mode == "1")
            referenceTiming = summary;
        else
            qInfo().noquote() << label << "reference/optimized median ratio"
                             << referenceTiming.medianMs / summary.medianMs
                             << "pixel comparison tolerance 0";
    }
}
#endif

void ViewerResampleGpuTest::shaderStageInterfacesMatch() {
    using D = QShaderDescription;
    struct Field { const char *name; D::VariableType type; int offset; int size; };
    const QList<Field> base {
        {"qt_Matrix", D::Mat4, 0, 64}, {"qt_Opacity", D::Float, 64, 4},
        {"viewportSize", D::Vec2, 72, 8}, {"checkerboardOffset", D::Vec2, 80, 8},
        // QSB/SPIR-V represents GLSL uniform-block bools as uint32.
        {"showCheckerboard", D::Uint, 88, 4}, {"checkerboardSize", D::Int, 92, 4},
        {"borderRadius", D::Float, 96, 4}, {"intermediate", D::Uint, 100, 4},
        {"pixelAlignedIdentity", D::Uint, 104, 4}, {"nearestNeighbor", D::Uint, 108, 4},
        {"sourceExtent", D::Vec2, 112, 8}, {"pixelAligned", D::Uint, 120, 4},
        {"hardwareSampling", D::Uint, 124, 4},
        {"itemSize", D::Vec2, 128, 8}, {"framebufferRect", D::Vec4, 144, 16},
        {"framebufferYDirection", D::Float, 160, 4},
    };
    QStringList variants {QStringLiteral("viewer_resample")};
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    variants.append(QStringLiteral("viewer_resample_video"));
    variants.append(QStringLiteral("viewer_resample_video_reference"));
    variants.append(QStringLiteral("viewer_video_linear"));
#endif
    for (const QString &variant : variants) {
        QList<Field> expected = base;
        const bool video = variant != QStringLiteral("viewer_resample");
        if (video) {
            expected.append({{"videoColorMatrix", D::Mat4, 176, 64},
                {"videoFrameSize", D::Vec2, 240, 8}, {"videoViewport", D::Vec4, 256, 16},
                {"videoPixelInfo", D::Int4, 272, 16}, {"videoTextureFormats", D::Int4, 288, 16},
                {"videoEnabled", D::Int, 304, 4}});
        }
        for (const QString &stage : {QStringLiteral("vert"), QStringLiteral("frag")}) {
            // All video fragments share the same video vertex stage and must
            // retain its complete 308-byte uniform layout, including unused fields.
            const QString asset = video && stage == QStringLiteral("vert")
                ? QStringLiteral("viewer_resample_video") : variant;
            QFile file(QStringLiteral(":/ZoinGallery/resources/%1.%2.qsb").arg(asset, stage));
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
            const QShader shader = QShader::fromSerialized(file.readAll());
            QVERIFY(shader.isValid());
            QCOMPARE(shader.stage(), stage == QStringLiteral("vert") ? QShader::VertexStage
                                                                     : QShader::FragmentStage);
            const auto blocks = shader.description().uniformBlocks();
            QCOMPARE(blocks.size(), 1);
            const auto block = blocks.first();
            QCOMPARE(block.blockName, QByteArray("buf"));
            QCOMPARE(block.structName, QByteArray("ubuf"));
            QCOMPARE(block.binding, 0);
            QCOMPARE(block.descriptorSet, 0);
            QCOMPARE(block.size, video ? 308 : 164);
            QCOMPARE(block.members.size(), expected.size());
            for (qsizetype index = 0; index < expected.size(); ++index) {
                const auto member = block.members[index];
                QCOMPARE(member.name, QByteArray(expected[index].name));
                QCOMPARE(member.type, expected[index].type);
                QCOMPARE(member.offset, expected[index].offset);
                QCOMPARE(member.size, expected[index].size);
                QCOMPARE(member.matrixStride, expected[index].type == D::Mat4 ? 16 : 0);
                QVERIFY(!member.matrixIsRowMajor);
                QVERIFY(member.arrayDims.isEmpty());
            }
            const auto description = shader.description();
            QVERIFY(description.separateImages().isEmpty());
            QVERIFY(description.separateSamplers().isEmpty());
            QVERIFY(description.storageImages().isEmpty());
            const auto samplers = description.combinedImageSamplers();
            if (stage == QStringLiteral("vert")) {
                QVERIFY(samplers.isEmpty());
                continue;
            }
            struct Sampler { const char *name; int binding; };
            QList<Sampler> expectedSamplers{{"source", 1}};
            if (video)
                expectedSamplers.append({{"videoPlane2", 2}, {"videoPlane3", 3}});
            if (variant == QStringLiteral("viewer_resample_video"))
                expectedSamplers.append({"linearVideoSource", 4});
            QCOMPARE(samplers.size(), expectedSamplers.size());
            QSet<int> bindings{block.binding};
            for (const auto &sampler : samplers) {
                QVERIFY2(!bindings.contains(sampler.binding), "Sampler must not alias another sampler or the uniform block");
                bindings.insert(sampler.binding);
                QCOMPARE(sampler.descriptorSet, 0);
                QCOMPARE(sampler.type, D::Sampler2D);
                QVERIFY(sampler.arrayDims.isEmpty());
                const auto expectedSampler = std::find_if(expectedSamplers.cbegin(), expectedSamplers.cend(),
                    [&](const Sampler &entry) { return entry.binding == sampler.binding; });
                QVERIFY(expectedSampler != expectedSamplers.cend());
                QCOMPARE(sampler.name, QByteArray(expectedSampler->name));
            }
        }
    }
}

int main(int argc, char **argv) {
    qputenv("F4_VIEWER_RESAMPLE_DRAW_TRACE", "1");
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
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    QGuiApplication application(argc, argv);
    ViewerResampleGpuTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ViewerResampleGpuTest.moc"
