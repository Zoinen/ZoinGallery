#ifndef QTIMAGEDECODER_H
#define QTIMAGEDECODER_H

#include "ImageDecoderInterface.h"

class QtDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(QtDecoder, -100)

public:
    QtDecoder() = default;
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override { return false; }
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override;

private:
    static const QStringList &formats();
};

#endif // QTIMAGEDECODER_H
