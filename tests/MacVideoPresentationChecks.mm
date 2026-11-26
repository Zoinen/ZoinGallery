#include "MacVideoEdr.h"
#include "MacVideoSample.h"
#include <QAbstractVideoBuffer>
#include <QMediaPlayer>
#include <QMap>
#include <QOperatingSystemVersion>
#include <QSet>
#include <QtEndian>
#include "ViewerVideoFrameSource.h"
#include "src/embed/VideoPlaybackController.h"
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <VideoToolbox/VideoToolbox.h>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QtGui/qrgbafloat.h>
#include <QtCore/private/qcore_mac_p.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

namespace {
constexpr std::array<int,8> pqCodes{64,119,300,509,582,588,723,723};
constexpr std::array<int,8> hlgCodes{64,90,220,420,620,640,900,900};
void fillBands(uchar *luma, int yStride, uchar *chroma, int uvStride, QSize size, bool pq)
{
    const auto &codes = pq ? pqCodes : hlgCodes;
    for (int y = 0; y < size.height(); ++y) {
        auto *row = reinterpret_cast<quint16 *>(luma + y*yStride);
        for (int x = 0; x < size.width(); ++x)
            row[x] = quint16(codes[8*x/size.width()] << 6);
    }
    for (int y = 0; y < size.height()/2; ++y) {
        auto *row = reinterpret_cast<quint16 *>(chroma + y*uvStride);
        for (int x = 0; x < size.width(); x += 2) {
            const bool cyan = x >= size.width()*7/8;
            row[x] = (cyan ? 532 : 512) << 6;
            row[x+1] = (cyan ? 467 : 512) << 6;
        }
    }
}

// Observe when the native presenter consumes an input, independently of the
// display refresh rate or the time it takes WindowServer to composite it.
class ObservedVideoBuffer final : public QAbstractVideoBuffer {
public:
    explicit ObservedVideoBuffer(QVideoFrame frame) : m_frame(std::move(frame)) {}
    QVideoFrameFormat format() const override { return m_frame.surfaceFormat(); }
    MapData map(QVideoFrame::MapMode mode) override {
        MapData data;
        if (!m_frame.map(mode)) return data;
        ++maps;
        data.planeCount = m_frame.planeCount();
        for (int p = 0; p < data.planeCount; ++p) {
            data.data[p] = m_frame.bits(p);
            data.bytesPerLine[p] = m_frame.bytesPerLine(p);
            data.dataSize[p] = m_frame.mappedBytes(p);
        }
        return data;
    }
    void unmap() override { m_frame.unmap(); }
    int maps = 0;
private:
    QVideoFrame m_frame;
};

// Independent AVFoundation reference. Supply native Core Video planes directly.
QCFType<CMSampleBufferRef> referenceSample(QSize size, bool pq)
{
    QCFType<CVPixelBufferRef> pixels;
    NSDictionary *options = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    if (CVPixelBufferCreate(nullptr, size.width(), size.height(), kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange,
                            (CFDictionaryRef)options, &pixels) != noErr)
        return {};
    CVPixelBufferLockBaseAddress(pixels, 0);
    fillBands((uchar *)CVPixelBufferGetBaseAddressOfPlane(pixels, 0), CVPixelBufferGetBytesPerRowOfPlane(pixels, 0),
              (uchar *)CVPixelBufferGetBaseAddressOfPlane(pixels, 1), CVPixelBufferGetBytesPerRowOfPlane(pixels, 1), size, pq);
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    CVBufferSetAttachment(pixels, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_2020, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_2020, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferTransferFunctionKey, pq ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ
        : kCVImageBufferTransferFunction_ITU_R_2100_HLG, kCVAttachmentMode_ShouldPropagate);
    QCFType<CMVideoFormatDescriptionRef> fmt;
    QCFType<CMSampleBufferRef> sample;
    CMSampleTimingInfo timing{kCMTimeInvalid, kCMTimeZero, kCMTimeInvalid};
    if (CMVideoFormatDescriptionCreateForImageBuffer(nullptr, pixels, &fmt) != noErr
        || CMSampleBufferCreateReadyWithImageBuffer(nullptr, pixels, fmt, &timing, &sample) != noErr)
        return {};
    auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, true);
    CFDictionarySetValue((CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0), kCMSampleAttachmentKey_DisplayImmediately, kCFBooleanTrue);
    return sample;
}

