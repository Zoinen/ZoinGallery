#include "GalleryVectorUploadGate.h"
#include "VectorUploadGateProtocol.h"
#include "GalleryViewportMotion.h"
#include <QQuickWindow>
#include <QSGTransformNode>
#include <QtQuick/private/qquickitem_p.h>
#include <atomic>

struct GalleryVectorUploadState {
    std::atomic<quint64> generation = 1, readyGeneration = 0, failedGeneration = 0;
    std::atomic<bool> complete = false, present = false, prepared = false;
    std::atomic<bool> known = false, available = false, pending = false;
    std::atomic<int> priority = 100;
    std::atomic<QSGNode *> root = nullptr;
    std::atomic<const F4VectorUploadGateInterface *> api = nullptr;
    // The following snapshot is owned by the render thread.
    std::shared_ptr<GalleryViewportMotionState> motion;
    QRectF motionBounds;
    quint64 motionGeneration = 0;
    bool motionReady = false;

    void configure(QSGNode *node) {
        if (auto *interface = api.load()) {
            const auto revision = generation.load();
            const bool completeSnapshot = complete.load();
            bool effectivePresent = present.load();
            int effectivePriority = priority.load();
            if (motion && motion->active.load(std::memory_order_acquire)) {
                const bool inside = motionBounds.intersects(motion->renderVisibleViewport);
                effectivePresent = inside && motionGeneration == revision && motionReady
                    && completeSnapshot && failedGeneration.load() != revision
                    && interface->ready(node, revision);
                if (effectivePriority >= 100) effectivePriority = inside ? 100 : 200;
            }
            interface->configure(node, revision, completeSnapshot, effectivePresent,
                                 effectivePriority, prepared.load());
        }
    }
};

namespace {
class UploadTransform final : public QSGTransformNode {
public:
    explicit UploadTransform(QSharedPointer<GalleryVectorUploadState> state) : state(std::move(state)) {
        this->state->root = this;
        this->state->configure(this);
    }
    ~UploadTransform() override {
        if (auto *api = state->api.load()) api->remove(this);
        QSGNode *expected = this;
        state->root.compare_exchange_strong(expected, nullptr);
    }
    void setViewportMotion(std::shared_ptr<GalleryViewportMotionState> motion,
        const QRectF &bounds, bool readySnapshot) {
        if (state->root.load() != this) return;
        state->motion = std::move(motion);
        state->motionBounds = bounds;
        state->motionGeneration = state->generation.load();
        state->motionReady = readySnapshot;
        state->configure(this);
    }
    void updateViewportMotion() {
        if (state->root.load() == this) state->configure(this);
    }
    void clearViewportMotion(const std::shared_ptr<GalleryViewportMotionState> &owner) {
        if (state->root.load() != this || state->motion != owner) return;
        state->motion.reset();
        state->configure(this);
    }
private:
    QSharedPointer<GalleryVectorUploadState> state;
};

class UploadItemPrivate final : public QQuickItemPrivate {
public:
    QSharedPointer<GalleryVectorUploadState> state = QSharedPointer<GalleryVectorUploadState>::create();
    QSGTransformNode *createTransformNode() override { return new UploadTransform(state); }
};
}

GalleryVectorUploadGate::GalleryVectorUploadGate(QQuickItem *parent)
    : QQuickItem(*new UploadItemPrivate, parent),
      _state(static_cast<UploadItemPrivate *>(QQuickItemPrivate::get(this))->state) {
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::windowChanged, this, &GalleryVectorUploadGate::watchWindow);
    watchWindow();
}

GalleryVectorUploadGate::~GalleryVectorUploadGate() {
    disconnect(_sync);
    disconnect(_rendered);
    disconnect(_published);
    disconnect(_invalidated);
}

void GalleryVectorUploadGate::setNodeViewportMotion(QSGTransformNode *node,
    std::shared_ptr<GalleryViewportMotionState> state, const QRectF &bounds,
    bool readySnapshot) {
    if (auto *upload = dynamic_cast<UploadTransform *>(node))
        upload->setViewportMotion(std::move(state), bounds, readySnapshot);
}
void GalleryVectorUploadGate::updateNodeViewportMotion(QSGTransformNode *node) {
    if (auto *upload = dynamic_cast<UploadTransform *>(node))
        upload->updateViewportMotion();
}
void GalleryVectorUploadGate::clearNodeViewportMotion(QSGTransformNode *node,
    const std::shared_ptr<GalleryViewportMotionState> &owner) {
    if (auto *upload = dynamic_cast<UploadTransform *>(node))
        upload->clearViewportMotion(owner);
}

