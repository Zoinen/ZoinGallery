#ifndef GALLERYQUICKSEARCHINDEX_H
#define GALLERYQUICKSEARCHINDEX_H

#include <QPointer>
#include <QString>
#include <QVector>

namespace ZoinGallery {

class GalleryCatalogModel;

struct GalleryQuickSearchMatch {
    int index = -1;
    int utf16Start = -1;
    int utf16Length = 0;
};

// Catalog-name index owned by GalleryPanelController. Names are read once per
// catalog revision. Extending a query filters the preceding candidate set;
// next/previous navigation is a binary search over sorted matching rows.
class GalleryQuickSearchIndex final {
public:
    void setModel(GalleryCatalogModel *model);
    void invalidate();

    [[nodiscard]] bool setQuery(const QString &query);
    [[nodiscard]] const QString &query() const;
    [[nodiscard]] int matchCount() const;
    [[nodiscard]] GalleryQuickSearchMatch matchAt(int index) const;
    [[nodiscard]] int nextMatch(int currentIndex, bool forward,
                                bool forceMove, bool wrap = true) const;

    [[nodiscard]] int indexedRowCount() const;
    [[nodiscard]] int lastVisitedRowCount() const;

private:
    void ensureIndex();

    QPointer<GalleryCatalogModel> _model;
    QVector<QString> _names;
    QVector<QString> _foldedNames;
    QVector<int> _matches;
    QString _query;
    QString _foldedQuery;
    bool _indexed = false;
    int _lastVisitedRows = 0;
};

} // namespace ZoinGallery

#endif // GALLERYQUICKSEARCHINDEX_H
