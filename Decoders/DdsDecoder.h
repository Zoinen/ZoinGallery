#ifndef DDSDECODER_H
#define DDSDECODER_H

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

class DdsDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(DdsDecoder, 1)

public:
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override { return false; }
    QImage decode(const QString &mimeType, const QByteArray& data, QSize targetSize) override;

private:
    static QImage decodeDds(const QByteArray& data, QSize targetSize);
};

#endif // DDSDECODER_H 