void GalleryVectorUploadGate::resetNativeState() {
    ++_state->generation;
    _state->prepared = false;
    _state->pending = false;
    _state->readyGeneration = 0;
    _state->failedGeneration = 0;
    _ready = false;
    _failed = false;
    emit nativeStateChanged();
    watchProgress();
    update();
}

void GalleryVectorUploadGate::setSourceKey(const QString &value) {
    if (_sourceKey == value) return;
    _sourceKey = value;
    resetNativeState();
    emit sourceKeyChanged();
}
bool GalleryVectorUploadGate::cpuComplete() const { return _state->complete; }
void GalleryVectorUploadGate::setCpuComplete(bool value) {
    if (_state->complete.exchange(value) == value) return;
    update();
    emit cpuCompleteChanged();
}
bool GalleryVectorUploadGate::present() const { return _state->present; }
void GalleryVectorUploadGate::setPresent(bool value) {
    if (_state->present.exchange(value) == value) return;
    update();
    emit presentChanged();
}
int GalleryVectorUploadGate::uploadPriority() const { return _state->priority; }
void GalleryVectorUploadGate::setUploadPriority(int value) {
    if (_state->priority.exchange(value) == value) return;
    update();
    emit uploadPriorityChanged();
}

void GalleryVectorUploadGate::watchWindow() {
    disconnect(_sync);
    disconnect(_rendered);
    disconnect(_published);
    disconnect(_invalidated);
    _sync = {}; _rendered = {}; _published = {};
    _known = !window();
    _available = false;
    _state->known = false;
    _state->available = false;
    _state->api = nullptr;
    if (auto *target = window()) {
        const auto state = _state;
        _sync = connect(target, &QQuickWindow::beforeSynchronizing, target, [state, target] {
            if (!state->known.load()) {
                auto *renderer = target->rendererInterface();
                const auto graphics = renderer->graphicsApi();
                const auto *api = QSGRendererInterface::isApiRhiBased(graphics)
                    ? static_cast<const F4VectorUploadGateInterface *>(
                        renderer->getResource(target, "f4.vectorUploadGate.v2")) : nullptr;
                const bool available = api && api->version == 2 && api->size >= sizeof(*api);
                state->api = available ? api : nullptr;
                state->available = available;
                state->known = true;
            }
            if (auto *root = state->root.load()) state->configure(root);
        }, Qt::DirectConnection);
        _invalidated = connect(target, &QQuickWindow::sceneGraphInvalidated, this, [this] {
            _known = false;
            _available = false;
            _state->known = false;
            _state->api = nullptr;
            resetNativeState();
        }, Qt::QueuedConnection);
    }
    resetNativeState();
}

void GalleryVectorUploadGate::watchProgress() {
    auto *target = window();
    if (!target) return;
    const auto state = _state;
    if (!_rendered) _rendered = connect(target, &QQuickWindow::afterRendering, target, [state, target] {
        const auto generation = state->generation.load();
        auto *root = state->root.load();
        if (const auto *api = state->api.load(); api && root) {
            if (api->ready(root, generation)) state->readyGeneration = generation;
            if (api->failed(root, generation)) state->failedGeneration = generation;
            state->pending = api->needsFrame(root, generation);
            // GPU residency can be lost after the GUI published source readiness.
            // Continue on the render thread without posting per-frame GUI work.
            if (state->pending) target->update();
        } else {
            state->pending = false;
        }
    }, Qt::DirectConnection);
    if (!_published) _published = connect(target, &QQuickWindow::afterRendering, this,
                         &GalleryVectorUploadGate::publishNativeState, Qt::QueuedConnection);
}

void GalleryVectorUploadGate::publishNativeState() {
    const auto generation = _state->generation.load();
    const bool known = _state->known;
    const bool available = _state->available;
    const bool ready = _ready || _state->readyGeneration.load() == generation;
    const bool failed = _state->failedGeneration.load() == generation;
    if (_known != known || _available != available || _ready != ready || _failed != failed) {
        _known = known; _available = available; _ready = ready; _failed = failed;
        _state->prepared = ready;
        emit nativeStateChanged();
    }
    if ((known && !available) || failed) {
        disconnect(_rendered); disconnect(_published);
        _rendered = {}; _published = {};
    } else if (ready) {
        // Re-entry may need fresh GPU batches even though source readiness is
        // unchanged. Keep only the lightweight render-thread progress observer.
        disconnect(_published);
        _published = {};
    }
}

QSGNode *GalleryVectorUploadGate::updatePaintNode(QSGNode *, UpdatePaintNodeData *data) {
    _state->configure(data->transformNode);
    return nullptr;
}
