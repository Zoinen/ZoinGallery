#include "GalleryScrollAnimation.h"
#include "MasonryLayout.h"
#include "GalleryViewportMotion.h"
#include "GalleryVectorUploadGate.h"
#include "GalleryAnimationClock.h"
#include "GalleryItemMappingScope.h"

#include <QtQuick/private/qquickanimatorjob_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquickclipnode_p.h>
#include <QAbstractItemModel>
#include <QQuickWindow>
#include <QSGTransformNode>
#include <QSGRendererInterface>
#include <QEvent>
#include <QRunnable>
#include <QScopeGuard>
#include <QTimer>
#include <QtMath>
#include <chrono>
#include <mutex>
#include <utility>

struct GalleryScrollAnimationState {
    std::shared_ptr<GalleryViewportMotionState> camera =
        std::make_shared<GalleryViewportMotionState>();
    std::mutex mutex;
    quint64 revision = 0;
    std::chrono::steady_clock::time_point requestedAt = std::chrono::steady_clock::now();
    qreal destination = 0;
    // GUI and render jobs can temporarily use different geometry revisions.
    // This accumulated translation maps the job's coordinates into the latest
    // layout; it never resets the animation clock or input-command revision.
    qreal geometryOffset = 0;
    qreal maximumOffset = 0;
    int duration = 150;
    int edgeDirection = 0;
    bool destinationDeadlineReached = false; // protected by mutex
    bool continuous = false;
    qreal velocity = 0;
    QEasingCurve easing;
    QObject *receiver = nullptr; // protected by mutex, cleared before destruction
    std::atomic_bool pending{false};
    std::atomic_bool settled{false};
    std::atomic<qreal> latest{0};
    QPointer<MasonryLayout> layout; // GUI use and GUI-locked synchronization only
    QPointer<QQuickItem> surface;
    QPointer<QQuickItem> scrollBar;
};

