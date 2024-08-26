#ifndef LIBRAWMETADATAREADER_H
#define LIBRAWMETADATAREADER_H

#include "MetadataReader.h"
#include "ImageFile.h"

class LibRaw;

class LibRawMetadataReader : public MetadataReader {
public:
    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override;
    bool isFormatSupported(const QString &path) override;

private:
    ExifOrientation readOrientationFromExif(LibRaw &rawProcessor);
    QVariantMap readExifToMap(LibRaw &rawProcessor);
};


#endif // LIBRAWMETADATAREADER_H