QImage capture(CGWindowID number, QSize size, const QString &tiffPath = {}) API_AVAILABLE(macos(15.0))
{
    struct Result { QSemaphore done; QCFType<CGImageRef> image; };
    auto result = std::make_shared<Result>();
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:NO completionHandler:^(SCShareableContent *content, NSError *) {
        for (SCWindow *w in content.windows) {
            if (w.windowID != number) continue;
            auto filter = [[[SCContentFilter alloc] initWithDesktopIndependentWindow:w] autorelease];
            auto config = [SCStreamConfiguration streamConfigurationWithPreset:SCStreamConfigurationPresetCaptureHDRScreenshotCanonicalDisplay];
            config.ignoreShadowsSingleWindow = YES;
            config.showsCursor = NO;
            config.width = size.width();
            config.height = size.height();
            [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError *) {
                if (image) result->image = CGImageRetain(image);
                result->done.release();
            }];
            return;
        }
        result->done.release();
    }];
    if (!QTest::qWaitFor([&]{ return result->done.available(); }, 5000) || !result->image) return {};
    result->done.acquire();
    QImage image(size, QImage::Format_RGBA32FPx4_Premultiplied);
    QCFType<CGColorSpaceRef> linear = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
    QCFType<CGContextRef> context = CGBitmapContextCreate(image.bits(), size.width(), size.height(), 32,
        image.bytesPerLine(), linear, kCGBitmapFloatComponents | kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedLast);
    if (!context) return {};
    // Overwrite the float bitmap. Source-over can blend uninitialized NaNs
    // from the destination into otherwise finite screenshot channels.
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0,0,size.width(),size.height()), result->image);
    if (!tiffPath.isEmpty()) {
        QCFType<CGImageRef> floatImage = CGBitmapContextCreateImage(context);
        QCFType<CGImageDestinationRef> dest = CGImageDestinationCreateWithURL(
            (CFURLRef)[NSURL fileURLWithPath:tiffPath.toNSString()], CFSTR("public.tiff"), 1, nullptr);
        if (!dest) return {};
        CGImageDestinationAddImage(dest, floatImage, nullptr);
        if (!CGImageDestinationFinalize(dest)) return {};
    }
    return image;
}
} // namespace

void checkMacVideoPresentation(QVideoFrameFormat::ColorTransfer transfer)
{
    using namespace ZoinGallery;
    using F = QVideoFrameFormat;
    const bool pq = transfer == F::ColorTransfer_ST2084;
    const QSize size(1024, 512);
    F format(size, F::Format_P010);
    format.setColorRange(F::ColorRange_Video);
    format.setColorSpace(F::ColorSpace_BT2020);
    format.setColorTransfer(transfer);
    QVideoFrame frame(format);
    QVERIFY(frame.map(QVideoFrame::WriteOnly));
    fillBands(frame.bits(0), frame.bytesPerLine(0), frame.bits(1), frame.bytesPerLine(1), size, pq);
    frame.unmap();
    ViewerVideoFrameSource source;
    auto *sink = qobject_cast<QVideoSink *>(source.sink());
    sink->setVideoFrame(frame);
    const auto revision = source.revision();
    QQuickWindow window;
    window.setFlags(Qt::FramelessWindowHint | Qt::Window);
    window.setColor(QColor("#808080"));
    window.resize(432,168);
    window.show();
    QTRY_VERIFY(window.isExposed());
    NSView *view = reinterpret_cast<NSView *>(window.winId());
    QCFType<CGColorSpaceRef> original = CGColorSpaceRetain(view.window.colorSpace.CGColorSpace);
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import ZoinGallery
        ViewerResample {
            x: 12; y: 24; width: 192; height: 96
            readonly property real dpr: Window.window ? Window.window.devicePixelRatio : 1
            viewportSize: Qt.size(width * dpr, height * dpr)
            Rectangle { x: 2; y: 2; width: 12; height: 12; color: "#5491bd"; z: 1 }
        }
    )", QUrl(QStringLiteral("qrc:/mac-video-test.qml")));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *item = qobject_cast<QQuickItem *>(object.get());
    item->setParentItem(window.contentItem());
    item->setProperty("videoFrameSource", QVariant::fromValue(static_cast<QObject *>(&source)));
    auto *output = item->findChild<MacVideoEdr *>();
    QVERIFY(output);
    QTRY_VERIFY_WITH_TIMEOUT(output->active(), 5000);
    QVERIFY2(CFEqual(original, view.window.colorSpace.CGColorSpace),
             "HDR video must leave the native UI compositing color space intact");

    AVSampleBufferDisplayLayer *reference = [AVSampleBufferDisplayLayer new];
    const auto remove = qScopeGuard([&] { [reference removeFromSuperlayer]; [reference release]; });
    reference.videoGravity = AVLayerVideoGravityResize;
    [view.layer addSublayer:reference];
    auto sample = referenceSample(size, pq);
    QVERIFY(sample);
    if (@available(macOS 14, *)) [reference.sampleBufferRenderer enqueueSampleBuffer:sample];
    else [reference enqueueSampleBuffer:sample];
    for (int width : {192, 128}) {
        item->setSize({qreal(width), width/2.0});
        reference.frame = [view convertRectToLayer:NSMakeRect(228, 24, width, width/2)];
        window.update();
        if (@available(macOS 14.4, *)) {
            QTRY_VERIFY(reference.readyForDisplay);
        }
        QTest::qWait(250);
        if (@available(macOS 15, *)) {
            if (CGPreflightScreenCaptureAccess()) {
                const auto image = capture(CGWindowID(view.window.windowNumber), window.size());
                QVERIFY(!image.isNull());
                const int y = 24+width/4;
                std::array<float,8> bands{};
                float error = 0;
                for (int i=0; i<8; ++i) {
                    const int center = width*(2*i+1)/16;
                    auto actual = image.pixelColor(12+center,y);
                    auto expected = image.pixelColor(228+center,y);
                    bands[i] = actual.greenF();
                    const float residual = std::max({std::abs(actual.redF()-expected.redF()),
                        std::abs(actual.greenF()-expected.greenF()), std::abs(actual.blueF()-expected.blueF())});
                    error = std::max(error, residual);
                    QVERIFY2(residual < .01f * std::max(1.f,expected.greenF()),
                        qPrintable(QString("Band %1, F4 %2/%3/%4, AVFoundation %5/%6/%7").arg(i)
                        .arg(actual.redF()).arg(actual.greenF()).arg(actual.blueF())
                        .arg(expected.redF()).arg(expected.greenF()).arg(expected.blueF())));
                }
                QVERIFY(bands[0] < bands[1] && bands[1] < bands[2] && bands[4] < bands[5]);
                QVERIFY(bands[6] > bands[5]);
                const auto ui = image.pixelColor(19,31);
                QVERIFY(ui.blueF() > ui.greenF() && ui.greenF() > ui.redF());
                qInfo() << "AVFoundation reference: width" << width << "peak" << bands[6] << "max RGB difference" << error;
            }
        }
        QCOMPARE(source.revision(), revision);
    }
    QSignalSpy idleUpdates(&window, &QQuickWindow::afterAnimating);
    QTest::qWait(350);
    QVERIFY2(idleUpdates.size() < 4, qPrintable(QStringLiteral("Paused video requested %1 window updates").arg(idleUpdates.size())));
    auto observed = std::make_unique<ObservedVideoBuffer>(frame);
    const auto *submitted = observed.get();
    QVideoFrame nextFrame(std::move(observed));
    nextFrame.setStartTime(41708);
    sink->setVideoFrame(nextFrame);
    QVERIFY2(submitted->maps > 0, "A playing native plane waited for another Qt window frame");
    item->setVisible(false);
    QTRY_VERIFY(!output->active());
    item->setVisible(true);
    QTRY_VERIFY(output->active());
    sink->setVideoFrame(QVideoFrame(QImage(64,64,QImage::Format_RGB32)));
    QTRY_VERIFY(!output->active());
    QVERIFY(CFEqual(original, view.window.colorSpace.CGColorSpace));
}