namespace {

QList<QQuickItem *> sceneParents(QQuickItem *surface) {
    QList<QQuickItem *> parents{surface};
    for (auto *child : surface->childItems())
        if (child->objectName() == "galleryMasonryVectorBranch"
            || child->objectName() == "galleryMasonryRasterBranch")
            parents.append(child);
    return parents;
}

template<class Visitor>
void visitVectorGates(QQuickItem *item, Visitor visitor) {
    QList<QQuickItem *> pending{item};
    for (int i = 0; i < pending.size(); ++i) {
        if (auto *gate = qobject_cast<GalleryVectorUploadGate *>(pending[i]))
            visitor(gate);
        else
            pending.append(pending[i]->childItems());
    }
}

class ClearCamera final : public QRunnable {
public:
    explicit ClearCamera(std::shared_ptr<GalleryScrollAnimationState> state)
        : _state(std::move(state)) {}
    void run() override {
        auto *surface = _state->surface.data();
        if (!surface) return;
        for (auto *parent : sceneParents(surface)) {
            if (parent != surface)
                if (auto *clip = QQuickItemPrivate::get(parent)->clipNode()) {
                    clip->setRect(parent->boundingRect());
                    clip->update();
                }
            for (auto *child : parent->childItems()) {
                auto *item = qobject_cast<GalleryDelegateItem *>(child);
                if (!item) continue;
                GallerySceneItem::clearNodeViewportMotion(
                    QQuickItemPrivate::get(item)->itemNodeInstance, _state->camera);
                auto *visual = qobject_cast<GalleryEntryVisual *>(item->visualModel());
                if (visual && visual->isVector())
                    visitVectorGates(item, [this](GalleryVectorUploadGate *gate) {
                        GalleryVectorUploadGate::clearNodeViewportMotion(
                            QQuickItemPrivate::get(gate)->itemNodeInstance, _state->camera);
                    });
            }
        }
    }
private:
    std::shared_ptr<GalleryScrollAnimationState> _state;
};

class ScrollJob final : public QQuickAnimatorJob {
public:
    explicit ScrollJob(std::shared_ptr<GalleryScrollAnimationState> state)
        : _state(std::move(state)) {}
    int duration() const override { return -1; }
    void invalidate() override { _node = nullptr; _thumbNode = nullptr; _roots.clear(); _vectors.clear(); _clips.clear(); _clock = nullptr; _clockResolved = false; _clockTimeline.reset(); }
    void writeBack() override {
        // Qt invokes this before the animation's stop notification. A
        // runningChanged observer can run after QML has persisted the old GUI
        // pose, so the handoff must finish here instead.
        QObject *receiver;
        {
            std::lock_guard lock(_state->mutex);
            receiver = _state->receiver;
        }
        if (receiver)
            QMetaObject::invokeMethod(receiver, "prepareNativeStop", Qt::DirectConnection);
    }
    void postSync() override {
        auto *layout = _state->layout.data();
        if (!m_target || !layout || !m_target->parentItem()) { invalidate(); return; }
        if (!_clockResolved && layout->window()) {
            auto *window = layout->window();
            auto *renderer = window->rendererInterface();
            _clock = renderer ? static_cast<const F4AnimationClockInterface *>(
                renderer->getResource(window, "f4.animationClock.v1")) : nullptr;
            _clockResolved = true;
        }
        {
            std::lock_guard lock(_state->mutex);
            const qreal delta = _state->geometryOffset - _geometryOffset;
            if (_started) {
                if (_state->camera->page)
                    _pagePose = qBound<qreal>(0, _pagePose + delta, _state->maximumOffset);
                const qreal translatedVisual = _visual + delta;
                const qreal translatedTarget = _state->edgeDirection
                    ? _state->destination : _to + delta;
                _visual = qBound<qreal>(0, translatedVisual, _state->maximumOffset);
                _to = qBound<qreal>(0, _state->camera->page && _state->revision == _revision
                    ? _state->destination : translatedTarget, _state->maximumOffset);
                if (_state->continuous) {
                    _segmentFrom = _visual;
                } else if (_visual != translatedVisual || _to != translatedTarget) {
                    // Clamping only the endpoint bends the already-running
                    // curve away from its current pose. Solve its origin at
                    // the same eased progress instead of restarting its clock.
                    const qreal progress = _curve.valueForProgress(qBound<qreal>(
                        0, qreal(_time - _start) / qMax(1, _duration), 1));
                    if (qAbs(1 - progress) > 0.000000001)
                        _segmentFrom = (_visual - _to * progress) / (1 - progress);
                    else
                        _visual = _segmentFrom = _to;
                } else {
                    _segmentFrom += delta;
                }
            } else {
                m_from += delta;
            }
            _geometryOffset = _state->geometryOffset;
        }
        _node = QQuickItemPrivate::get(m_target)->itemNodeInstance;
        _base = QMatrix4x4(m_target->itemTransform(m_target->parentItem(), nullptr));
        _logical = -m_target->property("appliedMotionOffset").toPointF().y();
        _viewport = m_target->mapRectFromItem(m_target->parentItem(),
            m_target->parentItem()->boundingRect());
        _thumbNode = nullptr;
        // Snapshot Qt's minimum-size thumb mapping while GUI state is locked.
        // Render frames reuse the matrix and ratio without reading the control.
        if (auto *bar = _state->scrollBar.data(); bar && bar->isVisible()
            && bar->property("orientation").toInt() == Qt::Vertical) {
            auto *thumb = bar->property("contentItem").value<QQuickItem *>();
            const qreal extent = layout->contentHeight();
            const qreal size = bar->property("size").toReal();
            if (thumb && thumb->parentItem() && extent > 0 && size < 1) {
                _thumbNode = QQuickItemPrivate::get(thumb)->itemNodeInstance;
                _thumbBase = QMatrix4x4(thumb->itemTransform(thumb->parentItem(), nullptr));
                _thumbLogical = layout->contentY();
                _thumbScale = bar->property("availableHeight").toReal()
                    * (1 - bar->property("visualSize").toReal()) / (1 - size) / extent;
            }
        }
        _roots.clear(); _vectors.clear(); _clips.clear();
        QSet<int> prepared;
        const auto parents = sceneParents(m_target);
        GalleryItemMappingScope mapping(m_target);
        for (auto *parent : parents) {
            if (parent != m_target)
                if (auto *clip = QQuickItemPrivate::get(parent)->clipNode())
                    _clips.append({clip, parent->boundingRect()});
            for (auto *child : parent->childItems()) {
                auto *item = qobject_cast<GalleryDelegateItem *>(child);
                if (!item) continue;
                auto *node = QQuickItemPrivate::get(item)->itemNodeInstance;
                if (!node) continue;
                const int index = item->viewIndex();
                if (!item->masonryMode() || index < 0 || !item->visualFacadeReady()) {
                    GallerySceneItem::clearNodeViewportMotion(node, _state->camera);
                    continue;
                }
                const bool visible = item->sceneVisible();
                const bool owned = layout->itemForIndex(index) == item;
                const bool matches = owned && ((visible && !_state->camera->page)
                    || layout->isMotionDelegatePrepared(index));
                if (!owned || (!visible && !matches)) {
                    // A rewrap can leave hidden slots at their previous
                    // positions. Keep their resources but exclude them from
                    // drawing and coverage until the layout commits them.
                    GallerySceneItem::setNodeViewportMotion(node, _state->camera, {});
                    continue;
                }
                auto *visual = qobject_cast<GalleryEntryVisual *>(item->visualModel());
                const bool retainPrepared = visual && visual->isVector()
                    && !visual->vectorSource().isEmpty()
                    && (visible || item->retainSceneWhenHidden());
                GallerySceneItem::setNodeViewportMotion(node, _state->camera,
                    mapping.map(item, item->boundingRect()), retainPrepared);
                _roots.append(node);
                if (matches) prepared.insert(index);
                if (visual && visual->isVector())
                    visitVectorGates(item, [this, &mapping](GalleryVectorUploadGate *gate) {
                        if (auto *node = QQuickItemPrivate::get(gate)->itemNodeInstance) {
                            GalleryVectorUploadGate::setNodeViewportMotion(node, _state->camera,
                                mapping.map(gate, gate->boundingRect()), gate->nativeReady());
                            _vectors.append(node);
                        }
                    });
            }
        }
        const auto height = _viewport.height();
        _coverage = {_logical, qMax<qreal>(0, _logical - height),
            qMin(qMax<qreal>(_logical, layout->contentHeight() - layout->height()), _logical + height)};
        const auto candidates = layout->viewportMotionCandidates(_viewport.adjusted(0, -height, 0, height));
        for (int index : candidates)
            if (!prepared.contains(index))
                _coverage.excludeUnprepared(layout->indexGeometry(index), _viewport);
    }
    void updateCurrentTime(int qtTime) override {
        const qreal deltaMs = _clockTimeline.deltaSeconds(qMax(0, qtTime - _qtTime), _clock) * 1000;
        const qreal time = _started ? _time + deltaMs : qtTime;
        _qtTime = qtTime;
        if (_state->continuous) {
            qreal elapsed = deltaMs / 1000;
            qreal velocity, maximum;
            {
                std::lock_guard lock(_state->mutex);
                if (!_started)
                    elapsed = std::chrono::duration<qreal>(
                        std::chrono::steady_clock::now() - _state->requestedAt).count();
                velocity = _state->velocity;
                const qreal delta = _state->geometryOffset - _geometryOffset;
                maximum = qMax<qreal>(0, _state->maximumOffset - delta);
                _revision = _state->revision;
                _to = _state->destination - delta;
            }
            if (!_started) { _started = true; _visual = m_from; }
            _time = time;
            _visual = qBound<qreal>(0, _visual + velocity * qMax<qreal>(0, elapsed), maximum);
            _settled = false;
            return;
        }
        _time = time;
        if (!_started) { _started = true; _visual = m_from; _segmentFrom = m_from; _pagePose = m_from; }
        advance(time);
        {
            std::lock_guard lock(_state->mutex);
            if (_revision != _state->revision) {
                const bool firstCommand = _revision == 0;
                _revision = _state->revision;
                _start = time; _segmentFrom = _visual;
                if (firstCommand) {
                    // The first render tick arrives after the GUI accepted the
                    // request. Preserve that elapsed time instead of spending
                    // another frame at the old pose during animator handoff.
                    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - _state->requestedAt).count();
                    _start -= int(qBound(qint64(0), qint64(age), qint64(_state->duration)));
                }
                _to = _state->destination - (_state->geometryOffset - _geometryOffset);
                _duration = _state->duration;
                _curve = _state->easing;
            }
        }
        advance(time);
    }
    void commit() override {
        if (!_node || !_state->camera->active.load(std::memory_order_acquire)) return;
        qreal constrained = _coverage.constrained(_visual);
        if (_state->camera->page) {
            // Leave room for the next render ticks while the GUI publishes
            // the next band. Running straight into the last prepared row
            // turns several otherwise usable frames into a single jump.
            if (_to > _coverage.maximum && constrained > _pagePose) {
                const qreal step = qMax<qreal>(4, (_coverage.maximum - _pagePose) * .4);
                constrained = qMin(constrained, _pagePose + step);
            } else if (_to < _coverage.minimum && constrained < _pagePose) {
                const qreal step = qMax<qreal>(4, (_pagePose - _coverage.minimum) * .4);
                constrained = qMax(constrained, _pagePose - step);
            }
            _pagePose = constrained;
        }
        if (qAbs(constrained - _visual) > .001) {
            _visual = constrained;
            if (!_state->edgeDirection) {
                if (_state->camera->page)
                    _duration = qMax(1, qCeil(_duration - (_time - _start)));
                _segmentFrom = constrained;
                _start = _time;
                _settled = false;
            }
        }
        auto matrix = _base;
        matrix(1, 3) += float(_logical - _visual);
        _node->setMatrix(matrix);
        if (_thumbNode) {
            auto thumbMatrix = _thumbBase;
            thumbMatrix(1, 3) += float((_visual - _thumbLogical) * _thumbScale);
            _thumbNode->setMatrix(thumbMatrix);
        }
        _state->camera->renderVisibleViewport = _viewport.translated(0, _visual - _logical);
        _state->camera->renderViewport = _state->camera->renderVisibleViewport;
        for (auto *node : _roots) GallerySceneItem::updateNodeViewportMotion(node);
        for (auto *node : _vectors) GalleryVectorUploadGate::updateNodeViewportMotion(node);
        for (const auto &clip : _clips) {
            clip.first->setRect(clip.second.translated(0, _visual - _logical));
            clip.first->update();
        }
        std::lock_guard lock(_state->mutex);
        if (!_state->camera->active.load(std::memory_order_acquire)) return;
        const bool current = _state->revision == _revision
            && _state->geometryOffset == _geometryOffset;
        if (current)
            _state->settled.store(_settled, std::memory_order_release);
        const qreal latest = qBound<qreal>(0,
            _visual + _state->geometryOffset - _geometryOffset, _state->maximumOffset);
        _state->camera->contentY.store(latest, std::memory_order_release);
        // Keep displaying the last prepared region. At the edge deadline the
        // GUI commits the final viewport in one step, including its delegates.
        _state->latest.store(current && _settled && _state->edgeDirection
            ? _state->destination : latest, std::memory_order_release);
        if (_state->receiver && !_state->pending.exchange(true))
            QMetaObject::invokeMethod(_state->receiver, "syncNativePose", Qt::QueuedConnection);
    }
private:
    void advance(qreal time) {
        const qreal t = qBound<qreal>(0, qreal(time - _start) / qMax(1, _duration), 1);
        _visual = _segmentFrom + (_to - _segmentFrom) * _curve.valueForProgress(t);
        _settled = t >= 1;
    }
    std::shared_ptr<GalleryScrollAnimationState> _state;
    QSGTransformNode *_node = nullptr;
    QSGTransformNode *_thumbNode = nullptr;
    QMatrix4x4 _thumbBase;
    qreal _thumbLogical = 0, _thumbScale = 0;
    QMatrix4x4 _base;
    QRectF _viewport;
    GalleryViewportMotionRange _coverage;
    QList<QSGTransformNode *> _roots, _vectors;
    QList<QPair<QQuickDefaultClipNode *, QRectF>> _clips;
    quint64 _revision = 0;
    qreal _logical = 0, _visual = 0, _segmentFrom = 0, _to = 0;
    qreal _geometryOffset = 0;
    qreal _pagePose = 0;
    qreal _time = 0, _start = 0;
    int _qtTime = 0, _duration = 150;
    QEasingCurve _curve;
    const F4AnimationClockInterface *_clock = nullptr;
    GalleryAnimationClock _clockTimeline;
    bool _clockResolved = false;
    bool _started = false;
    bool _settled = false;
};

} // namespace

