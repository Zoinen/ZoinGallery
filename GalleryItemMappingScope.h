#pragma once

#include <QQuickItem>
#include <QtQuick/private/qquickitem_p.h>
#include <array>

// Borrowed mappings for one GUI-locked synchronization pass. The caller must
// not change item geometry, transforms, parents or windows during this scope.
class GalleryItemMappingScope {
public:
    explicit GalleryItemMappingScope(QQuickItem *target) : _target(target) {}
    GalleryItemMappingScope(const GalleryItemMappingScope &) = delete;
    GalleryItemMappingScope &operator=(const GalleryItemMappingScope &) = delete;

    QRectF map(QQuickItem *item, const QRectF &rect) {
        auto *parent = item->parentItem();
        // A leaf's custom transform can introduce Z before a 3D ancestor.
        // Projected 2D matrices cannot safely compose that case.
        if (!_target || !parent || item == _target
            || item->window() != _target->window()
            || item->rotation() != 0 || item->scale() != 1
            || !QQuickItemPrivate::get(item)->transforms.isEmpty())
            return fullMap(item, rect);

        const auto translated = rect.translated(item->x(), item->y());
        if (parent == _target) {
            ++_fastMaps;
            return translated;
        }
        unsigned index = 0;
        while (index < _count && _entries[index].parent != parent)
            ++index;
        if (index == _count) {
            if (_count == _entries.size())
                return fullMap(item, rect);
            bool ok = false;
            const auto transform = parent->itemTransform(_target, &ok);
            ++_fullMaps;
            if (!ok)
                return fullMap(item, rect);
            _entries[_count++] = {parent, transform};
        }
        ++_fastMaps;
        return _entries[index].transform.mapRect(translated);
    }

    quint64 fullMaps() const { return _fullMaps; }
    quint64 fastMaps() const { return _fastMaps; }

private:
    struct Entry {
        QQuickItem *parent = nullptr;
        QTransform transform;
    };
    QRectF fullMap(QQuickItem *item, const QRectF &rect) {
        ++_fullMaps;
        return item->mapRectToItem(_target, rect);
    }
    std::array<Entry, 32> _entries;
    unsigned _count = 0;
    QQuickItem *_target;
    quint64 _fullMaps = 0;
    quint64 _fastMaps = 0;
};
