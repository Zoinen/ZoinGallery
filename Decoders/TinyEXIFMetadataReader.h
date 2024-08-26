#ifndef TINYEXIFMETADATAREADER_H
#define TINYEXIFMETADATAREADER_H

#include "MetadataReader.h"
#include "ImageFile.h"

namespace TinyEXIF{
class EXIFInfo;
}

class TinyEXIFMetadataReader : public MetadataReader {
public:
    static void init(); // No equivalent in TinyEXIF, but kept for compatibility
    bool readMetadata(ImageInfo& result) override;
    bool isFormatSupported(const QString &path) override;

    bool readPreviewAndMime(ImageData &result) override;

private:
    static ExifOrientation readOrientationFromExif(const TinyEXIF::EXIFInfo &exifInfo);
    static QSize readResolutionFromExif(const TinyEXIF::EXIFInfo &exifInfo);
    static QVariantMap readExifToMap(const TinyEXIF::EXIFInfo &exifInfo);
};

#endif // TINYEXIFMETADATAREADER_H
