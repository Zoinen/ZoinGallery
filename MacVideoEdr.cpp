#include "MacVideoEdr.h"
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
#include "ViewerVideoFrameSource.h"
#endif

namespace ZoinGallery {

#if !defined(Q_OS_MACOS) || !defined(ZOIN_ENABLE_VIDEO_PLAYBACK)
MacVideoEdr::MacVideoEdr(QQuickItem *parent) : QQuickItem(parent) {}
MacVideoEdr::~MacVideoEdr() = default;
void MacVideoEdr::updatePolish() {}
#endif

void MacVideoEdr::setSource(QObject *source)
{
    if (m_source == source) return;
    disconnect(m_frameChanged);
    disconnect(m_destroyed);
    m_source = source;
#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK
    if (auto *frames = qobject_cast<ViewerVideoFrameSource *>(source)) {
        m_frameChanged = connect(frames, &ViewerVideoFrameSource::frameChanged, this, [this] {
            // Qt's media clock has already released this frame. An attached
            // AVFoundation plane can consume it independently of the Qt UI.
            // Initial attachment still needs the settled scene geometry.
            if (m_active)
                updatePolish();
            else
                polish();
        });
        m_destroyed = connect(frames, &QObject::destroyed, this, [this] {
            trackWindow();
            polish();
        });
    }
#endif
    trackWindow();
    polish();
    emit sourceChanged();
}

void MacVideoEdr::trackWindow()
{
    disconnect(m_windowFrame);
    if (window() && m_source) {
        // Qt has settled ancestor layout on the GUI thread. Apply it in this
        // frame; scheduling another polish here keeps a paused window repainting.
        m_windowFrame = connect(window(), &QQuickWindow::afterAnimating, this, &MacVideoEdr::updatePolish);
    }
    polish();
}

void MacVideoEdr::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    update();
    emit activeChanged();
}

QSGNode *MacVideoEdr::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (!m_active) {
        delete old;
        return nullptr;
    }
    auto *rect = static_cast<QSGSimpleRectNode *>(old);
    if (!rect) {
        rect = new QSGSimpleRectNode(boundingRect(), Qt::transparent);
        rect->material()->setFlag(QSGMaterial::Blending, false);
    } else {
        rect->setRect(boundingRect());
    }
    return rect;
}
} // namespace ZoinGallery
