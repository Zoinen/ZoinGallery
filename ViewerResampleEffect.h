#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QSizeF>
#include <QVector2D>
#include <QtQmlIntegration/qqmlintegration.h>
#include <memory>

class QSGTextureProvider;

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
#include "ViewerVideoFrameSource.h"
#else
class QObject;
#endif

// Supplies the final image shader with the render target's physical viewport.
// The QML wrapper owns source selection and its retained half-size pyramid.
class ViewerResampleEffect : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QSizeF viewportSize READ viewportSize WRITE setViewportSize NOTIFY viewportSizeChanged)
    Q_PROPERTY(QSizeF sourceExtent READ sourceExtent WRITE setSourceExtent NOTIFY sourceExtentChanged)
    Q_PROPERTY(QVector2D checkerboardOffset READ checkerboardOffset WRITE setCheckerboardOffset NOTIFY checkerboardOffsetChanged)
    Q_PROPERTY(bool showCheckerboard READ showCheckerboard WRITE setShowCheckerboard NOTIFY showCheckerboardChanged)
    Q_PROPERTY(int checkerboardSize READ checkerboardSize WRITE setCheckerboardSize NOTIFY checkerboardSizeChanged)
    Q_PROPERTY(qreal borderRadius READ borderRadius WRITE setBorderRadius NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool intermediate READ intermediate WRITE setIntermediate NOTIFY intermediateChanged)
    Q_PROPERTY(bool pixelAligned READ pixelAligned WRITE setPixelAligned NOTIFY pixelAlignedChanged)
    Q_PROPERTY(bool nearestNeighbor READ nearestNeighbor WRITE setNearestNeighbor NOTIFY nearestNeighborChanged)
    Q_PROPERTY(bool hardwareSampling READ hardwareSampling WRITE setHardwareSampling NOTIFY hardwareSamplingChanged)
    Q_PROPERTY(bool pixelAlignedIdentity READ pixelAlignedIdentity WRITE setPixelAlignedIdentity NOTIFY pixelAlignedIdentityChanged)
    Q_PROPERTY(QObject *videoSource READ videoSourceObject
               WRITE setVideoSourceObject NOTIFY videoSourceChanged)
    Q_PROPERTY(QQuickItem *linearVideoSource READ linearVideoSource
               WRITE setLinearVideoSource NOTIFY linearVideoSourceChanged)
    Q_PROPERTY(bool convertVideoToLinear READ convertVideoToLinear
               WRITE setConvertVideoToLinear NOTIFY convertVideoToLinearChanged)
    Q_PROPERTY(bool linearVideoCacheSupported READ linearVideoCacheSupported
               NOTIFY linearVideoCacheSupportedChanged)
    Q_PROPERTY(QSize linearVideoCacheSize READ linearVideoCacheSize
               NOTIFY linearVideoCacheSupportedChanged)
    Q_PROPERTY(bool requestLinearVideoCache READ requestLinearVideoCache
               WRITE setRequestLinearVideoCache NOTIFY requestLinearVideoCacheChanged)
    Q_PROPERTY(bool referenceSampling READ referenceSampling CONSTANT)

public:
    explicit ViewerResampleEffect(QQuickItem *parent = nullptr);
    ~ViewerResampleEffect() override;
    bool isTextureProvider() const override;
    QSGTextureProvider *textureProvider() const override;

    QQuickItem *source() const { return m_source; }
    void setSource(QQuickItem *source);
    QSizeF viewportSize() const { return m_viewportSize; }
    void setViewportSize(QSizeF size);
    QSizeF sourceExtent() const { return m_sourceExtent; }
    void setSourceExtent(QSizeF size);
    QVector2D checkerboardOffset() const { return m_checkerboardOffset; }
    void setCheckerboardOffset(QVector2D offset);
    bool showCheckerboard() const { return m_showCheckerboard; }
    void setShowCheckerboard(bool enabled);
    int checkerboardSize() const { return m_checkerboardSize; }
    void setCheckerboardSize(int size);
    qreal borderRadius() const { return m_borderRadius; }
    void setBorderRadius(qreal radius);
    bool intermediate() const { return m_intermediate; }
    void setIntermediate(bool enabled);
    bool pixelAligned() const { return m_pixelAligned; }
    void setPixelAligned(bool enabled);
    bool nearestNeighbor() const { return m_nearestNeighbor; }
    void setNearestNeighbor(bool enabled);
    bool hardwareSampling() const { return m_hardwareSampling; }
    void setHardwareSampling(bool enabled);
    bool pixelAlignedIdentity() const { return m_pixelAlignedIdentity; }
    void setPixelAlignedIdentity(bool enabled);
    QObject *videoSourceObject() const;
    void setVideoSourceObject(QObject *source);
    QQuickItem *linearVideoSource() const { return m_linearVideoSource; }
    void setLinearVideoSource(QQuickItem *source);
    bool convertVideoToLinear() const { return m_convertVideoToLinear; }
    void setConvertVideoToLinear(bool enabled);
    bool linearVideoCacheSupported() const { return m_linearVideoCacheSupported; }
    QSize linearVideoCacheSize() const { return m_linearVideoCacheSize; }
    bool requestLinearVideoCache() const { return m_requestLinearVideoCache; }
    void setRequestLinearVideoCache(bool requested);
    bool referenceSampling() const { return m_referenceSampling; }
    Q_INVOKABLE bool windowProjectionMatches(qreal dpr) const;

signals:
    void sourceChanged();
    void viewportSizeChanged();
    void sourceExtentChanged();
    void checkerboardOffsetChanged();
    void showCheckerboardChanged();
    void checkerboardSizeChanged();
    void borderRadiusChanged();
    void intermediateChanged();
    void pixelAlignedChanged();
    void nearestNeighborChanged();
    void hardwareSamplingChanged();
    void pixelAlignedIdentityChanged();
    void videoSourceChanged();
    void linearVideoSourceChanged();
    void convertVideoToLinearChanged();
    void linearVideoCacheSupportedChanged();
    void requestLinearVideoCacheChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void releaseResources() override;

private:
    QPointer<QQuickItem> m_source;
    QMetaObject::Connection m_sourceDestroyed;
    QMetaObject::Connection m_sourceWindowChanged;
    QSizeF m_viewportSize{0, 0};
    QSizeF m_sourceExtent{0, 0};
    QVector2D m_checkerboardOffset;
    bool m_showCheckerboard = false;
    int m_checkerboardSize = 4;
    qreal m_borderRadius = 0;
    bool m_intermediate = false;
    bool m_pixelAligned = false;
    bool m_pixelAlignedIdentity = false;
    bool m_nearestNeighbor = false;
    bool m_hardwareSampling = false;
    QPointer<QQuickItem> m_linearVideoSource;
    QMetaObject::Connection m_linearVideoSourceDestroyed;
    bool m_convertVideoToLinear = false;
    bool m_linearVideoCacheSupported = false;
    QSize m_linearVideoCacheSize;
    bool m_requestLinearVideoCache = false;
    bool m_referenceSampling = false;
    bool m_disableLinearVideoCache = false;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    void releaseLinearProvider();
    mutable QPointer<QSGTextureProvider> m_videoLinearProvider;
    mutable QPointer<QQuickWindow> m_videoLinearWindow;
    mutable QMetaObject::Connection m_videoLinearInvalidated;
#endif
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    QPointer<ZoinGallery::ViewerVideoFrameSource> m_videoSource;
    QMetaObject::Connection m_videoSourceDestroyed;
    QMetaObject::Connection m_videoSourceFrameChanged;
#endif
};
