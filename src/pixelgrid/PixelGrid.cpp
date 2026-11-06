#include <ZoinGallery/PixelGrid.h>

#include <QEvent>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <mutex>
#include <QMatrix4x4>
#include <QMetaMethod>
#include <QQuickWindow>
#include <QSet>
#include <QThread>
#include <QVector>
#include <utility>

namespace ZoinGallery {

void registerPixelGridTypes() {
    static std::once_flag once;
    std::call_once(once, [] {
        constexpr auto uri = "ZoinGallery.PixelGrid";
        qmlRegisterType<PixelGridScope>(uri, 1, 0, "PixelGridScope");
        qmlRegisterUncreatableType<PixelGridContext>(uri, 1, 0,
            "PixelGridContext", QStringLiteral("The grid belongs to its window"));
        qmlRegisterUncreatableType<PixelGridAlignment>(uri, 1, 0,
            "PixelGridAlignment", QStringLiteral("Use the attached grid offset on a visual item"));
    });
}

namespace {
Q_LOGGING_CATEGORY(pixelGridTopologyLog, "zoingallery.pixelgrid.topology", QtWarningMsg)

bool isGridBoundary(QQuickItem *item) {
    return item && (qobject_cast<PixelGridScope *>(item) || item->property("_zoinPixelGridScope").toBool());
}
}

struct PixelGridContext::Private {
    struct Node : QObject {
        explicit Node(QObject *owner) : QObject(owner) {}
        QPointer<QQuickItem> item;
        Node *parentNode = nullptr;
        QSet<Node *> children;
        QSet<PixelGridAlignment *> targets;
        QSet<PixelGridAlignment *> deferred;
        QList<QMetaObject::Connection> connections;
        int references = 0;
        bool boundary = false;
        bool motionActive = false;
        bool ownerChecked = false;
    };
    PixelGridContext *q;
    QPointer<QQuickWindow> window;
    GridMetrics metrics;
    quint64 revision = 1;
    QHash<QQuickItem *, Node *> nodes;
    QHash<PixelGridAlignment *, QList<Node *>> chains;
    QSet<Node *> dirty;
    QSet<PixelGridAlignment *> reparented;
    bool flushing = false;
    bool collecting = false;
    quint64 visits = 0, maps = 0, topology = 0, flushes = 0;
    quint64 boundaryChecks = 0;