struct VideoTile {
    float topOnePercent;
    float peak;
};

static VideoTile measureTile(const QImage &image, const QRect &rect)
{
    std::vector<float> luminance;
    luminance.reserve(rect.width() * rect.height());
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const auto *pixels = reinterpret_cast<const QRgbaFloat32 *>(image.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const auto &pixel = pixels[x];
            luminance.push_back(.2126f * pixel.red() + .7152f * pixel.green() + .0722f * pixel.blue());
        }
    }
    // The capture is in extended linear sRGB, where 1 is SDR white. Compare
    // the whole bright region as well as its peak, which can be just one pixel.
    auto begin = luminance.begin() + luminance.size() * 99 / 100;
    std::nth_element(luminance.begin(), begin, luminance.end());
    return {float(std::accumulate(begin, luminance.end(), 0.0) / (luminance.end() - begin)),
            *std::max_element(begin, luminance.end())};
}

static void compareFile() API_AVAILABLE(macos(15.0));
static void compareFile()
{
    using namespace ZoinGallery;
    if (!CGPreflightScreenCaptureAccess()) QSKIP("Screen capture permission required for this comparison");
    if (!AVPlayer.eligibleForHDRPlayback) QSKIP("AVFoundation HDR playback is unavailable");
    const auto original = qEnvironmentVariable("ZOIN_VIDEO_GPU_FILE");
    const auto referencePath = qEnvironmentVariable("ZOIN_VIDEO_APPLE_REFERENCE");
    const auto position = qEnvironmentVariableIntValue("ZOIN_VIDEO_APPLE_SEEK_MS");
    const bool expectDynamic = qEnvironmentVariableIntValue("ZOIN_VIDEO_APPLE_EXPECT_DYNAMIC");
    QVERIFY(QFile::exists(original) && QFile::exists(referencePath));
    QMediaPlayer player;
    ViewerVideoFrameSource source;
    player.setVideoSink(qobject_cast<QVideoSink *>(source.sink()));
    player.setSource(QUrl::fromLocalFile(original));
    player.pause();
    QTRY_VERIFY_WITH_TIMEOUT(source.hasFrame() && player.isSeekable(), 15000);
    player.setPosition(position);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(source.snapshot().frame.startTime()/1000 - position) < 60, 10000);
    const auto frame = source.snapshot().frame;
    MacVideoSample factory;
    auto sample = factory.make(frame);
    QVERIFY(sample);
    const auto desc = CMSampleBufferGetFormatDescription(sample);
    const auto mastering = static_cast<CFDataRef>(CMFormatDescriptionGetExtension(desc,kCMFormatDescriptionExtension_MasteringDisplayColorVolume));
    const auto light = static_cast<CFDataRef>(CMFormatDescriptionGetExtension(desc,kCMFormatDescriptionExtension_ContentLightLevelInfo));
    qInfo() << "File PTS (us)" << frame.startTime() << "mastering peak" << frame.surfaceFormat().maxLuminance();
    if (qEnvironmentVariableIntValue("ZOIN_VIDEO_EXPECT_STATIC_HDR10")) {
        QVERIFY(mastering && CFDataGetLength(mastering) == 24);
        QVERIFY(light && CFDataGetLength(light) == 4);
        const auto bytes = CFDataGetBytePtr(mastering);
        QCOMPARE(qFromBigEndian<quint32>(bytes+16)/10000.f, frame.surfaceFormat().maxLuminance());
        qInfo() << "ST 2086 min/max" << qFromBigEndian<quint32>(bytes+20)/10000.f
                << qFromBigEndian<quint32>(bytes+16)/10000.f << "MaxCLL/MaxFALL"
                << qFromBigEndian<quint16>(CFDataGetBytePtr(light))
                << qFromBigEndian<quint16>(CFDataGetBytePtr(light)+2);
    }

    constexpr int tileWidth = 480;
    const int tileHeight = qRound(tileWidth * frame.height() / qreal(frame.width()));
    constexpr int columnStride = tileWidth + 8;
    QQuickWindow window;
    window.setFlags(Qt::FramelessWindowHint | Qt::Window);
    window.resize((expectDynamic ? 3 : 2) * columnStride + 8, tileHeight + 48);
    window.setColor(Qt::black);
    window.show();
    QTRY_VERIFY(window.isExposed());
    NSView *view = reinterpret_cast<NSView *>(window.winId());
    if (view.window.screen.maximumPotentialExtendedDynamicRangeColorComponentValue <= 1.01)
        QSKIP("The comparison requires an EDR-capable display");
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport ZoinGallery\nViewerResample { x: 8; y: 24 }", QUrl("qrc:/file-comparison.qml"));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *item = qobject_cast<QQuickItem *>(object.get());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(tileWidth, tileHeight));
    item->setProperty("videoFrameSource", QVariant::fromValue(static_cast<QObject *>(&source)));
    auto *output = item->findChild<MacVideoEdr *>();
    QTRY_VERIFY(output && output->active());
    auto url = [NSURL fileURLWithPath:referencePath.toNSString()];
    auto makeReference = [&](int column, bool disableDynamicMetadata) {
        auto asset = [AVPlayerItem playerItemWithURL:url];
        if (disableDynamicMetadata)
            asset.appliesPerFrameHDRDisplayMetadata = NO;
        auto layer = [AVPlayerLayer playerLayerWithPlayer:[AVPlayer playerWithPlayerItem:asset]];
        layer.videoGravity = AVLayerVideoGravityResize;
        layer.frame = [view convertRectToLayer:NSMakeRect(8 + column * columnStride, 24, tileWidth, tileHeight)];
        [view.layer addSublayer:layer];
        return layer;
    };
    AVPlayerLayer *reference = makeReference(1, false);
    AVPlayerLayer *staticControl = expectDynamic ? makeReference(2, true) : nil;
    const auto detach = qScopeGuard([&] { [reference removeFromSuperlayer]; [staticControl removeFromSuperlayer]; });
    const qint64 frameDurationUs = frame.endTime() - frame.startTime();
    const auto target = CMTimeMake(frame.startTime() + (frameDurationUs > 0 ? frameDurationUs / 2 : 1), 1000000);
    for (AVPlayerLayer *layer : {reference, staticControl}) {
        if (!layer) continue;
        QTRY_VERIFY_WITH_TIMEOUT(layer.player.currentItem.status == AVPlayerItemStatusReadyToPlay, 10000);
        auto done = std::make_shared<QSemaphore>();
        [layer.player seekToTime:target toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero
            completionHandler:^(BOOL finished){ if (finished) done->release(); }];
        QTRY_VERIFY_WITH_TIMEOUT(done->available() && layer.readyForDisplay, 10000);
    }
    if (!QTest::qWaitFor([&]{ return view.window.screen.maximumExtendedDynamicRangeColorComponentValue > 1.01; }, 5000))
        QSKIP("The display has no current EDR headroom");
    QTest::qWait(450);
    const qreal dpr = window.devicePixelRatio();
    const auto image = capture(CGWindowID(view.window.windowNumber), window.size() * dpr,
                               qEnvironmentVariable("ZOIN_VIDEO_APPLE_CAPTURE"));
    QVERIFY(!image.isNull());
    auto tileRect = [&](int column) {
        return QRect(qRound((8 + column * columnStride) * dpr), qRound(24 * dpr),
                     qRound(tileWidth * dpr), qRound(tileHeight * dpr)).adjusted(2, 2, -2, -2);
    };
    const auto actualRect = tileRect(0);
    const auto referenceRect = tileRect(1);
    const auto actual = measureTile(image, actualRect);
    const auto expected = measureTile(image, referenceRect);
    qInfo() << "Linear luminance, brightest 1% mean / peak: F4" << actual.topOnePercent << actual.peak
            << "AVPlayer (default)" << expected.topOnePercent << expected.peak;
    if (expectDynamic) {
        const auto control = measureTile(image, tileRect(2));
        qInfo() << "AVPlayer (per-frame metadata disabled)" << control.topOnePercent << control.peak;
        QVERIFY2(std::abs(expected.topOnePercent - control.topOnePercent) > .04f * std::max(1.f, expected.topOnePercent),
                 "Choose a frame on which AVPlayer visibly applies dynamic metadata");
        QVERIFY2(expected.peak > 1.01f, "The reference frame must contain captured EDR highlights");
    }
    QVERIFY2(std::abs(actual.topOnePercent - expected.topOnePercent) < .025f * std::max(1.f, expected.topOnePercent),
             "The brightest 1% of the F4 frame differs from default AVPlayer");
    QVERIFY2(std::abs(actual.peak - expected.peak) < .05f * std::max(1.f, expected.peak),
             "The brightest F4 pixel differs from default AVPlayer");
    std::vector<float> errors;
    errors.reserve(actualRect.width() * actualRect.height());
    for (int y = actualRect.top(); y <= actualRect.bottom(); ++y) {
        for (int x = 0; x < actualRect.width(); ++x) {
            auto a = image.pixelColor(actualRect.left()+x,y), b = image.pixelColor(referenceRect.left()+x,y);
            errors.push_back(std::max({std::abs(a.redF()-b.redF()), std::abs(a.greenF()-b.greenF()),
                                       std::abs(a.blueF()-b.blueF())}));
        }
    }
    std::sort(errors.begin(),errors.end());
    qInfo() << "AVPlayerLayer pixel RGB error median/p95/p99"
            << errors[errors.size()/2] << errors[errors.size()*95/100] << errors[errors.size()*99/100];
    QVERIFY(actual.peak > .1f);
    QVERIFY(errors[errors.size()/2] < .01f && errors[errors.size()*95/100] < .04f);
}


