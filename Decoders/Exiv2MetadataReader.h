#ifndef EXIV2METADATAREADER_H
#define EXIV2METADATAREADER_H

#include "MetadataReader.h"
#include "ImageFile.h"

namespace Exiv2 {
class Image;
}

class Exiv2MetadataReader : public MetadataReader {
public:
    static void init();
    bool readMetadata(ImageInfo& result) override;
    bool isFormatSupported(const QString &path) override;

    bool readPreviewAndMime(ImageData &result);

private:
    static ExifOrientation readOrientationFromExif(Exiv2::Image *image);
    static QSize readResolutionFromExif(Exiv2::Image *image);
    static QVariantMap readExifToMap(Exiv2::Image *image);

    static void fixMimeType(QString &mimeToUpdate, const QString &filePath);
};

#endif // EXIV2METADATAREADER_H