    void mark(Node *node) {
        if (!node) return;
        const bool requestFrame = dirty.isEmpty() && !flushing;
        dirty.insert(node);
        if (requestFrame && window) window->update();
    }
    void collect(Node *node, QSet<PixelGridAlignment *> &targets) {
        targets.unite(node->targets);
        for (auto *child : std::as_const(node->children)) collect(child, targets);
    }
    Node *watch(QQuickItem *item) {
        if (auto *existing = nodes.value(item)) { ++existing->references; return existing; }
        auto *node = new Node(q);
        node->item = item;
        node->references = 1;
        node->boundary = isGridBoundary(item);
        node->motionActive = node->boundary
            && item->property("_zoinPixelGridMotionActive").toBool();
        if (collecting) ++boundaryChecks;
        nodes.insert(item, node);
        const auto changed = [this, node] { mark(node); };
        node->connections = {
            QObject::connect(item, &QQuickItem::xChanged, q, changed),
            QObject::connect(item, &QQuickItem::yChanged, q, changed),
            QObject::connect(item, &QQuickItem::widthChanged, q, changed),
            QObject::connect(item, &QQuickItem::heightChanged, q, changed),
            QObject::connect(item, &QQuickItem::scaleChanged, q, changed),
            QObject::connect(item, &QQuickItem::rotationChanged, q, changed),
            QObject::connect(item, &QQuickItem::transformOriginChanged, q, changed),
            QObject::connect(item, &QQuickItem::visibleChanged, q, [this, node] {
                mark(node);
                if (node->item && node->item->isVisible()) {
                    QSet<PixelGridAlignment *> shown;
                    collect(node, shown);
                    for (auto *target : shown) q->invalidate(target);
                }
            }),
            QObject::connect(item, &QQuickItem::parentChanged, q, [this, node] {
                node->ownerChecked = false;
                collect(node, reparented);
                mark(node);
            })
        };
        if (collecting) ++topology;
        return node;
    }
    void setParentNode(Node *node, Node *parent) {
        auto *previous = node->parentNode;
        if (previous == parent) return;
        if (previous) previous->children.remove(node);
        node->parentNode = parent;
        if (parent) parent->children.insert(node);
        if (previous && parent)
            qCDebug(pixelGridTopologyLog) << "[FIX] Moved alignment subtree"
                                         << node->item.data() << previous->item.data()
                                         << parent->item.data();
    }
    void release(Node *node) {
        if (--node->references) return;
        for (const auto &connection : std::as_const(node->connections)) QObject::disconnect(connection);
        setParentNode(node, nullptr);
        if (node->item) nodes.remove(node->item);
        else for (auto it = nodes.begin(); it != nodes.end(); ++it)
            if (it.value() == node) { nodes.erase(it); break; }
        dirty.remove(node);
        node->deleteLater();
        if (collecting) ++topology;
    }
    void fit(Node *node) {
        if (!node->item || !node->item->isVisible()) return;
        // Parents first: inherited corrections are installed before descendants
        // read their final uncorrected position. No depth calculation or sort.
        const auto targets = node->targets;
        for (auto *alignment : targets) {
            if (!alignment->isEnabled()) continue;
            auto *scope = nodes.value(alignment->m_scope);
            if (scope && scope->motionActive) {
                scope->deferred.insert(alignment);
                continue;
            }
            if (collecting) ++visits;
            alignment->align();
        }
        // An aligned scope owns the phase of its children. Moving its parent
        // changes only this boundary, not any cached local child coordinates.
        if (node->boundary) return;
        const auto children = node->children;
        for (auto *child : children) fit(child);
    }
};

PixelGridContext *PixelGridContext::forWindow(QQuickWindow *window) {
    if (!window) return nullptr;
    auto *context = window->findChild<PixelGridContext *>(QStringLiteral("_zoinPixelGrid"), Qt::FindDirectChildrenOnly);
    return context ? context : new PixelGridContext(window);
}

PixelGridContext::PixelGridContext(QQuickWindow *window) : QObject(window), d(std::make_unique<Private>()) {
    setObjectName(QStringLiteral("_zoinPixelGrid"));
    d->q = this;
    d->window = window;
    d->metrics = GridMetrics(window->effectiveDevicePixelRatio());
    window->installEventFilter(this);
    connect(window, &QQuickWindow::afterAnimating, this, &PixelGridContext::flush);
}
PixelGridContext::~PixelGridContext() = default;
qreal PixelGridContext::dpr() const { return d->metrics.dpr; }
quint64 PixelGridContext::revision() const { return d->revision; }

bool PixelGridContext::eventFilter(QObject *object, QEvent *event) {
    if (object == d->window && event->type() == QEvent::DevicePixelRatioChange) {
        d->metrics = GridMetrics(d->window->effectiveDevicePixelRatio());
        ++d->revision;
        for (auto it = d->chains.cbegin(); it != d->chains.cend(); ++it) d->mark(it->first());
        emit metricsChanged();
    }
    return QObject::eventFilter(object, event);
}

void PixelGridContext::add(PixelGridAlignment *alignment) {
    auto *item = alignment->item();
    if (!item || d->chains.contains(alignment)) return;
    QList<Private::Node *> chain;
    auto *boundary = d->window->contentItem();
    for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
        auto *node = d->watch(ancestor);
        auto *owner = ancestor->parentItem();
        if (!node->ownerChecked) {
            node->ownerChecked = true;
            if (!node->boundary && owner
                && owner->metaObject()->indexOfProperty("moving") >= 0
                && owner->property("contentItem").value<QQuickItem *>() == ancestor) {
                // Publish before creating the adapter: its own alignment can
                // register recursively through this same watched node.
                node->boundary = true;
                qmlAttachedPropertiesObject<PixelGridScope>(ancestor, true);
            }
        }
        chain.append(node);
        if (ancestor == boundary) break;
    }
    for (qsizetype i = 0; i + 1 < chain.size(); ++i)
        d->setParentNode(chain[i], chain[i + 1]);
    d->setParentNode(chain.last(), nullptr);
    chain.first()->targets.insert(alignment);
    d->chains.insert(alignment, chain);
    alignment->m_scope = nullptr;
    for (qsizetype i = 1; i < chain.size(); ++i)
        if (chain[i]->boundary) { alignment->m_scope = chain[i]->item; break; }
    d->mark(chain.first());
}
void PixelGridContext::remove(PixelGridAlignment *alignment) {
    d->reparented.remove(alignment);
    const auto chain = d->chains.take(alignment);
    if (chain.isEmpty()) return;
    chain.first()->targets.remove(alignment);
    for (auto *node : chain) {
        node->deferred.remove(alignment);
        d->release(node);
    }
}
void PixelGridContext::invalidate(PixelGridAlignment *alignment) {
    const auto it = d->chains.constFind(alignment);
    if (it != d->chains.cend()) d->mark(it->first());
}
void PixelGridContext::invalidateScope(QQuickItem *item) {
    if (auto *node = d->nodes.value(item)) {
        // Scope installation can happen after descendants have registered.
        // Re-evaluate once here, rather than reflecting on every ancestor for
        // every descendant during the resulting chain rebuild.
        node->boundary = isGridBoundary(item);
        node->motionActive = node->boundary
            && item->property("_zoinPixelGridMotionActive").toBool();
        if (d->collecting) ++d->boundaryChecks;
        d->collect(node, d->reparented);
        d->mark(node);
    }
}
void PixelGridContext::setScopeMotionActive(QQuickItem *item, bool active) {
    auto *node = d->nodes.value(item);
    if (!node || node->motionActive == active) return;
    node->motionActive = active;
    if (!active) {
        const auto deferred = std::exchange(node->deferred, {});
        for (auto *alignment : deferred) invalidate(alignment);
    }
}
void PixelGridContext::flushWindow(QQuickWindow *window) { if (auto *context = forWindow(window)) context->flush(); }
void PixelGridContext::flush() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->flushing || d->dirty.isEmpty()) return;
    d->flushing = true;
    if (d->collecting) ++d->flushes;
    const auto moved = std::exchange(d->reparented, {});
    for (auto *alignment : moved) { remove(alignment); add(alignment); }
    // Corrections only translate painted content. A binding may nevertheless
    // update local geometry; consume that bounded dirty work before sync too.
    for (int pass = 0; pass < 8 && !d->dirty.isEmpty(); ++pass) {
        const auto dirty = std::exchange(d->dirty, {});
        for (auto *node : dirty) {
            bool inherited = false;
            for (auto *parent = node->parentNode; parent; parent = parent->parentNode) {
                if (parent->boundary) break;
                if (dirty.contains(parent)) { inherited = true; break; }
            }
            if (!inherited) d->fit(node);
        }
    }
    d->flushing = false;
    if (!d->dirty.isEmpty() && d->window) d->window->update();
}
QVariantMap PixelGridContext::counters() const {
    return {{"alignmentVisits", d->visits}, {"sceneMaps", d->maps}, {"topologyChanges", d->topology},
            {"flushes", d->flushes}, {"registeredLeaves", d->chains.size()}, {"watchedItems", d->nodes.size()},
            {"boundaryChecks", d->boundaryChecks}};
}
void PixelGridContext::resetCounters() { d->collecting = true; d->visits = d->maps = d->topology = d->flushes = d->boundaryChecks = 0; }