void compareMacFileWithAVPlayer()
{
    if (@available(macOS 15, *)) compareFile();
    else QSKIP("HDR screenshots require macOS 15");
}

using AttachmentBytes = QMap<QString, QByteArray>;

static AttachmentBytes propagatedData(CVImageBufferRef image)
{
    AttachmentBytes data;
    QCFType<CFDictionaryRef> attachments = CVBufferCopyAttachments(image, kCVAttachmentMode_ShouldPropagate);
    auto dictionary = (NSDictionary *)static_cast<CFDictionaryRef>(attachments);
    for (NSString *key in dictionary) {
        NSData *value = dictionary[key];
        if ([value isKindOfClass:NSData.class])
            data.insert(QString::fromNSString(key), QByteArray(static_cast<const char *>(value.bytes), value.length));
    }
    return data;
}

static QMap<qint64, AttachmentBytes> appleFrameMetadata(const QString &path)
{
    QMap<qint64, AttachmentBytes> frames;
    auto asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path.toNSString()] options:nil];
    auto track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
    if (!track) return {};
    auto reader = [AVAssetReader assetReaderWithAsset:asset error:nil];
    auto output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:@{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    }];
    if (![reader canAddOutput:output]) return {};
    output.alwaysCopiesSampleData = NO;
    [reader addOutput:output];
    if (![reader startReading]) return {};
    while (QCFType<CMSampleBufferRef> sample = [output copyNextSampleBuffer]) {
        const auto time = CMSampleBufferGetPresentationTimeStamp(sample);
        frames.insert(CMTimeConvertScale(time, 1000, kCMTimeRoundingMethod_Default).value,
                      propagatedData(CMSampleBufferGetImageBuffer(sample)));
    }
    return frames;
}

