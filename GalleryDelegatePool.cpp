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

void GalleryDelegatePool::adopt(GalleryDelegateItem *item) {
    if (!item) {
        return;
    }
    _free.remove(item);
    _used.insert(item);
}

void GalleryDelegatePool::release(GalleryDelegateItem *item) {
    if (!item) {
        return;
    }
    item->stopGeometryAnimation();
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

void GalleryDelegatePool::trimSurplus() {
    const QSet<GalleryDelegateItem *> surplus = std::exchange(_free, {});
    for (GalleryDelegateItem *item : surplus) {
        if (!item) {
            continue;
        }
        item->setVisible(false);
        item->setParentItem(nullptr);
        _delegateKeys.remove(item);
        delete item;
    }
}

const QSet<GalleryDelegateItem *> &GalleryDelegatePool::usedItems() const {
    return _used;
}

int GalleryDelegatePool::liveCount() const {
    return _used.size();
}

int GalleryDelegatePool::transientFreeCount() const {
    return _free.size();
}

} // namespace ZoinGallery
