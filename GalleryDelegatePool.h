#ifndef GALLERYDELEGATEPOOL_H
#define GALLERYDELEGATEPOOL_H

#include "GalleryDelegateItem.h"

#include <QHash>
#include <QSet>

#include <functional>

namespace ZoinGallery {

// Owns only live viewport slots and a transient free set used inside one
// materialization transaction. trimSurplus() destroys every unclaimed slot;
// this is deliberately not a panel, folder, row, or presentation cache.
class GalleryDelegatePool final {
public:
    using Factory = std::function<GalleryDelegateItem *()>;

    [[nodiscard]] GalleryDelegateItem *acquire(
        quintptr delegateKey, const Factory &factory);
    void adopt(GalleryDelegateItem *item);
    void release(GalleryDelegateItem *item);
    void resetTracking(const QSet<GalleryDelegateItem *> &retained);
    void trimSurplus();

    [[nodiscard]] const QSet<GalleryDelegateItem *> &usedItems() const;
    [[nodiscard]] int liveCount() const;
    [[nodiscard]] int transientFreeCount() const;

private:
    QSet<GalleryDelegateItem *> _used;
    QSet<GalleryDelegateItem *> _free;
    QHash<GalleryDelegateItem *, quintptr> _delegateKeys;
};

} // namespace ZoinGallery

#endif // GALLERYDELEGATEPOOL_H
