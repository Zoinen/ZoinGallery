#ifndef ZOINGALLERY_GALLERYCATALOGSOURCE_H
#define ZOINGALLERY_GALLERYCATALOGSOURCE_H

#include <QList>

class ImageFile;
struct ImageDecodeRequest;

namespace ZoinGallery {

// Data-source operations needed by a virtualized gallery viewport. Catalog
// storage and presentation stay independent: proxy/filter models forward
// these requests to their source while preserving their own row mapping.
class GalleryCatalogSource {
public:
    virtual ~GalleryCatalogSource() = default;

    virtual void decodeImages(
        const QList<ImageDecodeRequest> &requests) = 0;
    virtual void requestImageMetadata(const QList<int> &rows,
                                      bool highPriority,
                                      bool catalogWide = false) {
        Q_UNUSED(rows)
        Q_UNUSED(highPriority)
        Q_UNUSED(catalogWide)
    }
    virtual void cancelAllRunners() = 0;
    virtual void cancelAllDecodeRunners() = 0;
    virtual bool preserveViewStateOnReset() const { return false; }
    virtual ::ImageFile *rootItem() const { return nullptr; }
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYCATALOGSOURCE_H
