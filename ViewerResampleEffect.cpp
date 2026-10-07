#include "ViewerResampleEffect.h"
#include <ZoinGallery/MediaTimingTrace.h>

#include <QDebug>
#include <QQuickWindow>
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
#include <QtQuick/private/qquickrendertarget_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgdefaultrendercontext_p.h>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include "ViewerLinearVideoTexture.h"
#endif
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
#include <functional>

namespace {

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
// Bound aggregate source-data storage as well as each layer. Retired GPU
// allocations may remain alive until Qt's in-flight frames finish.
constexpr quint64 MaxLinearVideoBytes = 9ULL * 1024 * 1024 * 4 * sizeof(float);
class LinearVideoReservation
{
public:
    ~LinearVideoReservation() { release(); }

    bool reserve(QRhi *rhi, QSize size)
    {
        if (!rhi || size.width() <= 0 || size.height() <= 0
                || size.width() > 4096 || size.height() > 4096)
            return false;
        const quint64 bytes = quint64(size.width()) * quint64(size.height()) * 4 * sizeof(float);
        if (bytes > MaxLinearVideoBytes)
            return false;
        if (m_rhi == rhi && m_bytes == bytes)
            return true;
        release();
        auto &pool = budget();
        QMutexLocker lock(&pool.mutex);
        const quint64 occupied = pool.bytes.value(rhi);
        if (bytes > MaxLinearVideoBytes - occupied)
            return false;
        pool.bytes.insert(rhi, occupied + bytes);
        m_rhi = rhi;
        m_bytes = bytes;
        return true;
    }

