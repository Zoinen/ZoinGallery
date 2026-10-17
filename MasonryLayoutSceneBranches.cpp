#include "MasonryLayout.h"

#include <QtQuick/private/qquickitem_p.h>

namespace {

// Both branches use the existing viewport clip in scrolling coordinates.
// Keeping that rectangle near the viewport also bounds it for large catalogs.
class SceneBranch final : public QQuickItem {
public:
    explicit SceneBranch(QQuickItem *parent) : QQuickItem(parent) {
        setClip(true);
    }

    QRectF boundingRect() const override { return _viewportRect; }
    QRectF clipRect() const override { return _viewportRect; }
    bool contains(const QPointF &point) const override {
        return _viewportRect.contains(point);
    }

    void setViewportRect(const QRectF &rect) {
        if (_viewportRect == rect) return;
        _viewportRect = rect;
        setSize(rect.size());
        QQuickItemPrivate::get(this)->dirty(QQuickItemPrivate::Clip);
    }

private:
    QRectF _viewportRect;
};

} // namespace

void MasonryLayout::watchSceneBranchChanges(BrickItem *item) {
    connect(item, &BrickItem::geometryAnimationRunningChanged,
            this, &MasonryLayout::updateSceneBranchParents);
    connect(item, &BrickItem::presentationModeChanged, this,
            [this, item] { updateSceneBranchParent(item); });
    connect(item, &BrickItem::viewSourceIndexesChanged, this,
            [this, item] { updateSceneBranchParent(item); });
    if (auto *visual = qobject_cast<GalleryEntryVisual *>(item->visualModel()))
        connect(visual, &GalleryEntryVisual::mediaChanged, this,
                [this, item] { updateSceneBranchParent(item); });
}

void MasonryLayout::updateSceneBranchBounds() {
    if (!_viewport) return;
    const QRectF rect = _viewport->mapRectFromItem(this, boundingRect());
    for (auto *branch : {_masonryVectorBranch, _masonryRasterBranch}) {
        if (!branch) continue;
        static_cast<SceneBranch *>(branch)->setViewportRect(rect);
        branch->setVisible(_presentationMode == Masonry && !_containedPreview);
    }
}

void MasonryLayout::updateSceneBranchParent(BrickItem *item) {
    if (!_viewport || !item) return;
    QQuickItem *parent = _viewport;
    if (_presentationMode == Masonry && !_containedPreview
        && !_sceneBranchesSuspended && item->masonryMode()
        && item->viewIndex() >= 0 && item->nativeCullingAvailable()) {
        if (!_masonryVectorBranch) {
            _masonryVectorBranch = new SceneBranch(_viewport);
            _masonryVectorBranch->setObjectName("galleryMasonryVectorBranch");
            _masonryVectorBranch->setZ(-1);
            _masonryRasterBranch = new SceneBranch(_viewport);
            _masonryRasterBranch->setObjectName("galleryMasonryRasterBranch");
            updateSceneBranchBounds();
        }
        const auto *visual = qobject_cast<GalleryEntryVisual *>(item->visualModel());
        parent = visual && visual->isVector() && !visual->vectorSource().isEmpty()
            ? _masonryVectorBranch : _masonryRasterBranch;
    }
    if (item->parentItem() != parent) item->setParentItem(parent);
}

void MasonryLayout::updateSceneBranchParents() {
    bool animating = false;
    for (auto *item : _delegatePool.usedItems())
        animating |= item && item->geometryAnimationRunning();
    _sceneBranchesSuspended = animating;
    // Layout animations can overlap rows. Use their original common parent
    // during those animations, then restore independent branches at rest.
    for (auto *item : _delegatePool.usedItems())
        updateSceneBranchParent(item);
    if (animating && _viewport) {
        // QObject ownership retains the slot creation order while the visual
        // parent moves. Restore that same order for overlapping animations.
        QQuickItem *previous = nullptr;
        for (auto *object : _viewport->children()) {
            auto *item = qobject_cast<BrickItem *>(object);
            if (!item || !_delegatePool.usedItems().contains(item)) continue;
            if (previous) item->stackAfter(previous);
            previous = item;
        }
    }
    updateSceneBranchBounds();
}
