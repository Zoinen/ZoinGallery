#ifndef GALLERYSCENEITEM_H
#define GALLERYSCENEITEM_H

#include <QQuickItem>
#include <memory>

struct GalleryViewportMotionState;
class QSGTransformNode;

// Culls a visual subtree without QQuickItem's opacity-zero transition.
class GallerySceneItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool sceneVisible READ sceneVisible WRITE setSceneVisible
               NOTIFY sceneVisibleChanged)
    Q_PROPERTY(bool nativeCullingAvailable READ nativeCullingAvailable
               NOTIFY nativeCullingAvailableChanged)
    Q_PROPERTY(bool retainSceneWhenHidden READ retainSceneWhenHidden
               NOTIFY retainSceneWhenHiddenChanged)
    Q_PROPERTY(bool accessibilityLeaf READ accessibilityLeaf WRITE setAccessibilityLeaf
               NOTIFY accessibilityLeafChanged)
public:
    // Render-thread API for prepared delegate roots. Intrinsic child gates
    // keep their ordinary visibility; only viewport culling uses the camera.
    // Empty bounds block a stale root until the camera releases it.
    static void setNodeViewportMotion(QSGTransformNode *node,
        std::shared_ptr<GalleryViewportMotionState> state, const QRectF &bounds);
    static void setNodeViewportMotion(QSGTransformNode *node,
        std::shared_ptr<GalleryViewportMotionState> state, const QRectF &bounds,
        bool retainPreparedScene);
    static void updateNodeViewportMotion(QSGTransformNode *node);
    static void clearNodeViewportMotion(QSGTransformNode *node,
        const std::shared_ptr<GalleryViewportMotionState> &owner);
    explicit GallerySceneItem(QQuickItem *parent = nullptr);
    bool sceneVisible() const { return _sceneVisible; }
    void setSceneVisible(bool visible);
    bool nativeCullingAvailable() const;
    // Internal publication mask; authored opacity and visibility stay intact.
    void setIncubationReady(bool ready);
    // The owning viewport must prove its clip completely hides this item.
    bool retainSceneWhenHidden() const { return _retainSceneWhenHidden; }
    void setRetainSceneWhenHidden(bool retain);
    bool accessibilityLeaf() const { return _accessibilityLeaf; }
    void setAccessibilityLeaf(bool leaf);

signals:
    void sceneVisibleChanged();
    void nativeCullingAvailableChanged();
    void retainSceneWhenHiddenChanged();
    void accessibilityLeafChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    bool _sceneVisible = true;
    bool _retainSceneWhenHidden = false;
    bool _accessibilityLeaf = false;
    bool _incubationReady = true;
    bool _incubationCulled = false;
    void updateIncubationCulling();
};

#endif // GALLERYSCENEITEM_H
