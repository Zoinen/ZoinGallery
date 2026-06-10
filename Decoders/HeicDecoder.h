#pragma once

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

class HeicDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(HeicDecoder, 2)
public:
    QStringList supportedFormats() override;
    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData& result) override;
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override;
};
