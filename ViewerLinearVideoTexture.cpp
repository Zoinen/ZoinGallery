#include "ViewerLinearVideoTexture.h"
#include "VideoFrameGeometry.h"

#include <QDebug>
#include <QFile>
#include <QPointer>
#include <QtQuick/private/qsgdefaultrendercontext_p.h>
#include <QtMultimedia/private/qvideoframetexturepool_p.h>
#include <QtMultimedia/private/qvideotexturehelper_p.h>
#include <rhi/qrhi.h>
#include <array>
#include <utility>
#include <vector>

namespace {
constexpr quint32 UniformBytes = 308;
constexpr float Quad[] = {
    -1, -1, 0, 0,  1, -1, 1, 0,
    -1,  1, 0, 1,  1,  1, 1, 1,
};
QShader loadShader(const char *path)
{
    QFile file(QString::fromLatin1(path));
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}
}

struct ViewerLinearVideoTexture::Data
{
    struct Resources {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiBuffer> vertices, uniform;
        std::unique_ptr<QRhiSampler> sampler;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
        std::array<QRhiTexture *, 3> planes{};
        std::array<QSize, 3> planeSizes{};
        std::array<QRhiTexture::Format, 3> planeFormats{};
    };
    QPointer<QSGDefaultRenderContext> context;
    QRhi *rhi = nullptr;
    QVideoFrame frame;
    QVideoFrameTexturePool pool;
    QByteArray uniforms;
    QShader vertexShader, fragmentShader;
    QSize size;
    quintptr sourceIdentity = 0;
    quint64 revision = 0;
    bool dirty = false, ready = false, hasFailed = false, uploadedThisFrame = false;
    std::unique_ptr<Resources> resources;
    std::vector<std::unique_ptr<Resources>> retired;
    std::vector<QVideoFrame> submittedFrames;

    bool hasContext() const
    {
        return context && rhi && context->rhi() == rhi && !rhi->isDeviceLost();
    }
    bool fail(const char *stage)
    {
        ready = false;
        dirty = false;
        if (!hasFailed)
            qWarning() << "[FIX:viewer-sampling]" << stage << "failed, size" << size;
        hasFailed = true;
        return false;
    }
    void retireResources()
    {
        if (resources)
            retired.push_back(std::move(resources));
    }
    bool createResources(QRhiResourceUpdateBatch *updates)
    {
        resources = std::make_unique<Resources>();
        auto &res = *resources;
        res.texture.reset(rhi->newTexture(QRhiTexture::RGBA32F, size, 1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!res.texture || !res.texture->create())
            return fail("RGBA32F texture creation");
        res.target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(res.texture.get())}));
        if (!res.target)
            return fail("render target allocation");
        res.pass.reset(res.target->newCompatibleRenderPassDescriptor());
        if (!res.pass)
            return fail("render pass descriptor creation");
        res.target->setRenderPassDescriptor(res.pass.get());
        if (!res.target->create())
            return fail("render target creation");
        res.vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(Quad)));
        res.uniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UniformBytes));
        res.sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge,
            QRhiSampler::ClampToEdge));
        if (!res.vertices || !res.uniform || !res.sampler
                || !res.vertices->create() || !res.uniform->create() || !res.sampler->create())
            return fail("buffer/sampler creation");
        updates->uploadStaticBuffer(res.vertices.get(), Quad);
        res.bindings.reset(rhi->newShaderResourceBindings());
        return res.bindings ? true : fail("shader bindings allocation");
    }
    bool updateBindings(const std::array<QRhiTexture *, 3> &planes)
    {
        auto &res = *resources;
        bool changed = planes != res.planes;
        for (size_t i = 0; i < planes.size(); ++i)
            changed |= planes[i]->pixelSize() != res.planeSizes[i]
                || planes[i]->format() != res.planeFormats[i];
        if (!changed)
            return true;
        res.bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                res.uniform.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, planes[0], res.sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, planes[1], res.sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, planes[2], res.sampler.get()),
        });
        if (!res.bindings->create())
            return fail("shader bindings creation");
        res.planes = planes;
        for (size_t i = 0; i < planes.size(); ++i) {
            res.planeSizes[i] = planes[i]->pixelSize();
            res.planeFormats[i] = planes[i]->format();
        }
        return true;
    }
    bool createPipeline()
    {
        auto &res = *resources;
        if (!vertexShader.isValid() || !fragmentShader.isValid())
            return fail("conversion shader loading");
        res.pipeline.reset(rhi->newGraphicsPipeline());
        if (!res.pipeline)
            return fail("graphics pipeline allocation");
        QRhiVertexInputLayout layout;
        layout.setBindings({QRhiVertexInputBinding(16)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {0, 1, QRhiVertexInputAttribute::Float2, 8}});
        res.pipeline->setVertexInputLayout(layout);
        res.pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertexShader},
                                      {QRhiShaderStage::Fragment, fragmentShader}});
        res.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        res.pipeline->setSampleCount(1);
        res.pipeline->setCullMode(QRhiGraphicsPipeline::None);
        res.pipeline->setDepthTest(false);
        res.pipeline->setDepthWrite(false);
        res.pipeline->setTargetBlends({QRhiGraphicsPipeline::TargetBlend{}});
        res.pipeline->setShaderResourceBindings(res.bindings.get());
        res.pipeline->setRenderPassDescriptor(res.pass.get());
        return res.pipeline->create() ? true : fail("graphics pipeline creation");
    }
};

ViewerLinearVideoTexture::ViewerLinearVideoTexture(QSGDefaultRenderContext *rc, QRhi *rhi)
    : ViewerLinearVideoTexture(rc, rhi,
        loadShader(":/ZoinGallery/resources/viewer_resample_video.vert.qsb"),
        loadShader(":/ZoinGallery/resources/viewer_video_linear.frag.qsb"))
{
}