PixelGridAlignment::PixelGridAlignment(QObject *parent) : QObject(parent), m_item(qobject_cast<QQuickItem *>(parent)) {
    if (m_item) {
        connect(m_item, &QQuickItem::windowChanged, this, &PixelGridAlignment::attachWindow);
        attachWindow();
    }
}
PixelGridAlignment::~PixelGridAlignment() { if (m_context) m_context->remove(this); }
PixelGridAlignment *PixelGridAlignment::qmlAttachedProperties(QObject *object) { return new PixelGridAlignment(object); }
PixelGridAlignment *PixelGridAlignment::forItem(QQuickItem *item) {
    return item ? qobject_cast<PixelGridAlignment *>(qmlAttachedPropertiesObject<PixelGridAlignment>(item, true)) : nullptr;
}
void PixelGridAlignment::attachWindow() {
    if (m_context) m_context->remove(this);
    m_context = PixelGridContext::forWindow(m_item ? m_item->window() : nullptr);
    if (m_context) m_context->add(this);
    emit gridChanged();
}
void PixelGridAlignment::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit enabledChanged();
    if (enabled) invalidate();
}
void PixelGridAlignment::invalidate() { if (m_context) m_context->invalidate(this); }
void PixelGridAlignment::align() {
    if (!m_item || !m_item->parentItem() || !m_context || !m_item->window()) return;
    auto *parent = m_item->parentItem();
    auto *boundary = m_scope ? m_scope.data() : m_item->window()->contentItem();
    const QPointF origin = parent->mapToItem(boundary, m_item->position());
    const QPointF snapped = GridMetrics(m_context->dpr()).snap(origin);
    const QPointF offset = parent->mapFromItem(boundary, snapped) - m_item->position();
    if (m_context->d->collecting) m_context->d->maps += 2;
    if (qAbs(offset.x() - m_offset.x()) < 1e-7 && qAbs(offset.y() - m_offset.y()) < 1e-7) return;
    m_offset = offset;
    emit offsetChanged();
}

