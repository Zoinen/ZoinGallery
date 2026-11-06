#include "GalleryComponentHost.h"
#include "GallerySceneItem.h"

#include <QQmlContext>
#include <QQmlIncubator>
#include <QtQml/qqmlinfo.h>
#include <QtQuick/private/qquickitem_p.h>
#include <utility>

namespace {
void setIncubationDrawing(QQuickItem *item, bool ready) {
    if (auto *scene = qobject_cast<GallerySceneItem *>(item))
        scene->setIncubationReady(ready);
    else
        QQuickItemPrivate::get(item)->setCulled(!ready);
}
}

class GalleryComponentHost::Incubation final : public QQmlIncubator,
        public std::enable_shared_from_this<Incubation> {
public:
    Incubation(GalleryComponentHost *owner, IncubationMode mode)
        : QQmlIncubator(mode), host(owner) {}
    QPointer<GalleryComponentHost> host;
    QPointer<QQuickItem> candidate;
    bool fitWidth = false;
    bool fitHeight = false;
protected:
    void setInitialState(QObject *object) override {
        if (!host) return;
        object->setParent(host);
        candidate = qobject_cast<QQuickItem *>(object);
        if (!candidate) return;
        auto *itemPrivate = QQuickItemPrivate::get(candidate);
        fitWidth = !itemPrivate->widthValid();
        fitHeight = !itemPrivate->heightValid();
        if (fitWidth) candidate->setWidth(host->width());
        if (fitHeight) candidate->setHeight(host->height());
        setIncubationDrawing(candidate, false);
        candidate->setParentItem(host);
    }
    void statusChanged(Status status) override {
        // Publishing can synchronously change source or destroy the host.
        const auto keepAlive = shared_from_this();
        if (host && (status == Ready || status == Error)) host->complete(this);
    }
};

GalleryComponentHost::GalleryComponentHost(QQuickItem *parent) : QQuickItem(parent) {}

GalleryComponentHost::~GalleryComponentHost() {
    cancel();
}

void GalleryComponentHost::setSourceComponent(QQmlComponent *component) {
    if (_source == component) return;
    disconnect(_sourceStatusConnection);
    disconnect(_sourceDestroyedConnection);
    _source = component;
    if (component) {
        _sourceStatusConnection = connect(component, &QQmlComponent::statusChanged,
            this, [this] { start(); });
        _sourceDestroyedConnection = connect(component, &QObject::destroyed, this, [this] {
            _source = nullptr;
            QPointer<GalleryComponentHost> guard(this);
            start();
            if (guard) emit sourceComponentChanged();
        });
    }
    QPointer<GalleryComponentHost> guard(this);
    start();
    if (guard) emit sourceComponentChanged();
}

void GalleryComponentHost::setActive(bool active) {
    if (_active == active) return;
    _active = active;
    QPointer<GalleryComponentHost> guard(this);
    start();
    if (guard) emit activeChanged();
}

void GalleryComponentHost::setAsynchronous(bool asynchronous) {
    if (_asynchronous == asynchronous) return;
    _asynchronous = asynchronous;
    QPointer<GalleryComponentHost> guard(this);
    if (!asynchronous && _pending) _pending->forceCompletion();
    if (guard) emit asynchronousChanged();
}

void GalleryComponentHost::componentComplete() {
    QQuickItem::componentComplete();
    start();
}

void GalleryComponentHost::setStatus(int status) {
    if (_status == status) return;
    _status = status;
    emit statusChanged();
}

void GalleryComponentHost::cancel() {
    const auto pending = std::exchange(_pending, {});
    if (!pending) return;
    pending->host = nullptr;
    const QPointer<QQuickItem> candidate = pending->candidate;
    pending->clear();
    if (candidate && candidate != _item) delete candidate;
}

void GalleryComponentHost::discardItem() {
    const QPointer<QQuickItem> previous = std::exchange(_item, nullptr);
    if (!previous) return;
    QPointer<GalleryComponentHost> guard(this);
    setIncubationDrawing(previous, false);
    previous->deleteLater();
    if (guard) emit itemChanged();
}

void GalleryComponentHost::start() {
    if (!isComponentComplete()) return;
    QPointer<GalleryComponentHost> guard(this);
    const auto generation = ++_generation;
    cancel();
    if (!guard || generation != _generation) return;
    if (!_active || !_source) {
        discardItem();
        if (guard && generation == _generation) setStatus(QQmlIncubator::Null);
        return;
    }
    if (_source->isError()) {
        discardItem();
        if (guard && generation == _generation) setStatus(QQmlIncubator::Error);
        return;
    }
    setStatus(QQmlIncubator::Loading);
    if (!guard || generation != _generation) return;
    if (!_source->isReady()) return;
    auto pending = std::make_shared<Incubation>(this, _asynchronous
        ? QQmlIncubator::Asynchronous : QQmlIncubator::AsynchronousIfNested);
    _pending = pending;
    auto *context = _source->creationContext();
    if (!context) context = qmlContext(this);
    _source->create(*pending, context);
}

void GalleryComponentHost::complete(Incubation *incubation) {
    if (_pending.get() != incubation) return;
    const auto pending = std::exchange(_pending, {});
    QPointer<GalleryComponentHost> guard(this);
    const auto generation = _generation;
    QPointer<QQuickItem> candidate = pending->candidate;
    if (!pending->isReady() || !candidate) {
        if (!pending->errors().isEmpty()) qmlWarning(this, pending->errors());
        QPointer<QObject> rejected = pending->object();
        if (!rejected) rejected = candidate;
        pending->host = nullptr;
        pending->clear();
        if (rejected) delete rejected;
        if (!guard) return;
        discardItem();
        if (guard && generation == _generation) setStatus(QQmlIncubator::Error);
        return;
    }
    _fitWidth = pending->fitWidth;
    _fitHeight = pending->fitHeight;
    const QPointer<QQuickItem> previous = std::exchange(_item, candidate);
    pending->host = nullptr;
    pending->clear();
    if (_fitWidth) candidate->setWidth(width());
    if (_fitHeight) candidate->setHeight(height());
    setIncubationDrawing(candidate, true);
    if (previous) {
        setIncubationDrawing(previous, false);
        previous->deleteLater();
    }
    if (!guard || generation != _generation) return;
    emit itemChanged();
    if (!guard || generation != _generation || _item != candidate) return;
    setStatus(QQmlIncubator::Ready);
    if (guard && generation == _generation && _item == candidate) emit loaded();
}

void GalleryComponentHost::geometryChange(const QRectF &geometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(geometry, oldGeometry);
    if (_item) {
        if (_fitWidth) _item->setWidth(width());
        if (_fitHeight) _item->setHeight(height());
    }
    if (_pending && _pending->candidate) {
        if (_pending->fitWidth) _pending->candidate->setWidth(width());
        if (_pending->fitHeight) _pending->candidate->setHeight(height());
    }
}
