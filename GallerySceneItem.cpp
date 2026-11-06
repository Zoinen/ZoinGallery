#include "GallerySceneItem.h"
#include "GalleryViewportMotion.h"

#include <QSGTransformNode>
#include <QQuickWindow>
#include <QtQuick/private/qquickitem_p.h>
#include <QAccessible>
#include <mutex>
#if QT_CONFIG(accessibility)
#include <QtQuick/private/qaccessiblequickitem_p.h>
#endif

namespace {

#if QT_CONFIG(accessibility)
// A decorative drawing subtree has no independent controls or text semantics.
// Marking it as an ignored Item would still make Qt recursively enumerate all
// of its children whenever an accessible ancestor is queried.
class AccessibleDecoration final : public QAccessibleQuickItem {
public:
    explicit AccessibleDecoration(QQuickItem *item) : QAccessibleQuickItem(item) {}
    QAccessible::Role role() const override { return QAccessible::NoRole; }
    QAccessible::State state() const override {
        // The Windows bridge otherwise exposes NoRole as a custom control.
        QAccessible::State state;
        state.invisible = true;
        state.offscreen = true;
        return state;
    }
    int childCount() const override { return 0; }
    QAccessibleInterface *child(int) const override { return nullptr; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
};

QAccessibleInterface *accessibleSceneItem(const QString &key, QObject *object) {
    if (key != QLatin1String("GallerySceneItem")) return nullptr;
    auto *item = qobject_cast<GallerySceneItem *>(object);
    return item && item->accessibilityLeaf() ? new AccessibleDecoration(item) : nullptr;
}
#endif

class SceneTransformNode final : public QSGTransformNode {
public:
    bool isSubtreeBlocked() const override { return _blocked; }
    void setBlocked(bool blocked, bool retained, bool contentEnabled) {
        _ordinaryBlocked = blocked;
        _contentEnabled = contentEnabled;
        _retained = retained;
        updateViewportMotion();
    }
    void setViewportMotion(std::shared_ptr<GalleryViewportMotionState> state,
        const QRectF &bounds, bool retained) {
        _motionState = std::move(state);
        _motionBounds = bounds;
        _motionHasBounds = !bounds.isEmpty();
        // The render camera can cross the viewport before GUI synchronization
        // selects the outgoing vector for the bounded retained working set.
        _motionRetained = retained;
        updateViewportMotion();
    }
    void updateViewportMotion() {
        const bool viewportBlocked = _motionState
            && _motionState->active.load(std::memory_order_acquire)
            ? !_motionHasBounds
                || (!(_retained || _motionRetained)
                    && !_motionBounds.intersects(_motionState->renderViewport))
            : _ordinaryBlocked;
        const bool blocked = !_contentEnabled || viewportBlocked;
        if (_blocked == blocked) return;
        _blocked = blocked;
        markDirty(QSGNode::DirtySubtreeBlocked);
    }
    void clearViewportMotion(const std::shared_ptr<GalleryViewportMotionState> &owner) {
        if (_motionState != owner) return;
        _motionState.reset();
        _motionRetained = false;
        updateViewportMotion();
    }
private:
    bool _blocked = false;
    bool _ordinaryBlocked = false;
    bool _contentEnabled = true;
    bool _retained = false;
    bool _motionRetained = false;
    bool _motionHasBounds = false;
    std::shared_ptr<GalleryViewportMotionState> _motionState;
    QRectF _motionBounds;
};

class SceneItemPrivate final : public QQuickItemPrivate {
public:
    QSGTransformNode *createTransformNode() override {
        return new SceneTransformNode;
    }
};

} // namespace

void GallerySceneItem::setNodeViewportMotion(QSGTransformNode *node,
    std::shared_ptr<GalleryViewportMotionState> state, const QRectF &bounds) {
    setNodeViewportMotion(node, std::move(state), bounds, false);
}

void GallerySceneItem::setNodeViewportMotion(QSGTransformNode *node,
    std::shared_ptr<GalleryViewportMotionState> state, const QRectF &bounds,
    bool retainPreparedScene) {
    if (auto *scene = dynamic_cast<SceneTransformNode *>(node))
        scene->setViewportMotion(std::move(state), bounds, retainPreparedScene);
}

void GallerySceneItem::updateNodeViewportMotion(QSGTransformNode *node) {
    if (auto *scene = dynamic_cast<SceneTransformNode *>(node))
        scene->updateViewportMotion();
}

void GallerySceneItem::clearNodeViewportMotion(QSGTransformNode *node,
    const std::shared_ptr<GalleryViewportMotionState> &owner) {
    if (auto *scene = dynamic_cast<SceneTransformNode *>(node))
        scene->clearViewportMotion(owner);
}

GallerySceneItem::GallerySceneItem(QQuickItem *parent)
    : QQuickItem(*new SceneItemPrivate, parent) {
#if QT_CONFIG(accessibility)
    static std::once_flag accessibilityFactory;
    std::call_once(accessibilityFactory, [] { QAccessible::installFactory(accessibleSceneItem); });
#endif
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        updateIncubationCulling();
        emit nativeCullingAvailableChanged();
        update();
    });
}

