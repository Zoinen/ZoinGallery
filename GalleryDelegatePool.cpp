#include "GalleryDelegatePool.h"

#include <utility>

namespace ZoinGallery {

GalleryDelegateItem *GalleryDelegatePool::acquire(
    quintptr delegateKey, const Factory &factory) {
    GalleryDelegateItem *item = nullptr;
    for (GalleryDelegateItem *candidate : std::as_const(_free)) {
        if (candidate && _delegateKeys.value(candidate) == delegateKey) {
            item = candidate;
            _free.remove(candidate);
            break;
        }
    }
    if (!item && factory) {
        item = factory();
        if (item) {
            _delegateKeys.insert(item, delegateKey);
        }
    }
    if (item) {
        item->stopGeometryAnimation();
        _used.insert(item);
    }
    return item;
}

GalleryDelegateItem *GalleryDelegatePool::acquire(
    quintptr delegateKey, GalleryDelegateItem *prepared, const Factory &factory) {
    if (!prepared) {
        return acquire(delegateKey, factory);
    }
    prepared->stopGeometryAnimation();
    _free.remove(prepared);
    _delegateKeys.insert(prepared, delegateKey);
    _used.insert(prepared);
    return prepared;
}

void GalleryDelegatePool::adopt(GalleryDelegateItem *item) {
    if (!item) {
        return;
    }
    _free.remove(item);
    _used.insert(item);
}

GalleryDelegateItem *GalleryDelegatePool::findFree(
    quintptr delegateKey, const QString &entryId) const {
    if (entryId.isEmpty()) {
        return nullptr;
    }
    for (GalleryDelegateItem *item : _free) {
        if (_delegateKeys.value(item) == delegateKey && item->entryId() == entryId) {
            return item;
        }
    }
    return nullptr;
}

void GalleryDelegatePool::release(GalleryDelegateItem *item) {
    if (!item) {
        return;
    }
    item->stopGeometryAnimation();
    item->setRetainSceneWhenHidden(false);
    _used.remove(item);
    _free.insert(item);
}

void GalleryDelegatePool::resetTracking(
    const QSet<GalleryDelegateItem *> &retained) {
    for (GalleryDelegateItem *item : std::as_const(_used)) {
        if (!retained.contains(item)) {
            _free.insert(item);
        }
    }
    _used.clear();
}

void GalleryDelegatePool::trimSurplus(int spareLimit, quintptr delegateKey) {
    const QSet<GalleryDelegateItem *> surplus = _free;
    int retained = 0;
    for (GalleryDelegateItem *item : surplus) {
        if (!item) {
            continue;
        }
        if (retained < spareLimit && _delegateKeys.value(item) == delegateKey) {
            ++retained;
            continue;
        }
        _free.remove(item);
        item->setVisible(false);
        item->setParentItem(nullptr);
        _delegateKeys.remove(item);
        delete item;
    }
}

const QSet<GalleryDelegateItem *> &GalleryDelegatePool::usedItems() const {
    return _used;
}

quintptr GalleryDelegatePool::delegateKey(GalleryDelegateItem *item) const {
    return _delegateKeys.value(item);
}

int GalleryDelegatePool::liveCount() const {
    return _used.size();
}

int GalleryDelegatePool::transientFreeCount() const {
    return _free.size();
}

} // namespace ZoinGallery