    void release()
    {
        if (!m_rhi)
            return;
        auto &pool = budget();
        QMutexLocker lock(&pool.mutex);
        const quint64 remaining = pool.bytes.value(m_rhi) - m_bytes;
        if (remaining)
            pool.bytes.insert(m_rhi, remaining);
        else
            pool.bytes.remove(m_rhi);
        m_rhi = nullptr;
        m_bytes = 0;
    }

private:
    struct Budget { QMutex mutex; QHash<QRhi *, quint64> bytes; };
    static Budget &budget() { static Budget value; return value; }
    QRhi *m_rhi = nullptr;
    quint64 m_bytes = 0;
};
#endif

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
    qint32 hardwareSampling;
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
static_assert(offsetof(ResampleUniforms, hardwareSampling) == 124);
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

void fillVideoUniforms(ResampleUniforms &uniforms, QRhi *rhi, const QVideoFrame &frame)
{
    const auto format = frame.surfaceFormat();
    if (!frame.isValid() || !format.isValid())
        return;
    QByteArray videoData(sizeof(QVideoTextureHelper::UniformData), '\0');
    QVideoTextureHelper::updateUniformData(&videoData, rhi, format, frame, QMatrix4x4(), 1.0f);
    const auto *video = reinterpret_cast<const QVideoTextureHelper::UniformData *>(videoData.constData());
    std::memcpy(uniforms.videoColorMatrix, video->colorMatrix, sizeof(uniforms.videoColorMatrix));
    const QRect viewport = ZoinGallery::VideoFrameGeometry::visibleViewport(frame);
    const QSize displaySize = ZoinGallery::VideoFrameGeometry::displaySize(frame);
    uniforms.videoFrameSize[0] = float(displaySize.width());
    uniforms.videoFrameSize[1] = float(displaySize.height());
    uniforms.videoViewport[0] = float(viewport.x());
    uniforms.videoViewport[1] = float(viewport.y());
    uniforms.videoViewport[2] = float(viewport.width());
    uniforms.videoViewport[3] = float(viewport.height());
    const auto *description = QVideoTextureHelper::textureDescription(format.pixelFormat());
    uniforms.videoPixelInfo[0] = int(format.pixelFormat());
    uniforms.videoPixelInfo[1] = description ? description->nplanes : 0;
    uniforms.videoPixelInfo[2] = video->redOrAlphaIndex;
    uniforms.videoPixelInfo[3] = int(frame.rotation());
    for (int plane = 0; plane < 3; ++plane)
        uniforms.videoTextureFormats[plane] = plane < uniforms.videoPixelInfo[1]
            ? videoTextureFormatCode(video->planeFormats[plane]) : 0;
    uniforms.videoTextureFormats[3] = format.isMirrored() || frame.mirrored() ? 1 : 0;
    uniforms.videoEnabled = uniforms.videoPixelInfo[1] > 0;
}

class LinearVideoProvider final : public QSGTextureProvider
{
public:
    LinearVideoProvider(QQuickWindow *window, QSGDefaultRenderContext *context, QRhi *rhi)
        : converted(context, rhi), backend(rhi)
    {
        connect(window, &QQuickWindow::afterFrameEnd, this,
            [this] { converted.onFrameEnd(); }, Qt::DirectConnection);
    }
    QSGTexture *texture() const override { return const_cast<ViewerLinearVideoTexture *>(&converted); }
    ViewerLinearVideoTexture converted;
    QRhi *backend;
};

class DeleteVideoProviderJob final : public QRunnable
{
public:
    explicit DeleteVideoProviderJob(QSGTextureProvider *provider) : provider(provider) {}
    void run() override { provider.reset(); }
private:
    std::unique_ptr<QSGTextureProvider> provider;
};
#endif

class ResampleMaterial;

class ResampleShader final : public QSGMaterialShader
{
public:
    explicit ResampleShader(bool videoShader, bool referenceShader, bool convertVideoToLinear)
    {
        setShaderFileName(VertexStage,
            videoShader
                ? QStringLiteral(":/ZoinGallery/resources/viewer_resample_video.vert.qsb")
                : QStringLiteral(":/ZoinGallery/resources/viewer_resample.vert.qsb"));
        setShaderFileName(FragmentStage,
            videoShader
                ? (convertVideoToLinear
                    ? QStringLiteral(":/ZoinGallery/resources/viewer_video_linear.frag.qsb")
                    : referenceShader
                    ? QStringLiteral(":/ZoinGallery/resources/viewer_resample_video_reference.frag.qsb")
                    : QStringLiteral(":/ZoinGallery/resources/viewer_resample_video.frag.qsb"))
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
                              , bool reference, bool conversion
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                              , bool useVideoShader
#endif
                              )
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        : videoShader(useVideoShader)
#endif
    {
        referenceShader = videoShader && reference;
        convertVideoToLinear = videoShader && conversion;
        setFlag(RequiresFullMatrix, true);
        // The conversion target stores source data, not a presentation fade.
        // Disable blending to preserve float32 premultiplied texels exactly.
        setFlag(Blending, !convertVideoToLinear);
        QImage transparent(1, 1, QImage::Format_RGBA8888_Premultiplied);
        transparent.fill(Qt::transparent);
        fallbackTexture.reset(window->createTextureFromImage(transparent));
    }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType imageType;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        static QSGMaterialType videoType;
        static QSGMaterialType videoReferenceType;
        static QSGMaterialType videoConversionType;
        if (videoShader && convertVideoToLinear)
            return &videoConversionType;
        if (videoShader && referenceShader)
            return &videoReferenceType;
        return videoShader ? &videoType : &imageType;
#else
        return &imageType;
#endif
    }

    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override
    {
        ZoinGallery::MediaTimingTrace::event(
            QStringLiteral("qt.gallery.viewer.material_created"), {
                {QStringLiteral("video"), videoShader},
                {QStringLiteral("matchingShaderStages"), true},
                {QStringLiteral("referenceSampling"), referenceShader},
                {QStringLiteral("linearConversion"), convertVideoToLinear},
            });
        if (qEnvironmentVariableIsSet("F4_VIEWER_RESAMPLE_DEBUG"))
            qInfo() << "[FIX:viewer-sampling] video=" << videoShader
                    << "reference=" << referenceShader
                    << "linearConversion=" << convertVideoToLinear;
        return new ResampleShader(videoShader, referenceShader, convertVideoToLinear);
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
    bool hardwareSampling = false;
    QSGTexture *lastTracedTexture = nullptr;
    QSize lastTracedTextureSize;
    bool videoShader = false;
    bool referenceShader = false;
    bool convertVideoToLinear = false;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    QPointer<QSGTextureProvider> linearProvider;
    QRhi *rhi = nullptr;
    // Opaque identity captured while the GUI is blocked during scene sync.
    // Rendering uses the retained QVideoFrame, never dereferences this item.
    ViewerVideoFrameSource *videoSource = nullptr;
    bool linearCacheAdmitted = false;
    QSGTexture *drawLinearTexture = nullptr;
    QVideoFrame frame;
    quint64 frameRevision = 0;
    QVideoFrameTexturePool videoTexturePool;
    std::array<std::unique_ptr<QSGPlainTexture>, 3> videoPlaneTextures;
    bool videoUploadFailed = false;
#endif
};

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
QSGTexture *linearVideoTexture(const ResampleMaterial &material)
{
    if (!material.linearCacheAdmitted || material.referenceShader || material.convertVideoToLinear
            || material.nearestNeighbor || material.hardwareSampling || material.pixelAlignedIdentity
            || !material.linearProvider || !material.frame.isValid())
        return nullptr;
    auto *texture = material.linearProvider->texture();
    auto *converted = dynamic_cast<ViewerLinearVideoTexture *>(texture);
    if (!converted || !converted->readyFor(quintptr(material.videoSource),
            material.frameRevision, material.rhi))
        return nullptr;
    auto *native = texture ? texture->rhiTexture() : nullptr;
    // A requested QML format is not proof of a successful full-precision
    // allocation. A missing/wrong-size/wrong-format texture uses the original
    // YUV shader immediately, including during resize or resource loss.
    if (!native || native->format() != QRhiTexture::RGBA32F
            || native->sampleCount() != 1
            || native->pixelSize() != ZoinGallery::VideoFrameGeometry::displaySize(material.frame))
        return nullptr;
    return texture;
}
#endif

bool ResampleShader::updateUniformData(RenderState &state, QSGMaterial *newMaterial,
                                      QSGMaterial *)
{
    auto *material = static_cast<ResampleMaterial *>(newMaterial);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    // Freeze one selection for the uniform flag and every sampler in this draw.
    material->drawLinearTexture = linearVideoTexture(*material);
#endif
    // Explicit diagnostics only: count expensive shader submissions separately
    // from scene-graph frames or CPU synchronization (which do not imply draws).
    static const bool drawTrace = qEnvironmentVariableIsSet("F4_VIEWER_RESAMPLE_DRAW_TRACE");
    if (drawTrace)
        qInfo() << "F4_VIEWER_RESAMPLE_DRAW" << material->intermediate
                << material->nearestNeighbor << material->videoShader << material->viewportSize;
    if (drawTrace && material->hardwareSampling)
        qInfo() << "[FIX:viewer-flight] hardwareSampling" << material->videoShader
                << material->viewportSize;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (drawTrace && material->videoShader && !material->convertVideoToLinear
            && !material->nearestNeighbor)
        qInfo() << "F4_VIEWER_LINEAR_SAMPLE" << bool(material->drawLinearTexture)
                << material->frameRevision;
#endif
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
    uniforms.hardwareSampling = material->hardwareSampling;
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
    if (material->videoSource) {
        fillVideoUniforms(uniforms, state.rhi(), material->frame);
        if (uniforms.videoEnabled && material->drawLinearTexture)
            uniforms.videoEnabled = 2;
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
    if (binding < 1 || binding > 4)
        return;
    auto *material = static_cast<ResampleMaterial *>(newMaterial);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    QSGTexture *selected = nullptr;
    QSGTexture *linear = material->drawLinearTexture;
    if (binding == 4) {
        selected = linear;
    } else if (linear) {
        // The conversion pass owns/uploads the frame's original planes. Do
        // not upload the same frame a second time for this final filter.
        selected = material->fallbackTexture.get();
    } else if (material->videoSource && material->frame.isValid()) {
        if (material->videoTexturePool.texturesDirty()) {
            auto *textures = material->videoTexturePool.updateTextures(
                *state.rhi(), *state.resourceUpdateBatch());
            const auto *description = QVideoTextureHelper::textureDescription(material->frame.pixelFormat());
            bool failed = !textures || !description || description->nplanes < 1
                || description->nplanes > 3;
            for (int plane = 0; !failed && plane < description->nplanes; ++plane)
                failed = !textures->texture(plane);
            if (failed != material->videoUploadFailed) {
                ZoinGallery::MediaTimingTrace::event(
                    QStringLiteral("qt.gallery.viewer.video_upload_state"), {
                        {QStringLiteral("failed"), failed},
                        {QStringLiteral("backend"), int(state.rhi()->backend())},
                        {QStringLiteral("pixelFormat"), int(material->frame.pixelFormat())},
                    });
                material->videoUploadFailed = failed;
            }
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
        // texelFetch of float32 data needs no float-linear-filter support.
        selected->setFiltering(binding == 4 ? QSGTexture::Nearest : QSGTexture::Linear);
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
                          , bool reference, bool conversion
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                          , bool useVideoShader
#endif
                          )
        : geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4),
          material(window
                   , reference, conversion
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
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        // Bring the source conversion's live/dirty layer up to date before
        // deciding whether its full-precision texture can replace YUV reads.
        auto *linear = material.linearCacheAdmitted && material.linearProvider
            ? material.linearProvider->texture() : nullptr;
        auto *linearDynamic = qobject_cast<QSGDynamicTexture *>(linear);
        if (linearDynamic && linearDynamic->updateTexture())
            markDirty(DirtyMaterial);
        if (material.linearCacheAdmitted && material.linearProvider) {
            linear = material.linearProvider->texture();
            auto *native = linear ? linear->rhiTexture() : nullptr;
            const QSize expected = ZoinGallery::VideoFrameGeometry::displaySize(material.frame);
            const auto *converted = dynamic_cast<ViewerLinearVideoTexture *>(linear);
            if (!converted || converted->failed() || !native || native->format() != QRhiTexture::RGBA32F
                    || native->sampleCount() != 1 || native->pixelSize() != expected) {
                // Stay on the original YUV filter in this same frame. Latch an
                // allocation failure until the source size or RHI changes;
                // incoming revisions must not retry a large allocation loop.
                linearCacheFailed = true;
                material.linearCacheAdmitted = false;
                reservation.release();
                setLinearSupport(false, {});
            }
        }
#endif
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
    void setLinearProvider(QSGTextureProvider *provider)
    {
        if (material.linearProvider == provider)
            return;
        QObject::disconnect(linearTextureChanged);
        QObject::disconnect(linearProviderDestroyed);
        material.linearProvider = provider;
        if (provider) {
            linearTextureChanged = QObject::connect(provider, &QSGTextureProvider::textureChanged,
                this, [this] { markDirty(DirtyMaterial); }, Qt::DirectConnection);
            linearProviderDestroyed = QObject::connect(provider, &QObject::destroyed,
                this, [this] { material.linearProvider = nullptr; markDirty(DirtyMaterial); },
                Qt::DirectConnection);
        }
        markDirty(DirtyMaterial);
    }

    QMetaObject::Connection linearTextureChanged;
    QMetaObject::Connection linearProviderDestroyed;
    QMetaObject::Connection frameEndConnection;
    void setLinearSupport(bool supported, QSize size)
    {
        if (linearSupportPublished && linearSupportResult == supported
                && linearSupportSize == size)
            return;
        linearSupportPublished = true;
        linearSupportResult = supported;
        linearSupportSize = size;
        if (publishLinearSupport)
            publishLinearSupport(supported, size);
    }

    LinearVideoReservation reservation;
    std::function<void(bool, QSize)> publishLinearSupport;
    std::atomic<quint64> supportGeneration{0};
    bool linearSupportResult = false;
    bool linearSupportPublished = false;
    QSize linearSupportSize;
    bool linearCacheFailed = false;
    QSize checkedVideoSize;
    QRhi *checkedRhi = nullptr;
#endif
};

} // namespace

bool ViewerResampleEffect::windowProjectionMatches(qreal dpr) const
{
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    const auto *surface = window();
    if (!surface || surface->width() <= 0 || surface->height() <= 0 || dpr <= 0)
        return false;
    const auto target = surface->renderTarget();
    const QSize pixels = target.isNull()
        ? QSize(qRound(surface->width() * dpr), qRound(surface->height() * dpr))
        : QQuickRenderTargetPrivate::get(&target)->pixelSize;
    // Match the vertex shader's projection guard, including render-control
    // targets whose physical size differs from rounded logical size * DPR.
    return qAbs(qreal(pixels.width()) / surface->width() - dpr) <= 0.01
        && qAbs(qreal(pixels.height()) / surface->height() - dpr) <= 0.01;
#else
    Q_UNUSED(dpr);
    return false;
#endif
}

ViewerResampleEffect::ViewerResampleEffect(QQuickItem *parent) : QQuickItem(parent)
{
    m_referenceSampling = qEnvironmentVariableIntValue("F4_VIEWER_RESAMPLE_REFERENCE") == 1;
    m_disableLinearVideoCache = qEnvironmentVariableIntValue("F4_VIEWER_DISABLE_LINEAR_VIDEO_CACHE") == 1;
    setFlag(ItemHasContents, true);
}

ViewerResampleEffect::~ViewerResampleEffect()
{
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    releaseLinearProvider();
#endif
}

bool ViewerResampleEffect::isTextureProvider() const
{
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (m_convertVideoToLinear)
        return true;
#endif
    return QQuickItem::isTextureProvider();
}

QSGTextureProvider *ViewerResampleEffect::textureProvider() const
{
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (m_convertVideoToLinear) {
        // Called only on the render thread during sync; the GUI is blocked.
        auto *surface = window();
        auto *context = surface ? qobject_cast<QSGDefaultRenderContext *>(
            QQuickWindowPrivate::get(surface)->context) : nullptr;
        auto *rhi = context ? context->rhi() : nullptr;
        if (!rhi)
            return nullptr;
        auto *provider = static_cast<LinearVideoProvider *>(m_videoLinearProvider.data());
        if (provider && (provider->backend != rhi || m_videoLinearWindow != surface)) {
            QObject::disconnect(m_videoLinearInvalidated);
            delete provider;
            provider = nullptr;
        }
        if (!provider) {
            provider = new LinearVideoProvider(surface, context, rhi);
            m_videoLinearProvider = provider;
            m_videoLinearWindow = surface;
            m_videoLinearInvalidated = connect(surface, &QQuickWindow::sceneGraphInvalidated,
                this, [this, guarded = QPointer<QSGTextureProvider>(provider)] {
                    if (m_videoLinearProvider == guarded)
                        m_videoLinearProvider = nullptr;
                    delete guarded.data();
                }, Qt::DirectConnection);
        }
        auto *source = m_videoSource.data();
        const auto snapshot = source ? source->snapshot() : ViewerVideoFrameSource::Snapshot{};
        ResampleUniforms uniforms{};
        QMatrix4x4 matrix;
        if (rhi->isYUpInFramebuffer() != rhi->isYUpInNDC())
            matrix.scale(1, -1);
        std::memcpy(uniforms.matrix, matrix.constData(), sizeof(uniforms.matrix));
        uniforms.opacity = 1;
        uniforms.intermediate = 1;
        fillVideoUniforms(uniforms, rhi, snapshot.frame);
        provider->converted.setFrame(snapshot.frame, quintptr(source), snapshot.revision,
            QByteArray(reinterpret_cast<const char *>(&uniforms), VideoUniformByteSize));
        return provider;
    }
#endif
    return QQuickItem::textureProvider();
}

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
void ViewerResampleEffect::releaseLinearProvider()
{
    auto *provider = m_videoLinearProvider.data();
    if (!provider)
        return;
    m_videoLinearProvider = nullptr;
    QObject::disconnect(m_videoLinearInvalidated);
    if (m_videoLinearWindow)
        m_videoLinearWindow->scheduleRenderJob(new DeleteVideoProviderJob(provider),
            QQuickWindow::AfterSynchronizingStage);
    else
        delete provider; // An invalidated scene graph has already released its RHI.
    m_videoLinearWindow = nullptr;
}
#endif

void ViewerResampleEffect::releaseResources()
{
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    releaseLinearProvider();
#endif
    QQuickItem::releaseResources();
}

void ViewerResampleEffect::setLinearVideoSource(QQuickItem *source)
{
    if (m_linearVideoSource == source)
        return;
    QObject::disconnect(m_linearVideoSourceDestroyed);
    m_linearVideoSource = source;
    if (source) {
        m_linearVideoSourceDestroyed = connect(source, &QObject::destroyed, this, [this] {
            m_linearVideoSource = nullptr;
            emit linearVideoSourceChanged();
            update();
        });
    }
    emit linearVideoSourceChanged();
    update();
}

void ViewerResampleEffect::setConvertVideoToLinear(bool enabled)
{
    if (m_convertVideoToLinear == enabled)
        return;
    m_convertVideoToLinear = enabled;
    emit convertVideoToLinearChanged();
    update();
}

void ViewerResampleEffect::setRequestLinearVideoCache(bool requested)
{
    if (m_requestLinearVideoCache == requested)
        return;
    m_requestLinearVideoCache = requested;
    emit requestLinearVideoCacheChanged();
    update();
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

void ViewerResampleEffect::setHardwareSampling(bool enabled)
{
    if (m_hardwareSampling == enabled)
        return;
    m_hardwareSampling = enabled;
    emit hardwareSamplingChanged();
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
    if (m_convertVideoToLinear) {
        delete node;
        return nullptr; // The texture provider records its checked data pass.
    }
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
    if (node && (node->material.videoShader != useVideoShader
            || node->material.convertVideoToLinear != (useVideoShader && m_convertVideoToLinear))) {
        delete node;
        node = nullptr;
    }
#endif

    if (!node) {
        node = new ResampleNode(window(), m_referenceSampling, m_convertVideoToLinear
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
                                , useVideoShader
#endif
                                );
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
        const QPointer<ViewerResampleEffect> effect = this;
        const QPointer<QQuickWindow> testedWindow = window();
        const QPointer<ResampleNode> testedNode = node;
        // Retained pyramid passes can be culled, so updatePaintNode() alone
        // cannot release their admission. Before synchronization the GUI is
        // blocked; release inactive consumers on the render thread before a
        // newly selected pass tries to reserve the same bounded 4K storage.
        QObject::connect(window(), &QQuickWindow::beforeSynchronizing, node,
            [effect, testedNode] {
                if (!effect || !testedNode || effect->m_requestLinearVideoCache
                        || !testedNode->material.linearCacheAdmitted)
                    return;
                testedNode->reservation.release();
                testedNode->material.linearCacheAdmitted = false;
                testedNode->setLinearSupport(false, {});
                testedNode->markDirty(QSGNode::DirtyMaterial);
                if (qEnvironmentVariableIsSet("F4_VIEWER_RESAMPLE_DEBUG"))
                    qInfo() << "[FIX:viewer-pyramid] released inactive conversion admission"
                            << effect->objectName();
            }, Qt::DirectConnection);
        node->publishLinearSupport = [effect, testedWindow, testedNode](bool supported, QSize admittedSize) {
            if (!effect || !testedNode)
                return;
            const quint64 generation = testedNode->supportGeneration.fetch_add(1) + 1;
            QMetaObject::invokeMethod(effect.data(), [effect, testedWindow, testedNode, supported, admittedSize, generation] {
                if (!effect || !testedNode || effect->window() != testedWindow
                        || testedNode->supportGeneration.load() != generation
                        || (effect->m_linearVideoCacheSupported == supported
                            && effect->m_linearVideoCacheSize == admittedSize))
                    return;
                effect->m_linearVideoCacheSupported = supported;
                effect->m_linearVideoCacheSize = admittedSize;
                emit effect->linearVideoCacheSupportedChanged();
                if (qEnvironmentVariableIsSet("F4_VIEWER_RESAMPLE_DEBUG"))
                    qInfo() << "[FIX:viewer-sampling] float32 conversion admitted=" << supported
                            << "consumer=" << effect->objectName() << "size=" << admittedSize;
            }, Qt::QueuedConnection);
        };
#endif
    }
    node->setProvider(m_source && m_source->isTextureProvider()
        ? m_source->textureProvider() : nullptr);
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    node->setLinearProvider(m_linearVideoSource && m_linearVideoSource->isTextureProvider()
        ? m_linearVideoSource->textureProvider() : nullptr);
    const QSize videoSize = useVideoShader
        ? ZoinGallery::VideoFrameGeometry::displaySize(videoSnapshot.frame) : QSize{};
    auto *rhi = static_cast<QRhi *>(window()->rendererInterface()->getResource(
        window(), QSGRendererInterface::RhiResource));
    if (node->checkedVideoSize != videoSize || node->checkedRhi != rhi) {
        node->reservation.release();
        node->checkedVideoSize = videoSize;
        node->checkedRhi = rhi;
        node->linearCacheFailed = false;
    }
    bool admitted = false;
    if (m_requestLinearVideoCache && !m_referenceSampling && !m_disableLinearVideoCache
            && !node->linearCacheFailed && !m_convertVideoToLinear && rhi) {
        auto *renderContext = qobject_cast<QSGDefaultRenderContext *>(
            QQuickWindowPrivate::get(window())->context);
        const bool supported = rhi->backend() != QRhi::Null && renderContext
            && renderContext->msaaSampleCount() == 1
            && rhi->isFeatureSupported(QRhi::TexelFetch)
            && rhi->isTextureFormatSupported(QRhiTexture::RGBA32F,
                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource)
            && rhi->resourceLimit(QRhi::TextureSizeMax) >= 4096;
        admitted = supported && node->reservation.reserve(rhi, videoSize);
    }
    if (!admitted)
        node->reservation.release();
    node->material.linearCacheAdmitted = admitted;
    node->setLinearSupport(admitted, admitted ? videoSize : QSize{});
#endif

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
    material.hardwareSampling = m_hardwareSampling;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (material.videoSource != videoSource
        || material.frameRevision != videoSnapshot.revision) {
        material.videoSource = videoSource;
        material.frame = videoSnapshot.frame;
        material.frameRevision = videoSnapshot.revision;
        material.videoTexturePool.setCurrentFrame(videoSnapshot.frame);
    }
    material.rhi = static_cast<QRhi *>(window()->rendererInterface()->getResource(
        window(), QSGRendererInterface::RhiResource));
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
