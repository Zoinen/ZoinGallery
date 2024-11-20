#ifndef TIFFDECODER_H
#define TIFFDECODER_H

#include "ImageDecoderInterface.h"

class TiffDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(TiffDecoder, 0)

public:
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override { return false; }
    bool readPreviewAndMime(ImageData &result) override { return false; }
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override;
};

#endif // TIFFDECODER_H
