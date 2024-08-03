#ifndef METADATAREADER_H
#define METADATAREADER_H

#include <QString>

class ImageInfo;

class MetadataReader {
public:
    virtual ~MetadataReader() = default;
    virtual bool readMetadata(ImageInfo& result) = 0;
    virtual bool isFormatSupported(const QString &path) = 0;
};

#endif // METADATAREADER_H
