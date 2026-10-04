#include "ViewerResampleEffect.h"
#include <ZoinGallery/MediaTimingTrace.h>

#include <QDebug>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QMatrix4x4>
#include <rhi/qrhi.h>

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
#include <QtMultimedia/private/qvideoframetexturepool_p.h>
#include <QtMultimedia/private/qvideotexturehelper_p.h>
#include <QtQuick/private/qsgplaintexture_p.h>
#include "VideoFrameGeometry.h"
using ZoinGallery::ViewerVideoFrameSource;
#endif

#include <cstddef>
#include <cstring>
#include <memory>
#include <atomic>
#include <array>

namespace {

// std140 layout shared by viewer_resample.vert and viewer_resample.frag.
struct alignas(16) ResampleUniforms {
    float matrix[16];
    float opacity;
    float padding0;
    float viewportSize[2];
    float checkerboardOffset[2];
    qint32 showCheckerboard;
    qint32 checkerboardSize;
    float borderRadius;
    qint32 intermediate;
    qint32 pixelAlignedIdentity;
    qint32 nearestNeighbor;
    float sourceExtent[2];
    qint32 pixelAligned;
    float padding2;
    float itemSize[2];
    float padding3[2];
    float framebufferRect[4];
    float framebufferYDirection;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    qint32 videoPadding0[3];
    float videoColorMatrix[16];
    float videoFrameSize[2];
    qint32 videoPadding1[2];
    float videoViewport[4];
    qint32 videoPixelInfo[4];
    qint32 videoTextureFormats[4];
    qint32 videoEnabled;
    qint32 videoPadding2[3];
#endif
};

static_assert(offsetof(ResampleUniforms, opacity) == 64);
static_assert(offsetof(ResampleUniforms, viewportSize) == 72);
static_assert(offsetof(ResampleUniforms, checkerboardOffset) == 80);
static_assert(offsetof(ResampleUniforms, showCheckerboard) == 88);
static_assert(offsetof(ResampleUniforms, checkerboardSize) == 92);
static_assert(offsetof(ResampleUniforms, borderRadius) == 96);
static_assert(offsetof(ResampleUniforms, intermediate) == 100);
static_assert(offsetof(ResampleUniforms, pixelAlignedIdentity) == 104);
static_assert(offsetof(ResampleUniforms, sourceExtent) == 112);
static_assert(offsetof(ResampleUniforms, pixelAligned) == 120);
static_assert(offsetof(ResampleUniforms, itemSize) == 128);
static_assert(offsetof(ResampleUniforms, framebufferRect) == 144);
static_assert(offsetof(ResampleUniforms, framebufferYDirection) == 160);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
static_assert(offsetof(ResampleUniforms, videoColorMatrix) == 176);
static_assert(offsetof(ResampleUniforms, videoFrameSize) == 240);
static_assert(offsetof(ResampleUniforms, videoViewport) == 256);
static_assert(offsetof(ResampleUniforms, videoPixelInfo) == 272);
static_assert(offsetof(ResampleUniforms, videoTextureFormats) == 288);
static_assert(offsetof(ResampleUniforms, videoEnabled) == 304);
#endif
constexpr qsizetype BaseUniformByteSize = offsetof(ResampleUniforms, framebufferYDirection)
                                         + sizeof(float);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
constexpr qsizetype VideoUniformByteSize = BaseUniformByteSize
    + sizeof(ResampleUniforms::videoPadding0)
    + sizeof(ResampleUniforms::videoColorMatrix)
    + sizeof(ResampleUniforms::videoFrameSize)
    + sizeof(ResampleUniforms::videoPadding1)
    + sizeof(ResampleUniforms::videoViewport)
    + sizeof(ResampleUniforms::videoPixelInfo)
    + sizeof(ResampleUniforms::videoTextureFormats)
    + sizeof(ResampleUniforms::videoEnabled);
static_assert(VideoUniformByteSize
              == offsetof(ResampleUniforms, videoEnabled)
                     + sizeof(ResampleUniforms::videoEnabled));
#endif

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
int videoTextureFormatCode(int value)
{
    switch (static_cast<QRhiTexture::Format>(value)) {
    case QRhiTexture::R8: return 1;
    case QRhiTexture::R16: return 2;
    case QRhiTexture::RG8: return 3;
    case QRhiTexture::RG16: return 4;
    case QRhiTexture::RGBA8: return 5;
    case QRhiTexture::BGRA8: return 6;
    default: return 0;
    }
}
#endif

class ResampleMaterial;

class ResampleShader final : public QSGMaterialShader
{
public:
    explicit ResampleShader(bool videoShader)
    {
        setShaderFileName(VertexStage,
            QStringLiteral(":/ZoinGallery/resources/viewer_resample.vert.qsb"));
        setShaderFileName(FragmentStage,
            videoShader
                ? QStringLiteral(":/ZoinGallery/resources/viewer_resample_video.frag.qsb")
                : QStringLiteral(":/ZoinGallery/resources/viewer_resample.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial,
                           QSGMaterial *oldMaterial) override;
    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                            QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override;
};

class ResampleMaterial final : public QSGMaterial
{
public:
    explicit ResampleMaterial(QQuickWindow *window
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                              , bool useVideoShader
#endif
                              )
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        : videoShader(useVideoShader)
#endif
    {
        setFlag(Blending | RequiresFullMatrix, true);
        QImage transparent(1, 1, QImage::Format_RGBA8888_Premultiplied);
        transparent.fill(Qt::transparent);
        fallbackTexture.reset(window->createTextureFromImage(transparent));
    }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType imageType;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        static QSGMaterialType videoType;
        return videoShader ? &videoType : &imageType;
#else
        return &imageType;
#endif
    }

    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override
    {
        return new ResampleShader(videoShader);
    }

    QPointer<QSGTextureProvider> provider;
    std::unique_ptr<QSGTexture> fallbackTexture;
    QSizeF viewportSize;
    QSizeF sourceExtent;
    QSizeF itemSize;
    QVector2D checkerboardOffset;
    bool showCheckerboard = false;
    int checkerboardSize = 4;
    qreal borderRadius = 0;
    bool intermediate = false;
    bool pixelAligned = false;
    bool pixelAlignedIdentity = false;
    bool nearestNeighbor = false;
    QSGTexture *lastTracedTexture = nullptr;
    QSize lastTracedTextureSize;
    bool videoShader = false;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    QPointer<ViewerVideoFrameSource> videoSource;
    QVideoFrame frame;
    quint64 frameRevision = 0;
    QVideoFrameTexturePool videoTexturePool;
    std::array<std::unique_ptr<QSGPlainTexture>, 3> videoPlaneTextures;
#endif
};

bool ResampleShader::updateUniformData(RenderState &state, QSGMaterial *newMaterial,
                                      QSGMaterial *)
{
    const auto *material = static_cast<ResampleMaterial *>(newMaterial);
    QByteArray *buffer = state.uniformData();
    qsizetype uniformByteSize = BaseUniformByteSize;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (material->videoShader)
        uniformByteSize = VideoUniformByteSize;
#endif
    if (buffer->size() < uniformByteSize)
        return false;

    ResampleUniforms uniforms{};
    const QMatrix4x4 matrix = state.combinedMatrix();
    std::memcpy(uniforms.matrix, matrix.constData(), sizeof(uniforms.matrix));
    uniforms.opacity = state.opacity();
    uniforms.viewportSize[0] = float(material->viewportSize.width());
    uniforms.viewportSize[1] = float(material->viewportSize.height());
    uniforms.checkerboardOffset[0] = material->checkerboardOffset.x();
    uniforms.checkerboardOffset[1] = material->checkerboardOffset.y();
    uniforms.showCheckerboard = material->showCheckerboard;
    uniforms.checkerboardSize = material->checkerboardSize;
    uniforms.borderRadius = float(material->borderRadius);
    uniforms.intermediate = material->intermediate;
    uniforms.pixelAlignedIdentity = material->pixelAlignedIdentity;
    uniforms.nearestNeighbor = material->nearestNeighbor;
    uniforms.sourceExtent[0] = float(material->sourceExtent.width());
    uniforms.sourceExtent[1] = float(material->sourceExtent.height());
    uniforms.pixelAligned = material->pixelAligned;
    uniforms.itemSize[0] = float(material->itemSize.width());
    uniforms.itemSize[1] = float(material->itemSize.height());

    // This is the active draw target, including layer and render-control
    // targets. Nominal DPR and rounded logical window sizes are insufficient.
    const QRect viewport = state.viewportRect();
    uniforms.framebufferRect[0] = float(viewport.x());
    uniforms.framebufferRect[1] = float(viewport.y());
    uniforms.framebufferRect[2] = float(viewport.width());
    uniforms.framebufferRect[3] = float(viewport.height());
    const QRhi *rhi = state.rhi();
    uniforms.framebufferYDirection = rhi->isYUpInFramebuffer() == rhi->isYUpInNDC()
        ? 1.0f : -1.0f;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (material->videoSource && material->frame.isValid()) {
        const QVideoFrameFormat format = material->frame.surfaceFormat();
        if (format.isValid()) {
            QByteArray videoData(sizeof(QVideoTextureHelper::UniformData), '\0');
            QVideoTextureHelper::updateUniformData(&videoData, state.rhi(), format,
                material->frame, QMatrix4x4(), 1.0f);
            const auto *video = reinterpret_cast<const QVideoTextureHelper::UniformData *>(
                videoData.constData());
            std::memcpy(uniforms.videoColorMatrix, video->colorMatrix,
                        sizeof(uniforms.videoColorMatrix));
            const int rotation = int(material->frame.rotation());
            const QRect viewport =
                ZoinGallery::VideoFrameGeometry::visibleViewport(
                    material->frame);
            const QSize displaySize =
                ZoinGallery::VideoFrameGeometry::displaySize(
                    material->frame);
            uniforms.videoFrameSize[0] = float(displaySize.width());
            uniforms.videoFrameSize[1] = float(displaySize.height());
            uniforms.videoViewport[0] = float(viewport.x());
            uniforms.videoViewport[1] = float(viewport.y());
            uniforms.videoViewport[2] = float(viewport.width());
            uniforms.videoViewport[3] = float(viewport.height());
            const auto *description = QVideoTextureHelper::textureDescription(
                format.pixelFormat());
            uniforms.videoPixelInfo[0] = int(format.pixelFormat());
            uniforms.videoPixelInfo[1] = description ? description->nplanes : 0;
            uniforms.videoPixelInfo[2] = video->redOrAlphaIndex;
            uniforms.videoPixelInfo[3] = rotation;
            for (int plane = 0; plane < 3; ++plane) {
                uniforms.videoTextureFormats[plane] = plane < uniforms.videoPixelInfo[1]
                    ? videoTextureFormatCode(video->planeFormats[plane]) : 0;
            }
            uniforms.videoTextureFormats[3] =
                format.isMirrored() || material->frame.mirrored() ? 1 : 0;
            uniforms.videoEnabled = uniforms.videoPixelInfo[1] > 0;
        }
    }
#endif
    if (qEnvironmentVariableIsSet("F4_VIEWER_RESAMPLE_DEBUG")) {
        static std::atomic<int> emitted = 0;
        if (emitted.fetch_add(1, std::memory_order_relaxed) < 24) {
            qInfo().noquote() << QStringLiteral(
                "[FIX:viewer-resample-dpr] targetDpr=%1 viewport=(%2,%3 %4x%5) "
                "requested=%6x%7 item=%8x%9 aligned=%10 intermediate=%11")
                .arg(state.devicePixelRatio())
                .arg(viewport.x()).arg(viewport.y())
                .arg(viewport.width()).arg(viewport.height())
                .arg(material->viewportSize.width())
                .arg(material->viewportSize.height())
                .arg(material->itemSize.width()).arg(material->itemSize.height())
                .arg(material->pixelAligned).arg(material->intermediate);
        }
    }
    std::memcpy(buffer->data(), &uniforms, uniformByteSize);
    return true;
}

void ResampleShader::updateSampledImage(RenderState &state, int binding,
                                       QSGTexture **texture, QSGMaterial *newMaterial,
                                       QSGMaterial *)
{
    if (binding < 1 || binding > 3)
        return;
    auto *material = static_cast<ResampleMaterial *>(newMaterial);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    QSGTexture *selected = nullptr;
    if (material->videoSource && material->frame.isValid()) {
        if (material->videoTexturePool.texturesDirty()) {
            auto *textures = material->videoTexturePool.updateTextures(
                *state.rhi(), *state.resourceUpdateBatch());
            for (int plane = 0; plane < 3; ++plane) {
                if (plane >= material->videoPlaneTextures.size())
                    break;
                if (!material->videoPlaneTextures[plane])
                    material->videoPlaneTextures[plane] =
                        std::make_unique<QSGPlainTexture>();
                QRhiTexture *rhiTexture = textures ? textures->texture(plane) : nullptr;
                auto *plain = material->videoPlaneTextures[plane].get();
                plain->setOwnsTexture(false);
                plain->setTexture(rhiTexture);
                if (rhiTexture)
                    plain->setTextureSize(rhiTexture->pixelSize());
                plain->setHasAlphaChannel(false);
            }
        }
        const int plane = binding - 1;
        selected = material->videoPlaneTextures[plane].get();
    } else if (binding == 1) {
        selected = material->provider ? material->provider->texture() : nullptr;
    }
#else
    QSGTexture *selected = binding == 1 && material->provider
        ? material->provider->texture() : nullptr;
#endif
    if (selected && selected->isAtlasTexture()) {
        selected->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        // Qt's atlas texture owns and caches the extracted texture. Use
        // the current update batch so the upload precedes the extraction.
        selected = selected->removedFromAtlas(state.resourceUpdateBatch());
    }
    if (!selected)
        selected = material->fallbackTexture.get();
    if (selected) {
        selected->setFiltering(QSGTexture::Linear);
        selected->setMipmapFiltering(QSGTexture::None);
        selected->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        selected->setVerticalWrapMode(QSGTexture::ClampToEdge);
        if (ZoinGallery::MediaTimingTrace::enabled() && binding == 1
                && (material->lastTracedTexture != selected
                    || material->lastTracedTextureSize != selected->textureSize())) {
            ZoinGallery::MediaTimingTrace::Span span(
                QStringLiteral("qt.gallery.viewer.texture_selected"), {
                    {QStringLiteral("inputWidth"), selected->textureSize().width()},
                    {QStringLiteral("inputHeight"), selected->textureSize().height()},
                    {QStringLiteral("outputWidth"), material->viewportSize.width()},
                    {QStringLiteral("outputHeight"), material->viewportSize.height()},
                    {QStringLiteral("intermediate"), material->intermediate},
                    {QStringLiteral("nearestNeighbor"), material->nearestNeighbor},
                });
            selected->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
            material->lastTracedTexture = selected;
            material->lastTracedTextureSize = selected->textureSize();
        } else {
            selected->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        }
    }
    *texture = selected;
}

class ResampleNode final : public QObject, public QSGGeometryNode
{
public:
    explicit ResampleNode(QQuickWindow *window
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                          , bool useVideoShader
#endif
                          )
        : geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4),
          material(window
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                   , useVideoShader
#endif
                   )
    {
        geometry.setDrawingMode(QSGGeometry::DrawTriangleStrip);
        setGeometry(&geometry);
        setMaterial(&material);
        setFlag(UsePreprocess, true);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        frameEndConnection = QObject::connect(window, &QQuickWindow::afterFrameEnd,
            this, [this] { material.videoTexturePool.onFrameEndInvoked(); },
            Qt::DirectConnection);
#endif
    }

