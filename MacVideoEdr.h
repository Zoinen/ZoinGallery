#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>
#include <memory>

namespace ZoinGallery {

// A native video plane below Qt's ordinary UI. The scene graph draws its
// aperture at the item's normal stacking order, including ancestor clips.
class MacVideoEdr : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    explicit MacVideoEdr(QQuickItem *parent = nullptr);
    ~MacVideoEdr() override;
    QObject *source() const { return m_source; }
    void setSource(QObject *source);
    bool active() const { return m_active; }

signals:
    void sourceChanged();
    void activeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void updatePolish() override;

private:
    void trackWindow();
    void setActive(bool active);
    QPointer<QObject> m_source;
    QMetaObject::Connection m_frameChanged, m_destroyed, m_windowFrame;
    bool m_active = false;
#if defined(Q_OS_MACOS) && defined(ZOIN_ENABLE_VIDEO_PLAYBACK)
    class Presentation;
    std::unique_ptr<Presentation> m_presentation;
#endif
};

} // namespace ZoinGallery