GalleryScrollAnimation::GalleryScrollAnimation(QObject *parent) : QQuickNumberAnimation(parent) {
    connect(this, &QQuickAbstractAnimation::runningChanged, this, [this](bool running) {
        if (!running) finishNative(_commitOnStop);
    });
    connect(this, &QQuickPropertyAnimation::targetChanged, this, [this] { cancelNative(false); });
    connect(this, &QQuickPropertyAnimation::propertyChanged, this, [this] { cancelNative(false); });
}

GalleryScrollAnimation::~GalleryScrollAnimation() {
    disconnect(this, nullptr, this, nullptr);
    finishNative(false);
}

void GalleryScrollAnimation::setScrollBar(QQuickItem *bar) {
    if (_scrollBar == bar) return;
    cancelNative(true);
    if (_scrollBar) _scrollBar->removeEventFilter(this);
    disconnect(_scrollBarDestroyed);
    _scrollBar = bar;
    if (bar) {
        bar->installEventFilter(this);
        _scrollBarDestroyed = connect(bar, &QObject::destroyed, this, [this] {
            cancelNative(true);
            emit scrollBarChanged();
        });
    }
    emit scrollBarChanged();
}

bool GalleryScrollAnimation::eventFilter(QObject *watched, QEvent *event) {
    // Commit before the control evaluates a press against its logical thumb.
    if (watched == _scrollBar && _native
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin))
        cancelNative(true);
    return QQuickNumberAnimation::eventFilter(watched, event);
}

