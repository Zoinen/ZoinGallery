#pragma once

#include <QHash>
#include <QList>
#include <QSize>
#include <QStringList>

namespace ZoinGallery {

enum class DirectoryPreviewState { Unknown, Empty, HasImages };

// Presentation data only. No byte sources, local paths, read authority or
// leases survive here. Pixel allocations belong to ThumbnailMemoryCache.
struct DirectoryPreviewSnapshot {
    struct Child {
        QString name;
        QString sourceIdentity;
        QString contentVersion;
        qint64 size = -1;
        qint64 mtimeNs = 0;
        QSize originalSize;
        QSize thumbnailSize;
        QString thumbnailTransformKey;
        QString thumbnailKind;
    };
    DirectoryPreviewState state = DirectoryPreviewState::Unknown;
    QList<Child> children;
};

// Owned by GalleryRuntime and accessed only on its GUI thread. Catalog
// publication performs one RAM-only lookup pass, without worker admission.
class DirectoryPreviewCache final {
public:
    static constexpr qint64 DefaultByteBudget = 8LL * 1024 * 1024;
    explicit DirectoryPreviewCache(qint64 budget = DefaultByteBudget);
    QHash<QString, DirectoryPreviewState> states(const QStringList &keys);
    DirectoryPreviewSnapshot snapshot(const QString &key);
    void store(const QString &key, DirectoryPreviewSnapshot snapshot);
    void clear();
    qint64 retainedBytes() const { return _bytes; }
    qint64 byteBudget() const { return _budget; }
    qsizetype count() const { return _entries.size(); }

private:
    struct Entry {
        DirectoryPreviewSnapshot snapshot;
        qint64 bytes = 0;
        quint64 touched = 0;
    };
    QHash<QString, Entry> _entries;
    qint64 _budget;
    qint64 _bytes = 0;
    quint64 _clock = 0;
};
}