    void setProvider(QSGTextureProvider *provider)
    {
        if (material.provider == provider)
            return;
        QObject::disconnect(textureChanged);
        QObject::disconnect(providerDestroyed);
        material.provider = provider;
        if (provider) {
            textureChanged = QObject::connect(provider, &QSGTextureProvider::textureChanged,
                this, [this] { markDirty(DirtyMaterial); }, Qt::DirectConnection);
            providerDestroyed = QObject::connect(provider, &QObject::destroyed,
                this, [this] { material.provider = nullptr; markDirty(DirtyMaterial); },
                Qt::DirectConnection);
        }
        markDirty(DirtyMaterial);
    }

    void preprocess() override
    {
        QSGTexture *texture = material.provider ? material.provider->texture() : nullptr;
        auto *dynamic = qobject_cast<QSGDynamicTexture *>(texture);
        if (dynamic && dynamic->updateTexture())
            markDirty(DirtyMaterial);
    }

    QSGGeometry geometry;
    ResampleMaterial material;
    QMetaObject::Connection textureChanged;
    QMetaObject::Connection providerDestroyed;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    QMetaObject::Connection frameEndConnection;
#endif
};

} // namespace

ViewerResampleEffect::ViewerResampleEffect(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void ViewerResampleEffect::setSource(QQuickItem *source)
{
    if (m_source == source)
        return;
    QObject::disconnect(m_sourceDestroyed);
    QObject::disconnect(m_sourceWindowChanged);
    m_source = source;
    if (source) {
        m_sourceDestroyed = connect(source, &QObject::destroyed, this, [this] {
            m_source = nullptr;
            emit sourceChanged();
            update();
        });
        m_sourceWindowChanged = connect(source, &QQuickItem::windowChanged,
            this, [this] { update(); });
    }
    emit sourceChanged();
    update();
}

void ViewerResampleEffect::setViewportSize(QSizeF size)
{
    if (m_viewportSize == size)
        return;
    m_viewportSize = size;
    emit viewportSizeChanged();
    update();
}

void ViewerResampleEffect::setSourceExtent(QSizeF size)
{
    if (m_sourceExtent == size)
        return;
    m_sourceExtent = size;
    emit sourceExtentChanged();
    update();
}

void ViewerResampleEffect::setCheckerboardOffset(QVector2D offset)
{
    if (m_checkerboardOffset == offset)
        return;
    m_checkerboardOffset = offset;
    emit checkerboardOffsetChanged();
    update();
}

void ViewerResampleEffect::setShowCheckerboard(bool enabled)
{
    if (m_showCheckerboard == enabled)
        return;
    m_showCheckerboard = enabled;
    emit showCheckerboardChanged();
    update();
}

void ViewerResampleEffect::setCheckerboardSize(int size)
{
    if (m_checkerboardSize == size)
        return;
    m_checkerboardSize = size;
    emit checkerboardSizeChanged();
    update();
}

void ViewerResampleEffect::setBorderRadius(qreal radius)
{
    if (m_borderRadius == radius)
        return;
    m_borderRadius = radius;
    emit borderRadiusChanged();
    update();
}

void ViewerResampleEffect::setIntermediate(bool enabled)
{
    if (m_intermediate == enabled)
        return;
    m_intermediate = enabled;
    emit intermediateChanged();
    update();
}

void ViewerResampleEffect::setPixelAligned(bool enabled)
{
    if (m_pixelAligned == enabled)
        return;
    m_pixelAligned = enabled;
    emit pixelAlignedChanged();
    update();
}

void ViewerResampleEffect::setNearestNeighbor(bool enabled)
{
    if (m_nearestNeighbor == enabled)
        return;
    m_nearestNeighbor = enabled;
    emit nearestNeighborChanged();
    update();
}

void ViewerResampleEffect::setPixelAlignedIdentity(bool enabled)
{
    if (m_pixelAlignedIdentity == enabled)
        return;
    m_pixelAlignedIdentity = enabled;
    emit pixelAlignedIdentityChanged();
    update();
}

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
QObject *ViewerResampleEffect::videoSourceObject() const
{
    return m_videoSource;
}

void ViewerResampleEffect::setVideoSourceObject(QObject *object)
{
    auto *source = qobject_cast<ViewerVideoFrameSource *>(object);
    if (m_videoSource.data() == source)
        return;
    QObject::disconnect(m_videoSourceDestroyed);
    QObject::disconnect(m_videoSourceFrameChanged);
    m_videoSource = source;
    if (source) {
        m_videoSourceDestroyed = connect(source, &QObject::destroyed, this, [this] {
            m_videoSource = nullptr;
            emit videoSourceChanged();
            update();
        });
        m_videoSourceFrameChanged = connect(source,
            &ViewerVideoFrameSource::frameChanged, this, [this] { update(); });
    }
    emit videoSourceChanged();
    update();
}
#else
QObject *ViewerResampleEffect::videoSourceObject() const
{
    return nullptr;
}

void ViewerResampleEffect::setVideoSourceObject(QObject *)
{
}
#endif

QSGNode *ViewerResampleEffect::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<ResampleNode *>(oldNode);
    if (width() <= 0 || height() <= 0 || !window()) {
        delete node;
        return nullptr;
    }

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    ViewerVideoFrameSource *videoSource = m_videoSource.data();
    const auto videoSnapshot = videoSource
        ? videoSource->snapshot() : ViewerVideoFrameSource::Snapshot{};
    const bool useVideoShader = videoSource && videoSnapshot.frame.isValid();
    // QSGMaterial::type() identifies an immutable render pipeline. Rebuild the
    // node when the first current video frame arrives instead of mutating the
    // image material into the video material in place.
    if (node && node->material.videoShader != useVideoShader) {
        delete node;
        node = nullptr;
    }
#endif

    if (!node)
        node = new ResampleNode(window()
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                                , useVideoShader
#endif
                                );
    node->setProvider(m_source && m_source->isTextureProvider()
        ? m_source->textureProvider() : nullptr);

    ResampleMaterial &material = node->material;
    material.viewportSize = m_viewportSize;
    material.sourceExtent = m_sourceExtent;
    material.itemSize = size();
    material.checkerboardOffset = m_checkerboardOffset;
    material.showCheckerboard = m_showCheckerboard;
    material.checkerboardSize = m_checkerboardSize;
    material.borderRadius = m_borderRadius;
    material.intermediate = m_intermediate;
    material.pixelAligned = m_pixelAligned;
    material.pixelAlignedIdentity = m_pixelAlignedIdentity;
    material.nearestNeighbor = m_nearestNeighbor;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (material.videoSource.data() != videoSource
        || material.frameRevision != videoSnapshot.revision) {
        material.videoSource = videoSource;
        material.frame = videoSnapshot.frame;
        material.frameRevision = videoSnapshot.revision;
        material.videoTexturePool.setCurrentFrame(videoSnapshot.frame);
    }
#endif
    QSGGeometry::updateTexturedRectGeometry(&node->geometry, boundingRect(), QRectF(0, 0, 1, 1));
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}

void ViewerResampleEffect::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}
