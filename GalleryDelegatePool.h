#ifndef GALLERYDELEGATEPOOL_H
#define GALLERYDELEGATEPOOL_H

#include "GalleryDelegateItem.h"

#include <QHash>
#include <QSet>

#include <functional>

namespace ZoinGallery {

// Reuses anonymous viewport slots. Callers may retain a bounded spare set and
// control retired-row presentation.
class GalleryDelegatePool final {
public:
    using Factory = std::function<GalleryDelegateItem *()>;

    [[nodiscard]] GalleryDelegateItem *acquire(
        quintptr delegateKey, const Factory &factory);
    // prepared is an unclaimed row constructed from this delegateKey.
    // Prefer it to rebinding a generic free slot; null uses normal acquisition.
    [[nodiscard]] GalleryDelegateItem *acquire(
        quintptr delegateKey, GalleryDelegateItem *prepared,
        const Factory &factory);
    [[nodiscard]] GalleryDelegateItem *findFree(
        quintptr delegateKey, const QString &entryId) const;
    void adopt(GalleryDelegateItem *item);
    void release(GalleryDelegateItem *item);
    void resetTracking(const QSet<GalleryDelegateItem *> &retained);
    void trimSurplus(int spareLimit = 0, quintptr delegateKey = 0);

    [[nodiscard]] const QSet<GalleryDelegateItem *> &usedItems() const;
    [[nodiscard]] quintptr delegateKey(GalleryDelegateItem *item) const;
    [[nodiscard]] int liveCount() const;
    [[nodiscard]] int transientFreeCount() const;

private:
    QSet<GalleryDelegateItem *> _used;
    QSet<GalleryDelegateItem *> _free;
    QHash<GalleryDelegateItem *, quintptr> _delegateKeys;
};

} // namespace ZoinGallery

#endif // GALLERYDELEGATEPOOL_H
