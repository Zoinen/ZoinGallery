#ifndef JPEGDECODER_H
#define JPEGDECODER_H

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

namespace TinyEXIF{
class EXIFInfo;
}

class JpegDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(JpegDecoder, 2)

public:
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override { return false; }
    QImage decode(const QString &mimeType, const QByteArray& data, QSize targetSize) override;

private:
    static ExifOrientation readOrientationFromExif(const TinyEXIF::EXIFInfo &exifInfo);
    static QSize readResolutionFromExif(const TinyEXIF::EXIFInfo &exifInfo);
    static QVariantMap readExifToMap(const TinyEXIF::EXIFInfo &exifInfo);
};

#endif // JPEGDECODER_H
