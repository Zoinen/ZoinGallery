#ifndef PNGMETADATAREADER_H
#define PNGMETADATAREADER_H

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

class PngDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(PngDecoder, 0)

public:
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override;
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override { return QImage(); }
};

#endif // PNGMETADATAREADER_H