void GalleryScrollAnimation::setNativeEnabled(bool enabled) {
    if (_nativeEnabled == enabled) return;
    cancelNative(true);
    _nativeEnabled = enabled;
    emit nativeEnabledChanged();
}

bool GalleryScrollAnimation::canUseNative() const {
    return canUseNative(to());
}

bool GalleryScrollAnimation::canUseNative(qreal destination) const {
    auto *layout = qobject_cast<MasonryLayout *>(target());
    auto *window = layout ? layout->window() : nullptr;
    if (!_nativeEnabled || !layout || !window || !layout->isVisible()
        || !window->isVisible() || window->visibility() == QWindow::Minimized
        || !layout->viewport() || !layout->clip()
        || layout->containedPreview() || layout->presentationMode() != MasonryLayout::Masonry
        || property() != "contentY" || duration() <= 0) return false;
    if (!qIsFinite(destination) || destination < 0
        || destination > qMax<qreal>(0, layout->contentHeight() - layout->height())) return false;
    const auto api = window->rendererInterface()->graphicsApi();
    return api != QSGRendererInterface::Software && api != QSGRendererInterface::Unknown;
}

QQuickAbstractAnimation::ThreadingModel GalleryScrollAnimation::threadingModel() const {
    // Native transitions already return a proxy. Rechecking mutable layout
    // eligibility after preparation must not change the returned job's thread.
    return GuiThread;
}