void checkMacDecodedDynamicMetadata(const QString &path, const QString &staticPath,
                                    QVideoFrameFormat::ColorTransfer transfer, int minimumDynamicMacOS, bool hasUntaggedLeadIn)
{
    using namespace ZoinGallery;
    QVERIFY(QFile::exists(path) && QFile::exists(staticPath));
    QMediaPlayer player;
    ViewerVideoFrameSource source;
    player.setVideoSink(qobject_cast<QVideoSink *>(source.sink()));
    player.setSource(QUrl::fromLocalFile(path));
    player.pause();
    QTRY_VERIFY_WITH_TIMEOUT(source.hasFrame() && player.isSeekable(), 10000);
    MacVideoSample factory;
    const bool nativeHevc = VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)
                            && source.snapshot().frame.pixelFormat() == QVideoFrameFormat::Format_P010;
    // Older systems can play an 8.1 file through its HDR10 base, and a Mac
    // without a Main 10 hardware decoder can receive planar software frames.
    // Exercise that real fallback at a seek even when native testing is unavailable.
    if (!nativeHevc || QOperatingSystemVersion::current().majorVersion() < minimumDynamicMacOS) {
        player.setPosition(1300);
        QTRY_COMPARE_WITH_TIMEOUT(source.snapshot().frame.startTime() / 1000, 1250, 10000);
        const auto frame = source.snapshot().frame;
        QCOMPARE(frame.surfaceFormat().colorTransfer(), transfer);
        const auto sample = factory.make(frame);
        QVERIFY(sample);
        const auto pixels = CMSampleBufferGetImageBuffer(sample);
        QCOMPARE(CVPixelBufferGetPixelFormatType(pixels), OSType(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange));
        QCOMPARE(CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly), kCVReturnSuccess);
        const auto unlock = qScopeGuard([&]{ CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly); });
        const auto *luma = static_cast<const quint16 *>(CVPixelBufferGetBaseAddressOfPlane(pixels, 0));
        QVERIFY(std::abs(int(luma[16] >> 6) - 64) <= 2);
        QVERIFY(std::abs(int(luma[240] >> 6) - 730) <= 2);
        QSKIP("Verified the compatible HDR base layer; native dynamic decoding is unavailable for this profile");
    }
    // All fixtures use the same twelve original 4 fps gray-band frames.
    // Let Apple's native decoder identify the per-frame attachments added by
    // each format; their names and binary representation are platform details.
    const auto reference = appleFrameMetadata(path);
    const auto base = appleFrameMetadata(staticPath);
    QCOMPARE(reference.size(), 12);
    QCOMPARE(base.keys(), reference.keys());
    QSet<QString> dynamicKeys;
    for (auto it = reference.cbegin(); it != reference.cend(); ++it) {
        for (const QString &key : it.value().keys())
            if (!base.value(it.key()).contains(key)) dynamicKeys.insert(key);
    }
    if (dynamicKeys.isEmpty()) QSKIP("The system decoder did not expose per-frame metadata for this format");
    const auto selected = [&](const AttachmentBytes &all) {
        AttachmentBytes dynamic;
        for (const auto &key : dynamicKeys) {
            const auto it = all.constFind(key);
            if (it != all.constEnd()) dynamic.insert(key, *it);
        }
        return dynamic;
    };
    QCOMPARE(selected(reference.value(250)).isEmpty(), hasUntaggedLeadIn);
    QVERIFY(!selected(reference.value(1250)).isEmpty());
    QVERIFY(!selected(reference.value(2250)).isEmpty());
    QVERIFY(selected(reference.value(1250)) != selected(reference.value(2250)));
    // Seek both ways between grades, then back to the first shot. The HDR10+
    // fixture starts with a full second of static HDR10 before its first SEI.
    for (const int pts : {250, 1250, 2250, 1250, 250, 2250}) {
        player.setPosition(pts + 50);
        QTRY_COMPARE_WITH_TIMEOUT(source.snapshot().frame.startTime() / 1000, pts, 10000);
        const auto sample = factory.make(source.snapshot().frame);
        QVERIFY(sample);
        QCOMPARE(selected(propagatedData(CMSampleBufferGetImageBuffer(sample))), selected(reference.value(pts)));
    }
    // Keep the same player and sample factory when switching to an HDR10 file.
    const auto revision = source.revision();
    player.setSource(QUrl::fromLocalFile(staticPath));
    player.pause();
    QTRY_VERIFY_WITH_TIMEOUT(source.revision() > revision && player.isSeekable() && source.hasFrame(), 10000);
    player.setPosition(800);
    QTRY_COMPARE_WITH_TIMEOUT(source.snapshot().frame.startTime() / 1000, 750, 10000);
    const auto sample = factory.make(source.snapshot().frame);
    QVERIFY(sample);
    QVERIFY(selected(propagatedData(CMSampleBufferGetImageBuffer(sample))).isEmpty());
}