bool GallerySceneItem::nativeCullingAvailable() const {
    const auto api = window() ? window()->rendererInterface()->graphicsApi()
                              : QQuickWindow::graphicsApi();
    // The software visitor does not consult isSubtreeBlocked(). Its normal
    // visible/opacity path must remain responsible for hiding content.
    return api != QSGRendererInterface::Software;
}

void GallerySceneItem::setIncubationReady(bool ready) {
    if (_incubationReady == ready) return;
    _incubationReady = ready;
    updateIncubationCulling();
    update();
}

void GallerySceneItem::updateIncubationCulling() {
    const bool culled = !_incubationReady && !nativeCullingAvailable();
    if (_incubationCulled == culled) return;
    _incubationCulled = culled;
    QQuickItemPrivate::get(this)->setCulled(culled);
}

void GallerySceneItem::setSceneVisible(bool visible) {
    if (_sceneVisible == visible) return;
    _sceneVisible = visible;
    update();
    emit sceneVisibleChanged();
}

void GallerySceneItem::setRetainSceneWhenHidden(bool retain) {
    if (_retainSceneWhenHidden == retain) return;
    _retainSceneWhenHidden = retain;
    update();
    emit retainSceneWhenHiddenChanged();
}

void GallerySceneItem::setAccessibilityLeaf(bool leaf) {
    if (_accessibilityLeaf == leaf) return;
    _accessibilityLeaf = leaf;
#if QT_CONFIG(accessibility)
    QQuickItemPrivate::get(this)->isAccessible = leaf;
#endif
    emit accessibilityLeafChanged();
}

void GallerySceneItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
#if QT_CONFIG(accessibility)
    // An invisible drawing leaf has no accessible location to announce.
    auto *state = QQuickItemPrivate::get(this);
    const bool accessible = state->isAccessible;
    if (_accessibilityLeaf) state->isAccessible = false;
#endif
    QQuickItem::geometryChange(newGeometry, oldGeometry);
#if QT_CONFIG(accessibility)
    state->isAccessible = accessible;
#endif
}

QSGNode *GallerySceneItem::updatePaintNode(QSGNode *, UpdatePaintNodeData *data) {
    // Qt's opacity-zero transition forces a global renderer batch rebuild.
    // DirtySubtreeBlocked alone can preserve unaffected batches.
    static_cast<SceneTransformNode *>(data->transformNode)->setBlocked(
        nativeCullingAvailable() && !_sceneVisible && !_retainSceneWhenHidden,
        nativeCullingAvailable() && _retainSceneWhenHidden,
        !nativeCullingAvailable() || _incubationReady);
    return nullptr;
}