QAbstractAnimationJob *GalleryScrollAnimation::transition(QQuickStateActions &actions,
    QQmlProperties &modified, TransitionDirection direction, QObject *defaultTarget) {
    if (!canUseNative())
        return QQuickNumberAnimation::transition(actions, modified, direction, defaultTarget);
    auto *layout = qobject_cast<MasonryLayout *>(target());
    auto state = std::make_shared<GalleryScrollAnimationState>();
    state->layout = layout; state->surface = layout->viewport();
    state->scrollBar = _scrollBar;
    state->receiver = this;
    state->latest.store(layout->contentY());
    state->camera->contentY.store(layout->contentY());
    state->camera->targetY.store(layout->contentY());
    state->camera->active.store(true, std::memory_order_release);
    state->revision = 1; state->destination = to();
    state->maximumOffset = qMax<qreal>(0, layout->contentHeight() - layout->height());
    state->duration = duration(); state->easing = easing();
    state->edgeDirection = _edgeDirectionRequested;
    state->camera->edgeJump = state->edgeDirection != 0;
    state->camera->page = _pageRequested;
    state->continuous = _continuousRequested;
    state->camera->prepareAlongPath = !state->camera->edgeJump && !state->continuous;
    state->velocity = _continuousVelocity;
    _native = state;
    layout->setViewportMotionState(state->camera);
    auto cancel = [this] { cancelNative(false); };
    _nativeConnections.append(connect(layout, &QQuickItem::widthChanged, this, cancel));
    _nativeConnections.append(connect(layout, &QQuickItem::heightChanged, this, cancel));
    _nativeConnections.append(connect(layout, &MasonryLayout::presentationModeChanged, this, cancel));
    _nativeConnections.append(connect(layout, &MasonryLayout::modelChanged, this, cancel));
    const auto geometryChanged = [this, layout] {
        if (!layout->rewrapInProgress()) cancelNative(false);
    };
    _nativeConnections.append(connect(layout, &MasonryLayout::contentHeightChanged, this, geometryChanged));
    _nativeConnections.append(connect(layout, &MasonryLayout::layoutBandsChanged, this, geometryChanged));
    _nativeConnections.append(connect(layout, &MasonryLayout::rewrapAboutToBegin,
        this, &GalleryScrollAnimation::beginGeometryChange));
    _nativeConnections.append(connect(layout, &MasonryLayout::rewrapFinished,
        this, &GalleryScrollAnimation::endGeometryChange));
    _nativeConnections.append(connect(layout, &MasonryLayout::devicePixelRatioChanged, this, cancel));
    _nativeConnections.append(connect(layout, &QQuickItem::windowChanged, this, cancel));
    _nativeConnections.append(connect(layout, &QObject::destroyed, this, cancel));
    // Visibility changes preserve the same document and geometry. Keep the
    // camera pose reached while GUI synchronization was temporarily delayed.
    const auto suspend = [this] { cancelNative(true); };
    _nativeConnections.append(connect(layout, &QQuickItem::visibleChanged, this, suspend));
    _nativeConnections.append(connect(layout->window(), &QWindow::visibleChanged, this, suspend));
    _nativeConnections.append(connect(layout->window(), &QWindow::visibilityChanged, this,
        [this](QWindow::Visibility visibility) {
            if (visibility == QWindow::Hidden || visibility == QWindow::Minimized)
                cancelNative(true);
        }));
    if (auto *model = layout->model())
        _nativeConnections.append(connect(model, &QAbstractItemModel::modelAboutToBeReset, this, cancel));
    layout->prepareViewportMotionTarget(to());
    auto *job = new ScrollJob(state);
    job->setTarget(layout->viewport());
    job->setFrom(layout->contentY()); job->setTo(to());
    ++_nativeJobStarts;
    emit nativeRunningChanged();
    return new QQuickAnimatorProxyJob(initInstance(job), this);
}