void checkMacHlgAmbientViewingEnvironment(const QString &path)
{
    using namespace ZoinGallery;
    QVERIFY(QFile::exists(path));
    QMediaPlayer player;
    ViewerVideoFrameSource source;
    player.setVideoSink(qobject_cast<QVideoSink *>(source.sink()));
    player.setSource(QUrl::fromLocalFile(path));
    player.pause();
    QTRY_VERIFY_WITH_TIMEOUT(source.hasFrame(), 10000);
    const auto frame = source.snapshot().frame;
    QCOMPARE(frame.surfaceFormat().colorTransfer(), QVideoFrameFormat::ColorTransfer_STD_B67);
    MacVideoSample factory;
    const auto sample = factory.make(frame);
    QVERIFY(sample);
    auto data = static_cast<CFDataRef>(CMFormatDescriptionGetExtension(CMSampleBufferGetFormatDescription(sample),
                                                                     kCMFormatDescriptionExtension_AmbientViewingEnvironment));
    QVERIFY(data && CFGetTypeID(data) == CFDataGetTypeID());
    // H.265 D.2.39: 10 lux at D65. The native format description carries the
    // exact SEI payload in big-endian order for the system's HLG presentation.
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data)),
             QByteArray::fromHex("000186a03d134042"));
}

void checkMacVideoDeadlines(const QString &path)
{
    using namespace ZoinGallery;
    ViewerVideoFrameSource source;
    QMediaPlayer player;
    auto *playbackSink = ViewerVideoFrameSource::createPlaybackSink(&source);
    auto *observedSink = qobject_cast<QVideoSink *>(source.sink());
    QObject::connect(playbackSink, &QVideoSink::videoFrameChanged, observedSink, &QVideoSink::setVideoFrame);
    player.setVideoSink(playbackSink);
    player.setSource(QUrl::fromLocalFile(path));
    player.pause();
    QTRY_VERIFY_WITH_TIMEOUT(source.hasFrame(), 5000);
    MacVideoSample samples;
    const auto paused = source.snapshot().frame;
    const auto immediate = samples.make(paused);
    QVERIFY(immediate);
    if (!CVBufferHasAttachment(CMSampleBufferGetImageBuffer(immediate), CFSTR("F4DecoderSurfaceOwner")))
        QSKIP("Requires the VideoToolbox hardware decoder");
    const auto displaysImmediately = [](CMSampleBufferRef sample) {
        const auto array = CMSampleBufferGetSampleAttachmentsArray(sample, false);
        return array && CFDictionaryContainsKey(static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(array, 0)),
                                                kCMSampleAttachmentKey_DisplayImmediately);
    };
    QVERIFY(displaysImmediately(immediate));
    QVERIFY(!samples.upcoming(paused));

    std::optional<MacVideoSample::Upcoming> expected;
    bool matched = false;
    const auto connection = QObject::connect(&source, &ViewerVideoFrameSource::frameChanged, &source, [&] {
        const auto current = source.snapshot().frame;
        if (expected && samples.sameDecoderFrame(expected->frame, current))
            matched = true;
        expected = samples.upcoming(current);
        if (!expected) return;
        QVERIFY(expected->frame.startTime() > current.startTime());
        const auto timed = samples.make(expected->frame, expected->hostTime);
        QVERIFY(timed);
        QVERIFY(!displaysImmediately(timed));
        QCOMPARE(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(timed), expected->hostTime), 0);
        QCOMPARE(CMTimeCompare(CMSampleBufferGetDuration(timed), CMTimeMake(1, 4)), 0);
        QVERIFY(CVBufferHasAttachment(CMSampleBufferGetImageBuffer(timed), CFSTR("F4DecoderSurfaceOwner")));
    });
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(matched, 3000);
    QObject::disconnect(connection);
    player.pause();
    player.setPosition(2050);
    QTRY_COMPARE_WITH_TIMEOUT(source.snapshot().frame.startTime(), 2000000, 3000);
    QVERIFY(!samples.upcoming(source.snapshot().frame));
    QVERIFY(!samples.sameDecoderFrame(paused, source.snapshot().frame));

    QVideoSink ordinarySink;
    player.setVideoSink(&ordinarySink);
    player.setPosition(0);
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(ordinarySink.videoFrame().startTime() >= 250000
                             && ordinarySink.videoFrame().startTime() < 1000000, 3000);
    QVERIFY(!samples.upcoming(ordinarySink.videoFrame()));
    player.stop();

    // Exercise transport through the gallery mailbox as well. A held frame
    // must revoke its future deadline before returning to the event loop.
    ViewerVideoFrameSource gallery;
    VideoPlaybackController controller({});
    controller.setFrameSource(&gallery);
    QTRY_VERIFY(controller.available());
    controller.openSource({{QStringLiteral("path"), path}}, QStringLiteral("autoplay-muted"));
    const auto scheduled = [&] {
        const auto current = gallery.snapshot();
        return current.allowUpcoming && samples.upcoming(current.frame).has_value();
    };
    QTRY_VERIFY_WITH_TIMEOUT(scheduled(), 3000);
    controller.playPause();
    QVERIFY(!gallery.snapshot().allowUpcoming);
    QTRY_VERIFY(!controller.playing());
    controller.seekTo(1300);
    QTRY_COMPARE_WITH_TIMEOUT(gallery.snapshot().frame.startTime(), 1250000, 3000);
    QVERIFY(!gallery.snapshot().allowUpcoming);
    controller.playPause();
    QTRY_VERIFY_WITH_TIMEOUT(scheduled(), 3000);
    controller.seekBy(-1000);
    QVERIFY(!gallery.snapshot().allowUpcoming);
    QTRY_VERIFY_WITH_TIMEOUT(scheduled(), 3000);
    controller.setPresentationVisible(false);
    QVERIFY(!gallery.snapshot().allowUpcoming);
    controller.stop();
    QVERIFY(!gallery.hasFrame());
}

