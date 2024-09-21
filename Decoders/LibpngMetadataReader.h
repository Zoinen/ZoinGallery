#ifndef PNGMETADATAREADER_H
#define PNGMETADATAREADER_H

#include "MetadataReader.h"
#include "ImageFile.h"

class LibpngMetadataReader : public MetadataReader {
public:
    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override;
    bool isFormatSupported(const QString &path) override;
};

#endif // PNGMETADATAREADER_H