bool GalleryScrollAnimation::continuousRunning() const {
    return _native && _native->continuous;
}

bool GalleryScrollAnimation::setContinuousVelocity(qreal velocity) {
    auto *layout = qobject_cast<MasonryLayout *>(target());
    if (!layout || !qIsFinite(velocity) || velocity == 0) return false;
    const qreal maximum = qMax<qreal>(0, layout->contentHeight() - layout->height());
    const qreal destination = qBound<qreal>(0, layout->contentY() + velocity * .15, maximum);
    if (!canUseNative(destination)) return false;
    if (continuousRunning() && isRunning()) {
        {
            std::lock_guard lock(_native->mutex);
            _native->velocity = velocity;
            _native->destination = destination;
            ++_native->revision;
        }
        setTo(destination);
        layout->prepareViewportMotionTarget(destination);
        return true;
    }
    cancelNative(true);
    setTo(destination);
    _continuousRequested = true;
    _continuousVelocity = velocity;
    QQuickNumberAnimation::restart();
    _continuousRequested = false;
    return continuousRunning();
}

void GalleryScrollAnimation::stopContinuous() {
    if (continuousRunning()) cancelNative(true);
}

void GalleryScrollAnimation::retarget(int edgeDirection) {
    retargetMotion(edgeDirection, false);
}

