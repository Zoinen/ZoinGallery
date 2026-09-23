#ifndef ZOINGALLERY_GALLERYGROUPINDEX_H
#define ZOINGALLERY_GALLERYGROUPINDEX_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace ZoinGallery {

// A group range is expressed in source-catalog coordinates.  It never adds a
// row to GalleryCatalogModel, which keeps stable entry ids and sparse paging
// independent from section presentation.
struct GalleryGroupDescriptor {
    QString key;
    QString title;
    int startIndex = 0;
    int count = 0;
};

// Shared, presentation-neutral section index. MasonryLayout supplies section
// geometry after it has selected a presentation strategy; this class owns the
// O(log G) source-index mapping and the compact collapsed/prefix state.
class GalleryGroupIndex final {
public:
    bool setDescriptors(const QVariantList &groups, int catalogCount = -1);
    bool setDescriptors(const QVector<GalleryGroupDescriptor> &groups,
                        int catalogCount = -1);
    void clear();

    [[nodiscard]] bool active() const;
    [[nodiscard]] int groupCount() const;
    [[nodiscard]] int catalogCount() const;
    [[nodiscard]] const GalleryGroupDescriptor *descriptor(int group) const;
    [[nodiscard]] int groupForSourceIndex(int sourceIndex) const;
    [[nodiscard]] int localIndexForSourceIndex(int sourceIndex) const;
    [[nodiscard]] int visibleOrdinalForSourceIndex(int sourceIndex) const;
    [[nodiscard]] int sourceIndexForVisibleOrdinal(int visibleOrdinal) const;
    [[nodiscard]] QVector<int> visibleSourceIndexesInRange(int first,
                                                           int last) const;
    [[nodiscard]] int nearestVisibleSourceIndex(int sourceIndex,
                                                bool forward) const;
    [[nodiscard]] int visibleFileCountBeforeGroup(int group) const;
    [[nodiscard]] int visibleFileCount() const;
    [[nodiscard]] bool isCollapsed(int group) const;
    [[nodiscard]] bool isCollapsed(const QString &key) const;
    bool setCollapsed(int group, bool collapsed);
    bool setCollapsed(const QString &key, bool collapsed);
    bool toggleCollapsed(const QString &key);
    void setCollapsedKeys(const QSet<QString> &keys);
    [[nodiscard]] QSet<QString> collapsedKeys() const;

    // Offsets/extents are content-space values supplied by a renderer. They
    // are kept here so header materialization can use the same section index
    // without scanning the file catalog.
    void setSectionGeometry(const QVector<qreal> &offsets,
                            const QVector<qreal> &extents,
                            qreal headerExtent);
    [[nodiscard]] QVariantList headersForRange(qreal start, qreal end,
                                               qreal overscan = 0) const;

private:
    class PrefixTree final {
    public:
        void reset(int count);
        void add(int index, int delta);
        [[nodiscard]] int sumBefore(int index) const;
        [[nodiscard]] int total() const;
        [[nodiscard]] int lowerBound(int target) const;

    private:
        QVector<int> _tree;
    };

    void rebuildPrefixes();

    QVector<GalleryGroupDescriptor> _groups;
    QVector<int> _starts;
    PrefixTree _visibleCounts;
    PrefixTree _hiddenCounts;
    int _catalogCount = 0;
    int _visibleTotal = 0;
    int _leadingCount = 0;
    int _trailingCount = 0;
    QHash<QString, int> _groupIndexByKey;
    QSet<QString> _collapsedKeys;
    QVector<qreal> _sectionOffsets;
    QVector<qreal> _sectionExtents;
    qreal _headerExtent = 0;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYGROUPINDEX_H
