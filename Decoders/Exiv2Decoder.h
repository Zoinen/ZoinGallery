#ifndef EXIV2DECODER_H
#define EXIV2DECODER_H

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

namespace Exiv2 {
class Image;
}

class Exiv2Decoder : public ImageDecoderInterface {
    // REGISTER_DECODER_DECLARATION(Exiv2Decoder, -1)

public:
    Exiv2Decoder();
    QStringList supportedFormats() override;

    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData &result) override;
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override { return QImage(); }

private:
    static bool _initCalled;
    static ExifOrientation readOrientationFromExif(Exiv2::Image *image);
    static QSize readResolutionFromExif(Exiv2::Image *image);
    static QVariantMap readExifToMap(Exiv2::Image *image);

    static QString convertEXIFToDD(const QString& exifLat, const QString &exifLon);
};

#endif // EXIV2DECODER_H