void GalleryScrollAnimation::retargetPage() {
    retargetMotion(0, true);
}

void GalleryScrollAnimation::retargetMotion(int edgeDirection, bool page) {
    if (continuousRunning()) cancelNative(true);
    if (!_native || !isRunning() || !canUseNative()
        || _native->edgeDirection != edgeDirection || _native->camera->page != page) {
        _edgeDirectionRequested = edgeDirection;
        _pageRequested = page;
        QQuickNumberAnimation::restart();
        _pageRequested = false;
        _edgeDirectionRequested = 0;
        if (_native && edgeDirection) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - _native->requestedAt);
            const auto remaining = qMax(std::chrono::milliseconds::zero(),
                std::chrono::milliseconds(duration()) - age);
            QTimer::singleShot(remaining, Qt::PreciseTimer, this,
                [this, weak = std::weak_ptr(_native)] {
                    const auto state = weak.lock();
                    if (state && _native == state) completeAtDestination();
                });
        }
        return;
    }
    {
        std::lock_guard lock(_native->mutex);
        // Repeated wheel input can clamp to the same destination. Preserve the
        // running segment and its deadline when the command has not changed.
        if (_native->destination == to() && _native->duration == duration()
            && _native->easing == easing()) return;
        _native->destination = to(); _native->duration = duration(); _native->easing = easing();
        _native->requestedAt = std::chrono::steady_clock::now();
        ++_native->revision;
        _native->settled.store(false, std::memory_order_release);
    }
    _native->layout->prepareViewportMotionTarget(to());
}

void GalleryScrollAnimation::completeAtDestination() {
    if (!_native || !isRunning()) return;
    {
        std::lock_guard lock(_native->mutex);
        _native->destinationDeadlineReached = true;
    }
    QQuickNumberAnimation::stop();
}

void GalleryScrollAnimation::syncNativePose() {
    if (!_native) return;
    auto state = _native;
    const bool page = state->camera->page;
    if (!page) state->pending.store(false, std::memory_order_release);
    const auto finishSync = qScopeGuard([&] {
        if (page) state->pending.store(false, std::memory_order_release);
    });
    if (!isRunning() || !state->layout || _geometryChanging
        || !state->camera->active.load()) return;
    if (state->edgeDirection && state->settled.load(std::memory_order_acquire)) {
        QQuickNumberAnimation::stop();
        return;
    }
    state->layout->setContentY(state->latest.load(std::memory_order_acquire));
    if (_native != state) return;
    if (state->continuous) {
        qreal destination;
        {
            std::lock_guard lock(state->mutex);
            destination = qBound<qreal>(0, state->latest.load() + state->velocity * .15,
                state->maximumOffset);
            state->destination = destination;
        }
        setTo(destination);
        state->layout->prepareViewportMotionTarget(destination);
        return;
    }
    if (state->settled.load(std::memory_order_acquire)) {
        QQuickNumberAnimation::stop();
    }
}