void checkMacVideoSamples(const QString &path)
{
    using namespace ZoinGallery;
    QVERIFY(QFile::exists(path));
    ViewerVideoFrameSource source;
    QMediaPlayer player;
    player.setVideoSink(qobject_cast<QVideoSink *>(source.sink()));
    player.setSource(QUrl::fromLocalFile(path));
    player.pause();
    QTRY_VERIFY_WITH_TIMEOUT(source.hasFrame(), 10000);
    MacVideoSample factory;
    const auto decoded = factory.make(source.snapshot().frame);
    QVERIFY(decoded);
    auto desc = CMSampleBufferGetFormatDescription(decoded);
    const auto mastering = static_cast<CFDataRef>(CMFormatDescriptionGetExtension(desc, kCMFormatDescriptionExtension_MasteringDisplayColorVolume));
    const auto light = static_cast<CFDataRef>(CMFormatDescriptionGetExtension(desc, kCMFormatDescriptionExtension_ContentLightLevelInfo));
    QVERIFY(mastering && CFDataGetLength(mastering) == 24);
    QVERIFY(light && CFDataGetLength(light) == 4);
    auto bytes = CFDataGetBytePtr(mastering);
    const std::array<int,8> primariesAndWhite{13250,34500,7500,3000,34000,16000,15635,16450};
    for (size_t i = 0; i < primariesAndWhite.size(); ++i)
        QCOMPARE(qFromBigEndian<quint16>(bytes+2*i), primariesAndWhite[i]);
    QCOMPARE(qFromBigEndian<quint32>(bytes+16), 10000000u);
    QCOMPARE(qFromBigEndian<quint32>(bytes+20), 50u);
    QCOMPARE(qFromBigEndian<quint16>(CFDataGetBytePtr(light)), 1186);
    QCOMPARE(qFromBigEndian<quint16>(CFDataGetBytePtr(light)+2), 358);

    using F = QVideoFrameFormat;
    for (auto storage : {F::Format_YUV420P10, F::Format_P010, F::Format_P016}) {
        for (auto range : {F::ColorRange_Full, F::ColorRange_Video}) {
            F format(QSize(17,9), storage);
            format.setColorRange(range);
            format.setColorSpace(F::ColorSpace_BT2020);
            format.setColorTransfer(F::ColorTransfer_STD_B67);
            QVideoFrame frame(format);
            QVERIFY(frame.map(QVideoFrame::WriteOnly));
            const int shift = storage == F::Format_YUV420P10 ? 0 : 6;
            for (int p = 0; p < frame.planeCount(); ++p) {
                auto *data = reinterpret_cast<quint16 *>(frame.bits(p));
                std::fill_n(data, frame.mappedBytes(p)/2, quint16((p == 0 ? 730 : 490) << shift));
                if (p == 2) std::fill_n(data, frame.mappedBytes(p)/2, quint16(560));
                if (p == 1 && frame.planeCount() == 2) {
                    for (int i = 1; i < frame.mappedBytes(p)/2; i+=2) data[i] = 560 << 6;
                }
            }
            const auto details = QString("storage %1, Y %2/%3, U %4/%5, V %6/%7")
                .arg(int(storage)).arg(frame.mappedBytes(0)).arg(frame.bytesPerLine(0))
                .arg(frame.mappedBytes(1)).arg(frame.bytesPerLine(1))
                .arg(frame.mappedBytes(2)).arg(frame.bytesPerLine(2));
            frame.unmap();
            auto sample = factory.make(frame);
            QVERIFY2(sample, qPrintable(details));
            auto pix = CMSampleBufferGetImageBuffer(sample);
            QCOMPARE(CVPixelBufferGetPixelFormatType(pix), OSType(range == F::ColorRange_Full
                ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange));
            QCOMPARE(CVPixelBufferLockBaseAddress(pix,kCVPixelBufferLock_ReadOnly), kCVReturnSuccess);
            auto unlock = qScopeGuard([&]{ CVPixelBufferUnlockBaseAddress(pix,kCVPixelBufferLock_ReadOnly); });
            auto *luma = static_cast<const quint16 *>(CVPixelBufferGetBaseAddressOfPlane(pix,0));
            auto *chroma = static_cast<const quint16 *>(CVPixelBufferGetBaseAddressOfPlane(pix,1));
            QCOMPARE(luma[16],730 << 6);
            QCOMPARE(chroma[16],490 << 6);
            QCOMPARE(chroma[17],560 << 6);
            QVERIFY(!CMFormatDescriptionGetExtension(CMSampleBufferGetFormatDescription(sample),
                    kCMFormatDescriptionExtension_MasteringDisplayColorVolume));
        }
    }
}
