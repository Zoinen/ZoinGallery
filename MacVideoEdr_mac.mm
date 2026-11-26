#include "MacVideoEdr.h"
#include "MacVideoSample.h"
#include "ViewerVideoFrameSource.h"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QStringList>
#include <QtMultimedia/private/qmultimediautils_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <rhi/qrhi.h>

namespace ZoinGallery {
namespace {
Q_LOGGING_CATEGORY(videoLog, "zoin.gallery.edr", QtInfoMsg)

struct LayerTransaction {
    LayerTransaction() { [CATransaction begin]; [CATransaction setDisableActions:YES]; }
    ~LayerTransaction() { [CATransaction commit]; }
};

id<AVQueuedSampleBufferRendering> renderer(AVSampleBufferDisplayLayer *layer)
{
    if (@available(macOS 14, *)) return layer.sampleBufferRenderer;
    return layer;
}

void logFirstSample(const QVideoFrame &frame, CMSampleBufferRef sample)
{
    const auto pixel = CMSampleBufferGetImageBuffer(sample);
    QCFType<CFDictionaryRef> attachments = CVBufferCopyAttachments(pixel, kCVAttachmentMode_ShouldPropagate);
    auto tags = (NSDictionary *)static_cast<CFDictionaryRef>(attachments);
    QStringList payloads;
    for (NSString *key in tags) {
        if ([tags[key] isKindOfClass:NSData.class])
            payloads.append(QString::fromNSString(key));
    }
    payloads.sort();
    qCDebug(videoLog) << "[FIX:dynamic-hdr] AVFoundation video" << frame.size()
        << frame.surfaceFormat().colorTransfer()
        << "decoder surface" << bool(CVBufferHasAttachment(pixel, CFSTR("F4DecoderSurfaceOwner")))
        << "display metadata" << payloads;
}
} // namespace

class MacVideoEdr::Presentation {
public:
    ~Presentation() { detach(); }

