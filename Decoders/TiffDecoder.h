#ifndef TIFFDECODER_H
#define TIFFDECODER_H

#include "ImageDecoder.h"

class TiffDecoder : public ImageDecoder {
public:
    QStringList supportedFormats() override;

    bool canDecode(const QString& mimeType) override;
    QImage decode(const QByteArray& data, QSize targetSize) override;
};

#endif // TIFFDECODER_H