class PixelGridScope::Translation final : public QQuickTransform {
public:
    explicit Translation(QQuickItem *item) : QQuickTransform(item) { appendToItem(item); }
    QPointF offset;
    void set(QPointF value) { if (offset != value) { offset = value; update(); } }
    void applyTo(QMatrix4x4 *matrix) const override { matrix->translate(offset.x(), offset.y()); }
};
PixelGridScope::PixelGridScope(QQuickItem *parent) : QQuickItem(parent),
    m_translation(new Translation(this)), m_alignment(PixelGridAlignment::forItem(this)) {
    connect(this, &QQuickItem::activeFocusOnTabChanged, this, &PixelGridScope::tabFocusChanged);
    connect(m_alignment, &PixelGridAlignment::offsetChanged, this, &PixelGridScope::updateTranslation);
}
PixelGridScope::~PixelGridScope() = default;
qreal PixelGridScope::gridDpr() const { return window() ? window()->effectiveDevicePixelRatio() : 1; }
void PixelGridScope::setMotionOffset(QPointF offset) {
    if (offset == m_motionOffset) return;
    m_motionOffset = offset;
    updateTranslation();
    emit motionOffsetChanged();
}
void PixelGridScope::setMotionActive(bool active) {
    if (m_motionActive == active) return;
    m_motionActive = active;
    setProperty("_zoinPixelGridMotionActive", active);
    m_alignment->setEnabled(!active);
    if (auto *grid = m_alignment->grid()) grid->setScopeMotionActive(this, active);
    updateTranslation();
    emit motionActiveChanged();
}
void PixelGridScope::updateTranslation() {
    const QPointF applied = m_motionActive ? m_motionOffset : GridMetrics(gridDpr()).snap(m_motionOffset);
    m_translation->set(m_alignment->offset() + applied);
    if (m_appliedOffset != applied) { m_appliedOffset = applied; emit appliedMotionOffsetChanged(); }
}
void PixelGridScope::invalidate() { m_alignment->invalidate(); }
void PixelGridScope::itemChange(ItemChange change, const ItemChangeData &data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemDevicePixelRatioHasChanged) {
        updateTranslation();
        invalidate();
        emit gridDprChanged();
    }
}

class PixelGridScopeAttached::Translation final : public QQuickTransform {
public:
    explicit Translation(QQuickItem *item) : QQuickTransform(item) { appendToItem(item); }
    QPointF offset;
    void set(QPointF value) { if (offset != value) { offset = value; update(); } }
    void applyTo(QMatrix4x4 *matrix) const override { matrix->translate(offset.x(), offset.y()); }
};
PixelGridScopeAttached *PixelGridScope::qmlAttachedProperties(QObject *object) {
    return new PixelGridScopeAttached(object);
}
PixelGridScopeAttached::PixelGridScopeAttached(QObject *parent) : QObject(parent), m_item(qobject_cast<QQuickItem *>(parent)) {
    if (!m_item) return;
    m_item->setProperty("_zoinPixelGridScope", true);
    m_alignment = PixelGridAlignment::forItem(m_item);
    m_translation = new Translation(m_item);
    connect(m_alignment, &PixelGridAlignment::offsetChanged, this, &PixelGridScopeAttached::updateTranslation);
    connect(m_item, &QQuickItem::windowChanged, this, &PixelGridScopeAttached::invalidate);
    if (auto *owner = m_item->parentItem()) {
        const auto *meta = owner->metaObject();
        const int signal = meta->indexOfSignal("movingChanged()");
        if (signal >= 0)
            connect(owner, meta->method(signal), this,
                    metaObject()->method(metaObject()->indexOfSlot("updateOwnerMotion()")));
        updateOwnerMotion();
    }
    invalidate();
}
PixelGridScopeAttached::~PixelGridScopeAttached() = default;
void PixelGridScopeAttached::setMotionActive(bool active) {
    if (m_motionActive == active) return;
    m_motionActive = active;
    if (m_item) m_item->setProperty("_zoinPixelGridMotionActive", active);
    if (m_alignment) m_alignment->setEnabled(!active);
    if (m_alignment && m_alignment->grid())
        m_alignment->grid()->setScopeMotionActive(m_item, active);
    emit motionActiveChanged();
}
void PixelGridScopeAttached::updateTranslation() {
    if (m_translation && m_alignment) m_translation->set(m_alignment->offset());
}
void PixelGridScopeAttached::invalidate() {
    if (m_item && m_item->window()) PixelGridContext::forWindow(m_item->window())->invalidateScope(m_item);
}
void PixelGridScopeAttached::updateOwnerMotion() {
    if (m_item && m_item->parentItem())
        setMotionActive(m_item->parentItem()->property("moving").toBool());
}

} // namespace ZoinGallery

// Shared controls can load before a gallery session exists.
static void registerSharedPixelGrid() { ZoinGallery::registerPixelGridTypes(); }
Q_COREAPP_STARTUP_FUNCTION(registerSharedPixelGrid)
