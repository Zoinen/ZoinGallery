#ifndef RAWDECODER_H
#define RAWDECODER_H

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

class LibRaw;

class RawDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(RawDecoder, 3)

public:
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override;
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override { return QImage(); }

private:
    ExifOrientation readOrientationFromExif(LibRaw &rawProcessor);
    QVariantMap readExifToMap(LibRaw &rawProcessor);
};


#endif // RAWDECODER_H
