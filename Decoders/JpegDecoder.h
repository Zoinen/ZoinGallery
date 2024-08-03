#ifndef JPEGDECODER_H
#define JPEGDECODER_H

#include "ImageDecoder.h"

class JpegDecoder : public ImageDecoder {
public:
    QStringList supportedFormats() override;

    bool canDecode(const QString& mimeType) override;
    QImage decode(const QByteArray& data, QSize targetSize) override;
};

#endif // JPEGDECODER_H
