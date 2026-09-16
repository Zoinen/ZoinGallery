#include "DirectoryPreviewCache.h"

#include <algorithm>

namespace ZoinGallery {
DirectoryPreviewCache::DirectoryPreviewCache(qint64 budget)
    : _budget(qMax(qint64(0), budget)) {}

QHash<QString, DirectoryPreviewState> DirectoryPreviewCache::states(const QStringList &keys) {
    QHash<QString, DirectoryPreviewState> result;
    result.reserve(keys.size());
    for (const auto &key : keys) {
        auto it = _entries.find(key);
        if (it == _entries.end()) continue;
        it->touched = ++_clock;
        result.insert(key, it->snapshot.state);
    }
    return result;
}

DirectoryPreviewSnapshot DirectoryPreviewCache::snapshot(const QString &key) {
    auto it = _entries.find(key);
    if (it == _entries.end()) return {};
    it->touched = ++_clock;
    return it->snapshot;
}

void DirectoryPreviewCache::store(const QString &key, DirectoryPreviewSnapshot snapshot) {
    if (key.isEmpty() || snapshot.state == DirectoryPreviewState::Unknown) return;
    // Charge containers and UTF-16 storage conservatively, including hash
    // nodes and allocation overhead. Oversized entries cannot evict the cache.
    snapshot.children = snapshot.children.mid(0, 16);
    qint64 bytes = 256 + key.size() * 2;
    for (const auto &child : snapshot.children)
        bytes += 256 + 2 * (child.name.size() + child.sourceIdentity.size()
            + child.contentVersion.size() + child.thumbnailTransformKey.size());
    if (bytes > _budget) return;
    auto old = _entries.find(key);
    if (old != _entries.end()) {
        _bytes -= old->bytes;
        _entries.erase(old);
    }
    while (_bytes + bytes > _budget && !_entries.isEmpty()) {
        auto oldest = _entries.begin();
        for (auto it = _entries.begin(); it != _entries.end(); ++it)
            if (it->touched < oldest->touched) oldest = it;
        _bytes -= oldest->bytes;
        _entries.erase(oldest);
    }
    _entries.insert(key, {std::move(snapshot), bytes, ++_clock});
    _bytes += bytes;
}

void DirectoryPreviewCache::clear() { _entries.clear(); _bytes = 0; }
}
