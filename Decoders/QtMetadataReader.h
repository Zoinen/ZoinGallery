#ifndef QTMETADATAREADER_H
#define QTMETADATAREADER_H

#include "MetadataReader.h"

class QtMetadataReader : public MetadataReader {
public:
    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override;
    bool isFormatSupported(const QString &path) override;
};

#endif // QTMETADATAREADER_H