    bool present(MacVideoEdr *item, ViewerVideoFrameSource *frames)
    {
        auto *window = item->window();
        if (!window || !frames || !window->isExposed() || !item->isVisible()
            || item->width() <= 0 || item->height() <= 0
            || window->rendererInterface()->graphicsApi() != QSGRendererInterface::Metal
            || QGuiApplication::platformName() != QStringLiteral("cocoa") || !window->handle())
            return false;

        QRectF clip(QPointF(0, 0), window->size());
        for (QQuickItem *parent = item; parent; parent = parent->parentItem()) {
            if (parent->opacity() != 1) return false;
            auto *priv = QQuickItemPrivate::get(parent);
            if (parent != item->parentItem() && priv->extra.isAllocated() && priv->extra->layer && priv->extra->layer->enabled())
                return false;
            if (parent->clip()) clip &= parent->mapRectToScene(parent->boundingRect());
        }
        if (!clip.intersects(item->mapRectToScene(item->boundingRect()))) return false;

        auto *view = reinterpret_cast<NSView *>(window->winId());
        const auto proxy = QRhi::updateSwapChainProxyData(QRhi::Metal, window);
        auto *metal = static_cast<CAMetalLayer *>(proxy.reserved[0]);
        // A separate backing surface is essential. An offscreen render target
        // or a Cocoa platform without the container uses Qt's SDR renderer.
        if (!view.window || !metal || metal.superlayer != view.layer || metal.opaque) return false;
        const auto snapshot = frames->snapshot();
        if (!snapshot.frame.isValid()) return false;
        const auto orientation = qNormalizedFrameTransformation(snapshot.frame);
        LayerTransaction transaction;
        if (clipLayer.superlayer != metal.superlayer || boundSource != frames) {
            detach();
            boundSource = frames;
            clipLayer = [CALayer new];
            clipLayer.masksToBounds = YES;
            layer = [AVSampleBufferDisplayLayer new];
            layer.videoGravity = AVLayerVideoGravityResize;
            [clipLayer addSublayer:layer];
            [metal.superlayer insertSublayer:clipLayer below:metal];
            const QPointer<MacVideoEdr> guard(item);
            auto wake = ^(NSNotification *) { if (guard) guard->polish(); };
            auto notifications = NSNotificationCenter.defaultCenter;
            if (@available(macOS 14.4, *)) {
                readyObserver = [notifications addObserverForName:AVSampleBufferDisplayLayerReadyForDisplayDidChangeNotification
                    object:layer queue:NSOperationQueue.mainQueue usingBlock:wake];
            }
            if (@available(macOS 14, *)) {
                flushObserver = [notifications addObserverForName:AVSampleBufferVideoRendererRequiresFlushToResumeDecodingDidChangeNotification
                    object:layer.sampleBufferRenderer queue:NSOperationQueue.mainQueue usingBlock:wake];
            } else {
                flushObserver = [notifications addObserverForName:AVSampleBufferDisplayLayerRequiresFlushToResumeDecodingDidChangeNotification
                    object:layer queue:NSOperationQueue.mainQueue usingBlock:wake];
            }
        }
        auto output = renderer(layer);
        NSError *error;
        bool needsFlush;
        if (@available(macOS 14, *)) {
            error = layer.sampleBufferRenderer.error;
            needsFlush = layer.sampleBufferRenderer.requiresFlushToResumeDecoding;
        } else {
            error = layer.error;
            needsFlush = layer.requiresFlushToResumeDecoding;
        }
        if (needsFlush) {
            [output flush];
            pending = {};
            revision = 0; // Re-submit the held frame, including during pause.
        } else if (error) {
            qCWarning(videoLog) << "Video renderer failed" << error.localizedDescription.UTF8String;
            return false;
        }
        if (revision != snapshot.revision) {
            if (!pending || !samples.sameDecoderFrame(pending->frame, snapshot.frame)
                    || CMTimeCompare(pending->hostTime, CMClockGetTime(CMClockGetHostTimeClock())) > 0) {
                // A seek, a coalesced mailbox delivery, or an untimed frame
                // supersedes any image previously queued on the old timeline.
                if (pending || !output.readyForMoreMediaData) [output flush];
                auto sample = samples.make(snapshot.frame);
                if (!sample) return false;
                [output enqueueSampleBuffer:sample];
                if (!revision && videoLog().isDebugEnabled()) logFirstSample(snapshot.frame, sample);
            }
            pending = {};
            if (snapshot.allowUpcoming && output.readyForMoreMediaData) {
                const auto next = samples.upcoming(snapshot.frame);
                // A geometry or orientation change takes effect with the
                // authoritative frame, after the viewer has updated its layout.
                if (next && next->frame.size() == snapshot.frame.size()
                        && next->frame.surfaceFormat().viewport() == snapshot.frame.surfaceFormat().viewport()
                        && qNormalizedFrameTransformation(next->frame) == orientation
                        && CMTimeCompare(next->hostTime, CMClockGetTime(CMClockGetHostTimeClock())) > 0) {
                    if (auto sample = samples.make(next->frame, next->hostTime)) {
                        [output enqueueSampleBuffer:sample];
                        pending = next;
                    }
                }
            }
            revision = snapshot.revision;
        }
        // NSView and its backing layer can have different flippedness.
        const auto hostPoint = [view](QPointF scene) {
            NSPoint point = NSMakePoint(scene.x(), view.isFlipped ? scene.y() : view.bounds.size.height - scene.y());
            return [view convertPointToLayer:point];
        };
        const auto p0 = hostPoint(clip.topLeft()), p1 = hostPoint(clip.bottomRight());
        const auto clipRect = CGRectMake(qMin(p0.x,p1.x), qMin(p0.y,p1.y), qAbs(p1.x-p0.x), qAbs(p1.y-p0.y));
        if (!CGRectEqualToRect(clipLayer.frame, clipRect)) clipLayer.frame = clipRect;
        auto coordinate = [&](QPointF point) {
            return [clipLayer convertPoint:hostPoint(item->mapToScene(point)) fromLayer:view.layer];
        };
        const auto center = QPointF(item->width()/2, item->height()/2);
        const auto pos = coordinate(center);
        QTransform transform;
        if (orientation.mirroredHorizontallyAfterRotation) transform.scale(-1, 1);
        transform.rotate(int(orientation.rotation));
        const auto dx = coordinate(center + transform.map(QPointF(1, 0)));
        const auto dy = coordinate(center + transform.map(QPointF(0, 1)));
        const CGFloat sign = hostPoint({0,1}).y > hostPoint({0,0}).y ? 1 : -1;
        const bool transpose = int(orientation.rotation) % 180 != 0;
        const auto bounds = CGRectMake(0, 0, transpose ? item->height() : item->width(),
                                            transpose ? item->width() : item->height());
        const auto affine = CGAffineTransformMake(dx.x-pos.x, dx.y-pos.y, (dy.x-pos.x)*sign, (dy.y-pos.y)*sign, 0, 0);
        if (!CGRectEqualToRect(layer.bounds, bounds)) layer.bounds = bounds;
        if (!CGPointEqualToPoint(layer.position, pos)) layer.position = pos;
        if (!CGAffineTransformEqualToTransform(layer.affineTransform, affine)) layer.affineTransform = affine;
        return true;
    }

    bool ready() const
    {
        if (@available(macOS 14.4, *)) return layer.readyForDisplay;
        return layer != nil;
    }

    void detach()
    {
        if (!layer) return;
        LayerTransaction transaction;
        if (readyObserver) [NSNotificationCenter.defaultCenter removeObserver:readyObserver];
        if (flushObserver) [NSNotificationCenter.defaultCenter removeObserver:flushObserver];
        readyObserver = nil;
        flushObserver = nil;
        [clipLayer removeFromSuperlayer];
        [renderer(layer) flush];
        [layer release];
        [clipLayer release];
        clipLayer = nil;
        layer = nil;
        pending = {};
        boundSource = nullptr;
        revision = 0;
    }
private:
    MacVideoSample samples;
    std::optional<MacVideoSample::Upcoming> pending;
    QPointer<ViewerVideoFrameSource> boundSource;
    CALayer *clipLayer = nil;
    AVSampleBufferDisplayLayer *layer = nil;
    id readyObserver = nil;
    id flushObserver = nil;
    quint64 revision = 0;
};

MacVideoEdr::MacVideoEdr(QQuickItem *parent) : QQuickItem(parent),
    m_presentation(std::make_unique<Presentation>())
{
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        m_presentation->detach();
        setActive(false);
        trackWindow();
    });
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (!isVisible()) {
            m_presentation->detach();
            setActive(false);
        }
        polish();
    });
}
MacVideoEdr::~MacVideoEdr() = default;

void MacVideoEdr::updatePolish()
{
    const bool attached = m_presentation->present(this, qobject_cast<ViewerVideoFrameSource *>(source()));
    if (!attached) m_presentation->detach();
    setActive(attached && m_presentation->ready());
}
} // namespace ZoinGallery
