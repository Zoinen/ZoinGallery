#pragma once

#include <QByteArray>
#include <QSGTexture>
#include <QVideoFrame>
#include <memory>

QT_BEGIN_NAMESPACE
class QSGDefaultRenderContext;
class QShader;
QT_END_NAMESPACE

// Render-thread only; setFrame() runs during sync, updateTexture() before the
// parent render pass. The owner calls onFrameEnd() from Qt's afterFrameEnd and
// keeps this object alive through that notification after recording a draw.
class ViewerLinearVideoTexture final : public QSGDynamicTexture
{
public:
    ViewerLinearVideoTexture(QSGDefaultRenderContext *rc, QRhi *rhi);
    ViewerLinearVideoTexture(QSGDefaultRenderContext *rc, QRhi *rhi,
                            QShader vertexShader, QShader fragmentShader);
    ~ViewerLinearVideoTexture() override;

    void setFrame(QVideoFrame frame, quintptr sourceIdentity, quint64 revision,
                  QByteArray uniforms);
    bool readyFor(quintptr sourceIdentity, quint64 revision, QRhi *rhi) const;
    bool failed() const;
    void onFrameEnd();
    bool updateTexture() override;
    QRhiTexture *rhiTexture() const override;
    QSize textureSize() const override;
    qint64 comparisonKey() const override;
    bool hasAlphaChannel() const override { return true; }
    bool hasMipmaps() const override { return false; }

private:
    struct Data;
    std::unique_ptr<Data> d;
};