void GalleryScrollAnimation::beginGeometryChange() {
    if (!_native || _geometryChanging) return;
    auto state = _native;
    _geometryChanging = true;
    _geometryPageTargetIndex = -1;
    if (state->camera->page) {
        const int index = state->layout->currentIndex();
        const QRectF target = state->layout->indexGeometry(index);
        if (!target.isEmpty()) {
            std::lock_guard lock(state->mutex);
            if (state->destination > .01 && state->destination < state->maximumOffset - .01) {
                _geometryPageTargetIndex = index;
                _geometryPageTargetInset = target.top() - state->destination;
            }
        }
    }
    // Capture the old geometry's anchor from the committed camera, including
    // render frames produced while the GUI thread was busy. The layout still
    // owns the choice of entry anchor and restores it in its normal rewrap.
    state->layout->setContentY(state->latest.load(std::memory_order_acquire));
    if (_native != state) return;
    _geometryStartPose = state->layout->contentY();
}

bool GalleryScrollAnimation::rebasingPage() const {
    return _native && _native->camera->page && _geometryChanging;
}

void GalleryScrollAnimation::endGeometryChange() {
    if (!_native || !_geometryChanging) return;
    auto state = _native;
    auto *layout = state->layout.data();
    const qreal delta = layout->contentY() - _geometryStartPose;
    const qreal maximum = qMax<qreal>(0, layout->contentHeight() - layout->height());
    const QRectF pageTarget = layout->indexGeometry(_geometryPageTargetIndex);
    qreal destination;
    {
        std::lock_guard lock(state->mutex);
        state->geometryOffset += delta;
        state->maximumOffset = maximum;
        destination = state->edgeDirection > 0 ? maximum
            : state->edgeDirection < 0 ? 0
            : qBound<qreal>(0, state->destination + delta, maximum);
        if (state->camera->page) {
            if (state->destination >= state->maximumOffset - .01)
                destination = maximum;
            else if (state->destination <= .01)
                destination = 0;
            else if (!pageTarget.isEmpty() && _geometryPageTargetIndex == layout->currentIndex())
                destination = qBound<qreal>(0, pageTarget.top() - _geometryPageTargetInset, maximum);
        }
        state->destination = destination;
        const qreal latest = qBound<qreal>(0, state->latest.load() + delta, maximum);
        state->latest.store(latest, std::memory_order_release);
        state->camera->contentY.store(latest, std::memory_order_release);
        state->settled.store(false, std::memory_order_release);
    }
    _geometryChanging = false;
    // Wheel/key repeat accumulates from NumberAnimation::to. Publish the same
    // rebased endpoint to QML and bounded target-leading delegate preparation.
    setTo(destination);
    layout->prepareViewportMotionTarget(destination);
}

void GalleryScrollAnimation::cancelNative(bool commitPose) {
    if (!_native) return;
    _commitOnStop = commitPose;
    QQuickNumberAnimation::stop();
    _commitOnStop = true;
}

void GalleryScrollAnimation::prepareNativeStop() {
    finishNative(_commitOnStop);
}

void GalleryScrollAnimation::finishNative(bool commitPose) {
    if (!_native) return;
    auto state = std::exchange(_native, {});
    _geometryChanging = false;
    for (auto connection : std::as_const(_nativeConnections)) disconnect(connection);
    _nativeConnections.clear();
    qreal latest;
    {
        std::lock_guard lock(state->mutex);
        state->camera->active.store(false, std::memory_order_release);
        state->receiver = nullptr;
        latest = state->destinationDeadlineReached ? state->destination
            : state->latest.load(std::memory_order_acquire);
    }
    if (state->layout) {
        state->layout->clearViewportMotionState(state->camera);
        if (commitPose) state->layout->setContentY(latest);
    }
    if (state->surface) {
        if (auto *window = state->surface->window())
            window->scheduleRenderJob(new ClearCamera(state), QQuickWindow::AfterSynchronizingStage);
        QQuickItemPrivate::get(state->surface)->dirty(QQuickItemPrivate::Transform);
    }
    if (auto *bar = state->scrollBar.data())
        if (auto *thumb = bar->property("contentItem").value<QQuickItem *>())
            QQuickItemPrivate::get(thumb)->dirty(QQuickItemPrivate::Transform);
    emit nativeRunningChanged();
}