ViewerLinearVideoTexture::ViewerLinearVideoTexture(QSGDefaultRenderContext *rc, QRhi *rhi,
                                                  QShader vertexShader, QShader fragmentShader)
    : d(std::make_unique<Data>())
{
    d->context = rc;
    d->rhi = rhi;
    d->vertexShader = std::move(vertexShader);
    d->fragmentShader = std::move(fragmentShader);
    setFiltering(Nearest);
    setMipmapFiltering(None);
    setHorizontalWrapMode(ClampToEdge);
    setVerticalWrapMode(ClampToEdge);
    if (rhi) {
        rhi->addCleanupCallback(this, [this](QRhi *) {
            d->ready = false;
            d->hasFailed = true;
            d->resources.reset();
            d->retired.clear();
            // clearTextures() does not release the pool's pending old slot.
            // Dispose every native handle while this RHI is still alive.
            d->pool = QVideoFrameTexturePool{};
            d->submittedFrames.clear();
            d->frame = {};
            d->rhi = nullptr;
        });
    }
}
ViewerLinearVideoTexture::~ViewerLinearVideoTexture()
{
    if (d->rhi)
        d->rhi->removeCleanupCallback(this);
}
void ViewerLinearVideoTexture::setFrame(QVideoFrame frame, quintptr identity,
                                       quint64 revision, QByteArray uniforms)
{
    const QSize size = frame.isValid() ? ZoinGallery::VideoFrameGeometry::displaySize(frame) : QSize{};
    const bool resized = size != d->size;
    if (!resized && identity == d->sourceIdentity && revision == d->revision)
        return;
    if (resized) {
        d->retireResources();
        d->hasFailed = false;
    }
    d->size = size;
    d->sourceIdentity = identity;
    d->revision = revision;
    d->frame = std::move(frame);
    d->uniforms = std::move(uniforms);
    d->pool.setCurrentFrame(d->frame);
    d->ready = false;
    d->dirty = true;
}
bool ViewerLinearVideoTexture::readyFor(quintptr identity, quint64 revision, QRhi *rhi) const
{
    return d->ready && !d->hasFailed && d->sourceIdentity == identity
        && d->revision == revision && d->rhi == rhi && d->hasContext();
}
bool ViewerLinearVideoTexture::failed() const { return d->hasFailed; }
QRhiTexture *ViewerLinearVideoTexture::rhiTexture() const
{
    return d->ready && d->hasContext() && d->resources ? d->resources->texture.get() : nullptr;
}
QSize ViewerLinearVideoTexture::textureSize() const { return d->size; }
qint64 ViewerLinearVideoTexture::comparisonKey() const { return qint64(quintptr(this)); }
void ViewerLinearVideoTexture::onFrameEnd()
{
    d->pool.onFrameEndInvoked();
    d->uploadedThisFrame = false;
    d->submittedFrames.clear();
    if (d->hasFailed)
        d->retireResources();
    d->retired.clear();
}
bool ViewerLinearVideoTexture::updateTexture()
{
    if (!d->dirty || d->hasFailed || !d->frame.isValid() || d->size.isEmpty())
        return false;
    if (!d->hasContext())
        return d->fail("render context/device");
    auto *cb = d->context->currentFrameCommandBuffer();
    if (!cb || !d->rhi->isRecordingFrame() || d->uploadedThisFrame)
        return false;
    if (d->uniforms.size() != UniformBytes)
        return d->fail("308-byte uniform contract");
    if (d->size.width() > d->rhi->resourceLimit(QRhi::TextureSizeMax)
            || d->size.height() > d->rhi->resourceLimit(QRhi::TextureSizeMax)
            || !d->rhi->isFeatureSupported(QRhi::TexelFetch)
            || !d->rhi->isTextureFormatSupported(QRhiTexture::RGBA32F,
                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource))
        return d->fail("RGBA32F capability/size");
    auto *updates = d->rhi->nextResourceUpdateBatch();
    if (!updates)
        return d->fail("resource update batch allocation");
    bool success = d->resources || d->createResources(updates);
    const auto *description = QVideoTextureHelper::textureDescription(d->frame.pixelFormat());
    std::array<QRhiTexture *, 3> planes{};
    if (success && description && description->nplanes >= 1 && description->nplanes <= 3) {
        d->uploadedThisFrame = true;
        auto *textures = d->pool.updateTextures(*d->rhi, *updates);
        for (int i = 0; i < 3; ++i)
            planes[i] = textures ? textures->texture(i < description->nplanes ? i : 0) : nullptr;
        success = planes[0] && planes[1] && planes[2];
    } else {
        success = false;
    }
    success = success && d->updateBindings(planes);
    success = success && (d->resources->pipeline || d->createPipeline());
    if (!success) {
        updates->release();
        return d->fail("conversion preparation/plane upload");
    }
    auto &res = *d->resources;
    updates->updateDynamicBuffer(res.uniform.get(), 0, UniformBytes, d->uniforms.constData());
    d->submittedFrames.push_back(d->frame);
    cb->beginPass(res.target.get(), Qt::transparent, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(res.pipeline.get());
    cb->setShaderResources(res.bindings.get());
    cb->setViewport(QRhiViewport(0, 0, d->size.width(), d->size.height()));
    const QRhiCommandBuffer::VertexInput input(res.vertices.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4);
    cb->endPass();
    d->dirty = false;
    d->ready = true; // Commands recorded in the parent's CB, not GPU completion.
    static const bool trace = qEnvironmentVariableIntValue("F4_VIEWER_RESAMPLE_DRAW_TRACE") != 0;
    if (trace)
        qInfo() << "F4_VIEWER_LINEAR_CONVERSION" << d->revision;
    return true;
}
